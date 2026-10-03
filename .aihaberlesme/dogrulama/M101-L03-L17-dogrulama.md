# ⭐ AJ1 BAĞIMSIZ DOĞRULAMA — L03 ve L17

**Tarih:** 01 eylül 2026 · **Denetleyen:** AJ1 · **Kural:** alt-ajan raporu
kabul edilmez; her iddia **kaynaktan** yeniden ölçülür.

---

# L03 — Çekirdek tipler + vektör

## ⭐ L03-01 · CRIT · **KULLANICI VERİSİ SİLİNİYOR VE DİSKE YAZILIYOR** ✅ DOĞRULANDI

### İddia
Kullanıcının elle çizdiği LUT, GUI'de **tek bir repaint** ile siliniyor ve
`save_config` ile **diske yazılıyor**. `gain(500)`: **33.3 → 500.0 (15×)**.

### AJ1'in ölçümü — mekanizma, satır satır

| # | Kanıt | Satır |
|---|---|---|
| 1 | `mutable float data[LUT_RAW_DATA_CAPACITY] = {};` | `rawaccel-base.hpp:81` |
| 2 | `fill([&](double x){...}, args, range);` — lambda `args`'ı **referansla** yakalıyor | `accel-synchronous.hpp:129` |
| 3 | `static void fill(Func fn, const accel_args& args, ...)` — imza `const&` | `accel-synchronous.hpp:136` |
| 4 | `compute_curve(const accel_args& args, ...)` → `au.init(args)` | `gui/graph.inl:53,56` |
| 5 | `draw_curve(dp.prof.accel_x, C_CURVE)` — **canlı config nesnesi** referansla | `gui/graph.inl:165` |

### ⭐ KÖK NEDEN — `const` sözleşmesi yalan söylüyor

`mutable` üye + `const&` imzası birleşince, derleyici "değiştirilemez" güvencesi
**verilemez**. Kod derleniyor çünkü `mutable` bunu açıkça serbest bırakıyor.

⭐ Dosyanın **kendi yorumu** bunu teyit ediyor (`rawaccel-base.hpp:83`):
> *"Use field-by-field comparison — **memcmp is unreliable with mutable/padding members**."*

Yani `mutable` **bilinçli** bir karar; ama yan sonucu fark edilmemiş:
`const accel_args&` artık hiçbir şey garanti etmiyor.

### ⭐ Neden 34.164 iddia yeşilken olmuyor?

```
grep -rn 'compute_curve' tests/   →   0
```
GUI'nin repaint yolunun **hiçbir testi yok**. Kapı bu sınıfı yapısal olarak
**göremez** — çünkü ölçtüğü şey `daemon.cpp:1237`'deki **kopyalama** yolu;
GUI'de kopya yok, doğrudan canlı nesne var.

## L03-02 · HIGH ✅ · `cap = {15, 0}` varsayılanı
`rawaccel-base.hpp:78` → `{15, 0}`. Oracle / docs / `default.json` → **1.5**.
`"cap"` anahtarı olmayan bir config'te 100000 ips'te **501× vs 1.5×** fark.
Oracle görmüyor çünkü `oracle_cases.hpp:35` cap'i **açıkça** atıyor.

