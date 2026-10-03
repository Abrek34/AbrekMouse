### L07 | alt-ajan (cihaz kimlik tanıma / alıcı keşfi) | 2026-10-01

KAPSAM   : `include/logitech_quirks.hpp` (267 satır), `src/logitech_receiver.cpp` (117 satır)
           — SADECE OKUNDU, DEĞİŞTİRİLMEDİ. Referans (okuma): `src/logitech_hidpp.cpp`,
           `include/logitech_hidpp.hpp`, `include/logitech_receiver.hpp`, `cli/main.cpp:2265-2295`,
           `gui/hidpp_panel.inl:147-152`, `tests/test_accel.cpp:162-208, 724-823`.

⛔ ÇALIŞMA AĞACI BOZULMADI — kanıt:
```
$ git status --porcelain -- include/ src/ tests/ daemon/ cli/ gui/ scripts/
(boş — lane dosyaları dahil HİÇBİR şey değişmedi)
$ git status --porcelain
 M .aihaberlesme/GOREV_TAHTASI.md      <- başka ajanlar
 M .aihaberlesme/mesajlar/*.log        <- başka ajanlar
?? .aihaberlesme/DENETIM-BRIFING.md
?? .aihaberlesme/mesajlar/denetim-L0*.md ... (başka lane'ler)
```
Mutasyon testlerinin tamamı `/tmp/opencode/L07/mut` kopyasında yapıldı
(`cp` ile kopyalandı, `git checkout/stash/reset` hiç çalıştırılmadı).

⚠️ METODOLOJİK UYARI — KENDİ SORGUMUNU DÜZELTTİM:
Bu makinede `LANG=tr_TR.UTF-8`. Türkçe locale'te `grep -oE '...::[a-zA-Z_0-9]+'`
`logitech_receiver_kind::lightspeed` yerine **`logitech_receiver_kind::l`** üretti
(kırpma). Bu, tam olarak brifing §2'de yasaklanan "0 eşleşme" hatasının tersidir:
sorgu değişince SAYI değişir, kod değişmemiştir. **Aşağıdaki TÜM negatif
grep'ler `LC_ALL=C` ile yeniden doğrulandı** ve her birine pozitif kontrol eşlik ediyor.

---

## SORU 1 — Kaç cihaz tanımlı? Tam eşleme. Çakışma var mı?

**3 cihaz** (`LOGITECH_QUIRKS`). Çakışma **YOK**.

Kanıt — çalıştırılmış prob (başlık tek başına derlendi; gizli link bağımlılığı yok):
```
=== A) TABLE ENUMERATION (runtime) ===
LOGITECH_QUIRKS.size() = 3
  [0] model_id="4099C0950000" len=12  nvconfig_rows=1 headset_rows=0  [nv 0x0001:]
  [1] model_id="B38940B4C355" len=12  nvconfig_rows=2 headset_rows=0  [nv 0x0001: color1 color2]  [nv 0x0040: color1 color2]
  [2] model_id="32"         len=2   nvconfig_rows=0 headset_rows=2  [hs 0: color1]  [hs 1: color1 color2]

=== B) DUPLICATE KEY SCAN ===
  "32" x1
  "4099C0950000" x1
  "B38940B4C355" x1
duplicate keys = 0 ; distinct keys = 3 ; rows = 3
```

Tam model id → cihaz adı eşlemesi:

| # | model id | uzunluk | Cihaz adı | Satır | Satır içeriği |
|---|----------|---------|-----------|-------|---------------|
| 0 | `4099C0950000` | 12 | **G502 X PLUS** | `:91-92` | nvconfig: `0x0001` (startup), alan listesi **BOŞ** (sadece On/Off) |
| 1 | `B38940B4C355` | 12 | **G515 LIGHTSPEED TKL** | `:95-98` | nvconfig: `0x0001` (startup) + `0x0040` (shutdown), ikisi de `color1,color2` |
| 2 | `32` | **2** | **G522 LIGHTSPEED (Centurion)** | `:122-125` | headset 0x0622: slot 0 = `color1`, slot 1 = `color1,color2`; nvconfig boş |

Çakışma ölçümü (3 ayrı yöntem, hepsi aynı sonuç):
```
$ LC_ALL=C grep -o 't\.push_back({"[^"]*"' include/logitech_quirks.hpp | sed ... | sort | uniq -c | sort -rn
      1 B38940B4C355
      1 4099C0950000
      1 32                       -> her anahtar tam 1 kez
$ LC_ALL=C grep -n 't\.push_back' include/logitech_quirks.hpp   -> :91, :95, :122 (3 satır)
```
**Alıcı tablosu (`logitech_receiver.cpp`) — 25 alıcı, çakışma yok:**
```
declared: array<receiver_spec, 25>
actual rows: 25                      -> bildirim ile gerçek satır sayısı UYUŞUYOR
$ LC_ALL=C grep -o '{0x[0-9a-f]*' src/logitech_receiver.cpp | sed 's/{//' | sort | uniq -c
  -> hepsi 1 (0xc517, 0xc518, 0xc51a, 0xc51b, 0xc521, 0xc525, 0xc526, 0xc52e,
              0xc52f, 0xc52b, 0xc532, 0xc531, 0xc534, 0xc535, 0xc537, 0xc542,
              0xc539, 0xc53a, 0xc53d, 0xc53f, 0xc541, 0xc545, 0xc547, 0xc54d, 0xc548)
kind dağılımı: nano 14, lightspeed 9, unifying 3, bolt 2, legacy_27mhz 2   (= 25)
```

### L07-01 | `include/logitech_quirks.hpp:77-80` | **MED** — cihaz adı YALNIZCA yorumda
Model id → cihaz adı eşlemesi **makine tarafından okunamıyor**; struct'te isim alanı yok.
Bu yüzden "hangi cihaz destekleniyor" sorusu çalışma anında cevaplanamaz — bkz. L07-08.
```
$ LC_ALL=C sed -n '77,80p' include/logitech_quirks.hpp
struct logitech_quirks_entry {
    const char* model_id;   // composited modelId, upper-case hex
    logitech_quirks quirks;
};
$ LC_ALL=C grep -c 'const char\* name' include/logitech_quirks.hpp
0                                    <- pozitif kontrol değil: alan gerçekten yok
Cihaz adları yalnızca yorum satırlarında: :89, :93, :99
```

---

## SORU 2 — ⭐ Kimlik eşlemesi duyarlı mı? Kısmi eşleştirme var mı?

