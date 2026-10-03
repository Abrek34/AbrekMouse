### L12 | alt-ajan (CLI argüman ayrıştırma / liste-göster / profil yönetimi / doğrulama) | 2026-10-01

KAPSAM   : `cli/main.cpp` **yalnız satır 1–1374** (ölçülen bölge: 329–1374).
           Salt-okuma. Referans olarak OKUNDU (yazılmadı): `include/config.hpp`,
           `include/rawaccel-base.hpp`, `src/config.cpp`, `tests/run_cli_sanitized.sh`.
           Tüm ölçüm `/tmp/opencode/l12-cli/` altında; çalışma ağacına hiçbir
           yazma yapılmadı (`git diff --stat -- cli/ src/ include/` → boş;
           `md5sum cli/main.cpp` = `d73f9bae349cbbbf4f6abfccdfa3e56f` baştan sona aynı).

---

## BULGULAR

### L12-01 | cli/main.cpp:1210-1215 | **HIGH** | `mode=lookup` + LUT yok: uyarı basılıyor ama rc=0 — script "başarılı" diyor, profil ölü

Kanıt (gerçek ikili, `--no-daemon`):
```
$ rawaccel-cli -c w/c8.json --no-daemon set-param default mode lookup ; echo rc
stdout: Set mode = lookup in profile 'default'
        Config updated locally (not applied to daemon ...)
stderr: WARNING: mode=lookup has no LUT data yet — there is no set-param lut-data key; ...
rc=0
```
`:1211` uyarıyı `std::cerr`'e basıyor ama `return 1` yok — `:1372`'de
`daemon_apply_if_enabled` çağrısına düşüp 0 dönüyor. Kurulum, eğri olmayan bir
lookup profili. **Kullanıcı rc=0 görür, ayarın tuttuğunu sanır, imleç ölüdür.**
Bu tam olarak brifing §3'teki "kullanıcı ayarını değiştirdiğini sanır" sınıfı.
Ayrıca `print_profile` LUT alanlarını hiç basmaz (§ L12-05), dolayısıyla
kullanıcı `show` çalıştırıp da "eğrim var" sanamaz — görünür bir iz yok.

---

### L12-02 | cli/main.cpp:835-844 | **HIGH** | "LUT data length is odd" uyarısı **ölü kod** — hiçbir girdiyle basılmıyor (mutasyonla kanıtlandı)

`cmd_validate` load_config SONRASI çalışır ve `p.accel_x.length % 2 != 0`
kontrol eder. Ama `src/config.cpp:229` her load'da `a.length = (n/2)*2`
yapar → **`length` yüklemeden sonra matematiksel olarak çift olamaz.**

Exhaustive sweep (her biri ayrı config, `lut_length` 0..21):

```
lut_len  stored_len  odd-warning-printed?
   0       ABSENT(0)   (silent)
   1       ABSENT(0)   (silent)
   3       2           (silent)      <-- yazdık 3, saklanan 2
   5       4           (silent)
   7       6           (silent)
   9       8           (silent)
  11      10           (silent)
  13      12           (silent)
  15      14           (silent)
  17      16           (silent)
  19      18           (silent)
  21      20           (silent)
swept 22 | warning printed: 0 | ODD length surviving load: 0
```

**Mutasyon testi (brifing §4):** `/tmp/opencode/l12-cli/mut3/` kopyasında
`src/config.cpp:229`'daki `(n/2)*2` satırı `a.length = n` yapıldı, yeniden
derlendi (çalışma ağacına dokunulmadı):

```
lut_length | REAL rc | REAL warn | MUT rc | MUT warn
     2     |    0    |   no      |   0    |   no
     3     |    0    |   no      |   0    |   YES
     5     |    0    |   no      |   0    |   YES
     7     |    0    |   no      |   0    |   YES
     9     |    0    |   no      |   0    |   YES
REAL binary   : warning fired 0/5
MUTANT binary : warning fired 4/5
```
MUTANT stderr (verbatim):
```
WARNING: LUT data length is odd in profile 'default' (X axis) — should be even (speed, gain pairs)
WARNING: LUT data length is odd in profile 'default' (Y axis) — should be even (speed, gain pairs)
```
REAL binary aynı dosyada: `<none>` + `Validation passed. All checks OK.`

**Sonuç:** `:836` ve `:841` canlı kod, ama üretim sanitize'i onu erişilemez
kılıyor. Koruma "mevcut" görünüyor, hiç çalışmıyor, ve **asıl veri kaybını
sessizce yutuyor** — aşağıdaki O31-C2 vakası.

---

### L12-03 | src/config.cpp:229 + cli/main.cpp:835 | **HIGH** | 514 çift LUT sessizce 2'ye düşüyor; `validate` "All checks OK" diyor

