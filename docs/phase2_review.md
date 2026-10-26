# Phase 2 — typed asynchronous tasks

`submit` returns `std::future<R>` and stores a `std::packaged_task<R()>` behind a
shared pointer so a copyable queue entry can own move-only user code. Inputs are
decay-copied (or moved from rvalues), then invoked once as rvalues. Use `std::ref`
for explicit reference arguments; the referenced object must outlive the task.
Function pointers, member pointers, lambdas and callable objects use `std::apply`
and its C++17 invocation rules. Empty `std::function` now fails asynchronously
with `std::bad_function_call`, like other invocation errors.

Exceptions increment the failure counter then propagate into the packaged task's
shared state. `future.get()` retrieves either a value or the original exception.
Admission errors still throw synchronously without returning a future. A future
may outlive the pool; a pending task may not outlive it because destruction joins.

C++ concepts: forwarding references, decay, result deduction, tuples, move-only
ownership, type erasure, futures. OS concepts: asynchronous execution, blocking
result waits, thread synchronization. Do not wait for a child task from an
exhausted pool: futures do not solve dependency deadlocks.

Trade-offs: shared allocation makes move-only tasks easy to queue, but adds
allocation/reference-count cost. Moving stored arguments enables unique ownership
but requires `std::ref` for lvalue-only APIs. Keeping exception details in futures
requires callers to observe their futures.

Interview questions:

1. Why cannot a C++17 std::function directly own a move-only lambda?
2. How does packaged_task transport exceptions across threads?
3. What distinguishes a synchronous admission error from a future exception?
4. Why are reference captures a lifetime obligation for the caller?
5. How can waiting on a future inside a worker deadlock a pool?

Checklist: typed results, void tasks, exception propagation, function pointers,
move-only arguments/captures, and explicit borrowing implemented and tested.
Priority, capacity, cancellation, metrics and benchmarks remain for later phases.
