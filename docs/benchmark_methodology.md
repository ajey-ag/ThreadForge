# Benchmark methodology

Build Release without sanitizers. `scripts/run_benchmarks.sh` builds, runs CTest,
captures machine/compiler/OS/build metadata, and writes raw CSV. Example:

```sh
bash scripts/run_benchmarks.sh benchmarks/results/my-run --tasks 2000 --repeats 5 --iterations 20000 --max-workers 8
```

The first argument is an output prefix. Use a fresh prefix to preserve earlier
runs. `THREADFORGE_BUILD_DIR` selects an alternative build directory. Run on an
otherwise idle machine, record power settings, and repeat independently; WSL,
background work, CPU frequency and thermal changes add noise. Default worker
counts are powers of two through min(16, logical CPUs), with at least eight for
the requested comparison. Explicit max-workers can reduce oversubscription or
extend the sweep; a non-power-of-two maximum is included too.

## Workloads and controls

- CPU: deterministic unsigned integer mixing for `iterations` rounds per task.
- Short: eight mixing rounds, dominated by scheduler overhead.
- Mixed: every tenth task uses 10x iterations, others iterations/10 + 1.
- Blocking: sleep for at least 100 microseconds, then eight mixing rounds. This
  simulates waits, not actual disk/network I/O. OS sleep granularity affects it.
- Batch: 10x task count, sixteen mixing rounds per task.

Every task contributes to a checksum checked against serial execution, preventing
unused-result elimination and detecting wrong workload results. Each configuration
gets a warm-up of up to 100 tasks. Repetitions record all raw rows rather than
selecting a best run. Configuration order reverses on odd repetitions to reduce
fixed-order drift; the serial baseline runs first in each repetition.

Compare FIFO timing on/off to estimate the cost of timestamp instrumentation;
counts, futures, allocations, and benchmark latency sampling remain enabled in
both. Priority uses all Normal tasks for a controlled implementation-overhead
comparison, not a priority responsiveness or starvation experiment.
The final sweep also includes work stealing with timing enabled on the same
independent tasks. This tests round-robin placement and stealing overhead, not
recursive fork/join workloads. All policies still serialize queue access.

Generate a reproducible summary of final-format results with standard Python 3:

```sh
python3 scripts/summarize_benchmarks.py benchmarks/results/my-run.csv > benchmarks/results/my-run.summary.md
```

## Timestamp definitions

All clocks are `steady_clock`. Total elapsed includes submission, work, getting
every future and shutdown/join, but excludes pool construction, vector allocation,
sorting, checksum computation and output. All tasks finish before the timer stops.
No console/file output occurs in timed task execution.

Per-task CSV latency runs from immediately before submit (or direct serial call)
to workload completion inside the callable. It includes submission/queue delay
but excludes promise readiness, future retrieval and capture destruction. Each
task writes its own preallocated sample slot; futures synchronize later reads.
Percentiles use nearest rank on sorted individual task latencies within each run.

Pool queue wait: timestamp just before queue insertion to timestamp immediately
after dequeue/Running transition, both under the queue mutex. Execution time:
that start timestamp through future settlement/terminal-state publication, before
accounting-lock acquisition. Completion latency is their sum. Capture destruction
is excluded. Samples include successful and failed execution, exclude cancellation.
CSV means for disabled timings are blank, not zero. Serial execution has zero
queue wait and its measured callable latency as execution time.

Tasks/second = tasks / measured elapsed. Scaling efficiency = serial elapsed /
pool elapsed / workers, with the corresponding same-repetition serial baseline.
Efficiency is a fraction, not a percentage. Pool runtime throughput instead uses
completed + failed tasks divided by lifetime since construction, frozen at shutdown;
it is intentionally a different denominator from benchmark throughput.

Report medians and spread across repetitions, not a fabricated theoretical speedup.
Tiny tasks can be slower in parallel because allocations, locks, condition-variable
wakeups and futures cost more than useful work. More workers can add contention.

## Profiling

When installed and permitted on Linux:

```sh
perf stat -e task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions ./build-release/threadforge_benchmark --tasks 2000 --repeats 3 --max-workers 8 --output /tmp/threadforge-perf.csv
perf record -g -- ./build-release/threadforge_benchmark --tasks 2000 --repeats 3 --max-workers 8 --output /tmp/threadforge-profile.csv
perf report
```

These profile the whole benchmark sweep, including setup. For detailed attribution,
inspect symbols and call stacks rather than interpreting whole-sweep counters as
one workload. WSL/hardware-event permissions vary. perf and Clang were absent in
the validation environment; no hardware-counter or cross-compiler claims are made.
