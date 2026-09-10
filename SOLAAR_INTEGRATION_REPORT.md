# Solaar özellik entegrasyon raporu

Tarih: 2026-09-09  
Proje: RawAccel Linux v0.6.4  
Referans kaynak: `/home/a/Masaüstü/Solaar-master`

## 0.2 2026-09-09 — remaining read-only protocol gaps

The final comparison pass closed the remaining safe, user-visible gaps without
changing the acceleration or daemon motion path:

- HID++ `DEVICE_FW_VERSION` function-0 identity offsets now follow Solaar
  (`unit_id[1:5]`, transport flags `[6]`, model IDs `[7:13]`). Transport IDs
  are decoded in advertised BT/BLE/WPID/USB order. All firmware records are
  retained, and `DEVICE_FRIENDLY_NAME` is read with its echoed chunk-offset
  byte removed.
- `BATTERY_STATUS` (`0x1000`, function `0`) is separate from
  `UNIFIED_BATTERY` (`0x1004`, function `0x10`). Legacy battery fallback
  probes `BATTERY_CHARGE` register `0x0D` before register `0x07`, with a
  register-specific parser.
- Bolt pairing payloads use Solaar's WPID offsets (`payload[3], payload[2]`)
  and preserve the four-byte serial. Additional documented receiver products
  (`C525/C52E/C52F/C531/C535/C53A/C53D/C53F/C54D`) are capability-listed.
- The GUI watches `/dev` for `hidraw*` hotplug and drains at most eight
  read-only HID++ notifications once per second on a worker thread. Battery
  notification state is surfaced in the hardware panel; no notification work
  enters the evdev/uinput hot path.

Pairing/authentication writes, profile/LED/remapping/SmartShift writes,
firmware/DFU, and the broad Solaar Centurion bridge remain intentionally
deferred. The existing capability/model gates and read-only policy are
unchanged.

## 0. 2026-09-09 — yüksek öncelikli karşılaştırma düzeltmeleri

Solaar ile yapılan transport karşılaştırmasında bulunan dört düşük seviye
uyumsuzluk düzeltildi:

- HID++ function alanı transport sınırında normalize ediliyor. Solaar'ın
  request-id byte biçimi olan `0x10`/`0x50` artık sırasıyla wire function
  `0x1`/`0x5` olarak gönderiliyor; canonical `0x1`/`0x5` çağrıları da aynı
  sonucu veriyor. DPI, battery, FEATURE_SET ve onboard profile çağrıları
  canonical biçimi kullanıyor.
- `FEATURE_SET.GetCount` sonucu Solaar davranışına göre ROOT dışı count olarak
  ele alınıyor (`+1`). ROOT (`index=0`) ve FEATURE_SET (`index=fs_index`)
  metadata'ya ekleniyor; `GetFeatureId` cevabının version byte'ı dynamic
  index sanılmıyor, istek index'i kullanılıyor. Böylece son feature kaybı ve
  yanlış capability cache'i engellendi.
- HID++ 1.0 battery fallback artık hatalı `send_short(0x07, ...)` yolu yerine
  doğrudan `read_register(0x07)` kullanıyor. Legacy 0x07 payload'ı Solaar
  formatına göre (level kodu byte 0, charging bayrakları byte 1) çözülüyor;
  bilinmeyen kodlar güvenli `255/unknown` olarak kalıyor.
- HID++ 1.0 write request ID'si `0x82xx` olan long register yazımları, değer
  üç byte veya daha kısa olsa bile long report (`0x11`) seçiyor. Pairing veya
  profile flash write eklenmedi.

Function normalization, `0x82xx` report seçimi ve legacy battery decode için
odaksal unit testleri eklendi; mevcut acceleration semantiği ve güvenli yazma
sınırları korunuyor.

## 0.1 2026-09-09 — Device Information / receiver read-only düzeltmeleri