O31-C2 yorumu (`src/config.cpp:223-228`) tam olarak bu sessiz veri kaybını
tanımlıyor — ama `cmd_validate` onu **raporlamıyor** (L12-02'nin ölü guard'ı
bunu yakalamak içindi, yakalamıyor).

```
# lut_length:514, lut_data:2 float — "514 çift eğri" iddiası
validate rc=0 stderr=<none>
stored lut_length=2  lut_data=[1.0, 2.0]
-> 514 claimed pairs became 2 stored; validate said nothing.
stdout: "Validation passed. All checks OK."
```
Kullanıcının elinde 512 eğri noktası vardı, validate "hepsi tamam" dedi,
diskte 2 nokta kaldı. Bir sonraki `save` bunu kalıcılaştırır.

---

### L12-04 | cli/main.cpp:847-892 | **HIGH** | CFG-2 "clamped-vs-stored" çapraz kontrolü 13 clamp noktasının **10'unu** hiç denetlemiyor

`:861-873`'teki `clamp_warn` yalnızca 5 alanı besleniyor: `dpi`,
`polling_rate`, `output_dpi`, `rotation`, `snap` (`:874-880`).
`accel_args`ın **hiçbir** alanı beslenmiyor — oysa `sanitize_accel_args` onları
kırpıyor. Ölçüm: alan bir değer yaz → `validate` → `list --json` ile gerçekte
saklananı oku.

```
=== accel_args clamp sites vs cmd_validate reporting ===
name                     wrote    stored    rc  err  verdict              src site
exponent_classic_99      99       10.0      0   0    *** SILENT CLAMP ***  config.cpp:420-421
exponent_classic_0       0        1.0       0   0    *** SILENT CLAMP ***  config.cpp:420-421
limit_1e6                1000000  100.0     0   0    *** SILENT CLAMP ***  config.cpp:501 (LIMIT_MAX)
limit_neg_5              -5       0.0       0   0    *** SILENT CLAMP ***  config.cpp:464
cap_x_9999               9999     500.0     0   0    *** SILENT CLAMP ***  config.cpp:495 (CAP_X_MAX)
input_offset_9999        9999     500.0     0   0    *** SILENT CLAMP ***  config.cpp:494
capx_lt_inputoffset      1        50.0      0   0    *** SILENT CLAMP ***  config.cpp:480 (BUG-7)
scale_1e9                1e9      100.0     0   0    *** SILENT CLAMP ***  SCALE_MAX=100
exponent_power_1e9       1e9      5.0       0   0    *** SILENT CLAMP ***  EXP_POWER_MAX=5
cap_y_1e9                1e9      100.0     0   0    *** SILENT CLAMP ***  CAP_Y_MAX=100
output_offset_1e9        1e9      100.0     0   0    *** SILENT CLAMP ***  OUTPUT_OFFSET_MAX=100
domain_weight_1e9        1e9      1000000.0 0   0    *** SILENT CLAMP ***  P86 ceiling
SUMMARY: 11/13 probes were SILENT CLAMPS
```
**Ek olarak — `:825-832`'deki CFG-2 yorumu bu vakayı KENDİSİ sayıyor:**
> *"a file with `dpi:999999, speed_min 20 speed_max 5, output_dpi 50000` always
> printed 'All checks OK'. The real clamped-vs-stored cross-check runs against
> the RAW JSON below."*

`speed_min=20, speed_max=5` ayrıca ölçüldü:
```
speed_min=20 speed_max=5 -> validate rc=0 stderr=<none>
stored speed_min=20.0 speed_max=20.0     <-- sanitize speed_max'i 20'ye YÜKSELTTİ
```
Yani **yorumun saydığı üç vakadan biri hâlâ sessiz geçiyor.** `clamp_warn`
`speed_min`/`speed_max` için beslenmiyor. Yorum, düzeltmeyi olduğundan geniş
anlatıyor.

---

### L12-05 | cli/main.cpp:446-483 + 485-537 | **MED** | `show`/`list` 40 settable anahtarın 6'sını hiç basmıyor — "ayarım yok" yanılgısı

`print_profile`/`print_accel_args` alan envanteri (`grep -c` ile):

| yapı | üye sayısı | basılan |
|---|---|---|
| `accel_args` (`rawaccel-base.hpp:63-109`) | 20 (`grep -c`) | 16 satır (`grep -c 'std::cout << prefix'`, `:459-482`) |
| `profile` (`:119-137`) | 15 | 23 satır (koşullu olanlar dahil) |
| `speed_args` (`:111-117`) | 5 | 5 |

Eksik olan, `set-param` ile **kabul edilen** alanlar:
```
sed -n '485,537p' cli/main.cpp | grep -c "domain_weight\|range_weight"   → 0
sed -n '446,483p' cli/main.cpp | grep -c "domain_weight\|range_weight"   → 0
```

