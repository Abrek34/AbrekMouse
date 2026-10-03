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
#
# These flags select the backend ONLY.  They are deliberately NOT the shipping
# flag set: scripts/build.sh:92 adds -mfma (and build.sh:87 -O3 -march=native),
# and comparing this gate's AVX2 build against the shipping one is NOT the same
# question this gate answers.  See the -mfma note above BILINEN_OLUMLER below.
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

# OLU listesi tek duz bir sey DEGIL; iki farkli karari tasiyor ve kararin
# gerekcesi farkli.  Ilk gercek cagri geldiginde hangisinin "beklenen"
# hangisinin "sürpriz" oldugunu bilmek icin ayrim yazildi.
#
#   (A) CANLI OZELLIGIN BAGLANMAMIS SIMD IKIZI — silme.
#       rotate()/direction() SKALER hali math-vec2.hpp:66/:70'te yasiyor ve
#       canli: rawaccel.hpp:329, :357, :508.  Bu ikizler o ozelligin SIMD
#       surumu; baglanmadiklari icin oluler, silinirlerse ozelligin SIMD
#       yolu sessizce kaybolur.
#   (B) KARSILIGI OLMAYAN SPEKULATIF — istege bagli.
#       Bunlarin canli bir skaler karsiliklari yok; ilk cagri geldiginde
#       hangi sozlesmenin dogru oldugu OLCULEMEYE calisilir.
DEAD_TWINS="v2d_direction v2d_rotate"          # (A) canli ozelligin ikizi
DEAD_SPECULATIVE="v2d_cmp_ge v2d_cmp_gt v2d_cmp_le v2d_cmp_lt v2d_sub v2d_fast_exp v2d_fast_exp2 v2d_fast_pow"  # (B)
# (A) ve (B) birlikte tam olarak BILINEN_OLUMLER olmali; ayrim duzeni degil,
# karsi lastigi tutmaz.  Asagida denetlenir.

# -mfma NOTU (olculerek yazildi, tahmin degil)
# ------------------------------------------
# Bu kapi -O2 -mavx2 derler; sevkiyat build.sh:92'de -mfma da ekler.  Yani
# kapi, sevkiyatin DERLEMEDIGI bir AVX2 ikilisini karsilastiriyor.  Olcum:
# simd_math.hpp'teki 15 disa vurdugu fonksiyonun tamami, 40x40 girdi cifti
# uzerinde bit-bit karsilastirildi (16539 vaka).
#
#   kapi (-O2 -mavx2)  vs  sevkiyat (-O3 -march=native ... -mfma) : 140 ayrisma
#   kapi (-O2 -mavx2)  vs  -O2 -mavx2 -mfma  (yalnizca FMA)        : 140 ayrisma
#   kapi (-O2 -mavx2)  vs  -O3 -mavx2         (yalnizca -O3)       :   0 ayrisma
#
# Yani suclu -O3 DEGIL, -mfma: 140 ayrismanin TAMAMI v2d_rotate'da, diger
# 14 fonksiyonda sifir.  Sebep fiziksel: FMA a*b+c'yi tek yuvarlamada yapar,
# ara cifti korur; iki ayri yuvarlamada ayni ifade bakiye vermez.  v2d_rotate
# tam olarak x*cx - vy*sy + x*sy + vy*cx yaziyor (simd_math.hpp:174-186).
#
# !! BU YORUMUN KAPSAMI: YUKARDAKI SAYILAR YALNIZCA simd_math.hpp'in IZOLE
# YUZEYI icindir.  "Bilesik/canli yolda da ayni sonuc" DENILMEZ -- ve bir
# surekle de denilmemisti.  AJ2'nin olcumu (ve benden bagimsiz teyit) canli
# yolda SAPMA OLDUGUNU gosteriyor:
#
#   tests/oracle 1119 vaka, %.17g (bit duzeyine inen cozunurluk):
#     kapi (-O2 -mavx2)  vs  sevkiyat (-O3 -mfma)  : 6 satIR farkli
#     kapi (-O2 -mavx2)  vs  -O2 -mavx2 -mfma     : 6 satir  (-O3 degil, FMA)
#     kapi (-O2 -mavx2)  vs  -O3 -mavx2 (FMA'siz) : 0 satir
#
#   6 vakanin HEPSI 'natural' profili (canli: accel-union.hpp:18/34) ve
#   known_deviations.txt'te YOK:
#     game_office_natural  spd=0.001  350 ULP  rel 7.77e-14   <- 3 mertebe ayri
#     game_office_natural  spd=0.005   10 ULP  rel 2.22e-15
#     game_office_natural  spd=0.01     5 ULP  rel 1.11e-15
#     game_office_natural  spd=0.1      4 ULP  rel 8.85e-16
#     game_valorant_natural spd=0.1     2 ULP  rel 4.43e-16
#     game_valorant_natural spd=15      1 ULP  rel 1.81e-16
#
# Buradaki 140/16539 ile oradaki 6/1119 birbirine ZIT degil, FARKLI cozunurluk.
# onceki kayit bu ayrimi yapmadan "sinif olu kodda kalmis" diyordu; AJ2'nin
# geri cekmesiyle duzeltildi.  Ders: bir olcumun kapsamini yazmadan
# "temiz" demek, o olcumun sessizce genisletilmesidir.
#
# run_oracle.sh BU SAPMAYI iki bagimsiz sebepten GOREMEZ:
#   1) -O1 derliyor (run_oracle.sh:52) -- FMA hic derlenmiyor
#   2) %.9g basiyor (local.cpp:61, reference.cpp:81) -- 9 basamak, oluan fark
#      7.77e-14, yani cozunurlugun 1e-9'unun 4 mertebe ALTINDA.  %.9g en kotu
#      4.5 milyon ULP yutar (1 ULP = 2.22e-16).  Kanit: 0.1 ile 0.1+1e-12
#      %.9g ile AYNI metin, %.17g ile AYRI metin.
# Yani kapinin "bu derlemede sapma yok" cumlesi "bu derlemede" demek; sevkiyat
# derlemesi hic denenmemis oldugu icin bu bir KAPSAM boşlugu.
#
# Neden 4. bir backend eklenmedi: bu kapi bit-bit karsilastirir ve bu degeri.
# avx2 ile avx2-fma'yi toleransli karsilastirmak abs(-0)=+0 farkini (1 ULP)
# yutar -- yani v2d_abs sinifini KORLEMEZDI.  Ayrica olculerek kapatildi: en
# buyuk sapma 7.77e-14, oracle'in TOL=1e-9 toleransinin 4 mertebe altinda, yani
# avx2-fma backend'i 350 ULP'lik vakayi da GECIRIRDI -- gordugu sey yok, maliyeti
# var.  Ayni kaynagin iki derlemesini karsilastirmak bu kapinin isi degil: kapi
# UC AYRI ELLE YAZILMIS uygulamayi karsilastirir.  Asil onlem bayragin TEK
# kaynaktan gelmesi (SIMD_FLAGS) ve oracle cozunurlugunun yukseltilmesi.

