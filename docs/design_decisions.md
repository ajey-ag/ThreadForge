# Design decisions and limitations

- Standard C++17 keeps synchronization visible and dependencies small.
- One queue-state mutex simplifies cancellation and coherent snapshots, but
  limits tiny-task throughput, including under experimental work stealing.
- Phase 2 used packaged_task; Phase 4 uses promises for explicit TaskCancelled
  completion. One queue-removal winner prevents double completion.
- Decay-own and invoke-once semantics support move-only inputs. References remain
  the caller's lifetime responsibility.
- Bounded overload rejects instead of blocking workers. Applications select
  retry/shedding/upstream throttling; zero capacity means unbounded.
- Stable priority buckets are simple and cheap but do not ensure fairness.
- Queued-only cancellation frees capacity immediately with an O(n) search.
  Its error payload is preallocated; running code is never forcibly terminated.
- First shutdown mode wins, avoiding concurrent escalation complexity. Infinite
  user tasks can prevent graceful shutdown indefinitely.
- Aggregate metrics bound memory; timing-off still pays count/accounting costs.
- Serialized work stealing compares placement/ordering within the existing
  correctness model; it is not presented as a lock-contention solution.

Not implemented: aging, dependency graphs, running-task cancellation tokens,
resizing, affinity, deadlines, lock-free queues, allocation pools, GPU execution
or native Windows validation. No performance or real-time guarantee is implied.
