# L16 — GUI: widget inşası, senkronizasyon, çeviri

### L16 | alt-ajan (denetim turu) | 2026-10-01

**KAPSAM** (kesin):
- `gui/widgets_sync.inl` (895 satır) — tamamı okundu
- `gui/tr.inl` (774 satır) — tamamı okundu
- `gui/ui_builder.inl` (1670 satır) — widget inşası + sinyal bağları okundu (⛔ YAZILMADI)
- Okuma yardımcı olarak: `gui/app_state.hpp`, `gui/main.cpp`, `gui/devices.inl`,
  `gui/profile_mgr.inl`, `gui/daemon_comm.inl`, `src/config.cpp`, `include/config.hpp`,
  `include/rawaccel-base.hpp`, `cli/main.cpp` (ilgili bölümler), `tests/tr_coverage.cpp`,
  `tests/run_tr_coverage.sh`

---

## BULGULAR

### L16-01 | `gui/widgets_sync.inl:167` (etiket: `ax.input_offset = SPIN(offset_spin)`) | **HIGH**
**Panel, `sanitize`'in meşru olarak koruduğu bir değeri göstermiyor ve sonraki
herhangi bir widget düzenlemesinde onu kalıcı olarak geri yazıyor.**

Kanıt zinciri (üç ayrı ölçüm):

1) **Gauge, sanitize alanından DAR.** `gui/ui_builder.inl:242`
   `S->offset_spin = make_spin(0, 100, 0.5, 0.0);` — gauge `[0,100]`.
   `src/config.cpp:494` `if (a.input_offset > CAP_X_MAX) a.input_offset = CAP_X_MAX;`
   ve `include/config.hpp:18` `CAP_X_MAX = 500.0` — sanitize `[0,500]`.
   `cli/main.cpp:1176-1177` `input_offset` için CLI alanı da `[0, CAP_X_MAX]`.
   → **aralık `[100,500]` yalnız GUI'de erişilemez; `[0,500]` yalnız GUI'de yazılamaz.**

2) **GtkSpinButton gerçekten kırpar** (varsayım değil, çalıştırıldı):

```
$ g++ -std=c++20 $(pkg-config --cflags gtk4) clampback.cpp src/config.cpp ... 
$ DISPLAY=:0 GDK_BACKEND=x11 ./clampback
--- value the loader keeps, GUI gauge narrower than sanitize domain ---
  loader kept input_offset = 300        (sanitize domain allows it)
  GUI displays = 100        | after ANY later widget edit file+daemon = 100         *** SILENTLY REWRITTEN ***

  loader kept speed_min    = 100000     (sanitize domain allows it)
  GUI displays = 500        | after ANY later widget edit file+daemon = 500         *** SILENTLY REWRITTEN ***

--- control: value inside the gauge ---
  loader kept input_offset = 40         (sanitize domain allows it)
  GUI displays = 40         | after ANY later widget edit file+daemon = 40          intact
```

   Ayrı saf-ölçüm (`spinreal.cpp`, pozitif kontrolüyle):
```
offset_spin gauge 0..100, set_value(300) -> get_value()=100  CLAMPED
speed_min  gauge 0..500, set_value(100000) -> get_value()=500  CLAMPED
CONTROL     set_value(250) -> get_value()=250  KEPT (harness OK)
```
   ⭐ **Pozitif kontrol satırı şart**: kontrol değeri korunduğu için
   "CLAMPED" satırları bozuk bir test koşumundan değil, gerçek GtkSpinButton
   davranışından kaynaklanıyor.

3) **`load_config` değeri KORUYOR** (yani değer dosyaya/legal girdi):
```
[2] load_config -> input_offset=300 cap.x=500
[2] => sanitize KEEPS input_offset=300
```

