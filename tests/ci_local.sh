#!/usr/bin/env bash
# ci_local.sh — yedi kapıyı tek seferde koşturur, logları arşivler.
#
# Çıkış kodları (tartışmasız):
#   0  = PASS  (yedi kapının tamamı yeşil)
#   1  = FAIL  (en az bir kapı gerçek hatayla düştü)
#   77 = ortam atlaması (kapılardan en az biri 77 döndü, gerçek hata yok)
#
# Not: AGENTS.md'ye göre atlanan kapı "yeşil" sayılmaz; bu yüzden yalnızca
# 77'li bir koşuda RESULT: FAIL yazılır ama exit kodu 77 olur ki otomasyon
# bunun bir kod hatası değil ortam sorunu olduğunu ayırt edebilsin.
#
# Uyumluluk: docs/ci_billing_note.md — billing kilidi açıkken zorunlu kanal
# yerel ci_local.sh + artifact tar.gz'dir.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TS="$(date +%Y%m%d-%H%M%S)"
LOGDIR="logs/ci_local_${TS}"
mkdir -p "$LOGDIR"

GATES=(
  "1|build|scripts/build.sh"
  "2|run_tests|tests/run_tests.sh"
  "3|oracle|tests/oracle/run_oracle.sh"
  "4|simd_parity|tests/run_simd_parity.sh"
  "5|tr_coverage|tests/run_tr_coverage.sh"
  "6|cli_sanitized|tests/run_cli_sanitized.sh"
  "7|tracker_bridge|tests/run_tracker_bridge.sh"
)

fail=0
skip=0
declare -a SUMMARY=()

for entry in "${GATES[@]}"; do
  IFS='|' read -r n name cmd <<< "$entry"
  log="$LOGDIR/gate_${n}.log"
  echo "== [$n/7] $name: bash $cmd =="
  bash "$cmd" > "$log" 2>&1
  rc=$?
  case "$rc" in
    0)  st="PASS" ;;
    77) st="SKIP(77)"; skip=1 ;;
    *)  st="FAIL(rc=$rc)"; fail=1 ;;
  esac
  SUMMARY+=("gate_$n $name: $st")
  echo "   -> $st  (log: $log)"
  if [ "$rc" -ne 0 ]; then
    tail -n 5 "$log" | sed 's/^/   | /'
  fi
done

ARTIFACT="logs/ci_local_${TS}.tgz"
tar czf "$ARTIFACT" -C logs "ci_local_${TS}"
echo "artifact: $ARTIFACT"

echo "---- özet ----"
for line in "${SUMMARY[@]}"; do echo "  $line"; done

if [ "$fail" -eq 1 ]; then
  echo "RESULT: FAIL"
  exit 1
elif [ "$skip" -eq 1 ]; then
  echo "(kapi atlandi: ortam, kod hatasi degil)"
  echo "RESULT: FAIL"
  exit 77
else
  echo "RESULT: PASS"
  exit 0
fi
