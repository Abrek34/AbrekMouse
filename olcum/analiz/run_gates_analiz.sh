#!/usr/bin/env bash
# opencode — bağımsız doğrulama koşumu (kod değişikliği yok)
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/olcum/analiz"
mkdir -p "$OUT"; cd "$ROOT"
NAMES=(build run_tests oracle simd_parity tr_coverage cli_sanitized tracker_bridge)
CMDS=("bash scripts/build.sh" "bash tests/run_tests.sh" "bash tests/oracle/run_oracle.sh" \
      "bash tests/run_simd_parity.sh" "bash tests/run_tr_coverage.sh" \
      "bash tests/run_cli_sanitized.sh" "bash tests/run_tracker_bridge.sh")
S="$OUT/gates.log"; : > "$S"
echo "=== opencode bağımsız 7 kapı — $(date '+%F %T') ===" | tee -a "$S"
echo "kaynak: $(git rev-parse --short HEAD 2>/dev/null || echo 'git-yok (ağaç git deposu değil)')" | tee -a "$S"
for i in "${!NAMES[@]}"; do
  n="${NAMES[$i]}"; start=$(date +%s)
  bash -c "${CMDS[$i]}" > "$OUT/gate-$((i+1)).log" 2>&1; rc=$?
  echo "KAPI $((i+1))/7 $n → rc=$rc ($(( $(date +%s) - start ))s)  [ham: olcum/analiz/gate-$((i+1)).log]" | tee -a "$S"
done
echo "=== bitti: $(date '+%F %T') ===" | tee -a "$S"
