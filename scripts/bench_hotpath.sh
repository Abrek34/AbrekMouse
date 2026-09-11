#!/bin/bash
# bench_hotpath.sh — Hot-path microbenchmark for RawAccel Linux
# Measures cycles, instructions, and syscalls per event for different acceleration configurations.
# Usage: ./scripts/bench_hotpath.sh [iterations] [perf-runs] [output-file]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-manual"
BENCH_BIN="$BUILD_DIR/bench_hotpath"
ITERATIONS="${1:-1000000}"
PERF_RUNS="${2:-3}"
OUTPUT_FILE="${3:-$PROJECT_ROOT/bench_hotpath_results.txt}"
CXX="${CXX:-g++}"

if [[ ! -f "$BENCH_BIN" ]]; then
    echo "Building benchmark..."
    cd "$PROJECT_ROOT"
    # R4-L-4: $CXX onurlandır; RAWACCEL_PORTABLE=1 portable üretir (mirror build.sh).
    if [[ "${RAWACCEL_PORTABLE:-0}" = "1" ]]; then
        MARCH=""
    else
        MARCH="-march=native"
    fi
    "$CXX" -O3 $MARCH -std=c++20 \
        -I"$PROJECT_ROOT/include" \
        -I"$PROJECT_ROOT/include/nlohmann" \
        "$PROJECT_ROOT/tests/bench_hotpath.cpp" \
        -o "$BENCH_BIN"
fi

if [[ ! -f "$BENCH_BIN" ]]; then
    echo "ERROR: Failed to build benchmark binary" >&2
    exit 1
fi

{
    echo "=== RawAccel Hot-Path Benchmark ==="
    echo "Date: $(date)"
    echo "Host: $(hostname)"
    # SH-1: on non-x86 (/proc/cpuinfo uses 'Hardware'/'CPU implementer') the
    # 'model name' grep would exit non-zero under `set -euo pipefail` aborting
    # the whole benchmark for a cosmetic host line.  Try the common names.
    CPU_MODEL="$(grep -m1 -E 'model name|Hardware|CPU implementer' /proc/cpuinfo \
             | cut -d: -f2 | xargs || true)"
    echo "CPU: ${CPU_MODEL:-unknown}"
    echo "Kernel: $(uname -r)"
    echo "Iterations per run: $ITERATIONS"
    echo "Perf runs per config: $PERF_RUNS"
    echo "Binary: $BENCH_BIN"
    echo "Compiler: $($CXX --version | head -1)"
    echo ""
    
    # Run benchmark without perf (timing only)
    echo "=== Timing Results (nanoseconds per event) ==="
    "$BENCH_BIN" "$ITERATIONS"
    echo ""
    
    # Run with perf if available
    if command -v perf &> /dev/null; then
        echo "=== Hardware Counters (perf stat) ==="
        perf stat -r "$PERF_RUNS" -e cycles,instructions,syscalls -- "$BENCH_BIN" "$ITERATIONS" 2>&1 | \
            grep -E "(cycles|instructions|syscalls|ns/event|SUMMARY|===|Performance counter)"
        echo ""
    else
        echo "=== Hardware Counters ==="
        echo "perf not available. Install 'perf' package for cycles/instructions/syscalls measurements."
        echo ""
    fi
    
    echo "=== Baseline Summary ==="
    echo "Configuration                    | ns/event"
    echo "--------------------------------|----------"
    "$BENCH_BIN" "$ITERATIONS" 2>&1 | grep -E "(noaccel|power-whole|classic|power\+rot45|power-dual|apply_motion_math)" | \
        sed 's/: / | /' | sed 's/ ns\/event.*/ ns/'
    
} | tee "$OUTPUT_FILE"

echo ""
echo "Results saved to: $OUTPUT_FILE"