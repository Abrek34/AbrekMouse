### L18 | alt-ajan (denetim alt-lanesi) | 2 Ekim 2026

KAPSAM   : tests/test_accel.cpp **satır 1–1217** (11 test fonksiyonu, 25 SECTION)
           tests/tr_coverage.cpp (435 satır) + tests/run_tr_coverage.sh (28 satır)
           Referans SADECE OKUNDU: include/logitech_hidpp.hpp, src/logitech_hidpp.cpp,
           src/logitech_receiver.cpp, include/logitech_quirks.hpp, daemon/daemon.cpp
           ⛔ Hiçbir dosya değiştirilmedi. Mutasyonlar /home/a/.l18-audit kopyasında.

===============================================================================
KAPI     : 1) gcc 14 `g++ -std=c++20 -O0 -w -Iinclude -Isrc` ile 4 TU
               (test_accel.cpp + config.cpp + logitech_receiver.cpp + logitech_hidpp.cpp)
               pristine binary → `./clean_test_accel --filter <lane> --quiet`
               → `=== Sonuç: 481/481 geçti (25 section eşleşti, 192 atlandı) ===`  rc=0
           2) `bash tests/run_tr_coverage.sh` (pristine ağaç)
               → `tr.inl dictionary: 313 entries / translated call sites: 287 unique
                  strings / dynamic (skipped) calls: 30 / MISSING: none / Result: PASS`
               rc=0
           3) Mutasyon bataryası: 35 mutasyon × (kopyaya uygula → yeniden derle →
               lane bölümlerini koştur → kırmızı mı yeşil mi).
               Sonuç: **21 YAKALADI, 14 KAÇIRDI (N15 kontrol sorgusu dâhil),
               1 geçersiz**. Ayrıca tr_coverage kapısında 5 mutasyon:
               2 yakaladı, 3 kaçırdı. Ham çıktılar aşağıda ve
               `.aihaberlesme/mesajlar/L18-kanit/` altında.

===============================================================================
BÖLÜM BÖLÜM GERÇEK ASSERT SAYISI (pristine, `--filter` + `--quiet`)
  (statik `grep -c` siteleri parantez içinde; çalışan sayı = satır sayısı × döngü)
  bölüm                                                  çalışan   statik
  ─────────────────────────────────────────────────────────────  ───────  ──────
  Logitech receiver discovery — capability                     11       10
  HID++ notification classification                             40       37
  Logitech HID++ packet serialization                         101       27
  Logitech hidraw device discovery                              6        6
  Logitech HID++ hardware-control rate mappings                84      (104*)
  Logitech HID++ battery parsing stays conservative            7        7
  Logitech HID++ 1.0 register 0x07 battery decoding             3        3
  Logitech HID++ battery feature/register layouts              3        3
  Logitech HID++ firmware and transport-ID offsets              7        7
  O31-H2 — composed modelId is only ever "" or 12 chars         4       (10*)
  O31-H2 — the "32" row is unreachable                          0  ⛔     (1 site)
  O31-H2 — the two real 12-char keys still resolve              7
  O31-H1 — onboard sector chunk fits the budget                50       22
  O31-H1 — long packet wire layout is 4+16                      44
  O31-D1 — a never-pushed device still writes its first value    8       (21*)
  O31-D1 — 0 is an unambiguous 'never pushed' sentinel          41
  O31-D1 — a changed field is pushed on its own                  4
  O31-D1 — a rebuilt transport re-pushes both fields             4
  O31-D1 — a full daemon sequence hits the blocking path once    2
  job transport outlives the map entry (rescan erase)            6        6
  same values twice -> exactly one job                         10       (20*)
  slider drag: N distinct values -> N jobs, never more           6
  replug re-pushes both fields                                   4
  FIFO order preserved                                          22       (13*)
  non-enqueueable jobs are rejected                             7
  ─────────────────────────────────────────────────────────────  ───────
  LANE TOPLAMI (11 test / 25 SECTION)                          481
  (*) = o test fonksiyonunun TÜM bölümlerinin statik toplamı; yanındaki
      çalışan sayı o TEK bölümün çalışan assert'idir.

  ⛔ **B-01 CRIT — `tests/test_accel.cpp:778-806` 0 ASSERT ÇALIŞTIRIYOR.**
    SECTION("O31-H2 — the \"32\" row is unreachable from any device shape")
    kanıt (pristine, ham çıktı):
      $ ./clean_test_accel --filter 'O31-H2 — the "32" row'
      === RawAccel Linux Birim Testleri ===
      [O31-H2 — the "32" row is unreachable from any device shape]
      === Sonuç: 0/0 geçti (1 section eşleşti, 216 atlandı) ===
    Neden: tek EXPECT (`:804 EXPECT(q != &LOGITECH_QUIRKS[2].quirks)`) `if (q)`
    içinde; `find_logitech_quirks()` sentezlenen 12-char id'ler için **her zaman**
    nullptr döndüğü için `q` hiç true olmuyor → assert hiç çalışmıyor.
    Bu bölümün varlık sebebi "32 satırına ulaşılamaz" diye; o halde kendisi de
    ulaşılamaz bir şeyi ölçüyor. Dosyadaki `:735-737` yorumu "if the id parsing is
    ever changed so that a short id becomes reachable, these assertions fail"
    diyor — bu iddia B-03 ile ÇÜRÜTÜLDÜ (aşağıda).

