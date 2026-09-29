#!/usr/bin/env bash
# GÜVENLİ EKLEME — ajan logları ASLA bu yolla sıfırlanmaz.
#
#   bash .aihaberlesme/ekle.sh aj4 "### Aj.4 [M100] [29 eylül 2026] [21:39]" "satır 2" ...
#
# Neden var: 29 Eylül 2026'da Aj 4, aj4.log'u `>` ile yazdı ve
# 1171 satır (111 KB) tarihsel kaydı SİLİNDİ — dosya 14 satıra düştü.
# `git checkout` ile geri alındı. Yapısal çözüm: tek giriş noktası.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$ROOT/.aihaberlesme/mesajlar"

aj="${1:?kullanım: ekle.sh <aj1..aj4|ortak> <satır> [satır ...]}"
shift

case "$aj" in
  aj1|aj2|aj3|aj4) target="$LOG_DIR/$aj.log" ;;
  ortak)           target="$LOG_DIR/AJANLAR.log" ;;
  *) echo "geçersiz ajan: '$aj' (aj1..aj4 | ortak)" >&2; exit 2 ;;
esac

# ⛔ KAZARA-SIFIRLAMA KALKANI — dosya mevcutsa ASLA '>' kullanma.
before=0
[ -f "$target" ] && before=$(wc -c < "$target")

{
  printf '\n'
  printf '%s\n' "$@"
} >> "$target"

after=$(wc -c < "$target")

# Doğrulama: dosya KÜÇÜLMEDİ olmalı. Küçüldüyse bir şey bozdu.
if [ "$after" -le "$before" ]; then
  echo "⛔ HATA: log küçüldü ($before → $after bayt). $target bozuldu!" >&2
  exit 1
fi

printf '✅ %s  +%s bayt  (toplam %s)\n' "$(basename "$target")" "$((after - before))" "$after"
