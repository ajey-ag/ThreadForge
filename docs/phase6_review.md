# Phase 6 — experimental work stealing

Implemented after the shared-queue baseline passed tests and produced 195 measured
benchmark rows. Per-worker deques use local LIFO execution and cyclic FIFO stealing.
External placement is round-robin; worker submission is local. All deque access
retains the queue mutex to compare placement without introducing multiple-lock
cancellation/wakeup hazards or claiming scalable lock reduction.

C++ concepts: thread-local identity, deque ownership, policy composition and shared
invariants. OS concepts: balancing, locality, contention, wakeups and fairness.
Trade-offs: LIFO locality versus starvation, deterministic scans versus bias,
and simple global synchronization versus serialization.

The forced-steal test blocks a parent while another worker completes 32 locally
submitted children. The policy matrix tests cancellation/capacity/shutdown. Stress
executes 30,000 tasks per policy with multiple producers and task exceptions.
Benchmarks add stealing to identical workloads; measured comparisons belong in
the results report, not an assumption that stealing always wins.

Interview questions:

1. Why might owners pop newest work while thieves take oldest?
2. Why is notify_one correct even if it wakes a different worker than the owner?
3. What proof changes when replacing one mutex with per-deque locks?
4. Why does stealing not prevent every child-future deadlock?
5. Which workloads might benefit, and why can tiny tasks get slower?

Checklist: per-worker queues, stealing strategy, synchronization/wakeup contracts,
fairness limits, forced-steal tests and controlled comparisons implemented.
Per-deque locks, randomized stealing and dependency-aware helping remain extensions.
