#!/bin/bash
# RawAccel Linux — Test çalıştırıcı
set -e
set -o pipefail   # L-2: a forgotten gate in a pipeline must not pass silently

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."

# T53-01: temiz checkout'ta `build-manual/` dizininin YOKluğu koşuyu
# patlatıyordu (eski sürüm `-o "$ROOT/build-manual/test_accel"` diyordu;
# kardeş betikler `mkdir -p` yapıyordu — run_tests_asan.sh:61,
# run_cli_sanitized.sh:43 — bu betik yapmıyordu).  İkili artık $TMPDIR'a
# derleniyor (P106 madde 3), ama `build-manual/` hâlâ CLI/DAEMON kapıları ve
# sonrasında gelen inşa adımları için gerekli — önce kur.
mkdir -p "$ROOT/build-manual"
# P106 madde 3 (AJ1): test ikilisi KOŞUYA ÖZEL bir yere yazılır, paylaşılan
# `build-manual/`'a DEĞİL. Ölçüldü: iki ajan aynı anda 2. kapıyı koşunca ikisi
# de `build-manual/test_accel` derliyor, yürütülme sırasında biri diğerinin
# üzerine yazıyor → `ETXTBSY` ("Metin dosyası meşgul") → **rc=126**.
#   ölçülen imzalar ayrı: 134 = SIGABRT (paylaşılan geçici dosya), 1 = parse,
#   126 = ETXTBSY (paylaşılan ikili). Üçü tek bir "ortam meşgul" koduna
#   sığmaz — kök nedenler farklı.
# Yer `$TMPDIR` (aşağıda `mktemp -d` ile üretilir, EXIT trap'inde silinir), yani
# ek `mkdir -p` gerekmez ve koşu bittiğinde ikili de yok olur.
# NOT: `$BIN` aşağıda, `TMPDIR` kurulduktan SONRA atanır; buradaki yer tutucu
# `build-manual/`a yazmayı bilerek yapmaz.
BIN=""

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -I$ROOT/include -I$ROOT/src"

# ── T5: koşu başına ayrı geçici dizin ─────────────────────────────────────────
# İki `run_tests.sh` kopyası eşzamanlı koşunca SABİT /tmp yolları çarpışıyordu:
# biri diğerinin dosyasını `std::remove()` ile siliyor, öteki guardsız
# `load_config` çağrısında "Cannot open config file" alıp SIGABRT ile düşüyordu
# (ölçüldü: 3 kopya × 3 denemede 6/9 koşu rc≠0).  İki ayrı yönlendirme var:
#   · `mktemp` TMPDIR'a baktığı için aşağıdaki 14 `mktemp` çağrısı koşuya özel
#     dizine düşer — ek kod gerekmez;
#   · `tests/test_accel.cpp` içindeki 42 sabit yol `tmp_path()` ile aynı dizini
#     okur.
# KAPSAM NOTU (AJ3, 30 Eyl 2026) — bu not ÖNCEKİ hâlinde YANLIŞTI, düzeltildi.
# Önceki metin "build-manual/ ve /dev/shm hâlâ paylaşılmaktadır, iki kopya
# eşzamanlı koştuğunda kapı yine de çakışabilir" diyordu.  Ölçüm bunu çürüttü:
# `BIN` artık `$TMPDIR/test_accel` olduğu için (madde 3, AJ1) test ikilisi
# build-manual'a HİÇ yazılmıyor.  Doğrulama, sil-koş-yeniden-oluştu-mu:
#   rm -f build-manual/test_accel && bash tests/run_tests.sh
#   → rc=0, 34164/34164, build-manual/test_accel YENİDEN OLUŞMADI.
# Eşzamanlılık, doğrudan bu depodan (sembolik çift değil), her denemede
# run_tests.sh'in karması alınıp değişmediği doğrulanarak:
#   3 kopya × 3 deneme =  9/9   ·  4 kopya × 3 = 12/12   ·  5 kopya × 3 = 15/15
#   → 36/36 rc=0, 0 geçersiz deneme.  (T4+T5 TEK BAŞINA 6/9 idi, hatalar
#   rc=126 "Metin dosyası meşgul" = ETXTBSY; o imza artık yok.)
#
# HÂLÂ PAYLAŞILAN (yani bu notun kapsamı DIŞINDA, ve ölçülmüş bir eksiklik):
#   · `build-manual/rawaccel-cli` ve `rawaccel-daemon` (aşağıdaki CLI/DAEMON).
#     Bu kapı ikililer YOKSA önce `scripts/build.sh` çağırır (T53-01 —
#     temiz checkout'ta artık patlamaz), sonra ÇALIŞTIRIR; normal koşuda
#     derleme adımı yoktur, yani 2. kapı kendi başına ETXTBSY üretemez.
#   · `/dev/shm` — SEC-2 bloğundaki sabit yollar.
#   · `XDG_RUNTIME_DIR` — PID dosyası.
#   · `run_tests_asan.sh` — 0 adet `export TMPDIR`, yani ASAN modu izole
#     DEĞİL.  Ölçüldü: aynı ASan ikilisi 2 kopya, TMPDIR verilmezken
#     3/6 rc=0 (3× rc=134 SIGABRT, "Cannot open config file"), ayrı TMPDIR
#     verilirken 6/6 rc=0.  Düzeltmesi T5'in aynısı (1 satır) ama o dosya
#     bu turda yetki kapsamı dışındaydı.
RA_TMP_PARENT="${TMPDIR:-/tmp}"        # mktemp'e verdiğimiz ebeveyn
export TMPDIR="$(mktemp -d)"
RA_TMPDIR="$TMPDIR"
# P106 madde 3 (AJ1): ikili artık koşuya özel. `TMPDIR` C++17'de de görünür
# (`std::getenv("TMPDIR")`) — `test_accel.cpp`in `tmp_dir()`ı okuyor.
BIN="$TMPDIR/test_accel"

