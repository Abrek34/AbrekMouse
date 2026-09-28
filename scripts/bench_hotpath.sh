#!/bin/bash
# bench_hotpath.sh — Hot-path microbenchmark for RawAccel Linux
# Measures ns/event per configuration using C++ clock_gettime(CLOCK_MONOTONIC) median-of-N.
# Adaptive iterations per config to achieve >=100ms measurement region.
# No external tools required (perf, bc, /usr/bin/time are optional).
# Usage: ./scripts/bench_hotpath.sh [runs] [output-file] [--json] [--min-seconds N]
# Env: BENCH_POSITIVE_CONTROL=1  -- inject busy-loop to verify gate detects regression
#      BENCH_UPDATE_BASELINE=1   -- update perf_baseline.json with current results

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-manual"
BENCH_BIN="$BUILD_DIR/bench_hotpath"
BASELINE_FILE="$PROJECT_ROOT/tests/perf_baseline.json"
CXX="${CXX:-g++}"
JSON_OUTPUT=false

# Default values
RUNS=3
MIN_SECONDS=0.1

# Parse flags first, then positional args
POSITIONAL_ARGS=()
for arg in "$@"; do
    if [[ "$arg" == "--json" ]]; then
        JSON_OUTPUT=true
    elif [[ "$arg" == "--positive-control" ]]; then
        # Handled by benchmark binary via env var
        :
    elif [[ "$arg" == "--min-seconds" ]]; then
        # Next positional arg will be the value
        MIN_SECONDS_FLAG=true
    else
        if [[ "${MIN_SECONDS_FLAG:-false}" == "true" ]]; then
            MIN_SECONDS="$arg"
            MIN_SECONDS_FLAG=false
        else
            POSITIONAL_ARGS+=("$arg")
        fi
    fi
done

RUNS="${POSITIONAL_ARGS[0]:-3}"
OUTPUT_FILE="${POSITIONAL_ARGS[1]:-$PROJECT_ROOT/bench_hotpath_results.txt}"

# Build benchmark if needed
if [[ ! -f "$BENCH_BIN" ]]; then
    echo "Building benchmark..."
    cd "$PROJECT_ROOT"
    if [[ "${RAWACCEL_PORTABLE:-0}" = "1" ]]; then
        MARCH=""
    else
        MARCH="-march=native"
    fi
    case "$(uname -m)" in
        x86_64|amd64|i[3-6]86) FCF="-fcf-protection=full" ;;
        *) FCF="" ;;
    esac
    HARDENING="-fstack-protector-strong -fstack-clash-protection $FCF -D_FORTIFY_SOURCE=2 -D_GLIBCXX_ASSERTIONS -fPIE -Wformat -Wformat-security"
    LDFLAGS_HARDEN="-pie -Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code"

    PGO_FLAGS=""
    if [[ "${RAWACCEL_PGO:-0}" = "1" ]] && [[ "${RAWACCEL_PGO_GEN:-0}" = "1" ]]; then
        PGO_FLAGS="-fprofile-generate=$BUILD_DIR/pgo-data"
        mkdir -p "$BUILD_DIR/pgo-data"
        echo "[INFO] PGO profile generation enabled for benchmark"
    elif [[ "${RAWACCEL_PGO:-0}" = "1" ]] && [[ "${RAWACCEL_PGO_USE:-0}" = "1" ]]; then
        PGO_FLAGS="-fprofile-use=$BUILD_DIR/pgo-data -fprofile-correction"
        echo "[INFO] PGO profile use enabled for benchmark"
    fi

    "$CXX" -O3 $MARCH -std=c++20 $HARDENING $PGO_FLAGS \
        -I"$PROJECT_ROOT/include" \
        -I"$PROJECT_ROOT/include/nlohmann" \
        "$PROJECT_ROOT/tests/bench_hotpath.cpp" \
        $LDFLAGS_HARDEN \
        -o "$BENCH_BIN"
fi

if [[ ! -f "$BENCH_BIN" ]]; then
    echo "ERROR: Failed to build benchmark binary" >&2
    exit 1
fi

# Handle BENCH_UPDATE_BASELINE early
if [[ "${BENCH_UPDATE_BASELINE:-0}" == "1" ]]; then
    echo "=== Updating baseline ==="
    BENCH_OUTPUT="$("$BENCH_BIN" "$RUNS" --json --min-seconds $MIN_SECONDS)"
    BENCH_EXIT=$?
    if [[ $BENCH_EXIT -ne 0 ]]; then
        echo "ERROR: Benchmark binary failed" >&2
        exit 1
    fi
    echo "$BENCH_OUTPUT" > "$BASELINE_FILE"
    echo "Baseline updated: $BASELINE_FILE"
    cat "$BASELINE_FILE"
    exit 0