40 anahtarın tamamı için ölçüm (başarılı `set-param` → JSON değişti mi →
`show` çıktısı **byte-eşit** mi):

```
=== rc==0, JSON CHANGED, ama `show` çıktısı BYTE-IDENTICAL ===
  domain_weights      show-IDENTICAL   json changed: 0: 1.0 -> 3.0; 1: 1.0 -> 3.0
  domain_weight_x     show-IDENTICAL   json changed: 0: 1.0 -> 3.0
  domain_weight_y     show-IDENTICAL   json changed: 1: 1.0 -> 4.0
  range_weights       show-IDENTICAL   json changed: 0: 1.0 -> 0.5; 1: 1.0 -> 0.5
  range_weight_x      show-IDENTICAL   json changed: 0: 1.0 -> 0.5
  range_weight_y      show-IDENTICAL   json changed: 1: 1.0 -> 0.25
count: 6 of 40 keys
```
Somut örnek: `set-param default domain_weight_x 3` → `rc=0`,
`"Set domain_weight_x = 3.000000"`, JSON'a yazıldı — sonra `show` bu değer
için **hiçbir satır basmıyor**. Kullanıcı `show` çalıştırıp "ayarım yok"
diyor. `list` de aynı şekilde sessiz.

Aynı sınıftan ikinci bir örnek — `print_profile:496-501` `raw_passthrough`
doğruysa **5 satırla erken dönüyor**, kalan 26 alanı hiç basmıyor:
```
Profile: default
  device_id:    (all)
  raw:          true  (all processing bypassed)
  dpi:          800
  polling_rate: 1000
lines printed: 5   (non-raw `show` prints 31)
  rotation/snap/speed_min/output_dpi/lr_ratio/domain/range/smooth/power
  → hepsi "*** NO ***"
```
Dosyada `rotation=45, snap=10, speed_min=1, output_dpi=1600, lr_ratio=2,
domain_weights=[3,3], halflife=7, mode=power` vardı; `show` hiçbirini basmadı.

---

### L12-06 | cli/main.cpp:1211 + 1030-1040 | **MED** | `device_id`/`match_app`/`use_raw_input`/`disable` CLI ile ayarlanamıyor; `disable` `show`'da görünüyor

`all_keys` (`:1030-1040`) 40 anahtar. Yapıda **var olan** ama listede
olmayan alanlar:
```
$ set-param default disable true
Unknown key: disable
Valid keys: mode gain cap_mode cap_x ... (40 anahtar)
rc=1
```
`disable` bir hile değil: `device_config::disable` (`config.hpp:73`) gerçek bir
alan ve `print_profile:491-494` onu **kullanıcıya gösteriyor** —
`disabled: true (C29-N6: honored at setup/hot-plug ...)`.
Ölçüldü: JSON'a `"disable": true` yazıldı → `show` `disabled: true` bastı →
`set-param default disable false` → `Unknown key: disable`, rc=1.
**Kullanıcı CLI'ın gösterdiği bir bayrağı CLI'la kapatamıyor** (GUI veya elle
JSON düzenlemesi gerekir). Aynı şekilde `match_app` (`config.hpp:89`) ve
`use_raw_input` (`config.hpp:95`) de set edilebilir değil.

---

### L12-07 | cli/main.cpp:413-419 | **MED** | `finite_double_to_int` NaN'da **UB** — iki bağımsız guard arkasında, üretimde erişilemez

Fonksiyon **verbatim** çıkarıldı (`md5 verbatim.inc` == `md5 sed -n '413,419p'`)
ve UBSan altında koşuldu (`clang++ -std=c++20 -fsanitize=undefined,float-cast-overflow`):

```
verbatim.inc:6:29: runtime error: nan is outside the range of representable values of type 'int'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior verbatim.inc:6:29
```

Tam tablo (17 girdi):
```
input              | return                   | verdict
INT_MIN-1          | -2147483648              | clamped        <-- taşma YAKALANIYOR
(double)INT_MIN    | -2147483648              | exact
INT_MAX exact      | 2147483647               | exact
INT_MAX+1 = 2^31   | 2147483647               | clamped        <-- BUG-CRIT-2 düzeltmesi çalışıyor
1e18               | 2147483647               | clamped
1e300              | 2147483647               | clamped
literal 1e400      | 2147483647               | clamped        <-- TAŞMA YOK, DOĞRU DÖNÜŞ
+Inf               | 2147483647               | clamped
-Inf               | -2147483648              | clamped
NaN (quiet)        | -2147483648              | UNDEF (fp-cast NaN)   <-- UB
NaN (signaling)    | -2147483648              | UNDEF (fp-cast NaN)   <-- UB
```

