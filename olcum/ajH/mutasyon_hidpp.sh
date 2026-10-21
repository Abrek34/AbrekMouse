#!/usr/bin/env bash
# AJ-H — HID++ kapısının BOŞ OLMAĞININ kanıtı (mutasyon testi).
#
# "Kapı yeşil" demek, kapının gerçekten bir şeyi ölçtüğünü göstermez.
# Bu betik bilerek bozan mutasyonları uygular ve kapının KIRMIZIYA döndüğünü
# gösterir.
#
#   bash olcum/ajH/mutasyon_hidpp.sh
#
# ⚠️ GÜVENLİK: REPO'YA DOKUNMAZ.  Kaynaklar geçici bir kopyaya alınır,
#    mutasyon KOPYAYA uygulanır, orada derlenip koşulur.  Böylece eşzamanlı
#    bir derleme (başka ajan) mutasyonlu kaynağı yakalayamaz — ilk denemede
#    repo dosyası değiştiriliyordu, bu sürümde o risk yoktur.
#
# Beklenen çıktı (21 Ekim 2026 ölçümü):
#   M1 (iç kısa-yük koruması gevşetildi) → YAKALANMADI (eşdeğer mutant:
#       iç koruma herkese açık API'den geçilemiyor, tüm çağrı noktaları ≤16 bayt)
#   M2 (bildirim zulası devre dışı)       → 1 FAIL, rc=1
#   M3 (yanıt eşleştirme bozuldu)         → 4 FAIL, rc=1
#   M0 (kontrol, mutasyonsuz)             → 31/31 yeşil, rc=0
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/olcum/ajH"
mkdir -p "$OUT"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp -r "$ROOT/include" "$WORK/include"
cp -r "$ROOT/src"     "$WORK/src"
cp -r "$ROOT/tests"   "$WORK/tests"

CPP="$WORK/src/logitech_hidpp.cpp"
BIN="$WORK/hidpp_mutasyon_testi"

build_and_run() {
  g++ -std=c++20 -O2 -w -I "$WORK/include" -I "$WORK/src" \
      "$WORK/tests/hidpp_transport_test.cpp" \
      "$WORK/src/config.cpp" "$WORK/src/logitech_receiver.cpp" \
      "$WORK/src/logitech_hidpp.cpp" \
      -o "$BIN" -lpthread 2>/dev/null || { echo "derleme başarısız"; return 2; }
  local out rc fails sum
  out="$(timeout 120 "$BIN" 2>&1)"; rc=$?
  fails="$(printf '%s\n' "$out" | grep -c 'FAIL' || true)"
  sum="$(printf '%s\n' "$out" | tail -1)"
  echo "rc=$rc · FAIL satırı=$fails · $sum"
  # 0 dönerse kapı YEŞİL kaldı (mutasyon kaçtı); 1 dönerse kapı KIRMIZI
  # (mutasyon yakalandı).  Çağıran taraf bu anlamı kullanır.
  [ "$rc" -eq 0 ]
}

run_case() {
  local name="$1"; shift
  cp "$ROOT/src/logitech_hidpp.cpp" "$CPP"
  "$@"
  printf '%-46s' "$name"
  # build_and_run: rc=0 → kapı YEŞİL (mutasyon kaçtı) · rc≠0 → kapı KIRMIZI (mutasyon yakalandı)
  if build_and_run; then
    echo "  → YEŞİL ⚠ (bu mutasyon KAPIDAN KAÇTI — eşdeğer mutant olabilir)"
  else
    echo "  → KIRMIZI ✓ (kapı hatayı yakaladı)"
  fi
}

m1() { # iç kısa-yük koruması gevşetilir (gözlenebilir fark YOK → eşdeğer mutant)
  sed -i 's/if (param_len > HIDPP_SHORT_PAYLOAD_MAX) return std::nullopt;/if (param_len > HIDPP_SHORT_PAYLOAD_MAX + 4) return std::nullopt;/' "$CPP"
}
m2() { # bildirim zulası devre dışı (BUG-22 regresyonu)
  sed -i 's/if (pending_notifications_.size() < 16)/if (false \&\& pending_notifications_.size() < 16)/g' "$CPP"
}
m3() { # yanıt eşleştirme bozulur
  sed -i 's/if (buf\[1\] != request_device || buf\[2\] != feature_index ||/if (buf[1] != request_device || buf[2] != 0xFE ||/' "$CPP"
}

{
  echo "=== AJ-H mutasyon kanıtı — $(date '+%Y-%m-%d %H:%M:%S') ==="
  echo "repo'ya dokunulmaz; mutasyon geçici kopyaya uygulanır"
  echo
} | tee "$OUT/mutasyon_hidpp.log"

run_case "M1: iç kısa-yük koruması gevşetildi" m1 | tee -a "$OUT/mutasyon_hidpp.log"
run_case "M2: bildirim zulası devre dışı"       m2 | tee -a "$OUT/mutasyon_hidpp.log"
run_case "M3: yanıt eşleştirme bozuldu"         m3 | tee -a "$OUT/mutasyon_hidpp.log"
run_case "M0: kontrol (mutasyonsuz)"             true | tee -a "$OUT/mutasyon_hidpp.log"

echo
echo "Repo dosyaları DOKUNULMADI (mutasyon geçici kopyada uygulandı)." | tee -a "$OUT/mutasyon_hidpp.log"