fi

# Run benchmark and capture JSON output
BENCH_OUTPUT="$("$BENCH_BIN" "$RUNS" --json --min-seconds $MIN_SECONDS)"
BENCH_EXIT=$?
if [[ $BENCH_EXIT -ne 0 ]]; then
    echo "ERROR: Benchmark binary failed with exit code $BENCH_EXIT" >&2
    exit 1
fi

# Parse JSON output using awk (no jq/bc needed)
# Use index() for literal string matching instead of regex
# Only match top-level keys (not nested in _meta)
parse_bench_json() {
    local json="$1"
    local key="$2"
    echo "$json" | awk -v key="\"$key\"" '
        /^  "_meta":/ { in_meta = 1 }
        in_meta && /^  }$/ { in_meta = 0 }
        !in_meta && index($0, key) {
            # Find the number after the colon
            match($0, /: *([0-9]+\.?[0-9]*)/, arr)
            if (arr[1] != "") print arr[1]
        }
    '
}

# Extract all config results
CONFIGS=("noaccel" "power-whole" "classic" "power+rot45+snap15+clamp" "power-dual+4ema" "apply_motion_math(full)")
declare -A RESULTS

for config in "${CONFIGS[@]}"; do
    val=$(parse_bench_json "$BENCH_OUTPUT" "$config")
    if [[ -z "$val" ]]; then
        echo "ERROR: Failed to parse result for $config" >&2
        exit 1
    fi
    RESULTS["$config"]="$val"
done

# Load baseline
if [[ ! -f "$BASELINE_FILE" ]]; then
    echo "WARNING: Baseline file not found at $BASELINE_FILE" >&2
    echo "Cannot run regression gate without baseline. Exiting 77." >&2
    exit 77
fi