TMP_FILES=()
cleanup_tmp() {
    # `rm -f` cannot remove a directory — it fails with "is a directory" and,
    # under `set -e`, that aborts the function BEFORE the RA_TMPDIR removal
    # below, so a single SECD/SECD_SHM/SECD_RT directory in TMP_FILES leaked
    # both itself and the whole TMPDIR. Those three are added at :361/:371/:377.
    # `rm -rf` handles files and directories alike; `|| true` keeps one bad
    # entry from skipping the rest. (Found by AJ3's audit, 30 Sep 2026.)
    rm -rf "${TMP_FILES[@]}" 2>/dev/null || true
    # ⚠️ SİLME ÖNCESİ DOĞRULAMA — bu satırlar bir güvenlik ağıdır, süs değil.
    # Ölçüldü (TEST-1, AJ3): koruma olmadan, `export TMPDIR` satırı yanlışlıkla
    # `/tmp`'ye çevrilirse `RA_TMPDIR=/tmp` olur ve EXIT trap'i `rm -rf /tmp`
    # çalıştırır — o denemede oturumun /tmp kanıtının tamamı silindi. Yani
    # "temizleme" yolu, kendi betiğinin dışındaki her şeyi silme yoluydu.
    # `mktemp -d` ÖLÇÜLDÜ: daima `<ebeveyn>/tmp.` + tam 10 karakter üretiyor
    # (3 örnek: tmp.pApTW2fwVV / tmp.FpoQQTEn0q / tmp.Fk5BloUI8n). Silme ancak
    # ad bu kalıba uyuyor VE ebeveyn bizim verdiğimiz ebeveyn ise yapılır.
    local d="${RA_TMPDIR:-}"
    if [ -n "$d" ] && [ -d "$d" ] \
       && [[ "$(basename "$d")" == tmp.?????????? ]] \
       && [ "$(dirname "$d")" = "$RA_TMP_PARENT" ]; then
        rm -rf "$d"
    fi
    return 0   # EXIT trap'inin dönüş değeri çıkış kodunu bozmasın
}
trap cleanup_tmp EXIT   # P114 BUG-H: hiçbir fail-erken çıkışta /tmp kalmasın

echo "=== RawAccel Linux Birim Testleri ==="
echo "Derleniyor..."

# T53-03: kurulum/derleme adımlarını görünür kıl — set -e'nin suskun çıkışı
# hangi kapının/derlemenin kırıldığını logda bırakmaz. Her adımın rc'si
# denetlenip net teşhis basılır.
run_gate_step() {
    local desc="$1"; shift
    echo "[adım] $desc"
    if ! "$@"; then
        echo "Hata: $desc başarısız (rc=$?)" >&2
        exit 1
    fi
}