Solaar karşılaştırmasındaki iki yüksek riskli protokol hatası bu turda
düzeltildi:

- HID++ 2.0 feature sabitleri Solaar ile hizalandı. `FEATURE_INFO=0x0002`,
  `DEVICE_FW_VERSION=0x0003`, `DEVICE_UNIT_ID=0x0004`,
  `DEVICE_NAME=0x0005` ve `DEVICE_FRIENDLY_NAME=0x0007` artık doğru
  ID'leri kullanıyor. Eski `0x0003` generic-device-info varsayımı kaldırıldı.
- `get_device_info()` artık Device Name uzunluk/chunk sorgularını ve Device
  Firmware Version count/record sorgularını Solaar'ın function `0x10`
  biçimiyle yapıyor. Firmware adı, major/minor/build, unit/model ID,
  transport flags ve device kind salt-okunur olarak raporlanıyor.
- `FEATURE_SET.GetFeatureId` metadata'sındaki `flags` ve `version` cihaz
  snapshot'ına taşındı; CLI JSON ve metin çıktısı bunları gösteriyor.
  `GetCount` enumeration artık FEATURE_SET dynamic index'i 1 değilse de
  index kaybetmiyor.
- Önceki hatalı HID++ 2.0 `device_info` pairing sorgusu kaldırıldı. Pairing
  listesi yalnızca Solaar'da belgelenen receiver ürün ID'leri için, yalnızca
  HID++ 1.0 receiver-info register `0x02B5` üzerinden okunuyor; Bolt ve
  Unifying/Nano payload düzenleri ayrı doğrulanıyor. Yazma, eşleştirme,
  kilit açma ve firmware işlemleri eklenmedi.

Bu yollar için metadata, receiver payload ve gerçek HID++ feature-ID
regresyon testleri eklendi. Gerçek donanım receiver response'ları olmayan
ortamda CLI pairing çıktısı doğal olarak boş kalabilir.

## 1. Önemli lisans ve uygulama kararı

Solaar kaynak kodu GPLv2-or-later lisanslıdır. RawAccel içindeki mevcut
HID++ katmanı bağımsız C++ kodudur. Bu nedenle Solaar Python dosyalarını
satır satır C++'a çevirip kopyalamadım. Bunun yerine protokol davranışını,
HID++ feature ID'lerini, cihaz yeteneklerini ve Solaar dokümantasyonunu
referans alarak bağımsız C++ kodu yazıyorum.

Bu yaklaşım:

- RawAccel'in mevcut lisans yapısını korur.
- Python çalışma zamanı ve Solaar GUI bağımlılıklarını RawAccel'e taşımaz.
- HID++ protokolü ile hızlandırma/evdev/uinput katmanlarını birbirinden ayrı
  tutar.
- Her özelliğin gerçek cihaz tarafından ilan edilmesini zorunlu kılar;
  desteklenmeyen özelliklere körlemesine komut göndermez.

## 2. Bu turda yapılanlar

### Mevcut RawAccel HID++ altyapısı

Zaten mevcut olan ve korunan parçalar:

- `/dev/hidraw*` Logitech cihaz keşfi
- HID++ kısa, uzun ve çok-uzun rapor paketleri
- HID++ 2.0 dinamik `FEATURE_SET` keşfi
- Cihaz başına dinamik feature-index çözümleme ve önbellekleme
- HID++ 2.0 `ADJUSTABLE_DPI` (`0x2201`)
- HID++ 2.0 `EXTENDED_ADJUSTABLE_DPI` (`0x2202`)
- `REPORT_RATE` (`0x8060`)
- `EXTENDED_ADJUSTABLE_REPORT_RATE` (`0x8061`)
- DPI listesi/yeteneği doğrulaması
- X/Y DPI ve capability-gated LOD
- CLI ve GTK4 GUI üzerinden DPI, polling-rate ve LOD işlemleri
- HID++ cihaz bilgisi, seri/firmware sorguları
- Alıcı eşleştirme slotlarının okunması

