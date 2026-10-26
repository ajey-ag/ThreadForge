# Phase 1 validation — 2026-10-27

Environment: Ubuntu 26.04 LTS under WSL2, Linux
`6.18.33.2-microsoft-standard-WSL2`, x86_64; GCC 15.2.0
(`Ubuntu 15.2.0-16ubuntu1`), CMake 4.2.3. Sources and build directories were
on the Windows-mounted filesystem. These are correctness results, not benchmarks.

## Environment preparation

Windows PATH inspection found neither CMake nor GCC/Clang. `wsl --list --quiet`
initially failed with sandbox access denied; authorized execution found Ubuntu
and docker-desktop. Inspection inside Ubuntu confirmed no compiler or CMake.

The authorized command `apt-get update && apt-get install -y build-essential cmake`
failed during package downloads with network connection errors. This retry,
executed as root in Ubuntu, successfully installed the toolchain:

```sh
apt-get -o Acquire::ForceIPv4=true -o Acquire::Retries=1 \
  -o Acquire::http::Timeout=20 install -y --no-install-recommends build-essential cmake
```

## Commands and actual results

Commands below ran through `wsl -d Ubuntu -- bash -lc '...'` from
`/mnt/c/Users/dell/Documents/project/ThreadForge`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
./build/basic_usage
./build/thread_pool_tests
ctest --test-dir build --repeat until-fail:20 --output-on-failure
```

- Configure and build succeeded, exit 0, with no compiler warnings.
- Generated flags include `-g -std=c++17 -Wall -Wextra -Wpedantic`.
- Initial CTest: 2/2 passed, 0 failed (reported total 0.27 seconds).
- Example: `Verified: 100 tasks executed exactly once.`
- Direct correctness executable: eight PASS lines for validation,
  fifo_and_destruction, concurrent_workers, producers_exactly_once,
  shutdown_race, exceptions_and_worker_shutdown, drain_running_and_queued,
  and idle_shutdown.
- Repeated CTest: both executables passed 20 runs each, 40 successful
  invocations (reported total 5.69 seconds).

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined
cmake --build build-asan -j 2
ctest --test-dir build-asan --output-on-failure
```

Configure/build succeeded without warnings; CTest 2/2 passed, exit 0, no
AddressSanitizer or UndefinedBehaviorSanitizer diagnostics (0.36 seconds total).

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-fsanitize=thread
cmake --build build-tsan -j 2
ctest --test-dir build-tsan --output-on-failure
```

Configure/build succeeded without warnings; CTest 2/2 passed, exit 0, no
ThreadSanitizer diagnostics (0.55 seconds total). No sanitizer runtime limitation
was encountered in these runs. Results apply to the exercised executions, not all
possible interleavings or platforms.

## Remaining limits

Partial worker-creation failure cleanup was inspected but not fault-injected.
Only GCC on this WSL environment was tested. No Release/Clang/native Linux
validation or performance measurement was performed. Phase 2 has not begun.
The project resides within a parent Git repository; no repository was initialized
and no commit was made.