python3 - "$ROOT" "$BILINEN_OLUMLER" "$DEAD_TWINS" "$DEAD_SPECULATIVE" <<'PYEOF'
import re, sys, pathlib
root = pathlib.Path(sys.argv[1])
olum_ilan = set(sys.argv[2].split())
# Olu listesinin IKI KATEGORISI. Ikisi de kayittan ayrilir: (A) canli bir
# ozelligin baglanmamis SIMD ikizi, (B) karsiligi olmayan spekulatif.  Bir
# fonksiyon iki listeye de girerse ya da hicbirine girmese BIRIMI KIRILMISTIR
# bir kayittir — o zaman karar sahibi listede yazmadan bu kapi kirmizi verir.
ikiz = set(sys.argv[3].split())
spekulatif = set(sys.argv[4].split())

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
# Iki kategorinin birbirini tutmasi: A ∪ B tam olarak BILINEN_OLUMLER olmali.
for f in sorted(ikiz & spekulatif):
    hata.append(f"ÇİFTE KATEGORİ: {f} — hem (A) canlı özelliğin ikizi hem (B) spekülatif; karar tektir")
for f in sorted(ikiz | spekulatif):
    if f not in olum_ilan:
        hata.append(f"KATEGORİSİZ ÖLÜ: {f} — (A) veya (B) listesinde ama BILINEN_OLUMLER'de yok")
for f in sorted(olum_ilan - (ikiz | spekulatif)):
    hata.append(f"SONRASI BELİRTİLMEMİŞ ÖLÜ: {f} — BILINEN_OLUMLER'de ama (A)/(B) listesinde değil; silinsin mi kalsın mı kararı yaz")

print(f"    envanter: {len(tanimli)} v2d_* · kapsamlı {len(kapsamli)} · "
      f"canlı {len(canli)} · beyan edilmiş ölü {len(olum_ilan & tanimli)} "
      f"(A canlı-özellik-ikizi {len(ikiz & tanimli)} · B spekülatif {len(spekulatif & tanimli)})")
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
# L04-05: the probe used to only check COMPILATION.  On a host whose CPU
# cannot execute AVX2 (pre-Haswell, masked by hypervisor/OS) the probe
# succeeded, the avx2 binary then died with SIGILL, and the "graceful skip"
# contract (exit 77) was unreachable — the user saw a bare "TEST BAŞARISIZ".
# Compile AND run the probe so an unrunnable AVX2 host is classified skip.
have_avx2=1
if ! echo 'int main(){return 0;}' | $CXX $STD -mavx2 -x c++ - -o "$TMP/avx2probe" 2>/dev/null; then
    have_avx2=0
elif ! "$TMP/avx2probe" >/dev/null 2>&1; then
    have_avx2=0   # compiles but the CPU cannot execute it (SIGILL)
fi

outputs=()
ran=0
failed=0
avx2_ran=0

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
    [ "$label" = "avx2" ] && avx2_ran=1
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

# L04-06: the SSE2+scalar comparison passing is NOT the full contract — the
# AVX2 path (the one the shipping -march=native build executes) was NOT
# measured on this host.  Exiting 0 here used to make run_tests.sh print a
# plain green "N/N geçti" with no skip notice (the 77 branch never fired), so
# "loudly skipped, never quietly passed" held only on hosts where the binary
# could not even be built.  Emit the documented 77 so the caller shows the
# DİKKAT line.
if [ "$avx2_ran" -eq 0 ]; then
    echo
    echo "DİKKAT: AVX2 backend bu konakta çalıştırılamadı — yalnız $ran backend"
    echo "        karşılaştırıldı.  Üretimde çalışan AVX2 yolu BU KOŞUDA denenmedi."
    echo "Sonuç: PASS (kısmi) — AVX2 atlandı"
    exit 77
fi

echo
echo "Sonuç: PASS — $ran backend test edildi ve birebir aynı sonucu üretti"
exit 0
