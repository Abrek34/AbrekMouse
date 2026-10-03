### L05 | alt-ajan (config şema / yükleme / kaydetme / doğrulama) | 2026-10-01

**KAPSAM** (salt okuma + ölçüm; çalışma ağacında HİÇBİR dosya değiştirilmedi):
- `include/config.hpp` (205 satır) — okundu
- `include/presets.hpp` (228 satır) — okundu
- `src/config.cpp` (1253 satır) — okundu
- Referans: `docs/research/parameter_index.md` (250 satır) — okundu
- Ölçüm ekleri (raporla birlikte, aynı dizin): `denetim-L05-olcum.txt` (H1–H9),
  `denetim-L05-olcum2.txt` (bool/kapı-kısmi), `denetim-L05-olcum3.txt` (kazanç
  bedeli + preset atıl alanlar), `denetim-L05-mutbuild.log` /
  `denetim-L05-mutfiltered.log` / `denetim-L05-mutfull.log` (MUTASYON),
  `denetim-L05-basebuild.log` / `denetim-L05-basefiltered.log` /
  `denetim-L05-basefull.log` (POZİTİF KONTROL)
- Ölçüm yöntemi: `/tmp/opencode/L05/` altına **kopyalanmış** `src/config.cpp` +
  `tests/test_accel.cpp` + `include/` + `daemon/*.hpp` ile g++ `-O1 -std=c++20`.
  Mutasyon testi de bu kopyada yapıldı (`md5sum` kanıtı raporun altında).
  ⛔ `git checkout/stash/reset` çalıştırılmadı, hiçbir kaynak dosya yazılmadı.

---

## BULGULAR

### L05-01 | CRIT | `src/config.cpp:726-727` ve `src/config.cpp:662-663`
**`use_raw_input` ve `disable` sessizce DEĞERİNİ TERSİNE ÇEVİRİYOR — sayısal `0`/`1` kabul edilmiyor.**

`gain` (`:132-137`) ve `raw_passthrough` (`:283-288`) için O31-C31 ile sayısal
`0`/`1` **kabul** edilir. Aynı sınıftaki iki alan için bu tolerans **yok** —
okuyucu yalnızca `is_boolean()` kabul ediyor, geri kalanı `struct` varsayılanına
düşüyor:

```
$ cat .aihaberlesme/mesajlar/denetim-L05-olcum2.txt   (h78.cpp çıktısı)
### top-level use_raw_input and profiles[].disable with non-boolean values
  value=true      use_raw_input -> 1    disable -> 1
  value=false     use_raw_input -> 0    disable -> 0
  value=0         use_raw_input -> 1    disable -> 0     <-- 0 yazıldı, TRUE okundu
  value=1         use_raw_input -> 1    disable -> 0     <-- 1 yazıldı, FALSE okundu
  value=2         use_raw_input -> 1    disable -> 0
  value="true"    use_raw_input -> 1    disable -> 0
```

Aynı ölçüm, aynı binari, aynı JSON şekli — `gain`/`raw_passthrough` doğru davranıyor:
```
### raw_passthrough / gain with numeric and boolean JSON values
  value=0       gain -> 0    raw_passthrough -> 0
  value=1       gain -> 1    raw_passthrough -> 1      <-- bu ikisi TOLERANSlı
  value=2       gain -> 1    raw_passthrough -> 0
```

**Neden CRIT — ve bir düzeltme:** Önce bu lane'e verilen referans belge
(`docs/research/parameter_index.md:19` ve `:222`) her iki alanı da
**"Dormant (daemon okumuyor)"** diye tanımlıyor. **Bu belge YANLIŞ.** İkisi de
canlı birer daemon anahtarı. Ölçüldü:

```
$ grep -rn "use_raw_input" --include=*.cpp --include=*.hpp --include=*.inl src/ cli/ gui/ daemon/ include/
  daemon/daemon.cpp:483:  // PAS-1 — the top-level use_raw_input flag is now a real master switch.
  daemon/daemon.cpp:484:  raw_input_enabled_.store(config_.use_raw_input, std::memory_order_relaxed);
  daemon/daemon.cpp:887:  if (!raw_input_enabled_.load(std::memory_order_relaxed)) {   <-- TÜKETİCİ
  daemon/daemon.cpp:1430: const bool raw_now = new_cfg.use_raw_input;                  <-- reload yolu
  daemon/daemon.cpp:1568: if (!raw_input_enabled_.load(std::memory_order_relaxed)) {   <-- TÜKETİCİ
  daemon/daemon.hpp:416:  std::atomic<bool> raw_input_enabled_ = true;
  gui/widgets_sync.inl:614: S->config.use_raw_input = gtk_check_button_get_active(btn) != FALSE;  <-- GUI anahtarı

$ grep -rn "\.disable\b" --include=*.cpp --include=*.hpp --include=*.inl src/ cli/ gui/ daemon/
  daemon/daemon.cpp:911,1094,1350,1576   <-- DÖRT tüketici
$ sed -n '909,911p' daemon/daemon.cpp
  // PAS-2: per-device disable flag — leave the device untouched so the
  // desktop handles it normally ("safe mode", no acceleration applied).
  if (prof && prof->dev_cfg.disable) {
```

Yani etki zinciri şu:

| dosyada yazan | yüklenen | gerçek sonuç |
|---|---|---|
| `"use_raw_input": 0` | **`true`** | daemon `raw_input_enabled_ = true` → `:887`/`:1568` grab'ı **AÇIK** kalır → kullanıcı "her faremden ivmeyi kaldır" dedi, ivme **hâlâ uygulanıyor** |
| `"disable": 1` | **`false`** | `:911,1094,1350,1576` "skipping disabled device" **yolu hiç girmez** → kullanıcı "bu fareyi dokunma" dedi, ivme **ona da uygulanıyor** |

Her iki durumda da bayrak kullanıcının yazdığının **tam tersi** yükle
sabitleniyor, bir hata/uyarı yok, GUI anahtarı "doğru" değeri gösteriyor
(`gui/widgets_sync.inl:372` bağlı değeri okuyor) ve CLI durum çıktısı da
`cli/main.cpp:2120-2124` "Raw-input: on" diyecek. Bu, brifing §3'ün tarif ettiği
tam sınıf: *"bu kod çalışırken ne olduğunda fark edilir?"* → **fark edilmez**,
çünkü `config.cpp` içinde tek bir tanı bile yok (L05-05).

