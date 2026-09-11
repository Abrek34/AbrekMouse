#!/usr/bin/env bash
# E2E daemon test — drives the REAL rawaccel-daemon against a synthetic
# uinput source and validates the output byte stream (accel + raw phases).
#
# Requires:
#   - root (or member of the `input` group for /dev/uinput + grab)
#   - /dev/uinput, libevdev dev headers (libevdev-dev), g++
#   - the daemon binary (scripts/build.sh first) → build-manual/rawaccel-daemon
#
# Isolation: if a system rawaccel-daemon is already running it is SIGSTOPped
# for the duration of the run (its grabs stay held, so it does not steal the
# synthetic source) and SIGCONTed on exit — including on Ctrl-C / failure.
#
# Exit: 0 = all checks passed, 1 = a check failed, 77 = environment unusable.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAEMON="${ROOT}/build-manual/rawaccel-daemon"
HARNESS="${ROOT}/tests/e2e_harness"
CPP_HARNESS="${ROOT}/tests/e2e_harness.cpp"
PASS=0; FAIL=0; SKIP=0

echo "=== RawAccel E2E harness ==="

if [[ ! -x "$DAEMON" ]]; then
    echo "[ERR] daemon binary missing at $DAEMON — run bash scripts/build.sh first"
    exit 77
fi
if [[ ! -w /dev/uinput ]]; then
    echo "[ERR] /dev/uinput not writable — run as root or add user to 'input' group"
    exit 77
fi

# Build the harness if stale.
if [[ ! -x "$HARNESS" || "$CPP_HARNESS" -nt "$HARNESS" ]]; then
    echo "[INFO] building harness..."
    if ! g++ -std=c++17 -O2 -Wall -Wextra -o "$HARNESS" "$CPP_HARNESS" \
            $(pkg-config --cflags --libs libevdev); then
        echo "[ERR] harness build failed (need libevdev-dev / g++)"
        exit 77
    fi
fi

# ── Pause the running system daemon so it can't steal our synthetic source ──
SYS_PID="$(pgrep -x rawaccel-daemon 2>/dev/null | head -n1 || true)"
if [[ -n "$SYS_PID" ]]; then
    if kill -STOP "$SYS_PID" 2>/dev/null; then
        echo "[INFO] system daemon (pid $SYS_PID) paused for isolation"
        trap '[ -n "${SYS_PID:-}" ] && kill -CONT "$SYS_PID" 2>/dev/null || true' EXIT
    else
        echo "[WARN] could not pause system daemon — hot-plug may interfere"
    fi
fi

run_phase() {
    local phase=$1
    echo
    echo "── phase: $phase ──"
    "$HARNESS" --daemon "$DAEMON" --phase "$phase"
    local rc=$?
    # T30-N2: rc=77 ("environment unusable") is NOT a failed check — it must
    # propagate as a skip (exit 77) so CI can distinguish infra from failure.
    if [[ $rc -eq 0 ]]; then PASS=$((PASS+1));
    elif [[ $rc -eq 77 ]]; then SKIP=$((SKIP+1));
    else FAIL=$((FAIL+1)); fi
    return $rc
}

run_phase accel
run_phase raw

echo
echo "=== RESULT: phases=$((PASS+FAIL+SKIP)) passed=$PASS skipped=$SKIP failed=$FAIL ==="
if [[ $SKIP -gt 0 ]]; then exit 77; fi
[[ $FAIL -eq 0 ]] && exit 0 || exit 1