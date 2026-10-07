#!/bin/bash
# RawAccel Linux — CLI'yi ASan + UBSan altında koştur (kapı).
# Kullanım: bash tests/run_cli_sanitized.sh
#
# Neden ayrı bir kapı (run_tests_asan.sh yetmiyor):
#   run_tests_asan.sh yalnız tests/test_accel.cpp'yi koşturur. cli/main.cpp
#   (3285 satır: argüman ayrıştırma, config yolu doğrulama, JSON çevrimi,
#   print_profile, diff/export) hiçbir sanitizer altında ÇALIŞTIRILMAZ.
#   run_tests.sh gerçek CLI ikilisini koşturur ama sanitizer'sız. Yani
#   CLI'nin kendi kodundaki bellek hataları iki kapı arasındaki boşlukta
#   kalıyor. Bu kapı o boşluğu kapatır.
#
# PC (kanıt, elle yeniden koşulabilir):
#   cmd_list'e off-by-one, cmd_show'a gerçek heap-use-after-free enjekte
#   edildi → ikisi de yakalandı (exit 134):
#     cmd_list → SEGV + UBSan "load of null pointer" @ cli/main.cpp:548
#     cmd_show → "heap-use-after-free ... in cmd_show" @ cli/main.cpp:569
#
# Exit 0 = temiz · 1 = kapı başarısız · 77 = bu konakta sanitizer yok (atlanır,
# ama GÖRÜNÜR uyarı basılır — sessizce geçmez).
set -e
set -o pipefail   # L-2

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BIN="$ROOT/build-manual/rawaccel-cli-sanitized"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I$ROOT/include -I$ROOT/src"

export ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=1:abort_on_error=1:strict_string_checks=1:detect_leaks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:abort_on_error=1:print_stacktrace=1}"

# --- sanitizer bu derleyicide var mı? ---------------------------------------
# Sessizce derlenip sonra çalışmayan bir kapı kurmayalım: önce doğrula.
if ! echo 'int main(){return 0;}' | $CXX -fsanitize=address,undefined -x c++ - -o /dev/null 2>/dev/null; then
    echo "DİKKAT: $CXX ASan/UBSan desteklemiyor — CLI sanitizer kapisi ATLANDI"
    exit 77
fi

mkdir -p "$ROOT/build-manual"

echo "=== RawAccel Linux CLI (ASan + UBSan) ==="
echo "Derleniyor..."

# Kaynak seti = run_tests_asan.sh'inin seti, test_accel.cpp yerine cli/main.cpp.
# (logitech_receiver/hidpp olmadan linklenmiyor: CLI hidpp-set-dpi vb. çağırıyor.)
# T53-02: -lpthread nesnelerden SONRAYA (--as-needed riski).
if ! $CXX $CXXFLAGS \
    "$ROOT/cli/main.cpp" \
    "$ROOT/src/config.cpp" \
    "$ROOT/src/logitech_receiver.cpp" \
    "$ROOT/src/logitech_hidpp.cpp" \
    -o "$BIN" -lpthread; then
    echo "Hata: rawaccel-cli-sanitized derlemesi başarısız (rc=$?)" >&2
    exit 1
fi

# Sanitizer imzaları. UBSan "runtime error:" satırı, ASan/LSan "ERROR: <x>".
SAN_RE='ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:|SUMMARY: (Address|UndefinedBehavior)Sanitizer'

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
CFG="$WORK/settings.json"
# Bozuk (parse edilemez) config — "unreadable or invalid" yolunu sınamak için.
printf 'bu bir json degil {{{' > "$WORK/bozuk.json"

# --no-daemon ZORUNLU: mutasyon komutlarının çıkış kodu daemon_apply_if_enabled()
# (cli/main.cpp:374) dönüşüdür — daemon çalışıyorsa 0, çalışmıyorsa "kaydedildi
# ama uygulanmadı" uyarısıyla 1. Kapının daemon'e bağımlı olmaması için sabit
# davranışı seçiyoruz. (cli/main.cpp:375-389)
#
# -c paylaşılmıyor; her vaka kendi yolunu veriyor. Yoksa yola özel red
# vakaları ("-c /dev/shm/...") paylaşılan -c'nin üstüne ikinci bir -c daha
# yazardı ve CLI'de SONUNCU kazanıyor (ölçüldü) — yani vaka sessizce bambaşka
# bir yolu sınayabilirdi.
CLI=("$BIN" --no-daemon)

total=0; ok=0; yol_calismadi=0; san=0; hata=0
# vaka <açıklama> <beklenen işaret> <argümanlar...>
vaka() {
    local desc="$1" isaret="$2"; shift 2
    local out rc
    total=$((total + 1))
    set +e
    out="$("${CLI[@]}" "$@" 2>&1)"; rc=$?
    set -e
    if printf '%s' "$out" | grep -aqE "$SAN_RE"; then
        san=$((san + 1)); hata=$((hata + 1))
        echo "  ✗ SANITIZER [$desc]"
        printf '%s\n' "$out" | grep -aE "$SAN_RE" | head -3 | sed 's/^/      /'
        return
    fi
    if printf '%s' "$out" | grep -aqF "$isaret"; then
        ok=$((ok + 1))
        printf "  ✓ %-44s rc=%d\n" "$desc" "$rc"
    else
        # Komut çalışmadıysa (argüman adı değişti, komut kaldırıldı, ...) kapı
        # SESSİZCE yeşil kalmasın. Bu, "0 sanitizer ihbarı"nın tek başına
        # yeterli olmadığı durumu kapatır.
        yol_calismadi=$((yol_calismadi + 1)); hata=$((hata + 1))
        echo "  ✗ YOL ÇALIŞMADI [$desc] — beklenen işaret yok: '$isaret' (rc=$rc)"
        printf '%s\n' "$out" | head -2 | sed 's/^/      /'
    fi
}

