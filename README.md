# ThreadForge

A C++17 task execution engine built incrementally to study systems programming,
concurrency correctness, and performance. **This repository implements Phase 1
only:** a fixed-size FIFO worker pool with graceful shutdown.

Implemented features:

- Persistent workers sleeping on a condition variable when idle.
- Thread-safe submission of copyable `void()` callables to an unbounded queue.
- Exactly-once invocation of accepted tasks, including concurrent producers.
- Graceful, idempotent shutdown; concurrent external shutdown calls are serialized.
- Zero-worker and empty-task rejection; cleanup after partial thread creation.
- Task exception containment and a failure counter.
- A 100-task example and dependency-free CTest correctness tests.

## Build and run (Ubuntu / WSL2)

Install GCC and CMake if needed: `sudo apt-get install build-essential cmake`.
From the repository directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
./build/basic_usage
```

CMake requires C++17, disables language extensions, and enables `-Wall -Wextra
-Wpedantic` for GCC/Clang. The core uses only the standard library.

```cpp
#include <threadforge/thread_pool.hpp>
#include <atomic>

std::atomic<int> completed{0}; // Captured state outlives pool work.
threadforge::ThreadPool pool(4);
pool.submit([&completed] { ++completed; });
pool.shutdown(); // All accepted tasks have finished; completed == 1.
```

## Contract and architecture

Workers dequeue from one shared FIFO under a mutex, release it, and invoke the
task. FIFO describes dequeue order; multiple workers can start and finish in a
different order. Submissions are ordered by successful insertion under the mutex.

`submit` takes ownership of a `std::function<void()>`. Lambdas, function pointers,
and copyable callable objects work; bind arguments with captures. Reference
captures remain the caller's responsibility. Move-only callables, typed return
values, and futures are not implemented yet.

Shutdown changes Running to Draining under the queue mutex. Further submissions
throw `std::runtime_error`; accepted tasks drain before workers exit and are joined.
Stopped is reached after all joins. The destructor performs the same shutdown.
An empty callable throws `std::invalid_argument`. A failed queue allocation throws
without accepting the task. Rejected tasks are never invoked.

Task exceptions are caught at the worker boundary and counted by
`failed_task_count()`; workers continue. Phase 1 does **not** propagate those
exceptions to the submitter. After shutdown the failure count is final.

The pool must outlive every call to it. Destruction may not overlap public calls,
and must happen on a non-worker thread. Calling `shutdown()` from its own worker
throws `std::logic_error`; destroying the pool there violates the lifetime contract
and terminates through its noexcept destructor. Tasks must eventually return:
shutdown cannot forcibly stop user code. Avoid task dependency cycles, including
waiting for work queued behind a task on an exhausted pool or cross-pool joins.

See [architecture and concurrency](docs/architecture.md) and
[Phase 1 learning notes](docs/phase1_review.md).

## Testing and analysis

Tests coordinate with mutexes and condition variables, not arbitrary sleeps.
CTest gives each executable a 30-second timeout so a deadlock fails validation.
Checks remain active in release builds. Repeat the concurrency suite with:

```sh
ctest --test-dir build --repeat until-fail:20 --output-on-failure
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j 2
ctest --test-dir build-asan --output-on-failure
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
cmake --build build-tsan -j 2
ctest --test-dir build-tsan --output-on-failure
```

Run ThreadSanitizer separately from AddressSanitizer. Runtime support varies by
kernel and address-space configuration, including WSL. Startup failures are not
evidence of either a data race or race freedom. See
[actual validation results](docs/validation.md).

## Trade-offs and roadmap

The shared mutex keeps ownership and shutdown easy to reason about, but can limit
throughput. The unbounded queue avoids blocking-submission deadlocks at the cost of
unbounded memory growth. `std::function` simplifies ownership but requires copyable
targets and may allocate. No performance claims or benchmark results are made.

- Phase 2: typed futures, return values, and exception propagation.
- Phase 3: priority scheduling, capacity, and overload policies.
- Phase 4: queued-task cancellation and explicit task lifecycle.
- Phase 5: timing metrics, reproducible benchmarks, profiling, and optimization.
- Phase 6 (optional): work stealing and controlled scalability comparisons.

Benchmark instructions will be added with the benchmark suite in Phase 5. There
are no priorities, cancellation, bounded queues, timing metrics, or real-time
guarantees in this version.