### Bu turda eklenen battery kapsamı

Solaar'daki feature önceliklendirmesine uyacak şekilde battery sorgusu
genişletildi:

1. `UNIFIED_BATTERY_V2` (`0x1004`)
2. `UNIFIED_BATTERY` (`0x1000`)
3. `BATTERY_VOLTAGE` (`0x1001`)
4. HID++ 1.0 battery register fallback (`0x0007`)

`BATTERY_VOLTAGE` için millivolt değerinden yaklaşık yüzde üretimi eklendi.
Bu değer donanıma göre yaklaşık sonuçtur; RawAccel bunu kesin pil ölçümü gibi
sunmamalıdır. Mevcut `level`, `charging` ve `online` alanları korunmuştur.

Unified Battery payload eşlemesi Solaar ile düzeltildi: discharge yüzde
`payload[0]`, coarse level `payload[1]`, status `payload[2]` olarak okunuyor.
Şarj durumu `RECHARGING` veya `SLOW_RECHARGE` değerleriyle belirleniyor;
discharge bilinmiyorsa coarse seviye güvenli yaklaşık yüzdeye çevriliyor.

HID++ 1.0 `BATTERY_STATUS` fallback'i de ayrı parser helper'a ayrıldı:
`payload[0]` discharge, `payload[1]` next-level ve `payload[2]` status byte
olarak ele alınıyor. RawAccel modelinde next-level alanı bulunmadığı için bu
ara değer dışarı taşınmıyor; level/charging/online sonuçları doğru status
değerlerinden üretiliyor.

### Bu turda eklenen capability snapshot

`identify_logitech_device()` artık dinamik HID++ 2.0 feature listesini cihaz
kaydına (`hidpp_device.features`) taşır. Böylece aynı cihaz tanımlandıktan
sonra CLI/GUI capability göstergesi için tekrar feature discovery yapmak
zorunda kalmaz.

- `rawaccel-cli hidpp --json` her feature için `id`, `id_hex` ve dinamik
  `index` alanlarını verir.
- İnsan okunabilir `rawaccel-cli hidpp` çıktısı da `feature@index` çiftlerini
  gösterir.
- Bu snapshot yalnızca cihazın ilan ettiği yetenekleri raporlar; desteklenmeyen
  özelliklere komut gönderme davranışı değişmemiştir.

### Son güvenlik ve doğruluk sertleştirmeleri

Bu son turda doğruluk ve güvenilirlik iyileştirmeleri yapıldı:

- `hidpp_parse_unified_battery()` / `hidpp_parse_legacy_battery()` public helper'ları
  çıkartıldı; batarya decode mantığı artık tek noktadan erişilebilir ve test edilebilir.
- Bilinmeyen Logitech batarya payload'ları `0x00/0x00/0xFF` gibi durumlarda 255/unknown
  olarak korunuyor; eski sürümde bu değer yanlışlıkla 0% olarak yorumlanabiliyordu.
- `hidpp_notification::from_bytes()` artık `sub_id == 0` veya anlamsız zero-value
  HID++ 2.0 bildirimlerini red eder. Bu sayede feature mapping öncesinde bozuk
  notification paketleri sistemden ayrılır.
- `hidpp_device::notification_feature_id()` için `feature_index == 0` gate eklendi.
  Böylece geçersiz notification ayrıştırma sonrası yanlış feature resolution yaşanmaz.
- Yeni regresyon testleri `Logitech HID++ battery parsing stays conservative on unknown payloads`
  ve `invalid_subid` senaryolarını kapsar.

### Bu turda eklenen feature isimlendirme

Capability snapshot içindeki bilinen HID++ feature ID'leri artık sabit isimlerle
raporlanır. `rawaccel-cli hidpp --json` çıktısında `name` alanı, normal CLI
çıktısında ise `name=0xINDEX` biçimi kullanılır. Bilinmeyen veya model/vendor
özel feature'lar `"unknown"` olarak korunur; ham ID ve dinamik index her zaman
çıktıda kalır.

