# DÜZELTME: natural `decay_rate` alt tabanı (L02-14) — negatif kazanç / eksen tersliği

> **L02-15 notu (22 Ekim 2026):** Bu düzeltmenin "üretimde x ≥ 3.125e-4"
> güvencesi `domain_weights=1` varsayar; küçük yasal domain ağırlıkları x'i
> bunun altına indirebilir. O bant için `accel_union::apply()` içine
> `accel*x < 1e-9 → 1.0` kapısı eklendi (bkz. `FIX_LOG.md` round-21,
> `Bug Hata Raporları.md` TUR 57, `natural_small_speed_probe.cpp`). Bu
> belgedeki L02-14 ölçümleri tarihsel kayıt olarak geçerlidir.

**Düzeltme kim:** opencode (kullanıcı adına bağımsız analiz) · 21 Ekim 2026
**Bulgunun kaydı:** [`BULGU-accel-natural-negatif-kazanc.md`](BULGU-accel-natural-negatif-kazanc.md)
**Ölçüm programı:** [`fix_verify.cpp`](fix_verify.cpp) (yeni)
**Kapsam:** LANE-B (`cli/**`, `src/**`, `include/**`) sahipliği AJ-H'de →
değişiklikler `AJANLAR.log`'a duyuruldu (21 Ekim 2026, 23:12).

---

## 1. Hatırlatma: hata neydi

`natural` modunun iç katsayısı `accel = decay_rate / |limit-1|`
(`include/accel-natural.hpp:24`). GAIN dalı iki `O(1/accel)` terimin **farkını**
alır (`include/accel-natural.hpp:208`):

```cpp
double output = limit * (decay / accel - offset_x) - limit / accel;
```

`accel` küçüldükçe bu iki terim birbirini götürür; yuvarlama hatası
`~limit/accel · 2⁻⁵²` büyürlüğüne çıkar ve **kazanç negatif olur**. Negatif
kazanç → `scale` negatif → `in.x *= scale` → **imleç ekseni tersine döner**.
`modifier::modify()`'in sonundaki `isfinite` koruması bunu yakalamaz: değer
sonlu, sadece negatiftir.

Üretimde erişilebilir x bandı:
`x = |counts| × (NORMALIZED_DPI/dpi)/time_ms × domain_weight`,
`NORMALIZED_DPI = 1000`, `time_ms ≤ 100`, `dpi ≤ 32000`
→ **x ≥ 3.125e-4** (32000 DPI / 100 ms / 1 count); 800 DPI'da ≥ 1.25e-2.

## 2. Neden düzeltme FORMÜLDE değil, KONFİGÜRASYON KATMANINDA

Projenin kendi kararı (AGENTS.md, oracle bölümü): port **resmi referansla
bit-bit aynı** olmalı; `decay_rate ≤ 1e-4` bandındaki sapma **inherited**
(inherited sapma — referans da aynı sayıyı veriyor). Formülü değiştirmek:

* referansla uyumu bozar (oracle 1408 satırın sapma listesi değişir),
* "düzeltilmiş" davranışı referanstan ayırır.

Bu yüzden düzeltme **erişilebilirlik** katmanında yapıldı: bandı
**erişilemez** kılan bir alt sınır. Bu, bulgu dosyasındaki önerinin 1. maddesiyle
ve `include/accel-natural.hpp`'in kendi notuyla ("the fix is NOT in this file —
it is a lower bound on decay_rate in the config layer") birebir aynıdır.

Taban değeri **1e-4**: CLI'nin zaten "the established safety floor" dediği ve
`exponent_power` için `sanitize`'de uygulanan değer. Tek taban, tek kaynak.

**0 değeri muaf tutuldu**: `accay = 0` → `accel < 1e-12 → 1.0` guard'ı
(`accel-natural.hpp:206`) → düz kazanç 1.0 (ölçüldü: işaret hatası yok).
0, "natural = düz eğri" demektir ve kullanıcı bunu bilerek seçer.

