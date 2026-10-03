### L06 | denetim alt-ajanı (L06 lane) | 1 Ekim 2026

KAPSAM   : `include/logitech_hidpp.hpp` (791 satır) · `src/logitech_hidpp.cpp` (2572 satır)
           Referans (sadece okuma): `include/logitech_quirks.hpp`
           **Hiçbir dosya değiştirilmedi.** Tüm mutasyonlar `/tmp/opencode/L06/{m1..m6}`
           kopyaları üzerinde yapıldı; çalışma ağacına dokunulmadı.

---

## ⭐ ÖZET — 6 MUTASYONUN SONUCU

| # | Mutasyon | Koruma gerçekten yük taşıyor mu? | Kapı ne yaptı? |
|---|---|---|---|
| M1 | `send_feature_request` uzunluk kontrolü silindi | **EVET** — kanıtlandı | 🟢 **YEŞİL KALDI** (testler korumayı görmüyor) |
| M2 | `param_len` bütçe koruması silindi (2 nokta) | **EVET** — ASan stack-buffer-overflow | 🟢 **YEŞİL KALDI** |
| M3 | Bildirim deposu 16-lık üst sınırı silindi | **EVET** — 16 → 100.000+ | 🟢 **YEŞİL KALDI** |
| M4 | `hidpp_hw_take` FIFO → LIFO | EVET | 🔴 KIRMIZI (rc=1) |
| M5 | `hidpp_hw_plan_job` sensörü işaretlemeyi bıraktı | EVET | 🔴 KIRMIZI (rc=1) |
| M6 | `plan_for` transport kimlik kontrolünü kaybetti | EVET | 🔴 KIRMIZI (rc=1) |

**Ana hüküm:** Kodda **3 adet gerçek, yük taşıyan koruma var** ve **3'ünün de testi
yok**. Kuyruk (M4/M5/M6) tarafı iyi durumda; **paket-okuma tarafı (M1/M2/M3)
tamamen testsiz.** M1 ve M2 olmasa donanım/kötü niyetli cevap bellek taşmasına
yol açardı — koruma doğru yerde ve doğru çalışıyor, ama **kapı onu doğrulamıyor.**

---

## BULGULAR

### ID | dosya:satır | CRIT/HIGH/MED/LOW | kanıt

---

#### L06-B1 | src/logitech_hidpp.cpp:782 | **HIGH** — ⭐ KORUMA VAR, TESTİ YOK (sessiz yeşil)

`send_feature_request` içinde okunan uzunluk alanı kontrolsüz kalırsa
`payload_len = len - 4` **size_t underflow** yapıyor ve 2^64 baytlık
`std::vector` kurma girişimi **yakalanmamış `std::length_error` → SIGABRT** veriyor.

**Mutasyon (M1)** — `/tmp/opencode/L06/m1`, `src/logitech_hidpp.cpp:782`:

    $ grep -c "if (len != 7 && len != 20 && len != 64) continue;" src/logitech_hidpp.cpp
    0                          # mutant: kontrol kaldırıldı
    $ bash tests/run_tests.sh
    === Sonuç: 34164/34164 geçti ===
    RC=0                       # ← KIRMIZI DEĞİL

**Aynı mutasyonun ağır sonucu (sentetik hidraw düğümü ile ölçüldü):**
4 başlık baytı **doğru yazılı** (istek eşleştiricisi geçsin) ama `read()` daha
kısa bir uzunluk bildiriyor:

    ### M1 MUTANT (guard REMOVED) ###
    mode=liar1 -> terminate called after throwing an instance of 'std::length_error'
                   what():  cannot create std::vector larger than max_size()
                   rc=134
    mode=liar2 -> ... rc=134
    mode=liar3 -> ... rc=134

    ### BASE (guard PRESENT) ###
    liar1 -> nullopt   rc=0
    liar3 -> nullopt   rc=0

**Koruma yük taşıyor (ölçüldü):**

    $ /tmp/opencode/L06/proof/underflow      # korumayla/korumasız ayrı ayrı
    report_len | guard:ON | guard:OFF
            1 |        -1 |                -3
            3 |        -1 |                -1
            4 |        -1 |                 0
    payload_len = 18446744073709551613 (0xfffffffffffffffd)

