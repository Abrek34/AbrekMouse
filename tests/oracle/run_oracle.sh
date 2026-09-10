#!/usr/bin/env bash
# T13 — Differential oracle runner.
#
# Builds and runs the OFFICIAL RawAccel reference (vendored under ref/) and the
# LOCAL C++ port over the shared parameter grid, then compares every row with a
# tolerance on gain. Exits non-zero iff any row differs beyond tolerance.
#
# Usage:
#   bash tests/oracle/run_oracle.sh [--verbose] [--tolerance REL]
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
REF_DIR="$HERE/ref"
CXX="${CXX:-g++}"
STD="-std=c++20"

TOL="${TOL:-1e-9}"   # relative tolerance on gain (identical math ⇒ ~1e-15)
VERBOSE=0
# P114 BUG-E: --tolerance <val> flag'i gerçekten parse ediliyor (TOL env korunur).
while [ $# -gt 0 ]; do
    case "$1" in
        --verbose)   VERBOSE=1 ;;
        --tolerance) if [ $# -lt 2 ]; then
            echo "Hata: --tolerance <val> sayısal değer gerekli" >&2
            exit 2
        fi
        TOL="$2" ; shift ;;
        *)
            echo "Hata: bilinmeyen argüman '$1' (kullanım: --verbose | --tolerance REL)" >&2
            exit 2 ;;
    esac
    shift
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# My machine is x86-64; drop -march/-O3 to keep the diff pure algorithm.
# $CXX onurlandırılır (P114 BUG-D).
echo "[oracle] compiling official reference ..."
"$CXX" $STD -O1 -fpermissive -Wno-changes-meaning \
    -I "$REF_DIR" -include "$REF_DIR/refcompat.hpp" \
    "$HERE/reference.cpp" -o "$work/ref_bin" || exit 1

echo "[oracle] compiling local port ..."
"$CXX" $STD -O1 -I "$REPO" \
    "$HERE/local.cpp" -o "$work/local_bin" || exit 1

echo "[oracle] running reference ..."
"$work/ref_bin"   > "$work/ref.out"
echo "[oracle] running local port ..."
"$work/local_bin" > "$work/local.out"

known_set_file="$HERE/known_deviations.txt"

python3 - "$work/ref.out" "$work/local.out" "$TOL" "$VERBOSE" "$known_set_file" <<'PY'
import sys
ref = sys.argv[1]; loc = sys.argv[2]; tol = float(sys.argv[3]); verbose = int(sys.argv[4])
known_file = sys.argv[5]

def load(p):
    rows = {}
    for line in open(p):
        name, spd, gain = line.split("\t")
        rows[(name, spd)] = float(gain)
    return rows

r, l = load(ref), load(loc)
if r.keys() != l.keys():
    onlyr = r.keys() - l.keys(); onlyl = l.keys() - r.keys()
    print(f"ERROR: row-set mismatch (ref-only={len(onlyr)}, local-only={len(onlyl)})")
    for x in list(onlyr)[:5]: print("  ref-only:", x)
    for x in list(onlyl)[:5]: print("  local-only:", x)
    sys.exit(1)

worst = []   # (rel_err, name, spd, ref, loc)
known = set()
for lineno, raw in enumerate(open(known_file), 1):
    line = raw.strip()
    if not line or line.startswith("#"):
        continue
    fields = line.split("\t")
    if len(fields) != 2:
        print(f"ERROR: {known_file}:{lineno}: expected '<case>\\t<speed>', "
              f"got {len(fields)} field(s): {line!r}")
        sys.exit(1)
    known.add((fields[0], fields[1]))

if known - r.keys():  # a documented row that the grid never produces
    missing_docs = sorted(known - r.keys())
    print(f"ERROR: {len(missing_docs)} documented deviation(s) do not exist in the grid:")
    for x in missing_docs[:20]: print("  missing  %s\t%s" % x)
    print("  These rows are never generated; fix known_deviations.txt (typo/case/name")
    print("  drift silently inflates the documented count and hides real drift).")
    sys.exit(1)

for (name, spd) in r:
    rv, lv = r[(name, spd)], l[(name, spd)]
    denom = max(abs(rv), abs(lv), 1e-300)
    rel = abs(rv - lv) / denom
    if rel > tol:
        worst.append((rel, name, spd, rv, lv, (name, spd) in known))

# P114 BUG-C: STALE/known-integrity bookkeeping runs at the CANONICAL strict
# tolerance (1e-9) regardless of the user's TOL. `deviating` decides whether a
# documented deviation "really deviates today" — loosening TOL (or a perfectly
# clean run) must NOT flag every documented row as a stale lie and hard-fail a
# green run. The user's TOL above governs only *unknown* row drift below.
strict = 1e-9
deviating = set()
for (name, spd) in r:
    rv, lv = r[(name, spd)], l[(name, spd)]
    denom = max(abs(rv), abs(lv), 1e-300)
    rel = abs(rv - lv) / denom
    if rel > strict:
        deviating.add((name, spd))

unknown = [w for w in worst if not w[-1]]
# documented rows that still genuinely deviate at the canonical tolerance
known_cnt = len(known & deviating)

stale = sorted(known - deviating)

print(f"total rows compared : {len(r)}")
print(f"documented deviations: {len(known)} (known_deviations.txt)")
print(f"known deviations seen: {known_cnt} (genuinely drifting beyond canonical tolerance)")

if stale:
    print(f"ERROR: {len(stale)} documented deviation(s) are NO LONGER deviations:")
    for x in stale[:20]: print("  stale  %s\t%s" % x)
    print("  Update known_deviations.txt — a stale entry is masking a fix (or a")
    print("  typo) and the run must not report OK while the doc lies.")
    sys.exit(1)

if not unknown:
    print(f"RESULT: OK — local port matches official reference (rel tol {tol:g}) "
          f"on every row outside the documented deviations ({len(known)} rows).")
    sys.exit(0)

unknown.sort(reverse=True)
print(f"RESULT: DRIFT — {len(unknown)} UNKNOWN mismatched rows (of {len(r)})")
print("\nunknown worst 12:")
for rel, name, spd, rv, lv, _ in unknown[:12]:
    print(f"  rel={rel:.3e}  {name}  spd={spd:>8}  ref={rv:.9g}  local={lv:.9g}")
if verbose:
    print("\nall unknown mismatches:")
    for rel, name, spd, rv, lv, _ in unknown:
        print(f"  rel={rel:.3e}  {name}  spd={spd:>8}  ref={rv:.9g}  local={lv:.9g}")
sys.exit(1)
PY