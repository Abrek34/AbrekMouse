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
# R4-L-1: TOL env'den veya --tolerance bayrağından GELDİĞİNDE parse öncesi
# doğrulama atlanır (--tolerance abc, tanımsız env TOL, ...). Çözüm: değeri
# parse SONRASI yeniden doğrula — geçersiz değer python traceback'e gitmez.
# ORAC-TOL: TOL must be a POSITIVE number.  A leading '-' (or a 0 value) made
# every row exceed the tolerance → spurious whole-run failure.  Only an
# optional '+' sign and an exponent-sign are allowed; `TOL` must also be > 0.
[[ "$TOL" =~ ^\+?([0-9]+\.?[0-9]*|\.?[0-9]+)([eE][+-]?[0-9]+)?$ ]] \
    || { echo "Hata: TOL geçerli bir sayı değil: '$TOL'" >&2; exit 2; }
(( $(echo "$TOL" | LC_ALL=C awk '{ print ($1 > 0) ? 1 : 0 }') == 1 )) \
    || { echo "Hata: TOL pozitif olmalı (0/negatif kabul edilmez): '$TOL'" >&2; exit 2; }

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
"$work/ref_bin"   > "$work/ref.out" || { echo "Hata: reference binary crashed (exit $?)" >&2; exit 1; }
echo "[oracle] running local port ..."
"$work/local_bin" > "$work/local.out" || { echo "Hata: local binary crashed (exit $?)" >&2; exit 1; }

known_set_file="$HERE/known_deviations.txt"

python3 - "$work/ref.out" "$work/local.out" "$TOL" "$VERBOSE" "$known_set_file" "$REPO" <<'PY'
import sys
import math
import os
ref = sys.argv[1]; loc = sys.argv[2]; tol = float(sys.argv[3]); verbose = int(sys.argv[4])
known_file = sys.argv[5]
repo_root = sys.argv[6]

def load(p):
    rows = {}
    for line in open(p):
        name, spd, gain = line.split("\t")
        # Normalize speed to canonical string format (remove trailing zeros, no scientific notation)
        spd_norm = format(float(spd), 'g')
        rows[(name, spd_norm)] = float(gain)
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
        print(f"ERROR: {known_file}:{lineno}: expected '<case>\t<speed>', "
              f"got {len(fields)} field(s): {line!r}")
        sys.exit(1)
    # Normalize speed to canonical string format
    spd_norm = format(float(fields[1]), 'g')
    known.add((fields[0], spd_norm))

if known - r.keys():  # a documented row that the grid never produces
    missing_docs = sorted(known - r.keys())
    print(f"ERROR: {len(missing_docs)} documented deviation(s) do not exist in the grid:")
    for x in missing_docs[:20]: print("  missing  %s\t%s" % x)
    print("  These rows are never generated; fix known_deviations.txt (typo/case/name")
    print("  drift silently inflates the documented count and hides real drift).")
    sys.exit(1)

for (name, spd) in r:
    rv, lv = r[(name, spd)], l[(name, spd)]
    # Finiteness mismatch: one side NaN/Inf, other finite -> genuine deviation
    if math.isfinite(rv) != math.isfinite(lv):
        worst.append((float('inf'), name, spd, rv, lv, (name, spd) in known))
        # Do NOT continue here — the relative check below is left to run for the
        # non-finite halves too, so a row that is BOTH a finiteness mismatch AND
        # past tolerance is still recorded.  Note it cannot double-append for a
        # NaN side: `rel` is then NaN and `NaN > tol` is False (measured), so
        # :117 above is the single record for that case.
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
    # Finiteness mismatch: one side NaN/Inf, other finite -> genuine deviation
    # This MUST be in deviating set to avoid STALE errors for known deviations
    # (like p155_io_cap0_gain where ref=NaN, port=finite).
    if math.isfinite(rv) != math.isfinite(lv):
        deviating.add((name, spd))
        continue
    denom = max(abs(rv), abs(lv), 1e-300)
    rel = abs(rv - lv) / denom
    if rel > strict:
        deviating.add((name, spd))

unknown = [w for w in worst if not w[-1]]
# documented rows that still genuinely deviate at the canonical tolerance
known_cnt = len(known & deviating)

stale = sorted(known - deviating)

# ===== ORACLE COVERAGE CHECK =====
# The oracle grid only tests accel_* gain functions directly via au.apply() / run().
# It does NOT exercise the vector-to-speed layer (speed_processor, lp_distance,
# magnitude, rotate, direction, smoother/EMA, modifier::modify). This check
# verifies that the oracle's coverage declaration matches reality by scanning
# the local.cpp and reference.cpp source files for production function calls.

# Declared coverage — list of production functions the oracle actually exercises.
# If you add a call to a production function in local.cpp or reference.cpp,
# add it here. If a production function is used but NOT listed here, the
# oracle coverage is incomplete and this check will warn.
ORACLE_COVERED = {
    # accel gain functions (all modes)
    "accel_classic", "accel_power", "accel_natural", "accel_jump",
    "accel_synchronous", "accel_lookup", "accel_noaccel",
    "accel_union_apply", "accel_union_init",
}