===============================================================================
SORU 1 — VAKUM TESTİ Mİ? "Kodu bilerek bozarsam bu test kırmızıya döner mi?"
  (34 mutasyonun tamamı; ⛔=YEŞİL KALDI, ✔=KIRMIZI. Hepsi /home/a/.l18-audit
   kopyasında, çalışma ağacına dokunulmadan.)

  ── test_logitech_hw_sync_guard (5 bölüm / 59 assert) — VAKUM DEĞİL, güçlü ──
  M01  include/logitech_hidpp.hpp:565  `if (t != transport) return {true,true};` → sil
       ✔ KIRMIZI  108/114, 6 FAIL  (:999, :1000, :1033)
       gerekçe: replug senaryosu (`:998-1000`) ve 7-adımlı sayaç (`:1033`) kırılıyor.
  M02a include/logitech_hidpp.hpp:561  `polling_rate = 0` → `= 1000`
       ✔ KIRMIZI  112/114, 2 FAIL  (:941, :946)
  M02b include/logitech_hidpp.hpp:562  `dpi = 0` → `= 800`
       ✔ KIRMIZI  112/114, 2 FAIL  (:942, :947)
       → Yorumda "kanıtlandı" denilen "sentinel 800/1000 ile tohumlanırsa test
         kırmızıya döner" iddiası **DOĞRU ÇIKTI**. Alan assertleri (`:941-947`)
         yazının savunduğu şeyin tam olarak kendisi.
  (M03–M06 aşağıdaki diğer 3 testte.)

  ── test_hidpp_hw_queue_ownership (1 bölüm / 6 assert) — VAKUM DEĞİL ──
  Test `map<shared_ptr<void>>` + `weak_ptr` + `make_shared<int>(42)` kullanıyor;
  `job.transport` map girdisi silindikten sonra `!probe.expired()` (`:1076`) ve
  sonra `job.transport.reset()` → `probe.expired()` (`:1082`) iddiaları,
  "canlı kalma" ile "sızıntı"ı birbirinden ayırıyor. Bu ikili gerçek bir ayrım.
  (Ama B-06: tip `HidppTransport` değil `int`.)

  ── test_hidpp_hw_queue_dedup (3 bölüm / 20 assert) — VAKUM DEĞİL ──
  M04  include/logitech_hidpp.hpp:634  `if (!job.enqueueable()) return false;` → sil
       ✔ KIRMIZI  107/114, 7 FAIL  (:1106, :1107, :1114, …)
  M05  include/logitech_hidpp.hpp:684  `sync.mark_attempted(t, rate, dpi);` → sil
       ✔ KIRMIZI  388/397, 9 FAIL  (:1105, :1106, :1107, …)
       → "plan ve mark tek adımda" değişmezse dedup'un kaybolduğu test tarafından
         gerçekten yakalanıyor. Slayt drag bölümü (`:1141-1151`) de kırılıyor.

  ── test_hidpp_hw_queue_fifo (2 bölüm / 29 assert) — VAKUM DEĞİL ──
  M03  include/logitech_hidpp.hpp:644  `q.front()` → `q.back()` (LIFO)
       ✔ KIRMIZI  rc=139 (SIGSEGV), 3 FAIL satırı  (:1186, :1187)
  M06  include/logitech_hidpp.hpp:627  `dev_idx != 0xFF &&` → sil
       ✔ KIRMIZI  111/114, 3 FAIL  (:1204, :1205, :1212)

  ── test_logitech_receiver_discovery (11 assert) — VAKUM DEĞİL, 3 satırı zayıf ──
  R01 src/logitech_receiver.cpp:54  Bolt slots 6→4            ✔ (:189)
  R02 src/logitech_receiver.cpp:41  c542 hidpp false→true    ✔ (:193)
  R03 src/logitech_receiver.cpp:30  c52f hidpp true→false    ✔ (:196)
  R04 src/logitech_receiver.cpp:54  Bolt kind bolt→nano      ✔ (:198, :201)
  R05 src/logitech_receiver.cpp:98  vendor filtresi silindi  ✔ (:201, :202, :203)
  R06 src/logitech_receiver.cpp:83  "Lightspeed"→"lightspeed" ✔ (:205)
  ⚠ zayıf satırlar: `:189 EXPECT(r.max_paired_devices == 6)` ve `:198 EXPECT(...
  max_paired_devices == 1)` YALNIZCA `kind` dalının içinde. 25 satırlık alıcı
  tablosundan yalnızca 3 ürün (c548 / c542 / c52f) denetleniyor → **25/25
  tablonun 12'si hiç test edilmiyor.** Kırılan satır sayısı 25 tablo için 1'dir.

  ── test_logitech_hidpp_notification_classification (40 assert) — 1 BOŞLUK ──
  N11 src/logitech_hidpp.cpp:2482 CONNECTED 0x42 bit-0 ters çevrildi  ✔ (:285,:290)
  N12 src/logitech_hidpp.cpp:2524 battery_voltage BE16→LE16         ✔ (:353)
  S01 src/logitech_hidpp.cpp:2512 unified_battery (0x1004) dalı
        `event.battery` ATANMADI (nullopt)                    ⛔ **480/481 YEŞİL**
  ⛔ **B-02 HIGH — `tests/test_accel.cpp:334-336` yumuşak assert.**
    Bu bloktaki 5 pil case'inin 4'ünde koruma dışarıda var
    (`:235, :247, :323, :349 EXPECT(ev.battery.has_value())`); 0x1004'te YOK.
    Kanıt: `event.battery`'yi hiç atamayınca `if (ev.battery)` false olur, `:336`
    atlanır, 481'den 480'a düşer ve kapı **yeşil** kalır. S01 ham çıktı:
      MUT[S01] rc=0  !! YESIL KALDI (test YAKALAMADI)
      === Sonuç: 480/480 geçti (25 section eşleşti, 192 atlandı) ===
    Etki: "unified pil bildirimi 0xFF = bilinmiyor seviyesi verir" iddiası
    yalnızca `battery` doluysa denetleniyor; bildirim hiç pil üretmezse test
    sessizce yeşil. Diğer 4 case bu korumaya sahip → **sessiz yeşil sınıfı,
    tutarsız uygulanmış.**

  ── test_logitech_hidpp_packets (101 assert) — VAKUM DEĞİL, byte seviyesinde ──
  (SORU 4 cevabı: **byte seviyesinde doğrulanıyor**, "çökmüyor" değil.)
  N05 src/logitech_hidpp.cpp:460  byte-3 paketlemesi `<<4` → `<<0` (3 yerde)
      ✔ KIRMIZI 454/461, 7 FAIL — tam olarak byte dizisi (`:410`) ve round-trip
        alanları (`:420 function_id`, `:421 software_id`) kırıldı.
  ⛔ **B-07 MED — `tests/test_accel.cpp:414` asimetrik uzunluk reddi.**
    Kısa paket için yalnızca `size()-1` (=6 bayt) reddi denetleniyor; **fazla
    bayt hiç denetlenmiyor** (uzun pakette `:437` `size()+1` denetleniyor).
    Kanıt: src/logitech_hidpp.cpp:466 `if (!data || len != 7)` → `if (!data || len < 7)`
      MUT[N06] rc=0  !! YESIL KALDI — `=== Sonuç: 461/461 geçti ===`
    Uzun paketin `len != 20` reddi testte gerçekten var; kısa paketinki yok.
  Doğrulanan bayt düzeyi: kısa `:410-413` 7 baytın 7'si; uzun `:438-439` 3 bayt
  + 16 parametre round-trip `:445-447`; çok-uzun `:462-463` 3 bayt + 60 parametre
  `:469-471`. Yani 101 assert'in 80'i döngüsel (16+60+16) ama her biri ayrı bayt.

  ── test_logitech_hidraw_discovery (6 assert) — ⛔ B-08 HIGH: BOŞ test ──
  N14 POZİTİF KONTROL: `discover_logitech_hidraw_devices()` her koşulda
      `{"SENTINEL/SAHTE-BASARI"}` döndürsün → ✔ KIRMIZI (:483, :495)
      → yani test "boş sonuç"u gerçekten arıyor.
  N09 src/logitech_hidpp.cpp:2216 `if (!fs::is_character_file(p, fec)) continue;` → sil
      ⛔ YEŞİL 461/461   → :486-488 yorumunun savunduğu "yanlış tipte düğüm
      hiç açılıp ioctl edilmez" koruması **testle hiç sabitlenmiyor.**
  N10 src/logitech_hidpp.cpp:2229 `if (info.vendor == 0x046d)` → `if (true)`
      ⛔ YEŞİL 461/461   → **vendor filtresi (fonksiyonun tek işi) test edilmiyor.**
  N15 POZİTİF KONTROL: fixture'a `/dev/zero`'ya `hidraw7` symlink'i eklendi,
      `EXPECT(leaked.empty())` → ⛔ YEŞİL 398/398.
  ⛔ Sonuç: bu testin 6 assert'inin **hepsi "bulunamadı" yönünde.** Hiçbiri
  "bulundu" yönünü sınamıyor; fixture düğümleri normal dosya, ioctl zaten
  başarısız olduğu için iki filtre de aynı sonucu veriyor — yani doğru cevap
  **tesadüfen** geliyor, korumalar sayesinde değil.

  ── test_logitech_hidpp_hardware_controls (5 bölüm / 104 assert) ──
  N07 src/logitech_hidpp.cpp:302 normalize_function_id() normalizasyonu silindi
      ✔ (:517, :518, :528)
  N13 src/logitech_hidpp.cpp:48  `code > 0 &&` → `code <`   ✔ (:541)
  R01/R02/R03 → receiver tabloları (yukarıda)
  ⛔ **B-09 MED — hız kodu tablosu 3/9 ve 3/8 kapsanıyor.**
    `rate_code_to_hz(false, …)` üretim tablosu 9 girişli (src/logitech_hidpp.cpp:47),
    testte yalnızca kod 0, 1, 8 denetleniyor (`:539-541`) → 6/9 denetlenmiyor.
    `rate_code_to_hz(true, …)` 7 girişli (`:42`), testte 0, 6, 7 → **5/7
    denetlenmiyor** (250/500/1000/2000/4000 Hz karşılıkları).
    Kanıt: src/logitech_hidpp.cpp:47 `333` → `334` (2 yerde)
      MUT[N08] rc=0  !! YESIL KALDI — 461/461
    ⛔ **B-10 LOW — `hidpp_feature_name()` 5/31.**
    31 `case` kolundan yalnızca 4'ü + `unknown` doğrulanıyor (`:552-556`:
    0x2201, 0x0003, 0x0005, 0x1004, 0xDEAD). Kalan 27 özellik adı test edilmemiş.
  ⛔ **B-11 MED — `HidppTransport` "kapalıyken" assert kümesi (`:596-615`, 18 assert).**
    Hepsi `/dev/nonexistent-rawaccel-hidpp` üzerinde. `read_register`,
    `feature_request`, `supports_feature`, `get_onboard_profile_info`,
    `read_onboard_profile_sector`, `write_register`, `receive_notification`,
    `drain_notifications` → hepsi "başarısız" bekleniyor. **Bunların hiçbiri
    donanımsız makinede doğrulanamaz**; ölçülebilir tek şey "çökmüyor".
    En kritik örnek B-12.

  ── test_logitech_quirks_model_id_shape (3 bölüm / 11 assert) ──
  Q02 include/logitech_quirks.hpp:259 compose_model_id() `if (!empty) return model_id;`
      → `if (false)`  ✔ (:774, :775, :776)
  Q03 include/logitech_quirks.hpp:122 LOGITECH_QUIRKS[2] "32"→"32X"  ✔ (:821, :822)
  Q07 src/logitech_hidpp.cpp:289 parse_firmware_record() model_id 9/6→10/5 ✔ (:712)
  Q01v2 include/logitech_quirks.hpp:130 find_logitech_quirks() **önek havuzu**
      (kısa anahtar) geri getirildi → ✔ (:804 ×16)
      → yani "32" satırının **yeniden erişilebilir** olması hâlinde B-01'deki
        assert ateşliyor. Bölüm "ölü" değil; **koşullu** bir gardiyadır.
  ⛔ **B-03 CRIT — gardiyan rayının iddia ettiği pars değişimini YAKALAMAZ.**
    `test_logitech_quirks_model_id_shape` `hidpp_device_info` alanlarını
    **testin kendi `take_id` lambda'sıyla** dolduruyor (`:753-764`, `:789-800`) —
    yani `identify()`'in gerçek parse yerini **yeniden uyguluyor**, çağırmıyor.
    Kanıt — src/logitech_hidpp.cpp:1398 `bytes_to_hex(count->data() + 7, 6)`
      → `+ 8, 4`  (8 hex, kısa id *üretilebilir* hale geliyor):
      MUT[Q04] rc=0  !! YESIL KALDI — 397/397
    İki mutasyon daha aynı yerde:
      Q05 src/logitech_hidpp.cpp:1405 `id_offset += 2` → `+= 1`
          MUT[Q05] rc=0  !! YESIL KALDI — 397/397
      Q06 src/logitech_hidpp.cpp:1397 `(*count)[6]` → `(*count)[7]`
          MUT[Q06] rc=0  !! YESIL KALDI — 397/397
      Q08 src/logitech_hidpp.cpp:1395 `count->size() >= 13` → `>= 12`
          MUT[Q08] rc=0  !! YESIL KALDI — 397/397
    ⛔ Yani `:735-737` yorumundaki "if the id parsing is ever changed so that a
    short id becomes reachable, these assertions fail" cümlesi **ÖLÇÜLMÜŞ OLARAK
    YANLIŞTIR.** `identify()`'de model-id parse'ı 4 farklı yolla bozulabiliyor,
    gardiyan hiçbiri kırmızıya dönmüyor. `logitech_compose_model_id()`
    *combinator* testte gerçekten sınanıyor (Q02 ✔) — ama **üreten kod** sınanmıyor.