⭐ **Aynı koşul için projede zaten bir UYARI var — 1000 satır ötede.** CLI'nin
IPC wrapper okuyucusu aynı `is_boolean()` kontrolünü yapıyor ve **uyarıyor**:
```
$ sed -n '1768,1773p' cli/main.cpp
        if (wrapper_app.contains("use_raw_input")) {
            if (wrapper_app["use_raw_input"].is_boolean())
                cfg.use_raw_input = wrapper_app["use_raw_input"].get<bool>();
            else
                std::cerr << "Warning: ignoring non-boolean use_raw_input in wrapper\n";
        }
```
`cli/main.cpp:1766` ayrıca `active_profile` için de aynı uyarıyı veriyor. Yani
`config.cpp`'in sessizliği bir tasarım kararı **değil**, gözden kaçmış bir
asimetri: aynı hata sınıfı için üç yerde uyarı var (CLI), bir yerde
`is_boolean()` + uyarı var, `config.cpp`'de **sessiz var**.

⚠️ `parameter_index.md`'nin "dormant" hükmü bu lane'in referans belgesi olarak
kullanıldığı için ayrıca not düşüyorum: aynı belgede `disable` için de
"hiçbir kod yolu okumuyor" yazılı (`:58`, `:223`) — dört tüketici var. Bu,
docs lane'inin konusu; ben yalnız **kodun gerçekte ne yaptığını** ölçtüm.

---

### L05-02 | HIGH | `src/config.cpp:169-178`
**Kısmi `cap` dizisi: dosyada YAZILI olan değer sessizce atılıyor; `cap.y → 0` ise
cap'i tamamen kapatıyor (nötr varsayılan değil).**

```
$ cat .aihaberlesme/mesajlar/denetim-L05-olcum2.txt
### cap partial (corrected)
  {"cap":[500]}          -> cap={15,0}   (struct default is {15,0})   <-- 500 ATILDI
  {"cap":[null,2]}       -> cap={15,2}                                   <-- null -> 15
  {"cap":[24,"x"]}       -> cap={24,0}                                   <-- "x" -> 0
  {"cap":[]}             -> cap={15,0}
  {"cap":[500,3,99]}     -> cap={500,3}                                  <-- 99 sessizce düştü
  {"cap":["a","b"]}      -> cap={15,0}
```

`cap.size() >= 2` guard'ı (`:174`) boyut-1 diziyi **tamamen** reddediyor; eleman
type-guard'ları (`:175-176`) geçersiz elemanı **başka** bir elemana da dokunmadan
varsayılana bırakıyor. Kritik olan: `cap.y == 0` bir nötr varsayılan **değil**,
"cap YOK" sentineli (`include/accel-classic.hpp:33`: `cap.y > 0 ? max(0, cap.y-1)
: DBL_MAX`). Yani `"cap":[24,"x"]` bir yazım hatasını **kapalı cap'e** çeviriyor.

Ölçülen kazanç bedeli (aynı profil, tek fark `cap[1]`, 3000 ips, classic GAIN):
```
$ cat .aihaberlesme/mesajlar/denetim-L05-olcum3.txt
### (H10) `"cap":[24,"typo"]` -> cap.y silently 0 (= NO CAP)
  cap:[24,1.2]  (intended)     1.2        1.1998     1.00x
  cap:[24,"typo"] -> {24,0}    0          3.4495     2.88x  <-- SILENT

### (H11) `"cap":[500]` -> {15,0}
  cs2 intended cap {18,1.6}    {18,1.6}   1.5925     1.00x
  "cap":[500] -> {15,0}        {15,0}     13.0000    8.16x   <-- SILENT
```
ⓘ **Çapraz doğrulama (bağımsız):** 3.4495 ve 13.0000 sayıları bu depoda zaten
bağımsız olarak ölçülmüş ve `include/presets.hpp:56` (`cs2 … 13.0000`) ile
`include/presets.hpp:108` (`precision … **3.4495**`) içinde yazılı. Ölçüm
yöntemin aynı sonucu vermesi iki kanıtı birbirini doğruluyor.

---

### L05-03 | HIGH | `src/config.cpp:189-190` (+ yazan taraf `:96-102`)
**`lut_data` tek başına ( `lut_length` olmadan ) sessizce yok edilir ve bir sonraki
kayıtta kalıcılaşır.**

Okuyucu **iki** anahtarı birden şart koşuyor:
```
src/config.cpp:189-190
    if (j.contains("lut_data") && j["lut_data"].is_array() &&
        j.contains("lut_length")) {
```
Yazan taraf ikisini birlikte yazıyor (`:100-101`), dolayısıyla **normal** round-trip
sağlam. Ama yalnız diziyi üreten her araç için sessiz ve kalıcı kayıp var:

```
$ cat .aihaberlesme/mesajlar/denetim-L05-olcum.txt
### (H2) lut_data without lut_length: silent loss, made permanent by the next save
  file has lut_data=[10,20,30,40] (4 elements = 2 points) and NO lut_length key
  load_config -> accel_x.length = 0  data[0..3] = 0 0 0 0
  re-serialised JSON still contains lut_data? NO   <-- curve is GONE, no error, no warning
```

**Neden "kalıcı" olduğu ölçüldü:** `length=0` olduğu için yazan tarafın
`if (a.length > 0)` gate'i (`:96`) kapalı → bir sonraki `save_config` dosyayı
`lut_data` **içermeyen** halde yazar. Yani **tek** bir load→save turu eğriyi
sonsuza kadar siliyor, kurtarma yolu yok.

İlgili gözlem: depodaki LUT boyut denetimi (`check_import_lut_size`,
`src/config.cpp:1052-1054`) **yalnız `lut_data`** dizisine bakar — `lut_length`'e
bakmaz. Yani "LUT geçerli mi" sorusunu soran denetim ile onu *yükleyen* kod
farklı anahtarlara bakıyor.

---

### L05-04 | HIGH | `src/config.cpp:832` (`save_config`) ↔ kendi dokümanı `src/config.cpp:821-826`
**`MAX_PROFILES` kaydetme yolunda uygulanmıyor; doküman bu yolda yanlış iddia ediyor.**

Doküman iddiası (`:825-826`):
> "Every write path still refuses to exceed MAX_PROFILES, so this ceiling only
> bounds a pathological file."

`save_config` (`:832-1013`, 182 satır) içinde **hiçbir** profil sayısı kontrolü yok.
Ölçüm:

```
$ cat .aihaberlesme/mesajlar/denetim-L05-olcum.txt
### (H1) MAX_PROFILES enforcement on the SAVE path
  load_config(400-profile file) -> retained profiles = 400  (MAX_PROFILES=256, MAX_PROFILES_FILE=65536)
  save_config(...)  -> wrote profiles = 400
  => SAVE PATH DOES **NOT** ENFORCE MAX_PROFILES
  load+save 5000-profile file: retained=5000  written back=5000  size=12835111 bytes
```

Gerçekte bağlayan tavan `MAX_PROFILES_FILE = 65536` (`include/rawaccel-base.hpp:43`),
256 değil. `include/rawaccel-base.hpp:39-42` de aynı "refuses to go past
MAX_PROFILES" iddiasını tekrarlıyor. 65536 profil ≈ 168 MB'a ölçeklenir
(ölçülen 5000 profil = 12.8 MB). **MED yerine HIGH** sebebi: bu, `config.cpp`'nin
kendi yazdığı "veri kaybı düzeltmesinin" (`:691-693` "destructive truncation")
aynı sınıftaki ikinci yarısı — yazma yolu, okuma yolunun düzelttiği güvenlik
sözleşmesini bozuyor.