# config.cpp ayrı derleme birimi olarak derlenir (M2: ODR sorununu önler)
# T53-02: -lpthread NESNElerden SONRAYA — --as-needed sol-sağ okur, öne
# konursa lib düşürülüp sonraki nesneler çözümsüz kalabilir.
if ! $CXX $CXXFLAGS \
    "$ROOT/tests/test_accel.cpp" \
    "$ROOT/src/config.cpp" \
    "$ROOT/src/logitech_receiver.cpp" \
    "$ROOT/src/logitech_hidpp.cpp" \
    -o "$BIN" -lpthread; then
    echo "Hata: test_accel derlemesi başarısız (rc=$?)" >&2
    exit 1
fi

echo "Çalıştırılıyor..."
echo ""
# Forward any CLI args (e.g. --filter, --list, --quiet) to the test binary
"$BIN" "$@"

# ── CLI davranış kapıları (P83: create-preset 256-char senkronu) ──────────────
CLI="$ROOT/build-manual/rawaccel-cli"
DAEMON="$ROOT/build-manual/rawaccel-daemon"

die() { echo "Hata: $*" >&2; exit 1; }   # L-BUG-41: unhelpful chatter→açıklayıcı hata

# ── P111 (AJ3): daemon kapısı KURULUM-DUYARSIZ olsun ──────────────────────────
# `daemon/main.cpp:458-459` PID dosyası canlılık kontrolünü üç adayın BİRLEŞİMİ
# olarak yapar: `$XDG_RUNTIME_DIR/rawaccel.pid` **VE** `/run/rawaccel.pid` **VE**
# `/tmp/rawaccel.pid`. Yani `XDG_RUNTIME_DIR` yalnız *yazma* yolunu değiştirir,
# *okunan* aday kümesini değiştirmez — ve bu üretimde kasıtlıdır (XDG'siz
# başlatılmış bir daemon'ı da yakalamak için, tek instance garantisi).
# ÖLÇÜLDÜ: canlı bir sistem daemon'ı varken SEC-2 bloğu `XDG_RUNTIME_DIR`'ı
# koşuya özel verse bile "Another instance may already be running (PID file
# exists)" deyip kendi fişini kullanmayı reddediyor → kapı, ürünün kurulu olup
# olmadığına BAĞLIYDI. Kurulu değilken aynı blok yeşildi.
# Çözüm: daemon çağrıları root GEREKTİRMEYEN bir mount namespace'inde, `/run`
# üzerine boş bir tmpfs ile koşturulur (`unshare -Urm`; yoksa `bwrap`).
# KAPSAM SADECE `/run`: `/tmp` bilerek maskelenmez. Negatif kontrol ölçüldü —
# `/tmp` de maskelenince "kabul edilen meşru config yolu" vakası KIRILIYOR
# ("Config directory '/tmp/.../d' does not exist"), yani maske genişletilemez.
# Test /run'a HİÇ yazmıyor: /run/rawaccel.pid ölçüm boyunca değişmedi.
DAEMON_ISO=none
DAEMON_ISO_PROBED=0

