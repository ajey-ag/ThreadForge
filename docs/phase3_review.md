# Phase 3 — scheduling and backpressure

The scheduler owns three FIFO buckets. FIFO mode uses one bucket regardless of
priority; priority mode checks High, Normal, Low in that order. Queue operations
remain under the pool mutex. Equal-priority admission order is deterministic;
execution/completion order is not. Running tasks are never preempted.

Capacity counts waiting entries, excluding running tasks; zero means unbounded.
Full queues throw QueueFull synchronously; closed pools throw PoolClosed. Neither
returns a future or accepts work. This reject policy avoids blocking submission
deadlocks but requires application retry, shedding, or upstream backpressure.

Graceful shutdown remains the default. Cancellation policies follow in Phase 4.
Strict priority can starve low work. A future fairness policy could use weighted
round robin (for example 4 High, 2 Normal, 1 Low) or promote tasks after a waiting
threshold. Neither policy is currently implemented; there are no real-time claims.

C++ concepts: enum classes, container ownership, exception types and composition.
OS concepts: admission control, nonpreemptive scheduling, starvation. Trade-offs:
constant-time bucket selection and FIFO stability versus strict-priority fairness;
bounded waiting memory versus explicit admission failures.

Interview questions:

1. Why is stable dequeue order different from start/completion order?
2. Why does queue capacity exclude running tasks?
3. How can blocking submission deadlock a worker pool?
4. How would aging affect the scheduler's data structure and costs?
5. Where do overload rejection and shutdown linearize?

Checklist: FIFO/priority policies, stable equal priorities, configurable capacity,
explicit rejection, and graceful lifecycle implemented. Cancellation and metrics
remain. A gated worker test verifies backlog order and queue exhaustion.
