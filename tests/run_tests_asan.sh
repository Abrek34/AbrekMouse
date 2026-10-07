#!/bin/bash
# RawAccel Linux — ASan + UBSan ile birim testler
# Kullanım: bash tests/run_tests_asan.sh
set -e
set -o pipefail   # L-2

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."

# ── TEST-1 (AJ3): koşu başına ayrı TMPDIR + koşuya özel ikili ─────────────────
# Bu betik iki kaynağı paylaşıyordu ve ikisi de paralel koşuları kırıyordu.
# ÖLÇÜLDÜ (2 kopya × 3 deneme, hedef dosyanın md5'ı korunarak): 3/6 rc=0.
#   · rc=126 ×2 — `test_accel_asan: Metin dosyası meşgul` (ETXTBSY): ikili
#     `$ROOT/build-manual/test_accel_asan` yolunda ORTAKTI; bir kopya çalışırken
#     diğeri üzerine yazıyor.
#   · rc=134 ×1 — `filesystem error: cannot remove all: No such file or directory
#     [/tmp/test_p99_c_dir]`: TMPDIR verilmediği için testin `tmp_path()` çağrıları
#     SABİT /tmp'ye düşüyordu; `tests/test_accel.cpp:8892` `remove_all(dir)`'i
#     THROWING overload (error_code'siz) olduğundan bir kopyanın sildiği dizini
#     diğeri kullanınca `filesystem_error` → terminate → SIGABRT.
# Düzeltme `tests/run_tests.sh`'in T5'iyle aynı: `mktemp` TMPDIR'a baktığı için
# tek `export` hem betiğin hem de C++ tarafının (`tmp_dir()` → `std::getenv`)
# dizinini koşuya özele çevirir; `BIN` de koşuya özel olunca ETXTBSY kaynağı
# kalmaz. `trap` DERLEME'DEN ÖNCE kurulur: `set -e` ile erken çıkışta dizin
# geride kalmasın. (Sıra önemli: TMPDIR ikiliden SONRA devreye girse hiçbir işe
# yaramıyordu — T5'in ilk hâlindeki hatanın aynısı.)
RA_TMP_PARENT="${TMPDIR:-/tmp}"        # mktemp'e verdiğimiz ebeveyn
export TMPDIR="$(mktemp -d)"
RA_TMPDIR="$TMPDIR"
cleanup_asan_tmp() {
    # ⚠️ SİLME ÖNCESİ DOĞRULAMA — bu satırlar bir güvenlik ağıdır, süs değil.
    # Ölçüldü: bu satırlar olmadan, `export TMPDIR` satırı yanlışlıkla
    # `/tmp`'ye çevrilirse `RA_TMPDIR=/tmp` olur ve EXIT trap'i `rm -rf /tmp`
    # çalıştırır — o denemede oturumun /tmp kanıtının tamamı silindi. Yani
    # "temizleme" yolu, kendi betiğinin dışındaki her şeyi silme yoluydu.
    # `mktemp -d` ÖLÇÜLDÜ: daima `<ebeveyn>/tmp.` + tam 10 karakter üretiyor
    # (3 örnek: tmp.pApTW2fwVV / tmp.FpoQQTEn0q / tmp.Fk5BloUI8n). Silme ancak
    # ad bu kalıba uyuyor VE ebeveyn bizim verdiğimiz ebeveyn ise yapılır.
    local d="${RA_TMPDIR:-}"
    [ -n "$d" ] || return 0
    [ -d "$d" ] || return 0
    case "$(basename "$d")" in
        tmp.??????????) ;;
        *) return 0 ;;                 # bu dizin bizim üretimimiz değil
    esac
    [ "$(dirname "$d")" = "$RA_TMP_PARENT" ] || return 0
    rm -rf "$d"
    return 0   # EXIT trap'inin dönüş değeri çıkış kodunu bozmasın
}
trap cleanup_asan_tmp EXIT
BIN="$TMPDIR/test_accel_asan"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I$ROOT/include -I$ROOT/src"

export ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=1:abort_on_error=1:strict_string_checks=1:detect_leaks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:abort_on_error=1:print_stacktrace=1}"

mkdir -p "$ROOT/build-manual"

echo "=== RawAccel Linux Birim Testleri (ASan + UBSan) ==="
echo "Derleniyor..."
# T53-02: -lpthread nesnelerden SONRAYA (as-needed riski).
if ! $CXX $CXXFLAGS \
    "$ROOT/tests/test_accel.cpp" \
    "$ROOT/src/config.cpp" \
    "$ROOT/src/logitech_receiver.cpp" \
    "$ROOT/src/logitech_hidpp.cpp" \
    -o "$BIN" -lpthread; then
    echo "Hata: test_accel_asan derlemesi başarısız (rc=$?)" >&2
    exit 1
fi

echo "Çalıştırılıyor..."
echo ""
"$BIN" "$@"
