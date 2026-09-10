#!/usr/bin/env bash
# P138 — oracle_perf cross-check runner.
#
# Builds the SAME micro-benchmark (oracle_perf.cpp) twice — once against the
# LOCAL port (include/), once against the vendored OFFICIAL reference
# (tests/oracle/ref/) — and prints the per-case mean ns/apply for each side.
#
# Usage: bash tests/oracle/run_oracle_perf.sh [REPS]   (default REPS=5)
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
REF_DIR="$HERE/ref"
CXX="${CXX:-g++}"
REPS="${1:-5}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "[perf] building LOCAL side ..."
"$CXX" -std=c++20 -O3 -march=native -DORACLE_PERF_LOCAL \
    -I "$REPO" "$HERE/oracle_perf.cpp" -o "$work/perf_local" || exit 1

echo "[perf] building REF side ..."
"$CXX" -std=c++20 -O3 -march=native -DORACLE_PERF_REF -fpermissive \
    -Wno-changes-meaning -I "$REF_DIR" -include "$REF_DIR/refcompat.hpp" \
    "$HERE/oracle_perf.cpp" -o "$work/perf_ref" || exit 1

echo "[perf] running local ..."
"$work/perf_local" "$REPS" > "$work/local.out"
echo "[perf] running ref ..."
"$work/perf_ref"   "$REPS" > "$work/ref.out"

echo
echo "=== LOCAL (include/ port) ==="
cat "$work/local.out"
echo
echo "=== REF (vendored official) ==="
cat "$work/ref.out"
echo

# final comparison
lt=$(grep '^TOTAL' "$work/local.out" | sed 's/.*mean_ns=\([0-9.]*\).*/\1/')
rt=$(grep '^TOTAL' "$work/ref.out"   | sed 's/.*mean_ns=\([0-9.]*\).*/\1/')
ls=$(grep '^TOTAL' "$work/local.out" | sed 's/.*mean_cyc=\([0-9]*\).*/\1/')
rs=$(grep '^TOTAL' "$work/ref.out"   | sed 's/.*mean_cyc=\([0-9]*\).*/\1/')
echo "=== COMPARE: local total ns/apply = $lt ($ls cyc) | ref total ns/apply = $rt ($rs cyc)"
python3 - "$lt" "$rt" <<'PY'
import sys
l = float(sys.argv[1]); r = float(sys.argv[2])
if l > 0:
    print(f"local/ref ratio = {l/r:.3f}  (local is { 'slower' if l>r else 'faster' } than official ref by {abs(l/r-1)*100:.1f}%)")
PY