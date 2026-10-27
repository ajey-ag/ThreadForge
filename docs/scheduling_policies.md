# Scheduling policies

| Policy | Queue structure | Removal | Ordering/fairness |
|---|---|---|---|
| FIFO | One FIFO bucket | Oldest | Admission-order dequeue |
| Priority | Three FIFO buckets | High, Normal, Low | Stable within level; lower levels can starve |
| WorkStealing | One deque per worker | Own newest, otherwise another worker's oldest | No global FIFO or priority guarantee |

All are nonpreemptive and share the mutex, global capacity, cancellation, shutdown
and metrics contracts. FIFO and stealing ignore valid priority values; invalid
enum values throw. Task durations and OS scheduling determine completion order.

External stealing-mode submissions use round-robin placement. Submission from a
worker in the same pool targets its own deque; submission to another pool is
external. Owners pop their back (LIFO). Empty owners scan victims cyclically,
starting at the next worker, stealing the front (FIFO). The stolen metric counts
worker removals from another deque, not shutdown cancellation.

All deques remain protected by the pool mutex. This is a conservative experiment,
not a lock-free or per-deque-lock implementation. Victim search is O(workers);
cancellation is O(queued tasks). Arbitrary notified workers can steal available
tasks, preventing work from being stranded in an idle owner's queue.

Local LIFO can starve old tasks if owners keep receiving new work and no thief is
available. Cyclic scans are deterministic, not a fairness guarantee. Possible
follow-ups: rotating/random victims, periodic oldest-local work, weighted priority
service or aging. None is implemented.

Benchmarks use identical independent tasks and compare checksums; they do not
establish recursive fork/join performance. A unit test blocks a parent after local
submission so another worker must steal its children. Per-deque locks require a
new ownership, wakeup and cancellation proof, justified by measurement first.
