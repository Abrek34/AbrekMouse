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

# ── Envanter denetimi (derlemeden once, hizli basarisiz) ─────────────────────
# Bu kapı YALNIZCA simd_parity.cpp'de yazilmis v2d_* cagrilarini karsilastirir.
# simd_math.hpp'ye yeni bir v2d_* ekleyip kapida cagirmazsan kapi YESIL kalir —
# ve o fonksiyonun uc backend'i sessizce ayrisabilir. Orijinal AVX2 Y-ekseni
# hatasi tam olarak bu yoldan gecti: hatayi yazan kapiya girdi, onu kullanmayan
# yeni bir yol ekledi.
#
# Buradaki kural her v2d_* fonksiyonunu UC kategoriden birine sokar:
#   1) KAPSAMLI  — simd_parity.cpp'de koda gorunuyor (yorum saymaz)
#   2) CANLI     — simd_math.hpp ve tests/ disinda cagri yeri var  → 1 olmali
#   3) OLU       — ne kapsamli ne canli; o zaman acikca beyan edilmeli
# Bir fonksiyonun hem CANLI hem kapisiz olmasi KAPIDIR (kirilmis sozlesme).
BILINEN_OLUMLER="v2d_cmp_ge v2d_cmp_gt v2d_cmp_le v2d_cmp_lt v2d_direction v2d_fast_exp v2d_fast_exp2 v2d_fast_pow v2d_rotate v2d_sub"

python3 - "$ROOT" "$BILINEN_OLUMLER" <<'PYEOF'
import re, sys, pathlib
root = pathlib.Path(sys.argv[1])
olum_ilan = set(sys.argv[2].split())

def koddan_yorum_cikar(s):
    out, i, n = [], 0, len(s)
    while i < n:
        if s.startswith("//", i):
            j = s.find("\n", i); i = n if j < 0 else j
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2); i = n if j < 0 else j + 2
        else:
            out.append(s[i]); i += 1
    return "".join(out)

def v2d_adlari(kod):
    return set(re.findall(r'\b(v2d_[a-z0-9_]+)\s*\(', kod))

hdr_kod = koddan_yorum_cikar((root / "include/simd_math.hpp").read_text())
par_kod = koddan_yorum_cikar((root / "tests/simd_parity.cpp").read_text())
tanimli = v2d_adlari(hdr_kod)
kapsamli = v2d_adlari(par_kod) & tanimli

# Uretim cagri yerleri: simd_math.hpp'nin kendi tanimlari ve tests/ HARIC.
canli = set()
for d in ("include", "src", "daemon", "cli", "gui"):
    for p in (root / d).rglob("*"):
        if p.suffix not in (".cpp", ".hpp", ".inl"): continue
        if p.name == "simd_math.hpp": continue
        for f in v2d_adlari(koddan_yorum_cikar(p.read_text(errors="replace"))):
            canli.add(f)
canli &= tanimli

hata = []
for f in sorted(canli - kapsamli):
    hata.append(f"CANLI ama kapısız: {f} — üretimde çağrılıyor, simd_parity.cpp'de yok")
for f in sorted(tanimli - kapsamli - canli - olum_ilan):
    hata.append(f"SINIFLANDIRILMAMIŞ: {f} — ne kapsamlı ne canlı; ya kapıya ekle ya da BILINEN_OLUMLER'e al")
for f in sorted(olum_ilan - tanimli):
    hata.append(f"BAYAT liste girdisi: {f} — simd_math.hpp'te artık tanımlı değil, listeden çıkar")
for f in sorted(olum_ilan & kapsamli):
    hata.append(f"BAYAT liste girdisi: {f} — artık kapıda kapsanıyor, listeden çıkar")
for f in sorted(olum_ilan & canli):
    hata.append(f"BAYAT liste girdisi: {f} — artık üretimde çağrılıyor; ya kapıya ekle ya da sil")

print(f"    envanter: {len(tanimli)} v2d_* · kapsamlı {len(kapsamli)} · "
      f"canlı {len(canli)} · beyan edilmiş ölü {len(olum_ilan & tanimli)}")
if hata:
    print("    !!! ENVANTER İHLALİ:")
    for h in hata: print(f"        {h}")
    sys.exit(1)
print("    envanter denetimi: OK (her v2d_* kapsamlı, canlı ya da beyan edilmiş ölü)")
PYEOF
envanter_rc=$?
if [ "$envanter_rc" -ne 0 ]; then
    echo
    echo "Sonuç: FAIL — v2d_* envanter denetimi (bkz. yukarı)"
    exit 1
fi

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
