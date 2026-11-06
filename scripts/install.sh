#!/bin/bash
# RawAccel Linux Installer — thin wrapper around the canonical setup.sh.
#
# Tam kurulumun TEK kaynağı repo kökündeki setup.sh'tır (bağımlılıklar,
# eski kurulum temizliği, derleme, sistem dosyaları, servis, KDE fix).
# Bu betik yalnızca geriye dönük uyumluluk için duruyor ve yönlendirir.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SETUP="$SCRIPT_DIR/../setup.sh"

if [[ ! -f "$SETUP" ]]; then
    echo "ERROR: setup.sh bulunamadı ($SETUP)" >&2
    exit 1
fi

echo ":: scripts/install.sh → setup.sh (tam kurulum: bağımlılıklar + derleme + sistem dosyaları + KDE fix)"
exec bash "$SETUP" "$@"