# `daemon/main.cpp` `pid_file_is_live` ile AYNI mantık: `kill(pid,0)` 0
# dönerse canlı; EPERM (sinyal gönderilemiyor) ise **yine canlı** sayılır —
# "var ama doğrulanamıyor, bu yüzden reddet". ⚠️ bash'in `kill -0` komutu EPERM
# ile ESRCH'i AYNI rc ile verdiği için ayrım `/proc/<pid>` varlığıyla yapılır
# (hata metni çözüm yerelinden bağımsız değil: burada "İşleme izin verilmedi").
# Bu ayrım olmadan canlı root daemon **ölü** görünür ve düzeltme sessizce hiç
# devreye girmez — ölçüldü: ilk hâlde `kill -0 693` rc=1, `/proc/693` ise VAR.
# 2026-10-07: SIGSTOP'lu (state T/t) instance busy SAYILMAZ — kill(pid,0)'a
# yanıt verir ama olay döngüsü işlemez; daemon tarafı pid_file_is_live ile
# aynı indirimi uygular (e2e/systemd pid-kilit).
ra_pid_live () {
    local f="$1" n state
    [ -r "$f" ] || return 1
    n=$(tr -dc '0-9' < "$f" 2>/dev/null) || return 1
    [ -n "$n" ] && [ "$n" -gt 0 ] 2>/dev/null || return 1
    if kill -0 "$n" 2>/dev/null; then
        state=$(sed -E 's/^.*\) //' "/proc/$n/stat" 2>/dev/null | cut -c1)
        case "$state" in T|t) return 1 ;; esac   # SIGSTOP/trace-stop → busy değil
        return 0    # sinyal gidebiliyor = kesin canlı
    fi
    if [ -e "/proc/$n" ]; then
        state=$(sed -E 's/^.*\) //' "/proc/$n/stat" 2>/dev/null | cut -c1)
        case "$state" in T|t) return 1 ;; esac   # EPERM olsa da durdurulmuş → busy değil
        return 0            # EPERM: var ama sinyal gönderilemiyor
    fi
    return 1
}

# Testin sahiplenemediği iki GLOBAL aday (üçüncüsü zaten koşuya özel).
ra_foreign_daemon_live () {
    ra_pid_live /run/rawaccel.pid && return 0
    ra_pid_live /tmp/rawaccel.pid && return 0
    return 1
}

# Bir komutu gerekiyorsa izole namespace içinde koşturur. Geçiş şeffaftır:
# izolasyon gerekmiyorsa komut BİREBİB doğrudan çalışır.
ra_daemon_run () {
    if [ "$DAEMON_ISO_PROBED" -eq 0 ]; then
        DAEMON_ISO_PROBED=1
        if ra_foreign_daemon_live; then
            if unshare -Urm true 2>/dev/null; then
                DAEMON_ISO=unshare
            elif bwrap --bind / / --dev /dev true 2>/dev/null; then
                DAEMON_ISO=bwrap
            else
                echo "FAIL: canlı bir RawAccel daemon'ı var (PID dosyası: /run/rawaccel.pid veya" >&2
                echo "      /tmp/rawaccel.pid) ve kapıyı izole edecek root gerektirmeyen" >&2
                echo "      namespace yok: ne 'unshare -Urm' ne 'bwrap' çalışıyor." >&2
                echo "      SESSİZCE ATLANMAZ. Çözüm: 'rawaccel-cli stop', ya da" >&2
                echo "      kernel.unprivileged_userns_clone=1 olsun." >&2
                exit 1
            fi
        fi
    fi
    case "$DAEMON_ISO" in
        unshare)
            unshare -Urm /bin/sh -c '
                mount -t tmpfs tmpfs /run 2>/dev/null || exit 70
                exec "$@"' ra "$@" ;;
        bwrap)
            bwrap --bind / / --dev /dev --tmpfs /run -- "$@" ;;
        *)
            "$@" ;;
    esac
}

# Temiz checkout (veya silinen build-manual/): kapıların çalıştırdığı
# CLI/DAEMON ikilileri henüz yoksa ÖNCE derle.  P114 BUG-B korunur:
# sessiz SKIP hâlâ yok — ikili yoksa ve derlenemezse sert hata.
# (T53-01: `rm -rf build-manual && bash tests/run_tests.sh` kendi kendine
# toparlanmalı; es­kiden düz exit 1 ile “temiz checkout patlıyor”du.)
if [ ! -x "$CLI" ] || [ ! -x "$DAEMON" ]; then
    echo "CLI/DAEMON ikilisi yok — önce derleniyor (scripts/build.sh)..."
    if ! bash "$ROOT/scripts/build.sh"; then
        echo "Hata: scripts/build.sh başarısız oldu; CLI/DAEMON kapıları çalıştırılamadı." >&2
        exit 1
    fi
