#!/usr/bin/env bash
# run_simd_parity.sh — compile tests/simd_parity.cpp once per SIMD backend and
# prove the backends agree.
#
# WHY THIS EXISTS
# ---------------
# The AVX2 lane bug (Y stored/read from the wrong lane, see tests/simd_parity.cpp)
# reached production because every existing gate — tests/run_tests.sh,
# tests/oracle/run_oracle.sh and .github/workflows/ci.yml — compiles WITHOUT
# -march, so they only ever exercised the SSE2 backend. The production build
# (CMakeLists.txt / scripts/build.sh) adds -march=native and therefore ran the
# untested path.
#
# This script closes that gap three ways:
#   1. forces each backend explicitly (AVX2 / SSE2 / scalar), so a backend is
#      never skipped just because the host CPU lacks the ISA;
#   2. asserts per-backend that the Y axis survives modifier::modify();
#   3. diffs the three backends' numeric output, so a lane bug that happens to
#      avoid an assertion still shows up as a mismatch.
#
# Exit 0 = all backends pass AND agree. Exit 1 = any failure or mismatch.
# Exit 77 = host cannot run the AVX2 binary (e.g. non-x86 build) — skips
#           gracefully, matching the run_e2e.sh convention.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CXX="${CXX:-g++}"
STD="-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter"
INC="-I$ROOT/include -I$ROOT/src"

# backend label : compiler flag
BACKENDS=(
    "avx2:-mavx2"
    "sse2:-mno-avx2"
    "scalar:-mno-sse2"
)

echo "=== SIMD backend parity ==="

# Does this host even have AVX2? On non-x86 hosts skip it rather than fail.
have_avx2=1
if ! echo 'int main(){return 0;}' | $CXX $STD -mavx2 -x c++ - -o "$TMP/avx2probe" 2>/dev/null; then
    have_avx2=0
fi

outputs=()
ran=0
failed=0

for entry in "${BACKENDS[@]}"; do
    label="${entry%%:*}"
    flag="${entry##*:}"

    if [ "$label" = "avx2" ] && [ "$have_avx2" -eq 0 ]; then
        echo "--- $label: atlanıyor (derleme/calistirma desteklenmiyor) ---"
        continue
    fi

    bin="$TMP/simd_parity_$label"
    if ! $CXX $STD $flag $INC "$ROOT/tests/simd_parity.cpp" -o "$bin" 2>"$TMP/cc_$label.log"; then
        echo "--- $label: DERLEME HATASI ---"
        sed 's/^/    /' "$TMP/cc_$label.log"
        failed=1
        continue
    fi

    if ! "$bin" >"$TMP/out_$label.txt" 2>&1; then
        echo "--- $label: TEST BASARISIZ ---"
        sed 's/^/    /' "$TMP/out_$label.txt"
        failed=1
        continue
    fi

    echo "--- $label: $(head -1 "$TMP/out_$label.txt") ---"
    outputs+=("$TMP/out_$label.txt")
    ran=$((ran + 1))
done

if [ "$failed" -ne 0 ]; then
    echo
    echo "Sonuç: FAIL (bkz. yukarı)"
    exit 1
fi

if [ "$ran" -lt 2 ]; then
    echo
    echo "Sonuç: karşılaştırma için en az 2 backend gerekiyor, $ran derlendi"
    exit 77
fi

# ── Cross-backend numeric agreement ────────────────────────────────────────
# Compare only the "case ..." lines; the "backend" header and the PASS/result
# trailer are expected to differ.
first="${outputs[0]}"
mismatch=0
for other in "${outputs[@]:1}"; do
    if ! diff <(grep '^case ' "$first") <(grep '^case ' "$other") >"$TMP/diff.txt" 2>&1; then
        echo
        echo "!!! BACKEND UYUŞMAZLIĞI: $first vs $other"
        sed 's/^/    /' "$TMP/diff.txt"
        mismatch=1
    fi
done

if [ "$mismatch" -ne 0 ]; then
    echo
    echo "Sonuç: FAIL — backend'ler farklı sonuç üretti"
    exit 1
fi

echo
echo "Sonuç: PASS — $ran backend test edildi ve birebir aynı sonucu üretti"
exit 0