**Neden HIGH, CRIT değil:** `:782` kontrolü **yerinde ve çalışıyor** — temiz kod
kötü cevabı `nullopt` ile reddediyor (yukarıdaki BASE ölçümü). Ayrıca dürüst bir
hidraw `read()`'i başlığı yazıp daha kısa uzunluk bildiremez; `liar*` senaryosu
**yalan söyleyen fd** gerektirir, normal cihaz değil. Dolayısıyla sömürülebilir bir
açık **değil**; ama koruma **hiçbir testle doğrulanmıyor** ve bir düzenlemede
sessizce kaybolabilir. **Sessiz yeşil.**

**Aynı sınıf, diğer okuma noktalarında KORUMALI (kontrol edilip doğrulandı):**
- `:1022` `read_register` — `(len != 7 && len != 20) || buf[1] != target` ✔
- `:1061` `probe_hidpp10` — `(len != 7 && len != 20) || buf[1] != target` ✔
- `:879/927/974` `send_short/long/very_long` — `from_bytes()` `len != 7/20/64` ✔
- `:530` `hidpp_notification::from_bytes` — `len != 7 && len != 20 && len != 64` ✔
  (bu, `notification.payload.assign(data+4, data+len)` :568'i koruyor)
- `:1833` `read_onboard_profile_sector` — `reply->size() < wanted` ✔
- `:1394/1456/1750/1638/1652` — hepsi `size() >= N` kapılı ✔

Yani **karşılaştırılmayan yol yok** — 8/8 okuma noktası doğruluyor.
Sorun doğrulamanın yokluğu değil, **doğrulamanın testsizliği.**

---

#### L06-B2 | src/logitech_hidpp.cpp:747, :1072 | **HIGH** — ⭐ BÜTÇE KORUMASI VAR, TESTİ YOK

`test_hidpp_short_payload_budget` adlı test **bütçenin kendisini test etmiyor**,
sadece sabitlerin aritmetiğini ve tel düzenini test ediyor.

**Mutasyon (M2)** — iki koruma noktası silindi:

    $ grep -c "param_len > HIDPP_SHORT_PAYLOAD_MAX" src/logitech_hidpp.cpp
    0
    $ bash tests/run_tests.sh
    === Sonuç: 34164/34164 geçti ===
    RC=0                       # ← KIRMIZI DEĞİL

**Koruma yük taşıyor (ASan ile ölçüldü):** `param_len=17` ile:

    ### BASE (guard PRESENT) ###
    feature_request(param_len=17) -> nullopt (rejected locally)

    ### M2 MUTANT (guard REMOVED) ###
    ==25963==ERROR: AddressSanitizer: stack-buffer-overflow
      WRITE of size 17 at 0x7bfa09df0c54
        #1 rawaccel::HidppTransport::send_feature_request(...) m2/src/logitech_hidpp.cpp:766
        #2 rawaccel::HidppTransport::feature_request(...)       m2/src/logitech_hidpp.cpp:1085

`:766` = `std::memcpy(request.data() + 4, params, param_len)` — 20 baytlık dizinin
içine 17 bayt yazılıyor, **4 bayt taşma.**

⭐ **Testin "varmış gibi görünen" ancak boş olan iddiası kanıtlandı.**
`tests/test_accel.cpp:600` bütçe testi gibi görünüyor:

    600:  EXPECT(!unavailable.feature_request(0x2201, 0, nullptr, 17).has_value());

Ama `unavailable` = `HidppTransport("/dev/nonexistent-rawaccel-hidpp")` (`:596`) —
**transport kapalı.** `write_packet()` `fd_ < 0` yüzünden zaten `false` döner
(`:674`), yani `nullopt` **bütçe kontrolünden değil, kapalı fd'den** geliyor.
M2 mutantında bu satır **hâlâ yeşil** — çünkü bütçe kontrolü o noktaya hiç
ulaşmıyor olmasa da test geçiyor. **Bu satır hiçbir şey ölçmüyor.**

**`test_hidpp_short_payload_budget` gerçekten neyi ölçüyor (`:844-913`):**
sabit eşitlikleri (`16`, `14`, `2+14==16`), bir tamsayı bölme döngüsü
(`(len+13)/14`) ve `hidpp_long_packet` tel düzeni round-trip'i. **Koruma
çağrısının kendisi test edilmiyor** — çünkü `send_feature_request`/`feature_request`
`private`/donanım gerektiriyor ve test seam'i **kasıtlı olarak** eklenmemiş
(`:838-842` yorumu bunu açıkça söylüyor). Doğru karar, **yanlış izlenim bırakıyor.**

---

#### L06-B3 | src/logitech_hidpp.cpp:790,891,938,985 | **MED** — ⭐ DEPO ÜST SINIRI VAR, TESTİ YOK

**Mutasyon (M3)** — 4 noktadaki `if (pending_notifications_.size() < 16)` silindi:

    $ grep -c "pending_notifications_.size() < 16" src/logitech_hidpp.cpp
    0
    $ bash tests/run_tests.sh
    === Sonuç: 34164/34164 geçti ===
    RC=0                       # ← KIRMIZI DEĞİL

**Üst sınır yük taşıyor (ölçüldü):** 120 ms'lik bir istek penceresinde
bilinmeyen-feature bildirim seli (`:789-791` stash dalı):

    ### BASE (bound = 16) ###        shim: rx_packets=17,517,066
    DRAINED FROM STASH = 16
    ### M3 MUTANT (bound REMOVED) ### shim: rx_packets=13,417,300
    DRAINED FROM STASH = 100000

**Ölçülen sınırlar:**
- Üst sınır **VAR**: 16. Bellek sızıntısı **yok** (soru 4'ün cevabı: sınırsız büyüme yok).
- Ama **sessiz** düşüyor: 17,517,066 bildirimden yalnız 16'sı saklandı,
  **17,517,050'si sessizce atıldı.** Sınırın kendisi doğru; sınırı aşanların
  düşüşü **raporlanmıyor** (drop sayacı/log yok).
- Üst sınır olmasaydı: 17,5M × ~96 B ≈ **1,6 GiB** tek istek penceresinde.

**Sorduğun "bilinmeyen bildirim ne oluyor" sorusunun cevabı (ölçülmüş):**
`classify_hidpp_notification` (`:2496-2505`) — feature indeksi cihazın haritasında
yoksa → `hidpp_notification_event_kind::unhandled`; daemon bunu
`daemon.cpp:1952` `if (event.kind == unhandled) continue;` ile **düşürüyor**,
kuyruğa almıyor. Yani **sınırsız büyüme riski yok.** ✔

---

#### L06-B4 | src/logitech_hidpp.cpp:1845, :1869, :1988, :2041 | **MED** — ⭐ SESSİZ YEŞİL (dönüş değeri 3 çağrıda da yok sayılıyor)

`disable_onboard_profiles_for_write()` iki yerden `true` döndürüyor:
- `:1845` `if (!index) return true;` — 0x8100 çözümlenemedi (yok **veya** zaman aşımı)
- `:1859` `if (!onboard_active) return true;` — cihaz zaten onboard modunda değil

`set_dpi` / `set_polling_rate` / `set_lift_off_distance` bu değeri **hiç okumuyor:**

    1869:  disable_onboard_profiles_for_write(target_device_index);   // dönüş atıldı
    1988:  disable_onboard_profiles_for_write(target_device_index);   // dönüş atıldı
    2041:  disable_onboard_profiles_for_write(target_device_index);   // dönüş atıldı

    $ grep -rn "disable_onboard" tests/ | head
    (çıktı yok)                    # dönüş değeri HİÇBİR YERDE test edilmiyor

**Neden bulgu:** Fonksiyon `bool` döndürüyor — bu, "başarılı/başarısız" bilgisi
taşıdığı anlamına gelir. 3/3 çağırıcı sessizce geçiyorsa bu bir **ölü dönüş
değeri**; okuyucu "başarı döndü, sorun yok" varsayar. Gerçekte `:1845`'te
zaman aşımı da `true` demektir (0x8100 yok **veya** cihaz cevap vermiyor — ayırt
edilemiyor), dolayısıyla bu `true` "güvendeyim" anlamına gelmez.

**Bu kod çalışırken ne fark edilir?** Cihaz onboard profil modundayken host
DPI/rate yazmasını reddeder; `disable_...` başarısız olur, yazma yine de
denenir ve başarısız döner. Yani **dışarıdan görünen sonuç değişmiyor** — bulgu
"yanlış davranış" değil, **"doğru davranışı taşıyan ama taşımadığı gibi
görünen bir API"**. `HIGH` değil `MED`: ölçülebilir bir hatalı sonuç yok, ama
gelecekte bu değeri okumaya başlayan biri yanlış bir garantiye güvenir.

---

#### L06-B5 | src/logitech_hidpp.cpp:1113-1118 | **LOW** (bilinçli, dokümente) — "başarılı" sayılan ama doğrulanmayan yol

    1113: // HID++ 1.0 register writes (0x80RR) are fire-and-forget: no ACK is sent
    1114: // by the device. ... Return success immediately after a successful
    1115: // write_packet (the kernel accepted the report).
    1118: return true;

Hiçbir yerde cevap **beklenmiyor** ve `true` yalnız `write_packet()` başarısını
ifade ediyor — **bu yorum açık ve doğru.** Tek üretim çağrısı
`src/logitech_hidpp.cpp:2415` (bildirim register'ı yazımı) ve dönüş değeri
orada da **kullanılmıyor.** Bu yüzden `LOW`: yanlış bir "başarı" iddiası
yayılmıyor. *(Sorun 6'nın doğrudan cevabı: zaman aşımı hiçbir yerde "başarı"
sayılmıyor — `send_feature_request`/`send_short`/`send_long` hepsi zaman aşımında
`std::nullopt`/`false` döndürüyor, ölçüldü.)*

---

#### L06-B6 | include/logitech_hidpp.hpp:731 · src/logitech_hidpp.cpp:2558 | **MED** — ⭐ ÖLÜ KOD: sadece test çağırıyor

`drain_hidpp_notifications()` tanımlı, başlığa **dışa açık** bildirilmiş, testte
çağrılıyor — ama **hiçbir üretim yeri çağırmıyor.**

    $ grep -rn "drain_hidpp_notifications" daemon/ gui/ cli/ src/ include/
    src/logitech_hidpp.cpp:2558:size_t drain_hidpp_notifications(     ← tanım
    include/logitech_hidpp.hpp:731:size_t drain_hidpp_notifications(  ← bildirim
    $ grep -n "drain_hidpp_notifications" tests/*.cpp
    tests/test_accel.cpp:379:  // açıklama
    tests/test_accel.cpp:386:  delivered += drain_hidpp_notifications(  ← TEK çağrı

**Pozitif kontrol** (taramanın bozuk olmadığını kanıtlar — aynı desen
`drain_notifications` için 4 üretim noktası bulur):
`daemon.cpp:1932` ve `gui/hidpp_panel.inl:655` doğrudan
`transport.drain_notifications(...)` çağırıyor ve sınıflandırmayı **elle**
yapıyor (`daemon.cpp:1949` `classify_hidpp_notification`). Yani sarmalayıcının
varlığı **hiçbir şey kazandırmıyor**, sadece test kütüphanesine bakıyor.

**Bu kod çalışırken ne fark edilir?** Fark edilmez — hiç çalışmıyor. Sadece
"test ediliyor" yanılsaması üretiyor. `MED`, çünkü davranışsal risk yok.

---

#### L06-B7 | include/logitech_hidpp.hpp:633 · daemon/daemon.cpp:1228 | **LOW** — kuyruk boyutunda üst sınır yok

`hidpp_hw_enqueue` yalnız `enqueueable()` kontrolü yapıyor, **boyut sınırı yok:**

    inline bool hidpp_hw_enqueue(std::deque<hidpp_hw_job>& q, hidpp_hw_job&& job) {
        if (!job.enqueueable()) return false;
        q.push_back(std::move(job));        // ← boyut sınırı YOK
        return true;
    }

    $ grep -rn "hidpp_wq_" daemon/ | grep -i "size()\|max\|cap\|limit"
    daemon/daemon.cpp:1714:  dropped = hidpp_wq_.size();   ← yalnız kapanış sayacı

**Neden düşük:** dedup sensörü (`hidpp_hw_plan_job` tek adımda işaretler) aynı
değer için ikinci işi kuyruğa almıyor — ölçülen sınır **farklı değer sayısı**,
yani bir kullanıcının sürükleyebileceği DPI değerleri kadar. `drain_hidpp_writes`
kuyruğu **tüketene kadar** boşaltıyor (`daemon.cpp:1737 for(;;)`), "bir tur"
sınırı yok (`:1731` yorumu "Bounded per pass ... One full pass is enough" diyor —
**bu cümle kodla uyuşmuyor**, döngü `for(;;)` ve yalnız kuyruk boşalınca çıkıyor).
Pratikte sürükleme 25-30 değerle sınırlı (`test_accel.cpp:1130` ölçümü: 25 iş).

---

#### L06-B8 | include/logitech_hidpp.hpp:564-579 · tests/test_accel.cpp:919-1038 | **INFO** — FIFO/ownership/dedup **gerçekten** test altında (olumlu bulgu)

Soru 3'ün cevabı: **evet, FIFO gerçek.** Üç mutasyon da kırmızıya döndü:

| Mutasyon | Kırmızı satır | rc |
|---|---|---|
| M4 `hidpp_hw_take` → `pop_back()` (LIFO) | `test_accel.cpp:1186-1187` | 1 |
| M5 `hidpp_hw_plan_job` `mark_attempted` silindi | `test_accel.cpp:1105,1106,1107,1112` | 1 |
| M6 `plan_for` `t != transport` kontrolü silindi | `test_accel.cpp:999,1000,1033,1036` | 1 |

    $ grep -E "^=== Sonuç|RC=" /tmp/opencode/L06/m4.log
    === Sonuç: ... FAIL test_accel.cpp:1186  out.dpi == 100*(i+1)   (FIFO order preserved)
    RC=1

**Eşzamanlılık kapsamı (sorunun 2. yarısı) — ölçülen boşluk:** `test_hidpp_hw_sync_guard`
**tek iş parçacıklıdır; eşzamanlılık ölçmez.** 5 HID++ donanım testinin
tamamında (`:844-1214`) concurrency primitive **yok:**

    $ awk 'NR>=844 && NR<=1214' tests/test_accel.cpp | grep -cE "std::thread|std::mutex|std::atomic|std::async"
    0
    $ grep -cE "std::thread|std::mutex|std::atomic" tests/test_accel.cpp   # pozitif kontrol
    0                                    # 10.000 satırlık dosyanın tamamında 0

Yani "`test_hidpp_hw_sync_guard` eşzamanlı iki yazma senaryosunu ölüyor mu?"
→ **Hayır.** O test saf sıralı aritmetik (`plan_for`/`mark_attempted`).
Koruyan şey kuyruk **veri yapısının** FIFO olması + `daemon.cpp:1740`'ta
tek tüketicinin `hidpp_wq_mu_` altında `pop_front` yapması. Sıralama **kodla**
garanti ediliyor, testle değil. Pratikte **tek tüketici** var
(`run_hidpp_worker` → `drain_hidpp_writes`), bu yüzden yarış görünmüyor.

---

#### L06-B9 | (donanım yok) | **INFO** — ⭐ TEMİZ: çökme yok, askı yok, 0 istisna

Bu makinede Logitech donanımı **yok** (tek hidraw düğümü VMware faresi,
`HID_ID=0003:00000E0F:00000003`, Logitech `046d` **değil**). 37 adet
HidppTransport giriş noktasının **tamamı** kapalı transport üzerinde çağrıldı:

    $ ./nohw
    is_open() = 0 (expect 0)
    ... 37 satır: probe_hidpp10 / read_register / write_register / feature_request /
        send_short / send_long / send_very_long / get_feature_set / get_device_info /
        get_battery_status / get_pairing_info / get_dpi_info / set_dpi /
        set_polling_rate / set_lift_off_distance / get_change_host_info /
        set_change_host / led_feature_id / get_led_brightness /
        write_onboard_profile_sector / receive_notification / drain_notifications /
        drain_hidpp_notifications / identify_logitech_device(s) / discover ...  = ok
    THREW count = 0
    RUN_RC=0 (124 = HANG değil)

    $ ./probe_disc
    discovered=0  (empty => no Logitech hidraw on this host)

**Cevap: 0 istisna, 0 çökme, 0 askı.** Donanım yokken tüm yollar temiz.
Ayrıca `HidppTransport` kurucusu `:630-631`'de `HIDIOCGRAWINFO` başarısızlığında
fd'yi kapatıp `fd_ = -1` yapıyor — yani açılamayan düğüm "açık" sayılmıyor,
çağıran taraf `is_open()` ile doğru şekilde eleniyor.

---

## KAPI

```
# 1) Temel koşu (ölçüm tabanı) — /tmp/opencode/L06/base
$ bash tests/run_tests.sh
=== Sonuç: 34164/34164 geçti ===
SIMD parity kapısı: AVX2/SSE2/skaler birbirinin aynısı ✓
rc=0

# 2) 6 mutasyon — her biri kendi /tmp kopyasında
m1 (uzunluk kontrolü silindi)          → === Sonuç: 34164/34164 geçti ===  RC=0   ⛔
m2 (param_len bütçe koruması silindi)  → === Sonuç: 34164/34164 geçti ===  RC=0   ⛔
m3 (bildirim deposu 16-sınırı silindi) → === Sonuç: 34164/34164 geçti ===  RC=0   ⛔
m4 (hidpp_hw_take → LIFO)              → FAIL test_accel.cpp:1186-1187    RC=1   ✔
m5 (plan_job mark_attempted silindi)   → FAIL test_accel.cpp:1105,1106,1107,1112  RC=1  ✔
m6 (plan_for transport kontrolü sildi) → FAIL test_accel.cpp:999,1000,1033,1036    RC=1  ✔

# 3) Yük-taşıma kanıtları (sentetik hidraw düğümü, LD_PRELOAD, /tmp'de)
$ ./driver     liar1   (temiz kod)  → nullopt   rc=0
$ ./driver_m1  liar1   (M1 mutant)  → std::length_error → rc=134 (SIGABRT)
$ ./g_base   param_len=17  (temiz)   → nullopt (rejected locally)
$ ./g_m2     param_len=17  (M2)      → ASan: stack-buffer-overflow WRITE of size 17 @ :766
$ ./stash    (temiz)   17,517,066 bildirim → DRAINED = 16
$ ./stash_m3 (M3)      13,417,300 bildirim → DRAINED = 100000
$ ./nohw  → THREW count = 0, rc=0 (donanım yokken 37 giriş noktası)
```

**ÜRETİLEN SAYI:** 6 mutasyon · 3 kırmızı / 3 yeşil · 1 ASan taşma · 1 SIGABRT ·
1 bellek sınırı ölçümü (16 vs 100000) · 37 giriş noktası temizlik testi ·
0 istisna.

---

## KAPSANMAYAN

- **`include/logitech_quirks.hpp` (L07'nin sahibi)** — sadece `get_battery_status`
  (:1476-1480) yorumu üzerinden okundu, denetlenmedi.
- **`daemon/daemon.cpp` `drain_hidpp_writes` / `run_hidpp_worker`** — bu lane'in
  dosyası değil; L06-B7'deki "Bounded per pass" yorumu ile `for(;;)` döngüsünün
  uyuşmazlığı **orada** doğrulanabilir, burada yalnız okundu.
- **`gui/hidpp_panel.inl:655`** — `drain_notifications(8, 2ms)` çağrısı okundu,
  GUI'nin HID++ paneli denetlenmedi.
- **Kuyruk üst sınırı (L06-B7)** — "pratikte sınırlı" hükmü `test_accel.cpp:1130`'daki
  25-değer sürükleme senaryosundan türetildi; **gerçek bir GUI sürüklemesi
  ölçülmedi.**
- **`decode_dpi_levels` (:78-111) 2048 seviye tavanı** ve `get_dpi_info`'daki
  256-chunk döngüleri — bütçe/timeout korumaları kodda mevcut, mutasyonla
  sınanmadı (zaman).

---

## TEMSİL SINIRI

1. ⛔ **Fiziksel Logitech donanımı yok.** `ls /dev/hidraw*` → yalnız
   `/dev/hidraw0` = VMware VMware Virtual USB Mouse (`HID_ID=0003:00000E0F:00000003`).
   Bu yüzden:
   - **Uçtan uca cihaz round-trip'i doğrulanamadı.** `identify_logitech_device`,
     `get_dpi_info`, `set_dpi`, `set_polling_rate` gerçek donanımda **hiç
     çalıştırılmadı**; tüm donanım yolu **sentetik hidraw düğümü** (LD_PRELOAD
     shim) ile taklit edildi. Bu, **gerçek sürücünün** `read()` uzunluklarını
     taklit eder, onunla aynı değildir.
   - `liar*` (L06-B1) ve `param_len=17` (L06-B2) senaryoları **normal bir cihazın
     üretebileceği durumlar DEĞİLDİR**; bunlar korumanın **beklenen
     davranışını** ölçer. `liar*` dürüst olmayan bir fd gerektirir. Bu
     yüzden bulgular **"mevcut kod açık"** değil, **"mevcut koruma testsiz"**
     sınıfındadır — `HIGH` not `CRIT`.
2. ⛔ **Eşzamanlılık çalıştırılmadı.** `test_hidpp_hw_sync_guard` tek iş
   parçacıklıdır (L06-B8); iki yazmanın sırasını bozup bozmayacağı **kod
   incelemesiyle** söylendi (`daemon.cpp:1740` tek tüketici + `pop_front`), **eşzamanlı
   bir koşu ile ölçülmedi.** `daemon.cpp` L06'nın sahibi değil.
3. ⛔ **ASan/UBSan`in yedi kapıdan biri olmadığı** (AGENTS.md'ye göre)
   `src/logitech_hidpp.cpp` **kanonik pipeline'da sanitizer altında hiç
   koşmuyor.** Yukarıdaki ASan çıktısı **denetim için /tmp'de derlenmiş özel bir
   ikilinin** sonucudur, projenin kendi kapısının değil. Yani bu TU'yu bir
   sanitizer işi yakalasa, yedi kapı **bugün** yeşil kalırdı.
4. ⛔ **`drain_hidpp_notifications` yalnız testten çağrıldığı** kanıtlandı
   (L06-B6) — ancak bu lane'in dışındaki bir yol (dinamik `dlsym`, plugin)
   çağırıyor olabilir. Proje ağacında `grep -rn` ile tarandı, tarama
   `--include=*.cpp/*.hpp/*.inl` ile kapsamlıydı; `.aihaberlesme/` **hariç
   tutuldu** (AGENTS.md'de kayıtlı ölçüm tuzağı).
5. ⛔ **`/tmp/opencode/L06` iki kez sunucu yeniden başlatmasıyla silindi.**
   Yeniden üretildi; tüm mutasyonlar ve ham çıktılar bu raporun KAPI
   bölümüne yazıldı, ancak **shim kaynak dosyaları artık diskte değil.**
   Yeniden üretmek için `shim.cpp`/`driver.cpp`/`stash.cpp`/`nohw.cpp`
   içerikleri bu raporun tarifine göre yazılabilir.

---

## AJAN 1 İÇİN NOT

- **Doğrulanması gereken 3 iddia** (AJ1 kendi komutuyla ölçmeli):
  1. `m1`/`m2`/`m3` yeşil kalıyor → **3 koruma testsiz.**
  2. `m2` mutantında ASan **stack-buffer-overflow** veriyor → koruma yük taşıyor.
  3. `tests/test_accel.cpp:600` **bütçe ölçmüyor** (kapalı transport).
- **Düzeltilecekse öneri** (kod değiştirmedim): ya `feature_request`/`send_feature_request`
  için test seam'i, ya da (L06-B6) `drain_hidpp_notifications`'in ya üretimden
  çağrılması ya da başlıktan kaldırılması. `test_hidpp_short_payload_budget`'in
  adı mevcut kapsamından **daha geniş bir korumayı** ima ediyor.