fi
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
    echo "[adım] seed config baslatildi (rc=$SRC)"
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
    run_gate_step "P107 seed create-preset gaming" \
        "$CLI" -c "$TMPP" --no-daemon create-preset gaming g
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
    run_gate_step "O31-L2 cap_x 30" \
        "$CLI" -c "$TMPP" --no-daemon set-param g cap_x 30
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
    run_gate_step "diff seed create d1" "$CLI" -c "$TMPD1" --no-daemon create d1
    rm -f "$TMPD1.bak"
    run_gate_step "diff seed create d2" "$CLI" -c "$TMPD1" --no-daemon create d2
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
    run_gate_step "diff set-param d1 cap_x 77" "$CLI" -c "$TMPD1" --no-daemon set-param d1 cap_x 77
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
    run_gate_step "diff set-param d2 cap_x 77" "$CLI" -c "$TMPD1" --no-daemon set-param d2 cap_x 77
    TMPDF=$(mktemp --suffix=.json)
    TMP_FILES+=( "$TMPDF" )
    # T53-03: export'un JSON'ı stdout'u kirletmesin; [adım] satırı da TMPDF'e
    # karışmasın diye CLI çıktısı ayrı bir kabukta dosyaya yönlendirilir.
    run_gate_step "diff export d2" bash -c '"$1" -c "$2" --no-daemon export d2 > "$3"' _ "$CLI" "$TMPD1" "$TMPDF"
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
    # P106 (AJ1): bu blok "disallowed directory" özelliğini sınar, yani testin
    # anlamı **/dev/shm ALTINDA OLMAK**. Sabit bir isim iki eşzamanlı koşuda
    # çakışıp diğerinin fixture'ını siliyordu (ölçüldü: A rc=0, B rc=1, B'nin
    # 34164/34164'ü geçti — yalnız SEC-2 kırıldı). Benzersiz ad aynı özelliği
    # korur, çakışmayı kaldırır. `mktemp -d -p /dev/shm` -> /dev/shm/rawaccel-secdir.XXXXXX
    SECD_SHM="$(mktemp -d -p /dev/shm rawaccel-secdir.XXXXXX)"
    TMP_FILES+=( "$SECD_SHM" )
    # Aynı sebeple PID dosyası: daemon $XDG_RUNTIME_DIR → /run → /tmp sırasıyla
    # dener (daemon/main.cpp:466-468). Blok başına özel bir XDG_RUNTIME_DIR verilir,
    # yazma oraya düşer ve GLOBAL yollara hiç dokunulmaz. XDG_CONFIG_HOME'A DOKUNMA —
    # :488'in varsayılan-yol reddi testi onu bilerek /dev/shm'a yönlendiriyor.
    SECD_RT="$(mktemp -d)"
    TMP_FILES+=( "$SECD_RT" )
    export XDG_RUNTIME_DIR="$SECD_RT"
    mkdir -p "$SECD4"
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
    secd_reject "doğrudan /dev/shm" "$SECD_SHM/a.json" "disallowed directory"
    # (2) ATLATMA: çözülen yol /dev/shm — düzeltme öncesi KABUL ediliyordu
    secd_reject "traversal → /dev/shm" "$SECD4/$UP$SECD_SHM/a.json" "disallowed directory"
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
    OUT=$(XDG_CONFIG_HOME="$SECD_SHM" "$CLI" list 2>&1)
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
        OUT=$(ra_daemon_run timeout 5 "$DAEMON" -c "$path" 2>&1)
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
        OUT=$(ra_daemon_run timeout 3 "$DAEMON" -c "$path" 2>&1)
        RC=$?
        set -e
        if [ $RC -eq 1 ] || echo "$OUT" | grep -qE "disallowed directory|does not have a .json extension"; then
            echo "FAIL: daemon meşru config yolu [$desc] reddetti (rc=$RC): $OUT"
            exit 1
        fi
    }
    secd_daemon_reject "doğrudan /dev/shm" "$SECD_SHM/a.json" "disallowed directory"
    secd_daemon_reject "traversal → /dev/shm" "$SECD4/$UP$SECD_SHM/a.json" "disallowed directory"
    secd_daemon_reject "traversal → /proc" "$SECD4/$UP/proc/a.json" "disallowed directory"
    secd_daemon_reject "uzantısız, meşru dizin" "$SECD4/a.txt" "does not have a .json extension"
    secd_daemon_reject "sembolik bağ → /dev/shm" "$SECD/badlink/a.json" "disallowed directory"
    secd_daemon_accept "meşru yeni dosya" "$SECD4/yeni.json"
    # daemon'ın varsayılan yolu da doğrulanmalı (bu kopyada düzeltilen boşluk)
    set +e
    OUT=$(XDG_CONFIG_HOME="$SECD_SHM" ra_daemon_run timeout 5 "$DAEMON" 2>&1)
    RC=$?
    set -e
    if [ $RC -ne 1 ] || ! echo "$OUT" | grep -q "disallowed directory"; then
        echo "FAIL: daemon varsayılan config yolu (XDG_CONFIG_HOME) doğrulanmadı (rc=$RC): $OUT"
        exit 1
    fi
    # ilk çalıştırma: config dizini YOK — doğrulama bunu kırmamalı
    secd_daemon_accept "ilk çalıştırma (dizin yok)" "$SECD4/yeni.json"
    echo "SEC-2 config-yolu kapısı (daemon): atlatma/sembolik bağ/varsayılan yol ✓"
    # P106 (AJ1): yalnız KENDİ fixture'ını sil. Sabit isimli paylaşılan dizini
    # `rm -rf` etmek, eşzamanlı koşan başka bir ajanın dizinini de siliyordu.
    rm -rf "$SECD" "$SECD_SHM" "$SECD_RT"
    unset XDG_RUNTIME_DIR

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

