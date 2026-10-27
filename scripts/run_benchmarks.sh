#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${THREADFORGE_BUILD_DIR:-"$repo/build-release"}
prefix=${1:-"$repo/benchmarks/results/run-$(date -u +%Y%m%dT%H%M%SZ)"}
if (( $# > 0 )); then shift; fi
mkdir -p "$(dirname "$prefix")"
cmake -S "$repo" -B "$build" -DCMAKE_BUILD_TYPE=Release -DTHREADFORGE_SANITIZER=none
cmake --build "$build" -j 2
ctest --test-dir "$build" --output-on-failure
{
    date -u --iso-8601=seconds
    uname -a
    cat /etc/os-release
    lscpu
    cmake --version
    compiler=$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$build/CMakeCache.txt")
    "$compiler" --version
    cat "$build/CMakeFiles/threadforge_benchmark.dir/flags.make"
    printf 'Command: %q ' "$build/threadforge_benchmark"
    printf '%q ' "$@"
    printf '\nEnvironment: WSL if kernel contains microsoft; other apps may share CPUs. No affinity pinning.\n'
} > "$prefix.environment.txt"
"$build/threadforge_benchmark" "$@" --output "$prefix.csv"
printf 'Saved %s.csv and %s.environment.txt\n' "$prefix" "$prefix"
