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

Bunların commit'leri: `b01342b1`, `207732a3`, `36825e55`.
