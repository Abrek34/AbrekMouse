#!/bin/bash
# RawAccel Linux — Test çalıştırıcı
set -e
set -o pipefail   # L-2: a forgotten gate in a pipeline must not pass silently

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BIN="$ROOT/build-manual/test_accel"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -I$ROOT/include -I$ROOT/src"

echo "=== RawAccel Linux Birim Testleri ==="
echo "Derleniyor..."

# config.cpp ayrı derleme birimi olarak derlenir (M2: ODR sorununu önler)
$CXX $CXXFLAGS -lpthread \
    "$ROOT/tests/test_accel.cpp" \
    "$ROOT/src/config.cpp" \
    "$ROOT/src/logitech_receiver.cpp" \
    "$ROOT/src/logitech_hidpp.cpp" \
    -o "$BIN"

echo "Çalıştırılıyor..."
echo ""
# Forward any CLI args (e.g. --filter, --list, --quiet) to the test binary
"$BIN" "$@"

# ── CLI davranış kapıları (P83: create-preset 256-char senkronu) ──────────────
CLI="$ROOT/build-manual/rawaccel-cli"
DAEMON="$ROOT/build-manual/rawaccel-daemon"
TMP_FILES=()
cleanup_tmp() {
    rm -f "${TMP_FILES[@]}"
}
trap cleanup_tmp EXIT   # P114 BUG-H: hiçbir fail-erken çıkışta /tmp kalmasın
die() { echo "Hata: $*" >&2; exit 1; }   # L-BUG-41: unhelpful chatter→açıklayıcı hata

if [ ! -x "$CLI" ]; then
    echo "Hata: CLI kapısı çalıştırılamadı: $CLI" >&2
    echo "      rawaccel-cli derlenmemiş — P83/P99/P107 kapıları SESSİZCE ATLANAMAZ." >&2
    echo "      Önce 'bash scripts/build.sh' çalıştırıp yeniden deneyin." >&2
    exit 1   # P114 BUG-B: eksik CLI artık sessiz SKIP + exit 0 veremez
fi
if [ -x "$CLI" ]; then
    TMPCFG=$(mktemp --suffix=.json)   # SEC-2: config paths must end in .json
    TMP_FILES+=( "$TMPCFG" )
    rm -f "$TMPCFG"   # P42: var olan bos config uzerine yazilmaz; dosya yokken seed olusur
    TMPN256=$(mktemp)
    TMP_FILES+=( "$TMPN256" )
    TMPN257=$(mktemp)
    TMP_FILES+=( "$TMPN257" )
    command -v python3 || die "python3 is required for P83 test gates"
    python3 - "$TMPN256" "$TMPN257" <<'PY'