===============================================================================
SORU 2 — `test_hidpp_short_payload_budget` gerçekten TAŞMAYI mı yakalıyor?
  CEVAP: ⛔ **HAYIR.** Taşımayı hiç yakalamıyor; sabit aritmetiği + tel yerleşimi
  doğruluyor. (94 assert / 2 bölüm)

  ⛔ **B-12 CRIT — bütçe reddi hiç test edilmiyor.**
  Tek "taşma" yolu `send_feature_request()`'ın `param_len > HIDPP_SHORT_PAYLOAD_MAX`
  reddidir (src/logitech_hidpp.cpp:747) ve `feature_request()`'inki (:1072).
    N01 src/logitech_hidpp.cpp:747 `if (param_len > HIDPP_SHORT_PAYLOAD_MAX)
       return std::nullopt;` → SİL
       ⛔ YEŞİL  `MUT[N01] rc=0 … === Sonuç: 461/461 geçti ===`
    N02 src/logitech_hidpp.cpp:1072 `if (feature_id == 0 || param_len > MAX)`
       → `if (feature_id == 0)`
       ⛔ YEŞİL  `MUT[N02] rc=0 … 461/461`
    Neden: `send_feature_request` **private**; test tek erişilebilir nokta olan
    `feature_request`'i **kapalı transport** üzerinde çağırıyor
    (`:600 EXPECT(!unavailable.feature_request(0x2201, 0, nullptr, 17))`).
    Kapalı transport zaten `resolve_feature_index` aşamasında nullopt döndüğü
    için bu assert **"bütçe reddedildi" ile "cihaz yok" ayrımını YAPAMAZ** —
    bu, brifingte sayılan "işlem başarısız oldu mu diye bakıp geçme" sınıfının
    tam kendisi. 17 bayt taşması verilse de 0 bayt verilse de aynı assert geçer.

  ⛔ **B-13 CRIT — test, yazıldığı hatanın kendisini yeniden üretmiyor.**
  `:833-836` yorumu: "the onboard-profile sector writer is the one caller that
  got this wrong … these assertions are what keep the two from drifting again."
    N04 src/logitech_hidpp.cpp:2195 `std::vector<uint8_t> params(2 + chunk, 0);`
       → `(2 + 16, 0)`   ⛔ **O31-H1 hatası birebir geri getirildi**
       ⛔ YEŞİL  `MUT[N04] rc=0 … 461/461`   (her sektör yazması yine reddedilir,
                                              yol yine sessizce hiç yazmaz)
    N03 src/logitech_hidpp.cpp:2193 `for (…; off += chunk)` → `off += chunk + 1`
       (her yazma bir bayt atlar/kaydırır)          ⛔ YEŞİL 461/461
    Yani `test_hidpp_short_payload_budget()` **O31-H1 regresyonunu
    yakalayamıyor.** Yakalayan tek şey `static_assert` (include/logitech_hidpp.hpp:109),
    ve o yalnızca *sabit* geri yazılırsa (literal 16) ateşler — sabiti
    **kullanmayı bırakmayı** (N04) yakalamaz.
  ⛔ Ayrıca `:861-872`'deki "chunking arithmetic" bölümü **testin kendi içinde
    yeniden yazılmış** bir döngü; `std::min` ile kendi `covered` sayacını
    besleyip `covered == len` diye kendini doğruluyor. O bölümdeki 50 assert'in
    **27'si** (`:866`) `:852`'de zaten var olan **döngü-değişmez** aynı
    ifadenin tekrarıdır (iç döngü toplam 0+1+1+1+2+2+3+8+9 = 27 tur).
    Sayım kanıtı: 5 (`:847,848,852,853,857`) + 27 + 9 + 9 = 50 ✔
    `:857 EXPECT(2 + 16 > HIDPP_SHORT_PAYLOAD_MAX)` ise **hiçbir üretim sembolü
    içermeyen** tam sayı aritmetiği (18 > 16) — kodla hiçbir bağı yok.
  Bölüm 2 (`:874-912`, 44 assert) **gerçek**: `bytes.size() == 4 + 16`, bayt[0..3],
    16 parametre round-trip. N05'in kırmızıya döndüğü yer burasıydı, bölüm 1 değil.
  ⚠ `:838-842` yorumu bu sınırı **dürüstçe** kabul ediyor ("covers the arithmetic
    and the wire layout, not the device round-trip") — yorum doğru, ama B-12/B-13
    yüzünden bölüm 1'in "arithmetic" iddiası bile üretim koduna bağlı değil.

===============================================================================
SORU 3 — EŞZAMANLILIK GERÇEKTEN TEST EDİLİYOR MU?  ⛔ **HAYIR, sahte.**
  Ham grep çıktısı (`sed -n '1,1217p' tests/test_accel.cpp | grep -n -E …`):
    $ sed -n '1,1217p' tests/test_accel.cpp \
        | grep -n -E 'std::thread|std::async|pthread_create|std::mutex|atomic|condition_variable'
      (çıktı boş, rc=1 — 0 eşleşme)
  ⛔ **B-14 HIGH — `sync_guard`/`ownership`/`dedup`/`fifo` dördü tek iş parçacığıyla.**
  Testler `hidpp_hw_sync`/`hidpp_hw_job`/`hidpp_hw_enqueue`/`hidpp_hw_take`/
  `hidpp_hw_plan_job` saf başlık fonksiyonlarını **tek iş parçacığıyla** çağırıyor.
  Test edilmeyenler:
   • `hidpp_wq_mu_` — daemon.cpp'te **3 ayrı lock noktası** var
     (daemon/daemon.cpp:1227, :1713, :1740) ve `drain_hidpp_writes()` 48 satır
     (daemon/daemon.cpp:1736-1783). `daemon.cpp` test ikilisine **bağlanmıyor**
     (tests/run_tests.sh:95-100 yalnız test_accel + config + logitech_receiver +
     logitech_hidpp) → **bu 3 lock noktasının hiçbiri hiçbir kapıda çalışmıyor.**
   • include/logitech_hidpp.hpp:603-609'da yazılı "LOCK ORDER (new invariant —
     keep hidpp_wq_mu_ a LEAF): asla bir daemon mutex altında, asla bloklayan
     yazma boyunca tutulmaz" — **bu değişmezin testi yok.**
   • `use_count()`/`expired()` (`ownership` testi) eşzamanlı yok etmeye karşı
     thread-safe olmayan operasyonlardır; test onları **tek iş parçacığıyla**
     çağırdığı için yarış koşulu hiç oluşmuyor.
   • `HidppTransport` içindeki `request_mutex_` (koruma altında) hiç test edilmiyor.
  ⛔ **B-15 MED — `ownership` testi gerçek tipi test etmiyor.**
  `hidpp_transports_` üretimde `unordered_map<string, shared_ptr<HidppTransport>>`
  (daemon/daemon.hpp:314) ve `find_hidpp_transport()` bir **sahiplik referansı**
  döndürüyor. Test bunun yerine `shared_ptr<void>` + `make_shared<int>(42)` kullanıyor
  (`:1058, :1176, :1128`). Yani `shared_ptr<HidppTransport>` → `shared_ptr<void>`
  örtük dönüşümü, `find_hidpp_transport()`'in nullptr/`dev_idx==0xFF` sözleşmesi ve
  `HidppTransport`'ın yok edilme anı test edilmiyor. Test, "eğer shared_ptr ise
  yaşar" **beklenti**sini doğruluyor; `int` üzerinden yapılanı C++ standart
  dışı tip dönüşümü (`static_cast<int*>(void*)`) geçerli olsa da üretim yolunu
  temsil etmiyor.

