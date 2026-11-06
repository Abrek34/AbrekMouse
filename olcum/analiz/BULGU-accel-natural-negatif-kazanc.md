# BULGU: `accel-natural` GAIN modunda **negatif kazanç** → imleç eksen tersliyor

> ## ⚠️ L02-15 GÜNCELLEMESİ (22 Ekim 2026)
>
> Bu belgedeki "üretim tabanı x ≥ 3.125e-4" ölçümü **domain_weights = 1**
> varsayar. `domain_weights` yasal olarak 0'a kadar inebildiği için x daha
> küçük olabilir ve GAIN dalı yalnız negatif değil **pozitif** spike da
> üretebilir (ölçülen 5.77× @ x=3.125e-9). Bu yüzden üretim dağıtımına
> `accel*x < 1e-9 → 1.0` kapısı eklendi. Ayrıntı: `FIX_LOG.md` round-21,
> `Bug Hata Raporları.md` TUR 57 ve
> [`natural_small_speed_probe.cpp`](natural_small_speed_probe.cpp).
> Aşağıdaki metin L02-14 düzeltmesinin tarihsel kaydıdır.

> ## ✅ DURUM: **DÜZELTİLDİ** (21 Ekim 2026, aynı gün)
>
> Kullanıcı "düzelt" isteği üzerine bulgu **kodda düzeltildi**: `decay_rate`
> alt tabanı `sanitize_accel_args` katmanına eklendi
> (`NATURAL_DECAY_RATE_MIN = 1e-4`, `include/config.hpp` + `src/config.cpp`),
> CLI'nin `set-param` domaini buna aynılandı, yanlış beyan düzeltildi ve
> `test_natural_decay_rate_floor()` regresyon testi eklendi.
> Düzeltme kanıtı: [`BULGU-duzeltme-natural-decay-tabani.md`](BULGU-duzeltme-natural-decay-tabani.md).
> Aşağıdaki metin **düzeltme ÖNCESİ ölçümün** kaydıdır (kanıt olarak korunur).

**Bulgu kim:** opencode (kullanıcı adına bağımsız analiz) · 21 Ekim 2026
**Kapsam:** salt okunur analiz + kanıt üretimi. **Kod değişikliği YAPILMADI.**
(Düzeltme sonraki bölümde ayrı dosyada belgelendi.)
**Etkilenen dosya:** `include/accel-natural.hpp` (beyan satırı :151-153)
**Korumasız yol:** `src/config.cpp` (sanitize) + `daemon/daemon.cpp` (uygulama)

---

## 1. Kodda yazan beyan (yanlış)

`include/accel-natural.hpp:146-153`:

> It is a 19.5% error, NOT a sign flip and NOT a NaN:
> **no reachable input produces a negative gain** (measured over the same 864 points).

Bu beyan **ölçümle çürütüldü** (aşağıda). Ayrıca projenin kendi içinde
**çelişki** var: `cli/main.cpp:1051-1063` (L02-14) aynı olgu için şunu diyor:

> natural's internal accel coefficient is decay_rate/limit; with a tiny decay_rate
> the GAIN branch's cancellation can flip the gain negative (measured: negative
> gain / inverted axis at limit=100 decay=1e-9).

Yani **CLI sorunu biliyor**, **algoritma başlığı "olamaz" diyor**.

## 2. Erişilebilir kutu (sanitize sınırlarından türetildi)

| Parametre | sanitize sınırı | Kaynak |
|---|---|---|
| `limit` | `[0, 100]` (LIMIT_MAX) | `src/config.cpp:534` (alt) + `:580` (üst) |
| `decay_rate` | `>= 0`, **üst sınır YOK** | `src/config.cpp:522` |
| `input_offset` | `>= 0` | `src/config.cpp:527` |
| `gain` | bool | `src/config.cpp` |

`decay_rate` için **alt taban 1e-4 YOK** — CLI'nin yorumunda "1e-4 is the
established safety floor (… recommended config.cpp base …)" deniyor ama
`config.cpp`'de bu taban **hiç uygulanmamış**.