---

### L05-05 | HIGH | `src/config.cpp` (dosyanın tamamı)
**62 anahtarın 34'ü yanlış tipli değeri sessizce kabul ediyor; 62/62 eksik
alan varsayılana düşüyor; dosyada TEK BİR tanı/diagnostic yok.**

`app_config_to_json` ile yazan 62 anahtar yolunun **tamamı** için ölçüldü
(`.aihaberlesme/mesajlar/` — `fields.cpp` çıktısı, özet):

| sınıf | sayı | davranış |
|---|---|---|
| eksik (yok) → sessiz varsayılan | **62 / 62** | struct default (`rawaccel-base.hpp`) |
| yanlış tip → **sessiz kabul** | **34 / 62** | varsayılana düşer, hata yok |
| yanlış tip → **throw** | 28 / 62 | `config field 'X' must be a number, got string` |

Sessiz kabul eden 34 anahtarın tamamı (eksiksiz liste):
`version`, `active_profile`, `use_raw_input`, `profiles[].name`,
`profiles[].device_id`, `profiles[].match_app`, `profiles[].dpi`,
`profiles[].polling_rate`, `profiles[].disable`, `profile.name`,
`profile.raw_passthrough`, `profile.domain_weights`, `profile.range_weights`,
`profile.output_dpi`, `profile.yx_output_dpi_ratio`,
`profile.lr_output_dpi_ratio`, `profile.ud_output_dpi_ratio`,
`profile.degrees_rotation`, `profile.degrees_snap`, `profile.speed_min`,
`profile.speed_max`, `profile.speed_processor.whole`,
`profile.speed_processor.lp_norm`, üç `*_smooth_halflife`,
ve her iki eksende `gain`, `cap`, `lut_data`, `lut_length`  (4 × 2 = 8).
Throw eden 28: `mode`, `cap_mode` ve 12 sayısal `accel_args` alanı, her iki eksende
(14 × 2 = 28).

