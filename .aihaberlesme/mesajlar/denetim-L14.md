### L14 | alt-ajan (L14) | 2026-10-01

KAPSAM   : `cli/main.cpp` **satır 2265 – 3297** (bölge sınırı: `cmd_receivers` 2265,
           `cmd_hidpp` 2297, `parse_hidpp_uint` 2580, `hidpp_target_from_args` 2595,
           `cmd_hidpp_set_dpi` 2621, `cmd_hidpp_set_rate` 2666,
           `cmd_hidpp_set_lod` 2712, `print_help` 2788, `main` 2939-3297).
           Sadece OKUNDU: `src/logitech_hidpp.cpp`, `src/logitech_receiver.cpp`,
           `include/logitech_hidpp.hpp`, `tests/run_cli_sanitized.sh`,
           `tests/test_accel.cpp`. Hiçbir kaynak dosya değiştirilmedi (ölçüm §KAPI).

────────────────────────────────────────────────────────────────────────
## ⭐ SORU 1 — ÜÇ `set_*` KOMUTU CEVABI OKUYOR MU, BAŞARIYI DOĞRULUYOR MU?

**Kısa cevap: EVET cevap okunuyor ve HID++ hata çerçevesi denetleniyor — ama
DOĞRULANAN ŞEY "AYAR DEĞİŞTİ" değil, " paket geldi." Yani yarı-doğrulama.**

### 1a. `hidpp-set-dpi` — ham kod yolu

| adım | dosya:satır | ne yapıyor |
|---|---|---|
| 1 | `cli/main.cpp:2627` | `parse_hidpp_uint` + `dpi < 100 \|\| dpi > 32000` → red |
| 2 | `cli/main.cpp:2631` | `hidpp_target_from_args` → hedef index |
| 3 | `cli/main.cpp:2637-2641` | `HidppTransport` aç, `vendor_id()==0x046d` şart |
| 4 | `cli/main.cpp:2644` | `get_dpi_info()` → **cihazın kendi DPI listesi okunur** |
| 5 | `cli/main.cpp:2645-2657` | istenen DPI listede yoksa **yazmadan** `return 1` |
| 6 | `cli/main.cpp:2659` | `const bool ok = transport.set_dpi(...)` |
| 7 | `cli/main.cpp:2660-2663` | `{"ok":ok}` bas, `return ok ? 0 : 1` |

`ok`'un kaynağı — `src/logitech_hidpp.cpp:1904-1906` (extended) ve `:1917-1919`
(legacy): **her ikisi de `send_feature_request(...).has_value()`**.

`send_feature_request` gerçekten cevap okuyor (`src/logitech_hidpp.cpp:775-796`):
`read_packet` ile bekler, `:781`'de `is_hidpp_error()` ile hata çerçevesi elenir,
`:783-785`'te **device index + feature index + fonksiyon nibble + sw_id** eşleşmesi
doğrulanır, `:795`'te payload döner. **Yani: cevap okunuyor ve yanlış cevap
(başka sw_id / başka cihaz) eleniyor.** Bu iyi.

⛔ **AMA `has_value()` yalnızca "eşleşen, hata olmayan bir paket geldi" demektir.**
`is_hidpp_error` (`src/logitech_hidpp.cpp:34-38`) **sadece** `data[2]==0xFF||0x8F`
(feature-index alanı) durumunu yakalar. Ölçüm:

```
$ awk 'NR>=1868 && NR<=1920' src/logitech_hidpp.cpp | grep -c "get_dpi_info"
1                       ← yalnızca YAZMADAN ÖNCEki ön-okuma
$ awk 'NR>=1987 && NR<=2027' src/logitech_hidpp.cpp | grep -c "get_polling_rate"
0                       ← hiç okuma-yok
$ awk 'NR>=2039 && NR<=2062' src/logitech_hidpp.cpp | grep -c "get_lift_off_distance"
0                       ← hiç okuma-yok
```

`set_dpi` içinde `get_dpi_info` **1 kez** geçiyor ve o `src:1870`'te, yani
`:1904`'teki yazmanın **öncesinde**. `set_polling_rate` ve `set_lift_off_distance`
içinde **hiç** okuma-yok.

### 1b. Üç komut için ayrı ayrı hüküm

| komut | cevap okunuyor? | hata çerçevesi denetleniyor? | **yazdıktan sonra doğrulama** | exit |
|---|---|---|---|---|
| `hidpp-set-dpi` (2659) | ✅ `send_feature_request` | ✅ `is_hidpp_error` | ⛔ **YOK** — `dpi_current` geri okunmuyor | `ok?0:1` |
| `hidpp-set-rate` (2702) | ✅ aynı yol | ✅ aynı yol | ⛔ **YOK** — `get_polling_rate` geri çağrılmıyor | `ok?0:1` |
| `hidpp-set-lod` (2737) | ✅ aynı yol | ✅ aynı yol | ⛔ **YOK** — LOD geri okunmuyor | `ok?0:1` |