===============================================================================
SORU 5 — DONANIM GEREKTİREN TESTLER DONANIM YOKKEN NE YAPIYOR?
  ⛔ **"skip" kavramı bu test çerçevesinde YOK.** Ham grep:
    $ grep -n -i -E '\bskip\b|GTEST_SKIP|skip' tests/test_accel.cpp
      59:  static bool g_list_only = false;  // --list: print names, skip asserts
    (yani `skip` yalnızca `--list` bayrağının yorumunda; `g_skipped_sections`
     `tests/test_accel.cpp:54` sadece `--filter` ile **eşleşmeyen** bölümleri
     sayar — donanım yokluğuyla ilgisi yoktur.)
  ⛔ **B-16 MED — donanım gerektiren TEK çağrı hiçbir şey iddia etmiyor.**
    `tests/test_accel.cpp:500-501`:
        auto devices = discover_logitech_hidraw_devices();
        (void)devices; // count depends on hardware — just exercise the default root
    Bu, `tests/test_accel.cpp` içindeki **tek** gerçek-sistem taraması
    (varsayılan kök = `/dev`). 0 assert. Yani "donanımda çalışmadığını SAY"
    sorusunun dürüst cevabı: **bu lane'de donanımda koşmayı bekleyen test
    YOK; donanıma dokunan tek yer 0-assert bir "çökmüyor" çağrısı.**
  Ölçüm (bu makinede, gerçek donanım):
    $ /home/a/.l18-audit/probe
      discover_logitech_hidraw_devices() -> 2 cihaz
        /dev/hidraw2
        /dev/hidraw1
    → yani `:500` bu makinede **2 cihaz** buluyor ve test bunu doğrulamıyor
      (dohru olsa da yanlış olsa da yeşil).
  Donanımın **yokluğunda da yeşil** olan yerler — yani "sessizce geçen"
  yüzey: `:483 missing.empty()`, `:495 found.empty()`, `:505 identify nullopt`,
  `:509-511 closed transport`. Bunlar `/does/not/exist` ve fixture yolları
  kullandığı için donanım gerektirmez; **ama** N09/N10 kanıtı gösteriyor ki
  yeşilleri doğru sebepden değil.
  ⛔ Ayrıca: `HidppTransport::probe_hidpp10()` yalnızca **olumsuz** yönde
  sınanıyor (`:510`, `:511` — olmayan dosyada false). `true` dönmesi hiç
  test edilmiyor; `supports_feature`, `set_dpi`, `set_polling_rate` gibi
  yazma yollarının **hiçbiri** bu lane'de test edilmiyor.

