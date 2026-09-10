#!/usr/bin/env bash
# Aj 3 kanal gözcüsü — 10 saniyede bir haberleşme kanalını tarar,
# yeni EMIR/atama/yetki değişikliği varsa bastırır; yoksa sessiz tık atar.
cd /home/a/Masaüstü/rawaccel-linux || exit 1
REPO=/home/a/Masaüstü/rawaccel-linux

FILES="$REPO/.aihaberlesme/mesajlar/aj0.log
$REPO/.aihaberlesme/mesajlar/aj1.log
$REPO/.aihaberlesme/mesajlar/aj2.log
$REPO/.aihaberlesme/mesajlar/aj3.log
$REPO/.aihaberlesme/mesajlar/aj4.log
$REPO/.aihaberlesme/mesajlar/aj5.log
$REPO/.aihaberlesme/mesajlar/aj6.log
$REPO/.aihaberlesme/mesajlar/aj7.log
$REPO/.aihaberlesme/mesajlar/aj8.log
$REPO/aihaberlesme.md
$REPO/.aihaberlesme/AKIS.json"

snap() { for f in $FILES; do md5sum "$f"; done; }
prev_sum=$(snap | md5sum | cut -d' ' -f1)
prev_lines=$(wc -l < "$REPO/aihaberlesme.md")

for i in $(seq 1 30); do
  sleep 10
  cur_sum=$(snap | md5sum | cut -d' ' -f1)
  cur_lines=$(wc -l < "$REPO/aihaberlesme.md")
  ts=$(date +%H:%M:%S)
  if [ "$cur_sum" != "$prev_sum" ]; then
    echo "[$ts] poll$i DEGISIKLIK ALGINLANDI"
    echo "  -- aihaberlesme.md yeni satirlar ($prev_lines->$cur_lines):"
    if [ "$cur_lines" -gt "$prev_lines" ]; then
      sed -n "$((prev_lines+1)),${cur_lines}p" "$REPO/aihaberlesme.md" | sed 's/^/     | /'
      echo "  -- yeni EMIR YONETICI bloklari (aj1/aj3/aihaberlesme):"
      grep -h "^### YONETICI \[Aj 1\]" aihaberlesme.md | tail -3 | sed 's/^/     | /'
      grep -h "EMIR P" .aihaberlesme/mesajlar/aj1.log .aihaberlesme/mesajlar/aj3.log | tail -4 | sed 's/^/     | /'
    fi
    echo "  -- kilit (AKIS):"; python3 -c "import json,sys; d=json.load(open('$REPO/.aihaberlesme/AKIS.json')); print('     | kilit=', d.get('kilit')); [print('     |   P%s:',k,'durum=',v if not isinstance(v,dict) else v.get('durum')) if False else None for k,v in (d.get('isler',{}) or {}).items()]" 2>/dev/null | tail -6
    prev_lines=$cur_lines
  else
    echo "[$ts] poll$i kanal sessiz (kilit: $(python3 -c "import json;d=json.load(open('$REPO/.aihaberlesme/AKIS.json'));print(d.get('kilit'))" 2>/dev/null))"
  fi
  prev_sum=$cur_sum
done
echo "=== gozculuk penceresi bitti (30x10sn). Yeni atama varsa yukarida. ==="