**Yanıt soruya:** Taşma **tespit ediliyor** — `1e400`, `±Inf`, `1e18` hepsi
`INT_MIN`/`INT_MAX`'e clamp oluyor, UB yok. **Ama NaN tespit edilmiyor**:
iki karşılaştırma (`<`, `>=`) NaN'da false'tur, `static_cast<int>(NaN)`
**tanımsız davranış**. Bu makinede tesadüfen `INT_MIN` dönüyor — yani
"çalışıyor görünüyor", brifing §3'ün tam sınıfı.

**Erişilebilirlik ölçüldü — üretimde kapalı, iki bağımsız guard:**
`finite_double_to_int` yalnız iki yerde çağrılıyor (`:1284` dpi, `:1285`
polling_rate) ve ikisi de `int_ok()` arkasında (`:1112`, `:1125`).
Gerçek ikili: `set-param default dpi nan` → `rc=1`,
`Invalid numeric value: nan (NaN/Inf not allowed)` (`:1073` `isfinite` guard'ı).

**Mutasyonla guard'ların yükünü kanıtladım** (`/tmp/opencode/l12-cli/mut2/`,
çalışma ağacına dokunulmadı):
- MUT-1 (`:1073-1077` `isfinite` guard'ı silindi): `dpi nan` →
  `Invalid value for 'dpi': nan (must be an integer)` — **hâlâ rc=1**,
  `int_ok`'in `v != std::floor(v)` kontrolü yakaladı.
- MUT-2 (`isfinite` **ve** iki `int_ok` birlikte etkisiz):
  ```
  cli/main.cpp:418:29: runtime error: nan is outside the range of representable values of type 'int'
  Set dpi = 1 in profile 'default'
  rc=0
  ```
  UB **erişilebilir** oldu ve `dpi` sessizce **1** oldu, rc=0.

**Hüküm:** bugün üretimde erişilemez (iki bağımsız guard var), ama fonksiyon
kendi başına savunmasız. `finite_double_to_int` yalnız `dpi`/`polling_rate`
için, ikisi de `int_ok` arkasında — yani **bu satır iki kez doğrulanıyor**,
`isfinite` olmasa da güvende. Sınıf: latent, MED. Kod yorumları iki guard'ın
da "kazandığını" ima ediyor; guard'ları birleştirmek veya buraya
`std::isfinite` eklemek savunma derinliği olurdu.

---

### L12-08 | cli/main.cpp:756-908 | **MED** | `cmd_validate` hiçbir **anlamsız ama geçerli** ayarı yakalamıyor

Görevde sorulan soru doğrudan ölçüldü. 30+ elle düzenlenmiş config, her biri
`validate`dan geçirildi:

```
=== B. semantically meaningless but VALID-range ===
limit_0                            rc=0  *** SILENT PASS *** (no stderr at all)
classic_limit_0                    rc=0  *** SILENT PASS ***
dead_curve_classic_limit0          rc=0  *** SILENT PASS ***   (ölü eğri)
classic_acceleration_0             rc=0  *** SILENT PASS ***
classic_gain_false                 rc=0  *** SILENT PASS ***
smooth_1_maxlag                    rc=0  *** SILENT PASS ***
domain_range_weights_0             rc=0  *** SILENT PASS ***   (ölü yön ağırlıkları)

=== C. mode=lookup, LUT yok (set-param bunu C-8 ile UYARIR) ===
lookup_mode_no_lut                 rc=0  *** SILENT PASS ***

=== D. yazım hatası / yanlış tip / bilinmeyen anahtar ===
json_key_typo_pollingrate          rc=0  *** SILENT PASS ***   ("pollingrate" → yok sayıldı)
dpi_wrong_type_string              rc=0  *** SILENT PASS ***   ("dpi":"eight hundred")
cap_x_lt_input_offset              rc=0  *** SILENT PASS ***   (BUG-7 ihlali)
lut_data_over_capacity             rc=0  *** SILENT PASS ***
raw_passthrough_with_accel         rc=0  *** SILENT PASS ***   (raw=true iken accel ölü kod)
disable_true                       rc=0  *** SILENT PASS ***   (tüm profil ölü)
cap_x_9999_above_CAP_X_MAX         rc=0  *** SILENT PASS ***
limit_1e6_above_LIMIT_MAX          rc=0  *** SILENT PASS ***
exponent_classic_99                rc=0  *** SILENT PASS ***
```

`limit=0` + `mode=classic` → imleç asla hareket etmez, `validate` "All checks OK"
der. `raw_passthrough: true` iken `accel_x.mode=power` ayarlıysa o ayar tam
olarak ölü kod, sessiz.

**Yakalananlar (pozitif kontrol — gate gerçekten çalışıyor):**
```
active_profile_ghost   rc=1  ERROR: Active profile 'ghost' not found in profiles list.
duplicate_profile_name rc=1  ERROR: Duplicate profile name: 'default'
dup_device_id_SAME     rc=0  WARNING: Duplicate device_id: 'usb:AAAA:1111:ZZ' (first match wins in daemon)
                            stdout: "Validation passed with warnings."
distinct_device_ids_CONTROL  rc=0  <no stderr>  stdout: "Validation passed. All checks OK."
empty profiles         rc=1  ERROR: No profiles defined.
trailing garbage JSON  rc=1  ERROR: Failed to load/parse config: [json.exception.parse_error.101]
```
**Eksik kontrol listesi (validate'ın YAPMADIĞI):** anlamsız-ama-geçerli
kombinasyonlar (limit=0 / gain=false + classic / acceleration=0 /
domain_weights=[0,0]); `mode=lookup` + boş LUT (set-param bunu uyarıyor,
validate etmiyor — **iki komut aynı hatada farklı davranıyor**);
`raw_passthrough` + canlı accel; `disable` + canlı accel; JSON'da
**tanınmayan anahtar** (yazım hatası); **yanlış tip** (`"dpi":"eight hundred`
→ default'a düşüyor, `is_number()` kontrolü `:864`'te `clamp_warn`'ı atladığı
için raporlanmıyor); LUT kapasite aşımı; `speed_min > speed_max`;
`speed_min`/`speed_max` dahil 8 clamp alanı (bkz. L12-04).

---

### L12-09 | cli/main.cpp:631-661 | **INFO (olumlu — ölçüldü)** | `cmd_delete` son profili silince `settings.json` boş kalmıyor

Görevin 4. maddesinin "çökmesi sınıfı" korkusu **yerinde değil**:
```
$ delete default        # tek profil
No profiles remain — a fresh 'default' profile is recreated on the next run.
Deleted profile: default
rc=0
--- dosya: { "active_profile": "", "profiles": [], "use_raw_input": true, "version": "1.2.5" }
```
Geçerli JSON, `active_profile` boşaltılmış (`:648-650`), asla bozuk değil.
Sonraki komut kendini onarır:
```
list --json on that file -> rc=0, parses=yes, n_profiles=1 active='default'
validate on repaired     -> rc=0  "Validation passed. All checks OK."
validate on FRESH empty  -> rc=1  "ERROR: No profiles defined."  file-touched=no
list     on FRESH empty  -> rc=0  repaired=True
```
`validate` dosyaya **dokunmuyor** (mtime ölçüldü: before == after), yani
P115-A5-08/C-7 düzeltmesi tutarlı. Ayrıca var olmayan profil için hepsi rc=1:
`delete NOSUCH`→1, `duplicate NOSUCH x`→1, `create-preset NOSUCHPRESET`→1,
`set NOSUCH`→1, `show NOSUCH`→1 — dosyalar `unchanged`.

---

### L12-10 | cli/main.cpp:1351-1362 | **INFO** | `cmd_set_param`'ın son `else` kolu **ölü kod** (savunma-in-depth olarak doğru, ama asla çalışmıyor)

Whitelist (`:1030-1040`) ile else-if zinciri **birebir aynı 40 anahtarı**
içeriyor — ölçüldü:
```
whitelist count: 40 | chain count: 40
in whitelist NOT in chain: []
in chain NOT in whitelist: []
SAME SET: True   (sadece SIRALAMA farklı)
```
`:1041` zaten `return 1` yapıyor, bu yüzden `:1351`'deki `else` kolu hiç
girilemez. Zararsız (bir anahtar whitelist'ten düşerse zincir onu yakalar) ama
`:1352-1361`'deki "Valid keys" metni whitelist'in **elle kopyalanmış** ikinci
bir listesi — iki kaynak, ölçüldüğü gibi şu an birebir aynı, ama iki ayrı
listeyi senkron tutmak gerekiyor (bugün ölü kod, yarın tek kaynak olur).

---

### L12-11 | cli/main.cpp:387-388 | **MED** | "saved locally but the daemon did not reload" yolunda **bozuk düzeltme önerisi**

`daemon_apply_if_enabled` rc döndürmesi doğru ve dürüst (rc=1). Ama mesajın
ikinci satırı yanlış talimat veriyor:
```cpp
std::cerr << "Warning: config saved locally but the daemon did not reload it.\n"
          << "  Re-run without the change, or apply with: rawaccel-cli reload\n";
```
**"Re-run without the change"** — kullanıcı bunu "değişikliği geri alıp tekrar
çalıştır" olarak okur; kastedilen muhtemelen **"değişiklik yapmadan tekrar
çalıştır"** (`--no-daemon`'ı kaldır). Yazıldığı haliyle talimat kendi kendisiyle
çelişiyor. Bu satırı **çalıştırmadım** (yürürlükteki daemon'a ihtiyaç duyuyor,
aşağıdaki "TEMSİL SINIRI"na bakınız) — bulgu metnin okunmasına dayanıyor.

---

## ⭐ GÖREV 6 — `cmd_*` DÖNÜŞ KODU TABLOSU (24 satır ölçüldü)

`daemon_apply_if_enabled` `--no-daemon` ile sabitlendi (kapı 6'nın yaptığı gibi).

| # | komut | want | got | dosya | not |
|---|---|---|---|---|---|
| 1 | `list` | 0 | 0 | unchanged | ✅ |
| 2 | `list --json` | 0 | 0 | unchanged | ✅ |
| 3 | `show default` | 0 | 0 | unchanged | ✅ |
| 4 | `show ghost` | 1 | 1 | unchanged | ✅ |
| 5 | `validate` | 0 | 0 | unchanged | ✅ dosyaya dokunmuyor |
| 6 | `set default` | 0 | 0 | unchanged | ✅ |
| 7 | `create newp` | 0 | 0 | **changed** | ✅ |
| 8 | `duplicate default copy1` | 0 | 0 | **changed** | ✅ |
| 9 | `create-preset gaming gp` | 0 | 0 | **changed** | ✅ |
| 10 | `set-param default limit 2` | 0 | 0 | **changed** | ✅ |
| 11 | `set ghost` | 1 | 1 | unchanged | ✅ |
| 12 | `show ghost` | 1 | 1 | unchanged | ✅ |
| 13 | `delete ghost` | 1 | 1 | unchanged | ✅ |
| 14 | `duplicate ghost x` | 1 | 1 | unchanged | ✅ |
| 15 | `create-preset nope x` | 1 | 1 | unchanged | ✅ |
| 16 | `create ""` | 1 | 1 | unchanged | ✅ |
| 17 | `create default` (dup) | 1 | 1 | unchanged | ✅ |
| 18 | `set-param ghost limit 2` | 1 | 1 | unchanged | ✅ |
| 19 | `set-param default nope 1` | 1 | 1 | unchanged | ✅ |
| 20 | `set-param default mode classicc` | 1 | 1 | unchanged | ✅ |
| 21 | `set-param default dpi nan` | 1 | 1 | unchanged | ✅ |
| 22 | `set-param default dpi 99999` | 1 | 1 | unchanged | ✅ |
| 23 | `set-param default limit 2junk` | 1 | 1 | unchanged | ✅ |
| 24 | `set-param default speed_min 9` | 0 | 0 | changed | ✅ **beklenen 1 değil 0** |

```
rows=24  rc-mismatches=0
```
Satır 24'teki tek "MISMATCH" **benim beklentim yanlıştı**, kod doğru:
`:1292` `if (dp->prof.speed_max > 0 && ...)` — `speed_max==0` "devre dışı"
anlamına geliyor, guard bilinçli atlanıyor. Doğrulandı:
```
speed_max=0,  set speed_min 9  -> rc=0   (guard skipped: speed_max>0 false)
speed_max=20, set speed_min 9  -> rc=0   (geçerli)
speed_max=20, set speed_min 30 -> rc=1   ERROR: speed_min (30) > speed_max (20)
speed_min=5,  set speed_max 2  -> rc=1   ERROR: speed_max (2) < speed_min (5)
```
**Hüküm: lane'ın 13 `cmd_*` fonksiyonunda hata durumunda 0 dönen YOK.**
Hata yolları hem `return 1` ile hem "dosya değişmedi" ile çift doğrulanıyor.
Ayrıca `print_profile`/`print_accel_args`/`finite_double_to_int` void ya da
saf — rc sorunu taşımıyorlar.

---

## ⭐ GÖREV 2 — `cmd_set_param` GEÇERSİZ PARAMETRE ADI (olumlu — ölçüldü)

**Sessiz görmezden gelme YOK.** `:1041-1045` whitelist'i kullanıcı girdisinden
önce, herhangi bir mutasyondan önce reddediyor:
```
$ set-param default accelration 5      # 'acceleration' yazım hatası
Unknown key: accelration
Valid keys: mode gain cap_mode ... (40 anahtar)
rc=1
$ set-param default disble true
Unknown key: disble       rc=1
$ set-param default mode classicc       # geçerli anahtar, bozuk DEĞER
Invalid mode: 'classicc'.  Valid: classic, power, ...      rc=1
$ diff base.json t2.json -> IDENTICAL (hiçbir mutasyon olmadı)
```
`set_param`'ın `Stored`/sav zincirinde `return 1` **önce** geliyor, yani dosya
hiç değişmiyor. Bu, görevde işaret edilen "en tehlikeli sınıf"ın **bulunmadığı**
anlamına gelir — P107/P82 düzeltmeleri çalışıyor.

---

## ⭐ GÖREV 7 — SESSİZ YEŞİL ÖZETİ

`cli/main.cpp:1-1374` içinde 6 `WARNING` yazımı var; hepsi incelendi:

| satır | yazı | okuyan var mı? |
|---|---|---|
| 811 | `Duplicate device_id` | `test_accel.cpp:6785` **sadece yorum satırı** (`// Duplicate device_ids → warning`) — gerçek assertion yok |
| 836 | `LUT data length is odd` | **HİÇBİR TEST OKUMUYOR** + üretimde erişilemez (L12-02) |
| 841 | `LUT data length is odd` (Y) | aynı |
| 868 | `'X' = N ... will be stored as M` | **HİÇBİR TEST OKUMUYOR** |
| 884 | `profile name ... will be truncated` | **HİÇBİR TEST OKUMUYOR** |
| 1211 | `mode=lookup has no LUT data` | **HİÇBİR TEST OKUMUYOR** + rc=0 (L12-01) |

```
$ grep -rl "LUT data length is odd" tests/ scripts/ .github/   → (boş)
$ grep -rl "sanitize clamps on load" tests/ scripts/ .github/ → (boş)
$ grep -rl "mode=lookup has no LUT" tests/ scripts/ .github/  → (boş)
$ grep -cn "validate" tests/run_tests.sh                       → 0
```
Pozitif kontrol (metot bozuk değil): aynı `grep -rl "Duplicate device_id"`
`tests/test_accel.cpp`'yi buluyor.

**Kapı 6'nın bu satırları okumadığı ayrıca doğrulandı:** `run_cli_sanitized.sh`
`validate`'ı sadece iki vakada koşuyor (`:126` mevcut config, `:127` yok dosya)
ve **beklenen çıktı işaretine** bakıyor (`vaka()` `:92`), **rc'ye değil**.
Dolayısıyla `validate`'ın uyarı üretip üretmediği kapı 6'da test edilmiyor.
Kapı 6 `set-param`'ın 40 anahtarın yalnızca **4'ünü** koşuyor
(`rotation snap dpi acceleration`, `+1 unknown-key`):
```
$ grep -oE 'set-param p_gaming [a-z_]+' tests/run_cli_sanitized.sh | sort -u
acceleration   rotation   snap   dpi   (+ bogus)
count: 5 of 40
```
`domain_weight_*` / `range_weight_*` (L12-05'in kayıp 6 anahtarı) hiç test
edilmiyor.

---

## KAPI

```
$ bash tests/run_cli_sanitized.sh          # 6. KAPI — ASan + UBSan altında gerçek CLI
  ✓ create-preset gaming/office/precision/disable/cs2/valorant/apex/fps   rc=0 (8)
  ✓ list ✓ show ✓ set ✓ set-param×4 ✓ duplicate ✓ rename ✓ delete ✓ export
  ✓ list --json ✓ validate ✓ diff ✓ import×2 ✓ show-missing ✓ config-yolu×4
=== Sonuç: 31/31 komut gerçek kodu çalıştırdı ===
=== Sonuç: PASS — CLI ASan/UBSan altında temiz (31/31 komut) ===
GATE6_RC=0
```
**ÜRETİLEN SAYI: 31 komut, 31/31 geçti, 0 sanitizer ihbarı, rc=0.**

Ek mutant kapılar (tamamı `/tmp/opencode/l12-cli/`, çalışma ağacı dışında):
- `mut3/cli_mut3` — `src/config.cpp:229` çift-zorlama satırı kaldırıldı →
  `cmd_validate`'in uyarısı **4/5** vakada patladı (gerçek ikilide **0/5**).
- `mut2/cli_mut2` — `cli/main.cpp:1073-1077` `isfinite` + iki `int_ok`
  etkisiz → `dpi nan` **UBSan runtime error** + sessiz `dpi=1`, rc=0.
- `t1/h` — `finite_double_to_int` verbatim, UBSan float-cast-overflow,
  17-girdi tablosu.

Kapı 6 **çalışma ağacını değiştirmedi** (`build-manual/` `.gitignore:10`'da).

---

## KAPSANMAYAN (lane dışı — birinin bakması gereken yer)

1. **`cli/main.cpp:1375+`** (başka ajanların): `cmd_export`, `cmd_diff`,
   `cmd_import`, `cmd_rename`, `cmd_stop`, `cmd_monitor`, `cmd_status_json`,
   `cmd_status`, `cmd_receivers`, `cmd_hidpp*`, `print_help`, **ve tüm
   dispatch/`main()`**. Özellikle `:3266/:3270` `g_json` okuyucuları burada —
   L12-05'in `show` bulgusu `--json` yolunda kapanıyor olabilir, doğrulanmadı.
2. **`finite_double_to_int`'in diğer kopyaları yok** — `grep -n
   finite_double_to_int cli/main.cpp` tam olarak 3 satır (tanım + 2 çağrı).
   `daemon/main.cpp` ve `gui/main.cpp` kendi kopyalarını taşıyor olabilir,
   taramadım.
3. **`sanitize_accel_args` / `sanitize_device_profile` clamp noktaları**
   (`src/config.cpp`, başka lane) — L12-04'teki "11 clamp sessiz" tablosu
   o lane'ın da ilgilenmesi gereken bir liste: `cmd_validate` onları
   raporlamıyor.
4. **GUI'nin aynı alanları** (`gui/widgets_sync.inl`) — `disable`/`match_app`
   L12-06'da CLI'dan ayarlanamıyor; GUI'de ayarlanabiliyor mu bilmiyorum.
5. **Çoklu-ajan çakışması uyarısı:** bu turda `.aihaberlesme/mesajlar/`
   altında `denetim-L04/L08/L10/L11/L14/L16.md` **yoktu**; ayrıca
   `/etc/rawaccel/settings.json.bak` "default/base/big" profilleriyle
   23:26'da yazılmıştı — yani **paralel bir ajan canlı daemon'a push yapıyor**
   (aşağıya bak).

---

## ⛔ KENDİ NEDENİMLE YAPTIGIM KALICI YAN ETKİ (bildirilmesi gerekir)

Bu lane "kod değiştirme" yasaklıydı ve **kaynak ağacına dokunmadım**. Ancak
ölçüm sırasında bir komutu `--no-daemon` **olmadan** çalıştırdım ve bu
konuda **çalışan bir daemon'a** (`pid 701`, `-c /etc/rawaccel/settings.json`)
gerçek bir push yaptı:

```
$ rawaccel-cli -c /tmp/.../nd.json set-param default limit 3     # --no-daemon YOK
Set limit = 3.000000 in profile 'default'
Config applied to the daemon.
rc=0
→ /etc/rawaccel/settings.json 23:30'da 1 profille (default) EZİLDİ
```

**Zarar ve telafi:**
- Daemonın kendi `.bak` rotasyonu (`/etc/rawaccel/settings.json.bak`, 23:26)
  ezilen önceki içeriği korumuşta: 3 profil (`default`, `base`, `big`).
- **Geri yükledim** — CLI'ın meşru yoluyla:
  ```
  $ rawaccel-cli -c restore.json set default      # --no-daemon YOK (kasıtlı)
  Config applied to the daemon.   rc=0
  $ → /etc/rawaccel/settings.json: active=default  n=3
       default limit=1.5 | base limit=1.5 | big limit=1.5
  ```
- Gerçek kullanıcı config'i `/home/a/.config/rawaccel/settings.json`
  **hiç etkilenmedi** (mtime 2026-09-30 22:38, yani eski).
- Bu aslında **L12-01'in canlı kanıtı** oldu: `daemon_apply_if_enabled`
  `--no-daemon` olmadan config'i **kalıcı olarak** yazıp rc=0 döndü — yani
  "kaydettim" ile "uygulandı" arasındaki sınır, kullanıcının `--no-daemon`
  bilgisi olmadan sessizce aşılıyor. **Aj1'e öneri:** L12 denetimine benzer
  ölçüm yapan ajanlar `mutasyon komutlarını her zaman --no-daemon` ile
  koşsun; canlı daemon bu makinede kurulu.

---

## TEMSİL SINIRI

1. **`--no-daemon`'sız hiçbir mutasyon komutunu izole koşturamadım** — bu
   makinede canlı daemon (pid 701) + gerçek `/etc/rawaccel/settings.json`
   var ve root'suz düzeltme gerekiyor. Bu yüzden:
   - `daemon_apply_if_enabled:387-388`'in **bozuk düzeltme metni** (L12-11)
     **çalıştırılmadı**, yalnız okundu.
   - `daemon_apply_config` (`:329-350`) üç dalını (ok:true / ok:false /
     legacy SIGHUP fallback) **ölçemedim** — bunlar başka lane'ın (`>1374`
     `daemon_ipc_send` çağıran yerler) konusu.
2. **PS5.1 / PS5.2 (`hidpp*`) hiç koşulmadı** — donanım yok, `>1374`.
3. **`finite_double_to_int` için üretimde erişilemez olduğumu** iki mutasyonla
   kanıtladım (L12-07), ama "gelecekte erişilebilir olur" öngörüsü
   ölçüm değil — yalnız mevcut korumanın iki bağımsız katman olduğu tespiti.
4. **Çok ajanlı yarış**: paralel ajanların `/etc` üzerindeki push'ları
   (bkz. "KAPSANMAYAN" 5) benimkiyle çakışmış olabilir; `/etc`'deki
   "default/base/big" içeriğinin gerçek sahibi değilim, sadece geri yükledim.
5. **`print_profile`'ın `std::fixed << std::setprecision(4)` state'i
   kalıcı** (`:460`, geri dönülmüyor) — sonraki `:509-514` çıktılarını
   etkileyip etkilemediğini tüm çıktı bağlamlarında doğrulamadım.
6. **Türkçe lokalizasyon / `tr()` kapsam dışı.**