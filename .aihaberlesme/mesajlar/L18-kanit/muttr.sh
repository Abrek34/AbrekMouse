#!/bin/bash
# L18 — tr_coverage gate mutation harness (fast). Spec: TSV file OLD NEW desc
set -u
BASE=/home/a/.l18-audit
SRC="$BASE/tree"; WORK="$BASE/trwork"; BIN="$BASE/tr_mut"
LABEL="${1:-?}"; EXPRS="${2:-}"
rm -rf "$WORK"; cp -a "$SRC" "$WORK"
echo "MUT[$LABEL]"
python3 "$BASE/apply_mut.py" "$WORK" "$EXPRS" || { rm -rf "$WORK"; exit 9; }
cd "$WORK"
g++ -std=c++20 -O0 -w -o "$BIN" tests/tr_coverage.cpp 2>"$WORK/cc.err" || {
  echo "    BUILD-FAIL"; grep -m3 error "$WORK/cc.err" | sed 's/^/      /'; rm -rf "$WORK"; exit 8; }
out=$(bash tests/run_tr_coverage.sh 2>&1); rc=$?
sum=$(printf '%s\n' "$out" | grep -E 'dictionary:|unique strings|dynamic \(skipped\)|Result:|MISSING TRANSLATIONS' | tr '\n' '~')
sum=${sum//\~/| }
if [ $rc -eq 0 ]; then v="!! YESIL KALDI (gate YAKALAMADI)"; else v="OK  -> KIRMIZI (yakaladi)"; fi
echo "    gate_rc=$rc  $v"
echo "    $sum"
rm -rf "$WORK"
exit 0