echo "Çalıştırılıyor..."
echo ""
# Yazma vakaları (ölçülmüş işaret metinleri; CLI'nin bugün gerçekten dediği
# metinler — tahmin değil):
vaka "create-preset gaming"          "Created profile 'p_gaming'"  -c "$CFG" create-preset gaming  p_gaming
for p in office precision disable cs2 valorant apex fps; do
    vaka "create-preset $p"          "Created profile 'p_$p'"      -c "$CFG" create-preset "$p" "p_$p"
done
vaka "list"                          "Active profile:"             -c "$CFG" list
vaka "show"                          "Profile: p_gaming"           -c "$CFG" show p_gaming
vaka "set (aktif profil)"            "Active profile set to:"      -c "$CFG" set p_gaming
vaka "set-param rotation"            "Set rotation"                -c "$CFG" set-param p_gaming rotation 30
vaka "set-param snap"                "Set snap"                    -c "$CFG" set-param p_gaming snap 12
vaka "set-param dpi"                 "Set dpi"                     -c "$CFG" set-param p_gaming dpi 1600
vaka "set-param acceleration"        "Set acceleration"             -c "$CFG" set-param p_gaming acceleration 0.02
vaka "set-param bilinmeyen anahtar"  "Unknown key: bogus"           -c "$CFG" set-param p_gaming bogus 5
vaka "duplicate"                     "Duplicated profile"           -c "$CFG" duplicate p_gaming p_dup
vaka "rename"                        "Renamed profile"              -c "$CFG" rename p_dup p_ren
vaka "delete"                        "Deleted profile"              -c "$CFG" delete p_ren
vaka "export (JSON)"                 '"name":"p_gaming"'            -c "$CFG" export p_gaming
vaka "list --json"                   '"profiles"'                   -c "$CFG" list --json
vaka "validate (var olan config)"    "Validating config"            -c "$CFG" validate
vaka "validate (yok dosya)"          "config file not found"        -c "$WORK/yok.json" validate
vaka "diff (iki preset)"             "diff"                         -c "$CFG" diff p_gaming p_apex
vaka "import yok dosya"              "Cannot open"                  -c "$CFG" import /nonexistent-xyz.json
vaka "import reddedilen yol"         "Cannot open"                  -c "$CFG" import /etc/shadow
# L13-15: the two import cases above both exit BEFORE any JSON is read, so
# cmd_import's 215-line parse/validate block ran zero times under ASan.  These
# cases go THROUGH the JSON path: a valid single-profile import and a
# malformed one (the latter must be rejected without touching the config).
printf '%s' '{"name":"p_asan_import","device_id":"","disable":false,"dpi":800,"polling_rate":1000,"profile":{"accel_x":{"mode":"classic","acceleration":0.01}}}' > "$WORK/asan_import.json"
vaka "import (geçerli JSON)"          "Imported profile"             -c "$CFG" import "$WORK/asan_import.json"
printf '%s' '{"name":"bad","profile":{"accel_x":{"mode":"NOT_A_MODE"}}}' > "$WORK/asan_bad.json"
vaka "import (bozuk mode JSON)"       "unknown accel mode"           -c "$CFG" import "$WORK/asan_bad.json"
printf '%s' 'INVALID JSON {{{' > "$WORK/asan_parse.json"
vaka "import (parse hatası)"          "Invalid profile JSON"         -c "$CFG" import "$WORK/asan_parse.json"
vaka "show olmayan profil"           "Profile not found"            -c "$CFG" show yok_boyle_profil

# Yol doğrulama reddedişleri (SEC-2 sınıfı) — hepsi ölçülmüş metin:
vaka "config yolu reddi (/dev/shm)"  "is in a disallowed directory"  -c /dev/shm/nope.json list
vaka "config dizini yok"             "does not exist"                -c "$WORK/yok_dizin/d.json" list
vaka "config yolu .json degil"       "does not have a .js"           -c "$WORK" list
vaka "config okunamaz/gecersiz"      "unreadable or invalid"         -c "$WORK/bozuk.json" list

echo ""
echo "=== Sonuç: $ok/$total komut gerçek kodu çalıştırdı ==="
if [ "$san" -ne 0 ]; then
    echo "=== SONUÇ: BAŞARISIZ — $san komutta sanitizer ihbari ==="
    exit 1
fi
if [ "$yol_calismadi" -ne 0 ]; then
    echo "=== SONUÇ: BAŞARISIZ — $yol_calismadi komut beklenen işareti üretmedi ==="
    exit 1
fi
echo "=== Sonuç: PASS — CLI ASan/UBSan altında temiz ($ok/$total komut) ==="
exit 0