# Parse baseline using awk
parse_baseline_json() {
    local json="$1"
    local key="$2"
    echo "$json" | awk -v key="\"$key\"" '
        index($0, key) {
            # Match either number or quoted string
            match($0, /: *([0-9]+\.?[0-9]*|"[^"]*")/, arr)
            if (arr[1] != "") {
                val = arr[1]
                # Remove surrounding quotes if present
                gsub(/^"|"$/, "", val)
                print val
            }
        }
    '
}

BASELINE_JSON="$(cat "$BASELINE_FILE")"
THRESHOLD_PCT=$(parse_baseline_json "$BASELINE_JSON" "threshold_percent")
if [[ -z "$THRESHOLD_PCT" ]]; then
    THRESHOLD_PCT=5.0
fi

declare -A BASELINE
for config in "${CONFIGS[@]}"; do
    val=$(parse_baseline_json "$BASELINE_JSON" "$config")
    if [[ -z "$val" ]]; then
        echo "ERROR: Failed to parse baseline for $config" >&2
        exit 1
    fi
    BASELINE["$config"]="$val"
done

# G-5: CPU model/governor validation — exit 77 if mismatch
# Baseline stores cpu_model and cpu_governor in _meta; current run captures them.
# A different CPU model/governor means regression gate is not valid for this hardware.
BASELINE_CPU_MODEL=$(parse_baseline_json "$BASELINE_JSON" "cpu_model")
BASELINE_CPU_GOVERNOR=$(parse_baseline_json "$BASELINE_JSON" "cpu_governor")
CURRENT_CPU_MODEL="$(grep -m1 -E 'model name|Hardware|CPU implementer' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs || true)"
CURRENT_CPU_GOVERNOR="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null | xargs || true)"

if [[ -n "$BASELINE_CPU_MODEL" && "$BASELINE_CPU_MODEL" != "unknown" && "$CURRENT_CPU_MODEL" != "$BASELINE_CPU_MODEL" ]]; then
    echo "⚠️  CPU MODEL MISMATCH: baseline='$BASELINE_CPU_MODEL', current='$CURRENT_CPU_MODEL'" >&2
    echo "Performance regression gate is not valid on different hardware." >&2
    echo "To create a baseline for this hardware, run:" >&2
    echo "  BENCH_UPDATE_BASELINE=1 $0 $RUNS" >&2
    exit 77
fi

# CPU Governor validation — skip if unreadable on this system
if [[ "$BASELINE_CPU_GOVERNOR" != "unknown" && "$BASELINE_CPU_GOVERNOR" != "unreadable" ]]; then
    if [[ "$CURRENT_CPU_GOVERNOR" == "unreadable" ]]; then
        echo "⚠️  CPU GOVERNOR UNREADABLE: cannot read /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor" >&2
        echo "Governor validation skipped — baseline='$BASELINE_CPU_GOVERNOR', current=unreadable" >&2
    elif [[ "$CURRENT_CPU_GOVERNOR" != "$BASELINE_CPU_GOVERNOR" ]]; then
        echo "⚠️  CPU GOVERNOR MISMATCH: baseline='$BASELINE_CPU_GOVERNOR', current='$CURRENT_CPU_GOVERNOR'" >&2
        echo "Performance characteristics may differ (powersave vs performance vs ondemand)." >&2
        echo "To create a baseline for this governor, run:" >&2
        echo "  BENCH_UPDATE_BASELINE=1 $0 $RUNS" >&2
        exit 77
    fi
fi

# Compare results against baseline
echo "=== RawAccel Hot-Path Benchmark ==="
echo "Date: $(date)"
echo "Host: $(hostname)"
CPU_MODEL="$(grep -m1 -E 'model name|Hardware|CPU implementer' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs || true)"
echo "CPU: ${CPU_MODEL:-unknown}"
echo "Kernel: $(uname -r)"
echo "Runs per config (median): $RUNS"
echo "Min seconds per run: $MIN_SECONDS"
echo "Binary: $BENCH_BIN"
echo "Compiler: $("$CXX" --version 2>/dev/null | head -1 || echo "unknown")"
echo "Threshold: ${THRESHOLD_PCT}%"
echo ""

FAILED=0
REGRESSED_CONFIGS=()

echo "Configuration                    | Current (ns/event) | Baseline (ns/event) | Delta % | Status"
echo "----------------------------------|--------------------|---------------------|---------|-------"

for config in "${CONFIGS[@]}"; do
    current="${RESULTS[$config]}"
    baseline="${BASELINE[$config]}"

    # Calculate delta percentage using awk (no bc)
    delta_pct=$(awk -v cur="$current" -v base="$baseline" 'BEGIN {
        if (base == 0) { print "inf"; exit }
        printf "%.2f", ((cur - base) / base) * 100
    }')

    # Check if regression (current > baseline * (1 + threshold/100))
    is_regression=$(awk -v cur="$current" -v base="$baseline" -v thresh="$THRESHOLD_PCT" 'BEGIN {
        if (base == 0) { print 0; exit }
        if (cur > base * (1 + thresh / 100)) print 1; else print 0
    }')

    if [[ "$is_regression" == "1" ]]; then
        status="REGRESSION"
        FAILED=1
        REGRESSED_CONFIGS+=("$config")
    else
        status="OK"
    fi

    printf "%-34s | %18s | %19s | %7s | %s\n" "$config" "$current" "$baseline" "$delta_pct" "$status"
done

echo ""

if [[ $FAILED -eq 1 ]]; then
    echo "=== PERFORMANCE REGRESSION DETECTED ==="
    echo "The following configurations exceeded the ${THRESHOLD_PCT}% threshold:"
    for config in "${REGRESSED_CONFIGS[@]}"; do
        echo "  - $config"
    done
    echo ""
    echo "To update baseline (after verifying the change is intentional):"
    echo "  BENCH_UPDATE_BASELINE=1 $0 $RUNS"
    exit 1
else
    echo "=== ALL CONFIGURATIONS WITHIN THRESHOLD ==="
    echo "No performance regression detected."
fi

# Save results (human-readable or JSON)
{
    if [[ "$JSON_OUTPUT" == "true" ]]; then
        echo "$BENCH_OUTPUT"
    else
        echo "=== RawAccel Hot-Path Benchmark Results ==="
        echo "Date: $(date)"
        echo "Runs: $RUNS (median), Min seconds per run: $MIN_SECONDS"
        echo ""
        for config in "${CONFIGS[@]}"; do
            echo "${config}: ${RESULTS[$config]} ns/event"
        done
        echo ""
        echo "Baseline threshold: ${THRESHOLD_PCT}%"
        if [[ $FAILED -eq 1 ]]; then
            echo "Status: REGRESSION"
        else
            echo "Status: OK"
        fi
    fi
} | tee "$OUTPUT_FILE"

echo ""
echo "Results saved to: $OUTPUT_FILE"
exit 0