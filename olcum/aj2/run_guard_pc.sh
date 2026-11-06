#!/usr/bin/env bash
# AJ2 · R16 guard'ın üç ZORUNLU PC'sini koşar (ölçüm aracı — üretim kodu DEĞİL).
#
# AJ1 §5'in PC listesini birebir uygular:
#   PC1 DUYARLILIK  — guard SONLU girdilerin sonucunu değiştirmiyor
#   PC2 ETKI        — guard sonlu olmayan bileşeni sonlu yapıyor
#   PC2'nin PC'si   — guard'ı geri alan mutasyon aynı satırları inf'e döndürüyor
#   PC3             — AYRI KAPI: tests/oracle/run_oracle.sh (1407/79/79/OK)
#
# ⚠️ MUTASYON NASIL YAPILIR (AJ1 README'sindeki "include gölgelemesi" tuzağı):
#   include/math-vec2.hpp'yi -I ile gölgelemek İŞE YARAMAZ: tırnaklı include
#   önce içeren dosyanın dizinini arar. Doğru yol: include/ dizininin
#   TAMAMINI kopyala, guard satırını KOPYADA sil, -I'yi kopyaya ver.
#   (Ayrıca #define ile aynı kaynaktan iki yol türetmek denendi — işe yaramadı:
#   iki ikili BAYT bayt aynı çıktı, yani mutasyon fiilen bir NO-OP'tu ve
#   PC2 "geçti" derken hiçbir şey ölçmemiş olurdu. Bu, sessizce geçen
#   ölçümün en kötü hâlidir ve burada PC ile yakalandı.)
#
# Kullanım: bash olcum/aj2/run_guard_pc.sh
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
GUARD_LINE='    if (!std::isfinite(ax) || !std::isfinite(ay)) return 0;'

echo "### 0) kaynak doğrulaması (dosya HEMEN ÖNCE varlık + boyut)"
for f in include/math-vec2.hpp olcum/aj2/cmp_guard.cpp; do
  if [[ ! -f "$ROOT/$f" ]]; then echo "YOK: $f — ölçüm çalışmadı"; exit 2; fi
  stat -c '  %n %s bayt' "$ROOT/$f"
done

# --- mutasyonlu include ağacı: guard satırı silinmiş kopya ---
cp -r "$ROOT/include" "$WORK/include_mut"
MUT="$WORK/include_mut/math-vec2.hpp"
if ! grep -qF "$GUARD_LINE" "$MUT"; then
  echo "MUTASYON YAPILAMADI: guard satırı bulunamadı (yorum değişmiş olabilir)"; exit 2
fi
python3 - "$MUT" "$GUARD_LINE" <<'PY'
import io,sys
p,line=sys.argv[1],sys.argv[2]
s=io.open(p,encoding="utf-8").read()
assert line in s
io.open(p,"w",encoding="utf-8").write(s.replace(line,"    // [MUTASYON] guard kaldirildi",1))
PY
echo "### 1) mutasyon uygulandi:"
diff <(grep -c . "$ROOT/include/math-vec2.hpp") <(grep -c . "$MUT") >/dev/null \
  && echo "  ⚠️ satir sayisi degismedi — sed etkisiz mi?" || echo "  satir sayisi degisti (beklenen)"

echo "### 2) derleme"
g++ -std=c++20 -I "$ROOT/include"                 -o "$WORK/guard"    "$ROOT/olcum/aj2/cmp_guard.cpp" 2>&1 | head -5
g++ -std=c++20 -I "$WORK/include_mut"            -o "$WORK/mutasyon" "$ROOT/olcum/aj2/cmp_guard.cpp" 2>&1 | head -5
for b in guard mutasyon; do [[ -x "$WORK/$b" ]] || { echo "derleme basarisiz: $b"; exit 2; }; done
echo "  guard=$(stat -c%s "$WORK/guard")  mutasyon=$(stat -c%s "$WORK/mutasyon") bayt"
if cmp -s "$WORK/guard" "$WORK/mutasyon"; then
  echo "  ⛔ İKİ İKİLİ BAYT BAYT AYNI — mutasyon NO-OP, PC ölçümü anlamsız. DUR."; exit 3
fi
echo "  ✓ ikililer farklı (mutasyon gerçekten kod değiştirdi)"

echo "### 3) koşum"
"$WORK/guard"    > "$WORK/g.txt"; echo "  guard    exit=$?"
"$WORK/mutasyon" > "$WORK/m.txt"; echo "  mutasyon exit=$?"

python3 - "$WORK/g.txt" "$WORK/m.txt" <<'PY'
import io,sys
def rows(p):
    # ROW\t<i>\t<etiket>\t<got>\t<ref> -- etiket bosluk icerir, TAB ayirici
    d={}
    for ln in io.open(p,encoding="utf-8"):
        if ln.startswith("ROW\t"):
            f=ln.rstrip("\n").split("\t")
            d[int(f[1])]=(f[2],f[3],f[4])
    return d
g,m = rows(sys.argv[1]), rows(sys.argv[2])
PC1=range(0,7)   # sonlu girdiler
fails=[]
# PC1: guard sonlu girdilerin SONUCUNU degistirmemeli
print("\n--- PC1 duyarlılık: sonlu girdilerde guard öncesi/sonrası ---")
for i in PC1:
    ad,gg,_ = g[i]; _,gm,_ = m[i]
    same = (gg==gm)
    print(f"  [{'OK ' if same else 'FARK'}] {ad.strip():34s} guard={gg:>24s} mutasyon={gm:>24s}")
    if not same: fails.append(f"PC1 sonlu girdi degisti: {ad.strip()}")
# PC2: sonlu olmayan girdide guard SONLU döndürmeli.
#   AJ1 "mutasyon inf'e döndürmeli" dedi; ÖLÇÜM BUNU YANLIŞ ÇIKARDI.
#   (NaN,3) guard'sızken inf DEĞİL, "uydurma sayı" 3 dönüyordu (NaN>3 false
#   olduğu için M=3 seçiliyor). Doğru ölçüt: guard'ın o satırda DEĞER
#   DEĞİŞTİRMESİ — yani mutasyonun ya sonlu olmayan ya da uydurma döndürmesi.
print("\n--- PC2 etki: sonlu olmayan bileşen ---")
BOZUK = ("inf","-inf","nan")
degisen = 0
for i in range(10,13):
    ad,gg,_ = g[i]; _,gm,_ = m[i]
    g_sonlu = gg not in BOZUK
    ok = g_sonlu and (gm != gg)
    if ok: degisen += 1
    print(f"  [{'OK ' if ok else 'FARK'}] {ad.strip():34s} guard={gg:>24s} mutasyon={gm:>24s}")
    if not ok: fails.append(f"PC2 guard sonlu olmayanda karsi tarafi dondurmedi: {ad.strip()}")
print("\n--- PC2 PC'si: mutasyon guard'in ETKISINI geri almali (3 satirda da) ---")
print(f"  guard'in degistirdigi satir sayisi = {degisen} (3 olmali; 0 ise alet duyarsiz)")
if degisen < 3: fails.append(f"PC2 PC'si zayif: mutasyon yalnizca {degisen}/3 satırda fark yaratti")
print("\n=== PC hükmü: " + ("PASS" if not fails else "FAIL") + " ===")
for f in fails: print("  ! "+f)
sys.exit(1 if fails else 0)
PY
rc=$?
echo
echo "### 4) PC3 — AYRI KAPI, bu script ÇALIŞMAZ:"
echo "    bash tests/oracle/run_oracle.sh    # 1407/79/79/OK olmalı"
exit $rc