## 3. Ölçüm (üretim kodunun kendisi, `accel-natural.hpp` doğrudan include edilerek)

`olcum/analiz/neg_gain.cpp` → 9 limit × 12 decay_rate × 13 hız × 2 mod = **2808 nokta**

```
toplam skore edilebilir nokta : 2808
NEGATIF kazanc noktasi         : 4
en kotu negatif kazanc         : -46.683716
  limit=50 decay_rate=1e-06 x=1e-08 gain_mode=1
```

Negatif olan (limit, decay) çiftleri:

| limit | decay_rate | x | gain |
|---|---|---|---|
| 50 | 1e-6 | 1e-8 | **-46.68** |
| 100 | 1e-7 | 1e-6 | **-14.26** |
| 10 | 1e-9 | 1e-5 | -0.526 |
| 0 | 1e-9 | 1e-7 | -0.192 |

## 4. Negatif bölgenin üretim tabanına göre erişilebilirliği

Üretimde hız ölçeği (`include/rawaccel.hpp:601`):
`x = |counts| × (NORMALIZED_DPI/dpi) / time_ms × domain_weight`,
`NORMALIZED_DPI = 1000` (`include/rawaccel-base.hpp:45`),
`time_ms ∈ [DEFAULT_TIME_MIN, DEFAULT_TIME_MAX=100]` (`daemon/daemon.cpp:2646-2647`),
`dpi ≤ 32000`.

→ **üretim tabanı**: `x ≥ 1000/32000/100 = 3.125e-4` (32000 DPI, 100 ms boşluk, 1 count)
→ tipik masaüstü (800 DPI): `x ≥ 1.25e-2`

`olcum/analiz/neg_gain2.cpp` → negatif bölgenin **x üst sınırı** arandı:

| limit | decay_rate | x_neg_max | min_gain | 32000-DPI tabanı ≥ x_neg_max ? |
|---|---|---|---|---|
| 100 | 1e-10 | 0.0148 | -0.0557 | **EVET** (üstü) |
| 50 | 1e-10 | 0.003079 | -0.2688 | **EVET** |
| 100 | 1e-9 | 0.001951 | -0.00094 | **EVET** |
| 10 | 1e-11 | 0.000792 | -0.2330 | **EVET** |
| 50 | 1e-9 | 0.000484 | -0.00867 | **EVET** |
| 20 | 1e-10 | 0.000472 | -0.0351 | **EVET** |
| 100 | 1e-8 | 0.000239 | -0.0205 | hayır |

→ Negatif bölge **üretim tabanının üstüne çıkıyor**; sadece "ölçülebilir
uç" değil, fiziksel olarak ulaşılabilir bir aralık.

## 5. UÇTAN UCA DOĞRULAMA — gerçek `modifier::modify()`

`olcum/analiz/prod_path.cpp`: profili kurar (`mode=natural`, `gain=true`,
`limit=100`, `decay_rate=1e-10`) ve **üretim yolundan** geçirir
(`modifier::modify()` → `scale = 1 + (gain-1)*range_weight` → `in *= scale`).

```
dpi      time_ms   in.x         out.x        not
800      100       1               -0.250000   <== TERS YON
1600     100       2               -0.500000   <== TERS YON
1600      50       1               -0.250000   <== TERS YON
32000     80       1              -39.000000   <== TERS YON
...
4/63 durumda imlec TERSINE cevrildi (in.x>0 idi)
```

**`in.x = +1` iken çıktı `-0.25` / `-39.0`** → imleç girdiğinin **ters yönüne**
gidiyor. 800 DPI + 100 ms boşluk + 1 count: fare hareketsizlikten sonra
1 count'lık en ufak hareketi — fiziksel olarak **çok olağan**.

## 6. Neden sadece "uç" değil, bir güvenlik açığı

