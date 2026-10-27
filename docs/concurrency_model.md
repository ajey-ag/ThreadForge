# Concurrency invariants and lifetime

1. Every scheduler access, admission decision, capacity check and count snapshot
   holds the queue-state mutex. The scheduler is not independently thread-safe.
2. An accepted task has exactly one queue owner until removed by one worker or
   cancellation operation. Removal and the Queued transition share the mutex.
3. Only the removal winner calls the promise-resolving wrapper: it invokes user
   code once or resolves cancellation without invoking user code.
4. At every snapshot: submitted = queue_depth + running + completed + failed +
   cancelled. Rejections are not submissions; completed means successful.
5. Invocation and captured-resource destruction occur outside queue locks.
   Shutdown-cancelled captures also outlive the shutdown lock before destruction.
6. Nested lock order is shutdown mutex -> queue mutex. Workers and cancellation
   take only the queue mutex. Joining never holds the queue mutex.

Waits use `lifecycle != Running || !tasks.empty()`. Submission publishes under the
same mutex before notify_one; shutdown publishes before notify_all. The predicate
persists if notification precedes waiting and is rechecked after spurious wakeups.
With stealing, any awakened worker can scan every deque under the mutex, so
notify_one does not need to target an owner.

Task state uses atomic release stores/acquire loads for handle queries. It does
not replace the mutex for ownership transitions. Promise readiness synchronizes
result reads; join synchronizes full worker completion. A reference result still
requires valid referenced lifetime.

Calls, including handle cancellation, can overlap while the pool lives; destruction
must not overlap them. Surviving handles can inspect terminal state and return
false from cancel after the pool expires. Shared internal state does not authorize
access to a destroyed ThreadPool object.

There is no work-helping while waiting on futures. An exhausted pool whose workers
wait for children can deadlock even with stealing. Cross-pool dependency cycles,
indefinite user locks and infinite task production remain application concerns.

Tests exercise barriers, multiple producers, cancellation/shutdown races and long
batches. Sanitizers inspect executed interleavings, not all schedules. Partial
thread-creation cleanup is reviewed, not deterministically fault-injected.
