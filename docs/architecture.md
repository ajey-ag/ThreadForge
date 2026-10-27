# ThreadForge architecture

The pool owns persistent workers and shared queue state containing the scheduler,
lifecycle, condition variable, mutex and metrics. Task handles hold weak references
to that state and independent atomic task state. Futures own result state separately.

## Submission and ownership

The template API decay-copies/moves a callable and arguments into a one-shot
invocation. A promise supplies a typed future. A copyable queue wrapper owns the
invocation through shared ownership, allowing move-only inputs in C++17. Rejected
admission throws synchronously and destroys resources after leaving the queue lock.
Accepted admission updates the queue and counters together under that lock.

The scheduler provides FIFO, stable three-level priority and experimental
per-worker-deque work stealing. It has no internal mutex: the pool supplies all
synchronization. Capacity is global waiting-task count, excluding running work.

## Workers

Construction validates inputs and starts workers. On partial thread-creation
failure, created workers are closed and joined before rethrowing. Each worker
waits for a nonempty queue or lifecycle change, dequeues under the mutex, publishes
Running, then unlocks before invocation. It publishes result/error and terminal
state, accounts for completion under the mutex, then destroys captured resources
outside the lock. Threads persist across tasks and never detach.

User exceptions become future exceptions and Failed state; workers continue.
Future readiness may slightly precede terminal state/accounting. Shutdown provides
fully settled final accounting.

## Cancellation

Queued -> Running and Queued -> Cancelled compete under the same mutex. Handles
remove queued work immediately, free capacity, and resolve TaskCancelled outside
the lock without invoking user code. The cancellation exception is allocated once
at construction. In-flight cancellation resolution is tracked so shutdown waits
for it. Running or terminal tasks cannot be cancelled.

## Shutdown

Running -> Draining -> Stopped is monotonic. The shutdown mutex serializes joins;
the queue mutex decides admission versus closure. Graceful mode closes admission,
wakes workers, drains and joins. CancelPending removes queued work, resolves its
futures, and joins running work. Removed captures are destroyed after both locks
are released. The first caller selects the mode; later callers wait.

Thread-local worker identity rejects self-shutdown before attempting the shutdown
mutex. Destruction requires exclusive object lifetime and must occur off-pool.
Infinite user work or dependency cycles can prevent graceful completion.

## Metrics and costs

Counts and aggregate timings share the queue mutex for coherent snapshots. Timing
can be disabled; accounting locks remain. Benchmarks own per-task latency arrays;
the engine keeps bounded aggregates. Shared locks, task allocations, type erasure
and futures are explicit correctness-first costs.

See concurrency_model.md, scheduling_policies.md and benchmark_methodology.md for
invariants, ordering, fairness and measurement boundaries.