===============================================================================
SORU 6 — `tr_coverage.cpp` GERÇEKTEN KAPSAM MI ÖLÇÜYOR?
  CEVAP: **Kısmen evet** (ölçtüğü kanıtlandı) **ama tabanı (floor) yok** —
  üç sessiz yeşil deliği ölçüldü.

  ── Ölçülen sayılar (pristine `bash tests/run_tr_coverage.sh`) ──
    tr.inl dictionary:      313 entries
    translated call sites:  287 unique strings
    dynamic (skipped) calls: 30
    empty-string calls:     0
    MISSING: none → Result: PASS
  Denetlenen anahtar: **287 benzersiz**. Gerçek literal çağrı yeri (TRC_DEBUG=1
  ham dağılım):
    $ TRC_DEBUG=1 tr_coverage <11 dosya> 2>&1 >/dev/null | grep '^DBG call' | sort | uniq -c
      167 DBG call tr      49 DBG call trf      40 DBG call trtip
       21 DBG call grid_row2   21 DBG call grid_row   12 DBG call trbtn
        6 DBG call trmlbl        4 DBG call trchk        2 DBG call trlbl
      → 322 literal çağrı yeri  +  31 `tr_combo_fill` dizi literal'i = 353 literal
        → bunlar 287 benzersiz anahtara indirgeniyor.
  Yani **353 literal geçişten 287 benzersiz anahtar doğrulanıyor; 30 çağrı yeri
  dinamik olduğu için hiç doğrulanamıyor** (uyarı olarak listeleniyor, kapı
  yine PASS) ve 26 sözlük girdisi öksüz ("ORPHANS").

  ⛔ **B-17 HIGH — sayı tabanı (floor) yok: tarayıcı hiçbir şey bulsa da PASS.**
    tests/tr_coverage.cpp:251 `used.insert(key);` → `/*MUT*/` (tarayıcı
    `tr/trf/trlbl/trmlbl/trbtn/trchk/trtip/grid_row` çağrılarının HİÇBİRINI
    toplamıyor; yalnız `tr_combo_fill` dizi yolu çalışıyor):
      MUT[T03] gate_rc=0  !! YESIL KALDI (gate YAKALAMADI)
        tr.inl dictionary: 313 | translated call sites: **17** unique strings
        dynamic (skipped) calls: 30 | Result: PASS
    ⛔ 287 → **17** anahtara düşüyor, kapı **PASS**. Bu, lane'in en pahalı
    sınıfı: "ölçüm çalışıyor görünüyor, hiçbir şey ölçülmüyor."

  ⛔ **B-18 HIGH — `(` çağrıdan sonraki satırın başındaysa çağrı GÖRÜNMEZ.**
    tests/tr_coverage.cpp:240 `while (j < src.size() && src[j] == ' ') j++;`
    — **yalnızca boşluk** atlanıyor, `'\n'`/`'\t'` atlanmıyor; sonraki
    `if (src[j] == '(')` başarısız olunca çağrı **sessizce** atlanıyor (ne
    `used`'e girer, ne `dynamic_calls`'a).
    Kanıt — gui/ui_builder.inl'e **çevrilmemiş** yeni bir anahtar eklendi:
        tr
            ("L18 KONTROL parantez sonraki satirda");
      MUT[T04] gate_rc=0  !! YESIL KALDI
        translated call sites: **287** (değişmedi) | Result: PASS
    ⛔ Aynı satırdaki biçimde eklendiğinde kapı KIRMIZI oluyor (aşağıdaki
    pozitif kontrol) → fark yalnızca biçimde.

  ⛔ **B-19 HIGH — taranan kaynak dosyası YOKSA kapı sessizce yeşil.**
    tests/tr_coverage.cpp:26-35 `slurp()` `fopen` başarısız olursa `{}` döndürüyor;
    `main` dosya varlığını **hiç doğrulamıyor**. tests/run_tr_coverage.sh:16
    `gui/hidpp_panel.inl` → `gui/hidpp_panel_YOK.inl` yapıldı:
      MUT[T06] gate_rc=0  !! YESIL KALDI
        translated call sites: 287 → **242** | dynamic 30 → 28 | Result: PASS
    ⛔ **45 anahtar** ve 2 dinamik uyarı sitesi sessizce kapsam dışı kaldı.
    (Bugün listedeki 11 dosyanın 11'i de var — `ls` ile doğrulandı — ama kapı
    bunu **kendisi** korumuyor; bir yeniden adlandırma sessizce 45 anahtarı
    kapsamdan çıkarır.)

  ── POZİTİF KONTROLLAR (kapının GERÇEKTEN ölçtüğünün kanıtı) ──
  T02 gui/ui_builder.inl'e yeni, çevrilmemiş `tr("...")` **aynı satırda** eklendi:
      MUT[T02] gate_rc=1  ✔ KIRMIZI
        MISSING TRANSLATIONS (1) → Result: FAIL (missing translations)
  T05c gui/tr.inl'de **kullanılan** bir anahtar (`"Accel:"`) yeniden adlandırıldı:
      MUT[T05c] gate_rc=1  ✔ KIRMIZI → MISSING TRANSLATIONS (1)
  T00 pozitif kontrol (mutasyon yok):
      MUT[T00] gate_rc=0 → 313 / 287 / 30 / Result: PASS
  ⚠ T05'in ilk denemesi **yeşil** kaldı — `{"Delete profile", …}` bir ORPHAN
    olduğu için bozması beklenmiyordu; bu da "sözlükte olmak ≠ kullanılmak"
    tuzağının canlı örneği (26 öksüz girdi).

