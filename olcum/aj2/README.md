# `olcum/aj2/` — AJ2 (space-bunny-free) ölçüm araçları

Bu dizin **yazan yol** kuralı için burada: araç repoda değilse, kimse
çalıştırmıyor ve ölçümün arkasındaki gerekçe kayboluyor. Her araç, kendini
doğrulayan bir **pozitif kontrol** ile birlikte gelir — PCsiz bir "temiz"
sonucu kanıt değildir.

## `prove_kod_ayni.py` — "kod değişmedi" iddiasını ölçümle kanıtlar

Yorum eklendiğini söylemek yetmez; derlenmiş artefaktı değiştirip
değiştirmediğini göstermek gerekir. Yorumlar ve boş satırlar soyulur, satır
içi `//` silinir, kalan çıktı bayt bayt karşılaştırılır.

```bash
python3 olcum/aj2/prove_kod_ayni.py --git include/math-vec2.hpp   # HEAD~1 -> HEAD
python3 olcum/aj2/prove_kod_ayni.py once.hpp sonra.hpp
python3 olcum/aj2/prove_kod_ayni.py --pc include/math-vec2.hpp    # ZORUNLU
```

`--pc` bilinen bir mutasyonla sayının değiştiğini gösterir. **Bunu
atlamadan "AYNI" sonucu kanıt değildir** — alet duyarsızsa daima 0 döner.

Bu projede yolun açtığı üç tuzak, aracın docstring'inde ve burada:

1. **grep locale tuzağı.** `LANG=tr_TR.UTF-8` altında `grep -oE "[A-Za-z0-9_]+"`
   eşleşmeyi sessizce keser (`_mm256_div_pd` → `_mm256_d`) ve **eşleşme
   sayısı doğru çıkar**, yani gözle kontrol eden "12 intrinsic var, tamam"
   der ve geçer. Her ölçümde `LC_ALL=C` ya da `[[:alnum:]]` ya da Python `re`.
2. **include gölgelemesi.** `simd_parity.cpp → rawaccel.hpp → "simd_math.hpp"`
   tırnaklı include; **önce include eden dosyanın dizinini** arar, `-I` sırası
   işe yaramaz. Başlığı değiştirmek için `include/` dizinin **tamamını**
   kopyala, sonra kopyada değiştir. (Symlink denemesi de başarısız:
   `#pragma once` ayrışmıyor, yeniden tanım hataları.)
3. **Ölü kod.** `return;`'den sonra yazılan mutasyon hiçbir şey yapmaz. Mutasyonu
   yazdıktan sonra **canlı olduğunu doğrula**.

## Ölçülen sonuçlar (2026-09-28)

| dosya | yorum eklendi | yorumsuz kod | sonuç |
|---|---|---|---|
| `include/accel-natural.hpp` | 109 satır | değişmedi | `+/0` |
| `include/accel-power.hpp` | 21 satır | değişmedi | `+/0` |
| `include/simd_math.hpp` | 56 satır | 241 → 241 | byte-level ayni |
| `include/math-vec2.hpp` | 35 satır | 45 → 45 | byte-level ayni |

Commit'ler: `b01342b1`, `207732a3`, `36a53c8c`.

## ⛔ Hangi dosyayı HANGİ KAPI doğruluyor — karıştırma

Bu ayrım ölçülerek konuldu (2026-09-28) çünkü "oracle'da gerçek fark yok"
gerekçesi **üç dosyanın ikisinde geçerli değil**:

| dosya | `run_oracle.sh` | hangi kapı gerçekten doğruluyor |
|---|---|---|
| `include/accel-*.hpp` | ✅ 1407 satır | `run_oracle.sh` **+** `run_tests.sh` |
| `include/simd_math.hpp` | ❌ hiç çağrılmıyor | `run_simd_parity.sh` (kendi envanter denetimli) |
| **`include/math-vec2.hpp`** | ❌ **hiç çağrılmıyor** | **yalnız `run_tests.sh`** |

Ölçüm — oracle harness'inde (`local.cpp` + `reference.cpp`) ve
`oracle_cases.hpp`'te geçen sayılar:

```
  lp_distance        harness 0   oracle_cases 0
  calc_speed_whole   harness 0   oracle_cases 0
  speed_processor    harness 0   oracle_cases 0
  rotate             harness 0   oracle_cases 0
  magnitude          harness 0   oracle_cases 2   (alan adi, cagri degil)
```

Yani oracle ızgarası hızı **skaler** verip doğrudan kazanç eğrisine sokuyor;
**vektör → hız** katmanı (`speed_processor`, `lp_distance`, `magnitude`,
`rotate`) ne yerelde ne referansta **hiç çalışmıyor**.

`simd_math.hpp` için bu boşluğu `run_simd_parity.sh`'in `BILINEN_OLUMLER`
envanteri kapatıyor. **`math-vec2.hpp` için hiçbir şey kapatmıyor** — onu
sadece `run_tests.sh`'in 34k iddiası doğruluyor, ve o iddialar
`lp_distance`'ı doğrudan, çoğu **sonlu** girdiyle çağırıyor.

**Bu yüzden:** `math-vec2.hpp` hakkındaki bir değişikliği gerekçelendirirken
*"oracle 1407 satırı yeşil"* demek **yanlış kapsamdır** — doğrusu
*"bu katman oracle'ın dışında; doğrulaması `run_tests.sh` + ölçülen
erişilebilirlik zinciri"*. Oracle yeşil kalması bu katmanda **bilgi
vermez**, çünkü katman orada çalışmıyor.

