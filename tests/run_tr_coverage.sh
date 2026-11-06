#!/bin/bash
# Translation coverage test — every GUI string passed to tr()/trf()/tr*()
# helpers must have a Turkish entry in gui/tr.inl.
# Exit 0 = full coverage; exit 1 = missing translations.
set -e
cd "$(dirname "$0")/.."
# T53-16: liste elle-kodluydu — dizine yeni .inl eklendiğinde sessizce
# denetim-dışı kalıyordu. Artık gui/*.inl glob'u otomatik taranır; ana
# kaynaklar açıkça eklenir, yinelenenler ayıklanır.
SRC=(gui/main.cpp)
shopt -s nullglob
for f in gui/*.inl; do SRC+=("$f"); done
shopt -u nullglob
# yinelenenleri ayıkla (ör. gui/tr.inl açıkça eklenmiş olsaydı)
SRC=($(printf '%s\n' "${SRC[@]}" | awk '!seen[$0]++'))
# Bekçi: gui/ altında .inl varken glob boş kalırsa tarama gizlice daralır.
if [ ${#SRC[@]} -lt 3 ]; then
    echo "META-FAIL: gui/*.inl taraması ${#SRC[@]} dosya verdi — glob kırık olabilir" >&2
    exit 1
fi
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