### CEVAP: **EVET, duyarlıdır.** `strstr`/`strncmp`/önek/alt-dizgi eşleştirmesi **YOK**.
Eşleştirme `std::string::operator==` ile **birebir** — `include/logitech_quirks.hpp:131`.

```
$ LC_ALL=C sed -n '129,133p' include/logitech_quirks.hpp
inline const logitech_quirks* find_logitech_quirks(const std::string& model_id) {
    for (const auto& entry : LOGITECH_QUIRKS)
        if (model_id == entry.model_id)      <-- std::string::operator==, tam eşitlik
            return &entry.quirks;
```

Kısmi eşleştirme yapısı **hiç yok** (negatif grep + pozitif kontrol):
```
$ LC_ALL=C grep -nE 'strstr|strncmp|strncasecmp|strcasecmp|\.find\(|\.rfind\(|\.substr\(|\.compare\(|starts_with|ends_with' include/logitech_quirks.hpp
rc=1                                        -> 0 eşleşme
POZİTİF KONTROL (aynı desen, bu desende EŞLEŞMESİ gereken dosya):
$ LC_ALL=C grep -cE 'strstr|strncmp|\.find\(|\.rfind\(|\.substr\(|\.compare\(|starts_with' src/logitech_hidpp.cpp
5                                           -> sorgu çalışıyor, 0 gerçek bir sıfır
```

### Ölçüm — düzenleme uzaklığı (edit distance) taraması
Kimliğin kısmi eşleşmeyle "kayabileceği" tek yol, anahtara 1-2 karakter mesafede
olmaktır. Her iki anahtar için **tüm** komşular tarandı:
```
=== ED1 (tüm ikame/ekleme/silme komşuları, kimlik dışı bırakıldı) ===
  key=4099C0950000  ed1_probes=384   hits=0
  key=B38940B4C355  ed1_probes=387   hits=0
  key=32            ed1_probes=78    hits=0
ED1 TOTAL probes=1014 FALSE HITS=0
=== ED2 (hepsiz 0/1/2-düzenleme komşusu, hex alfabesinde) ===
  key=4099C0950000  ed2_probes=68952    hits=0
  key=B38940B4C355  ed2_probes=71285    hits=0
  key=32            ed2_probes=2629     hits=0
ED2 TOTAL probes=142866 FALSE HITS=0
POZİTİF KONTROL:  "4099C0950000"->HIT  "B38940B4C355"->HIT  "32"->HIT
```
**Toplam 143 880 kısmi-eşleşme probu, 0 yanlış eşleşme.** Eşleşme duyarlıdır.

### Somut örnek talebi — "Logitech olmayan bir cihaz yanlış tanınır mı?"
Ölçülen cevap: **hayır, tanınmaz.** Gerçek ve düşmanca niyetli girdiler:
```
=== D) NON-LOGITECH / REAL-OTHER-BRAND device strings ===
  "Logitech G Pro X Superlight 2" -> nullptr      (gerçek Logitech, tabloda değil)
  "Logitech USB Receiver"        -> nullptr      (alıcı adı, model id değil)
  "000000000000"                 -> nullptr
  "B338B0B7B041" / "B381B0B8B041"-> nullptr
  "FFFFFFFFFFFF"                 -> nullptr      (biçim doğru, cihat yok)
  "ZZZZZZZZZZZZ"                 -> nullptr      (12 karakter, hex DEĞİL)
  "4099C0950000 "                -> nullptr      (son boşluk -> ısınmadı)
  " 4099C0950000"                -> nullptr      (ön boşluk -> ısınmadı)
  ""                             -> nullptr
  "RAZER Viper V3 Pro"           -> nullptr
```
Yani **yanlış-tanıma riski ölçülmüş olarak sıfır**; ters yön (eşleşmemek) üstün
güvenli davranış, `nullptr` = "bu model için doğrulanmış satır yok".

### ⭐ L07-02 | `include/logitech_quirks.hpp:131` | **INFO (olumlu)** — duyarlılık kazara değil, kapıyla korunuyor
Bu bir tesadüf değil: eşleştirmeyi kısmi yapan bir mutasyon **testleri kırmızıya
çeviriyor** (kopya üzerinde ölçüldü).
```
MUTASYON C: find_logitech_quirks() -> iki yönlü önek/alt-dizgi karşılaştırması
$ ./t_mutC --filter 'O31-H2'  ->  Sonuç: 11/27 geçti, 16 BAŞARISIZ   (exit 1)
$ ./t_mutC                     ->  Sonuç: 34164/34180 geçti, 16 BAŞARISIZ (exit 1)
16 hatanın HEPSİ tek bir assertion'dan: tests/test_accel.cpp:804
   q != &LOGITECH_QUIRKS[2].quirks   (section: O31-H2 — the "32" row is unreachable...)
```
**Dürüstlük notu:** bu 16 hata *tek* bir assertion kaynaklı. Yani kısmi eşleştirmeyi
yakalayan şey, genel bir "yakın-vuruş eşleşmemeli" kuralı değil, `32` satırının
erişilemezliğini koruyan `:804`'tür. İki **gerçek** anahtarın (12 karakterlik) kısmi
eşleşmeye karşı doğrudan bir negatif assertion'ı yok — onları yalnızca dolaylı olarak
koruyor. Bugün zararsız (eşleştirme zaten tam), ama koruma incedir.

---

## SORU 3 — ⭐ `test_logitech_quirks_model_id_shape` gerçekten bozuk biçimleri reddediyor mu?

### CEVAP: HAYIR — ve bu testin belgelediği sav tam olarak yanlış. Bir **sessiz yeşil** bulgusu.

Testin kendi yorumu (`:735-737`) şunu iddia ediyor:
> *"This test is the guard rail: if the id parsing is ever changed so that a short id
> becomes reachable, these assertions fail"*

**Ölçüm — id üretimini değiştir, kapı kırmızıya dönsün mü?**

Sorun: test, `hidpp_device_info` nesnesini **elle kuruyor** (`:751` `info.model_id =
"000000000000"`), gerçek ayrıştırıcıyı (`src/logitech_hidpp.cpp:1397`) **hiç çağırmıyor**.
Dolayısıyla 12 karakterlik model id'yi üreten tek gerçek satır bozulduğunda test bunu
göremez. Kopya üzerinde ölçüldü:

