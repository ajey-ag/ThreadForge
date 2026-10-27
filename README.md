# ThreadForge

ThreadForge is a reusable C++17 task execution engine for studying systems
programming, concurrency correctness, scheduling and performance measurement.
It uses the standard library, CMake and CTest, with Linux/Ubuntu/WSL2 as the
primary environment. It has no CUDA, web application or external C++ dependency.

Phases 1–5 are implemented. Phase 6 adds an **experimental work-stealing policy**
with per-worker deques and serialized queue access; it is not lock-free and does
not claim better scalability. See [validation](docs/validation.md) for actual
build, test, sanitizer and benchmark results.

## Build and run on your Windows machine

Open PowerShell, enter Ubuntu, then run the Linux commands:

```powershell
wsl -d Ubuntu
```

```sh
cd /mnt/c/Users/dell/Documents/project/ThreadForge
# Already installed during development; needed on a fresh Ubuntu installation:
sudo apt-get install build-essential cmake

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure

./build/basic_usage
./build/parallel_tasks
./build/priority_demo
```

`basic_usage` verifies 100 tasks execute exactly once. `parallel_tasks` demonstrates
results, move-only inputs and exception propagation. `priority_demo` creates a
controlled backlog, demonstrates High/Normal/Low order and cancels a queued task.
This project is a C++ library with command-line examples, not a GUI application.
On native Linux, use your checkout path instead of the `/mnt/c/...` path.

CMake requires standard C++17 without extensions and enables `-Wall -Wextra
-Wpedantic` on GCC/Clang. Use `-j 2` to limit compiler memory usage; increase it if
your machine has spare memory. Build directories are ignored by Git.

## Use the library

Link your CMake target to `threadforge` and include the public header:

```cpp
#include <threadforge/thread_pool.hpp>

threadforge::ThreadPool pool(4);
auto sum = pool.submit([](int a, int b) { return a + b; }, 10, 20);
int value = sum.get(); // 30; rethrows the task's exception if it failed.
pool.shutdown();      // All accepted work is settled before return.
```

The constructor accepts worker count, policy, waiting-queue capacity and whether
task timing is enabled:

```cpp
threadforge::ThreadPool pool(
    4, threadforge::SchedulingPolicy::Priority, 256, true);
auto urgent = pool.submit_with_priority(threadforge::Priority::High, [] { return 42; });
auto pending = pool.submit_cancellable([] { return 7; });
if (pending.handle.cancel()) {
    // pending.future.get() throws threadforge::TaskCancelled.
}
pool.shutdown();
```

- Policies: `Fifo` (default), `Priority`, experimental `WorkStealing`.
- Capacity zero means unbounded; otherwise it counts queued tasks, not running ones.
- `QueueFull` and `PoolClosed` throw synchronously; rejected work is not accepted.
- `submit_cancellable_with_priority` combines handles and explicit priority.
- Cancellation succeeds only while queued, frees capacity immediately, and never
  forcibly terminates running code. Handles expose `TaskState` and can outlive the
  pool. A moved-from handle cannot cancel; querying its state throws `logic_error`.
- `shutdown(ShutdownMode::CancelPending)` cancels queued work and waits for running
  work. Default shutdown drains all work. The first shutdown caller chooses the
  mode; concurrent later callers wait for that shutdown. The destructor drains.

Callables and arguments are decay-copied/moved into the task, then invoked once
as rvalues. Move-only captures and arguments work. Use `std::ref` for explicit
borrowing; referenced objects must outlive execution. Function and member pointers,
lambdas and callable objects follow C++17 invocation rules. Void and reference
results work. Empty `std::function` reports `bad_function_call` through its future.
A returned reference does not extend the referenced object's lifetime.

## Architecture and contracts

Workers sleep on a predicate-based condition variable. One mutex protects queue
ownership, admission, task dequeue/cancellation and metric accounting. User code
and captured-resource destruction occur outside that mutex. A separate mutex
serializes shutdown/join. Lifecycle is Running → Draining → Stopped.

