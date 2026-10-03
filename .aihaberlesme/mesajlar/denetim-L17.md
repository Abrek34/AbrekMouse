### L17 | alt-ajan (subagent) | 2026-10-01

KAPSAM   : gui/graph.inl (602), gui/mouse_test.inl (581), gui/hidpp_panel.inl (852),
           gui/profile_mgr.inl (646), gui/devices.inl (311) — **salt okuma, hiçbir
           dosya değiştirilmedi.** Doğrulanan satır sayıları:
           `wc -l gui/*.inl` → 852/646/311/602/581 = 2992 (birebir brifing ile aynı).
           Çalışma ağacı temiz: `git status --short | grep -v aihaberlesme` → **0 satır**.

═══════════════════════════════════════════════════════════════════════════
## ÖZET — 3 bulgu "ölçülmüş sayı yanlış" sınıfında, 3'ü sessiz yeşil
═══════════════════════════════════════════════════════════════════════════

**⭐ ANA BULGU (CRIT):** Kullanıcı "Gain" kelimesini **iki yerde** görüyor ve
ikisi **farklı nicelikler**:

| Ekran | Değişken | Kaynak | Tanım |
|---|---|---|---|
| Grafik Y ekseni | `graph_Y` | `accel_union::apply(speed)` | **sadece eğri** çarpanı |
| Fare testi "Gain (×)" | `telem_gain` | `out_ips/in_ips` | eğri **× output-DPI normalizasyonu** |

`graph.inl` içinde `output_dpi` / `dpi_factor` / `NORMALIZED_DPI` **hiç geçmiyor**
(0 eşleşme). Oyun gördüğü `out/in` = `graph_Y × dpi_adjustment`, ölçüm 5/5 tutuyor.

---

BULGULAR :

### L17-1 | CRIT | gui/graph.inl:88-89,165 + gui/mouse_test.inl:293 | grafik "Gain" ≠ fare testi "Gain"

**Kanıt 1 — ayrı kopya YOK, ama farklı nicelikler.**
Grafik gerçekten uygulanan algoritmayı çağırıyor (ayrı kopya bulunamadı, bu iyi haber):
```
gui/graph.inl:53-67   compute_curve():  accel_union au; au.init(args); au.apply(s,args)
include/accel-union.hpp:52   double apply(double speed,const accel_args&)   <-- AYNI fonksiyon
include/rawaccel.hpp:568     double scale = 1.0 + (data.accel_x.apply(speed,args.accel_x)-1.0)*range_weight;
```
`grep -rn "accel_union\|au.apply" gui/graph.inl` → `compute_curve` **gerçek** `accel_union`'ı
kullanıyor; elle yazılmış eğri formülü **yok**. ⛔ Yani "hızlandırma eğrisi ayrı
kopyadan" bulgusu **DOĞRU DEĞİL** — bu lane'in varsayımı yanlış çıktı.

Ama sapma **başka bir yerde**: `telem_gain` (`daemon/daemon.cpp:2527,2532`)
`out_x`/`out_y`'yi kullanır, bunlar `modifier::modify()` adım-5'ten
(`rawaccel.hpp:595-599`) **geçmiş** çıktıdır:
```cpp
// rawaccel.hpp:596
double dpi_adjustment = (args.output_dpi / NORMALIZED_DPI) * dpi_factor;
in.x *= dpi_adjustment;
```
Grafikte bu faktörün **hiçbir izi yok**:
```
$ grep -n "output_dpi\|dpi_factor\|NORMALIZED_DPI" gui/graph.inl ; echo RC=$?
RC=1                      <-- 0 eşleşme
```