# ── SIMD backend parity kapısı ───────────────────────────────────────────────
# Neden run_tests.sh'in İÇİNDE: AVX2 yolu üretimde çalışıyor
# (scripts/build.sh + CMakeLists.txt `-march=native`; Haswell'den beri her
# x86-64) ama BU koşucu `-march` vermiyor, yalnız SSE2'yi geziyordu.  Üretimi
# vuran hata tam olarak bu boşluktan geçti: `v2d_store`/`v2d_get_y` lane 2
# (sıfır dolgu lane) okuduğu için her AVX2 derlemesi Y bileşenine 0.0 yazdı
# ve dikey fare hareketi sessizce ölüydü.  run_simd_parity.sh bu boşluğu
# kapatıyordu ama yalnız CI'da ve elle çalışıyordu — AGENTS.md'de belgelenen
# "bash tests/run_tests.sh" komutu AVX2'yi HİÇ çalıştırmıyordu.
#
# Ölçülen maliyet: 2710 ms (3 derleme + 3 koşum + 2 diff).  run_tests.sh'in
# 35902 ms'ine %7.5 ekliyor.  Doğruluk bedelini ödüyor.
#
# exit 77 = konak AVX2 ikili dosyasını çalıştıramıyor (x86 dışı).  Bu da
# SESSİZCE GEÇİŞ DEĞİLDİR: ne geçtiğini ne yapmadığını açıkça yazar ve
# çalışmanın sonunda tekrar hatırlatır (SEC-2 emeli).  x86 olmayan bir katkıcıyı
# bloklamamak için hata değildir — ama saklanmaz da.
#
# exit 1 (ayrışma/başarısızlık) ve beklenmeyen her çıkış kodu HATADIR.
set +e
bash "$ROOT/tests/run_simd_parity.sh"
SIMD_RC=$?
set -e
case "$SIMD_RC" in
    0)
        echo "SIMD parity kapısı: AVX2/SSE2/skaler birebir aynı ✓"
        ;;
    77)
        echo "UYARI: SIMD parity kapısı ATLANDI (exit 77) — bu konak AVX2'yi"
        echo "       çalıştıramıyor.  Üretimde çalışan AVX2 yolu BU KOŞUDA"
        echo "       denenmedi; AVX2'ye özgü bir hata burada görünmez."
        echo "=== Sonuç: N/N geçti — DİKKAT: SIMD parity ATLANDI (konak AVX2 çalıştıramıyor) ==="
        ;;
    1)
        echo "FAIL: SIMD backend parity — backend'ler farklı sonuç üretti"
        echo "      (yukarıya bak: run_simd_parity.sh ayrışan satırları basar)"
        exit 1
        ;;
    *)
        echo "FAIL: SIMD parity kapısı beklenmeyen çıkış kodu: $SIMD_RC"
        echo "      (run_simd_parity.sh yalnız 0 / 1 / 77 dönmeli)"
        exit 1
        ;;
esac