**Bu kod çalışırken ne olduğunda fark edilir?** Kullanıcı
`rawaccel-cli set-param X input_offset 300` (CLI kabul ediyor) veya elle
`"input_offset": 300` yazar. GUI açılır, **100 gösterir** — kullanıcı
dosyadaki 300'ün farkında olmadan. `profile_to_widgets` bu değeri struct'a
yazmaz, ama `widgets_to_widgets`'ın **bir sonraki** çalışmasında
(`gui/widgets_sync.inl:167`, `ax.input_offset = SPIN(offset_spin)`) okunan artık
**100**'dür ve `gui/main.cpp:84` `save_config` onu diske yazar. Yani:
**panel kullanıcıya daemon'ın çalışmadığı değeri gösterir, hiçbir uyarı yoktur,
ve kullanıcı panelde hiçbir widget'e dokunmadan bile bir sonraki Apply'de
değer kalıcı olarak 100'e düşer.**

**Aynı sınıfın ikinci örneği — `speed_min` / `speed_max`:**
`gui/ui_builder.inl:453-454` `make_spin(0, 500, 1, 0, 0)`.
`src/config.cpp:568-571` yalnızca alt sınırı uygular, **üst sınır YOK**:
```
    // speed_min / speed_max: non-negative; max >= min if both nonzero
    if (p.speed_min < 0) p.speed_min = 0;
    if (p.speed_max < 0) p.speed_max = 0;
    if (p.speed_max > 0 && p.speed_max < p.speed_min) p.speed_max = p.speed_min;
```
`cli/main.cpp:1174` `if (!min_ok(key.c_str(), 0)) return 1;` — CLI de üst sınır
koymuyor. Ölçüldü: `load_config -> speed_min=100000` korunuyor, GUI 500 gösteriyor.

**Aynı alan sınıfının kalan 3 üyesi** (ölçüldü, `make_spin` satırı verildi):
`motivity_spin` `ui_builder.inl:248` alt `[0.01,10]` vs sanitize `src/config.cpp:470`
`if (a.motivity < 0) a.motivity = 0;` → `[0,10]` · `gamma_spin` `:249` vs `:471` ·
`accel_spin` `:238` `[0,20]` vs `src/config.cpp:439-443` negatif `acceleration`
`cap.y == 0` iken meşru (azaltma özelliği).

⭐ `include/config.hpp:65-70` bu sınıfı **kendisi** kaydediyor ve yalnız
`input_offset`'u sayıyor ("NOT clamped (reported, not fixed — GUI-side)").
`motivity`/`gamma` alt sınırı ve `speed_min`/`speed_max` üst sınırı
o listede **yok** — yani kayıt eksik.

---

### L16-02 | `gui/widgets_sync.inl:585-587` | **MED**
**"discarded" (silindi) mesajı gerçeği söylemiyor; değişiklik duruyor ama
tek hatırlatıcı susturuluyor.**

