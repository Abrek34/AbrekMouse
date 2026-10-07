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

# T53-04: daemon binary'si bayat mı denetle — daemon/**, include/** veya
# src/** altında binary'den YENİ kaynak varsa sonuç yeni koda yansımaz.
stale_src="$(find "${ROOT}/daemon" "${ROOT}/include" "${ROOT}/src" \
    \( -name '*.cpp' -o -name '*.hpp' -o -name '*.c' -o -name '*.h' \) \
    -newer "$DAEMON" 2>/dev/null | head -n1)"
if [[ -n "$stale_src" ]]; then
    echo "[WARN] daemon binary'si bayat görünüyor: $stale_src daha yeni —"
    echo "       'bash scripts/build.sh' çalıştırman önerilir; aksi hâlde sonuç"
    echo "       ESKİ koda aitmiş gibi raporlanır."
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
PRESERVED_PIDFILES=""
if [[ -n "$SYS_PID" ]]; then
    if kill -STOP "$SYS_PID" 2>/dev/null; then
        echo "[INFO] system daemon (pid $SYS_PID) paused for isolation"
        # E2E-PID: the PID liveness gate scans EVERY candidate path, so a merely
        # stopped system daemon still counts as "another running instance" and
        # the clean-room test daemon could never start.  Remove the pid file(s)
        # owned by the paused daemon and restore them on exit.  Only steal files
        # whose stored PID matches $SYS_PID (never clobber a third daemon's lock).
        for p in "${XDG_RUNTIME_DIR:-/run/user/0}/rawaccel.pid" /run/rawaccel.pid /tmp/rawaccel.pid; do
            if [[ -f "$p" ]] && [[ "$(cat "$p" 2>/dev/null)" == "$SYS_PID" ]]; then
                rm -f "$p"
                PRESERVED_PIDFILES="$PRESERVED_PIDFILES $p"
            fi
        done
        trap '[ -n "${SYS_PID:-}" ] && { for p in $PRESERVED_PIDFILES; do [ -f "$p" ] || echo "$SYS_PID" > "$p"; done; kill -CONT "$SYS_PID" 2>/dev/null || true; }; rm -rf /tmp/rawe2e-* 2>/dev/null || true' EXIT
    else
        echo "[WARN] could not pause system daemon — hot-plug may interfere"
    fi
fi
# T53-06: sistem daemon'ı duraklatılmadıysa da bayat workdir'leri süpür.
if [[ -z "$(trap -p EXIT)" ]]; then
    trap 'rm -rf /tmp/rawe2e-* 2>/dev/null || true' EXIT
fi

E2E_PHASE_TIMEOUT="${E2E_PHASE_TIMEOUT:-120}"

run_phase() {
    local phase=$1
    echo
    echo "── phase: $phase ──"
    local rc
    # T53-05: her faza üst sınır — grab/poll asılması CI'yı sonsuza kilitlemesin.
    # 124 = timeout; FAIL sayılır (77 çevre atlaması ayrı propagates).
    if command -v timeout >/dev/null 2>&1; then
        timeout "$E2E_PHASE_TIMEOUT" "$HARNESS" --daemon "$DAEMON" --phase "$phase"
        rc=$?
        if [[ $rc -eq 124 ]]; then
            echo "[ERR] phase '$phase' ${E2E_PHASE_TIMEOUT}s içinde bitmedi — FAIL"
            rc=1
        fi
    else
        echo "[WARN] 'timeout' komutu yok — faz asılırsa CI kilitlenir" >&2
        "$HARNESS" --daemon "$DAEMON" --phase "$phase"
        rc=$?
    fi
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
# R10-E2EX: a real failed check must not be masked as an environment skip
# (77) just because another phase also skipped — check FAIL before SKIP.
if [[ $FAIL -gt 0 ]]; then exit 1; fi
if [[ $SKIP -gt 0 ]]; then exit 77; fi
exit 0