FIFO is dequeue order, not completion order. Priority is stable within a level,
nonpreemptive and can starve lower-priority work. Work stealing uses round-robin
external placement, worker-local placement for nested tasks, local LIFO removal
and cyclic FIFO stealing. All deque access still shares the pool mutex.

The pool must outlive all calls, including handle cancellation; destruction must
not overlap public calls. Do not destroy a pool from one of its own workers.
Calling shutdown from its own worker throws `logic_error`; violating the destructor
contract terminates through the noexcept destructor. Tasks must eventually return.
Waiting for child futures inside an exhausted pool can deadlock even with work
stealing. No real-time guarantees, forced interruption or dependency scheduling
are provided.

`metrics()` returns coherent counts, queue depth/peak, running tasks, steals,
optional mean wait/execution/completion times in nanoseconds, and lifetime
throughput. Successful completion, failure and cancellation are distinct counts.
A future can become ready just before terminal state/accounting is published;
call shutdown before expecting final counts. Timing averages are absent with no
samples or when timing is disabled. Counts remain enabled. See
[metric boundaries and benchmark methodology](docs/benchmark_methodology.md).

## Tests and sanitizers

```sh
ctest --test-dir build --output-on-failure
ctest --test-dir build -R thread_pool_tests --repeat until-fail:20 --output-on-failure
./build/stress_tests

cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DTHREADFORGE_SANITIZER=address -DCMAKE_CXX_FLAGS=
cmake --build build-asan -j 2
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DTHREADFORGE_SANITIZER=thread -DCMAKE_CXX_FLAGS=
cmake --build build-tsan -j 2
ctest --test-dir build-tsan --output-on-failure
```

Address mode combines AddressSanitizer and UndefinedBehaviorSanitizer. Thread mode
runs separately. The empty CMAKE_CXX_FLAGS clears any old manually supplied
sanitizer flags in reused build directories. Sanitizer runtime support varies by
kernel/address-space setup. Startup failures are reported separately from races.
Tests use synchronization rather than arbitrary sleeps; CTest applies timeouts.
The stress executable checks 30,000 tasks per policy across concurrent producers,
including cancellation, exceptions, typed results and exactly-once accounting.

## Benchmarks

```sh
bash scripts/run_benchmarks.sh benchmarks/results/my-run --tasks 2000 --repeats 5 --iterations 20000 --max-workers 8
```

This builds Release, runs tests, then saves `my-run.csv` and
`my-run.environment.txt`. Choose a fresh output prefix for each experiment. Default
workloads cover CPU, very short, mixed-duration, simulated blocking, and larger
batches. Rows compare serial execution, FIFO with timing on/off, priority and work
stealing. Each run includes warm-up, repeated measurements, checked results,
throughput, latency percentiles, mean queue/execution time and scaling efficiency.

For a quick run after building Release:

```sh
./build-release/threadforge_benchmark --tasks 100 --repeats 2 --iterations 2000 --max-workers 4 --output benchmarks/results/quick.csv
```

The script accepts `THREADFORGE_BUILD_DIR=/var/tmp/threadforge-release` to build on
WSL's Linux filesystem, which was faster here than the mounted Windows directory.
Benchmarks are environment-specific; the tiny-task case often loses to serial
execution because coordination costs exceed useful work. See the checked-in
[results](benchmarks/results) and [methodology](docs/benchmark_methodology.md).

## Design notes and learning path

- [Architecture](docs/architecture.md) and [concurrency model](docs/concurrency_model.md)
- [Scheduling policies](docs/scheduling_policies.md) and [design decisions](docs/design_decisions.md)
- Phase reviews: [1](docs/phase1_review.md), [2](docs/phase2_review.md),
  [3](docs/phase3_review.md), [4](docs/phase4_review.md), [5](docs/phase5_review.md),
  [6](docs/phase6_review.md). Each records concepts, trade-offs and five interview questions.
- [Validation commands and results](docs/validation.md)

Remaining research: per-deque locks and less contended accounting, fairness/aging,
allocation pooling, dependency-aware waiting, deterministic thread-creation fault
injection, and broader compiler/platform tests. These are not implemented features.
Changes should follow measured bottlenecks, not assumed speedups. MIT licensed.