`on_profile_changed` (`:576-596`):
```cpp
    if (S->unsaved) {
        set_status(S, trf("Warning: unsaved changes to \"%s\" were discarded.",
                          cur_prof(S).name.c_str()));
        S->unsaved = false;
    }
    S->current_profile_idx = idx;
    ...
    profile_to_widgets(S);
```
Akışta eski profilin struct'ını **geri alan hiçbir satır yok.**
`widgets_to_profile` her widget düzenlemesinde çalıştığı için düzenleme
zaten `S->config.profiles[eski]` içindedir. Ölçüm (GTK'siz, gerçek veri akışı):

```
$ ./profswitch
after edit:   gaming.acceleration = 0.019  unsaved=1
after switch: gaming.acceleration = 0.019  unsaved=0
status shown : "Warning: unsaved changes to "gaming" were discarded."

window-close guard (unsaved==false) prompts user?  NO  <- user is never told the edit is pending
value that a later Save would write for "gaming": 0.019  -> the edit SURVIVED, so "discarded" is FALSE
```

**Kullanıcıya ne görünür?** Durum çubuğu "silindi" der; kullanıcı
`gui/ui_builder.inl:1062`'deki `if (!S->unsaved) return FALSE;` kapanış
korumasının **tetiklenmeyeceğini** bilmez. Sonraki herhangi bir Apply'de
`gui/main.cpp:84` `save_config` 0.019'u **yazar**. Yani kullanıcı hem yanlış
bilgilendirilir hem de "saklamadım" deyip kapatırsa sessiz veri kaybı olur.

⛔ `S->unsaved = false` satırı olmasaydı kapanış koruması çalışır ve
kayıp kullanıcıya sorulurdu. **Bu, "sessiz yeşil" sınıfıdır: koruma var,
görünür değil, ve `unsaved` temizlenmesi onu işsiz bırakıyor.**

---

### L16-03 | `tests/tr_coverage.cpp:213` | **MED**
**Çeviri kapsam kapısı, `tr()` sarmalayıcısı OLMAYAN ham bir metni
yakalayamıyor — sessiz yeşil.**

Kapı tarayıcısı yalnız şu isimleri arıyor (`tests/tr_coverage.cpp:213`):
```
    "tr", "trf", "trlbl", "mlbl", "trbtn", "trchk", "trtip", ...
```

**Mutasyon kanıtı** (çalışma ağacına dokunmadan, `/tmp` kopyasında):
```
$ cp -r gui tests include src /tmp/opencode/mut2/
$ # widgets_sync.inl'e tr() SARMAYAN yeni bir kullanıcı metni enjekte edildi:
$ #   set_status(S, "Reload failed unexpectedly.");
$ bash /tmp/opencode/mut2/tests/run_tr_coverage.sh
MISSING: none — every UI string has a Turkish entry
Result: PASS
=== gate rc=0 ===
```

**Pozitif kontrol — kapı gerçekten çalışıyor mu?**
```
$ # /tmp/opencode/mut kopyasında tr.inl'den KULLANILAN bir "Cancel" girdisi silindi
$ bash /tmp/opencode/mut/tests/run_tr_coverage.sh
Result: FAIL (missing translations) — eksik çeviri(ler) mevcut
=== gate rc=1 ===
```
⛔ Yani kapı **sessiz yeşil değil** — `tr()` içindeki eksikleri yakalıyor.
Ama **`tr()` dışındaki** eksik çeviri sınıfını yapısal olarak yakalayamıyor.

**Pratikte mevcut kodda bu sınıf boş** (ölçüldü):
```
$ grep -anP 'set_status\s*\(\s*\w+\s*,\s*"|gtk_label_set_(text|markup)\s*\([^,]+,\s*"|...' gui/*.inl gui/main.cpp \
  | grep -avP 'trf?\(' | grep -av '//'
gui/mouse_test.inl:225..229   gtk_label_set_text(..., "—");   ×5
```
5 eşleşmenin beşi de değişken çizgisi (`—`), çeviri gerektirmiyor.
→ **bulgu kodda değil KAPIDA.** Bu bir "gelecek tuzak": ilk `tr()`siz
gerçek metin geldiğinde kapı yeşil kalacak.

---

### L16-04 | `gui/tr.inl:551-552` | **INFO**
**İki ölü çeviri anahtarı.** `"Saved & reloaded: %s"` ve `"Saved: %s"`
hiçbir yerde kullanılmıyor; `save_config_now` artık farklı dizeler
kullanıyor (`gui/main.cpp:111,115,128,136,137`).

Pozitif kontrol (aynı grep, aynı biçimde) — iki anahtar 0, kontrol anahtarları ≥1:
```
DEAD CHECK (0 = truly unused):
  Saved & reloaded: %s         -> 0 hits outside tr.inl
  Saved: %s                    -> 0 hits outside tr.inl
POSITIVE CONTROL (must be >0):
  Applied & reloaded: %s       -> 1 hits outside tr.inl
  Save error: %s               -> 1 hits outside tr.inl
```

---

### L16-05 | `gui/tr.inl` sözlüğü | **INFO** (olumlu — "bitti" iddiası tutuyor)
**Eksik çeviri anahtarı YOK. Ölçüm, bağımsız olarak yapıldı.**

```
distinct tr() keys used:   129   -> MISSING = 0
distinct trf() keys used:   41   -> MISSING = 1
```
Tek `trf` "eksik"i sahte pozitiftir: `gui/hidpp_panel.inl:610-612` çok satırlı
string literal'ı birleştirir; birleşmiş hâli `gui/tr.inl:445`'te **vardır**
(birebir karşılaştırıldı). Dolayısıyla gerçek eksik sayısı **0**.

Ayrıca **kaydet/geri al çevirisi TAM** (görev 8):
```
gtk_button_new_with_label(tr(...)) — 7 anahtar:
OK   Cancel      OK   Delete      OK   OK         OK   Overwrite
OK   Quit Without Saving          OK   Reset      OK   Save and Quit
trbtn/trchk — 16 anahtar: 16/16 OK  (Apply, Save As, Reload, Start, Stop, ...)
```
Türkçe karşılıklar mevcut: `{"Cancel","İptal"}` (`tr.inl:514`),
`{"Save and Quit","Kaydet ve Çık"}`, `{"Applied & reloaded: %s","Daemon'a uygulandı ve
yeniden yüklendi: %s"}` (`tr.inl:550`). **"Save"/"Cancel" tutarsızlığı YOK.**

Kapı 5 (`tests/run_tr_coverage.sh`) exit 0, `Result: PASS`, 26 ORPHAN raporlu.

---

### L16-06 | `gui/widgets_sync.inl` (tamamı) | **INFO** (olumlu — sessiz yeşil YOK)
**Bu dosyada hiçbir senkron fonksiyonu çağrılmadan durmuyor.** Görev 5'in
istenen `grep -c` karşılaştırması, 26 fonksiyonun tamamı için:

| fonksiyon | tanım | çağrı | G_CALLBACK |
|---|---|---|---|
| `widgets_to_profile` | 1 | 8 | 0 |
| `profile_to_widgets` | 1 | 7 | 0 |
| `update_raw_sensitivity` | 1 | 3 | 0 |
| `update_mode_sensitivity` | 1 | 3 | 0 |
| `on_param_changed` | 1 | 2 | 4 |
| `on_notify_param_changed` | 1 | 2 | 5 |
| `on_profile_changed` | 1 | 2 | 1 |
| `on_xy_link_toggled` | 1 | 2 | 1 |
| `on_raw_input_toggled` | 1 | 2 | 1 |
| `on_apply_clicked` | 1 | 2 | 1 |
| `on_daemon_start/stop/reload` | 1 ea | 2 ea | 1 ea |
| `on_perf_clicked`, `on_save_clicked` | 1 | 2 | 1 |
| `idx_to_mode/mode_to_idx/idx_to_cap/cap_to_idx` | 1 ea | ≥1 ea | 0 |
| `mode_uses`, `row_set_visible`, `fmt_us`, pkexec yardımcıları | 1 ea | ≥1 ea | 0 |

**Çağrısız fonksiyon sayısı = 0.** Karşılaştırma yöntemi pozitif kontrollü:
aynı tarama `fmt_us`'u 6, `mode_uses`'i 22, `row_set_visible`'ı 20 buluyor.

---

### L16-07 | `gui/widgets_sync.inl:150-439` | **INFO** (olumlu — ölü arayüz YOK)
**Panel↔config simetrik; yazılmayan widget YOK.**

```
panel->config okuma noktası (SPIN/CHECK/DD):  39
config->panel yazma noktası (SET_*):           39   (40. madde `w` = #define satırı)
A \ B  (okunup yüklenmeyen widget):            BOŞ
B \ A  (yüklenip okunmayan widget):            BOŞ
```
Tüm `AppState` değer taşıyan widget'ları (`_spin|_combo|_check|_entry`) = **42**;
39'u makro kümesinde. Kalan 3'ü makro dışı ama **yine de senkronize** —
`device_id_combo` (`:265` `gtk_drop_down_get_selected`),
`match_app_entry` (`:290` `gtk_editable_get_text` / `:409` `gtk_editable_set_text`),
`raw_input_check` (`:614` kendi geri çağrısı yazar). `lang_combo` ve
`profile_combo` config profiline yazmaz (dil tercihi ayrı dosyaya;
profil seçimi `rebuild_profile_combo`).

Sinyal bağı taraması: 33 spin'in 14'ü doğrudan `connect_spin`, 19'u
`ui_builder.inl:253-259` ve `:414-416` döngüleriyle bağlanıyor — **0 bağlanmamış spin**.

---

### L16-08 | `gui/ui_builder.inl` | **INFO**
**Satır dizisi sınırları içinde — sessiz bellek hatası yok.**
`app_state.hpp:237` `accel_row_label[15]`, `:249` `y_row_label[6]`.
`ui_builder.inl:313-327` 0..14, `:418-423` 0..5 dolduruyor.
`widgets_sync.inl:63-98` okuduğu indeksler: accel `0..14`, y `1..5`. Hepsi sınırda.
`idx_to_mode` (`widgets_sync.inl:124`) `GTK_INVALID_LIST_POSITION`
(= `0xffffffff`, `/usr/include/gtk-4.0/gtk/gtktypes.h:79`) gelirse
`(i >= 0 && i < 7)` ile `noaccel`'a düşüyor — **tanımsız indeks okuması yok.**

---

### L16-09 | `gui/main.cpp:224` (tek `load_config` çağrısı) | **INFO** (tasarım, olumlu)
**"Yenile" akışı kullanıcının kaydedilmemiş değişikliğini EZMİYOR — çünkü
daemon'dan config çeken hiçbir yol yok.**

```
$ grep -anP 'load_config' gui/*.inl gui/main.cpp
gui/main.cpp:224:            state.config = load_config(state.config_path);
```
**Tam olarak 1 çağrı, açılışta.** GUI daemon'dan config **çekmiyor**
(`daemon_ipc_query` yalnız `"ping"`, `"status"`, `"reload"`, `"set_active_app"`
— ölçüldü; `"set_config"` yalnız **gönderim** `daemon_comm.inl:253`).
Yenileme yerine **algıla-ve-uyar** var: `main.cpp:73-83` STALE-1 mtime
koruması, değişikliği sessizce ezmek yerine status çubuğuna yazıyor.
→ Görev 3'ün sorduğu "kayıp var mı" → **bu akışta kayıp yok.**
(Karşı yönde, L16-01'de ölçülen yazma kaybı var.)

---

## ⭐ KAPI (koşturulanlar + ham çıktı)

```
1) bash tests/run_tr_coverage.sh
   → "Result: PASS" + "ORPHANS (unreferenced entries, 26)" listesi
   → ÜRETİLEN SAYI: 26 ORPHAN, 0 MISSING        rc=0

2) g++ … clampback.cpp src/config.cpp  &&  DISPLAY=:0 ./clampback
   → 2 "*** SILENTLY REWRITTEN ***" + 1 "intact" kontrol
   → rc=0

3) g++ … spinreal.cpp  &&  DISPLAY=:0 ./spinreal
   → 2 CLAMPED + 1 "KEPT (harness OK)" pozitif kontrol     rc=0

4) g++ … profswitch.cpp  &&  ./profswitch
   → edit 0.019 korunuyor, unsaved 1→0, "discarded" yalan     rc=0

5) g++ … xylink.cpp  &&  ./xylink
   → P157 senaryosu; guard'ın KAPALI olduğu kanıtlandı        rc=0

6) MUTASYON /tmp/opencode/mut2/  (tr()suz metin enjekte)
   → "Result: PASS", rc=0        ← KAPI KAÇIRDI
7) MUTASYON /tmp/opencode/mut/   (kullanılan "Cancel" girdisi silindi)
   → "Result: FAIL", rc=1        ← KAPI YAKALADI (pozitif kontrol)
```
⛔ **7 kapının hiçbiri koşturulmadı** (lane dışı, ~169 s; bkz. KAPSANMAYAN).
⛔ **Hiçbir dosya değiştirilmedi.** Mutasyonlar yalnız `/tmp/opencode/` kopyalarında.
`git checkout/stash/reset` **çağrılmadı.**

---

## KAPSANMAYAN (lane dışı kaldığım, birinin bakması gereken yer)

1. **`gui/ui_builder.inl` içinde değişiklik gerektiren düzeltmeler.** L16-01'in
   asıl onarımı `ui_builder.inl:242` (offset gauge'ı `[0,500]`'e çıkarmak) ve
   `:453-454`'te `speed_min/max` için üst sınır — bu dosya başka ajanın sahibi.
   Ben yalnız ölçtüm, **dokunmadım.**
2. **`on_import_profile` (`profile_mgr.inl:633-635`)** import sonrası
   `save_config_now` çağırıyor ama `rebuild_profile_combo` **yeni profili seçmiyor**
   (`current_profile_idx` eski kalır) — L16'nın konusu değil (profil CRUD),
   ama L16-01 ile birleşince: import edilen profilin `input_offset=300`'ü
   import sonrası ilk Apply'de yine 100'e düşer. ⛔ Ölçmedim, sadece okudum.
3. **LUT editörü senkronu** (`gui/graph.inl:415,540,587`) — `xy_linked`'ken
   `accel_y = accel_x` kopyası. L16-01 ile aynı sınıf ama grafik yolunda
   ölçmedim.
4. **HID++ panel widget'ları** (`hw_dpi_spin` vb.) config'e yazmıyor (donanıma
   yazıyor) — kapsam dışı, `gui/hidpp_panel.inl` başka lane.
5. **Yedi kapı / build / ASan** — lane'de değil, koşturulmadı.

---

## TEMSIL SINIRI (ne doğrulayamadım)

1. ⛔ **Çalışma anı / gerçek fare yok.** `DISPLAY=:0` üzerinde GTK widget'ları
   *programatik* test ettim (`gtk_spin_button_set_value` → `get_value`),
   ama **gerçek kullanıcı sürükleme-düşürme etkileşimi, klavye, kip
   değiştirme, pencere yeniden boyutlandırma ölçülmedi.** L16-01'in "kullanıcı
   görür mü" yönü bu yüzden **kod + widget API düzeyinde kanıtlı, gözle
   doğrulanmamıştır.**
2. ⛔ **Daemon yok, `/dev/input` yok.** Panel→daemon→panel tam döngüsü
   (kaydedilen değerin gerçekten daemon'da değişmesi) çalıştırılmadı.
   L16-01'in kanıtı `save_config` + `load_config` üzerinden **dosya
   düzeyindedir**; daemon'ın bunu aynen uyguladığı varsayılmıştır
   (uygulama `daemon/daemon.cpp`'ta, o lane).
3. ⛔ **L16-02 vektörü değişmedi** — eşitlik karşılaştırması `operator==`'in
   `1e-9`/`1e-5` epsilonsuz varyantıyla modellendi; gerçek `accel_args::operator==`
   (`rawaccel-base.hpp:93-108`) `deq()` epsilonsuz **değil**, yani "same"
   kararı gerçekte daha muhafazakâr olabilir. Etkilenen bulgu **yok**
   (L16-02 hiç `operator==` kullanmıyor), ama L16-01'in `same` dalı için
   not düşülmeli.
4. ⛔ **`xy_linked` sıfırlama senaryosu (L16-05'teki ölçüm) üretimde
   ERİŞİLEBİLİR DEĞİL**: `ui_builder.inl:986` açılışta `profile_to_widgets`
   çağırdığı için asimetrik bir Y ile `xy_linked` zaten `false`'a düşüyor.
   Senaryoyu kurdum ve **guard'ın kapalı olduğunu gördüm** — bu yüzden
   rapora bulgu olarak değil **doğrulanmış-negatif** olarak girdi.
5. ⛔ **Çeviri: sözlükteki 313 anahtarın tümü tek tek gözden geçirilmedi.**
   Ölçüm makine-kesin (kullanılan ∌ kullanılmayan) ama "Türkçe karşılık
   **anlam bakımından** doğru mu" sorusu **insan gözüyle incelenmedi**.
   Örn. `tr.inl` `{"Lookup (LUT)","Arama (LUT)"}` — "Arama" kelimesi
   Türkçede "search" anlamına gelir, LUT/"lookup" için belirsiz;
   anlamsal bir düzeltme önerisi, **eksik anahtar değil.**
6. ⛔ **Çok dilli/i18n dışı**: `TR_EN` diye ikinci bir sözlük **yok**
   (ölçüldü: `grep -anP 'TR_EN' gui/tr.inl` → 0). İngilizce anahtarın kendisidir,
   AGENTS.md ile uyumlu. Doğrulanmadı: `refresh_language`'ın
   `S->updating = true` blokunda `rebuild_profile_combo` → `profile_to_widgets`
   → `widgets_to_profile` geri çağrısı sırasında **gizli bir geri-yazım**
   olup olmadığı (alttan-üstte `updating` koruması var, `widgets_sync.inl:151`
   `if (S->updating ...) return;` — gözle doğrulandı, **çalıştırılmadı**).