**"Eksik alan → varsayılan" dökümü (brifing madde 1'in istediği tam liste):**

| JSON anahtarı | yoksa ne olur | varsayılan |
|---|---|---|
| `version` | sessiz | `RAWACCEL_VERSION` = `1.2.5` (`config.cpp:1247`) |
| `active_profile` | sessiz | `"default"` |
| `use_raw_input` | sessiz | `true` |
| `profiles[].name` | sessiz | `""` |
| `profiles[].device_id` | sessiz | `""` = tüm fareler |
| `profiles[].match_app` | sessiz | `""` = her zaman uygula |
| `profiles[].dpi` | sessiz | `800` |
| `profiles[].polling_rate` | sessiz | `1000` |
| `profiles[].disable` | sessiz | `false` |
| `profile.name` | sessiz | `"default"` |
| `profile.raw_passthrough` | sessiz | `false` |
| `profile.domain_weights` | sessiz | `[1, 1]` |
| `profile.range_weights` | sessiz | `[1, 1]` |
| `profile.output_dpi` | sessiz | `1000` |
| `profile.yx_output_dpi_ratio` | sessiz | `1` |
| `profile.lr_output_dpi_ratio` | sessiz | `1` |
| `profile.ud_output_dpi_ratio` | sessiz | `1` |
| `profile.degrees_rotation` | sessiz | `0` |
| `profile.degrees_snap` | sessiz | `0` |
| `profile.speed_min` | sessiz | `0` |
| `profile.speed_max` | sessiz | `0` |
| `speed_processor.whole` | sessiz | `true` |
| `speed_processor.lp_norm` | sessiz | `2` |
| `speed_processor.input_speed_smooth_halflife` | sessiz | `0` |
| `speed_processor.scale_smooth_halflife` | sessiz | `0` |
| `speed_processor.output_speed_smooth_halflife` | sessiz | `0` |
| `accel_{x,y}.mode` | sessiz | `noaccel` (= ivme YOK) |
| `accel_{x,y}.gain` | sessiz | `true` |
| `accel_{x,y}.input_offset` | sessiz | `0` |
| `accel_{x,y}.output_offset` | sessiz | `0` |
| `accel_{x,y}.acceleration` | sessiz | `0.005` |
| `accel_{x,y}.decay_rate` | sessiz | `0.1` |
| `accel_{x,y}.gamma` | sessiz | `1` |
| `accel_{x,y}.motivity` | sessiz | `1.5` |
| `accel_{x,y}.exponent_classic` | sessiz | `2` |
| `accel_{x,y}.scale` | sessiz | `1` |
| `accel_{x,y}.exponent_power` | sessiz | `0.05` |
| `accel_{x,y}.limit` | sessiz | `1.5` |
| `accel_{x,y}.sync_speed` | sessiz | `5` |
| `accel_{x,y}.smooth` | sessiz | `0.5` |
| `accel_{x,y}.cap` | sessiz | `[15, 0]` |
| `accel_{x,y}.cap_mode` | sessiz | `out` |
| `accel_{x,y}.lut_data` | sessiz | yok (`length=0`) |
| `accel_{x,y}.lut_length` | sessiz | `0` |

ⓘ **Şema ile kanıt arasındaki boşluk (brifing madde 2):** `require_number`
(`:145-156`) **hata mesajına alan adını koyuyor** (`config field 'scale' must be a
number, got string`) — bu iyi ve doğru. Ama (a) **satır numarası hiçbir yerde
verilmiyor**; `json::parse` hataları nlohmann'ın kendi `"parse error at line 1,
column 39: syntax error while parsing object key"` metnini exception mesajı olarak
taşıyor, yani hatayı **JSON anahtarına** çeviren bir katman yok —
kullanıcı 300 profillik bir dosyada 39. sütundaki hatanın **hangi profilde**
olduğunu öğrenemiyor. (b) Throw eden 28 ile sessiz 34 arasındaki sınır
**keyfi**: `cap` iki sayı taşıyan bir dizi olduğu için `require_number`'ın dışında
kalmış, ama `mode` bir **string** ve `cap_mode` da string — yani tip denetimi
mantığı value'nun JSON tipine değil, alanın "hesap yapan sayı" olup olmadığına
bağlı. Sonuç: kullanıcı `cap`'i `"24"` yazdığında hata alır, `cap_x`'i
(aynı veri, farklı anahtar biçimi) yazarsa sessiz `15` alır.

**Tanı yok — pozitif kontrollü:**
```
$ grep -nE "cerr|printf|log|warn|stderr" src/config.cpp
10:#include <cerrno>
207:        // B5 (P43): ... (yorum)
693:///     lost entries with no warning ...   (yorum)
708:    // log and fall back WITHOUT touching the file).   (yorum)
736:    // with no warning.  Measured: ...                (yorum)
746:    // Throwing is safe for both callers: daemon.cpp:470-481 logs the reason  (yorum)
826:    // exceed MAX_PROFILES ...                          (yorum)
1039:/// dead code (widgets_sync.inl truncation warning) ...  (yorum)
  -> 8 satırın 7'si YORUM, 1'i #include. Çalışma zamanı tanısı: 0.

$ grep -cE "cerr|printf|log|warn|stderr" daemon/daemon.cpp cli/main.cpp
daemon/daemon.cpp:159
cli/main.cpp:197                                    <-- pozitif kontrol: desen çalışıyor
```

**Kısmi nesneler de aynı sınıf:**
```
$ cat .aihaberlesme/mesajlar/denetim-L05-olcum.txt
### (H5) speed_processor partial object
  {"whole":false} only -> whole=0 lp_norm=2 ih=0 sh=0 oh=0  (all others silently defaulted)
  {"lp_norm":7} only   -> whole=1 lp_norm=7  (whole silently defaulted to TRUE)

### (H6) accel_x / profile present but wrong container type
  accel_x:"oops" -> SILENT: mode=6 len=0  (whole accel block replaced by defaults, no error)
  profile:42 -> SILENT: inner name='default' all defaults
```
`accel_x` bir **string** olsa bile (`:303` `is_object()` guard'ı) blok sessizce
tamamen varsayılana düşüyor. `profile:42` de öyle.

**Alan dışı değerler sessizce kırpılıyor** (ölçüldü, `denetim-L05-olcum.txt` H9):
```
  dpi=0            polling_rate=0            -> dpi=1  polling_rate=125
  dpi=99999        polling_rate=99999        -> dpi=32000 polling_rate=8000
  dpi=2147483648   polling_rate=2147483648   -> dpi=32000 polling_rate=8000
```
ve `accel_args` tarafında (`denetim-L05-olcum3.txt` / H-C ölçümü):
`cap_y 1000→100`, `cap_x 5000→500`, `scale 1e6→100`, `exponent_power 1e9→5`,
`limit 1e6→100`, `acceleration 999→20`, `decay_rate 1000→10`, `smooth 16→1`,
`sync_speed 500→100`, `motivity -5→0`, `exponent_classic 0.5→1`, `scale 0→0.01`,
`sync_speed 0→0.0001`. Bunlar **bilinçli** (GUI gauge tavanı, `config.hpp:11-64`)
ve dokümante — bulgu değil, **bağlam**.

---

### L05-06 | MED | `src/config.cpp:792-795` (+ `:1217`, `:1240`)
**Sürüm alanı verbatim korunuyor ama bilinmeyen anahtarlar atılıyor → dosya kendi şeması hakkında yalan söylüyor ve bu, tek kurtarma mekanizmasını kalıcı olarak kilitler.**

```
$ cat .aihaberlesme/mesajlar/denetim-L05-olcum.txt
### (H3) newer stored version
  stored version = "9.9.9" (RAWACCEL_VERSION=1.2.5)
  after load:  cfg.version = "9.9.9"
  after save:  file version = "9.9.9"   active_profile = "FUTURE"  accel_x.acceleration = 0.004
  unknown keys kept? top='NO' in-profile='NO' in-accel='NO'
  -> VERSION LIES: file claims 9.9.9 but contains 1.2.5-schema data (unknown keys gone)
```

CFG-5 yorumu (`:785-791`) "unknown keys are still dropped" diye **bunu biliyor**;
bildirmediği sonuç şu: `migrate_config` `:1217`'de `version == RAWACCEL_VERSION`
üzerinde kısa devre yapıyor, aksi halde yalnızca `version_lt(...)` doğruysa
migrate ediyor (`:1233`, `:1240`). `9.9.9` kalıcı damgalanmış bir dosya **bir daha
asla migrate edilmez**. Dolayısıyla CFG-5'in ileri-veri kaybını azalttığı iddiası
(yalnız sürüm satırı düşmesin diye) **bu kombinasyonda tersine işliyor**: sürüm
satırını korumak, düşen verinin geri dönüş yolunu kapatıyor. Ya sürüm de
düşmeliydi (veri kaybı görünür olurdu) ya da bilinmeyen anahtarlar korunmalıydı.

---

### L05-07 | MED | `tests/test_accel.cpp:2582` (`test_atomic_write`) + `:5082` (`test_save_config_durability_path`)
**Atomiklik adını taşıyan testler atomikliği test ETMİYOR — mutasyonla kanıtlandı.**

`test_atomic_write` (`:2583`) section adı: *"atomic config write — no .tmp file left
after successful save"*. Yapısal olarak tek iddiası:
```
tests/test_accel.cpp:2598
    EXPECT(tmp_leftover_count(atomic_path) == 0); // tmp files gone (renamed to final)
```
/tmp dosyası hiç oluşmadığında bu **totolojik olarak** doğrudur.

**Mutasyon M1** (yalnız `/tmp/opencode/L05/mut/` kopyasında; kanıt aşağıda):
- `src/config.cpp:867` → `tmp_path = path` (tmp == hedef)
- `:901` `O_EXCL` → `O_TRUNC` + `:902-909` retry bloğu silindi
- `:989` `fs::rename(tmp_path, path, ec)` → `if (false) ...` (yayınlama adımı YOK)
Sonuç: `save_config` artık hedef dosyayı **doğrudan kırpıp** yazıyor, yani
atomik **değil**.

```
$ md5sum /home/a/Masaüstü/AbrekMouse-main/src/config.cpp /tmp/opencode/L05/mut/src/config.cpp
a399dea352e91bf5fe744dbcad98f243  /home/a/Masaüstü/AbrekMouse-main/src/config.cpp   (değişmedi)
17c4390b8e11388be734215c5ad95cb0  /tmp/opencode/L05/mut/src/config.cpp              (mutant)

$ cat .aihaberlesme/mesajlar/denetim-L05-mutfiltered.log     (BUILD_RC=0, derlendi)
[BUG-22 — save_config supports bare relative filenames]              5 PASS
[atomic config write — no .tmp file left after successful save]      3 PASS
[P54-B3 — save_config tmp is pid-suffixed, atomic, no .tmp left]     3 PASS
[BUG-13 — save_config: tmp file is removed and target updated atomically]  5 PASS
[K4 — save_config: permission bits are preserved, umask-independent] 6 PASS
=== Sonuç: 22/22 geçti (5 section eşleşti, 212 atlandı ===
FILT_RC=0

$ grep -E "Sonuç|FULL_RC|FAIL " .aihaberlesme/mesajlar/denetim-L05-mutfull.log
  FAIL  tests/test_accel.cpp:8915  bak_bytes == first_bytes  (section: P99-C — .bak rotate + simulated write failure restores previous)
  FAIL  tests/test_accel.cpp:8924  threw  (section: P99-C — .bak rotate + simulated write failure restores previous)
=== Sonuç: 34162/34164 geçti, 2 BAŞARISIZ ===
FULL_RC=1
```

**Doğru okuma şart:** tam suite M1'de **kırmızı** oluyor (2/34164), yani kapı
tamamen ölü değil. Ama **tek yakalayan test `P99-C`** (`:8888`) — adı "atomic"
değil, `.bak rotate + simulated write failure`; ve M1'de o da yanlış sebepten
kırılıyor (`.bak` üretimi de aynı koddan geçtiği için `bak_bytes == first_bytes`
başarısız). **Pozitif kontrol:** değiştirilmemiş ağaç aynı derleme komutuyla
`34164/34164, rc=0` (`denetim-L05-basefull.log`, `BASE_FULL_RC=0`).

⭐ Bu bulgunun en dürüst ifadesi şudur: **atomiklik kodu gerçekten atomik**
(yazma → `fsync` → hard-link `.bak` → `rename` → parent `fsync`, hepsi ölçülüp
okunmuş satırlar). Bulgu **eksik test kapsamı**, **kırık kod** değil. `CRIT`
değil `MED` sebebi de bu: mümkün olan en kötü sonuç (yarım dosya) gerçekleşmiyor,
sadece kapı onu doğrulamıyor.

ⓘ **Yan halka — `test_save_config_relative_path` ölü temizlik:**
`tests/test_accel.cpp:1847` `std::remove("settings.json.tmp")` çağırıyor, ama
`src/config.cpp:867` artık pid-sonekli ad üretiyor:
```
$ grep -n 'path + "\.tmp"\|+ "\.tmp"' src/config.cpp
861:    // B3 (P43): deterministic `path + ".tmp"` ...   <-- yorum
867:    std::string tmp_path = path + "." + std::to_string(::getpid()) + ".tmp";
```
Çıplak `<path>.tmp` **hiç üretilmiyor** → `:1847` her zaman başarısız bir `remove`.
Doğru temizlik `remove_tmp_leftovers()` yardımcısı (`:2567`), ama o section
kullanmıyor. Etkisi: BUG-22 section'ı geçtiğinde gerçekte hiçbir şey temizlenmiyor.

---

### L05-08 | MED | `src/config.cpp:994-995`, `:971-972`, `:979-980`
**Üç `std::error_code` yazılıyor, HİÇ OKUNMUYOR (brifing madde 6).**

`$script:` deseni bu projede **yok** — ölçtüm, uydurmuyorum:
```
$ grep -rn '\$script:' src/config.cpp include/config.hpp include/presets.hpp
(çıktı yok, rc=1)
```
Bu yüzden brifing'in istediği sayaç, PowerShell tarzı bayraklar yerine
`std::error_code` / `bool` / `errno` değişkenleri için yapıldı.

| değişken | toplam geçiş | **okuma sayısı** | karar |
|---|---|---|---|
| `ec_rm` (`:994-995`) | 2 | **0** | ⛔ yazılıp hiç okunmuyor |
| `ec_remove` (`:971-972`) | 2 | **0** | ⛔ yazılıp hiç okunmuyor |
| `ec_remove` (`:979-980`) | 2 | **0** | ⛔ yazılıp hiç okunmuyor |
| `ec_bak` | 9 | 5 | okunuyor (`:964,968,973,976,978`) |
| `ec_canon` | 3 | 1 | okunuyor (`:844`) |
| `ec` | 4 | 2 | okunuyor (`:990,997`) |
| `bool known` | 3 | 1 | okunuyor (`:123`) |
| `bool migrated` | 7 | 2 | okunuyor (`:1246,1250`) |
| `int err` | 4 | 2 | okunuyor (`:939,947`) |
| `size_t left` | 4 | 2 | okunuyor (`:932,941`) |

```
$ grep -n "\bec_rm\b" src/config.cpp
994:        std::error_code ec_rm;
995:        fs::remove(tmp_path, ec_rm);        <-- sonra hiç geçmiyor
```

**Ölçülen sonuç değil, izlenen sonuç (ayrım önemli):** `fs::remove(tmp_path,
ec_rm)` (`:995`) **başarısız olursa** `path.<pid>.tmp` yetim kalır. Aynı pid'li
bir sonraki kayıt `:902`'de `EEXIST` görüp `:906-908`'de unlink+retry ile
kendini onarır; **farklı** pid'li bir kayıt onarmaz ve yetim dosya config
dizininde kalır. `test_atomic_write`'in `tmp_leftover_count` (`:2550`) çağrısı
her zaman temiz bir dizinde çalıştığı için bunu göremez.
`ec_remove` (`:972`, `:980`) aynı sınıf — `bak_tmp_path` temizliği.
Ayrıca not: `test_accel.cpp:2560`'ta `tmp_leftover_count` için `>= stem.size() + 4`
kullanılıyor; gerçek son ek `.<pid>.tmp` en az 5 karakter (`1.tmp`), yani asgari
sınır sağlanıyor — bu kısım doğru.

---

### L05-09 | MED | `src/config.cpp:266-276` ↔ `:344-355`
**`profiles[].name` ve `profiles[].profile.name` iki bağımsız alan; hiçbir yerde uzlaştırılmıyor ve her kayıtta ayrık hâliyle kalıcılaştırılıyor.**

Yazan taraf ikisini **ayrı ayrı** yazıyor (`:346` `j["name"] = dp.name` ve `:236`
`j["name"] = std::string(p.name)`), okuyan taraf da ayrı ayrı okuyor (`:652` ve
`:266-276`). Ölçüm:
```
  hand-edited divergence        outer='OUTER' inner='INNER'
  after save+load               outer='OUTER' inner='INNER'
  only inner renamed in file    outer='OUTER' inner='INNER2'
  after save+load (inner only)  outer='OUTER' inner='INNER2'
```
Dış ad CLI'ın hedeflediği ve GUI'ın listelediği addır (`parameter_index.md:54,60`);
iç ad `profile.name`'dir. Ayrışma fark edilmiyor, raporlanmıyor, ve **her turda
kaydedilerek kalıcılaşıyor**. Uzunluk tavanları ölçüldü ve sabit: ad 256,
`device_id` 256, `match_app` 128 — round-trip sonrası aynı (`:651-659`).

---

### L05-10 | MED | `include/presets.hpp` (8 presetin tamamı)
**8/8 preset X≡Y; 8'in 7'si kendi modunun hiç okumadığı alanları JSON'a yazıyor ve
GUI bunları kullanıcıya gösteriyor.**

Preset sayısı ve listesi (`presets.hpp:10-14`) ölçüldü:
`PRESET_COUNT = 8` — `gaming, office, precision, disable, cs2, valorant, apex, fps`.
Hepsi için `accel_x == accel_y` (`accel_args::operator==`, `rawaccel-base.hpp:94`)
→ **8/8 preset tamamen eksen-simetrik.** Bu, `apex` yorumunun
"tracking-heavy + **verticality**" vaadini (`presets.hpp:162-163`) ve diğer
presetlerin dikey niyetini kodda karşılamıyor.

Pozitif kontrollü okuyucu haritası:
```
$ grep -l "args\.$f\|\.$f\b" include/accel-{classic,natural,power,jump,synchronous,lookup,noaccel}.hpp
  acceleration       read by: include/accel-classic.hpp
  exponent_classic   read by: include/accel-classic.hpp
  exponent_power     read by: include/accel-power.hpp
  limit              read by: include/accel-natural.hpp
  decay_rate         read by: include/accel-natural.hpp
  motivity           read by: include/accel-synchronous.hpp
  gamma              read by: include/accel-synchronous.hpp
  scale              read by: include/accel-power.hpp
  sync_speed         read by: include/accel-synchronous.hpp
  smooth             read by: include/accel-jump.hpp include/accel-synchronous.hpp
  output_offset      read by: include/accel-power.hpp
  input_offset       read by: include/accel-classic.hpp include/accel-natural.hpp
```
(Pozitif kontrol: `motivity`/`gamma`/`sync_speed` 0 değil — `accel-synchronous.hpp`
bulundu; hiçbiri `accel-noaccel.hpp` çıkmadı, yani "0" olan yerler gerçek 0.)

Preset başına **ölçülen** atıl alanlar:
```
$ cat .aihaberlesme/mesajlar/denetim-L05-olcum3.txt   (H12)
  gaming     classic   limit, decay_rate, motivity, gamma, exponent_power, scale, sync_speed, smooth, output_offset
  office     natural   acceleration, exponent_classic, exponent_power, scale, gamma, sync_speed, smooth, output_offset, cap
  precision  classic   limit, decay_rate, motivity, gamma, exponent_power, scale, sync_speed, smooth, output_offset
  cs2        classic   limit, decay_rate, motivity, gamma, exponent_power, scale, sync_speed, smooth, output_offset
  valorant   natural   acceleration, exponent_classic, exponent_power, scale, gamma, sync_speed, smooth, output_offset, cap
  apex       power     acceleration, exponent_classic, limit, decay_rate, motivity, gamma, sync_speed, smooth, input_offset
  fps        classic   limit, decay_rate, motivity, gamma, exponent_power, scale, sync_speed, smooth, output_offset
```
Preset **adı** ile içeriğin örtüşmemesi (brifing madde 3'ün asıl sorusu), somut
üç örnek:
- **`office`** (`natural`) `motivity = 1.2` yazıyor — `motivity`'yi yalnız
  `accel-synchronous.hpp` okuyor. Doğal modda **ölü**.
- **`valorant`** (`natural`) `motivity = 1.2` **ve** `cap = {30, 2.0}` yazıyor —
  ikisi de natural'da ölü (`natural`'in asimptotu `limit`; `cap`'i classic/power/jump
  okuyor). Yani presetin en "belirgin" ayarı etkisiz.
- **`apex`** (`power`) `input_offset = 0.02` yazıyor — `input_offset`'i yalnız
  classic+natural okuyor. power'da ölü.
- `gaming`/`precision`/`cs2`/`fps` (`classic`) `limit` yazıyor — yalnız natural
  okuyor. Bunu `presets.hpp:194-195` **zaten** yazıyor ("gaming/precision/cs2/fps'in
  limit değeri de bildirimsel"), yani bulgu yeni değil; `office`/`valorant`/
  `apex` için aynı analiz **yok**.

⭐ Ad-etkisizlik kalıcılaşıyor: `presets.hpp:193` kendi yorumunda
`gui/profile_mgr.inl:98`'in `tr("limit")` ile bu sayıyı **kullanıcıya gösterdiğini**
söylüyor. Yani kullanıcı ekranda görüp düzenleyebildiği bir sayı, hesaba hiç
girmiyor.

---

### L05-11 | LOW | `include/presets.hpp:10-14` ↔ `:115`
**`make_preset` kabul ettiği ad kümesi `PRESET_NAMES`'ten geniş — tek-kaynak
iddiası liste için doğru, kabul edilen küme için değil.**

`PRESET_COUNT = 8`; `make_preset` ayrıca `"none"` ve `"off"` takma adlarını da
kabul ediyor (`:115`). Ölçüldü:
```
  make_preset('none') -> mode_x=noaccel raw=1     (yani 'disable' ile aynı)
  make_preset('off')  -> mode_x=noaccel raw=1
  make_preset('CS2')  -> name '' (bilinmeyen sinyali)   <-- büyük/küçük harf DUYARLI
  make_preset('Gaming')-> name ''
  make_preset('val')  -> name ''
  make_preset('default') -> name ''
```
GUI açılır listesi 8 kalem (`presets.hpp:10-13`), CLI 10 ad yazabiliyor. Bilinçli
tasarım (bilinmeyen preset `dp.name`'i temizler), ama "preset sayısı" bir
yüzeyde 8, diğerinde 10.

---

### L05-12 | LOW | `src/config.cpp:843` (ve `test_accel.cpp:1812`)
**Brifing madde 7'nin varsayımı bu şemada TEMSİL EDİLEMEZ: göreli LUT yolu
yoktur. LUT JSON içine gömülü bir dizidir. Ölçülen asıl davranış: göreli yol
çalışma dizinine göre çözülüyor.**

```
$ grep -n "relative\|absolute\|current_path\|getcwd\|weakly_canonical" \
      src/config.cpp include/config.hpp include/presets.hpp
(çıktı YOK, rc=1)
$ grep -rn "lut_data\|lut_file\|lut_path\|\.csv" --include=*.cpp --include=*.hpp --include=*.inl \
      src/ cli/ gui/ daemon/
src/config.cpp:89,221,223,371,403,613,614,1052,1054   cli/main.cpp:1669,1671,1688,1689   gui/profile_mgr.inl:597
  -> hepsi `lut_data` DİZİSİ; hiçbiri dosya yolu değil.
```
Yani "config'i kaydet ve taşı → göreli LUT yolu bozulur" senaryosu **bu şemada
mümkün değil** (taşınabilirlik sorunu yok, çünkü referans yok).

Ölçülebilir olan, `save_config`'in **kendi** `path` argümanı:
`fs::canonical(arg_path, ec_canon)` (`:843`) yol **varsa** mutlaklaştırıyor, yoksa
ham göreli dize kalıyor (`:844`) — her iki durumda da referans noktası **sürecin
CWD'si**, hiçbir sabit yer değil. Ölçüm:
```
  saved in A: settings.json
  saved in B: settings.json  (aynı göreli ad, farklı CWD)
  A/settings.json profile0.name='fromA'
  B/settings.json profile0.name='fromB'      <-- iki ayrı dosya, ikisi de geçerli, hata yok
```
`test_save_config_relative_path` (`:1812`, section "BUG-22") yalnız `chdir` ettiği
dizine dosya düştüğünü doğruluyor; CWD bağımlılığını **tespit edemez** (aynı
sebeple L05-07'deki mutasyon bu testi geçirdi).

---

## ⭐ ROUND-TRIP ÖLÇÜMÜ (brifing madde 5) — KAYIP YOK

Bu lane'in **olumlu** sonucu, kanıtla:

| ölçüm | sonuç |
|---|---|
| 8/8 preset: `save → load → save` JSON farkı | **0 anahtar farklı, 0 anahtar kayıp** |
| 8/8 preset: 2. tur → 3. tur (sabit nokta) | **0 fark, 0 kayıp** |
| 41 alanın tamamı ayrışık değerle (maks. profil: her iki eksen, LUT, `cap_mode=io`, `disable=true`, `use_raw_input=false`, `match_app`, `device_id`) | **0 fark, 0 kayıp** |
| aynı profil 2. tur → 3. tur | **0 fark, 0 kayıp** (sabit nokta) |
| yazan tarafın ürettiği anahtar yolu sayısı | **62** (maks. durum) |
| yazan→okunan 41 değer hayatta kalma kontrolü | **41/41 geçti** (tek bir MISMATCH satırı yok) |
| uzunluk tavanları (ad 256 / `device_id` 256 / `match_app` 128) round-trip sonrası | **değişmedi** |

Kapsam notu (dürüstlük): round-trip **yazan tarafın ürettiği** alanlar için
sabit noktadır. **Yazılmayan** alanlar (bilinmeyen anahtarlar) round-trip'in
konusu değil — onlar L05-06'da ölçüldü ve **kayboluyor**. Ayrıca `lut_data`'nın
`lut_length` olmadan gelmesi round-trip'i değil **parse'ı** bozuyor (L05-03).

ⓘ `lut_data` iki eksende de yazılıyor — ayrıca ölçüldü (pozitif kontrol):
aks-x ve aks-y'nin ikisi de LUT taşıyan bir profilde üretilen JSON'da
`"lut_data"` **2 kez** geçiyor. `accel_args_to_json` (`:96`) bir mode kapısı
taşımıyor, bu yüzden lookup→diğer→lookup geçişi eğriyi koruyor (yorum `:87-95`
bunu doğru belgeliyor).

---

## KAPI

```
$ md5sum /home/a/Masaüstü/AbrekMouse-main/src/config.cpp
a399dea352e91bf5fe744dbcad98f243
   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ çalışma ağacındaki config.cpp MUTASYONSUZ
     (denetim başı ve sonu aynı md5; hiçbir kaynak dosya yazılmadı)

1) ÖLÇÜM KOŞUMLARI (hepsi /tmp/opencode/L05/ kopyası üzerinde, rc=0):
   $ ./presets    -> 8 presetin tamamı + 8 alias denemesi          (rc=0)
   $ ./fields     -> 62 anahtar x {eksik, yanlış-tip} matrisi      (rc=0)
   $ ./fin        -> H1..H9 ölçümleri                             (rc=0)
   $ ./h78        -> bool/kapı-kısmi ölçümleri (ayrı ikili)        (rc=0)
   $ ./capgain    -> H10..H12 kazanç bedeli                       (rc=0)

2) MUTASYON KAPI (M1: atomik tmp+rename YOK):
   $ g++ -std=c++20 -O1 -Iinclude -Isrc tests/test_accel.cpp src/config.cpp \
       src/logitech_receiver.cpp src/logitech_hidpp.cpp -o ta_mut
   BUILD_RC=0
   $ ./ta_mut --filter 'atomic|BUG-13|P54-B3|BUG-22|save_config'
   === Sonuç: 22/22 geçti (5 section eşleşti, 212 atlandı) ===   FILT_RC=0
   $ ./ta_mut
   === Sonuç: 34162/34164 geçti, 2 BAŞARISIZ ===                FULL_RC=1

   ⭐ POZİTİF KONTROL (aynı test_accel.cpp, aynı bayraklar, TEK fark config.cpp
     yolunun ağaçtaki pristine kopyası olması):
   $ g++ ... /home/a/Masaüstü/AbrekMouse-main/src/config.cpp ... -o ta_base2
   BASE_BUILD_RC=0
   $ ./ta_base2
   === Sonuç: 34164/34164 geçti ===                              BASE_FULL_RC=0
   $ ./ta_base2 --filter 'atomic|BUG-13|P54-B3|BUG-22|save_config'
   === Sonuç: 22/22 geçti (5 section eşleşti, 212 atlandı) ===    BASE_FILT_RC=0
   → 22/22'lik sonuç mutant'a ÖZGÜ değil; ama 22/22'yi mutant da verdiği için
     "bu kapı atomikliği ölçmüyor" hükmü mutant'a özgüdür ve ölçülmüştür.
     Ham çıktı: denetim-L05-basebuild.log / -basefull.log / -basefiltered.log

ÜRETİLEN SAYILAR:
   62 anahtar yolu incelendi · 62/62 eksik-alan sessiz varsayılan
   34/62 yanlış tip sessiz · 28/62 throw
   8/8 preset round-trip sabit nokta (0 fark, 0 kayıp)
   41/41 alan hayatta kalma kontrolü geçti
   3 yazılıp-hiç-okunmayan error_code (ec_rm ×1, ec_remove ×2 kapsam)
   2 derleme (mutant + baseline), 5 gate koşumu
```

---

## KAPSANMAYAN (lane dışı kaldı, birinin bakması gereken)

- **`daemon.cpp:470-481`, `:1866-1870`, `:677-681`** — `load_config`/`save_config`
  çağrılarının try/catch ve "rejected config stays intact on disk" iddiası.
  `config.hpp:131-134` ve `:164-171` 7/7 ve 5/5 çağrı noktasının try/catch içinde
  olduğunu iddia ediyor; ben bu iddiayı **doğrulamadım** (başka lane'in dosyası).
  Özellikle L05-01/L05-02 sessiz kabul olduğu için "catch" hiç devreye girmiyor.
- **`gui/main.cpp:184-186`** — `.corrupt-<unixtime>` zarfı. L05-01/02/03/05'teki
  sessiz durumlar bu zarfı **tetiklemez** (throw etmedikleri için), yani GUI'de
  kurtarma yolu da yok. Doğrulanmadı.
- **`docs/research/parameter_index.md:19,58,222,223`** — "dormant / daemon
  okumuyor" hükümleri **ölçümle çürütüldü** (`use_raw_input` → `daemon.cpp:484,
  887, 1568`; `disable` → `daemon.cpp:911, 1094, 1350, 1576`). Bu lane'in
  referans belgesi olduğu için ayrıca vurgulanıyor; docs lane'inin konusu, ben
  yalnız çelişkiyi belgeledim.
- **`cli/main.cpp:1688-1689`** — CLI'nin kendi LUT boyut denetimi (yalnız
  `lut_data`'ya bakıyor). L05-03 ile ilişkili ama CLI lane'inin.
- **`find_config_path()` (`src/config.cpp:1083-1124`)** — `XDG_CONFIG_HOME` /
  `SUDO_USER` / `HOME` önceliği ve `getpwnam_r` ERANGE yeniden denemesi. Kod olarak
  okudum, **çalışma zamanı ölçümü yapmadım** (ortam bağımlı).
- **GUI gauge ↔ sanitize tavan eşleşmesi** — `config.hpp:11-64` 7 alan için
  "gauge tavanı == R15 sınır değeri" diyor. `olcum/arsiv/gauge_scan.py`'ya
  erişemedim, `gui/ui_builder.inl` başka lane'in. **Doğrulamadım.**
- **`MAX_PROFILES` tavanının CLI/GUI tarafı** — L05-04 `src/config.cpp` içinde
  ölçüldü; "`every write path`" iddiasının CLI/GUI kolları ölçülmedi.
- **`config/default.json`** — `parameter_index.md:156-188` ile çapraz kontrol
  yapmadım.

---

## TEMSİL SINIRI

1. **`load_config`/`save_config` çağıran 12 dış nokta denetlenmedi.** Sadece
   `src/config.cpp`'nin kendi davranışını ölçtüm. "Kullanıcı bu hatayı
   görebiliyor mu" sorusunun **yarısı** cevaplanmamıştır — çünkü cevap çağrı
   zincirinde ve çağrı zinciri başka lane'lerde.
2. **Eşzamanlı okuyucu yarışı ölçülmedi.** Atomikliğin *doğrudan* kanıtı,
   mutasyon testidir (L05-07); canlı "okuyucu hiç yarım dosya görmüyor" ölçümünü
   kurmaya çalıştım (`race.cpp`: 300 profil / 816.601 bayt, fork'lu okuyucu,
   `nlohmann::json::parse` ile doğrulama) ancak `/tmp/opencode` iki kez
   silindiği için çalıştıramadım. **Bu ölçüm YAPILMADI** — L05-07'deki "atomik
   olmayan mutantta okuyucu yarım dosya görür" ifadesi bir **çıkarımdır**,
   ölçülmüş değildir. Doğrulayan şey mutantın 22/22 geçmesidir, ki bu ölçülmüştür.
3. **Yalnız Linux/x86-64, `RAWACCEL_PORTABLE=1` değil, `-O1` ile derlendi.**
   Yedili kapı koşumları ağacın kendi `src/` kopyasıyla (`ta_base`) ve
   `mut/src` ile yapıldı. `-march=native` / ASan / UBSan **koşulmadı**; bu lane
   ölçümleri yapısal (JSON + dosya I/O), aritmetiksel değil — ama bu bir
   varsayımdır, doğrulanmış değildir.
4. **`tests/run_tests.sh` yedili kapı olarak KOŞULMADI** (ağaçta çalıştırmak
   diğer ajanların kanıtını riske atar; brifing §1 yalnız build'e izin veriyor).
   Test ikilisini `/tmp` kopyasında derleyip çalıştırdım. Dolayısıyla "bu testler
   yeşilken mutant kırmızı oluyor" iddiam **bu ikili** için geçerli; projenin
   kendi `run_tests.sh` sarmalayıcısı (SIMD parity, tracker bridge, TR coverage
   bölümleri dahil) mutantta koşulmadı.
5. **Kaybedilen verinin "kalıcı" olduğu iki yerde kod-izlemesiyle desteklendi,
   dosya üzerinde kanıtlanmadı:** L05-03'te `save_config` sonrası dosyanın
   `lut_data` içermediği **ölçüldü** (`re-serialised JSON still contains lut_data? NO`),
   ama "üçüncü turda da geri gelmez" turu koşulmadı. L05-04'te 12.835.111 bayt
   ölçüldü, 65536 profille ~168 MB **tahmindir** (doğrusal ölçek varsayımı).
6. **`$script:` sayacı yapılamadı** — desen bu projede yok (rc=1, ham çıktı
   yukarıda). Bunun yerine `std::error_code`/`bool`/`errno` sayacı yapıldı;
   bu, brifing'in istediği "yazma ve okuma sayısı" ölçümünün aynı amacını
   gerçekten karşılıyor, ama talep eden birebir kalem değil.
7. **`presets.hpp` yorumlarındaki sayısal iddiaları (3.4495, 13.0000, 8.89x,
   %46.7 sapma) yeniden üretmedim** — bunlar başka ajanların ölçümleri.
   Bağımsız olarak yalnız **2.88x** (L05-02/H10) ve **8.16x** (L05-02/H11)
   kazanç çarpanlarını kendi hâlimle ölçtüm; bunlar `presets.hpp`'in kendi
   ölçtüğü mutlak kazançlarla (3.4495 / 13.0000) çakışıyor, yani
   çapraz doğrulama var, ama "8.89x" çarpanının kaynağını doğrulamadım.
8. **Hiçbir düzeltme yapılmadı** (brifing §1). Bu rapor teşhis içindir; L05-01
   (`use_raw_input`/`disable` sayısal 0/1) tek satırlık bir okuyucu genişletmesiyle,
   L05-07 tek bir mutasyon testiyle kapatılabilir — ama **karar AJ1'inkidir**.
9. **`use_raw_input`/`disable` tüketicilerinin davranışını çalıştırarak
   doğrulamadım** — yalnız `grep` ile tüketici satırlarını saydım ve okudum
   (`daemon.cpp:484/887/911/1094/1350/1430/1568/1576`,
   `gui/widgets_sync.inl:372/614`, `cli/main.cpp:2120-2124`). Bunlar **başka
   lane'lerin dosyaları** ve root + `/dev/uinput` gerektiriyor. L05-01'in
   "kullanıcının yazdığının tersi yükleniyor" kısmı **bu lane'de ölçüldü**
   (`config.cpp` çıktısı); "bunun şu sonucu doğurur" kısmı **kod okumasıdır**,
   ölçülmüş değildir. Bu ayrım bilinçlidir.
10. **L05-01'in CRIT sınıfı, "hata mesajı yok" gerekçesine dayanıyor; "yanlış
    değer yükleniyor" gerekçesine değil.** Ölçülen kısım
    `use_raw_input: 0 → true`, `disable: 1 → false`'dir (belgeli çıktı). Bunların
    canlı birer daemon anahtarı olduğu `grep` ile sayıldı. Aradaki nedensellik
    zinciri (`raw_input_enabled_` → grab açık/kapalı) kod okumasıdır; bir ajan
    `daemon` lane'inden bu zinciri koşturarak doğrulamalıdır.
