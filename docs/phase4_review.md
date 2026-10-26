# Phase 4 — cancellation and lifecycle

`submit_cancellable` returns a typed future and a copyable task handle. Handles
inspect Queued, Running, Completed, Failed, or Cancelled. The queue mutex decides
the Queued -> Running versus Queued -> Cancelled race. Running transitions once
to Completed/Failed. Cancellation removes an entry immediately and frees capacity;
it never invokes user code. Repeated cancellation and cancellation after dequeue
return false. There is no forced termination of running C++ code.

Promises replace the Phase 2 packaged-task wrapper so both execution and removal
can explicitly settle a future. The cancellation winner supplies TaskCancelled;
the execution winner supplies the value or original exception. A future can be
ready slightly before the worker publishes its terminal state/accounting. Use
shutdown for a fully settled snapshot. Captured arguments remain decay-owned.

The shared queue state lets a handle safely detect an expired pool. Handles retain
task state after the pool is gone. As with all pool operations, callers must not
overlap destruction with cancellation or other public calls. The pool waits for
in-flight cancellation resolution during shutdown. Queue removal is O(n).

CancelPending shutdown removes all queued tasks, resolves cancellation futures,
and joins running work. It is not an instantaneous return. The first shutdown
caller selects the mode; later calls wait for it and cannot escalate graceful
shutdown. Removed task captures are destroyed outside both internal locks.

C++ concepts: promises, weak/shared ownership, atomic state publication, explicit
exception transport. OS concepts: cooperative boundaries and nonpreemptive work.
Trade-offs: safe explicit handles and immediate capacity release cost allocations
and a linear queue search; forced cancellation is deliberately unsupported.

Interview questions:

1. Which lock decides cancellation versus worker execution?
2. Why is cancelling a running C++ thread unsafe in general?
3. Why use a weak pointer from handles to pool state?
4. Can future readiness precede the worker's terminal-state update?
5. Why must captured objects be destroyed outside scheduler locks?

Checklist: queued cancellation, cancellation exceptions, task states, safe stale
handles, capacity reuse and cancel-pending shutdown implemented. Tests cover
cancel/dequeue races, duplicate cancellation, running-task rejection and futures
settling while a running task remains blocked. Metrics/benchmarks remain.