import sys
open(sys.argv[1], 'w').write('a' * (256))
open(sys.argv[2], 'w').write('a' * (257))
PY
    # Geçerli bir seed config olustur (bos dosya uzerine P42 reddeder)
    set +e
    "$CLI" -c "$TMPCFG" --no-daemon create-preset office seed >/dev/null 2>&1
    SRC=$?
    set -e
    if [ $SRC -ne 0 ]; then
        echo "FAIL: seed config baslatilamadi (rc=$SRC)"
        exit 1
    fi
    # 256 char: kabul (create-preset siniri MAX_NAME_LEN = 256)
    set +e
    OUT=$("$CLI" -c "$TMPCFG" --no-daemon create-preset cs2 "$(cat "$TMPN256")" 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 0 ] || ! echo "$OUT" | grep -qE "Created profile|updated"; then
        echo "FAIL: 256-char create-preset rejected (rc=$RC): $OUT"
        exit 1
    fi
    # 257 char: reddedilmeli ("too long")
    set +e
    OUT=$("$CLI" -c "$TMPCFG" --no-daemon create-preset cs2 "$(cat "$TMPN257")" 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -qiE "too long"; then
        echo "FAIL: 257-char create-preset accepted (rc=$RC): $OUT"
        exit 1
    fi
    echo "CLI create-preset ad kapısı: 256 OK, 257 red ✓ (P83)"
    rm -f "$TMPCFG" "$TMPN256" "$TMPN257"

    # ── P99: arity + -c "" kapıları ───────────────────────────────────────────
    # Ekstra argümanlar sessizce yutulmuyor; hepsi rc=1 + usage. "-c ''" reel
    # config'e düşüp onu değiştirmemeli (P74 BULGU-2 sınıfı).
    TMPA=$(mktemp --suffix=.json)   # SEC-2: config path must end in .json
    TMP_FILES+=( "$TMPA" )
    rm -f "$TMPA"
    set +e
    OUT=$("$CLI" -c "$TMPA" --no-daemon create pro >/dev/null 2>&1; "$CLI" -c "$TMPA" --no-daemon set pro BONUS 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "takes at most"; then
        echo "FAIL: P99 extra-arg set rejected (rc=$RC): $OUT"
        exit 1
    fi
    ACTIVE=$("$CLI" -c "$TMPA" --no-daemon list | sed -n 's/^Active profile: //p')
    if [ "$ACTIVE" != "default" ]; then
        echo "FAIL: P99 extra-arg set mutated config (active=$ACTIVE)"
        exit 1
    fi
    set +e
    OUT=$("$CLI" -c "$TMPA" --no-daemon create-preset cs2 a b c 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "takes at most"; then
        echo "FAIL: P99 extra-arg create-preset rejected (rc=$RC): $OUT"
        exit 1
    fi
    set +e
    OUT=$("$CLI" -c "" --no-daemon status 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "non-empty path"; then
        echo "FAIL: P99 -c '' not rejected (rc=$RC): $OUT"
        exit 1
    fi
    echo "CLI P99 arity/-c\"\" kapısı: ekstra-arg red + -c\"\" red ✓"
    rm -f "$TMPA"

    # ── P107: set-param domain kapısı (sessiz clamp → red) ──────────────────
    # Out-of-domain set-param values must exit 1 AND leave the config file
    # byte-identical (previously snap 90 silently stored 45 and exited 0).
    TMPP=$(mktemp --suffix=.json)   # SEC-2: config path must end in .json
    TMP_FILES+=( "$TMPP" "$TMPP.bak" )
    rm -f "$TMPP" "$TMPP.bak"
    "$CLI" -c "$TMPP" --no-daemon create-preset gaming g >/dev/null 2>&1
    set +e
    "$CLI" -c "$TMPP" --no-daemon set-param g snap 20 >/dev/null 2>&1
    RC_OK=$?
    set -e
    if [ $RC_OK -ne 0 ]; then
        echo "FAIL: P107 valid in-domain set-param rejected (rc=$RC_OK)"
        exit 1
    fi
    BEFORE=$(cat "$TMPP")
    for BAD in "snap 90" "dpi 999999" "exponent_classic 0.5" "lp_norm 0" "polling_rate 50" "snap abc"; do
        set +e
        OUT=$("$CLI" -c "$TMPP" --no-daemon set-param g $BAD 2>&1)
        RC=$?
        set -e
        AFTER=$(cat "$TMPP")
        if [ $RC -eq 0 ]; then
            echo "FAIL: P107 boundary 'set-param g $BAD' accepted (rc=$RC): $OUT"
            exit 1
        fi
        if [ "$BEFORE" != "$AFTER" ]; then
            echo "FAIL: P107 boundary 'set-param g $BAD' mutated config"
            exit 1
        fi
    done
    echo "CLI P107 set-param domain kapısı: out-of-domain red + config dokunulmadı ✓"

    # ── O31-L2: input_offset üst sınır + cap_x ↔ input_offset çapraz kısıtı ─
    # input_offset CLI domain'i [0, CAP_X_MAX=500] olmalı (sanitize 500'e
    # klamp ettiği için P107 bytes-birebir); ayrıca BUG-7 kuralı gereği
    # cap_x >= input_offset olmalı — aksi halde loader sessizce cap_x'i
    # input_offset'e çeker (kullanıcının istediği değer yazılmaz).
    set +e
    OUT=$("$CLI" -c "$TMPP" --no-daemon set-param g input_offset 501 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "valid range"; then
        echo "FAIL: O31-L2 'input_offset 501' (above CAP_X_MAX) accepted (rc=$RC): $OUT"
        exit 1
    fi
    set +e
    OUT=$("$CLI" -c "$TMPP" --no-daemon set-param g input_offset 30 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "input_offset"; then
        echo "FAIL: O31-L2 'input_offset 30' with cap_x 15 accepted (rc=$RC): $OUT"
        exit 1
    fi
    "$CLI" -c "$TMPP" --no-daemon set-param g cap_x 30 >/dev/null 2>&1
    set +e
    "$CLI" -c "$TMPP" --no-daemon set-param g input_offset 30 >/dev/null 2>&1
    RC=$?
    set -e
    if [ $RC -ne 0 ]; then
        echo "FAIL: O31-L2 'input_offset 30' with cap_x 30 rejected (rc=$RC)"
        exit 1
    fi
    L2BEFORE=$(cat "$TMPP")
    set +e
    OUT=$("$CLI" -c "$TMPP" --no-daemon set-param g cap_x 20 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "input_offset"; then
        echo "FAIL: O31-L2 'cap_x 20' with input_offset 30 accepted (rc=$RC): $OUT"
        exit 1
    fi
    if [ "$L2BEFORE" != "$(cat "$TMPP")" ]; then
        echo "FAIL: O31-L2 rejected set-param mutated config"
        exit 1
    fi
    echo "CLI O31-L2 kapısı: input_offset [0,500] + cap_x>=input_offset çapraz kısıt ✓"

    # ── O31-L1: tek (odd) sayıda LUT eleman içeren import reddedilmeli ─────
    # 515 eleman (257.5 nokta): n/2=257 kapasiteyi "geçmiyordu" ve 515. eleman
    # sessizce düşüyordu.  n%2 koruması ile import rc=1 + net mesaj.
    TMPLUT=$(mktemp)
    TMP_FILES+=( "$TMPLUT" )
    python3 - "$TMPLUT" <<'PY'
import sys, json
flat = []
for i in range(257):
    flat.extend([float(i+1), 1.0])
flat.append(999999.0)   # 515 eleman = tek (odd)
blob = {
    "name": "oddlut",
    "profile": {
        "accel_x": {"mode": "lookup", "lut_length": len(flat), "lut_data": flat},
        "accel_y": {"mode": "lookup", "lut_length": 0, "lut_data": []}
    }
}
json.dump(blob, open(sys.argv[1], 'w'))
PY
    set +e
    OUT=$("$CLI" -c "$TMPP" --no-daemon import "$TMPLUT" 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -qiE "odd"; then
        echo "FAIL: O31-L1 odd-element LUT import accepted (rc=$RC): $OUT"
        exit 1
    fi
    rm -f "$TMPLUT"
    echo "CLI O31-L1 kapısı: odd-lut import reddi ✓"
    rm -f "$TMPP" "$TMPP.bak"

    # ── diff: farklı profiller işaretlenir, özdeş olan temiz çıkar ──────────
    # diff `<a> <b>` profile adı ya da JSON dosyası alır; çıktı yalnızca farklı
    # alanları gösterir; rc 0 = özdeş, 1 = en az bir fark.  Sessiz
    # dönüşüm-farkları (1e-9 epsilon) sahte fark üretmemeli.
    TMPD1=$(mktemp --suffix=.json)
    TMP_FILES+=( "$TMPD1" "$TMPD1.bak" )
    rm -f "$TMPD1" "$TMPD1.bak"
    "$CLI" -c "$TMPD1" --no-daemon create d1 >/dev/null 2>&1
    rm -f "$TMPD1.bak"
    "$CLI" -c "$TMPD1" --no-daemon create d2 >/dev/null 2>&1
    # özdeş profiller önce: rc 0 + "no differences" (isimler farklı ise isim farkı sayılır)
    set +e
    OUT=$("$CLI" -c "$TMPD1" --no-daemon diff d1 d1 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 0 ] || ! echo "$OUT" | grep -q "no differences"; then
        echo "FAIL: diff of identical profiles (rc=$RC): $OUT"
        exit 1
    fi
    # tek bir alanı değiştir: tam o alan + rc 1
    "$CLI" -c "$TMPD1" --no-daemon set-param d1 cap_x 77 >/dev/null 2>&1
    set +e
    OUT=$("$CLI" -c "$TMPD1" --no-daemon diff d1 d2 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 1 ]; then
        echo "FAIL: diff of differing profiles rc=$RC (want 1): $OUT"
        exit 1
    fi
    if ! echo "$OUT" | grep -q "cap_x" || ! echo "$OUT" | grep -q "77"; then
        echo "FAIL: diff missing the changed field (cap_x 77): $OUT"
        exit 1
    fi
    # geri al → özdeş; dosya-karşılaştırma formu da aynı sonucu vermeli
    "$CLI" -c "$TMPD1" --no-daemon set-param d2 cap_x 77 >/dev/null 2>&1
    TMPDF=$(mktemp --suffix=.json)
    TMP_FILES+=( "$TMPDF" )
    "$CLI" -c "$TMPD1" --no-daemon export d2 > "$TMPDF"
    set +e
    OUT=$("$CLI" -c "$TMPD1" --no-daemon diff d2 "$TMPDF" 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 0 ] || ! echo "$OUT" | grep -q "no differences"; then
        echo "FAIL: diff profile vs its own export (rc=$RC): $OUT"
        exit 1
    fi
    # hatalı kaynak: yok + dosya değil → rc 1, config'e dokunulmaz
    set +e
    OUT=$("$CLI" -c "$TMPD1" --no-daemon diff d1 "no-such-name-zzz" 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 1 ] || echo "$OUT" | grep -q "no differences"; then
        echo "FAIL: diff with nonexistent source (rc=$RC): $OUT"
        exit 1
    fi
    echo "CLI diff kapısı: özdeş/dosya-gidiş-geliş/eksik-kaynak ✓"
    rm -f "$TMPD1" "$TMPD1.bak" "$TMPDF"

    # ── SEC-2: config yolu politikası ÇÖZÜLMÜŞ yola uygulanmalı (R5-S-9) ─────
    # Yasağın (".json" + /proc/ /sys/ /dev/) "dosya yok" dalında ham dizgeye
    # uygulanması ve stat()'in ".." bileşenlerini çözmesi atlatmaya yol açıyordu:
    # doğrudan /dev/shm reddedilirken /tmp/x/../../../dev/shm/a.json kabul ediliyordu.
    # Bu kapı iki şeyi birden kilitler: atlatma kapalı, meşru yol hâlâ açık.
    if [ ! -d /dev/shm ]; then
        echo "FAIL: /dev/shm yok — config-yolu güvenlik kapısı çalıştırılamıyor (sessizce atlanmaz)" >&2
        exit 1
    fi
    SECD=$(mktemp -d)
    TMP_FILES+=( "$SECD" )
    # Traversal'ı: gerekli ".." sayısını HESAPLA ve çözümlemeyi
    # DOĞRULA — derinlik yanlışsa kapı yanlış sebeple kırılıp yeşil görünebilir.
    SECD4="$SECD/a/b/c/d"
    mkdir -p "$SECD4" /dev/shm/rawaccel-secdir
    UP=""
    probe="$SECD4"
    while [ "$probe" != "/" ]; do
        probe=$(dirname "$probe")
        UP="../$UP"
    done
    if [ "$(readlink -f "$SECD4/$UP/dev/shm")" != "/dev/shm" ]; then
        echo "FAIL: traversal çözümlemesi beklenmedik: '$SECD4/$UP/dev/shm' -> '$(readlink -f "$SECD4/$UP/dev/shm")'" >&2
        exit 1
    fi
    # kanıt: yasak önek doğrudan yazıldığında reddediliyor (kapının canlı olduğunun
    # ilk kanıtı) ve aynı hedef yalnızca ".." ile erişildiğinde de reddedilmeli.
    case "$UP" in *..*) ;; *) echo "FAIL: traversal üretilemedi (UP='$UP')" >&2; exit 1 ;; esac

    # reddedilmesi gerekenler: çıkış kodu 1 + BEKLENEN SPESİFİK mesaj.
    # Mesaj ayrımı şart: tek bir regex iki kuralı ayırt edemiyorsa, birinin
    # kaldırılması kapıyı geçiyordu (pozitif kontrolle ölçüldü — PC4).
    secd_reject () {
        local desc="$1" path="$2" want="$3"
        set +e
        OUT=$("$CLI" -c "$path" list 2>&1)
        RC=$?
        set -e
        if [ $RC -ne 1 ]; then
            echo "FAIL: config yolu [$desc] reddedilmedi (rc=$RC): $OUT"
            exit 1
        fi
        if ! echo "$OUT" | grep -qE "$want"; then
            echo "FAIL: config yolu [$desc] yanlış sebeple reddedildi — '/$want/' bekleniyordu: $OUT"
            exit 1
        fi
    }
    # kabul edilmesi gerekenler: çıkış kodu 0
    secd_accept () {
        local desc="$1" path="$2"
        set +e
        OUT=$("$CLI" -c "$path" list 2>&1)
        RC=$?
        set -e
        if [ $RC -ne 0 ]; then
            echo "FAIL: meşru config yolu [$desc] reddedildi (rc=$RC): $OUT"
            exit 1
        fi
    }

    # (1) doğrudan yasak önek — düzeltme öncesi de reddediliyordu (kontrol)
    secd_reject "doğrudan /dev/shm" "/dev/shm/rawaccel-secdir/a.json" "disallowed directory"
    # (2) ATLATMA: çözülen yol /dev/shm — düzeltme öncesi KABUL ediliyordu
    secd_reject "traversal → /dev/shm" "$SECD4/$UP/dev/shm/rawaccel-secdir/a.json" "disallowed directory"
    # (3) aynı atlatma /proc ve /sys için
    secd_reject "traversal → /proc" "$SECD4/$UP/proc/a.json" "disallowed directory"
    secd_reject "traversal → /sys"  "$SECD4/$UP/sys/a.json"  "disallowed directory"
    # (4) UZANTI kuralı BAĞIMSIZ ölçülmeli: yasak önek altında değil, meşru dizinde.
    #     (yasak önek altında denemek iki kuralı birbirine bulaştırıyordu)
    secd_reject "uzantısız, meşru dizin" "$SECD4/a.txt" "does not have a .json extension"
    secd_reject "uzantısız, sembolik bağ" "$SECD/badlink2/a.txt" "does not have a .json extension"
    ln -sf "$SECD4" "$SECD/goodlink"
    secd_reject "sembolik bağ → uzantısız" "$SECD/goodlink/a.txt" "does not have a .json extension"
    # (5) sembolik bağ /dev/shm'yi göstermeli → çözülünce yakalanmalı
    ln -sf /dev/shm "$SECD/badlink"
    secd_reject "sembolik bağ → /dev/shm" "$SECD/badlink/a.json" "disallowed directory"
    # (6) meşru yeni dosya hâlâ kabul edilmeli (ilk çalıştırma)
    secd_accept "meşru yeni dosya" "$SECD4/yeni.json"
    # (7) sembolik bağ üzerinden MEŞRU yol da kabul edilmeli
    secd_accept "sembolik bağ → meşru" "$SECD/goodlink/yeni2.json"
    # (7) varsayılan yol da denetlenmeli: find_config_path() XDG_CONFIG_HOME'dan
    #     türetiyor ve o yol düzeltme öncesi HİÇ doğrulanmıyordu — aynı hedef
    #     -c ile verilince reddedilirken burada KABUL ediliyordu.
    set +e
    OUT=$(XDG_CONFIG_HOME=/dev/shm/rawaccel-secdir "$CLI" list 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 1 ] || ! echo "$OUT" | grep -q "disallowed directory"; then
        echo "FAIL: varsayılan config yolu (XDG_CONFIG_HOME=/dev/shm/...) doğrulanmadı (rc=$RC): $OUT"
        exit 1
    fi
    # (8) ... ve varsayılan yolun kabul edildiği hâl durumu bozulmamalı
    mkdir -p "$SECD/xdg/rawaccel"
    set +e
    OUT=$(XDG_CONFIG_HOME="$SECD/xdg" "$CLI" list 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 0 ]; then
        echo "FAIL: meşru varsayılan config yolu reddedildi (rc=$RC): $OUT"
        exit 1
    fi
    echo "SEC-2 config-yolu kapısı (CLI): atlatma/sembolik bağ/uzantı/varsayılan yol ✓"

    # ── O31-L4: CLI-3 kendini-onarma bloğu OKUNAMADIĞI profil verisini yazıyor ──
    # `load_config` bir dosyada profil bulamazsa, komut YÖNLENDİRMESİNDEN ÖNCE
    # (cli/main.cpp:3219) bir "default" profil uydurup save_config çağırıyordu.
    # `save_config` tam dört üst-düzey anahtar yazar ve app_config'in tek dizi
    # üyesi `profiles` olduğu için, `profiles` DIŞINDA bir anahtarda duran veri
    # tanımıyla kaybolur.  Ölçülen veri kaybı: 80 baytlık `profile_list`
    # configi üzerinde SALT OKUNUR `list` (rc=0) dosyayı 2679 bayta yazıp
    # `profile_list`'i yok ediyordu; `show x` ve `list --json` de aynısıydı.
    #
    # Kapı iki yönlüdür: (a) yabancı anahtarda hiçbir komut dosyayı bayt bayt
    # DEĞİŞTİRMEZ, (b) meşru `delete`-son-profil kendini onarması ÇALIŞMAYA
    # DEVAM EDER — (b) olmadan (a)'yi "CLI-3'ü kaldır" diye sağlamak da mümkün.
    TMPL4=$(mktemp --suffix=.json)
    TMP_FILES+=( "$TMPL4" )
    rm -f "$TMPL4"
    python3 - "$TMPL4" <<'PY4'
import sys
open(sys.argv[1], 'w').write(
    '{"version":"1.2.3","active_profile":"x",'
    '"profile_list":[{"name":"x","dpi":800,"polling_rate":1000}]}')
PY4
    chmod 0644 "$TMPL4"
    L4_BEFORE=$(wc -c < "$TMPL4")
    L4_SUM=$(md5sum < "$TMPL4" | cut -d' ' -f1)
    for L4_CMD in list show list --json; do
        set +e
        "$CLI" -c "$TMPL4" --no-daemon $L4_CMD >/dev/null 2>&1
        set -e
        L4_AFTER=$(wc -c < "$TMPL4")
        L4_SUM2=$(md5sum < "$TMPL4" | cut -d' ' -f1)
        if [ "$L4_BEFORE" != "$L4_AFTER" ] || [ "$L4_SUM" != "$L4_SUM2" ]; then
            echo "FAIL: O31-L4 — '$L4_CMD' yabanci anahtarli configi degistirdi ($L4_BEFORE -> $L4_AFTER bayt); veri kaybi."
            exit 1
        fi
    done
    # meşru onarma: delete son profil -> list kendini kendine onarmali
    printf '{"version":"1.2.3","active_profile":"x","profiles":[{"name":"x","dpi":800}]}' > "$TMPL4"
    chmod 0644 "$TMPL4"
    set +e
    "$CLI" -c "$TMPL4" --no-daemon delete x >/dev/null 2>&1
    "$CLI" -c "$TMPL4" --no-daemon list >/dev/null 2>&1
    set -e
    L4_N=$(python3 -c "import json,sys;print(len(json.load(open(sys.argv[1]))['profiles']))" "$TMPL4" 2>/dev/null || echo ERR)
    if [ "$L4_N" != "1" ]; then
        echo "FAIL: O31-L4 — meşru delete-son-profil kendini onarmadi (profiles=$L4_N, beklenen 1); CLI-3 yanlislikla kaldirilmis olabilir."
        exit 1
    fi
    echo "O31-L4 kapısı: yabancı anahtar korunuyor + delete-son-profil onarımı çalışıyor ✓"

    # ── AYNI KOPYANIN daemon/ varyantı ──────────────────────────────────────
    # Kusur cli/main.cpp ve daemon/main.cpp'de BİREBİR aynıydı; yalnız CLI
    # kopyasını kapamak yetmezdi (pozitif kontrolle ölçüldü: daemon kopyasındaki
    # "varsayılan yolu doğrulama" bozulunca kapı yeşil kaldı).  daemon root
    # çalıştığı için bu kopyası asıl önemli olan.
    if [ ! -x "$DAEMON" ]; then
        echo "FAIL: daemon kapısı çalıştırılamadı: $DAEMON (sessizce atlanmaz)" >&2
        exit 1
    fi
    # daemon reddederse 1 ile çıkar; kabul ederse çalışmaya devam eder (timeout).
    # Ayrıştırıcı: reddedilmemek = "doğrulama hatası mesajı yok VE rc 1 değil".
    secd_daemon_reject () {
        local desc="$1" path="$2" want="$3"
        set +e
        OUT=$(timeout 5 "$DAEMON" -c "$path" 2>&1)
        RC=$?
        set -e
        if [ $RC -ne 1 ] || ! echo "$OUT" | grep -qE "$want"; then
            echo "FAIL: daemon config yolu [$desc] reddedilmedi (rc=$RC): $OUT"
            exit 1
        fi
    }
    secd_daemon_accept () {
        local desc="$1" path="$2"
        set +e
        OUT=$(timeout 3 "$DAEMON" -c "$path" 2>&1)
        RC=$?
        set -e
        if [ $RC -eq 1 ] || echo "$OUT" | grep -qE "disallowed directory|does not have a .json extension"; then
            echo "FAIL: daemon meşru config yolu [$desc] reddetti (rc=$RC): $OUT"
            exit 1
        fi
    }
    secd_daemon_reject "doğrudan /dev/shm" "/dev/shm/rawaccel-secdir/a.json" "disallowed directory"
    secd_daemon_reject "traversal → /dev/shm" "$SECD4/$UP/dev/shm/rawaccel-secdir/a.json" "disallowed directory"
    secd_daemon_reject "traversal → /proc" "$SECD4/$UP/proc/a.json" "disallowed directory"
    secd_daemon_reject "uzantısız, meşru dizin" "$SECD4/a.txt" "does not have a .json extension"
    secd_daemon_reject "sembolik bağ → /dev/shm" "$SECD/badlink/a.json" "disallowed directory"
    secd_daemon_accept "meşru yeni dosya" "$SECD4/yeni.json"
    # daemon'ın varsayılan yolu da doğrulanmalı (bu kopyada düzeltilen boşluk)
    set +e
    OUT=$(XDG_CONFIG_HOME=/dev/shm/rawaccel-secdir timeout 5 "$DAEMON" 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 1 ] || ! echo "$OUT" | grep -q "disallowed directory"; then
        echo "FAIL: daemon varsayılan config yolu (XDG_CONFIG_HOME) doğrulanmadı (rc=$RC): $OUT"
        exit 1
    fi
    # ilk çalıştırma: config dizini YOK — doğrulama bunu kırmamalı
    secd_daemon_accept "ilk çalıştırma (dizin yok)" "$SECD4/yeni.json"
    echo "SEC-2 config-yolu kapısı (daemon): atlatma/sembolik bağ/varsayılan yol ✓"
    rm -rf "$SECD" /dev/shm/rawaccel-secdir

    # ── monitor: arity + aralık kapıları (daemon yoksa çalışmaz — kapı değil) ─
    # Monitor, daemon gerektirir; testte sadece argüman doğrulama + (daemon
    # varsa) tek örnek akışı kontrol edilir.  Süresiz döngü test sürücünü
    # kilitlememeli: yalnızca aşağıdaki geçersiz çağrılar sinyalsiz döner.
    set +e
    OUT=$("$CLI" -c "$(mktemp --suffix=.json)" --no-daemon monitor abc 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "interval"; then
        echo "FAIL: monitor non-numeric interval accepted (rc=$RC): $OUT"
        exit 1
    fi
    set +e
    OUT=$("$CLI" monitor 0 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "interval"; then
        echo "FAIL: monitor 0 interval accepted (rc=$RC): $OUT"
        exit 1
    fi
    set +e
    OUT=$("$CLI" monitor 60001 2>&1)
    RC=$?
    set -e
    if [ $RC -eq 0 ] || ! echo "$OUT" | grep -q "interval"; then
        echo "FAIL: monitor 60001 interval accepted (rc=$RC): $OUT"
        exit 1
    fi
    echo "CLI monitor kapısı: aralık doğrulama ✓"
fi

# ── kayıt↔kod köprü kapısı ────────────────────────────────────────────────────
# Bir kayıt ⏸ <dosya> kilidi / AÇIK etiketiyle dururken kodda o kaydın düzeltme
# işareti varsa çelişki vardır.  170c5e14 tam olarak bunu yaptı: 8 kaydın kodu
# düzeltildi (düzeltici `// O31-C2:` yorumunu bıraktı), tracker'daki ⏸
# etiketlerine dokunulmadı → kayıtlar 22 commit bayat kaldı.  Sinyal vardı,
# yazıldığı yer ölçülmüyordu.
#
# Karşı yön ("✅ ama kodda işaret yok") BİLEREK DIŞARIDA: 23 kapalı kaydın 4'ünde
# işaret yok, 3'ü fiilen düzeltilmiş ama işaret commit mesajına yazılmış —
# ~%15-17 yanlış pozitif, kapı olarak kullanılamaz.
#
# SESSİZCE ATLANMAZ: python3 veya tracker yoksa hata verir (SEC-2 emeli).
if ! bash tests/run_tracker_bridge.sh; then
    exit 1
fi