## 3. Mevcut doğrulama durumu

- Portable build: başarılı
- Derleyici warning/error taraması: temiz
- Birim ve entegrasyon testleri: `33738/33738` (183 grup; P168 +131 assertion sonrası güncel)
- CLI kapıları: P83, P99 ve P107 başarılı
- Türkçe çeviri kapsamı: başarılı
- Mevcut RawAccel hızlandırma algoritmaları: değiştirilmedi
- Daemon evdev grab/uinput hareket hattı: değiştirilmedi
- HID++ desteklemeyen cihazlarda güvenli başarısızlık: korunuyor

Gerçek donanım testi oyun bilgisayarındaki hedef mouse ile yapılmalıdır.
Geliştirme bilgisayarındaki Logitech M185 referans cihaz değildir ve HID++
desteklememesi proje hatası olarak yorumlanmamalıdır.

## 4. Solaar ile karşılaştırmalı kapsam

| Özellik | Durum | Not |
|---|---|---|
| HID++ 2.0 feature discovery | Tamamlandı | Dinamik index + count korunuyor |
| Feature metadata | Bu tur tamamlandı | GetFeatureId flags/version snapshot + CLI |
| HID++ device information | Bu tur düzeltildi | 0x0003 firmware, 0x0005 name, unit/model |
| Capability snapshot / CLI görünümü | Bu tur tamamlandı | JSON ve insan okunabilir çıktı |
| Feature ID isimlendirme | Bu tur tamamlandı | Bilinen ID'ler adlandırılıyor |
| HID++ 1.0 read-register transport API | Bu tur tamamlandı | Ayrı 16-bit request-ID yolu |
| DPI / DPI listesi | Mevcut | Capability ve liste doğrulamalı |
| X/Y DPI | Mevcut | Extended DPI destekliyorsa |
| Polling/report rate | Mevcut | Eski ve extended rate feature yolları |
| LOD | Mevcut | Yalnızca ilan edilen capability ile |
| Battery status | Bu tur tamamlandı | 2.0 feature ailesi + 1.0 fallback |
| HID++ 1.0 genel register katmanı | Kısmi | Read/write primitive tamamlandı; policy callers beklemede |
| Notification parser | Bu tur tamamlandı | Yan etkisiz HID++ 1.0/2.0 ayrıştırıcı |
| Notification receive primitive | Bu tur tamamlandı | Salt-okuma hidraw poll API'si |
| Bounded notification drain | Bu tur tamamlandı | Sınırlı batch tüketim API'si |
| Notification type classification | Bu tur tamamlandı | HID++1.0/2.0 ve legacy türleri |
| Capability gate helper | Bu tur tamamlandı | `hidpp_device::supports_feature()` |
| Dynamic feature index helper | Bu tur tamamlandı | `hidpp_device::feature_index()` |
| Generic feature request API | Bu tur tamamlandı | Capability-gated HID++ 2.0 request |
| Transport capability gate | Bu tur tamamlandı | `HidppTransport::supports_feature()` |
| Feature-set cache | Bu tur tamamlandı | Target-index scoped discovery cache |
| Onboard profile descriptor | Bu tur tamamlandı | Read-only function `0x00` |
| Onboard descriptor CLI output | Bu tur tamamlandı | Human/JSON capability görünümü |
| Onboard profile headers | Bu tur tamamlandı | Read-only function `0x50` |
| Onboard sector read | Bu tur tamamlandı | Bounded 16-byte chunk reads |
| Unified battery payload mapping | Bu tur tamamlandı | Solaar-compatible offsets/status |
| HID++ 1.0 battery payload mapping | Bu tur tamamlandı | Discharge/next/status ayrımı |
| Notification feature index | Bu tur tamamlandı | Dispatch için dinamik index alanı |
| Reverse feature lookup | Bu tur tamamlandı | `hidpp_device::feature_id()` |
| Notification feature resolution | Bu tur tamamlandı | `notification_feature_id()` |
| Receiver pairing/unpairing | Salt-okunur kısmi | Yalnızca belgelenen 0x02B5 receiver layout'ları |
| HID++ notifications | Parser tamamlandı | Asenkron event kuyruğu/handler beklemede |
| Onboard profiles (`0x8100`) | Henüz yok | Model ve veri formatı doğrulaması gerekiyor |
| Model quirks/descriptors | Henüz yok | Solaar'daki geniş tablo bağımsız C++ tablosuna çevrilecek |
| GUI battery/capability görünümü | Kısmi | Mevcut status/CLI yolları genişletilebilir |
| Hotplug HID++ yeniden tarama | Kısmi | GUI refresh var; udev tabanlı sürekli izleme yok |