## L03-04 · ⭐ YANIT · soru 6
`rawaccel.hpp` içinde **hiçbir** sanitize/clamp **tanımlı değil** (hepsi
`config.cpp:400`). Ama `reset_smoothers()` (`:224`) tanımlı ve
**hiçbir yerden çağrılmıyor** — mutasyonla kanıtlandı (pozitif kontrol:
aynı yöntem `reconfigure`'da basıyor).
⭐ Yan not: prodüksiyonun **tek** `speed_processor` girişi olan
`reconfigure()`'ı **hiçbir test çağırmıyor**.

## ⭐ Soru 2 (simetri) — sapma DEĞİL
14 alanın 13'ü tek taraflı `[0, M]`, `|alt| = 0 ≠ M`. Yalnız `acceleration`
çift taraflı. Ölçüldü ve **bilinçli politika** — bulgu değil.

---

# L17 — GUI paneller / profil / grafik

## ⭐ L17-ANA · CRIT · **İKİ EKRANDA İKİ FARKLI "GAIN"** ✅ DOĞRULANDI

| Ekran | Değişken | Kaynak | Tanım |
|---|---|---|---|
| Grafik Y ekseni | `graph_Y` | `accel_union::apply(speed)` | **sadece eğri** |
| Fare testi "Gain (×)" | `telem_gain` | `out_ips/in_ips` (`daemon.cpp:2532`) | eğri **× output-DPI normalizasyonu** |

Ölçülen ayrışma (gaming, output_dpi=1000):
```
dpi=800    grafik 1.125   oyun 1.400   (+24 %)
dpi=3200   grafik 1.125   oyun 0.350   (−69 %)
```

**En kötü hali:** `noaccel` profilinde gain tanım gereği **≡1** iken fare testi
**2.5 / 1.25 / 0.63 / 0.32** gösteriyor. İlişki mutasyonla doğrulandı:
`telem_gain == curve_gain × dpi_adjustment`, **5/5** tuttu.

⭐ `graph.inl`de `output_dpi`/`dpi_factor`/`NORMALIZED_DPI` **0 eşleşme** —
yani GUI farkın **varlığından** bile haberdar değil.

### ⭐ Brifing varsayımı YANLI çıktı (doğru çürütme)
"Ayrı kopya var mı?" → **YOK**. `graph.inl:53-67` gerçek
`accel_union::apply`'i çağırıyor. Sapma **kopyadan** değil, daemon'ın eklediği
**görünmeyen çarpandan**. BenimLane brifingim yanlış öncül sunmuştu.

## Diğerleri
- **CRIT** `graph.inl:165` — ham 1:1 (`raw_passthrough`) modda bile eğri
  çiziliyor; oyun tam 1.0 görür. `raw_passthrough` **0 eşleşme**.
- **HIGH** `daemon_comm.inl:503` — "Tüm cihazlar" modunda **başka fare**
  ölçülüyor, pencere cihaz kimliğini göstermiyor (ölçüm: `gain=1` vs gerçek `0.25`).
- **HIGH** `hidpp_panel.inl:588` — LOD **yazılmıyor**, `ok_lod=true` ile
  **"başarılı"** raporlanıyor. (DPI/rate dürüst, yalnız LOD kırık.)
- **HIGH** `daemon.cpp:2347→2543` — gösterilen gecikme **ivme hesabını ölçmüyor**:
  `t_now` girişte okunuyor, `modifier::modify()` (`:2510`) dışarıda kalıyor →
  **%83.5** hariç.

## ✅ L17'nin ölçüp **doğru** çıktıkları
Eksen etiketleri geometrik olarak **0 uyuşmazlık**. X ekseni **ters değil**
(1000 count @1000 DPI/1ms → 1000 ips). Zoom/pan kesin yeniden ölçekliyor.
LUT noktaları eğriyle tutarlı. `now_ns`/`CLOCK_MONOTONIC_RAW` doğru.
2 sn bayatlama kapısı doğru.

---

# ⭐ ORTAK ÖRÜNTÜ (L03 + L04 + L10 + L11 + L16 + L17)

Bu altı lane'in bulguları tek bir kalıbı gösteriyor:

## "ÖLÇÜM VARSA GÖSTERİYOR, AMA ÖLÇTÜĞÜ ŞEY BAŞKA"

| Nerede | Gösterilen | Gerçekte ölçülen |
|---|---|---|
| GUI grafik | "Gain" | yalnız eğri (DPI düzeltmesi yok) |
| Fare testi | "Gain" | eğri × **görünmeyen** DPI çarpanı |
| Grafik (raw 1:1) | eğri | oyun tam 1.0 görür |
| Gecikme göstergesi | ivme | `modify()` hariç %83.5 |
| SIMD parity | SIMD = skaler | envanter **sayımı** |
| `test_lat_stats` | percentile | percentile **çağrılmıyor** |

## ⭐ SONUÇ (bu turun asıl teslimi düzeltmeler değil)

**Bu proje "ölçtüğünü sandığı şeyi ölçmüyor."** Altı bağımsız lane, altı
ayrı yerde, aynı sınıfı buldu. README'nin "Still not verified" bölümü
bu turla ** kapanmadı** — üstüne yeni maddeler eklenmeli.

⚠️ Bu düzeltme turunda yapılmıyor (denetim turu). Ama
**"kurulabilir" diye ilan etmek için önce bu sınıf kapatılmalı.**