```
MUTASYON A — GERÇEK AYRIŞTIRICI bozuldu (kısa id üretsin diye):
  logitech_hidpp.cpp:1397
  -  info.model_id = bytes_to_hex(count->data() + 7, 6);
  +  info.model_id = bytes_to_hex(count->data() + 7, 1);   // MUTANT: 2 karakterlik kısa id

$ ./t_mutA --filter 'O31-H2'  ->  Sonuç: 11/11 geçti      (exit 0)   <-- HÂLÂ YEŞİL
$ ./t_mutA  (TAM SUITE)      ->  Sonuç: 34164/34164 geçti (exit 0)   <-- HÂLÂ YEŞİL
```
**Gerçek ayrıştırıcı 12 karakterlik model id'yi 2 karakterlik kısa id'ye dönüştürdüğü
halde, 34 164 assertion'ın tamamı yeşil.** Testin "koruyacağım" dediği durum tam olarak
bu. Bu, brifing §3'teki "bir test yazılmamış ama kapı yeşil" sınıfıdır ve `HIGH`'tir.

### Sınır tam olarak nerede? Karşı-mutasyon ile ölçüldü
```
MUTASYON B — testin ÇAĞIRDIĞI fonksiyon (logitech_quirks.hpp:259) bozuldu:
  -      return info.model_id;
  +      return info.model_id.substr(0, 2);  // MUTANT: kısa anahtara kırp

$ ./t_mutB --filter 'O31-H2'  ->  Sonuç: 5/27 geçti, 22 BAŞARISIZ  (exit 1)  <-- KIRMIZI
```
**Sonuç:** test `logitech_compose_model_id()`'i **kapsıyor** (koruma çalışıyor), ama
`info.model_id`'yi **üreten** `logitech_hidpp.cpp:1397`'yi **kapsamıyor** (koruma yok).
`compose()` katmanı ile ayrıştırma katmanı arasındaki bu boşluk, O31-H2 kararının
dayandığı tek kanıttır — ve kanıt eksik.

### İkinci yarı: biçim doğrulaması hiç yok — "şekil doğru, içerik geçersiz" kabul ediliyor
`logitech_compose_model_id()` (`:258-266`) **hiçbir doğrulama yapmıyor**; `info.model_id`
dolguyla geri verir. Ölçüldü:
```
=== Q3: does compose() VALIDATE shape, or pass it through? ===
  model_id='ZZZZZZZZZZZZ'   len=12  compose()->'ZZZZZZZZZZZZ'    -> VERBATIM
  model_id='NOT-A-MODEL-ID' len=14  compose()->'NOT-A-MODEL-ID'  -> VERBATIM
  model_id='!!!!'           len=4   compose()->'!!!!'            -> VERBATIM
  model_id='4099C09500000'  len=13  compose()->'4099C09500000'   -> VERBATIM
  model_id='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' len=30 -> VERBATIM
rejected=0 of 5     -> 5'inin 5'i de bozuk biçim, 0'i reddedildi
```

### L07-03 | `tests/test_accel.cpp:739-777` (koruyan `:802-805`) | **HIGH** — gerçek ayrıştırıcı kapsanmıyor
Yukarıdaki MUTASYON A kanıtı. Etki: O31-H2 "32 satırı erişilemez" kararının **dayanağı
test tarafından doğrulanmıyor**; `logitech_hidpp.cpp:1397` bir gün kısa id döndürmeye
başlarsa (mutasyon A tam olarak bunu yaptı) `32` satırı canlanır ve yeşil kapı bunu
söylemez. Not: `logitech_hidpp.cpp` L06'nın sahibi olduğu için **düzeltme L06 ile
koordinasyon gerektirir**; burada yalnızca kapı boşluğu raporlanıyor.