## 5. Bundan sonra yapılacak işler

### Aşama A — HID++ 1.0 ve transport sağlamlaştırma

- HID++ 1.0 register okuma için açık C++ API oluşturuldu:
  `HidppTransport::read_register()` Solaar'daki `0x8100 | register`
  request-ID biçimini ayrı bir transport yolu ile kullanıyor.
- HID++ 1.0 register yazma primitive'i de eklendi:
  `HidppTransport::write_register()` `0x8000 | register` request-ID'sini ve
  kısa/uzun rapor boyutlarını kullanır. Henüz pairing veya başka bir kullanıcı
  ayarı bu primitive'e bağlanmamıştır.
- Solaar'ın `make_notification()` davranışından bağımsız C++ olarak
  `hidpp_notification::from_bytes()` eklendi. HID++ 1.0 sub-id aralığı,
  legacy battery/illumination biçimleri ve HID++ 2.0 software-id/address
  kuralı ayrıştırılır; request reply/error paketleri `nullopt` döner.
  Henüz daemon read loop'una bağlanmadı, bu nedenle mevcut request davranışı
  etkilenmez.
- Parser hidraw transport'a `HidppTransport::receive_notification()` ile
  bağlandı. Bu API istek göndermeden tek bir bekleyen bildirimi okur; sıfır
  timeout kısa poll yapar. Request reply/error paketleri araya girerse timeout
  içinde sonraki paketleri tarar ve ilk gerçek bildirimi döndürür. Daemon event
  queue/handler entegrasyonu henüz yapılmadı.
- `HidppTransport::drain_notifications()` ile bounded batch tüketimi eklendi.
  `max_count` limiti busy cihazların event loop'u aç bırakmasını önler; bu API
  henüz daemon/GUI handler'larına bağlanmadı. Batch drain artık tek mutex
  altında kendi parser döngüsünü kullanır; `receive_notification()` üzerinden
  yeniden mutex kilitleme yapmadığı için self-deadlock riski yoktur.
- `hidpp_notification::kind` ile parser sonucu artık `hidpp10`, `hidpp20`,
  `legacy_battery` veya `legacy_illumination` olarak sınıflandırılır. Handler
  katmanı `sub_id` bitlerini yeniden yorumlamadan doğru dispatch yapabilir.
- `hidpp_device::supports_feature()` ile capability kontrolü tek bir yardımcıya
  taşındı. Pairing/profile gibi ileride eklenecek yazma caller'ları bu gate'i
  kullanarak ilan edilmemiş feature'lara komut göndermeyecek.
- `hidpp_device::feature_index()` aynı capability kaydından dinamik feature
  index'i `std::optional<uint8_t>` olarak verir. Böylece yeni caller'lar
  feature listesini elle taramaz ve bulunamayan feature'ı açıkça işler.
