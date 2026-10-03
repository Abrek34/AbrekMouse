#!/bin/bash
# Translation coverage test — every GUI string passed to tr()/trf()/tr*()
# helpers must have a Turkish entry in gui/tr.inl.
# Exit 0 = full coverage; exit 1 = missing translations.
set -e
cd "$(dirname "$0")/.."
SRC=(
  gui/main.cpp
  gui/tr.inl
  gui/devices.inl
  gui/daemon_comm.inl
  gui/graph.inl
  gui/widgets_sync.inl
  gui/profile_mgr.inl
  gui/ui_builder.inl
  gui/hidpp_panel.inl
  gui/mouse_test.inl
  gui/kwin_focus.inl
)
# C3 (L18 B-19): her kaynak dosyanın varlığını ÖNCE doğrula. Eksik bir dosya
# CLI'ye hiç geçmezse tarayıcı onu sessizce atlar ve daha az anahtar taranır —
# "hiç bulamadı ama PASS" sınıfının bir kolu. Eksik dosya META-FAIL'dir.
for f in "${SRC[@]}"; do
    if [ ! -f "$f" ]; then
        echo "META-FAIL: $f missing" >&2
        exit 1
    fi
done
CXX="${CXX:-g++}"
# Compile to a unique temp path (mktemp) instead of a predictable
# /tmp/tr_coverage — a pre-created symlink there would let a local attacker
# redirect the compiler output / swapped binary (TOCTOU).
TMPDIR_bin="${TMPDIR:-/tmp}"
BIN="$(mktemp "$TMPDIR_bin/tr_coverage.XXXXXX")"
trap 'rm -f "$BIN"' EXIT
"$CXX" -std=c++20 -Wall -Wextra -O1 -o "$BIN" tests/tr_coverage.cpp
"$BIN" "${SRC[@]}"