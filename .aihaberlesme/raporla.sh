#!/usr/bin/env bash
# AJAN → AJ1 RAPOR GÖNDERİCİ
#   bash .aihaberlesme/raporla.sh <aj-no> <görev-no> "durum" "satır" "satır" ...
# Örnek:
#   bash .aihaberlesme/raporla.sh 2 P108 "tamam" "BULGU: ..." "KAPI: rc=0 34164/34164"
#
# Ne yapar:
#   1) Raporunu .aihaberlesme/mesajlar/aj<N>.log dosyasının SONUNA ekler (kanıt)
#   2) AJ1'in oturumuna doğrudan mesaj gönderir (uyandırır)
#
# ⚠️  `--prompt` bayrağı `opencode run`'da YOKTUR ("Unrecognized flag") — burada
#     `--file` + konumsal özet kullanılıyor.
# ⚠️  `nohup ... &` şart: yoksa komut hedef ajanın turu bitene kadar seni
#     bekletir ve karşılıklı kilitlenme olur.
set -euo pipefail

AJ="${1:?kullanım: raporla.sh <aj1..aj5|--kontrol> <görev> <satır...>}"

# ── yönetici kontrol modu: son raporlar (görev no gerekmez) ───────────────
if [ "$AJ" = "--kontrol" ]; then
  echo "=== AJAN RAPOR DURUMU ==="
  for n in 2 3 4 5; do
    f=".aihaberlesme/mesajlar/aj$n.log"
    if [ -f "$f" ]; then
      printf "  aj%s.log  %5s satır  son yazım: %s\n" "$n" "$(wc -l < "$f")" "$(stat -c %y "$f" | cut -c1-16)"
      printf "      son görev: %s\n" "$(grep -oE '^### Aj\.[0-9]+ \[[^]]+\]' "$f" | tail -1)"
    else
      printf "  aj%s.log  (yok — henüz rapor yazmamış)\n" "$n"
    fi
  done
  exit 0
fi

GOREV="${2:?görev no gerekli (örn. P108)}"
shift 2

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

AJ1_SES="ses_f1199eaf8ffe1sFoTv46EmtzBf"
AJ1_MODEL="opencode/space-bunny-free"
STAMP="$(date '+%d eylül %Y %H:%M')"
LOGDIR=".aihaberlesme/mesajlar"
TMPD="/tmp/opencode"
mkdir -p "$TMPD"

# ── 1) kalıcı kayıt: kendi loguna SONA ekle ────────────────────────────────
mkdir -p "$LOGDIR"
BEFORE=$( [ -f "$LOGDIR/aj$AJ.log" ] && wc -c < "$LOGDIR/aj$AJ.log" || echo 0 )
{
  printf '\n'
  printf '### Aj.%s [%s] [%s]\n' "$AJ" "$GOREV" "$STAMP"
  for l in "$@"; do printf '%s\n' "$l"; done
} >> "$LOGDIR/aj$AJ.log"
AFTER=$( wc -c < "$LOGDIR/aj$AJ.log" )

if [ "$AFTER" -le "$BEFORE" ]; then
  echo "⛔ HATA: log küçülmedi ($BEFORE → $AFTER). $LOGDIR/aj$AJ.log bozuldu!" >&2
  exit 1
fi
printf '✅ aj%s.log  +%s bayt  (toplam %s)\n' "$AJ" "$((AFTER-BEFORE))" "$AFTER"

# ── 2) doğrudan bildirim: AJ1'in oturumuna mesaj ──────────────────────────
RPT="$TMPD/rapor_aj${AJ}_$(date +%s).txt"
{
  printf 'AJ%s -> AJ1 RAPORU  [%s]  %s\n\n' "$AJ" "$GOREV" "$STAMP"
  for l in "$@"; do printf '%s\n' "$l"; done
  printf '\n(kanıt: .aihaberlesme/mesajlar/aj%s.log, +%s bayt)\n' "$AJ" "$((AFTER-BEFORE))"
} > "$RPT"

nohup /home/a/.opencode/bin/opencode run \
  --session "$AJ1_SES" --model "$AJ1_MODEL" \
  --file "$RPT" "AJ${AJ} -> AJ1 RAPORU: ${GOREV}" \
  > "$TMPD/rapor_aj${AJ}.log" 2>&1 &

echo "✅ AJ1'e doğrudan gönderildi (arka planda)  →  $RPT"
echo "   ⏳ 1-2 dk içinde bana ulaşacak. Kontrol:  bash .aihaberlesme/raporla.sh --kontrol"