- Solaar'daki genel `device.feature_request()` davranışının bağımsız C++
  karşılığı `HidppTransport::feature_request()` eklendi. Feature index dinamik
  olarak çözülür, bilinmeyen feature ve 16 byte üzeri payload yerelde reddedilir;
  cevap payload'ı report header olmadan döner. Onboard profile/pairing caller'ı
  henüz bu primitive'e bağlanmadı.
- `HidppTransport::supports_feature()` ile `feature_request()` artık isteği
  göndermeden önce cihazın dinamik `FEATURE_SET` içinde feature ID'yi ilan
  ettiğini doğrular. Böylece bilinmeyen feature'a yalnızca index çözmeye
  çalışıp komut gönderme riski kapatıldı.
- Solaar `ONBOARD_PROFILES` akışının güvenli ilk parçası olarak
  `get_onboard_profile_info()` eklendi. Function `0x00` descriptor'ı parse
  edilerek bellek türü, aktif profil, profil sayısı, button/sector sayıları,
  profil boyutu ve shift bilgisi döndürülür. Normal onboard-profile memory
  layout'ı dışında descriptor reddedilir; sektör okuma/yazma henüz yoktur.
- Descriptor bilgisi CLI'ye bağlandı. `rawaccel-cli hidpp --json` altında
  `onboard_profiles` nesnesi; normal çıktıda ise profil sayısı, aktif profil,
  sektör sayısı ve profil boyutu gösterilir. Cihaz desteklemiyorsa alan
  yazılmaz; bu akış yalnızca read-only sorgudur.
- `get_onboard_profile_headers()` Solaar'ın function `0x50` header okuma
  akışını bağımsız C++ olarak uygular. Profil numarası, sektör ve enabled
  alanları CLI/JSON çıktısına eklenir. RAM header alanı boşsa Solaar
  davranışına uygun ROM (`storage=0x01`) fallback uygulanır; sonlandırıcı,
  geçersiz sektör veya hatalı cevap görülürse okuma durur. Profil sektör
  içeriğine hiçbir yazma yapılmaz.
- `read_onboard_profile_sector()` Solaar'ın read-sector chunk akışının
  bağımsız C++ karşılığıdır. Sector, descriptor profile/sector sınırlarıyla
  doğrulanır; okumalar 16-byte parçalar halinde yapılır ve toplam boyut
  4096 byte ile sınırlandırılır. Bu API yalnızca okur; write-sector/flash
  işlemi özellikle eklenmemiştir.
- `HidppTransport` artık `get_feature_set()` sonucunu hedef device index'i
  başına önbellekler. Böylece battery/feature caller'ları her sorguda aynı
  discovery paketlerini tekrar göndermez. `set_device_index()` cache'i
  hem feature-set hem dinamik index tablolarını temizler; aynı hedef yeniden
  seçildiğinde bile stale firmware/cihaz index'i kullanılmaz.
- Notification veri modeline `feature_index` alanı eklendi. HID++ 2.0
  bildirimlerinde bu alan dinamik feature index'i olarak handler dispatch'ine
  hazırdır; HID++ 1.0 bildirimlerinde `sub_id` ile aynı değer korunur.
- `hidpp_device::feature_id(dynamic_index)` ters eşleme helper'ı eklendi.
  Handler'lar artık notification'daki dinamik index'i ilan edilmiş gerçek
  feature ID'sine çevirip capability'ye göre dispatch edebilir.
- `hidpp_device::notification_feature_id()` bu çözümlemeyi notification türüne
  göre merkezileştirir: HID++ 2.0 dinamik index'i ters çevrilir; HID++ 1.0 ve
  legacy bildirimler register/sub-id olarak korunur. Handler katmanı artık
  dört bildirime özgü yol yazmak zorunda değildir.
- Kısa/uzun rapor cevaplarının feature'a göre doğru uzunlukta işlenmesini
  sağlamak.
- Battery status, charging status ve bağlantı bildirimlerini ortak bir
  normalize edilmiş modele dönüştürmek.