### L07-04 | `include/logitech_quirks.hpp:258-266` | **LOW** — biçim doğrulaması yok
`compose()` uzunluk/hex kümesi doğrulamaz. Bugün **zararsız**: geçersiz biçimli id
tablodaki hiçbir anahtarla eşleşmediği için `nullptr` döner (yani yanlış-tanıma değil,
doğru "bilinmiyor" cevabı — L07-02'nin ölçümü bunu doğruluyor). Yine de testin
adı "model_id_shape" olduğu halde **şekli reddetmiyor, yalnızca üretilen şekli
sayar** (`:766-776`: `produced_12`/`produced_empty`/`produced_other` sayaçları).
Gerçek `hidpp_device_info` girdisine hiç uygulanmıyor.

### L07-05 | `tests/test_accel.cpp:802-822` | **MED** — negatif vaka (bilinmeyen → nullptr) hiç test edilmiyor
Dört kullanım yerinin **hiçbiri** "tabloda olmayan bir model id `nullptr` döner" diye
söylemiyor:
```
$ LC_ALL=C grep -n 'find_logitech_quirks' tests/test_accel.cpp
802:  const logitech_quirks* q = find_logitech_quirks(info);
813:  EXPECT(find_logitech_quirks(info) != nullptr);            <- pozitif
814:  EXPECT(find_logitech_quirks(info) == find_logitech_quirks(std::string(key)));  <- pozitif
822:  EXPECT(find_logitech_quirks(std::string("32")) != nullptr); <- pozitif
$ LC_ALL=C grep -c 'find_logitech_quirks.*nullptr' tests/test_accel.cpp
2                                                            -> ikisi de '!= nullptr'
```
"Desteklenmeyen cihaz nullptr alır" davranışı — yani alttaki **en kritik** kullanıcı
yolu — hiçbir assertion ile sabitlenmemiş.

### L07-06 (olumlu) | `tests/test_accel.cpp:810-815` | **INFO** — ölü/yanlış anahtar yakalanıyor
Gerçekçi bir yazım hatası (büyük/küçük harf) sessiz kalmıyor:
```
MUTASYON E: anahtar "B38940B4C355" -> "b38940b4c355" (satır ölür, asla eşleşemez)
$ ./t_mutE  ->  Sonuç: 34163/34164 geçti, 1 BAŞARISIZ   (exit 1)   <-- YAKALANDI
```
Not: anahtarlar büyük harfli ve `bytes_to_hex` de `std::uppercase` kullanıyor
(`src/logitech_hidpp.cpp:115`), yani küçük-harfli id üretilmiyor. Buna rağmen test
küçük-harf mutasyonunu yakalıyor — çünkü `:813` doğrudan iki gerçek anahtarı zorlar.

### L07-07 (olumlu) | `include/logitech_quirks.hpp:258-266` | **INFO** — büyük/küçük harf yönü ölçüldü
Eşleştirmenin büyük/küçük harf duyarsızlaştırılması bir mutasyonla denendi:
```
MUTASYON D: karşılaştırmayı büyük harfe normalize et
$ ./t_mutD  ->  Sonuç: 34164/34164 geçti  (exit 0)      <-- testler bunu YAKALAMIYOR
```
**Yine de düşük önem:** üretilen id her zaman büyük harfli (`bytes_to_hex`,
`src/logitech_hidpp.cpp:113-119`), dolayısıyla bu mutasyon pratikte zararsız
görünüyor. Rapora "eksik kapı" olarak yazıyorum, "hata" olarak değil.

---

## SORU 4 — `src/logitech_receiver.cpp`: alıcı keşfi `/dev/hidraw*` tarayarak mı?

### CEVAP: **HAYIR.** Sysfs tarar (`/sys/bus/usb/devices`). Hata durumunda **BOŞ LİSTE** döner — hata değil.

```
$ LC_ALL=C grep -n 'hidraw' src/logitech_receiver.cpp
rc=1                                          -> 0 eşleşme
POZİTİF KONTROL (aynı desen, hidraw'ı kullanan dosya):
$ LC_ALL=C grep -c 'hidraw' include/logitech_hidpp.hpp
11                                            -> sorgu çalışıyor
$ LC_ALL=C grep -n 'sysfs_root\|/sys/' include/logitech_receiver.hpp src/logitech_receiver.cpp
include/logitech_receiver.hpp:33: /// Enumerate Logitech USB receivers from sysfs. `sysfs_root` is injectable so
include/logitech_receiver.hpp:36: discover_logitech_receivers(const std::string& sysfs_root = "/sys/bus/usb/devices");
src/logitech_receiver.cpp:90: discover_logitech_receivers(const std::string& sysfs_root) {
src/logitech_receiver.cpp:93:  fs::directory_iterator it(sysfs_root, ec), end;
```

### Projede İKİ ayrı keşif yolu var (ve bu, "alıcı keşfi hidraw mı" sorusunun cevabı)
| Fonksiyon | Dosya | Ne tarar | Ne döner | Hata halinde |
|---|---|---|---|---|
| `discover_logitech_receivers()` | `src/logitech_receiver.cpp:90` | `/sys/bus/usb/devices/*` (idVendor/idProduct/product) | `vector<logitech_receiver_info>` (VID/PID → kind/slots/hidpp bayrağı) | **boş liste** |
| `discover_logitech_hidraw_devices()` | `src/logitech_hidpp.cpp:2210` | `/dev/hidraw*` karakter cihazları, `open()`+`ioctl(HIDIOCGRAWINFO)`, vendor `0x046d` | `vector<std::string>` (yol listesi) | **boş liste** |

`logitech_receiver.cpp` **hiçbir hidraw düğümü açmaz, hiçbir vendor komutu göndermez** —
bu, `include/logitech_receiver.hpp:8-11`'de bilinçli bir tasarım kararı olarak yazılı
("discovery-only layer … never opens a hidraw node or sends a vendor command"). Yani
`receivers` komutu bir **alıcı-çipi** listeler, `hidpp` komutu **cihaz düğümlerini** listeler.

### Hata mı, boş liste mi? → **BOŞ LİSTE, ve hata sinyali hiç taşınmıyor**
Dönüş tipi `std::vector<...>`; `error_code`, `bool` veya `errno` çıkış parametresi **yok**.
Ölçüldü:
```
=== D) NONEXISTENT sysfs_root -> error or empty list? ===
  discover("/nonexistent/L07/...")  -> size=0
  indistinguishable from a machine with zero Logitech receivers? size==0 both ways: YES
=== E) sysfs_root that is a FILE, not a directory ===
  discover("/etc/hostname")        -> size=0
```
`:93` `std::error_code ec` alıyor ama `:94` `while (!ec && it != end)` ile **sessizce
döngüye hiç girmiyor** — `ec` ne bir yere yazılıyor ne de çağırana taşınıyor.

### ⭐ L07-08 | `src/logitech_receiver.cpp:89-115` | **HIGH** — "alıcı yok" ile "tarama başarısız" **ayırt edilemiyor**
Bulgu, `cli/main.cpp:2281-2284` ile **kullanıcıya** ulaşıyor:
```
cli/main.cpp:2281    if (receivers.empty()) {
cli/main.cpp:2282        std::cout << "No Logitech USB receivers found.\n";
cli/main.cpp:2283        return 0;                      <-- SIFIR = BAŞARI
cli/main.cpp:2284    }
```
Gerçek ikili ile ölçüldü (bu makinede):
```
$ timeout 20 ./build-manual/rawaccel-cli receivers
No Logitech USB receivers found.
EXIT=0
$ timeout 20 ./build-manual/rawaccel-cli --json receivers
[]
EXIT=0
```
Sistemde `/sys/bus/usb/devices` **var ve okunabilir** (8 giriş), iki USB aygıtı
`cat` ile okunabildi (`idVendor=0e0f`, `1d6b`); **dürüstlük notu:** bu makinede
`grep -rl 046d /sys/bus/usb/devices/` **0 sonuç** verdi, yani Logitech alıcısı
**gerçekten yok** — buradaki mesaj doğru. Ama **aynı mesaj ve aynı exit 0**, tarama
tamamen başarısız olduğunda da üretilir. Kullanıcının ayırt edeceği tek fark: yok.
JSON çıktıda hata alanı hiç yok (`[]`), yani bir betik de ayırt edemez.
Bu, brifing §3'teki "sessiz yeşil"in taşınmış hâli: `exit 0` + anlaşılır olmayan mesaj.

---

## SORU 5 — ⭐ Erişim reddi ile "cihaz yok" ayrılıyor mu?

### CEVAP: **HAYIR — üstelik ayrılamaz olacak şekilde.** `open`/`stat` hatası sessiz `continue`'dur.

Hidraw yolundaki kritik sıralama, `src/logitech_hidpp.cpp:2224-2232`:
```
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;                       <-- EACCES burada, ÇİFTİT YOK
    ...
    if (ioctl(fd, HIDIOCGRAWINFO, &info) >= 0) {
        if (info.vendor == 0x046d) {            <-- vendor kontrolü ASLA ULAŞILMAZ
```
`open()` **vendor kontrolünden ÖNCE** gelir. Bir düğümü açma izni yoksa kod o düğümün
Logitech olup olmadığını **hiç öğrenemez**; bu yüzden "erişim reddi" ile "Logitech değil"
aynı `continue` ile aynı kola düşer.

### Kanıt 1 — bu makinede GERÇEK bir EACCES ölçüldü
`/dev/hidraw0` bu makinede **0600 root**; kullanıcı uid 1000:
```
crw------- 1 root root 243, 0 /dev/hidraw0
$ head -c 1 /dev/hidraw0 >/dev/null 2>&1 ; echo "open rc=$?"
open rc=1                                     (EACCES)
```
Döngünün enstrümente edilmiş kopyası (gövde `:2213-2235`'ten **birebir** alıntı,
her karar noktasına yazdırma eklendi; çıktı gerçek fonksiyonla karşılaştırıldı):
```
=== A) THE REAL FUNCTION on /dev ===
  discover_logitech_hidraw_devices() -> 0 devices
=== B) SAME LOOP, INSTRUMENTED ===
  /dev/hidraw0   open() FAILED errno=13 (Permission denied)  -> `continue` (SILENT DROP)
                   ^ vendor id NEVER CHECKED. The code cannot know
                     whether this was a Logitech receiver it was denied.
=== C) BRANCH ACCOUNTING ===
  char-device entries seen in /dev .................. 1
  skipped: not a character file .................... 29
  skipped: name does not start with 'hidraw' ....... 160
  *** SILENTLY DROPPED: open() failed ............... 1  <== EACCES yolu
  dropped: ioctl ok but vendor != 046d ............. 0
  ADDED to the result list ......................... 0
  replica result size = 0 ; REAL function size = 0 ; agree = YES
```
**Dürüstlük notu (kanıt sınırı):** bu makinedeki hidraw0 bir VMware faresidir
(`HID_ID=0003:00000E0F:00000003`, üst USB aygıtı `idVendor=0e0f`) — yani Logitech
**değildir** ve mesaj burada tesadüfen doğrudur. **Elimde Logitech donanım yok; bu
makinede "Logitech alıcısı izin yüzünden gizlendi" senaryosunu çalıştırarak
gösteremedim.** Ölçtüğüm şey şudur: **EACCES dalı gerçekten çalışıyor** (errno=13
ölçüldü, vendor kontrolüne hiç ulaşılmadı) ve kullanıcıya giden mesaj bunu **hiç
ayırt etmiyor**. Aynı anda 0 hidraw düğümü için vendor bile incelenmedi.

### Kanıt 2 — sysfs tarafı: enjekte edilen ağaçla, kontrollü A/B
Altı adet **byte-ça byte aynı** Logitech alıcısı (046d:c53f); tek fark `idProduct`:
```
=== SIX otherwise-IDENTICAL Logitech receivers (046d:c53f) ===
  a_readable  normal, 0646            -> size=1 0xc53f      <- KONTROL: cihaz bulunuyor
  b_noread    EACCES (mode 0000)      -> size=0
  c_absent    idProduct file ABSENT   -> size=0
  d_empty     idProduct EMPTY (0 byte) -> size=0
  e_ws        c53f + newline           -> size=1 0xc53f
  f_junk      c53fZZ (hex + artık)     -> size=0
  g_upper     C53F büyük harf          -> size=1 0xc53f
  h_nlonly    yalnızca newline         -> size=0
  KONTROL a_readable size=1 (beklenen 1) -> OK

=== C) IS EACCES DISTINGUISHABLE FROM 'NO DEVICE'? ===
  b_noread size=0  vs  c_absent size=0  -> IDENTICAL
  b_noread size=0  vs  d_empty  size=0  -> IDENTICAL
  b_noread size=0  vs  e_dir    size=0  -> IDENTICAL
  b_noread == c_absent ? YES: the caller cannot tell them apart
```
Yani `discover_logitech_receivers()` için: **izin yok ≡ dosya yok ≡ dosya boş ≡
dizin bile değil** — dördü de `size=0`.
(`f_junk`'in düşmesi doğru ve sıkı davranış: `src/logitech_receiver.cpp:67`
`ec == std::errc{} && end == last`. `g_upper`'in geçmesi de doğru: `from_chars`
büyük harf kabul ediyor. Gerçek sysfs satır sonu `\n` ile bittiği için `e_ws` çalışıyor.)

### Kanıt 3 — `errno` **güvenilir** bir sinyal değil (ve bu ölçüldü)
`errno` çağrıdan sonra **EACCES'i** gösteriyor, ama aynı çağrı içindeki sonraki başarılı
sistem çağrısı onu **üzerine yazıyor**:
```
  errno==EACCES(13): 20/20   errno==0: 0/20   other: 0/20   (tek dizgi, 20 tekrar)
  PROOF of clobber:
    single-dir call   -> errno=13 (Permission denied)
    multi-entry call  -> errno=2  (No such file or directory)  <-- AYNI EACCES dizini, ezilmiş
```
Yani `errno` tek dizgide işe yarar, çok dizgide **güvenilmezdir** — ve hiçbir yerde
belgelenmemiş bir yan üründür (API bildirimi `errno`'dan söz etmiyor).

### ⭐ L07-09 | `src/logitech_hidpp.cpp:2225` (hidraw) + `src/logitech_receiver.cpp:98-100` (sysfs) | **HIGH** — iki keşif yolunda da izin reddi "cihaz yok" ile aynı kola düşüyor
Kanıt: yukarıdaki üç blok. Kullanıcıya giden mesaj **ikisinde de** izin/IPC ipucu vermiyor
ve `exit 0` ile bitiyor:
```
$ timeout 25 ./build-manual/rawaccel-cli hidpp
No Logitech hidraw devices found.
EXIT=0
$ timeout 25 ./build-manual/rawaccel-cli --json hidpp
[]
EXIT=0
```
**Kullanıcı udev kuralı eksikse anlayabileceği bir metin görüyor mu? → HAYIR.**
Ne `input` grubunu ne `uaccess`'i ne udev'i ne de izin hatasını hiçbir yerde anmıyor.
**Dengeli taraf:** projede bu sorunu *çözen* bir udev kuralı **var** —
`scripts/99-rawaccel.rules:27`:
```
KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="046d", GROUP="input", MODE="0660", TAG+="uaccess"
```
`setup.sh:330` bunu `/etc/udev/rules.d/99-rawaccel.rules` olarak kuruyor. Kural
**vendor 046d ile sınırlı** olduğu için bu makinedeki VMware `hidraw0`'ın 0600 kalması
**doğru** davranış (kurulumdan sonra da öyle kalacaktır). Yani sorun "kural yok" değil,
**"kural uygulanmamışken teşhis de yok"**: kullanıcı `rawaccel-cli hidpp` çalıştırıp
"bulunamadı" görüyor, oysa tek yapması gereken `sudo usermod -aG input $USER` + yeniden
oturum açmak (kurulum zaten bunu yapıyor) — ve hiçbir çıktı onu söylemiyor.

### L07-10 | `tests/test_accel.cpp:162-208` | **MED** — alıcı keşif testi YALNIZCA mutlu yolu kapsıyor
Test 3 **düzgün** aygıt tohumluyor (c548/c542/c52f) ve `EXPECT(found.size() == 3)`
diyor; yani bir cihazın düşmesini **yakalar**. Ama **hiçbir hata yolu tohumlanmıyor**:
okunamayan / eksik / bozuk biçimli `idProduct` fixture'ı yok.
```
$ LC_ALL=C sed -n '162,181p' tests/test_accel.cpp
static void test_logitech_receiver_discovery() {
    SECTION("Logitech receiver discovery — capability classification");
    const std::string root = tmp_path("rawaccel_receiver_fixture");
    ... 3 adet ofstream(idVendor/idProduct/product) hepsi 0644, hepsi geçerli ...
    const auto found = discover_logitech_receivers(root);
    EXPECT(found.size() == 3);
```
Yani "izin reddi farklı ele alınmalıydı" davranışı için **kapı yok** — L07-09'un
düzeltilmesi (hata ayrımı) bu teste bir kırılma getirmezdi. L07-08'in boş-`
sysfs_root` davranışı da test edilmemiş.

---

## SORU 6 — ⭐ SESSİZ YEŞİL: cihaz tanınmıyorsa ne olur?

### İKİ AYRI CEVAP — ve ikisi de sessiz yeşil, farklı derecede

**(a) Alıcı tarafı (`logitech_receiver.cpp`) → SESSİZ YEŞİL DEĞİL, dürüst.**
Bilinmeyen bir Logitech ürün **listelenir** ve "bilinmiyor" olarak işaretlenir:
`src/logitech_receiver.cpp:107-112` — `receiver_for()` `nullptr` dönerse `if` bloğu
atlanır ve `logitech_receiver_info` varsayılanlarını korur
(`include/logitech_receiver.hpp:26-28`: `kind=unknown`, `max_paired_devices=0`,
`hidpp_supported=false`). Ölçüldü (sahte sysfs ağacına bilinmeyen 046d:c5ff eklendi):
```
  sysfs/1-4 pid=0xc5ff vid=0x046d kind=Unknown  slots=0 hidpp=false product="Mystery Logitech"
  sysfs/1-5 pid=0xc542 vid=0x046d kind=Nano     slots=1 hidpp=false product="Unifying"
  sysfs/1-2 pid=0xc53f vid=0x046d kind=Lightspeed slots=1 hidpp=true  product="USB Receiver"
```
Ve `:21-23` yorumundaki söz ("shown but never treated as HID++-safe") **kodda doğru**;
`logitech_receiver_kind_name` bilinmeyen için `"Unknown"` döner (`:85`). Alıcı tarafı
bu soruyu dürüstçe cevaplıyor. ✅

**(b) Cihaz/quirks tarafı (`logitech_quirks.hpp`) → ⭐ SESSİZ YEŞİL.**
Bilinmeyen model için `find_logitech_quirks()` `nullptr` döner (`include/logitech_quirks.hpp:145`).
Bu **davranış olarak doğru ve muhafazakâr** (L07-02'de 143 880 prob ile ölçüldü:
kısmi/varsayımsal eşleşme sıfır). Ama **tek üretim tüketicisinde karşılığı hiçbir
metin değil** — `gui/hidpp_panel.inl:147-152`:
```
    const std::string mid = logitech_compose_model_id(dev.info);
    if (!mid.empty()) {
        m += std::string(tr("Model ID: ")) + mid + "\n";
        if (find_logitech_quirks(dev.info))
            m += std::string(tr("Model quirks: known (RGB effects not written)")) + "\n";
    }                                                        ^^^^^^^^^ else YOK
$ LC_ALL=C grep -c 'Model quirks' gui/hidpp_panel.inl
1                                          -> tek kullanım, `else` dalı YOK
```
Yani bilinmeyen bir cihazda kullanıcı "Model ID: XXXX…" satırını görür ve **quirks
hakkında hiçbir şey öğrenmez** — satır yoktur, "desteklenmiyor" yazısı yoktur.
"Desteklenmiyor" ile "bu model için quirks uygulanabilir değil" **aynı görünür**.

### ⭐ L07-11 | `gui/hidpp_panel.inl:150-152` | **HIGH** — "desteklenmiyor" mesajı yok, bilinmeyen cihaz sessizce yok sayılıyor
Kanıt: yukarıdaki tek-kullanım/`else`-yok ölçümü + `find_logitech_quirks` üretimde
**sadece burada** çağrıldığı:
```
$ LC_ALL=C grep -rn 'find_logitech_quirks' --include=*.hpp --include=*.cpp --include=*.inl . \
    | grep -v '\.aihaberlesme' | grep -v 'olcum/aj5'
./gui/hidpp_panel.inl:150:        if (find_logitech_quirks(dev.info))     <- TEK üretim çağrısı
./include/logitech_quirks.hpp:129,157-159 (tanım)
./tests/test_accel.cpp:802,813,814,822 (test)
```
Düzensizliğin fark edilir olması: **Türkçe sözlükte "Model quirks: known ..." için bir
girdi VAR** (`gui/tr.inl:442-443`) — yani `tr` kapsamı kapısı yeşil, çünkü *mevcut*
olan tek metin çevrilmiş. Eksik olan metin **hiç yazılmamış**, dolayısıyla çeviri
kapısının göremeceği bir boşluk:
```
$ LC_ALL=C grep -n 'Model quirks\|Model ID' gui/tr.inl
441:            {"Model ID: ",                "Model Kimliği: "},
442:            {"Model quirks: known (RGB effects not written)",
443:                                         "Model quirks: biliniyor (RGB efektleri yazılmaz)"},
POZİTİF KONTROL: $ LC_ALL=C grep -c 'tr(' gui/tr.inl  -> 22   (sözlük okunuyor)
```

### ⭐ L07-12 | `README.md`, `docs/`, `CHANGELOG.md` | **MED** — hangi cihazın desteklendiği BELGELENMEMİŞ
Sorunun "kullanıcı hangi cihazın desteklendiğini nereden öğreniyor — belgelenmiş mi?"
kısmının cevabı: **hiçbir yerden.** Üç anahtarın ve üç cihazın hiçbiri kullanıcıya
dönük bir belgede geçmiyor:
```
$ LC_ALL=C grep -rn '4099C0950000\|B38940B4C355\|G502 X\|G515\|G522\|Centurion' \
    README.md docs/ CHANGELOG.md
README.md:277:  firmware/DFU, and the Solaar Centurion bridge are not enabled.
CHANGELOG.md:299:  - **Centurion batarya `/0x0104` desteği:** PRO X 2 LIGHTSPEED, G515 LS TKL
rc=0
```
Tek iki eşleşme de **quirks tablosuyla ilgisiz**: README satırı "Centurion bridge
etkin değil" diyor (ters yön), CHANGELOG satırı ise **pil** özelliğini anlatıyor
(`0x0104`, `logitech_quirks.hpp:172`deki `centurion_battery` enum'u) — quirks
tablosundaki G515 satırı değil. Kullanıcıya "bu tablo şu 3 cihazı kapsıyor" diyen
hiçbir çıktı ve hiçbir dosya yok; üstelik isimler struct'te de yok (L07-01).
`docs/` içeriği: `performance_tuning.md`, `real_hardware_test.md`, `research/`,
`test_window_usage.md`, `user_guide.md` — hiçbiri Logitech cihaz matrisi değil.

### L07-13 | `include/logitech_quirks.hpp:87-127` | **INFO (olumlu)** — "3 satır, 2 erişilebilir" durumu dürüstçe belgelenmiş
`:102-125` "⚠ O31-H2 — THIS ROW IS CURRENTLY UNREACHABLE, AND THAT IS KNOWN, NOT
OVERLOOKED" diye, neden erişilemediğini ve iki çıkış yolunu (gerçek 12 karakterlik id
okumak ya da satırı düşürmek) açıkça yazıyor. Bunu **bağımsız olarak doğruladım**:
```
=== Does a 2-char id reach the "32" row via the info overload? ===
  info.model_id="32" -> find_logitech_quirks(info) = MATCHED (row[2] reached!)
  identity: &result == &LOGITECH_QUIRKS[2].quirks ? YES
```
Yani satır gerçekten yalnızca model_id tam olarak `"32"` ise eşleşiyor; normal ayrıştırma
yolu (`bytes_to_hex(...,6)`) 12 karakter ürettiği için üretimde erişilemez. Bu,
brifing'in övdüğü türden bir "park edilmiş karar" — **ama yukarıdaki L07-03 yüzünden
gerekçesi testle doğrulanmıyor.**

---

## KAPI (koşturulan kapılar + üretilen sayılar)

| # | Komut | rc | ÜRETİLEN SAYI |
|---|-------|----|---------------|
| 1 | `bash tests/run_tests.sh --filter 'Logitech receiver discovery\|O31-H2\|HIDPP short payload\|logitech'` | **0** | `22/22 geçti (4 section eşleşti, 213 atlandı)` + 10 CLI/SEC-2 köprü alt-kapısı ✓ + SIMD parity 3/3 ✓ |
| 2 | `./t_base --filter 'O31-H2'` (kopya, mutasyon öncesi taban) | 0 | `11/11 geçti (3 section, 214 atlandı)` |
| 3 | **MUTASYON A** `logitech_hidpp.cpp:1397` kısa id | **0** | `11/11 geçti` **ve** tam suite `34164/34164 geçti` → **kapı kırmızıya DÖNMEDİ** |
| 4 | **MUTASYON B** `logitech_quirks.hpp:259` `compose()` kırpma | **1** | `5/27 geçti, 22 BAŞARISIZ` → kapı çalışıyor |
| 5 | **MUTASYON C** kısmi/önek eşleştirme | **1** | `11/27 geçti, 16 BAŞARISIZ`; tam suite `34164/34180, 16 BAŞARISIZ` → kapı çalışıyor |
| 6 | **MUTASYON D** büyük/küçük harf duyarsız | 0 | `34164/34164 geçti` → **kapı YAKALAMADI** (zararsız mutasyon, L07-07) |
| 7 | **MUTASYON E** anahtarı küçük harfe çevir (ölü satır) | **1** | `34163/34164 geçti, 1 BAŞARISIZ` → kapı çalışıyor |
| 8 | `./probe1` (tablo envanteri + yabancı cihaz probu) | 0 | 3 satır, 0 çakışma, 12 yabancı girdi → 12/12 `nullptr` |
| 9 | `./ed1only` (ED1 taraması) | 0 | `probes=1014 FALSE HITS=0` + 3/3 pozitif kontrol HIT |
| 10 | `./ed2` (ED2 taraması) | 0 | `probes=142866 FALSE HITS=0` (toplam **143 880**) |
| 11 | `./q3` (biçim doğrulaması) | 0 | 10 yapışkan girdi → 5 bozuk biçimin **0**'ı reddedildi (`rejected=0 of 5`) |
| 12 | `./probe5` (sysfs hata matrisi) | 0 | 8 durum; `b_noread==c_absent` → **IDENTICAL**; kontrol size=1 OK |
| 13 | `./probe6` (hidraw dalı enstrümanlı) | 0 | 1 hidraw düğümü: `open() FAILED errno=13` → `continue`; vendor **hiç** incelenmedi |
| 14 | `rawaccel-cli receivers` / `--json receivers` | **0** | `"No Logitech USB receivers found."` / `[]` |
| 15 | `rawaccel-cli hidpp` / `--json hidpp` | **0** | `"No Logitech hidraw devices found."` / `[]` |
| 16 | `git status --porcelain -- include/ src/ tests/ daemon/ cli/ gui/ scripts/` | 0 | **boş** — çalışma ağacı bozulmadı |

**Mutasyonların tamamı `/tmp/opencode/L07/mut` kopyasında yapıldı** (`cp include/ src/
tests/ daemon/ config/`; `tests/run_tests.sh:22`'nin `CXXFLAGS`'ı ile derlendi).
`git checkout/stash/reset` hiç çalıştırılmadı. Kullanılan komutlar: `cp`, `sed -i`
(yalnızca `/tmp` kopyasında), `python3` (yalnızca `/tmp` kopyasında), `g++`, `chmod`,
`mknod` (başarısız), `rm -rf` (yalnızca `/tmp` altında).

### Bulgu sıklığı (kanıtlanmış, sınıflandırılmış)
```
CRIT : 0
HIGH : 4   (L07-03, L07-08, L07-09, L07-11)
MED  : 4   (L07-01, L07-05, L07-10, L07-12)
LOW  : 1   (L07-04)
INFO : 3   (L07-02, L07-06, L07-07, L07-13  -> 4)
```

---

## KAPSANMAYAN (lane dışı — birinin bakması gereken)
1. **`src/logitech_hidpp.cpp` (L06'nın sahibi) — `discover_logitech_hidraw_devices()` (`:2210`).**
   L07-09'un **yarısı** bu dosyada: `if (fd < 0) continue;` (`:2225`). L06'ya iletilmeli.
   L07-03'teki mutasyon da bu dosyada (`:1397`) — koruma boşluğu L06'nın dosyasında.
2. **`cli/main.cpp:2281-2284` ve `:2297+` (`cmd_receivers` / `cmd_hidpp`)** — boş listede
   `return 0`. Bu, L07-08/L07-09'un **kullanıcıya ulaşan** yüzü. CLI'nin sahibi olan
   lane'in konusu; ben yalnızca çağrı zincirini ölçtüm.
3. **`gui/hidpp_panel.inl:150-152`** (L07-11) — GUI lane'inin dosyası. Ayrıca GUI'da
   `/dev/hidraw` izin reddi için bir uyarı var mı diye **kontrol etmedim**.
4. **`scripts/99-rawaccel.rules:27`** — kural 0600→0660'i düzeltiyor ama yalnızca
   `ATTRS{idVendor}=="046d"` için. **Bolt/Unity alıcılarının ve `hidraw` dışındaki
   Logitech HID++ düğümlerinin** kural kapsamına girip girmediğini ölçmedim.
5. **Daemon'ın kendi alıcı/cihaz keşfi** (`daemon/daemon.cpp`, `gui/devices.inl`) —
   bu lane'in iki dosyasını kullandığını doğrulamadım; `discover_logitech_receivers()`
   **tek** üretim tüketicisi olarak `cli/main.cpp:2266` çıktı.
6. **`RECEIVERS` tablosunun doğruluğu** (25 alıcı, slot sayıları, `hidpp` bayrakları) —
   yalnızca *iç tutarlılık* ölçüldü (25 bildirim = 25 satır, 0 çakışma). Solaar
   karşılaştırması veya donanım doğrulaması yapılmadı.
7. **Çeviri kapsamı kapısı (gate 5)** koşmadım — L07-11'in metni zaten `tr.inl`'de
   bulunduğu için o kapının yeşil kalacağını tahmin ediyorum, **ölçmedim**.

---

## TEMSİL SINIRI (neyi doğrulayamadım ve neden)
1. **⚠️ HİÇBİR LOGİTECH DONANIMI YOK.** Bu sanal makinede VMware fare/köprü
   (`0e0f`, `1d6b`) ve bir sanal `/dev/hidraw0` var; `grep -rl 046d
   /sys/bus/usb/devices/` → **0 sonuç**. Bu yüzden:
   - **`4099C0950000` (G502 X PLUS) ve `B38940B4C355` (G515 LS TKL) anahtarlarının
     GERÇEKTEN doğru cihazlara ait olduğunu DOĞRULAYAMADIM.** Solaar'ın
     `device_quirks.py`'siyle karşılaştırma yapmadım (ağ erişimi veya referans yok).
   - **"G522'nin gerçek 12 karakterlik model id'si nedir" sorusunu cevaplayamadım** —
     `logitech_quirks.hpp:113-118` bunun için fiziksel cihaz istiyor ve benim elimde yok.
     Bu, L07-13'teki park edilmiş kararın çözülmesi demektir; benim ölçümüm bunu
     yapamaz.
   - **Erişim reddinin GERÇEK bir Logitech alıcısını gizlediğini uçtan uca gösteremedim.**
     Ölçtüğüm: EACCES dalı gerçekten çalışıyor (errno=13) ve mesaj onu ayırt etmiyor.
     Gösteremediğim: "izin verilseydi Logitech olarak listelenecek bir düğüm" — çünkü
     öyle bir düğüm yok.
2. **`strace`/`ltrace` bu makinede YOK** (`which` → bulunamadı). Bu yüzden `open()`
   hatalarının *hangi syscall'i* ürettiği kernel tarafında doğrulanmadı; kanıtım
   `ifstream`/`open` dönüş kodları ve `errno` okumalarıyla sınırlı (errno=13 açıkça
   ölçüldü, ama bu satır içi `errno` okuması, syscall izi değil).
3. **`strace` yokluğu nedeniyle "hidraw vendor kontrolüne hiç ulaşılmadı" iddiam**,
   enstrümanlı bir **kopyaya** dayanıyor (`:2213-2235` gövdesi birebir alıntı).
   Alıntının doğruluğunu doğruladım — kopyanın çıktısı gerçek fonksiyonla
   **aynı** sonucu verdi (`replica size = 0 ; REAL function size = 0 ; agree = YES`).
4. **PS5.1 / çalışma anı yok** — denetim 1 Ekim 2026'da, sanal makinede
   (`VMware Virtual USB Mouse`), root **değil** (uid 1000) yapıldı. Root olarak
   koşmadım; root olsaydı `mode 0000` EACCES vermeyecek ve L07-09'un ölçümü
   bu makinede tekrarlanamaz olurdu.
5. **Tüm `/dev` tarama sonucunu etkileyen başka yolları ölçmedim**: `discover_logitech_hidraw_devices()`
   içindeki `HIDIOCGRAWINFO` **başarısızlığı** (yoksa) ayrı bir sessiz `continue`'dur
   (`:2231`) — bu makinede o dal **hiç çalışmadı** (`ioctl failed = 0`, çünkü `open`
   daha önce başarısız oldu). Ölçülmedi.
6. **`errno`'nun 20/20 EACCES vermesi tek dizgide ölçüldü.** Çok dizgide ezildiğini
   gösterdim, ama bu "daima ezilir" değil — "her zaman güvenilir değil"dir. Hata
   ayrımı için `errno`'ya güvenilebileceğini söylemiyorum, **güvenilemeyeceğini**
   söylüyorum.
7. **Gate 2'yi `--filter` ile koşturdum** (22/22). Yedi kapının **tamamını**
   koşturmadım — L07 kapsamındaki iki dosya yalnızca gate 2'de (`tests/run_tests.sh:98`)
   derleniyor, diğer kapılar bu iki dosyayı içermiyor; dolayısıyla tam koşumun bu lane
   için **yeni kanıt üretmeyeceğini** değerlendirdim (bu bir çıkarım, ölçüm değil).
8. **`logitech_hidpp.hpp` içindeki `HidppTransport` sınıfının gerçek donanım
   round-trip'ini test etmedim** — `send_feature_request`/`resolve_feature_index`
   özeldir, gerçek hidraw düğümü gerektirir; bu makinede Logitech düğümü yok.
   L07'nin konusu da değil, sınır olarak yazıyorum.