`scale` negatif olduğu için `in.x *= scale` imleci ters çevirir; bu
`modifier::modify()`'in sonundaki `isfinite` korumasına **takılmaz** (değer
sonlu, sadece negatiftir). Yani üretimdeki "defense-in-depth" burada işe yaramaz.

## 7. Öneri (düzeltme ÖNCESİ yazılmıştı — durum aşağıda)

1. **`src/config.cpp` `sanitize_accel_args`**: `natural` + GAIN modunda
   `decay_rate` için alt taban (`1e-4` — CLI yorumunda zaten "the established
   safety floor" diye yazılı). Bu, L02 raporunun önerdiği ve **uygulanmamış**
   satırdır. Uygulanırsa GUI/CLI/daemon üçü birden korunur (tek kesişim
   noktası).
2. **Beyan düzeltmesi** (`include/accel-natural.hpp:151-153`): "no reachable
   input produces a negative gain" cümlesi ölçümle yanlışlanmıştır; gerçek
   olan, **"negatif kazanç üretilebilir ve `decay_rate` alt tabanı olmadan
   eksen tersine döner"**.
3. **`include/accel-natural.hpp` GAIN dalına savunma katmanı**: `output/x + 1`
   sonrası `gain < 0` ise 1.0'a dönmek (referansla bit-bit uyum bozulmadan
   *değil* — bu yüzden önce 1. maddedeki taban uygulanmalı, 3. madde ek
   güvenlik olarak düşünülmeli).
4. **CLI eşiği**: uyarı `decay_rate < 1e-4` tetikliyor (doğru ve muhafazakâr);
   sorun uyarının yokluğu değil, **daemon'da hiçbir koru olmaması** ve
   config dosyasından doğrudan girilebilmesi.

### 7.1 Bu maddelerin 21 Ekim 2026 itibarıyla DURUMU

| # | Öneri | Durum | Nerede |
|---|---|---|---|
| 1 | sanitize'da natural `decay_rate` alt tabanı | ✅ **UYGULANDI** | `include/config.hpp` (`NATURAL_DECAY_RATE_MIN = 1e-4`), `src/config.cpp` (`sanitize_accel_args`) |
| 2 | Yanlış beyanın düzeltilmesi | ✅ **UYGULANDI** | `include/accel-natural.hpp` — "⚠ CORRECTION (2026-10-21)" bloğu + ✅ DONE notu |
| 3 | GAIN dalında `gain < 0 → 1.0` savunma katmanı | ⏸ **YAPILMADI (bilinçli)** | Referansla bit-bit uyumu bozacağı için; taban bandı erişilemez kılınca gereksiz. Aşağıda "kalan öneri" olarak duruyor. |
| 4 | CLI eşiği | ✅ **UYGULANDI** (daha da sıkı) | `cli/main.cpp` — `set-param decay_rate` artık `(0, 1e-4)` bandını rc=1 ile reddediyor; `validate` komutunun ham-vs-saklanan karşılaştırması zaten kırpmayı bildiriyor |
| + | Regresyon testi | ✅ **EKLENDİ** | `tests/test_accel.cpp` — `test_natural_decay_rate_floor()` (3 SECTION, 12 assertion) |
| + | Uçtan uca kanıt | ✅ **EKLENDİ** | `olcum/analiz/fix_verify.cpp` — üretim erişilebilir kutuda HAM 5 negatif → SANİTE 0 negatif |

Ayrıntı ve ölçüm çıktıları: [`BULGU-duzeltme-natural-decay-tabani.md`](BULGU-duzeltme-natural-decay-tabani.md).

## 8. Yeniden üretim

```bash
cd /tmp/opencode/analiz
g++ -O2 -std=c++20 -I "<REPO>/include" neg_gain.cpp  -o neg_gain   && ./neg_gain
g++ -O2 -std=c++20 -I "<REPO>/include" neg_gain2.cpp -o neg_gain2  && ./neg_gain2
g++ -O2 -std=c++20 -I "<REPO>/include" prod_path.cpp -o prod_path  && ./prod_path
```