- Gerçek cihaz olmadan çalışan fake transport testleri eklemek.

### Aşama B — Capability ve bildirim modeli

- Her cihaz için feature ID, dynamic index, version ve capability bayraklarını
  tek bir kayıt yapısında sunmak.
- HID++ notification raporlarını ayırmak; sorgu cevapları ile karıştırmamak.
- Cihaz çıkarılma/yeniden bağlanma durumlarını açıkça raporlamak.
- CLI `hidpp --json` çıktısında desteklenen feature/capability listesini
  göstermek.

### Aşama C — Receiver pairing

- Unifying/Nano/Bolt/Lightspeed receiver türlerini ayırmak.
- Slot listeleme, cihaz adı/PID/bağlantı türü sorgusunu tamamlamak.
- Pairing lock, authentication ve unpair akışlarını yalnızca cihaz capability
  ilan ediyorsa etkinleştirmek.
- Varsayılan olarak yazma işlemlerini kapalı tutup açık CLI komutlarıyla
  çalıştırmak.

Bu listenin pairing yazma maddeleri özellikle ertelenmiştir; bu tur yalnızca
ürün-ID'si ve payload layout'u doğrulanmış salt-okunur receiver sorgularını
uyguladı.

### Aşama D — Onboard profiles

- `ONBOARD_PROFILES` feature'ını dinamik olarak tespit etmek.
- Profil sayısı, aktif profil ve report-rate/DPI alanlarını okumak.
- Modelin desteklediği format doğrulanmadan profile yazmamak.
- RawAccel yazılım profilleri ile mouse üzerindeki onboard profilleri ayrı
  kavramlar olarak GUI'de göstermek.

### Aşama E — Model quirks ve kullanıcı arayüzü

- Solaar cihaz dokümantasyonunu model davranışı için referans almak.
- Kod kopyalamadan, yalnızca doğrulanmış protokol davranışlarını C++ capability
  tablolarına aktarmak.
- GUI'de desteklenmeyen alanları disabled ve açıklamalı göstermek.
- Udev/hidraw izinlerini aktif seat kullanıcısı ve `input` grubu için
  kurulumdan sonra yeniden doğrulamak.

## 6. Bilinçli olarak yapılmayanlar

- Solaar Python kodu veya GPL kodu RawAccel'e kopyalanmadı.
- RawAccel acceleration matematiği değiştirilmedi.
- Daemon'ın gerçek mouse'u grab edip sanal mouse üretme çalışma biçimi
  değiştirilmedi.
- Desteklenmeyen mouse'lara DPI/polling komutu zorla gönderilmedi.
- Gerçek oyun bilgisayarındaki mouse modeli bilinmeden model-özel varsayım
  eklenmedi.
- Pairing/unpairing, pairing lock, discovery/authentication ve profile
  değişiklikleri yazılmadı.
- Firmware/DFU, onboard profile sector yazımı, genel Solaar kontrol tablosu
  ve notification handler/event-loop entegrasyonu ertelendi.
- Tanınmayan receiver ürün ID'leri için pairing register probe'u yapılmıyor;
  yanlış register/layout varsayımıyla cihazı bozma riski alınmadı.

## 7. Son durum

RawAccel şu anda Solaar'ın temel HID++ donanım kontrol yolunun bağımsız C++
karşılığına sahiptir: doğru feature discovery/metadata, device-info, DPI,
polling-rate, LOD, genişletilmiş battery ve sınırlı salt-okunur receiver
pairing sorguları çalışır. Solaar seviyesine yaklaşmak için sıradaki teknik
öncelik bildirim kuyruğu, model-quirk tablosu ve gerçek cihaz doğrulamasıdır.
Pairing/profile/firmware yazma yolları özellikle kapsam dışıdır; her biri
ayrı mock test + capability gate + gerçek cihaz doğrulaması olmadan
eklenmemelidir.