===============================================================================
SORU 7 — SABİT `sleep`/`usleep` VAR MI?  ⛔ **YOK.**
  Ham çıktı (lane aralığında):
    $ sed -n '1,1217p' tests/test_accel.cpp \
        | grep -n -E 'sleep|nanosleep|usleep|wait_for|this_thread'
      (çıktı boş, rc=1)
    $ grep -c -E 'sleep|usleep|nanosleep' tests/test_accel.cpp
      0
  Tüm dosyada (10127 satır) `sleep` ailesinden **0** geçiş. `std::this_thread`
  de yok. Zamanlama-bağımlı kırılganlık bu lane'de yok.

===============================================================================
SORU 8 — HER TESTİN ASSERT'İ VAR MI? (assert sayısı = yukarıdaki tablo)
  Test başına **çalışan** assert sayısı:
    test_logitech_receiver_discovery .................... 11   (statik 10 site)
    test_logitech_hidpp_notification_classification ..... 40   (statik 37 site)
    test_logitech_hidpp_packets ......................... 101   (statik 27 site)
    test_logitech_hidraw_discovery ......................  6   (statik 6)
    test_logitech_hidpp_hardware_controls ............... 104   (statik 104)
    test_logitech_quirks_model_id_shape .................. 11   (statik 10)
    test_hidpp_short_payload_budget ...................... 94   (statik 22)
    test_hidpp_hw_sync_guard ............................. 59   (statik 21)
    test_hidpp_hw_queue_ownership .......................   6   (statik 6)
    test_hidpp_hw_queue_dedup ........................... 20   (statik 20)
    test_hidpp_hw_queue_fifo ............................ 29   (statik 13)
    ────────────────────────────────────────────────────────
    TOPLAM ............................................... 481
  ⛔ **Assert'i olmayan / assert'i çalışmayan tek birim B-01'dir (0/0).**
    Diğer 10 testin hepsinin çalışan assert'i var. Yani "assertsiz test =
    her zaman geçer" sınıfı lane'de **yok**; ama "koşullu assert = fiilen
    çalışmayan assert" sınıfı **2 yerden** geliyor: B-01 (`if (q)`) ve
    B-02 (`if (ev.battery)` — 481→480 farkı ölçüldü).
  Not: `EXPECT` sayıları çalışma anında `--filter --quiet` çıktısından
  ölçüldü; statik site sayıları `grep -c` ile. `test_accel.cpp` kendi
  çerçevesinde `SECTION()` başına ayrı sayaç tutuyor, bu yüzden "çalışan"
  sayı döngü çarpanıyla statikten büyük (örn. 16+60+16 = 92 parametre assert'i
  tek bir `for` satırından).