## 3. Değişiklikler (dosya/satır bazında)

| # | Dosya | Değişiklik |
|---|---|---|
| 1 | `include/config.hpp:61-76` | `NATURAL_DECAY_RATE_MIN = 1e-4` sabiti + gerekçe yorumu |
| 2 | `src/config.cpp:523-535` | `sanitize_accel_args`: `mode == natural && decay_rate > 0 && decay_rate < NATURAL_DECAY_RATE_MIN` → yukarı kırpma |
| 3 | `include/accel-natural.hpp:136-215` | Yanlış beyan ("no reachable input produces a negative gain") ölçümle çürütülüp düzeltildi; "⚠ CORRECTION (2026-10-21)" + "✅ DONE" bloğu; bayat satır referansları güncellendi (`:501`→`:593`, `:452`→`:533-535`, `line ~137 above`→`line 208 below`) |
| 4 | `cli/main.cpp:1549-1580` | `set-param decay_rate` kendi dalına ayrıldı: `DECAY_RATE_MAX` tavanı + `(0, 1e-4)` bandı **moddan bağımsız** rc=1 ile reddediliyor |
| 5 | `cli/main.cpp:1051-1080` | L02-14 uyarısı yeni alana göre daraltıldı: artık yalnız `0 < decay_rate < 1e-4` için (0 artık uyarılmıyor — 0 güvenli) |
| 6 | `cli/main.cpp:3592-3594` | Yardım metni: "Domain 0–10, and in natural mode 0 or ≥1e-4 …" |
| 7 | `tests/test_accel.cpp:3042-3178` | `test_natural_decay_rate_floor()` — 3 SECTION, 12 assertion |
| 8 | `tests/test_accel.cpp:72` | `EXPECTED_TEST_FUNCTIONS` 152 → **153** (kendi içindeki sayım denetimi) |
| 9 | `tests/test_accel.cpp:10323-10327` | `RUN_TEST(test_natural_decay_rate_floor)` kaydı |
| 10 | `docs/research/deviations.md` | Bayat sayaçlar 1047/45 → **1408/79** + sınıf tablosu `known_deviations.txt` ile hizalandı (23+9+0+23+24 = 79) |
| 11 | `AGENTS.md:166,377-378,512,643,+ "Input validation"` | Makine denetimli sayılar (153 fonk / 224 SECTION / 1649 EXPECT / 34417 assertion) + sanitize alan listesine yeni taban |
| 12 | `olcum/aj2/sayisal_iddialar.txt:64` | Regex içindeki literal `34405` → `34417` (denetim regex'i sayıyı içinde taşıdığı için zorunlu) |

## 4. Ölçüm: düzeltme öncesi / sonrası

`fix_verify.cpp` → üretim erişilebilir x bandı (`x ≥ 3.125e-4`),
11 limit × 16 decay_rate × 5 offset × 10 x × 2 gain modu.

```
=== (A) HAM (sanitize YOK) — üretim erişilebilir x bandı ===
skore edilebilir nokta : 10560
NEGATIF kazanc          : 5
en kotu negatif         : -30.250000
  gain=  -30.250000 limit=100     decay=1e-10     x=0.0005      off=0       gain_mode=1
  gain=   -0.562500 limit=20      decay=1e-10     x=0.0003125   off=0       gain_mode=1
  gain=   -0.250000 limit=100     decay=1e-10     x=0.0125      off=0       gain_mode=1
  gain=   -0.250000 limit=100     decay=1e-10     x=0.0125      off=0.001   gain_mode=1
  gain=   -0.250000 limit=100     decay=1e-10     x=0.0125      off=0.01    gain_mode=1

=== (B) SANİTE (gerçek üretim yolu) — üretim erişilebilir x bandı ===
skore edilebilir nokta : 10560
NEGATIF kazanc          : 0

SONUC: ham=5 negatif, sanite=0 negatif -> DUZELTME ETKILI
```

`-0.25` satırı 800 DPI / 100 ms / 1 count noktasıdır (`x = 1.25e-2`) — yani
**tipik masaüstü** hareketi. Düzeltme öncesi bu noktada imleç tersine dönüyordu.

### Yeniden üretim

```bash
g++ -O2 -std=c++20 -I include olcum/analiz/fix_verify.cpp src/config.cpp -o fix_verify && ./fix_verify
```

## 5. Neden her yol kapalı (koruma katmanları)

| Yol | Koruma |
|---|---|
| JSON dosyası elle yazılmış (`decay_rate: 1e-10`) | `load_config` → `device_profile_from_json` → `sanitize_profile` → `sanitize_accel_args` → **taban** |
| GUI'nin IPC ile daemon'a profil göndermesi | `app_config_from_json` → aynı sanitize yolu → **taban** |
| `rawaccel-cli set-param decay_rate 1e-10` | CLI domaini reddediyor (rc=1) → **dosyaya hiç yazılmıyor** |
| `rawaccel-cli set-param mode natural` (önce `decay_rate 1e-9` yazılmışsa) | CLI bandı **moddan bağımsız** reddettiği için değer zaten dosyaya giremiyor |
| GUI arayüzü (`decay_spin`) | `make_spin(0, 10, 0.01, 0.1)`, digits=3 → yalnız 0 veya ≥ 0.001 üretebilir → **GUI hiçbir zaman kırpmaya girmez** (0 muaf) |

## 6. Yan etkiler ve kasıtlı kararlar

* **Referansla uyum bozulmadı**: formül değişmedi; oracle'ın 1408 satırı ve
  79 sapması değişmedi (kapı 3 kanıtı).
* **GUI değişikliği gerekmedi**: arayüzün üretebileceği değerler zaten
  sanitize alanının içinde.
* **`decay_rate = 0` hâlâ geçerli**: "natural = düz eğri" anlamına gelir,
  koruma katmanları devrede (ölçüldü: `accel < 1e-12 → 1.0`).
* **CLI'da `0` uyarılmıyor**: eski L02-14 uyarısı `decay_rate < 1e-4`
  koşuluydu ve güvenli olan `0` için de "gain can go negative" diyordu —
  yanlış uyarıydı. Artık yalnız gerçekten tehlikeli bant uyarılır, kırpmayı
  bildiren ham-vs-saklanan karşılaştırması (`accel_warn "decay_rate"`) ise
  dosyada eski banttan bir değer varsa onu adıyla bildiriyor.
* **CLI reddi moddan bağımsız**: `decay_rate` yalnız natural algoritması tarafından
  okunur, ama mod sonradan değiştirilebilir; moddan bağımsız reddetmek
  "clamp-on-load sürprizi"ni baştan keser.

## 7. Kalan (bilinçli yapılmayan) öneri

**GAIN dalında `gain < 0 → 1.0` savunma katmanı** (`accel-natural.hpp:208`
sonrası) yapılmadı. Gerekçe: (a) referansla bit-bit uyumu bozar,
(b) taban bandı erişilemez kıldığı için gereksiz. Yine de **çift katmanlı
güvenlik** isteyen bir yönetici kararı varsa, bu satır tek başına eklenebilir
(ölçüm: eklenmesi oracle'da yeni sapma üretmez, çünkü hiçbir oracle satırı
o banda düşmez — yani **referans uyumu bozulmaz**; asıl maliyeti, referansın
kasten negatif kazandığı bir noktada yerel portun farklı davranması olur ki
o nokta zaten üretimde erişilemez).

## 8. Doğrulama kapıları

7 kapı (build · run_tests · oracle · simd · tr · cli-asan · tracker) düzeltme
sonrası yeniden koşuldu — sonuçlar: `olcum/analiz/GATE-SONUC.md` ve
`olcum/analiz/gate-*.log`.
