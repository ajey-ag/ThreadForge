# Phase 5 — metrics and reproducible measurements

Metrics share the scheduler mutex, yielding a consistent accounting snapshot:
submitted = queued + running + completed + failed + cancelled. Completed counts
only successes. Rejected admissions never increment submitted. Peak depth counts
waiting tasks only. Timing averages are optional when no samples exist or timing
is disabled. Counts remain enabled. Cancellation state/count can be visible just
before its future is resolved; shutdown waits for resolution.

C++ concepts: chrono durations, optional values, coherent snapshots, futures as
completion fences. OS concepts: contention, scheduling overhead, oversubscription,
blocking versus CPU-bound work, virtualized timing variability.

Trade-offs: shared-lock accounting is easy to audit but adds contention. Timestamp
sampling adds overhead, now measurable with the on/off benchmark. Per-task latency
storage belongs to the benchmark rather than the long-lived engine so metrics do
not grow without bound. No speculative optimization was applied.

Interview questions:

1. Why must steady_clock rather than wall-clock time measure durations?
2. Why can a pool's lifetime throughput differ from benchmark throughput?
3. What biases arise from always running configurations in the same order?
4. Why do tiny tasks often become slower with more workers?
5. Which timing overhead remains in the timing-disabled benchmark?

Checklist: counts, queue depth/peak, wait/execution/latency averages, throughput,
timing toggle, serial and worker sweeps, warm-ups, repeated measurements,
percentiles, scaling efficiency and metadata capture implemented. Tests validate
accounting and no-sample behavior. Actual results and sanitizer outcomes belong
in validation.md; optional work stealing is considered only after this baseline.