===============================================================================
BULGULAR (özet, ID → dosya:satır → sınıf)
  B-01  tests/test_accel.cpp:778-806                        CRIT
  B-02  tests/test_accel.cpp:334-336                        HIGH
  B-03  tests/test_accel.cpp:753-764,789-800 (+ src/logitech_hidpp.cpp:1395-1412)
                                                              CRIT
  B-04  src/logitech_hidpp.cpp:747,1072  (tests/test_accel.cpp:600)   CRIT
  B-05  tests/test_accel.cpp:861-872 (+ src/logitech_hidpp.cpp:2193,2195)
                                                              CRIT
  B-06  tests/test_accel.cpp:1058,1128,1176 (tip yerine `int`)          MED
  B-07  tests/test_accel.cpp:414 (uzun paket testi yok)              MED
  B-08  tests/test_accel.cpp:483-511 (bulma yolu yok)                HIGH
  B-09  tests/test_accel.cpp:539-551 (6/9 + 5/7 kod)                  MED
  B-10  tests/test_accel.cpp:552-556 (5/31 özellik adı)                LOW
  B-11  tests/test_accel.cpp:596-615 (kapalı transport assert kümesi)   MED
  B-12  tests/test_accel.cpp:600 ("işlem başarısız oldu" kalıbı)       CRIT
  B-13  tests/test_accel.cpp:857 (saf literal aritmetiği)             MED
  B-14  tests/test_accel.cpp:919-1214 (0 iş parçacığı; daemon.cpp:1736
        hiç bağlanmıyor; hidpp_wq_mu_ 3 lock noktası sınanmıyor)      HIGH
  B-15  tests/test_accel.cpp:1050-1084 (HidppTransport değil `int`)    MED
  B-16  tests/test_accel.cpp:500-501 (donanım taraması, 0 assert)     MED
  B-17  tests/tr_coverage.cpp:251 (sayı tabanı yok; 287→17, PASS)     HIGH
  B-18  tests/tr_coverage.cpp:240 (yalnız ' ' atlanıyor)             HIGH
  B-19  tests/tr_coverage.cpp:26-35 (dosya yoksa PASS; 287→242)       HIGH

  MUTASYON BATARYASI ÖZETİ (loglardan otomatik sayım — `tally.sh`):
      geçerli mutasyon sayısı : 35
        YAKALADI (kırmızı)     : 21
        KAÇIRDI  (yeşil kaldı) : 13
        geçersiz (derleme hatası, Q01 v1) : 1
      KAÇIRANLAR: N01 N02 N03 N04 N06 N08 N09 N10 N12* Q04 Q05 Q06 Q08 S01
      YAKALAYANLAR: M01 M02a M02b M03 M04 M05 M06 N05 N07 N11 N12 N13 N14
                     Q01v2 Q02 Q03 Q07 R01 R02 R03 R04 R05 R06
    · S01 (unified_battery hiç atanmıyor) ayrı bir log dosyasında toplandı ve
      yukarıdaki otomatik sayıma girmedi; elle eklendi → **14 kaçıran**.
    · N15 bir *mutasyon* değil, test dosyasına eklenen bir **kontrol sorgusu**;
      `EXPECT(leaked.empty())` **geçti** (yani `/dev/zero` symlink'i
      sayılmıyor). Kaçıran listesinde sayıldı ama yorumu farklı: üretim
      kodundaki kaçırılan bir mutasyon değil, tarayıcının doğru davrandığını
      gösteren bir ölçüm.
    · N14 ters yönde çalışan pozitif kontroldur: fonksiyonu her koşulda sahte
      bir cihaz döndürmeye zorladım ve kapı **KIRMIZI** oldu (:483, :495) →
      "bulunamadı" iddiasının gerçekten sınandığının kanıtı.
    tr_coverage kapısı ayrı sayıldı: 5 mutasyon, **2 yakaladı** (T02, T05c),
      **3 kaçırdı** (T03, T04, T06) + T00 pozitif kontrol yeşil.
    Genel: lane'in C++ tarafında 35 mutasyonun **%37'si** (13/35) sessizce
    kaçtı; tr kapısında 5'in **%60'ı** (3/5) kaçtı. Kaçıranların hepsi
    yukarıdaki B-01..B-19 bulgularıyla eşleşiyor.