**"Donanım cevabı geldi" ≠ "ayar uygulandı".** HID++ 2.0'de SetDPI/SetReportRate/
SetLOD bir **komut**dur; cevabı `0x11 report + index + fn<<4|swid` eşleşmesidir,
içinde "uygulandı / şu değere ayarlandı" taşıyan bir payload yoktur. Cihaz değeri
**sessizce clamp ederse** (ör. 32000 iste, firmware 16000'e düşürür) ya da
**kendi DPI listesine göre yuvarlarsa**, `has_value()` yine `true` döner ve CLI
**"DPI set to 32000." basıp `return 0` der.** Kullanıcı "DPI 32000 yaptım" sanır,
fare 16000'dedir.

⛔ **Kanıtlanabilir ama burada ölçülemez kısım:** "cihaz clamp ederse sessiz yeşil"
senaryosu **donanım gerektirir**; bu konakta ölçemedim (bkz. TEMSİL SINIRI).
Kod yolu (`has_value()` tek kriter) ölçülmüştür; "donanım gerçekten clamp eder mi"
ölçülmemiştir. `cli/main.cpp:2623-2626`'daki yorum "The device itself will reject
values outside its advertised DPI list" der — bu **doğrulanmamış bir varsayımdır**
ve `has_value()` onu doğrulamaz.

### 1c. ⭐ Buna karşı CLI'nin **ÖN-OKUMA** koruması var (bu iyi ve ölçüldü)

`cli/main.cpp:2644-2658` yazmadan **önce** `get_dpi_info()` çağırıp cihazın
`dpi_levels` listesini tarıyor; istenen DPI listede yoksa **hiç yazmadan** `return 1`
+ desteklenen listeyi basıyor. Ve transport da aynı kapıyı **tekrar** koyuyor
(`src/logitech_hidpp.cpp:1874-1877`). Yani **aralık dışı/geçersiz DPI'nın
donanıma ulaşması engelleniyor** — donanım katmanındaki `return false` bile
caymanın kendi giriş doğrulaması sayesinde burada tetiklenmiyor.

⚠️ Bu ön-okuma **`get_dpi_info` `std::nullopt` dönerse TÜMüyle atlanır**
(`cli/main.cpp:2644`'te `if (auto dpi_info = ...)` — nullopt girmez). O durumda
tek koruma transport'un kendi `return false`'u kalır; yine de güvenli (fail-closed),
ama kullanıcı "DPI 1600 is not supported by this device" yerine **"DPI was rejected
by the device."** görür — teşhisi zayıflatan bir mesaj.

────────────────────────────────────────────────────────────────────────
## ⭐ SORU 2 — `cmd_hidpp_set_dpi` ARALIK DOĞRULAMASI

**VAR ve İYİ. Ölçülen kabul aralığı: 100 ≤ dpi ≤ 32000, ondalık tam sayı.**

Kaynak: `cli/main.cpp:2627`
```cpp
if (!parse_hidpp_uint(args[2], dpi) || dpi < 100 || dpi > 32000) {
    std::cerr << "DPI must be an integer from 100 to 32000.\n";
    return 1;
}
```
`parse_hidpp_uint` (`cli/main.cpp:2580-2593`): boş string reddi, **baştaki `-` redi**,
`strtoul(...,10)` (yalnız taban 10 — `0x320` ve `0100` reddedilir), `ERANGE`
reddı, artık karakter reddi, `>UINT32_MAX` reddi.

### Ölçülen kabul tablosu (gerçek ikili, açılamayacak yol üzerinden)
```
$ ./build-manual/rawaccel-cli hidpp-set-dpi /dev/nonexistent-hidraw <DPI>
  "1"            rc=1  DPI must be an integer from 100 to 32000.
  "50"           rc=1  DPI must be an integer from 100 to 32000.
  "99"           rc=1  DPI must be an integer from 100 to 32000.
  "100"          rc=1  Unable to open HID++ device      ← 100 KABUL EDİLİYOR
  "101"          rc=1  Unable to open HID++ device      ← 101 KABUL EDİLİYOR
  "1600"         rc=1  Unable to open HID++ device      ← KABUL
  "32000"        rc=1  Unable to open HID++ device      ← 32000 KABUL (sınır dahil)
  "32001"        rc=1  DPI must be an integer from 100 to 32000.
  "65535"        rc=1  DPI must be an integer from 100 to 32000.
  "99999"        rc=1  DPI must be an integer from 100 to 32000.
  "4294967295"   rc=1  DPI must be an integer from 100 to 32000.
  "4294967296"   rc=1  DPI must be an integer from 100 to 32000.
  "1e3"          rc=1  DPI must be an integer from 100 to 32000.
  "0x320"        rc=1  DPI must be an integer from 100 to 32000.
  ""             rc=1  DPI must be an integer from 100 to 32000.
  "+100"         rc=1  Unable to open HID++ device      ← ⚠ kabul (aşağıda)
  " 100"         rc=1  Unable to open HID++ device      ← ⚠ kabul (aşağıda)
```

**0 ve negatif değer gönderiliyor mu? HAYIR.** `"0"` → `dpi < 100` → red.
`"-100"` → `parse_hidpp_uint` `text[0]=='-'` → red. Ölçüldü.

### ⭐ "Logitech geçerli DPI adımları dışında değer" sorusu
Kullanıcı **herhangi bir 100..32000 tamsayısını** yazabilir (ör. `1234`,
`2500`, `777`). CLI **tek başına** geçerli adım listesini bilmez ve reddetmez —
reddi **cihazın kendi `dpi_levels` listesine** bırakır (`cli/main.cpp:2645-2656`).
Bu **tasarım olarak doğru** (adım listesi modele özeldir, 200/400/800/1600/3200
bir varsayım olamaz) ve transport'ta da ikinci kez doğrulanır
(`src/logitech_hidpp.cpp:1874-1877`).

### "Geçersizse donanım sessizce reddedip kod başarılı diyor mu?"
**HAYIR — ölçüldü.** Üç katmanlı:
1. `cli/main.cpp:2627` — 100..32000 dışı → yazmadan `return 1`.
2. `cli/main.cpp:2645-2657` — cihaz listesinde yoksa → **yazmadan** `return 1`.
3. `src/logitech_hidpp.cpp:1874-1877` — aynı kapı, transport içinde tekrar.
Ayrıca `src:1871` `if (!info || dpi == 0) return false;` → `dpi==0` ikinci kez reddedilir.

**Hüküm: "aralık doğrulaması var mı / geçersiz değer koda sızıyor mu" sorusunun
cevabı HAYIR — koruma gerçek ve üç katmanlı. Bu lane'in en sağlam kısmı.**

**INFO (düşük önem):** `"+100"` ve `" 100"` kabul ediliyor (`strtoul` baştaki `+`/boşluk
atlar; `parse_hidpp_uint` yalnız `-` reddediyor, `cli/main.cpp:2581`). Değer yine de
100'e normalize olduğu için **zararsız** — sadece mesaj "must be an integer" ile
"100 kabul" arasındaki sınır biraz yumuşak. Zararlı bir değer sızmıyor.

**INFO:** `cmd_hidpp_set_dpi` cihazın `dpi_levels` listesini okumak için **ek bir tam
HID++ turu** harcar (`get_dpi_info` `src:1671-1691`'deki chunk döngüsü, 256 iterasyon,
`kIdentifyBudget = 20 sn` kapağıyla). `set_dpi` **ikinci kez** `get_dpi_info` çağırır
(`src:1870`). Ölçüm: `get_dpi_info` çağrı sayısı `set_dpi` içinde 1, CLI'de 1 →
**tek bir `hidpp-set-dpi` çağrısı iki tam DPI sorgusu yapar**. Doğruluk değil
gecikme/USB yükü bulgusu.

────────────────────────────────────────────────────────────────────────
## ⭐ SORU 3 — `cmd_hidpp_set_lod`: ESKİ DURUM GERİ ALINIYOR MU?

**HAYIR — geri alma yolu yok, ama bu büyük ölçüde bir BUG değil: enum'da
"kapalı" değeri yok.** Ölçüm:

```
$ grep -n -A4 "enum class hidpp_lift_off_distance" include/logitech_hidpp.hpp
280: enum class hidpp_lift_off_distance : uint8_t {
281:     low = 1,
282:     medium = 2,
283:     high = 3,
284: };
```
HID++ 0x2202'de LOD byte'ı 1/2/3'tür; `0` "destek yok" sentineldir ve firmware
yazmayı reddeder (`src/logitech_hidpp.cpp:1878-1882` yorumu bunu açıkça söylüyor:
"0 is the 'not supported' sentinel and is rejected by firmware when written").
**Yani "kapatmak" diye bir fiziksel durum yok; kullanıcı yalnız 3 seviye
 arasında geçiş yapabilir ve eski değeri (`low`/`medium`/`high`) yeniden
 yazarak her zaman geri dönebilir.** Bu ayrıntı önemli: bulgu "geri alma yolu
 yok" değil, **"geri alma otomatik değil ve CLI hiçbir yerde mevcut LOD'u
 göstermiyor"**.

### Yazma öncesi mevcut değer okunuyor mu?
- **CLI'de HAYIR.** Ölçüm:
  ```
  $ awk 'NR>=2712 && NR<=2743' cli/main.cpp | grep -n "get_dpi_info\|get_lift_off\|info->"
  (boş)   ← CLI mevcut LOD'u hiç okumuyor
  ```
- **Transport'ta EVİL ama yanlış kullanım için.** `set_lift_off_distance`
  (`src:2042`) `get_dpi_info()` çağırıyor ve `info->lift_off_distance`'ı
  (`src:1663`) **yalnız yetenek kontrolü** için kullanıyor; ardından
  `params[5] = distance` ile **istenen** değeri yazıyor. Eski LOD hiçbir yerde
  saklanmıyor, döndürülebilir bir kayıt yok.

### ⭐ Okunmadan yazılan kritik alan: **DPI**
`set_lift_off_distance` 0x2202 fn 0x6'yı kullanır ve bu çağrı **DPI'yi de içerir**
(6 byte: sensor, dpiX hi/lo, dpiY hi/lo, lod). Kod mevcut DPI'yi geri yazıyor —
`src:2050-2056`:
```cpp
const uint16_t y = info->supports_y && info->dpi_current_y
    ? info->dpi_current_y : info->dpi_current;
const uint8_t params[6] = { 0, dpi_current>>8, dpi_current, y>>8, y, (uint8_t)distance };
```
**Bu doğru ve önemli bir koruma:** LOD yazılırken DPI yanlışlıkla sıfırlanmıyor.
Pozitif kontrol: `info->dpi_current` okunmadan yazılıyor olsaydı `params[1..4]` sıfır
gönderilirdi. Kullanıcı "sadece LOD değiştirdim" der, DPI 0'a düşerdi — **bu olmuyor.**

⛔ **Ama burada bir sessiz yeşil var:** `info->dpi_current` **cihazdan okunan
gerçek değerdir**, "kullanıcının istediği" değil. Cihaz 0 dönerse kod
`dpi_current = dpi_default`'a düşer (`src:1656`). Yani LOD yazımı, cihazın o an
bildirdiği DPI'yi **korur** — doğru. Ancak CLI `hidpp-set-lod` çıktısı yalnız
`"Lift-off distance set to low."` der; **DPI'nin de aynı pakette yeniden yazıldığını
söylemez.** Cihaz 0 bildirirse kullanıcı LOD'yi değiştirirken DPI'nın default'a
düştüğünü öğrenmez.

### Gerçek boşluk: LOD değişikliğini geri almak için kullanıcı ELDE İŞARET ALAMAZ
`cmd_hidpp` (`cli/main.cpp:2524-2530`) LOD'u yalnız **`dpi->supports_lift_off_distance`
doğruysa** basıyor, ve `src:2526-2527`'de `lod_names[dpi->lift_off_distance]` —
`lift_off_distance` 1..3 dışındaysa `"unknown"`, 0 ise `""` (boş) ve basılmıyor.
Yani kullanıcı mevcut LOD'u **`rawaccel-cli hidpp` çalıştırarak** öğrenebilir —
bu yol **var**. Ama `hidpp-set-lod` çıktısı önceki değeri **kendi içinde göstermez**
ve hiçbir yerde bir "eski LOD: X" satırı basılmaz.

**Hüküm: "okunmadan yazılıyor mu" → HAYIR, transport okuyor (dpi_current/dpi_current_y
için) ve LOD yeteneğini de doğruluyor. "Eski LOD kaydediliyor/geri alınabilir mi"
→ HAYIR, ama enum'da kapatma değeri olmadığı için bu bir *eksik özellik*, geri
dönüşü engelleyen bir kusur değil (kullanıcı `low/medium/high` arasında geri
yazabilir). MED.**

────────────────────────────────────────────────────────────────────────
## ⭐ SORU 4 — `cmd_receivers`: LİSTE BOŞKEN NE BASILIYOR?

`cli/main.cpp:2281-2284`:
```cpp
if (receivers.empty()) {
    std::cout << "No Logitech USB receivers found.\n";
    return 0;                       // ← SESSİZ YEŞİL
}
```
`--json` yolu (`:2267-2279`) boş listede **`[]`** basıp `return 0` verir.

### ⭐⭐ AYRIM YAPILAMIYOR — ÖLÇÜLDÜM

`discover_logitech_receivers()` (`src/logitech_receiver.cpp:90-115`) hata kodunu
**yutar**:
```cpp
std::error_code ec;
fs::directory_iterator it(sysfs_root, ec), end;
while (!ec && it != end) { ... }   // ← :94  ec varsa döngü hiç girmez
return result;                      // ← :114  hep boş vektör döner
```

Ölçüm — üç **farklı** durumu, üretim fonksiyonunu doğrudan çağıran bir probla:
```
$ ./probe
/tmp/opencode/l14/sys_empty      (exists, empty) -> 0 receiver(s)
/tmp/opencode/l14/sys_noperm     (chmod 000)   -> 0 receiver(s)
/tmp/opencode/l14/sys_missing    (ENOENT)      -> 0 receiver(s)
```
Aynı şekilde üçünün `directory_iterator` hata kodu:
```
$ ./probe2
/tmp/opencode/l14/sys_empty              ec=0  (Success)
/tmp/opencode/l14/sys_noperm             ec=13 (Permission denied)
/tmp/opencode/l14/sys_missing            ec=2  (No such file or directory)
```
**`ec=13` (EACCES) sessizce atılıyor.** Üç durum da `0 receiver(s)` →
CLI üçünde de **aynı metni** basıp **exit 0** veriyor.

### ⭐ Buna karşı aynı dosyanın İÇİNDE ayrım YAPILAN bir yol var (tutarsızlık)
`cli/main.cpp:209` ve `:228` — daemon sinyal yolu `permission_denied`'ı **ayrı
bir enum değeri** olarak taşıyor:
```cpp
enum class signal_result { sent, not_running, permission_denied, other };
...
if (errno == EPERM) return signal_result::permission_denied;
```
ve `:397` `case signal_result::permission_denied:` ile kullanıcıya **ayrı bir
remediation mesajı** veriyor. ⛔ **Proje bu ayrımı başka bir komutta yapıyor;
`receivers`/`hidpp` yapmıyor.** Bu, bir "tercih" değil, **aynı dosyada tutarsızlık**.

### ⭐⭐ EN AĞIR ÖLÇÜM — bu konakta canlı olarak
```
$ ls -la /dev/hidraw0
crw------- 1 root root 243, 0        ← major 243 = hidraw, GERÇEK cihaz takılı

$ ./build-manual/rawaccel-cli hidpp
No Logitech hidraw devices found.
rc=0

$ ./build-manual/rawaccel-cli --json hidpp
[]
rc=0
```
`discover_logitech_hidraw_devices` (`src/logitech_hidpp.cpp:2224-2225`) keşif
sırasında `open(path, O_RDONLY)` yapıyor ve **başarısız olursa `continue` ediyor**:
```cpp
int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
if (fd < 0) continue;                // ← :2225  EACCES yutuluyor
```
Sonuç: **gerçekte takılı, Logitech olabilecek bir hidraw düğümü var**, kullanıcı
root olmadığı için `open` EACCES alıyor, düğüm listeden **sessizce düşüyor**, ve
CLI **"No Logitech hidraw devices found." + exit 0** diyor. Kullanıcı
**"alıcı/fare takılı değil" ile "erişim yok" ayırt EDEMİYOR** — halbuki `/dev/hidraw0`
orada duruyor. Bu, görevin ⭐ olarak işaretlediği ayrımın en somut kanıtı.

**MED (yüksek görünürlük).** Kullanıcı ek olmadan `sudo`'ya geçemez ve sorunu
sebep-sonuç zinciriyle göremez; en iyi ihtimalle " faresi çalışmıyor sanır.

────────────────────────────────────────────────────────────────────────
## ⭐ SORU 5 — `cmd_hidpp` BİLİNMEYEN ALT KOMUT (2297-2620)

**Sessizce `0` dönme YOK — bilinmeyen alt komut REDDEDİLİR ve `1` döner. Ölçüldü.**

`cmd_hidpp` hiç argüman almaz; arity tablosu (`cli/main.cpp:3028`) bunu zorlar:
```cpp
{ "hidpp", 0, 0 },
```
ve fazladan argüman kontrolü `:3070-3073`:
```cpp
if (nargs > max_args) {
    std::cerr << "Command '" << args[0] << "' takes at most " << max_args
              << " argument(s), but got " << nargs << ".\n";
    ...
    return 1;
}
```
Ölçüm:
```
$ ./build-manual/rawaccel-cli hidpp foo
rc=1   Command 'hidpp' takes at most 0 argument(s), but got 1.
       Usage: rawaccel-cli hidpp
$ ./build-manual/rawaccel-cli hidpp bogus sub
rc=1   Command 'hidpp' takes at most 0 argument(s), but got 2.
$ ./build-manual/rawaccel-cli hidpp --bogus
rc=1   Command 'hidpp' takes at most 0 argument(s), but got 1.
```
Bilinmeyen **üst** komut da aynı şekilde reddediliyor (`:3105-3109`):
```
$ ./build-manual/rawaccel-cli hidpp-set-dpix /dev/hidraw0 800
rc=1   stderr: "Unknown command: hidpp-set-dpix"   + 101 satırlık help (stdout)
$ ./build-manual/rawaccel-cli hidppbogus
rc=1   stderr: "Unknown command: hidppbogus"        + help
```
**Hüküm: SORU 5'İÇİN BULGU YOK. Bu davranış doğru ve ölçülmüş.**

⚠️ **INFO (aşırı gürültü):** bilinmeyen komutta 101 satırlık `print_help()`
**stdout'a** basılıyor; hata mesajı stderr'da. Bir script `rawaccel-cli <typo>`
çalıştırıp stdout'u JSON olarak ayrıştırmaya çalışırsa help metni gelir. Doğru
yön ayrımı (P115-A5-10 yorumu `:2965`'te aynı sınıftan bir düzeltmeyi anımsatıyor).

⭐ **`cmd_hidpp` içinde asıl sessiz-yeşil riski (SORU 7 ile aynı):**
`cmd_hidpp`'in **hiçbir** dalı `1` dönmüyor:
```
$ awk 'NR>=2297 && NR<=2578 && /return/' cli/main.cpp
  2364:  return s.str();      (lambda — JSON hex üretimi)
  2373:  return s.str();      (lambda — JSON hex üretimi)
  2448:  return 0;            (JSON yolu)
  2453:  return 0;            (hidraw_devices boş)
  2577:  return 0;            (metin yolu)
$ awk 'NR>=2297 && NR<=2578' cli/main.cpp | grep -c "return 1\|return 2\|return 77"
0
```
**`cmd_hidpp` 300+ satırda hiçbir zaman başarısız çıkmıyor.**

────────────────────────────────────────────────────────────────────────
## ⭐ SORU 6 — DONANIM ERİŞİLEMEZSE HER YOL ANLAŞILIR HATA VERİYOR MU?

### Yazma komutları (`set_*`) — ✅ ANLAŞILIR, ÖLÇÜLDÜ
Üçü de aynı kapıdan geçiyor (`cli/main.cpp:2638`, `:2691`, `:2732`):
```cpp
if (!transport.is_open() || transport.vendor_id() != 0x046d) {
    std::cerr << "Unable to open HID++ device: " << args[1] << "\n";
    return 1;
}
```
Ölçülen üç ayrı erişilemezlik yolu — hepsi `rc=1` + mesaj:
```
hidpp-set-dpi /dev/hidraw0 800            rc=1  Unable to open HID++ device: /dev/hidraw0
hidpp-set-dpi /dev/nonexistent-hidraw 800 rc=1  Unable to open HID++ device: /dev/nonexistent-hidraw
hidpp-set-dpi /etc/hostname 800           rc=1  Unable to open HID++ device: /etc/hostname
hidpp-set-dpi /dev/null 800               rc=1  Unable to open HID++ device: /dev/null
hidpp-set-rate /dev/hidraw0 1000          rc=1  Unable to open HID++ device: /dev/hidraw0
hidpp-set-lod  /dev/hidraw0 low           rc=1  Unable to open HID++ device: /dev/hidraw0
```
Arity/validasyon hataları da `rc=1`:
```
hidpp-set-dpi                              rc=1  missing 2 argument(s) + Usage
hidpp-set-dpi /dev/hidraw0                 rc=1  missing 1 argument(s) + Usage
hidpp-set-dpi /dev/hidraw0 99              rc=1  DPI must be an integer from 100 to 32000.
hidpp-set-dpi /dev/hidraw0 800 999         rc=1  Device index must be an integer from 0 to 255.
hidpp-set-dpi /dev/hidraw0 800 256         rc=1  Device index must be an integer from 0 to 255.
hidpp-set-dpi /dev/hidraw0 800 -1          rc=1  Device index must be an integer from 0 to 255.
hidpp-set-lod  /dev/hidraw0 off            rc=1  Lift-off distance must be low, medium, or high.
hidpp-set-lod  /dev/hidraw0 0              rc=1  Lift-off distance must be low, medium, or high.
hidpp-set-lod  /dev/hidraw0 HIGH           rc=1  Unable to open HID++ device   ← büyük harf kabul
```
⛔ **Bu yolların HİÇBİRİ boş çıktı + `0` dönmüyor. SORU 6'nın yazma tarafı TEMİZ.**

**MED — ama teşhis kaybı:** dört farklı durum (yol yok / EACCES / Logitech değil /
`HIDIOCGRAWINFO` başarısız — `src/logitech_hidpp.cpp:620-632`) **tek satırda**
çözülüyor. `errno` hiç yere yazılmıyor. Ölçüm:
```
$ grep -n "EACCES\|EPERM\|errno\|strerror" cli/main.cpp   (2265..3297 aralığı)
(bu aralıkta HİÇBİR errno/EACCES/strerror yok)
```
Karşılaştırma: aynı dosyanın `:209/:228/:397`'i daemon için `permission_denied`
ayrımı yapıyor. **hidraw yolu bu ayrımı yapmıyor.** Kullanıcı `sudo` gerektiğini
anlamıyor.

### Liste komutları (`receivers` / `hidpp`) — ⛔ HAYIR, HAYIR, boş + 0
Bu yolun cevabı SORU 4'te ölçüldü: **"erişilemiyor" ve "yok" ayrımı yapılmıyor,
`return 0` veriliyor.** `cmd_hidpp` ayrıca **hiçbir zaman** `1` dönmüyor (SORU 5).

────────────────────────────────────────────────────────────────────────
## ⭐ SORU 7 — "BAŞARILI" SAYILAN AMA HİÇBİR ŞEY YAPILMAYAN DAL

### 7a. ⭐ `cmd_hidpp` ve `cmd_receivers`: boş liste = başarı (MED)
Yukarıda ölçüldü. `cli/main.cpp:2283` ve `:2453` ve `:2577` → `return 0`.
Bu "sessiz yeşil"in en yaygın hali: **donanım yok / erişim yok → `rc=0`.**
Otomasyonda `rawaccel-cli hidpp` çalıştırıp "başarılı" gören bir betik, hiçbir
cihazı doğrulamadığını sanır.

### 7b. ⭐ ÖLÜ DAL — `cmd_hidpp_set_rate:2697-2701` (dahi **yanlış bir yorum** içeriyor)
```cpp
if (auto rate_info = transport.get_polling_rate(target)) {
    // The device supports some rate, but we need to check if the requested rate is supported
    // We can't easily get the list of supported rates from the transport, so we'll just try to set it
    // and if it fails, we'll show a generic message
}
const bool ok = transport.set_polling_rate(hz, target);
```
`rate_info` **hiç okunmuyor** — ölçüm:
```
$ awk 'NR>=2697 && NR<=2701' cli/main.cpp | grep -c "rate_info"
1        ← yalnız if-init'te bağlanıyor; DEĞERİ 0 kez okunuyor
```
Bu **sessiz yeşil değil ama sessiz pahalı**: bloğun tek işlevi
`transport.get_polling_rate(target)` çağırmak, yani **kullanıcı `hidpp-set-rate`
çalıştırdığında gereksiz bir tam HID++ turu** (`:1927-1984`, en kötü 4 tur × 500 ms
= ~2 sn; `include/logitech_hidpp.hpp:530-532`'deki türetilmiş bütçeye göre
`set_polling_rate` zaten 1200 ms + dallar). Sonucu atılmış bir USB turu.
**Yorumu da yanlış:** "We can't easily get the list of supported rates" —
`set_polling_rate` **zaten** cihazın maskesini soruyor (`src:1994-1998` legacy
`fn 0x0`, `src:2013-2018` extended `fn 0x1`) ve bit maskesi desteklenmeyen hızı
reddeder. Yani yorum, var olan korumayı yokmuş gibi anlatıyor.

### 7c. `--json` hata yolları JSON üretmiyor (MED, otomasyon için)
Üç yazma komutunun `--json` modunda **hata durumunda stdout boş**, stderr düz metin:
```
$ ./build-manual/rawaccel-cli --json hidpp-set-dpi /dev/hidraw0 800
rc=1  stdout=[]  stderr=[Unable to open HID++ device: /dev/hidraw0]
$ ./build-manual/rawaccel-cli --json hidpp-set-dpi /dev/hidraw0 99
rc=1  stdout=[]  stderr=[DPI must be an integer from 100 to 32000.]
```
Aynı şekilde `--json hidpp-set-rate` ve `--json hidpp-set-lod`.
`{"ok":false}` **sadece transport'a ulaştıktan sonra** basılıyor
(`cli/main.cpp:2660`, `:2703`, `:2739`) — yani `"ok":false` göremezseniz hata
**nedeni** de kaybolur. Ölçüm: `grep -n '"ok"\|"error"' cli/main.cpp` →
sadece 2660/2703/2739'da `{"ok",...}`, **hiçbir yerde `{"error":...}` yok.**
Karşılaştırma: `--json -c <bozuk> list` de stdout boş + düz metin stderr →
bu CLI'de **kabul edilmiş bir tutarsızlık**, `--json` sözleşmesi tanımlanmamış.

### 7d. ⭐ Bu lane'in 3 yazma komutu **hiçbir kapıyla test edilmiyor**
Gate 6 (`tests/run_cli_sanitized.sh`) 31 komut çalıştırıyor, **0 tanesi bu lane**:
```
$ grep -cE '^\s*vaka .*(hidpp|receivers)' tests/run_cli_sanitized.sh
0
$ grep -cE '^\s*vaka ' tests/run_cli_sanitized.sh
25      (24 statik + 1 for-döngüsü × 7 preset = 31 çalıştırma)
```
`tests/test_accel.cpp` içinde de:
```
$ grep -rn "set_dpi\|set_polling_rate\|set_lift_off" tests/
(boş)        ← ÜÇ YAZICIYA HİÇBİR TEST ÇAĞRISI YOK
$ grep -rc "hidpp" tests/test_accel.cpp
200         ← POZİTİF KONTROL: grep çalışıyor, sadece bu üçü çağrılmıyor
```
`test_receivers` (`tests/test_accel.cpp:170-200`) yalnızca **mutlu yolu** sınıyor
(3 sahte sysfs girişi → `found.size()==3`). **Erişilemeyen/boş/olmayan sysfs kökü
sınamıyor** — yani SORU 4'teki bulgunun kapısı yok.

Benim lane'imdeki 7 komutu ASan+UBSan altında elle koşturdum — **temiz**:
```
$ build-manual/rawaccel-cli-sanitized (yalnız lane komutları)
  rc=0 san=0  receivers            | No Logitech USB receivers found.
  rc=0 san=0  --json receivers     | []
  rc=0 san=0  hidpp                | No Logitech hidraw devices found.
  rc=0 san=0  --json hidpp         | []
  rc=1 san=0  hidpp-set-dpi /dev/hidraw0 800     | Unable to open HID++ device
  rc=1 san=0  hidpp-set-lod /dev/hidraw0 low     | Unable to open HID++ device
  rc=1 san=0  hidpp-set-rate /dev/hidraw0 1000   | Unable to open HID++ device
```
(`san=0` = `AddressSanitizer|LeakSanitizer|runtime error:` eşleşmesi yok.)
⛔ Bu **gate'in yerine geçmez** — `run_cli_sanitized.sh` bu komutları koşmuyor,
dolayısıyla CI'da bu lane **hiçbir sanitizer kapsamında değil**.

────────────────────────────────────────────────────────────────────────
## BULGULAR (özet tablo)

| ID | dosya:satır | SEVİYE | bulgu |
|---|---|---|---|
| L14-1 | `cli/main.cpp:2627`, `src/logitech_hidpp.cpp:1869` | **HIGH** | `set_dpi` yazma **sonrası doğrulama yok**: `ok` yalnız `send_feature_request(...).has_value()` (= "eşleşen hatasız paket geldi", `src:1904`/`:1917`). Cihaz clamp/yuvarlarsa sessiz yeşil. `get_dpi_info` yalnız yazma **öncesi** çağrılıyor (`src:1870`, sayım=1). `is_hidpp_error` (`src:34-38`) yalnız feature-index `0xFF/0x8F` yakalar, payload durum byte'ı denetlenmez. |
| L14-2 | `src/logitech_hidpp.cpp:2225` + `cli/main.cpp:2451-2453` | **MED** ⭐ | **Canlı kanıt:** `/dev/hidraw0` (major 243) takılı ama `root:root 0600` → `open()` EACCES → `continue` → CLI **"No Logitech hidraw devices found." + rc=0**. "Fare takılı değil" ile "erişim yok" **ayırt edilemiyor**. |
| L14-3 | `src/logitech_receiver.cpp:92-94` + `cli/main.cpp:2281-2284` | **MED** ⭐ | `discover_logitech_receivers` `ec`'yi yutuyor; **ölçüldü:** boş dizin / `chmod 000` / ENOENT → üçü de `0 receiver(s)` (`ec=0 / 13 / 2` doğrulandı). `receivers` boşken `rc=0` + "found" → sessiz yeşil. Aynı dosyada `:209/:228` `permission_denied` ayrımı **yapılıyor** → tutarsızlık. |
| L14-4 | `cli/main.cpp:2297-2578` | **MED** | `cmd_hidpp` **hiçbir dalda `1` dönmüyor** (yalnız 2448/2453/2457→ `return 0`; sıfır `return 1/2/77`). 300+ satır, sıfır hata çıkış kodu. Cihaz bulunsa bile iletişim kurulamazsa `rc=0`. |
| L14-5 | `src/logitech_hidpp.cpp:1869` vs `:1874` | **MED** | `disable_onboard_profiles_for_write()` (cihazı **kalıcı** değiştiren yazma) `set_dpi` içinde **doğrulamadan ÖNCE** çalışıyor. Sonraki `return false` yolunda bile cihaz "On-Board mode"→host mode geçirilmiş olur. Aynı sıra `set_polling_rate:1988`, `set_lift_off_distance:2041`. |
| L14-6 | `cli/main.cpp:2697-2701` | **LOW** (INFO) | **ÖLÜ DAL:** `rate_info` bağlanıyor, değeri **0 kez** okunuyor. Blok tek işlevi gereksiz bir `get_polling_rate()` USB turu (~0.5-2 sn). Yorumu de yanlış: "We can't easily get the list of supported rates" — `src:1994-1998/2013-2018` maskeyi zaten soruyor. |
| L14-7 | `cli/main.cpp:2660/2703/2739` | **MED** | `--json` modunda **hata durumunda stdout boş**, stderr düz metin. `{"ok":false}` yalnız transport'a ulaşınca basılıyor; `--json` sözleşmesi tanımsız (CLI'de hiçbir yerde `{"error":...}` yok). |
| L14-8 | `tests/run_cli_sanitized.sh` + `tests/test_accel.cpp` | **MED** | 3 yazıcıya ve 2 listeleyiciye **hiçbir kapı dokunmuyor**: `grep -cE '^\s*vaka .*(hidpp|receivers)'` → **0**; `grep -rn "set_dpi\|set_polling_rate\|set_lift_off" tests/` → **boş** (pozitif kontrol: `grep -rc hidpp tests/test_accel.cpp` → **200**). CI'da bu lane sanitizer kapsamı dışında. |
| L14-9 | `cli/main.cpp:2712-2743` | **LOW** | `cmd_hidpp_set_lod` mevcut LOD'u hiç okumuyor/raporlamıyor; eski değer kaydedilmiyor. Ancak `hidpp_lift_off_distance` enum'unda (`include/logitech_hidpp.hpp:280-284`) **yalnız low/medium/high var, "off" yok** (`0` firmware'e reddedilen "destek yok" sentineli, `src:1878-1882`) → bu eksik özellik, dönüşü engelleyen kusur değil. |
| L14-10 | `src/logitech_hidpp.cpp:2050-2056` | **INFO (olumlu)** | LOD yazımı DPI'yı **doğru koruyor** (`params[1..4] = info->dpi_current / dpi_current_y`) — "sadece LOD değiştirdim" derken DPI sıfırlanmıyor. Ancak kullanıcıya aynı pakette DPI'nin de yeniden yazıldığı söylenmiyor. |
| L14-11 | `cli/main.cpp:2627` + `:2645` + `src:1874` | **INFO (olumlu)** | DPI aralık koruması **üç katmanlı ve sağlam**: 100–32000 CLI kapısı, cihaz `dpi_levels` ön-kontrolü, transport tekrarı. Ölçülen kabul aralığı 100–32000; `0`/negatif/çok büyük/hex/`1e3` reddediliyor. **Geçersiz DPI donanıma ulaşmıyor.** |
| L14-12 | `cli/main.cpp:3028` + `:3070-3073` | **INFO (olumlu)** | Bilinmeyen `hidpp` alt komutu **reddediliyor** (`rc=1` + Usage), sessiz `0` yok. SORU 5'in cevabı: bulgu yok. |
| L14-13 | `cli/main.cpp:2581` | **INFO** | `parse_hidpp_uint` `+100` ve `" 100"`'ı kabul ediyor (`strtoul` baştaki `+`/boşluk atlar; yalnız `-` reddediliyor). Değer 100'e normalize olduğu için **zararsız**. |

**Kritik (CRIT) bulgu yok.** Yazma komutlarının **fail-closed** olması (donanıma
geçersiz değer ulaşmaması, yazılamıyorsa `rc=1`) ve liste komutlarının
`hidpp` alt komutlarını reddetmesi, lane'in en kritik risklerini kapatıyor.
Kalan asıl risk **sessiz yeşil** sınıfıdır: yazma sonrası doğrulamanın olmaması
(L14-1) ve erişilemezliğin "cihaz yok" gibi basılması (L14-2/L14-3).

────────────────────────────────────────────────────────────────────────
KAPI     : 
  * `bash tests/run_cli_sanitized.sh` (kanonik 6. kapı) → **rc=0**,
    "=== Sonuç: 31/31 komut gerçek kodu çalıştırdı ===" /
    "=== Sonuç: PASS — CLI ASan/UBSan altında temiz (31/31 komut) ===".
    ⛔ **Bu 31'in 0 tanesi bu lane'in komutu** (bkz. L14-8) — yeşil, kapsam dışı.
  * Elle sanitizer koşumu (lane'in 7 komutu, `rawaccel-cli-sanitized` ile):
    7/7 komut çalıştı, `san=0` (ASan/LSan/UBSan ihbarı yok), rc'ler 0/0/0/0/1/1/1.
  * Ölçüm sayıları üretildi: 
    - kabul edilen DPI değerleri: **4** (`100`,`101`,`1600`,`32000`)
    - reddedilen DPI girdisi: **13** (ölçülen listeden)
    - `cmd_hidpp` içinde sıfır-dışı `return`: **0**
    - `receivers` keşfi: 3 farklı durum → **3/3** aynı `0 receiver(s)`
    - `ec` değerleri: **0 / 13 / 2**
    - `rate_info` değer okuması: **0**
    - `set_dpi` içinde `get_dpi_info`: **1** (hepsi yazma öncesi)
    - `set_polling_rate`/`set_lift_off_distance` içinde okuma-yok: **0 / 0**
    - kapıdaki hidpp/receivers vakası: **0**
    - `tests/` içinde üç yazıcı çağrısı: **0** (pozitif kontrol `hidpp`: **200**)
    - `/dev/hidraw0` mevcut: **1** (major 243, ama EACCES → listede görünmüyor)

KAPSANMAYAN: 
  * `cli/main.cpp` **1–2264** (başka ajanların): `cmd_list`, `cmd_show`, `cmd_set`,
    `cmd_set_param`, `cmd_import`, `cmd_monitor`, `cmd_status_json`, `cmd_status`,
    `validate_config_path`, `print_signal_failure`, `daemon_ipc_query` — **dokunulmadı**.
  * `cli/main.cpp` **2939–3297**'deki `main()` yalnız **okundu**: bilinen-komut
    reddi (`:3105-3109`), arity tablosu (`:3011-3037`), bilinmeyen-komut
    config-yan etkisi (P130-R49) **başka lane'in**; buradan ölçtüm, düzeltme önermedim.
  * `src/logitech_hidpp.cpp` ve `include/logitech_hidpp.hpp` **başka ajanların** —
    sadece okundu. L14-1 ve L14-5'in kökü bu dosyalarda; **düzeltme önermem**
    (brifing §1.4).
  * GUI'nin aynı yazma yolları (`gui/hidpp_panel.inl:586` `set_lift_off_distance`)
    başka lane'in; sadece `grep` ile varlığını doğruladım.
  * `daemon/daemon.cpp` `drain_hidpp_writes()` / `hidpp_hw_*` kuyruğu — **başka lane'in**.
    L14-1'in CLI'ya etkisi ölçüldü; daemon tarafının doğrulama davranışı ölçülmedi.
  * `--json` çıktısının **başarılı** hali (donanım yokken basılamıyor) ölçülemedi.

TEMSIL SINIRI:
  * ⛔ **"Cihaz yanıt verir ama değeri clamp/yuvarlarsa sessiz yeşil"** senaryosu
    **ÖLÇÜLEMEDİM.** Bu konakta Logitech HID++ donanımı yok ve `/dev/hidraw0`
    `root:root 0600`; root değilim (`uid=1000`), `sudo` yok, `/dev/uinput` yok.
    `HidppTransport` ctor'ı `open(O_RDWR)` + `ioctl(HIDIOCGRAWINFO)` gerektiriyor
    (`src/logitech_hidpp.cpp:615-632`) ve **gerçek bir hidraw düğümü olmadan
    sahte bir transport üretilemiyor** (ioctl sıfır dışı dosyada başarısız →
    fd kapatılıyor → `is_open()==false`). Bu yüzden L14-1 bir **kod-yolu
    kanıtıdır** (`has_value()`in tek kriter olması, okuma-yok sayımı = 0),
    **çalışma anı kanıtı değildir.** Donanımlı bir turda `hidpp-set-dpi 32000`
    sonrası `hidpp`'nin `dpi.current` alanıyla karşılaştırılmalı.
  * ⛔ **Cihazın `dpi_levels` listesi hiç gözlenmedi.** DPI adım listelerinin
    gerçekten 200/400/800/1600/3200 biçiminde olduğu bu konakta doğrulanamaz;
    ölçülen şey **CLI'nin 100..32000 kapısı ve listede-olmayanı reddetme mantığı**.
  * ⛔ `hidpp-set-rate`'in `get_polling_rate` ölü turunun **gerçek süresi**
    ölçülmedi (tür atılmış; `include/logitech_hidpp.hpp:530-532`'deki türetilmiş
    bütçeden ~0.5–2 sn tahmini — **bunu süre olarak ÖLÇMEDİM, yazma yolundan
    geçmediği için gerçekten çalışmıyor da olabilir**; yalnız "çalışsa bile
    sonucu atılıyor" yapısal olarak kesin).
  * ⛔ `cmd_hidpp`'in **dolu listede** (cihaz bulunmuş ama iletişim kurulamamış)
    metin/JSON çıktısı ölçülmedi — o zaman `identify_logitech_devices` başarısız
    olmalı, ki bu da bir hidraw cihaz gerektirir. Bulgu L14-4 yalnız
    **statik** kanıta dayanıyor (sıfır `return 1/2/77`), çalıştırma kanıtı yok.
  * ⛔ `print_help()` 101 satırının **stdout**'a basılması ölçüldü (bilinmeyen komut
    testinde), ama bunun bir **script'i bozduğu** ölçülmedi — etkisi makul.
  * ASan/UBSan altındaki lane koşumu **elle** yapıldı; bu 7 komut CI'da
    sanitizer altında **çalıştırılmıyor** (L14-8).
  * ⛔ Hiçbir kaynak dosya değiştirilmedi. `git status --porcelain` sonrası:
    yalnız `.aihaberlesme/**` (diğer ajanların logları + bu rapor) listelendi;
    `cli/main.cpp`, `src/*.cpp`, `include/**`, `tests/**` **temiz**.
    Mutasyon deneyi `/tmp/opencode/l14/` kopyası üzerinde yapıldı
    (brifing §4.1).