**Kanıt 2 — sayısal ayrışma (gaming preset'i, output_dpi=1000):**
`/tmp/opencode/l17/probe6` (kurulum/CLI'nin `make_preset` ve `modifier` başlıklarını
gerçek koddan bağlar):
```
dev_dpi  in_ips       graph_Y        true_out/in      delta_vs_graph   in->out counts
400      25.0000      1.125000       2.800000         1.675000         10->25
800      25.0000      1.125000       1.400000         0.275000         20->25
1000     25.0000      1.125000       1.120000         -0.005000        25->25
1600     25.0000      1.125000       0.700000         -0.425000        40->25
3200     25.0000      1.125000       0.350000         -0.775000        80->25
```
**800 DPI'de grafik 1.125 der, oyun 1.400 görür (+24%).** `dpi=3200`'de grafik
1.125, oyun 0.350 (**−69%**). Kullanıcı iki ekrana bakıp **iki farklı cihaz**
kullandığını sanır.

**Kanıt 3 — en kötü durum: HİÇ ivme yokken "Gain" 1.0 değil.**
`/tmp/opencode/l17/probe4` — `accel_mode::noaccel` (tanım gereği gain ≡ 1):
```
dpi      out_dpi    counts   ips          graph_Y      mouse_gain       note
400      1000       100      250.0000     1.000000     2.500000         <-- MISMATCH, no accel configured
800      1000       100      125.0000     1.000000     1.250000         <-- MISMATCH, no accel configured
1600     1000       100      62.5000      1.000000     0.630000         <-- MISMATCH, no accel configured
3200     1000       100      31.2500      1.000000     0.320000         <-- MISMATCH, no accel configured
```
Sıfır ivme ayarlıyken fare testi **2.5 / 1.25 / 0.63 / 0.32** gösteriyor.
Kullanıcı "ivmem açık ama hızlandırma 2.5× uygulanıyor" diye **yanlış teşhis**
koyar. `output_dpi` varyasyonu:
```
out_dpi=0      telem_gain=1.000000   (0 = "no normalization" sentineli — doğru davranış)
out_dpi=400    telem_gain=0.500000
out_dpi=1000   telem_gain=1.250000
out_dpi=3200   telem_gain=4.000000
```

**Kanıt 4 — açıklama MUTASYONLA doğrulandı (5/5).**
`/tmp/opencode/l17/probe17` — model: `telem_gain == curve_gain × dpi_adjustment`.
Küçük sayaçlarda tam sayı kesme (`motion_math.hpp:50-54`) baskın olduğu için
sayım 100× büyütüldü:
```
dpi     out_dpi  graph_Y        curve*dpi_adj  telem_gain     model holds?
400     1000     1.787200       4.468000       4.468000       YES
800     1000     1.787200       2.234000       2.234000       YES
1000    1000     1.787200       1.787200       1.787200       YES
1600    1000     1.787200       1.117000       1.117000       YES
3200    1000     1.787200       0.558500       0.558500       YES

  model holds 5/5
```
Model yanlış olsaydı 5/5 "no" çıkardı. Ayrışmanın tamamı bu tek çarpan.

**Hata sınıfı: CRIT** — kullanıcının karar verdiği sayı, oyunun gördüğünün
ötesinde başka bir niceliği gösteriyor ve **kullanıcı bunu fark edemez.**

---

### L17-2 | CRIT | gui/graph.inl:165 (raw_passthrough kontrolü YOK) | ham 1:1 modda bile eğri çiziliyor

**Kanıt:**
```
$ grep -n "raw_passthrough" gui/graph.inl ; echo RC=$?
RC=1                      <-- 0 eşleşme
$ grep -n "raw_passthrough" gui/mouse_test.inl
276:    if (cur_prof(S).prof.raw_passthrough) {    <-- fare testi kontrol EDİYOR
```
`graph.inl:165` `draw_curve(dp.prof.accel_x, C_CURVE)`'ı **koşulsuz** çağırır.

**Ölçüm** — `/tmp/opencode/l17/probe15` (gaming preset'i + `raw_passthrough=true`):
```
s(ips)     graph_Y        true out/in (game)
5.00       1.025000       1.000000
12.50      1.062500       1.000000
25.00      1.125000       1.000000
50.00      1.250000       1.000000
```
Ham modda daemon (`daemon.cpp:2352-2364`) `dx,dy`'i **olduğu gibi** yazar → oyun
tam 1.0 görür, grafik 1.0→1.25 arası bir eğri çizer. `PAS-1` master switch'i
(`widgets_sync.inl:614`) veya `disable` preset'i ile bu duruma **kolayca** girilir.

**Hata sınıfı: CRIT** — "1:1 ham moddayım, grafik neden ivme gösteriyor?"
Kullanıcı hızlandırmayı açtığını sanır. `mouse_test.inl:176` bunu doğru söylüyor
(durum 4: *"Raw 1:1 passthrough — no telemetry is produced"*), **grafik söylemiyor.**

---

### L17-3 | HIGH | gui/daemon_comm.inl:479-504 + gui/mouse_test.inl:293 | "tüm cihazlar" modunda FARKLI fare ölçülüyor

`daemon_device_slice` fallback zinciri (`daemon_comm.inl:503`):
```cpp
return !match.empty() ? match : !live.empty() ? live : first;
```
`want` (aktif profilin `device_id`) **boşsa** ("Tüm cihazlar" — yeni profilin
**varsayılan** durumu) `match` asla dolmaz ve GUI `live`'ı seçer = **dizideki
`telem_in_ips` taşıyan İLK cihaz.**

**Kanıt** — `/tmp/opencode/l17/probe8` (iki fare, ikisi de taze telemetri;
kullanıcı yalnızca ikisini hareket ettiriyor):
```
profile device_id=(All devices) -> GUI reads 'Mouse A (idle)' (id=usb:aaaa) gain=1
profile device_id=usb:aaaa       -> GUI reads 'Mouse A (idle)' (id=usb:aaaa) gain=1
profile device_id=usb:bbbb      -> GUI reads 'Mouse B (moving)' (id=usb:bbbb) gain=0.25
```
Mouse B hareket ediyor, gerçek gain'i **0.250**; GUI **Mouse A**'nın
donmuş **1.000**'ini gösteriyor.

**Neden "geçersiz" işaretlenmiyor:** `mouse_test.inl:275`'teki kapı
`fresh` **tazelik** kontrol ediyor, **kimlik** değil:
```cpp
bool fresh = wall >= 0 && (now_mono_raw_ms() - wall) < 2000.0;
```
`/tmp/opencode/l17/probe14` — 2000 ms eşiği doğru çalışıyor (ölü/ölü cihaz ~2.25 s
= 8 poll'da yakalanıyor, sağlıklı cihaz asla yanlış geçersizlenmiyor):
```
age_ms         fresh?     verdict
1999           true       shows live values
2000           false      blanked, 'Awaiting motion'
```
Ama **bir cihazın taze örneği bu kapıdan geçer** ve test edilen fareymiş gibi
gösterilir. Pencere `mouse_test.inl:516-541` **tam olarak 5 satır** kuruyor
(In/Out/Gain/Latency/Poll) — **hiçbir yerde cihaz kimliği yok.** Kullanıcı
hangi farenin ölçüldüğünü **ekrandan anlayamaz**.

**Hata sınıfı: HIGH** — geçersiz ölçüm "başarılı" gibi gösteriliyor
(brifing §3'ün en pahalı sınıfı).

---

### L17-4 | HIGH | daemon/daemon.cpp:2347→2543 (mouse_test.inl:298-304 bunu gösteriyor) | gösterilen "gecikme" ivme hesabını ÖLÇMÜYOR

`mouse_test.inl` gecikmeyi **kendi ölçmez** — `lat_p50_us`/`lat_p95_us`'u
daemon'dan okur. ⛔ İyi haber: `mouse_test.inl:240-245`'teki `now_mono_raw_ms()`
`CLOCK_MONOTONIC_RAW` kullanıyor, daemon'un `now_ns()` ile **aynı saat** —
`now_ns`/`now_us` kullanımı **doğru**.

Ama daemon'un kaydettiği pencere yanlış:
```
daemon.cpp:2556   uint64_t lat_anchor_ns = now_ns();   // process_device GİRİŞİ
daemon.cpp:2347   const uint64_t t_now = now_ns();     // flush_motion GİRİŞİ  <-- ÖLÇÜM BAŞLANGICI
daemon.cpp:2510   apply_motion_math(...)               // <-- İVMENİN KENDİSİ, ÖLÇÜM DIŞINDA
daemon.cpp:2543   double lat_us = (t_now - lat_anchor_ns)/1000.0;
```
`t_now` **girişte** okunuyor; `lat_us` hesaplanırken bu değer **tekrar
okunmuyor**. Yani `lat_us` = *önceki flush'ın girişi → bu flush'ın girişi*;
aradaki `modifier::modify()` + telemetri store'ları **dışarıda kalıyor**.

**Ölçüm** — `/tmp/opencode/l17/probe9` (20000 kare, 256 noktalık LUT):
```
  daemon.cpp:2543 records (t_now-anchor):                   0.013 us/frame
  true end-to-end (anchor->last write):                     0.077 us/frame
  modifier::modify() alone:                                 0.038 us/frame
  excluded fraction = 83.5% of end-to-end
```
Sinyal *ağırlıklı* olarak yanlış ölçülüyor: `now_ns()` iki çağrı arası (~13 ns)
ölçümün **tamamı**; `modifier::modify()`'in **tamamı** dışarıda.

Kodun **kendi yorumu** da iddiası çürütüyor:
```
daemon.cpp:2539-2542  "...to this last write). A subsequent flush ... measures
                       from this write instead (MED-4), so each flush quantifies
                       only its own work."
```
"to this last write" **yanlış** — `t_now` yazma *öncesinde* alınıyor.

**Hata sınıfı: HIGH** — fare testi "Latency p50/p95 (µs)" etiketiyle
(`mouse_test.inl:533`) kullanıcıya **hızlandırmanın işlem maliyetini**
sunuyor; ölçülen şey bunun değil. **Kapsam notu:** satırlar daemon/daemon.cpp'de
(başka lane) ama **bulgu bu lane'in ekranındadır** — L14/L20 ile paylaşılmalı.

---

### L17-5 | HIGH | gui/hidpp_panel.inl:588 + :620 | LOD yazılmıyor, "başarılı" yazıyor (SESSİZ YEŞİL)

**Kanıt:**
```cpp
// hidpp_panel.inl:585-590
if (task->supports_lod) {
    out.ok_lod = transport.set_lift_off_distance(...);
} else {
    out.ok_lod = true; // no change requested     <-- YAZMA YOK, BAŞARI DÖNDÜ
}
```
```cpp
// hidpp_panel.inl:620-622  (render)
parts += std::string(tr("LOD→")) + tr(lod_en[std::clamp(r->out.lod,1,3)])
          + (r->out.ok_lod ? "" : tr("(rejected)"));    <-- (rejected) YOK
```
LOD desteği olmayan bir Logitech farede her "Apply" şunu yazar:
**`DPI→1600 · Rate→1000 Hz · LOD→Low`** — ama `set_lift_off_distance()`
**hiç çağrılmamıştır.** Combo `hw_update_ui_state:757`'de zaten disabled;
kullanıcı değiştiremez, ama durum satırı her seferinde **uygulandı** diyor.

**Karşılaştırma — DPI ve rate DÜRÜST:**
```
hidpp_panel.inl:534   out.ok_dpi  = transport.set_dpi(...)
logitech_hidpp.cpp:1915  return send_feature_request(...).has_value();  <-- ack ZORUNLU
logitech_hidpp.cpp:1876-1879  dpi_levels'a uymayan değer -> return false
```
DPI/rate `(rejected)` gösteriyor. **Sadece LOD** sessiz.

`/tmp/opencode/l17/probe13` üç dalı da izledi; sonuç:
```
### => DPI and Rate are honest; LOD is the silent green.
```

**Hata sınıfı: HIGH** — yazılmayan bir işlem "yazıldı" diye raporlanıyor.

---

### L17-6 | MED | gui/graph.inl:53-67,88-89 | grafik daemon'ın range/domain ağırlıklarını ve sınırlayıcılarını hesaba katmıyor

Grafik ham `au.apply(s)` çiziyor; daemon ise (`rawaccel.hpp:557-568`)
```cpp
double speed = sp.calc_speed_whole(abs_vel, time);        // domain_weights + EMA
double range_weight = args.range_weights.x;
double scale = 1.0 + (au.apply(speed,...) - 1.0) * range_weight;
```
uyguluyor. `graph.inl` içinde **hiçbiri yok**:
```
$ grep -n "range_weights\|domain_weights\|speed_min\|speed_max\|smooth" gui/graph.inl ; echo RC=$?
RC=1        <-- 0 eşleşme
```

**Ölçüm** — `/tmp/opencode/l17/probe` (grafik `compute_curve` birebir kopyalandı):
```
=== B: classic, range_weights.x=0.5 (max_speed=50.0 ips) ===
  s(ips)     graph_y(gain)  daemon_scale   out/in(graph)  delta
  1.0        2.000000       1.500000       2.000000       -25.0000%
  50.0       2.000000       1.500000       2.000000       -25.0000%
```
**Grafik %25 daha yüksek gösteriyor.** `range_weights`/`domain_weights`
GUI'de **widget'ı yok** (`grep -rn range_weights gui/` → 0), yalnız JSON/CLI
`set-param` ile — ama `sanitize` bunları 0..1e6 kabul ediyor (`config.cpp:603-608`),
yani **JSON ile gelen geçerli bir config** bu sapmayı üretir.

**Düşük risk tarafı (ölçüldü, sorun DEĞİL):** 8 hazır preset'in tamamı
`range_weights=1, domain_weights=1` ile **birebir eşleşiyor**
(`/tmp/opencode/l17/probe2`, tüm presetlerde `err% = ±0.000`). Yani sapma
yalnız preset dışı elle ayarlanmış profillerde görünür.

**Hata sınıfı: MED** — yaygın yolda doğru, geçerli ama az bilinen bir config
yolunda yanlış sayı.

---

### L17-7 | MED | gui/mouse_test.inl:298-307 + daemon/daemon.cpp:1284 | gösterilen gecikme test **penceresi** değil, **ömürlük** toplam

Fare testi açıldığında `dev.lat` sıfırlanmıyor:
```
$ grep -n "reset\|SIGUSR1\|snapshot" gui/mouse_test.inl ; echo RC=$?
RC=1        <-- 0 eşleşme (yalnız lat_samples okuması, :300)
```
`dev.lat`'i sadece iki yer sıfırlıyor: `daemon.cpp:1284` (yalnız **raw geçişinde**)
ve `daemon.cpp:2923` (SIGUSR1 `snapshot_and_reset`). Yani "Latency p50/p95"
etiketiyle (`mouse_test.inl:533`) gösterilen sayı **daemon açılışından bu yana
biriken** histogram — kullanıcının o anki testinin değil. Kullanıcı testi
sağlıklı bulup ayar yaparsa gerçekten o ayarı etkileyen gecikme değil,
saatler öncesinden kalan ortalama görür.

**Hata sınıfı: MED** — "ölçtüğünü sandığı şeyi ölçmüyor".

---

### L17-8 | MED | gui/profile_mgr.inl:378-388 | profil silme geri alınamaz, ama "At least one profile is required" koruması boşluğu var

Silme onay **var** (`profile_mgr.inl:359` `"Delete profile \"%s\"?"`) ve
`grep -n "undo\|backup\|\.bak\|trash\|restore" gui/profile_mgr.inl` → **0 eşleşme**
(tek "cannot be undone" metni `:460`'ta, **Reset** için). Yani geri alma yolu yok.

**Ama dikkat:** `save_config_now` (`main.cpp:59-86`) diske yazmadan önce mtime'ı
kontrol ediyor ve **çakışmada yazmayı reddediyor** (`:73-83`) — bu iyi bir koruma,
silme de bundan geçiyor. Yani "yanlış dosyayı silme" riski yok; **kullanıcının
kendi profilini geri dönüşsüz silme** riski var. Onay kutusu bunu söylüyor,
bu yüzden MED (HIGH değil).

`reset` (`:444-498`) "This cannot be undone" diyor ve `unsaved=true` bırakıp
kullanıcıdan Save istiyor — **daha güvenli**. Silme ise `save_config_now` ile
**anında diske yazıyor** (`:386`), yani Reset'in aksine. Tutarsız ama kasıtlı
görünüyor.

**Aktif profil değişikliği "anında" uygulanıyor mu? HAYIR** (ölçüldü):
```
$ sed -n '576,596p' gui/widgets_sync.inl | grep -n "save_config_now\|daemon" ; echo RC=$?
RC=1        <-- 0 eşleşme
```
`on_profile_changed` yalnız `current_profile_idx` + `active_profile` **bellekte**
güncelliyor; daemon'a itiş **yok**. `widgets_to_profile` her düzenlemede
`unsaved=true` koyuyor (`:152`) ama görünür bir "kaydedilmedi" göstergesi yok
(`grep -rn unsaved gui/ui_builder.inl` → yalnız pencere-kapatma uyarısı, `:1062`).
⛔ **Sonuç: kullanıcı profili değiştirir, grafik ANINDA yeni eğriyi çizer,
oyun ESKİ profili çalıştırır, ekranda "değişmedi" işareti yoktur.** Kullanıcı
yeni eğrinin etkisini ölçtüğünü sanar. Bu L17-1'in üzerine binen ikinci bir
"sessiz yeşil".

---

### L17-9 | LOW | gui/devices.inl | cihaz listesinde tazelik göstergesi yok

`list_mice()` (`devices.inl:46`) `/proc/bus/input/devices`'i **her seferinde
canlı** okuyor — sahte/bayat liste **yok**. inotify (`ui_builder.inl:700-703`,
`/dev/input` `IN_CREATE|IN_DELETE`) takılıp çıkarılmayı otomatik yakalıyor.

Ancak:
```
$ grep -n "time\|stamp\|ago\|fresh\|last_scan\|updated" gui/devices.inl ; echo RC=$?
RC=0   → yalnız yorum satırları ve "Device list refreshed: N mouse(s) found." (`:242-243`)
```
 liste **ne zaman** tarandığını göstermiyor. `device_combo_select`
(`:177-202`) bağlı olup **fiilen takılı** cihaz için **"(unplugged)"**
placeholder'ı ekliyor — bu **iyi** davranış, sessizce "Tüm cihazlar"'a düşmüyor.

**Hata sınıfı: LOW** — yanlış liste gösterilmiyor, sadece yaşı
bilinmiyor. `inotify` kurulumu başarısız olursa tazelik sessizce durur
(pozitif kontrol yapmadım — bkz. TEMSİL SINIRI).

---

## ⭐ SORU 1'in DOĞRU CEVABI (brifing varsayımı yanlış çıktı)

Brifing "ayrı kopya var mı?" diye varsayıyordu. **Ayrı kopya YOK.**
`graph.inl:53-67` gerçek `accel_union::apply()`'i çağırıyor. Sapma, kopyadan
değil ** daemon'ın çizgiye eklediği, grafikte görünmeyen çarpandan** geliyor
(`rawaccel.hpp:595-599` output-DPI normalizasyonu — L17-1).

---

## ✅ ÖLÇÜLDÜ VE **DOĞRU** ÇIKANLAR (olumsuz sonuçlar da kayıttır)

Bunları da raporluyorum çünkü "araştırıldı, sorun yok" da bir ölçümdür:

**Eksen etiketleri geometrik olarak DOĞRU** — `/tmp/opencode/l17/probe11`:
```
i    label_value  row_pixel      curve_pixel    match?
0    2.40         30.00          2.4000         yes   ...   (5    0.00  330.00  0.0000  yes)
Y-axis mismatches = 0
X-axis mismatches = 0  (labels land on the right gridlines)
```
`graph.inl:179` `sx = max_speed*i/5` ile `:181` `GRAPH_ML+PW*i/5` **birebir**
örtüşüyor; `:184` `max_gain*(5-i)/5` ile `:187` `GRAPH_MT+PH*i/5` de öyle.

**X ekseni birimi DOĞRU ve ters DEĞİL** — `tr("Speed (ips)")` (`:194`).
`daemon.cpp:1236` `dpi_factor = NORMALIZED_DPI/dev.dpi` ve `:2525`
`ips_factor = dpi_factor/time_ms` → `speed = counts/ms × 1000/dpi` = **inç/saniye**.
`/tmp/opencode/l17/probe5`:
```
dpi      dpi_factor     counts/ms @1ips
400      2.500000       0.4000
800      1.250000       0.8000
1000     1.000000       1.0000
1600     0.625000       1.6000
3200     0.312500       3.2000
=> 'ips' IS dpi-normalised: 1 ips means 1 inch/s on ANY dpi.
```
**DPI/px ekseni ters değil — hatta px bile değil, DPI-normalize ips.** Somut örnek:
giriş **1000 count @ 1000 DPI, 1 ms** → `1000 × (1000/1000) / 1 = 1000 ips`.
Grafikte **1000 ips** noktasında okunur (varsayılan 50 ips penceresinin
**20× dışında** — kullanıcı kaydırmak zorunda). Aynı fiziksel 1 inç/s,
800 DPI'da `1250 count/ms` → yine **1.0 ips**. Doğru.

**Yeniden ölçekleme (zoom/pan) KESİN** — `to_cx` (`:88`) affine, `s/max_speed`.
`/tmp/opencode/l17/probe10`:
```
s(ips)    span=50       span=25       span=12       span=100      span=200
5.00      gain=1.0250@10%gain=1.0250@20%gain=1.0250@40%gain=1.0250@5%gain=1.0250@2%
12.50     gain=1.0625@25%gain=1.0625@50%gain=1.0625@100%gain=1.0625@12%gain=1.0625@6%
```
Aynı hız → **her zaman aynı gain**. Eksen sabit kaldığı için yanlış göstermiyor.

**Eğri tepe kırpılması (to_cy clamp) ulaşılabilir DEĞİL** (bulgu çıkmadı) —
`/tmp/opencode/l17/probe12`: `compute_max_gain`'in 200 örnekli taraması gerçek
tepeyi **kaçırmadı** (`worst case: true_max exceeds axis top by 0.000000`),
1.2× pay güvenliği yeterli. `:89`'daki `std::clamp` bu yüzden sessiz veri
kaybı üretmiyor.

**LUT noktaları ile eğri TUTARLI** — `/tmp/opencode/l17/probe16`: LUT
velocity modunda her saklanan x'te `lut_stored_to_gain()` (`:245`) ile
`au.apply(x)` (`:124`) **0 uyuşmazlık**.

**HID++ DPI/rate yazma sonucu GÖSTERİLİYOR** (L14 ile aynı sınıf, burada
temiz): `logitech_hidpp.cpp:1915` `.has_value()` ile ack zorunlu;
`hidpp_panel.inl:616-622` `(rejected)` basıyor; ayrıca yazma sonrası
`hw_query_current(S)` (`:626`) cihazdan **yeniden okuyup** ekrana geri basıyor.
Sadece LOD kırık (L17-5).

**Fare testi gecikmesi için `now_ns`/`now_us` kullanımı DOĞRU** —
`mouse_test.inl:242` `CLOCK_MONOTONIC_RAW`, daemon `now_ns()`
(`daemon.cpp:2178-2182`) ile **aynı saat**; sapma yok (L17-4 saat değil,
**pencere tanımı** hatası).

**2 sn bayatlama kapısı DOĞRU TASARLANMIŞ** — `/tmp/opencode/l17/probe14`:
ölü cihaz ~2.25 s'de (8 poll) yakalanıyor, sağlıklı cihaz asla yanlış
geçersizlenmiyor. ⛔ Kusur **kimlik** doğrulamasının olmaması (L17-3).

---

KAPI     : `bash tests/run_tr_coverage.sh` → **rc=0**, `Result: PASS`
           (10 GUI dosyasını tarıyor, `tests/run_tr_coverage.sh:7-18` —
           dahil `gui/graph.inl` ve `gui/mouse_test.inl`).
           ⛔ Bu kapı yalnız **çeviri kapsamı**; sayısal doğruluk yapmıyor.
           Ayrıca 9 bağımsız ölçüm probu derlendi ve koşuldu (tamamı rc=0):
           `probe`(L17-6), `probe2`(preset eşleşmesi), `probe3`, `probe4`(noaccel),
           `probe5`(ips birimi), `probe6`(DPI ayrışması), `probe7/8`(cihaz kimliği),
           `probe9`(gecikme penceresi), `probe11`(eksen), `probe12`(kırpma),
           `probe13`(LOD sessiz yeşil), `probe15`(raw mod), `probe16`(LUT),
           `probe17`(mutasyon 5/5).
           Derleme kanıtı: `g++ -std=c++20 -O2 … -I include/` → **BUILD_RC=0**;
           problar **gerçek proje başlıklarını** (`rawaccel.hpp`, `presets.hpp`,
           `accel-union.hpp`) bağlar, elle yazılmış formül yoktur.

KAPSANMAYAN:
  - ⛔ **`gui/graph.inl:19-44` `compute_max_gain` yalnız 200 nokta tarıyor.**
    Dar bir LUT/düzleme tepe kaçırabilir; `probe12` classic modda bulamadı
    ama **LUT modunda `accel_union::apply()` her örnekte ikili arama yaptığı
    için tarama maliyeti 200× daha yüksek** — `draw_curve` (`:124`, 601 örnek)
    ile `compute_max_gain` (`:24`, 200 örnek) **farklı örnek sayısı** kullanıyor.
    LUT'ta tepe iki örnek arasına düşerse `to_cy` sessizce kırpar. **Ölçemedim**
    (tarama çok uzun sürdü, 120 s zaman aşımı). → L20/başka lane.
  - ⛔ **`gui/graph.inl:260-266` `on_graph_scroll` ve `:283-306` pan** —
    gesture math'ı statik okudum, **çalışma anı ölçmedim** (X server yok).
  - ⛔ **`gui/mouse_test.inl` X11 pointer grab (Tier 1/2/3)** — `XGrabPointer`
    gerçek çağrısı **ölçemedim** (X server yok). `static_assert`'ler
    (`:61-63`) doğru ama canlı grab davranışı temsil dışı.
  - ⛔ **`gui/devices.inl` inotify kurulumu** — `/dev/input` izlemesinin
    gerçekten takılıp takılmadığını **çalıştırmadım** (cihaz yok).
    `list_mice()`'ın statik doğruluğunu okudum.
  - ⛔ **`hidpp_panel.inl` gerçek HID++ yazma** — `set_dpi`/`set_polling_rate`
    `HidppTransport` + gerçek `/dev/hidraw` istiyor; `include/logitech_hidpp.hpp:410`
    bunları özel yol gerektiriyor, **ölçemedim**. L17-5'i **kod yolu +
    dal izlemesiyle** kanıtladım, koşarak değil.
  - ⛔ **`profile_mgr.inl` `preset_preview_text` (`:62-104`)** — 8 preset için
    `make_preset` tek kaynak; **çalıştırmadım**, statik okudum.
  - ⛔ **`gui/widgets_sync.inl` / `ui_builder.inl`** — başka lane'in sahibi;
    L17-8'in "anında uygulanıyor mu" cevabı **oradan** geldi, o dosyalara
    **yazmadım**.

TEMSIL SINIRI:
  - **PS5.1/çalışma anı yok.** Bu turda **gerçek fare, gerçek Logitech HID++
    cihazı, X server ve root yok.** Tüm bulgular **statik kod yolu izlemesi +
    gerçek proje başlıklarına bağlanan ölçüm programları** ile kanıtlandı.
    GUI'nin **ekrana piksel çizen kısmı** (Cairo `on_graph_draw`, `:71-257`)
    hiç çalıştırılmadı — ölçümler `to_cx`/`to_cy`/`compute_curve` gibi
    **saf lambdaları** birebir kopyalayıp matematiği doğruladı; piksel
    çıktısının kendisi doğrulanmadı.
  - **`telem_gain` yalnız `daemon/daemon.cpp:2525-2532`'nin matematiğini
    birebir taklit ederek** elde edildi. Gerçek daemon süreci
    (evdev→uinput, kernel frame-stamp SM-1 aralığı) **koşmadım**. Yani
    L17-1'in sayıları *canlı telemetri* değil, **canlı telemetriyi üreten
    formülün** sayılarıdır — `time_ms`'in SM-1 kernel yolundan gelmesi
    (`:2397-2399`) hızı değiştirir ama **oranı değil** (oran
    `dpi_adjustment`'dan geliyor, `probe17` 5/5).
  - **SOMUT ÖRNEK (L17-1, brifing istedi):** giriş **1000 count @ 1000 DPI,
    1 ms frame, `gaming` preset'i, output_dpi=1000** →
    `in_ips = 1000 × (1000/1000)/1 = 1000 ips`. Grafik bu noktada
    `gain = 1.200` der (varsayılan 50 ips penceresi **dışında**, kaydırmak
    gerekir). `output_dpi` yok → grafik **1.200** der, oyun **1.000**.
    `output_dpi=2000` → grafik **1.200**, oyun **2.000** (+67%).
  - **`probe17` mutasyon testi 5/5 tuttu** ama bu **istatistiksel** değil
    **algebraik** bir kanıt: model 100× büyük sayımla (`out_x` tam sayı
    kesmesi ihmal edilebilir) birebir tuttu. `now_ns()` iki çağrı arası
    gürültüsü (~13 ns) `probe9`'daki mutlak değerleri etkiler, **oranı değil**.