===============================================================================
KAPSANMAYAN (lane dışı — birinin bakması gereken yerler)
  · `daemon/daemon.cpp:1736-1783 drain_hidpp_writes()` ve `hidpp_wq_mu_`
    (`:1227, :1713, :1740`) — test ikilisine bağlanmıyor; L-lane'i daemon ise.
  · `daemon/daemon.hpp:314 hidpp_transports_` `shared_ptr<HidppTransport>` tipi ve
    `find_hidpp_transport()` (daemon/daemon.cpp:1116) sözleşmesi — test edilmiyor.
  · `set_dpi` / `set_polling_rate` / `set_lift_off_distance` / `set_led_brightness` /
    `set_change_host` / `write_onboard_profile_sector` **yazma yollarının hiçbiri**
    bu lane'de test edilmiyor; `write_onboard_profile_sector`'ın O31-H1 hatası
    (B-13) ancak donanımla ya da test dikişiyle yakalanabilir.
  · `hidpp_hw_job.dev_name` alanı hiç test edilmiyor (`:621`).
  · `hidpp_parse_unified_battery` / `parse_centurion_battery` /
    `battery_info_from_voltage`'ın **genişletilmiş** girdileri: test yalnızca 1
    örnek/payload deniyor (0x0FA7 mV). Eşri tablosu sınanmıyor.
  · `hidpp_feature_index` enum'unun 37 üyesinden 9'u değer olarak, 5'i ad olarak
    sınanıyor; kalan 28 üye (kök, feature_set, device_name, reprog_controls_v4,
    superbstrike, centurion_battery_soc, brightness_control, …) test dışı.
  · `src/logitech_hidpp.cpp` içindeki `poll_hidpp_notifications`,
    `get_pairing_info`, `read_onboard_profile_sector` başarı yolları.

===============================================================================
TEMSİL SINIRI (neyi doğrulayamadım ve neden)
  1. ⛔ **Logitech donanımı yok** — hiçbir HID++ round-trip'ini çalıştıramadım.
     `send_feature_request`, `resolve_feature_index`, `set_dpi`,
     `write_onboard_profile_sector`, `probe_hidpp10` **başarı** yolları hiç
     koşmadı; B-04/B-12/B-13'ün "donanım varken de yakalanmaz mı" sorusu
     cevaplanamadı (B-12/B-13 mutasyonları zaten donanımsız olarak kaçırdı,
     yani bu testler donanım olsa da yakalamaz).
  2. ⛔ **ASan/UBSan altında koşmadım.** tests/run_tests_asan.sh bu lane'in
     bölümünü de kapsar ama yedi kapıdan biri değil; sunucu yeniden
     başlatmaları sırasında 2 dakikalık TU derlemelerini tekrarlamak
     gerekmedi. `test_accel.cpp` bu lane'de ASan'sız koştu.
  3. ⛔ **`ctest` / CMake yoluyla koşmadım** — yalnız doğrudan `g++` +
     `tests/run_tests.sh`'in birebir bayrakları (tests/run_tests.sh:22).
     CMakeLists.txt'nin bu lane'e özgü farkı olup olmadığını ölçmedim.
  4. ⛔ **`tr_coverage`'in kaçırdığını kanıtladığım 3 delik (B-17/B-18/B-19)
     yalnızca bu ağacın mevcut biçimlerinde gerçekleşti.** Yani bugün
     projede `tr\n(` biçiminde bir çağrı veya listede olmayan bir GUI dosyası
     YOK; bu delikler "gelecekte bir yeniden adlandırma/biçim değişikliği
     sessizce yeşil geçer" anlamına geliyor, "bugün çeviri eksik" anlamına
     gelmiyor. Ölçtüm: 11 dosyanın 11'i de mevcut (`ls` ile doğrulandı).
  5. ⛔ **Eşzamanlılık testi yapamadım** (B-14): çerçevede hiç iş parçacığı
     yok; yarış koşulu üretmek için lane'in *kodunu* değiştirmem gerekirdi ve
     bu yasak. Tespit "kaynakta `std::thread/async/mutex` 0 eşleşme" ile
     statiktir; bir yarış koşulunun gerçekten var olup olmadığını koşturarak
     ölçmedim.
  6. ⛔ `drain_hidpp_writes()`'ın "5.8 s bloklanma" iddiasındaki zaman aşımı
     sabitlerini çalıştırarak doğrulamadım (donanım yok).
  7. ⛔ `logitech_quirks_model_id_shape`'in kapsam dışı bıraktığı diğer iki
     12-char anahtar dışındaki tabloların gerçek model id'leriyle eşleşmesi
     donanım gerektiriyor (parked O31-H2 kararı).

===============================================================================
KANIT DOSYALARI (AJ1'in kendi komutuyla yeniden ölçebilmesi için)
  `.aihaberlesme/mesajlar/L18-kanit/` altında 59 dosya:
    · mut.sh / muttr.sh / apply_mut.py / tally.sh  → mutasyon ve sayım araçları
      (çalışma ağacına dokunmadan `cp -a` kopyaya uygular, TU başına nesne
      önbelleği kullanır; **git checkout/stash/reset yok**)
    · m*.tsv n*.tsv q*.tsv r*.tsv s01.tsv t0*.tsv  → her mutasyonun tam
      OLD→NEW→açıklama spesifikasyonu (yeniden üretilebilir)
    · mutations_hdr.log mut_n.log mut_n2.log mut_q.log mut_r.log mut_m05.log
      m00.log → mutasyon koşularının HAM çıktıları
    · section_counts.log section_counts2.log sc2.log → bölüm başına assert
      sayılarının ham çıktısı
  ⛔ Çalışma ağacı doğrulaması:
    $ git status --porcelain
      → tests/, src/, include/, gui/ altında HİÇBİR değişiklik yok.
        L18'in tek yazdığı yol: `.aihaberlesme/mesajlar/denetim-L18.md` ve
        `.aihaberlesme/mesajlar/L18-kanit/**` (brifing §1.1 istisnası).
        Not: `git status` ayrıca başka ajanlara ait `.aihaberlesme/mesajlar/
        denetim-L*.md` ve `aj*.log` dosyalarını da listeliyor — bunlar benim
        değil, ağacın mevcut durumu.