# Production functions NOT covered by the oracle grid (intentionally).
# These are tested elsewhere (tests/test_accel.cpp, run_simd_parity.sh, etc.).
ORACLE_NOT_COVERED = {
    "modifier_modify", "speed_processor", "lp_distance", "magnitude",
    "rotate", "direction", "smoother_ema", "modifier_init",
    "speed_processor_init", "lp_norm", "lp_distance_impl", "rotate_vec",
    "direction_angle", "magnitude_impl", "hypot",
}

def extract_calls(source_path, patterns):
    """Scan a C++ source file for calls to production functions.
    Returns a set of function NAMES (keys from PATTERNS) that were found.
    """
    import re
    found = set()
    try:
        with open(source_path, 'r') as f:
            content = f.read()
        for key, pattern in patterns.items():
            if re.search(pattern, content):
                found.add(key)  # Add the function NAME (key), not the regex pattern
    except FileNotFoundError:
        pass
    return found

# Patterns to search for in C++ source files
PATTERNS = {
    "modifier_modify": r"modifier.*\.modify\(",
    "speed_processor": r"speed_processor\b",
    "lp_distance": r"lp_distance\(",
    "magnitude": r"\bmagnitude\(",
    "rotate": r"\brotate\(",
    "direction": r"\bdirection\b",
    "smoother_ema": r"smoother\b|ema\b|exp2\(",
    "modifier_init": r"modifier\b.*init",
    "speed_processor_init": r"speed_processor\b.*init",
    "lp_norm": r"\blp_norm\b",
    "lp_distance_impl": r"lp_distance\b",
    "rotate_vec": r"rotate.*vec|vec.*rotate",
    "direction_angle": r"direction\b",
    "magnitude_impl": r"magnitude\(",
    "hypot": r"std::hypot\(",
    "accel_union_apply": r"accel_union.*apply|au\.apply\(",
    "accel_union_init": r"accel_union.*init|au\.init\(",
}

# Scan both oracle binaries' source files
local_cpp = os.path.join(repo_root, "tests", "oracle", "local.cpp")
reference_cpp = os.path.join(repo_root, "tests", "oracle", "reference.cpp")

found_calls = set()
for cpp_file in [local_cpp, reference_cpp]:
    found_calls |= extract_calls(cpp_file, PATTERNS)

# Check for unlisted covered functions
# FATAL, not a warning: the run below ends in "RESULT: OK — local port matches
# official reference ... on every row", and that claim is an attribution claim.
# A production function the oracle actually called but never declared makes the
# grid's coverage unstated, so OK would be printed for a scope nobody wrote down.
# A warning here left the gate green while saying the same thing (measured).
unlisted_covered = found_calls - ORACLE_COVERED
if unlisted_covered:
    print(f"ERROR: {len(unlisted_covered)} unlisted production function call(s):")
    for fn in sorted(unlisted_covered):
        print(f"  unlisted  {fn}")
    print("  The oracle called these but ORACLE_COVERED does not declare them, so")
    print("  the coverage behind 'RESULT: OK' is unaccounted for. Add them to")
    print("  ORACLE_COVERED if intentional (then re-run) — do not ignore.")
    sys.exit(1)

# Check for a self-contradictory coverage declaration.
# This was `pass` — the loop body computed nothing and the set stayed empty, so
# the branch was unreachable dead code.  The condition that is actually
# reachable here is the OVERLAP: a name in both lists is by definition not
# "unlisted", so the check above cannot see it, and it makes one name both
# covered and not covered.  Fatal.
#
# Note on the original "called but declared NOT covered" formulation: it is
# strictly subsumed.  If a name is called and declared NOT covered, it is
# caught by the unlisted check above unless it is also in ORACLE_COVERED — and
# then it is an overlap, which is this check.  So it is reported here as extra
# detail on the overlapping names, not as a second independent guard.
both_lists = ORACLE_COVERED & ORACLE_NOT_COVERED
if both_lists:
    print(f"ERROR: {len(both_lists)} name(s) declared COVERED *and* NOT COVERED:")
    for fn in sorted(both_lists):
        tag = " (and the grid calls it)" if fn in found_calls else " (not called)"
        print(f"  contradicted  {fn}{tag}")
    print("  ORACLE_COVERED and ORACLE_NOT_COVERED must be disjoint; a name in")
    print("  both makes the coverage declaration self-contradictory. Move it to")
    print("  one list, then re-run.")
    sys.exit(1)

# Report coverage summary
if verbose:
    # found_calls ⊆ ORACLE_COVERED is guaranteed by the fatal check above, so
    # this prints the verified fact rather than a re-derived guess.
    print(f"  Oracle coverage scan: found {len(found_calls)} production function "
          f"patterns, all declared in ORACLE_COVERED")
    for fn in sorted(found_calls):
        print(f"  {fn}: COVERED")

# ===== END ORACLE COVERAGE CHECK =====

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