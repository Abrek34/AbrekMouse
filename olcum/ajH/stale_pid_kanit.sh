#!/usr/bin/env bash
# AJ-H — LIVENESS (stale-PID) düzeltmesinin kanıtı.
#
# Senaryo: canlı bir daemon var (IPC soketi cevap veriyor) ama PID dosyası
# silinmiş ya da bayat.  Düzeltme ÖNCESİ CLI "Daemon: stopped" (rc=2) diyor
# ve sokete hiç bakmıyordu; DÜZELTME SONRASI "running" (rc=0) diyor.
#
#   bash olcum/ajH/stale_pid_kanit.sh
#
# İzolasyon: `unshare --map-root-user --mount` ile /run geçici olarak boş bir
# tmpfs'e bağlanır — canlı sistemin /run/rawaccel.pid dosyasına DOKUNULMAZ.
#
# ⚠️ Bu betik bir CLI DERLEMESİ yapar (geçici dizine; build-manual'a yazmaz).
#    Başka bir ajan build-manual'da derleme yapıyorsa çalıştırma.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/olcum/ajH"
mkdir -p "$OUT"
LOG="$OUT/stale_pid_kanit.log"

WORK="$(mktemp -d)"
XDG="$WORK/xdg"
mkdir -p "$XDG"

# ÖNCE hâli (21 Ekim 2026, 17:47 ölçümü — düzeltme öncesi kurulu 1.2.7):
#   SENARYO A (PID dosyası YOK  + canlı soket) → "Daemon: stopped"  rc=2
#   SENARYO B (bayat/ölü PID   + canlı soket) → "Daemon: stopped"  rc=2
#   SENARYO C (ölü soket       + canlı PID)   → "Daemon: unreachable" rc=3
#   SENARYO E (ne soket ne PID)               → "Daemon: stopped"  rc=2
# Bu tablo kanıtın "önce" yarısıdır; aşağıdaki koşum "sonra" yarısını üretir.

echo "=== AJ-H stale-PID kanıtı — $(date '+%Y-%m-%d %H:%M:%S') ===" | tee "$LOG"

CLI="$WORK/rawaccel-cli"
echo "CLI derleniyor (geçici dizine: $CLI) ..." | tee -a "$LOG"
g++ -std=c++20 -O2 -w -I"$ROOT/include" -I"$ROOT/src" \
    "$ROOT/cli/main.cpp" "$ROOT/src/config.cpp" \
    "$ROOT/src/logitech_receiver.cpp" "$ROOT/src/logitech_hidpp.cpp" \
    -o "$CLI" -lpthread || { echo "derleme başarısız" | tee -a "$LOG"; exit 1; }

run_scenario() {
  local name="$1" pid_mode="$2" expect_state="$3" expect_rc="$4"
  pkill -f "fake_daemon.py $XDG" 2>/dev/null || true
  rm -f "$XDG/rawaccel.sock" "$XDG/rawaccel.pid"
  # "yok-daemon" kipi: ne soket ne PID dosyası — "stopped" senaryosu için.
  local dpid=""
  if [ "$pid_mode" != "yok-daemon" ]; then
    python3 "$ROOT/olcum/ajH/fake_daemon.py" "$XDG/rawaccel.sock" "$pid_mode" \
        > "$WORK/daemon.log" 2>&1 &
    dpid=$!
    for _ in $(seq 1 50); do [ -S "$XDG/rawaccel.sock" ] && break; sleep 0.1; done
  fi
  local out rc state
  out="$(unshare --map-root-user --mount bash -c \
        "mount -t tmpfs none /run 2>/dev/null; \
         XDG_RUNTIME_DIR=$XDG '$CLI' -c '$WORK/cfg.json' status 2>&1")"
  rc=$?
  if [ -n "$dpid" ]; then
    kill "$dpid" 2>/dev/null || true
    wait "$dpid" 2>/dev/null || true
  fi
  state="$(printf '%s\n' "$out" | head -1)"
  printf '%-46s → %-28s rc=%s\n' "$name" "$state" "$rc" | tee -a "$LOG"
  if [ "$rc" != "$expect_rc" ]; then
    echo "   ⚠ BEKLENEN rc=$expect_rc idi" | tee -a "$LOG"
  fi
}

printf '{"active_profile":"sahte","profiles":[{"name":"sahte"}]}' > "$WORK/cfg.json"

run_scenario "SENARYO A: PID dosyası YOK + canlı soket"  yok   "running"     0
run_scenario "SENARYO B: bayat PID (ölü 999999) + canlı soket" bozuk "running" 0
run_scenario "SENARYO E: ne soket ne PID"            yok-daemon "stopped" 2

echo
echo "ÖNCE (21 Ekim 17:47, düzeltme öncesi): A → stopped rc=2 · B → stopped rc=2 · E → stopped rc=2" | tee -a "$LOG"
echo "SONRA (yukarıdaki ölçüm): A/B → running rc=0 · E → stopped rc=2 (değişmedi)" | tee -a "$LOG"
rm -rf "$WORK"
echo "=== bitti ===" | tee -a "$LOG"
