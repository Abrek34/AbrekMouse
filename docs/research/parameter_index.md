# RawAccel Config Parameter Index

> Aj 0 (P103, R47) · DOC-ONLY · kod değişikliği yok.
> Kaynaklar: `include/rawaccel-base.hpp`, `include/config.hpp`, `src/config.cpp`,
> `cli/main.cpp` (set-param eşlemesi), `config/default.json`, `gui/*`.

## Özet

| Metrik | Değer |
|--------|-------|
| Toplam parametre | **43** |
| `set-param` ile doğrudan set edilebilir | **35** |
| Diğer CLI komutuyla set edilebilir | **2** (`active_profile` → `set`, `name` → `create/rename/duplicate/import`) |
| `set-param` üzerinden dolaylı | **1** (`whole` → `distance_mode`) |
| Otomatik yönetilen (kullanıcı set etmez) | **1** (`version`) |
| **CLI'den HİÇ erişilemeyen (GAP)** | **4** (`use_raw_input`, `disable`, `lut_data`, `lut_length`) |

**Yönetici turu için gerçek boşluklar (set-param'tan erişilemeyen):**
- `use_raw_input` — CLI'de hiçbir anahtar yok (JSON elle düzenleme / config-push yoluyla ayarlanır). Daemon hattında ayrıca **tüketilmiyor** (dormant). **R48 kararı:** saklı bayrak olarak kalır (kaldırılmaz); ham 1:1 hattı `raw_passthrough`'tur (P101), bayrak R48'den itibaren CLI durum çıktısında görünür (P120/A5-09).
- `disable` (`profiles[].disable`) — CLI'de hiçbir anahtar yok. Daemon hattında **tüketilmiyor** (dormant). **R48 kararı:** saklı bayrak olarak kalır; yalnız saklanır/görünür (P120/A5-09), işlevsel etkisi yok.
- `lut_data` / `lut_length` — lookup tablosu yalnızca GUI LUT editöründe (`gui/graph.inl` `lut_set_points`) veya JSON'da; CLI'de lookup verisi set etme komutu yok.
- `whole` — doğrudan anahtar yok; yalnızca `distance_mode separate|max|lp|euclidean` üzerinden dolaylı (ayrı parametre olarak kabul edilmeli).
- `version` — şema sürümü, kaydederken otomatik yazılır; kullanıcı parametresi değildir (GAP sayılmaz).

## Sözleşme / gösterim

- **JSON key**: `src/config.cpp` serileştirme/parse anahtarı (nlohmann::json).
- **CLI key**: `rawaccel-cli set-param <profil> <key> <value>` anahtarı (`cmd_set_param()`; domain bloğu, ikiz set, tek set).
  Çoğu set-param anahtarı X ve Y eksenine **birlikte** yazar (ikiz set).
- **Domain/range**: `sanitize_*()` (`src/config.cpp`) sonrası garanti edilen aralık; NaN/Inf önce
  güvenli varsayılana dönüştürülür.
- **Varsayılan**: `rawaccel-base.hpp` alan ilk değeri (default.json ile aynı aile).
- **Etkilediği modlar**: hangi `accel_*.hpp` algoritması o alanı gerçekten **okur** (stored-only
  alanlar ayrıca işaretli).

---

## 1. Uygulama düzeyi (`app_config`)

| # | Parametre | JSON key | CLI key | Domain | Varsayılan | Birim | Modlar |
|---|-----------|----------|---------|--------|-----------|-------|--------|
| 1 | **active_profile** | `active_profile` | `set <profil>` (set-param DEĞİL) | string; `profiles[]` içinde var olmalı | `"default"` | — | tümü |
| 2 | **use_raw_input** | `use_raw_input` | **YOK (GAP)** | bool | `true` | — | tümü |
| 3 | **version** | `version` | **oto (set edilmez)** | string şema sürümü | `"0.5.0"` | — | tümü (migration) |

1. Hangi kaydın/kayda ait yapılandırmanın etkin olduğunu seçer; `rawaccel-cli set <ad>` ile değiştirilir (set-param değil). `cmd_set()`.
2. Ham giriş bayrağı — varsayılan `true`; daemon sıcak yolu bu alanı okumuyor (dormant; yalnızca JSON'da taşınıyor). **R48:** saklı bayrak olarak kalır; durum çıktısında görünür (P120/A5-09) — gerçek ham hat `raw_passthrough` (P101).
3. Config şema sürümü; `save_config` her yazımda günceller, `migrate_config` migration kararlarını sürüme göre verir (P43-BF1). Elle değiştirilmez.

## 2. Cihaz profili düzeyi (`device_profile`, `profiles[]`)

| # | Parametre | JSON key | CLI key | Domain | Varsayılan | Birim | Modlar |
|---|-----------|----------|---------|--------|-----------|-------|--------|
| 4 | **name** | `profiles[].name` (+ iç ikiz `profile.name`) | `create` / `rename` / `duplicate` / `import` (set-param DEĞİL) | string 1–256 | `"default"` | — | tümü |
| 5 | **device_id** | `profiles[].device_id` | `device_id` | string; boş = tüm fareler; `usb:VVVV:PPPP:serial` / by-id yolu / eventN | `""` | — | tümü |
| 6 | **dpi** | `profiles[].dpi` | `dpi` | 1–32000 | `800` | DPI | tümü |
| 7 | **polling_rate** | `profiles[].polling_rate` | `polling_rate` | 125–8000 | `1000` | Hz | tümü |
| 8 | **disable** | `profiles[].disable` | **YOK (GAP)** | bool | `false` | — | tümü (dormant) |

4. Profil adı; daemon/CLI komutları hedefi bu ada göre bulur (ilk eşleşme). Dış (`device_profile.name`) ve iç (`profile.name`) ikizi birlikte yazılır.
5. Profil-fare eşlemesi; boş `""` tüm eşleşmemiş farelere uygulanır. CLI'de dize olarak verilir (numerik ayrıştırma kapalı; atama `cmd_set_param()` `device_id` dalında).
6. Sensör çözünürlüğü; `NORMALIZED_DPI=1000` normalizasyonu için `dpi_factor` hesabında kullanılır (`daemon.cpp` `apply_profile`).
7. Raporlama hızı; zaman bütçesi / alt-saniye zamanlama için saklanır.
8. Kayıt-dışı bayrak; serileşir ama daemon sıcak yolu okumuyor (dormant — `device_config::disable`). **R48:** saklı bayrak olarak kalır; durum çıktısında görünür (P120/A5-09), işlevsel etkisi yok.

## 3. Hızlandırıcı düzeyi (`accel_args`, `profile.accel_{x,y}.*`)

| # | Parametre | JSON key | CLI key | Domain (sanitize) | Varsayılan | Birim | Modlar |
|---|-----------|----------|---------|-------------------|-----------|-------|--------|
| 9 | **mode** | `mode` | `mode` | `classic|power|natural|jump|synchronous|lookup|noaccel` (CLI alias: `off`/`none` → noaccel) | `noaccel` | — | seçici |
| 10 | **gain** | `gain` | `gain` | bool | `true` | — | classic, power, natural, jump, synchronous, lookup |
| 11 | **acceleration** | `acceleration` | `acceleration` | reel (NaN→0; negatif serbest, classic tam-sayılı üsle decel) | `0.005` | boyutsuz (ips-normalize) | classic |
| 12 | **exponent_classic** | `exponent_classic` | `exponent_classic` | 1–10 | `2` | boyutsuz | classic |
| 13 | **exponent_power** | `exponent_power` | `exponent_power` | 1e-4–5 (üst = `EXP_POWER_MAX`, GUI gauge üstü) | `0.05` | boyutsuz | power |
| 14 | **limit** | `limit` | `limit` | ≥ 0 | `1.5` | boyutsuz (gain asimptotu) | natural (stored-only diğerleri) |
| 15 | **decay_rate** | `decay_rate` | `decay_rate` | ≥ 0 | `0.1` | 1/ips (e-yarılanma) | natural |
| 16 | **motivity** | `motivity` | `motivity` | ≥ 0 | `1.5` | boyutsuz (duyarlılık aralığı) | synchronous (stored-only diğerleri) |
| 17 | **gamma** | `gamma` | `gamma` | ≥ 0 | `1` | boyutsuz | synchronous (stored-only diğerleri) |
| 18 | **input_offset** | `input_offset` | `input_offset` | ≥ 0 | `0` | ips | classic, natural |
| 19 | **output_offset** | `output_offset` | `output_offset` | 0–100 (üst = `OUTPUT_OFFSET_MAX`) | `0` | ips (çıkış uzayı) | power (stored-only diğerleri) |
| 20 | **scale** | `scale` | `scale` | 0–100 (üst = `SCALE_MAX`; negatif→0) | `1` | boyutsuz | power (stored-only diğerleri) |
| 21 | **sync_speed** | `sync_speed` | `sync_speed` | ≥ 1e-4 | `5` | ips | synchronous |
| 22 | **smooth** | `smooth` | `smooth` | ≥ 0 | `0.5` | boyutsuz (0=sert) | jump, synchronous |
| 23 | **cap_x** | `cap[0]` | `cap_x` | 0–500 (üst = `CAP_X_MAX`) | `15` | ips (giriş cap'ı / jump adım konumu) | classic, power, jump |
| 24 | **cap_y** | `cap[1]` | `cap_y` | 0–100 (üst = `CAP_Y_MAX`) | `1.5` | boyutsuz (gain/çıkış cap'ı / jump adımı) | classic, power, jump |
| 25 | **cap_mode** | `cap_mode` | `cap_mode` | `in|out|io` (CLI ayrıca `both`/`in_out`) | `out` | — | classic, power |
| 26 | **lut_data** | `lut_data` | **YOK (GAP — GUI LUT editörü)** | 0–514 float; (hız, çıktı) çiftleri; hız artan sıralı | `[]` (boş → noaccel) | ips (x), ips (y; gain modu: çıktı hızı) | lookup |
| 27 | **lut_length** | `lut_length` | **YOK (GAP — lut_data ile otomatik)** | 0–514, çift | `0` | adet (float eleman) | lookup |

9. Eksen hızlandırma algoritmasını seçer; `noaccel` = düz 1.0 (identity). set-param X+Y ikizine birlikte yazar.
10. GAIN (çıkış-hızı integral formu) vs LEGACY (doğrudan çarpan) varyantı; lookup'ta `velocity` (y=çıktı hızı, etkin gain y/x).
11. Classic eğrinin ivme sabiti: GAIN `power·(acc·(x−off))^(exp−1)`, LEGACY `acc^(exp−1)·(x−off)^exp/x`. Negatif değer tamsayı üsle legal deceleration.
12. Classic üs; `≤1` "linear path" sabit-gain (port kuralı), sanitize alt sınırı 1.0 GUI ile aynı (`gui/ui_builder.inl` max).
13. Power üssü; `scale·x)^n + C/x`. Sanitize tabanı 1e-4.
14. Natural asimptot `limit−1` (gain→limit); classic/power/jump algoritmaları **okumaz** — yalnızca depolanır (GUI natural için gösterir).
15. Natural çürüme: iç katsayı `decay_rate / |limit−1|`; gain eğrisinin asimptota eğimi.
16. Synchronous sigmoid duyarlılık aralığı `1/m → m`; log-motivity: `log(motivity)`. Başka modlar okumaz.
17. Synchronous log-uzayı eğimi: `gamma / log(motivity)` (linear clamp dalı). Başka modlar okumaz.
18. Hız eşiği; altında `operator() → 1.0` (ivme başlamaz). Classic + natural offset bandı. **Power'da INERT (P110/P118): `accel-power.hpp` bu alanı hiç okumaz** — power'ın offset kolu `output_offset`'tur; `input_offset=20` ile korunan yavaş 1:1 bant power'a geçince sessizce kaybolur (bkz. precision.md §8.2).
19. Power çıkış eğrisinin offset'i; `gain_inverse` ile `offset.x` çözülür; cap_mode=io+legacy'de yok sayılır. P120 üst sınırı `OUTPUT_OFFSET_MAX=100` (config.h'te GUI gauge üstü).
20. Power dikey/hız ölçeği; `io` cap modunda `scale_from_gain_point`/`scale_from_output_point` ile türetilir (legacy+io). P120 üst sınırı `SCALE_MAX=100`.
21. Synchronous sigmoid orta noktası (gain 1.0 kavşak hızı).
22. Jump: adım genişliği sinyali (`2π/(smooth·cap.x)`); synchronous: `sharpness = 0.5/smooth`.
23. `cap[0]` — in/io modlarında giriş hızı limiti; jump'ta adımın olduğu hız. P120 üst sınırı `CAP_X_MAX=500`.
24. `cap[1]` — out/io modlarında gain/çıktı limiti; jump'ta adım büyüklüğü (`cap.y−1` kazanç adımı). P120 üst sınırı `CAP_Y_MAX=100`.
25. Cap uzayı: `in`=giriş hızında, `out`=çıkış gain'inde, `io`=ikisi birden (classic/power yorumu moda göre değişir).
26. Lookup eğri noktaları; `lookup::operator()` ikiye böler; yalnız `mode=lookup` iken serileşir (synchronous GAIN LUT'u karışmasın diye kapı var). Sanitize: float tarama + finite clamp (P99).
27. `data[]` içindeki float eleman sayısı (2×nokta); `lut_data` ile birlikte yazılır; int overflow'a karşı double-guard (BUG-5).

## 4. Profil düzeyi — modülatör / DPI (`profile.*`)

| # | Parametre | JSON key | CLI key | Domain (sanitize) | Varsayılan | Birim | Modlar |
|---|-----------|----------|---------|-------------------|-----------|-------|--------|
| 28 | **raw_passthrough** | `raw_passthrough` | `raw` | bool | `false` | — | tümü (baypas) |
| 29 | **output_dpi** | `output_dpi` | `output_dpi` | 1–32000 | `1000` | DPI | tümü |
| 30 | **yx_output_dpi_ratio** | `yx_output_dpi_ratio` | `yx_ratio` | 0.01–100 | `1` | boyutsuz | tümü |
| 31 | **lr_output_dpi_ratio** | `lr_output_dpi_ratio` | `lr_ratio` | 0.01–100 | `1` | boyutsuz | tümü |
| 32 | **ud_output_dpi_ratio** | `ud_output_dpi_ratio` | `ud_ratio` | 0.01–100 | `1` | boyutsuz | tümü |
| 33 | **degrees_rotation** | `degrees_rotation` | `rotation` | 0–360 (fmod normalize; negatif → +360) | `0` | derece | tümü |
| 34 | **degrees_snap** | `degrees_snap` | `snap` | 0–45 | `0` | derece | tümü |
| 35 | **speed_min** | `speed_min` | `speed_min` | ≥ 0 | `0` | ips | tümü (clamp) |
| 36 | **speed_max** | `speed_max` | `speed_max` | ≥ min | `0` | ips | tümü (clamp) |
| 37 | **domain_weights** | `domain_weights` `[x,y]` | `domain_weights` (ikisi), `domain_weight_x`, `domain_weight_y` | 0–1e6 | `[1,1]` | boyutsuz | tümü |
| 38 | **range_weights** | `range_weights` `[x,y]` | `range_weights` (ikisi), `range_weight_x`, `range_weight_y` | 0–1e6 | `[1,1]` | boyutsuz | tümü |

28. True ise tüm hattı baypas (1:1 ham); GUI raw_check + 18 widget sensibilite (`update_raw_sensitivity`).
29. Çıkış DPI normalizasyonu: `dpi_adjustment = (output_dpi/1000)·dpi_factor`; X'e, Y'ye ayraç `yx` ile.
30. Y ekseninin X'e göre DPI oranı (dikey hız farkı); Y her zaman `dpi_adjustment·yx_ratio` ile çarpılır.
31. Sol yön DPI çarpanı; yalnızca `in.x < 0` iken (RawAccel negatif yön kuralı).
32. Aşağı yön DPI çarpanı; yalnızca `in.y < 0` iken.
33. Giriş vektörünü döndürür (`direction()`); eğriyi yön eksenli uygular. `-45° → 315°` korunur (fmod).
34. Açı yakalama: `reference_angle` bu açı içindeyse hareketi tam eksene hizalar (0°/90°).
35. `clamp_speed` alt sınırı (`speed_max > 0 && min ≤ max` iken aktif).
36. `clamp_speed` üst sınırı; 0 = clamp kapalı; sanitize `max < min` ise max'ı min'e eşitler.
37. Hız-uzayı (domain) eksen ağırlığı; `abs_vel` bileşenlerini ölçekler (whole ağırlıklandırılmış hız + separate per-eksen).
38. Gain-uzayı ağırlığı; whole: yönsel interpolasyon `rwx + (rwy−rwx)·(2/π)·angle`; separate: `scale = 1+(accel−1)·weight`. 0 = ivme yok (fare değil).

## 5. Hız işlemcisi (`profile.speed_processor.*`)

| # | Parametre | JSON key | CLI key | Domain (sanitize) | Varsayılan | Birim | Modlar |
|---|-----------|----------|---------|-------------------|-----------|-------|--------|
| 39 | **whole** | `whole` | `distance_mode` (dolaylı; doğrudan anahtar YOK) | bool; CLI `distance_mode` alias: `separate`↔`manhattan`, `max`↔`chebyshev` | `true` | — | tümü |
| 40 | **lp_norm** | `lp_norm` | `lp_norm` (+ `distance_mode`) | ≥ 0; ≥16 veya ≤0 → max; 2 → euclidean | `2` | boyutsuz | tümü (whole) |
| 41 | **input_speed_smooth_halflife** | `input_speed_smooth_halflife` | `input_smooth_halflife` | ≥ 0 | `0` | ms | tümü |
| 42 | **scale_smooth_halflife** | `scale_smooth_halflife` | `scale_smooth_halflife` | ≥ 0 | `0` | ms | tümü |
| 43 | **output_speed_smooth_halflife** | `output_speed_smooth_halflife` | `output_smooth_halflife` | ≥ 0 | `0` | ms | tümü |

39. `true` → iki eksen ortak hızla (whole, kayan çerçeve); `false` → per-eksen ayrık (separate). CLI'de yalnız `distance_mode separate` (false) / `max|lp|euclidean` (true) ile; doğrudan `whole` anahtarı yok.
40. Lp-norm derecesi; `lp_distance` p-norm; `MAX_NORM=16` üstü max/chebyshev davranışı, tam 2 → euclidean (epsilon 1e-9).
41. `input_speed_smoother` (linear EMA, trend 1.25 halflife); >0 iken smoothing aktif; `init()` katsayı hesabı.
42. `scale_smoother` (simple EMA) — gain(scale) çıktısını yumuşatır.
43. `output_speed_smoother` (linear EMA, trend 0.70 halflife) — magnitude'u yumuşatıp yönü oranla korur (whole) / copysign (separate).

---

## Çapraz-doğrulama: `config/default.json` (temsilci settings.json)

Temsilci dosyadaki **her** anahtarın indeksteki karşılığı (sol = JSON anahtarı, sağ = tablo #):

```
active_profile                       → 1
use_raw_input                        → 2
profiles[].name                      → 4
profiles[].device_id                 → 5
profiles[].dpi                       → 6
profiles[].polling_rate              → 7
profiles[].disable                   → 8
profiles[].profile.name              → 4 (iç ikiz)
profiles[].profile.raw_passthrough   → 28
profiles[].profile.domain_weights    → 37
profiles[].profile.range_weights     → 38
profiles[].profile.output_dpi        → 29
profiles[].profile.yx_output_dpi_ratio → 30
profiles[].profile.degrees_rotation  → 33
profiles[].profile.degrees_snap      → 34
profiles[].profile.speed_min         → 35
profiles[].profile.speed_max         → 36
profiles[].profile.lr_output_dpi_ratio → 31
profiles[].profile.ud_output_dpi_ratio → 32
accel_x/y.mode / gain / acceleration / exponent_classic / exponent_power / limit
  / decay_rate / input_offset / output_offset / scale / cap / cap_mode / smooth
  / sync_speed / motivity / gamma                                    → 9–25
speed_processor.whole / lp_norm / input_speed_smooth_halflife
  / scale_smooth_halflife / output_speed_smooth_halflife             → 39–43
```

Varsayılan dosyada görünmeyip serileştirmede üretilen ek anahtarlar: `version` (→3, oto),
`lut_data`/`lut_length` (→26/27, yalnız lookup modunda yazılır, `config.cpp:84-90`).

## CLI erişilebilirlik denetimi (set-param)

Her set-param anahtarının hedeflediği parametre (#):

| CLI anahtarı | Parametre # | CLI anahtarı | Parametre # |
|--------------|-------------|--------------|-------------|
| `mode` | 9 | `rotation` | 33 |
| `gain` | 10 | `snap` | 34 |
| `acceleration` | 11 | `dpi` | 6 |
| `exponent_classic` | 12 | `polling_rate` | 7 |
| `exponent_power` | 13 | `speed_min` | 35 |
| `limit` | 14 | `speed_max` | 36 |
| `decay_rate` | 15 | `output_dpi` | 29 |
| `motivity` | 16 | `lr_ratio` | 31 |
| `gamma` | 17 | `ud_ratio` | 32 |
| `input_offset` | 18 | `yx_ratio` | 30 |
| `output_offset` | 19 | `distance_mode` | 39 (dolaylı) |
| `scale` | 20 | `lp_norm` | 40 |
| `sync_speed` | 21 | `input_smooth_halflife` | 41 |
| `smooth` | 22 | `scale_smooth_halflife` | 42 |
| `cap_x` | 23 | `output_smooth_halflife` | 43 |
| `cap_y` | 24 | `domain_weights` / `_x` / `_y` | 37 |
| `cap_mode` | 25 | `range_weights` / `_x` / `_y` | 38 |
| `raw` | 28 | `device_id` | 5 |

set-param **dışı** CLI yolları: `set <profil>` → 1 (active_profile); `create`/`rename`/
`duplicate`/`import` → 4 (name).

## GAP listesi (set-param'tan erişilemeyen — yönetici turu için)

| # | Parametre | Durum | Etki |
|---|-----------|-------|------|
| 2 | `use_raw_input` | JSON elle düzenleme; set-param anahtarı YOK; **durum çıktısında GÖRÜNÜR** (P120/A5-09) | Dormant (daemon okumuyor; ham 1:1 `raw_passthrough`'tan). **R48 kararı:** saklı bayrak olarak kalır — bağlanmaz/kaldırılmaz; gerçek ham hat `raw_passthrough=true` |
| 8 | `disable` | JSON elle düzenleme; set-param anahtarı YOK; **durum çıktısında GÖRÜNÜR** (P120/A5-09) | Dormant (hiçbir kod yolu okumuyor). **R48 kararı:** saklı bayrak olarak kalır; etkisi yoktur, yalnız saklanır/görünür |
| 26 | `lut_data` | GUI LUT editörü + JSON; CLI yok | Lookup profili CLI'den kurulsuz — `import` yolu geçici çözüm |
| 27 | `lut_length` | lut_data ile otomatik; CLI anahtarı yok | Ayrı anahtar gerekmez, ancak lut_data set etme yeteneğiyle birlikte değerlendirilmeli |
| 39 | `whole` | Dolaylı: yalnız `distance_mode`; doğrudan anahtar yok | Küçük tutarsızlık: `stored_value_str()` da `whole` anahtarını bilmiyor (yalnız `distance_mode` dalı) |
| 3 | `version` | Oto-yönetilen; kullanıcı parametresi değil | GAP sayılmaz (schema migration aracı) |
| 1 | `active_profile` | CLI `set <profil>` var; set-param değil | GAP sayılmaz (ayrı komut mevcut) |
| 4 | `name` | CLI `create/rename/duplicate/import` var; set-param değil | GAP sayılmaz |

**Gerçek GAP sayısı: 4** (use_raw_input, disable, lut_data, lut_length) + 1 küçük (whole anahtar yok).

## Notlar

- **Varsayılan config ≠ raw passthrough:** taze kurulum default'u `noaccel` + `output_dpi` normalizasyonudur
  (birebir 1:1 geçiş DEĞİL — P14/P101 bulgusu). Kesin 1:1 için `disable` preset (`raw_passthrough=true`).
- **/etc ↔ ~/.config ayrışması:** daemon `/etc/rawaccel/settings.json`'u uygular; GUI/CLI
  `~/.config/rawaccel/settings.json`'u okur/yazar ve kaydederken daemon'a IPC `set_config` push eder
  (P100 raporu). Parametre JSON şeması iki yerde de aynıdır; değerler ayrışabilir.
- `limit`, `motivity`, `gamma`, `output_offset`, `input_offset`(power), `scale`(classic vb.)
  bazı modlarda **stored-only**: set edilir, JSON'a yazılır, o modun algoritması okumaz
  (geleceğe dönük / GUI paritesi). Algılama: hangi `accel_*.hpp` o alanı referanslıyor.
- `output_offset` power dışında; `input_offset` classic+natural dışında hiçbir algoritmada
  okunmaz; `cap` classic/power/jump okur; `smooth` jump+synchronous okur.
- GUI spin aralıkları ile sanitize **R48'den beri eşleşiyor** (P120/BUG-3): power exp `0.01–5`,
  scale `0.01–100`, cap_x `0–500`, cap_y `0–100`, output_offset `0–100` — `sanitize_accel_args`
  bu beş alanı GUI gauge maksimumlarıyla sınırlar (`config.hpp:18-22`). Diğer alanlarda
  GUI aralığı için `gui/ui_builder.inl` `make_spin` çağrıları.
- `sanitize_accel_args` `cap.x/cap.y` üst sınırı (P120): `CAP_X_MAX=500`, `CAP_Y_MAX=100`;
  bu cap'ler GUI gauge üstü = R15 boundary round-trip değerleri. classic GAIN yine de
  `cap_x/cap_y → DBL_MAX` guard'ı ile bozuk/NaN cap'leri yutar (`accel-classic.hpp`).