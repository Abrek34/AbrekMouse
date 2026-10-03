#!/bin/bash
# L18 mutation harness — per-TU object cache keyed on TU + transitive headers.
# Mutation spec: TSV  <relpath>\t<OLD>\t<NEW>\t<desc>
set -u
BASE=/home/a/.l18-audit
SRC="$BASE/tree"; WORK="$BASE/work"; CACHE="$BASE/ocache"; BIN="$BASE/mut_test_accel"
LABEL="${1:-?}"; EXPRS="${2:-}"; FILTER="${3:-.}"
mkdir -p "$CACHE"; rm -rf "$WORK"; cp -a "$SRC" "$WORK"
echo "MUT[$LABEL]"
python3 "$BASE/apply_mut.py" "$WORK" "$EXPRS" || { rm -rf "$WORK"; exit 9; }
cd "$WORK"
key_of () {
  local tu="$1" dep="$CACHE/$(echo "$1" | tr '/' '_').d"
  g++ -std=c++20 -Iinclude -Isrc -MM -MF "$dep" "$tu" 2>/dev/null
  { md5sum "$tu"; grep -oE '[^ \\]+' "$dep" | sort -u | while read -r h; do
      [ -f "$h" ] && md5sum "$h"; done; } | md5sum | cut -d' ' -f1
}
OBJS=()
for tu in tests/test_accel.cpp src/config.cpp src/logitech_receiver.cpp src/logitech_hidpp.cpp; do
  k=$(key_of "$tu"); o="$CACHE/$(echo "$tu" | tr '/' '_').$k.o"
  if [ ! -f "$o" ]; then
    g++ -std=c++20 -O0 -w -Iinclude -Isrc -c "$tu" -o "$o" 2>"$WORK/e.$(basename $tu)" || {
      echo "    BUILD-FAIL [$tu]"; grep -m4 error "$WORK/e.$(basename $tu)" | sed 's/^/      /'
      rm -rf "$WORK"; exit 8; }
  fi
  OBJS+=("$o")
done
g++ -O0 -w "${OBJS[@]}" -lpthread -o "$BIN" || { echo "    LINK-FAIL"; rm -rf "$WORK"; exit 8; }
export TMPDIR=$(mktemp -d)
out=$("$BIN" --filter "$FILTER" --quiet 2>&1); rc=$?
summ=$(printf '%s\n' "$out" | grep -E '=== Sonuç' | tail -1)
nfail=$(printf '%s\n' "$out" | grep -c 'FAIL ')
if [ $rc -eq 0 ]; then v="!! YESIL KALDI (test YAKALAMADI)"; else v="OK  -> KIRMIZI (yakaladi)"; fi
echo "    rc=$rc  $v"
echo "    $summ  (FAIL satiri: $nfail)"
printf '%s\n' "$out" | grep FAIL | head -3 | sed 's/^/      /'
rm -rf "$WORK"
exit 0
