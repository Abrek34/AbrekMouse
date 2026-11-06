#!/usr/bin/env bash
# Aj-G — bağımsız yedi kapı koşusu (yeniden kullanılabilir, kalıcı konum)
# Kullanım: bash olcum/ajG/run_gates.sh <tur-etiketi>
# Çıktı: olcum/ajG/<tur>.log
set -u
ROOT=/home/bbbb/Masaüstü/AbrekMouse-main
TUR="${1:-turx}"
LOG="$ROOT/olcum/ajG/$TUR.log"
echo "=== Aj-G bağımsız yedi kapı '$TUR' $(date +%H:%M:%S) ===" > "$LOG"
cd "$ROOT" || exit 1
run_gate() {
  local idx="$1" name="$2" cmd="$3"
  echo ">>> KAPI $idx başlıyor: $cmd ($(date +%H:%M:%S))" >> "$LOG"
  local t0=$SECONDS rc
  eval "$cmd" >> "$LOG" 2>&1
  rc=$?
  echo "<<< KAPI-$idx $name bitti rc=$rc süre=$((SECONDS-t0))s ($(date +%H:%M:%S))" >> "$LOG"
  return $rc
}
run_gate 1 "build"         "bash scripts/build.sh"             || { echo "KIRMIZI: 1"; exit 1; }
run_gate 2 "tests"         "bash tests/run_tests.sh"           || { echo "KIRMIZI: 2"; exit 1; }
run_gate 3 "oracle"        "bash tests/oracle/run_oracle.sh"   || { echo "KIRMIZI: 3"; exit 1; }
run_gate 4 "simd"          "bash tests/run_simd_parity.sh"     || { echo "KIRMIZI: 4"; exit 1; }
run_gate 5 "tr-coverage"   "bash tests/run_tr_coverage.sh"     || { echo "KIRMIZI: 5"; exit 1; }
run_gate 6 "cli-sanitized" "bash tests/run_cli_sanitized.sh"   || { echo "KIRMIZI: 6"; exit 1; }
run_gate 7 "tracker"       "bash tests/run_tracker_bridge.sh"  || { echo "KIRMIZI: 7"; exit 1; }
run_gate 8 "installer-smoke" "bash tests/run_installer_smoke.sh" || { echo "KIRMIZI: 8"; exit 1; }
bash install-gui.sh --detect >> "$LOG" 2>&1 || { echo "KIRMIZI: 8b detect"; exit 1; }
echo "=== SONUÇ (7 kapı + installer smoke/detect): YEŞİL — $(date +%H:%M:%S) ===" >> "$LOG"
echo "OK TÜM"
