#!/usr/bin/env bash
# AJ1 — CI perf-gate "ATLANDI" yollarının görünürlüğünü kanıtlar.
#
# Soru: .github/workflows/ci.yml'de atlanan perf kapısı, ölçüm hiç yapılmadan
#       yeşil tik mi gösteriyordu? (Evet — düzeltildi.)
#
# Neden önemli: `exit 0` doğru bir karar (kullanılamayan host bir regresyon
# DEĞİLDİR ve pipeline'ı düşürmemek gerekir), ama tek başına GitHub'da yeşil
# tik demektir. Hiçbir şey ölçülmemişken yeşil tik, doğrulama iddiasıdır. Bu
# repo aynı şeyi başka yerde açıkça yasaklıyor: run_cli_sanitized.sh:20 ve
# run_simd_parity.sh "sessizce atlamaz, gürültülü atlar" diyor.
#
# Bu betik GERÇEK run: bloğunu .github/workflows/ci.yml'den çıkarır, sahte
# bench betikleriyle dört yolu koşturur ve her birinin hangi GitHub işaretini
# ürettiğini gösterir. YAML katmanı (indent/run: ayrıştırma) kapsam dışıdır;
# bash -n ile sözdizimi ayrıca doğrulanır.
#
# Çıkış kodu: 0 = beklenen tablo, 1 = sapma.
set -u

CI="${1:-.github/workflows/ci.yml}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
fails=0

# ── 1) run: bloğunu çıkar ────────────────────────────────────────────────────
# "run: |" sonrasındaki gövde, 8 boşluk girintili; iki nokta üst üste boş
# satırla sınırlıdır (blok sonu). Bulamazsak hata ver — sessizce boş dosyayı
# test etmek bu betiğin kendi amacına aykırı.
start=$(grep -n '^ *run: |' "$CI" | tail -1 | cut -d: -f1)
[ -n "$start" ] || { echo "HATA: '$CI' içinde 'run: |' bulunamadı"; exit 1; }
body=$(awk -v s="$start" 'NR>s { if ($0 ~ /^[[:space:]]*$/ && seen) exit; sub(/^        /,""); print; if ($0 !~ /^[[:space:]]*$/) seen=1 }' "$CI")
[ -n "$body" ] || { echo "HATA: run: bloğu boş çıkarıldı"; exit 1; }
printf '%s\n' "$body" > "$WORK/step.sh"

if bash -n "$WORK/step.sh" 2>"$WORK/syn.err"; then
  echo "  bash -n (sözdizimi)        : TEMİZ  ($(wc -l < "$WORK/step.sh") satır)"
else
  echo "  bash -n (sözdizimi)        : ⛔ HATA"; sed 's/^/      /' "$WORK/syn.err"; fails=$((fails+1))
fi

# ── 2) dört durumu sahte bench'lerle koştur ──────────────────────────────────
for rc_in in 77 126 1 0; do
  cat > "$WORK/fake$rc_in.sh" <<EOF
exit $rc_in
EOF
  chmod +x "$WORK/fake$rc_in.sh"
  # Betik, scripts/bench_hotpath.sh'i koşturan yeri sahte ikiliyle değiştirir.
  sed "s#^ *bash scripts/bench_hotpath.sh#bash $WORK/fake$rc_in.sh#" "$WORK/step.sh" > "$WORK/run$rc_in.sh"
  out=$(bash "$WORK/run$rc_in.sh" 2>&1); job_rc=$?
  ann=$(printf '%s' "$out" | grep -oE '::(warning|error)::' | head -1)
  [ -z "$ann" ] && ann="(yok)"

  case $rc_in in
    77)  exp_rc=0; exp_ann="::warning::" ;;
    126) exp_rc=0; exp_ann="::warning::" ;;
    1)   exp_rc=1; exp_ann="::error::"   ;;
    0)   exp_rc=0; exp_ann="(yok)"        ;;
  esac

  ok=PASS
  [ "$job_rc" = "$exp_rc" ] || { ok=FAIL; fails=$((fails+1)); }
  [ "$ann" = "$exp_ann" ]   || { ok=FAIL; fails=$((fails+1)); }

  printf "  bench exit %-3s -> job rc=%s  işaret=%-13s  beklenen rc=%s işaret=%-13s %s\n" \
    "$rc_in" "$job_rc" "$ann" "$exp_rc" "$exp_ann" "$ok"
done

# ── 3) regresyon kontrolü: yalnız uyarı, asla yeşil/sessiz kalmamalı ─────────
# Bu, düzeltmenin asıl iddiası: atlanan ölçüm SARı olmalı, ölçülen PASS
# sessiz yeşil kalmalı. İkisi de aynı anda mümkün değil — çakışma olsaydı
# tabloda görünürdü (77/126 uyarı verirken 0 da uyarı verse mutasyon yok).
echo
if [ "$fails" -eq 0 ]; then
  echo "  SONUÇ: PASS — atlanan ölçüm uyarı üretiyor, ölçülen geçiş sessiz kalıyor."
  exit 0
fi
echo "  SONUÇ: FAIL — $fails sapma"
exit 1
