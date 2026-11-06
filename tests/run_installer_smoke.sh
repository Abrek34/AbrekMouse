#!/usr/bin/env bash
# Installer smoke test — build and exercise the non-GUI interface of
# rawaccel-installer.  The GTK window itself is not opened here; this gate
# verifies that the launcher/command construction still works in CI/headless
# environments, where the GUI path cannot be exercised.
#
# Exit: 0 = pass, 1 = failure, 77 = GTK4 development files unavailable.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-g++}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
BIN="$TMP/rawaccel-installer"

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists gtk4; then
    echo "installer smoke: GTK4 development files unavailable — SKIP"
    exit 77
fi

echo "=== [1/3] Building rawaccel-installer ==="
# shellcheck disable=SC2086
"$CXX" -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wno-unused-parameter \
    -I"$ROOT/include" \
    $(pkg-config --cflags gtk4) \
    "$ROOT/installer/main.cpp" \
    $(pkg-config --libs gtk4) -ldl \
    -o "$BIN"

echo "=== [2/3] CLI contract ==="
"$CXX" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
    -I"$ROOT/installer" \
    "$ROOT/tests/installer_log_scan_test.cpp" \
    -o "$TMP/installer_log_scan_test"
"$TMP/installer_log_scan_test"
"$BIN" --help | grep -q -- '--source'
"$BIN" --version | grep -q 'rawaccel-installer'
"$BIN" --detect | grep -q '^installed='
"$BIN" --detect | grep -q '^pacman_lock=[01]$'
"$BIN" --detect | grep -q '^pacman_busy=[01]$'
"$BIN" --detect | grep -q '^source='

install_cmd="$("$BIN" --print-command --source "$ROOT")"
grep -q 'setup.sh' <<<"$install_cmd"
grep -q -- '--reinstall' <<<"$install_cmd"

no_deps_cmd="$("$BIN" --print-command --no-deps --source "$ROOT")"
grep -q -- '--no-deps' <<<"$no_deps_cmd"

uninstall_cmd="$("$BIN" --print-command --uninstall --source "$ROOT")"
grep -q -- '--uninstall' <<<"$uninstall_cmd"

if "$BIN" --print-command --source "$TMP" >/dev/null 2>&1; then
    echo "FAIL: invalid source folder was accepted" >&2
    exit 1
fi
echo "  CLI contract: OK"

echo "=== [3/3] Source detection ==="
detect_out="$(cd "$TMP" && "$BIN" --detect --source "$ROOT")"
grep -Fqx "source=$ROOT" <<<"$detect_out"
echo "  source detection: OK"

echo "=== Sonuç: PASS — installer smoke test ==="
