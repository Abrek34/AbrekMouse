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
    TMPCFG=$(mktemp)
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
    TMPA=$(mktemp)
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
    TMPP=$(mktemp)
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
    rm -f "$TMPP" "$TMPP.bak"
fi
