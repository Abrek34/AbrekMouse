#!/usr/bin/env bash
# AJ-H — yedi kapının tur koşucusu (Aj-G'nin olcum/ajG/run_gates.sh düzeninin
# aynısı: kalıcı loglar bu dizine düşer, /tmp'ye kanıt bırakılmaz).
#
#   bash olcum/ajH/run_gates.sh <tur-etiketi>     ör. tur4
#
# Çıktı:
#   olcum/ajH/<tur>.log              özet (kapı başına rc + süre)
#   olcum/ajH/<tur>.gate-<n>.log     her kapının ham çıktısı
#
# ⚠️ build-manual/ ORTAK STADIR: bu koşum sırasında başka ajan derleme
# başlatmamalıdır. Koşumdan önce duyuru yapılır.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TUR="${1:?kullanım: bash olcum/ajH/run_gates.sh <tur-etiketi>}"
OUT="$ROOT/olcum/ajH"
mkdir -p "$OUT"
cd "$ROOT"

SUMMARY="$OUT/$TUR.log"
: > "$SUMMARY"

NAMES=(build run_tests oracle simd_parity tr_coverage cli_sanitized tracker_bridge)
CMDS=(
  "bash scripts/build.sh"
  "bash tests/run_tests.sh"
  "bash tests/oracle/run_oracle.sh"
  "bash tests/run_simd_parity.sh"
  "bash tests/run_tr_coverage.sh"
  "bash tests/run_cli_sanitized.sh"
  "bash tests/run_tracker_bridge.sh"
)

{
  echo "=== AJ-H yedi kapı koşusu — tur: $TUR ==="
  echo "başlangıç: $(date '+%Y-%m-%d %H:%M:%S')"
  echo "root     : $ROOT"
  echo "kaynak   : $(git rev-parse --short HEAD 2>/dev/null || echo 'git-yok (ağaç git deposu değil)')"
  echo
} | tee -a "$SUMMARY"

for i in "${!NAMES[@]}"; do
  n="${NAMES[$i]}"
  echo "=== KAPI $((i+1))/7: $n başlıyor ($(date '+%H:%M:%S')) ===" | tee -a "$SUMMARY"
  start=$(date +%s)
  bash -c "${CMDS[$i]}" > "$OUT/$TUR.gate-$((i+1)).log" 2>&1
  rc=$?
  dur=$(( $(date +%s) - start ))
  echo "KAPI $((i+1))/7 $n → rc=$rc (${dur}s)  [ham: $OUT/$TUR.gate-$((i+1)).log]" | tee -a "$SUMMARY"
  if [ "$rc" -ne 0 ]; then
    echo "!!! $n BAŞARISIZ — dur" | tee -a "$SUMMARY"
    tail -30 "$OUT/$TUR.gate-$((i+1)).log" | tee -a "$SUMMARY"
    exit "$rc"
  fi
done

{
  echo
  echo "=== YEDİ KAPI: 7/7 YEŞİL ==="
  echo "bitiş: $(date '+%Y-%m-%d %H:%M:%S')"
} | tee -a "$SUMMARY"
