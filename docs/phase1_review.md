# Phase 1 learning and checklist

## Concepts

The architecture is a shared producer-consumer queue: many callers produce work,
fixed workers consume it, and graceful shutdown closes production before draining.
C++ concepts include RAII, deleted copy/move operations for synchronization owners,
`std::function` type erasure, move transfer, exception-safe construction, thread
local storage, mutex guards, and relaxed atomics for independent counters.

OS concepts include persistent threads, blocking waits instead of busy loops,
OS-dependent execution order, context switching, and contention on shared state.
The queue mutex prevents duplicate dequeue and state races; the wait predicate
prevents lost/spurious wakeup bugs; unlocking before execution avoids locking
around arbitrary user code; joining establishes completion and lifetime safety.

## Five interview questions

1. Why must a condition-variable predicate be checked while holding the mutex,
   and why is notification alone insufficient?
2. Where do submission and shutdown linearize, and what happens when they race?
3. Why can FIFO dequeue coexist with out-of-order task completion?
4. Why must a constructor explicitly join workers when later thread creation fails?
5. Why is relaxed ordering sufficient for the failure count, but insufficient as
   a general mechanism for publishing task output?

## Trade-offs to understand

- One shared queue simplifies correctness but creates a contention point.
- An unbounded queue simplifies admission but does not control memory pressure.
- Copyable type-erased tasks simplify the API but exclude move-only captures.
- Exception containment keeps workers alive but loses error details in Phase 1.
- Graceful shutdown preserves accepted work but has no bounded completion time.
- Serialized joins support concurrent shutdown at the cost of a second mutex.

## Checklist

- [x] C++17, CMake/CTest, standard-library-only implementation, strict warnings.
- [x] Fixed reusable workers, FIFO submission, predicate waits, task execution outside locks.
- [x] Zero-worker rejection, partial-creation cleanup, explicit lifetime contract.
- [x] Graceful shutdown, submission rejection after closure, concurrent join protection.
- [x] Exactly-once 100-task example; deterministic coordination and 10,000-task producer test.
- [x] Tests for task exceptions, worker self-shutdown, FIFO, destructor draining.
- [ ] Typed results, futures, exception propagation (Phase 2).
- [ ] Priority, capacity/backpressure, cancellation, timing metrics and benchmarks.
- [ ] Optional work stealing; deterministic thread-creation failure injection.

Build and test outcomes are recorded separately in [validation.md](validation.md).
