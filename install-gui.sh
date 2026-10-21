#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# RawAccel Linux — GUI installer launcher (source tree)
#
# Builds the small GTK4 installer on first use and opens it.  The installer
# wraps the canonical setup.sh, so this file is only a launcher — it does not
# duplicate any installation logic.
#
# Usage:
#   bash install-gui.sh                 # open the GUI installer
#   bash install-gui.sh --detect        # print installed state (no GUI)
#   bash install-gui.sh --help          # installer options
#
# If GTK4 development files are missing, run the canonical terminal installer
# once (it installs all dependencies):  sudo bash setup.sh
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build-manual"
BIN="$BUILD/rawaccel-installer"
SOURCE="$ROOT/installer/main.cpp"

# CLI-only modes must work without a graphical session.
cli_only=0
for arg in "$@"; do
    case "$arg" in
        --detect|--print-command|--help|-h|--version|-V) cli_only=1 ;;
    esac
done

# No display: fall back to the canonical terminal installer *before* trying to
# build a GTK4 binary (a headless server may not have GTK4 development files).
if [[ "$cli_only" -eq 0 && -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    echo "Grafik oturumu bulunamadı (DISPLAY / WAYLAND_DISPLAY boş)." >&2
    echo "Terminal kurulumuna geçiliyor…" >&2
    if command -v sudo >/dev/null 2>&1; then
        setup_args=()
        for arg in "$@"; do
            case "$arg" in
                --uninstall|--reinstall|--no-deps) setup_args+=("$arg") ;;
            esac
        done
        exec sudo bash "$ROOT/setup.sh" "${setup_args[@]}"
    fi
    echo "sudo bulunamadı; şu komutu çalıştırın: sudo bash \"$ROOT/setup.sh\"" >&2
    exit 77
fi

if [[ ! -x "$BIN" || "$SOURCE" -nt "$BIN" || "$ROOT/include/rawaccel-base.hpp" -nt "$BIN" || "$ROOT/installer/log_scan.hpp" -nt "$BIN" ]]; then
    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "Hata: pkg-config bulunamadı. Kurulum için: sudo bash \"$ROOT/setup.sh\"" >&2
        exit 1
    fi
    if ! pkg-config --exists gtk4 2>/dev/null; then
        echo "Hata: GTK4 geliştirme dosyaları bulunamadı (pkg-config gtk4)." >&2
        echo "GUI kurulum sihirbazı için önce bağımlılıkları kurun:" >&2
        echo "    sudo bash \"$ROOT/setup.sh\"" >&2
        echo "Ardından bu betiği tekrar çalıştırın: bash install-gui.sh" >&2
        exit 77
    fi

    CXX="${CXX:-g++}"
    if ! command -v "$CXX" >/dev/null 2>&1; then
        echo "Hata: C++ derleyicisi bulunamadı ($CXX)." >&2
        exit 1
    fi

    mkdir -p "$BUILD"
    tmp_bin="$BIN.tmp.$$"
    trap 'rm -f "$tmp_bin"' EXIT
    echo "[kurulum-gui] GTK4 kurulum sihirbazı derleniyor…"
    # shellcheck disable=SC2086  # pkg-config output is intentionally split
    "$CXX" -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wno-unused-parameter \
        -I"$ROOT/include" -I"$ROOT/installer" \
        $(pkg-config --cflags gtk4) \
        "$SOURCE" \
        $(pkg-config --libs gtk4) -ldl \
        -o "$tmp_bin"
    mv -f "$tmp_bin" "$BIN"
    trap - EXIT
fi

exec "$BIN" --source "$ROOT" "$@"
