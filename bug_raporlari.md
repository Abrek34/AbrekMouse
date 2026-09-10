# Bug Raporları — Linux RawAccel denetimi

Denetleyen: **aj2**  (ilk dalga: yalnızca raporlanır.)
Odak: mouse hareket hesaplamaları, mouse tepki süresi, sensör izlemesi.

Her madde `dosya:satır` ile işaretlenmiştir.

---

## DÜZELTİLEN BUGLAR — aj1 tarafından (2026-09-10)

Aşağıdaki bulgular **aj1** tarafından incelenmiş, gerçek bug olduğu doğrulanmış ve düzeltilmiştir.

| Madde | Bug | Düzeltme |
|-------|-----|----------|
| 1 | PID dosyası `fscanf("%d")` taşma UB | `gui/daemon_comm.inl:238-249` → `fgets` + `strtol` + `errno`/`INT_MAX` kontrolü (proc yoluyla aynı yaklaşım) |
| 2 | `getpwnam_r` ERANGE kontrolü yok | `daemon/main.cpp:112-116` → `ERANGE`'te 2× boyutlu tamponla yeniden dene |
| 7 | classic GAIN/io `cap.x < input_offset` dejenere eğri çökmesi | `include/accel-classic.hpp:114-116` cap_x'i input_offset'e sabitle + `src/config.cpp:394-395` sanitize kısıtı (`cap.x >= input_offset`) |
| 16 | `detect_polling_rate` speed sysfs okunamazsa yanlış (8× düşük) rapor | `daemon/daemon.cpp:175-191` → `usb_speed == 0` iken önce high-speed yorumunu dene, geçerli aralıktaysa onu kullan |
| 67 | LUT editor velocity modunda sağ-tık silme hit-test'i ham stored değere bakıyor (grafik gain y=x iddiasında) | `gui/ui_builder.inl` → silme hit-test'i `lut_stored_to_gain()` ile gain koordinatına çevrildi; ek aynı kök neden: sol-tık ekleme `lut_gain_to_stored(spd, gain, ax.gain)` ile stored'a çevriliyor (on_lut_spin_changed/on_lut_add_point ile tutarlı) |
| 20 | T24 sim "motion batch sonunda kaybolur" diyor; daemon sentetik SYN ile flush ediyor | `tests/test_accel.cpp` → `t24syn::sim` daemon.web`kurulumuyla uyumlu yeniden modellendi: `has_syn`/`wrote_unsynced`, `end_batch()` sentetik SYN flush'ı; bayat bölüm adı +3 yeni bölüm |
| 21 | AGENTS.md test sayacı bayat (33738/33746) | `AGENTS.md:117,210` → 33759'a güncellendi (BUG-20 sim +13 assert) |

Doğrulama (BUG-20/21 fix sonrası): 33759/33759 birim test ✓, oracle (1047 satır / 45 bilinen sapma) ✓, tr coverage PASS ✓, build 0 uyarı ✓.

---

## 1) `gui/daemon_comm.inl:241` — PID dosyası `fscanf("%d")` ayrıştırması taşma ihtimali (potansiyel UB) — ✅ DÜZELTİLDİ (aj1)

```c
(void)!fscanf(fp, "%d", &pid);
```

- Yorum, "bozuk/malformed PID dosyası → fscanf 0 döner, pid 0 kalır" diyor. Bu **sayısal olmayan** girdi için doğru; ancak PID dosyası `2147483648` gibi `int` sınırını aşan bir değer içerirse `%d` dönüşümü tanımsız davranıştır (C11 §7.21.6.2). `d_write` yazarları bunun tersini PCI-ya ters düşen yerde strtol ile çözmüş (aşağıya bakın).
- Aynı fonksiyonun /proc taraması (satır 280-282) `strtol` + `errno` + `INT_MAX` kontrolü ile savunma yapıyor — **aynı dosya içinde tutarsızlık**. Şu anki PID dosyası taşmalı sayı üretirse fscanf UB yapar.
- Etki: düşük (PID dosyasını daemon yazar), ama kodun kendi ilan ettiği "BUG-6/atoi UB" politikasıyla çelişiyor; aynı dosyada iki farklı yaklaşım kullanılmış.
- **→ DÜZELTME:** `fgets`+`strtol`+`errno`/`INT_MAX` (dosyadaki /proc yoluyla aynı). **YENİDEN ARAMAYA GEREK YOK.**

## 2) `daemon/main.cpp:104-110` — `getpwnam_r` tampon boyutu yanlış olabilir — ✅ DÜZELTİLDİ (aj1)

```cpp
std::vector<char> pw_buf(16384);
int ret = getpwnam_r(sudo_user, &pwd_buf, pw_buf.data(), pw_buf.size(), &result);
```

- `getpwnam_r` boyutu garanti etmez; 16 KB bazı sistemlerde (LDAP/NSS uzun girişler) yetersizdir ve `ERANGE` döner. Burada `ERANGE` hiç kontrol edilmiyor; sadece `ret == 0 && result` bakılıyor. Kullanıcı adı uzun/sistem NSS gecikmeli ise `sudo_user` yapılandırması sessizce yanlış yola düşer. (Düşük önem / nadir.)
- **→ DÜZELTME:** `ERANGE` dönerse tampon iki katına çıkarılıp tekrar dene. **YENİDEN ARAMAYA GEREK YOK.**

## 3) `gui/daemon_comm.inl:441` — `(int)daemon_device_field(...)` ondalık kesmesi

```c
int battery = (int)daemon_device_field(resp, S, "detected_battery");
```

- `daemon_device_field` `double` döndürür; `(int)` kesme yapar. Daemon `detected_battery`'yi int olarak `std::to_string` ile gönderiyor (daemon.cpp:1623), yani pratikte tamsayı. Ama `daemon_device_field` `strtod` ile okuduğu için varsayımsal `50.9` değeri `50` gösterilir; önem derecesi çok düşük. Doğruluk açısından `lround`/`>=0 ve <=100` kontrolü zaten var — işlevsel hata değil.

## 4) `daemon/daemon.cpp:181-182` — yüksek hızlı USB `bInterval` hesabı limiti

```cpp
if (binterval < 1 || binterval > 16) return 0;
const double interval_us = 125.0 * (1u << (binterval - 1));
```

- USB high-speed interrupt endpoint bInterval değer aralığı 1..16'dır, doğru. Ancak **SuperSpeed (≥5)`bInterval` 1..16 geçerlidir, hesap doğru**. Not: `usb_speed >= 3` SuperSpeed dahildir, hesap yine 125µs microframe kullanır; USB3 için de bInterval mikrosaniye uzunluğunu üstel verir — bu da doğru. (Onaylandı: hata yok.)

## 5) `daemon/daemon.cpp:1522` — `DevSnap::detected_battery` int, daemon `-1` bütünlüğü

- Daemon `detected_battery` yoksa -1 gönderiyor. GUI `battery >= 0 && battery <= 100` ile filtreliyor — doğru. Onaysız gri alan yok.

## 6) `include/accel-power.hpp:154-160` — `scale_from_output_point()` `diff` sınırında yanlış davranış adayı

```cpp
double diff = output - C / input;
if (diff < 0) return 1.0;
```

- Legacy+io modunda `constant=0` olduğu için (satır 58) bu `diff = cap_y` olur; `output` ti paki. `cap_y <= 0` ise başta `cap.y <= 0` kontrolü (satır 41) zaten yakalanıyor — güvenli. **(İncelendi: hata yok.)**

## 7) `include/accel-classic.hpp:122` — GAIN `cap_mode::io` + `cap.x` < input_offset durumunda sabit — ✅ DÜZELTİLDİ (aj1)

```cpp
constant = (base_fn(cap_x, accel_raised, args) - cap_y) * cap_x;
```

- **DOĞRULANDI** (accel-classic.hpp:57-61 + 110-122 + 161-168). İzleme:
  - `init_gain::io`: `constant = (base_fn(cap_x, ar, args) - cap_y) * cap_x`
  - `base_fn(x) = ar * pow(x - input_offset, exp) / x`. `cap_x <= input_offset` ise `x - input_offset <= 0`; tam `==0` için `pow(0, exp) = 0` (exp>1) → `base_fn = 0`; `< 0` için `pow(neg, kesirli_exp) = NaN` → guard → `base_fn = 0`. Her iki durumda `constant = (0 - cap_y)*cap_x = -cap_y*cap_x`.
  - `operator()`: `x < cap_x` iken gerçek eğri `base_fn(x)`, bunun üstünde `output = constant/x + cap_y = -cap_y*cap_x/x + cap_y`.
- Sonuçlar:
  1. **`cap.x < input_offset`**: tüm `x > input_offset` yolu (`x > cap_x`) bu artık-dejenere sabit kuyrukla işlenir — yani **gerçek ivme eğrisinin tamamı sessizce iptal edilir**; çıktı yalnızca `cap.y·(1 - cap_x/x)` ifadesidir. `cap_x/x` küçükken çıktı ≈ `cap_y` sabitlenir (input_offset boşuna), `cap.y<1` ise sign flip ile negatif gain bölgeleri oluşur.
  2. **`cap.x == 0` (input_offset=0)** → `constant = 0` → her `x>0` için `output = cap_y` → **eğri tamamen `cap_y` sabit gain noktasına çöker**.
- `src/config.cpp:354-407` sanitize yalnızca `cap.x ∈ [0, CAP_X_MAX]` uygular; **`cap.x >= input_offset` kısıtı yok** (input_offset≥0 zorunlu). GUI spinbox cap.x alt sınırı da yoksa bu dejenere duruma düşmek serbest → **hareket ölçeği/yönü hataları** üretebilir. CLI ile de ulaşılabilir.
- Tavsiye (DÜZELTME DEĞİL): `init_gain::cap_mode::io` içinde `cap_x > input_offset` guard + sanitize'te kısıt.
- **→ DÜZELTME (yapıldı):** `accel-classic.hpp:114-116` cap_x'i input_offset'e sabitler + `config.cpp:394-395` sanitize `cap.x >= input_offset` zorunlu kılar. Test garantisi: `tests/test_accel.cpp` `test_classic_io_degenerate_cap` (R7 — eğri input_offset üzerinde artık 1.0→cap.y asimptotik, negatif gain yok). **YENİDEN ARAMAYA GEREK YOK.**

## 8) `include/rawaccel.hpp:242` — `modifier::modify()` t ≤ 0 için hareketi sessizce ATAR

```cpp
if (!std::isfinite(time) || time <= 0) return;
```

- Daemon `flush_motion` içinde (daemon.cpp:1222) `time_ms <= 0 → DEFAULT_TIME_MIN`'e çekiyor, yani bu dal canlı yolda giriliyor mu diye rapor; ama **GUI "Mouse Test" ve başka çağrıcı** `time <= 0` verirse olay tamamen yutulur (hareket kaybı). Canlı flush yolu güvenli olduğundan şu an düşük gerçek risk. Not olarak yazıldı.
- Ayrıca ilk çağrıda `last_time_ms == 0` olduğundan `time_ms` çok büyük çıkar ve `DEFAULT_TIME_MAX=100`'e çekilir — **ilk hareket olayı 100ms penceresiyle değerlendirilir** (belirgin düşük hız). Bu, mouse uyandıktan hemen sonraki ilk hareketin ivmesinde küçük bir "cold start" hatası yaratır. (Bilinçli D6 tasarımı; etki küçük.)

## 9) `daemon/daemon.cpp:1226` — `time_ms <= 0 → DEFAULT_TIME_MIN` sıfır vuruşlu/duraksama penceresi

```cpp
if (time_ms <= 0) time_ms = DEFAULT_TIME_MIN; // clamp to minimum window instead of zero
```

- İki olay aynı nanosaniyede gelirse `now_ms` aynı olabilir → `time_ms = 0` → 0.0625 ms'ye sabitlenir, bu bir 8kHz cihazın gerçek vuruşuna yakındır — makul. `NOW_MS` aynı CLOCK_MONOTONIC_RAW olduğu için geriye gitmez. **(Hata değil.)**

## 10) `daemon/daemon.cpp:1559-1580` — seqlock okuyucu sonsuz dönme değil (8 denemede keser)

- Eşlik tek sayı → writer devam ediyor → `continue` → 8 deneme sonunda `telem_ok=false` alanları gönderilmiyor. Onaylandı: güvenli. **(Hata değil.)**

## 11) `include/accel-synchronous.hpp:145` — `std::ilogb(x)` `INT_MIN` dönebilir

```cpp
int e = std::min(std::ilogb(x), range.stop - 1);
```

- `x` son derece küçük (subnormal, örneğin 1e-300) ise `ilogb(x) = INT_MIN` (C'de FP_ILOGB0/INT_MIN). Ancak `e >= range.start` kontrolü negatif olduğundan `e` için sorun yok; glibc'de subnormal `ilogb` gerçek üssü döndürür (negatif büyük sayı) → `e < range.start → data[0]` döner → sabit gain. Üstel sınırlar kapalı. **(Hata değil; korumalı.)**

## 12) `gui/graph.inl` (LUT editörü) — incelendi, sınır hatası bulunmadı; 2 not ↗

- `lut_set_points` (318-345) yazım sınırları doğru: `LUT_RAW_DATA_CAPACITY=514`, `LUT_POINTS_CAPACITY=257`; `ax.length = pts.size()*2 ≤ 514` ve döngü `i<pts.size()` → `data[i*2+1] ≤ 513`. `safe_f` float taşması/NaN guard'ı. **(Sınır hatası yok.)**
- `lut_list_changed` sipariş-değişimi tespiti epsilon (1e-3) ile — BUG-3 düzeltmesi doğru. **(Hata değil.)**
- **Not-1:** `on_lut_spin_changed` (372-380) VE `lut_list_changed` (450-462) widget ağacında **sabit konum** varsayar (spin→hbox→row→list; hbox çocuk sırası: lbl_s, spin_s, lbl_g, spin_g, del_btn). Düzen değişirse sessizce bozulur; bakım riski.
- **Not-2:** `rebuild_lut_list` "app-state" verisini `lut_list_box`'a bağlı varsayar (`on_lut_row_delete:357`); bu qdata'nın gerçekten orada olduğu ui_builder ile doğrulanmalı (aşağıda 18 numara).

## 13) `gui/daemon_comm.inl:202-206` — `daemon_ipc_push_config` yalnızca `"ok":true` arar

```cpp
return daemon_ipc_send_raw(req, 5000)
    .find("\"ok\":true") != std::string::npos;
```

- Daemon `{"ok":true,...}` döndürüyor; boşluk/küçük-büyük harf farkı yok. `"ok":false` durumunu doğru şekilde "başarısız" sayar. **(Hata değil.)**

## 14) Sensör izleme gözlemi — `detected_dpi` ve `detected_polling_rate` yalnızca DONANIM bilgisi

- `detect_dpi_sysfs` (daemon.cpp:196-212) `resolution` sysfs düğümünü okur; çoğu oyuncu faresi bunu göstermez → `detected_dpi = 0` ("unknown"). `detect_polling_rate` (147-192) bInterval ile tümevarımlı tahmin yapar; SuperSpeed/500Hz üstü hatalı olabilir. GUI "sensör izleme" satırında bunlar işlenir; pratikte değer kullanıcının gerçek DPI/polling değerinden sapabilir çünkü yalnızca tahmindir. Raporda **bilinen sınırlama** olarak not edilir.

## 15) `daemon/daemon.cpp` ana döngü / `run_loop` — incelendi, hata yok

- `run_loop` (1008-1127): `reload_flag_`/`push_cfg_pending_` yalnızca loop thread'de işlenir; epoll 10ms timeout; hot-plug retry 8×10ms; boş aygıt 2s rescan. `devices_` içi tüm mutasyon loop thread'de → `status_json` (IPC thread, mutex'lü) ile data race yok. `epoll_wait` hata dalı loop'u bitirir (EBADF vb. nadir). `process_device(devices_[it->second])` index kontrolü loop thread içinde, tutarlı. **(Hata yok.)**
- `daemon/main.cpp`: PID O_EXCL yazımı + stale-PID temizliği (`kill(pid,0)`+ESRCH) + "başka canlı var mı" kontrolü doğru (iki daemon aynı cihazı tutamaz). `reload()`/`request_stop()` sadece atomik store — sinyal handler güvenli. `getpwnam_r` ERANGE madde 2'de. ✓ tamamlandı.

## 16) `daemon/daemon.cpp:183-186` — `detect_polling_rate`: yüksek hızlı farede `speed` sysfs okunamazsa yanlış rapor — ✅ DÜZELTİLDİ (aj1)

```cpp
} else {
    // Full-speed (or unknown/low): bInterval is in 1ms units
    rate_hz = 1000 / binterval;
}
```

- `usb_speed` düğümü 0 (okunamadı/eksik/"Unknown") veya `sysfs_read_int` hatası döndürdüğünde kod **full-speed mantığına düşer**. Ama cihaz gerçekte high-speed ise (1000Hz farede bInterval=4), `1000/4 = 250` rapor edilir. Gerçekte 8 kat düşük polling rate gösterilir → "sensör izleme" ekranında yanlış bilgi; `detected_polling_rate` yalnızca tahmini olduğu için (madde 14) kullanıcıyı yanıltabilir.
- Koşul: sysfs `speed` normalde USB HID'de mevcuttur; ancak `speed` = `0` veya `2` (gerçek full-speed hub kasası) + yüksek-bInterval fare nadiren olur. Düşük-orta önem.
- **→ DÜZELTME:** `usb_speed == 0` (okunamadı) iken önce high-speed yorumunu onayla; sonuç `[POLL_RATE_MIN, POLL_RATE_MAX]` aralığındaysa kullan, değilse full-speed fallback. **YENİDEN ARAMAYA GEREK YOK.**

## 17) Sensör izleme / Mouse Test — raw_passthrough modunda canlı telemetri HİÇ doldurulmuyor

- `daemon.cpp:1178-1212` raw REL yolu `flush_motion`'a gelmez; REL_X/REL_Y inline tek tek ilerlenir (P121/BUG-05 belgeli: `has_motion` hiç set olmaz) → `telemetry` hiç güncellenmez → `status_json` `telem_ok=false` (alanlar yayımlanmaz) → `gui/mouse_test.inl` canlı hız/gain/DPI boş görünür; "Performans/latency" de BUG-21 gereği hiç ölçülmez.
- Kod içi **belgelenmiş** bu davranış; ama **oyuncu kurulumunun varsayılanı raw mod** olduğundan, en yaygın senaryoda "sensör izleme / tepki süresi" ekranları boş görünür. Bu bir hata değil, **UX/izlenebilirlik boşluğu**: raw modda da benzer bir sayıcı (ör. sadece count/hz) sunulabilir.
- Ek: `dump_latency_stats` raw modda açıkça "no per-event measurement" yazar — doğru niyet, yeterli değil.

## 18) `gui/graph.inl:355,372` — LUT satır silme/spin callbacks widget-ağacı bağımlılığı

- `on_lut_row_delete` AppState'i `g_object_get_data(G_OBJECT(list_box), "app-state")` ile alır; `on_lut_spin_changed` list_box'a 3 üstten ulaşır. **Doğrulandı (18B):** `ui_builder.inl:358` `g_object_set_data(G_OBJECT(S->lut_list_box), "app-state", S)` — qdata doğru, kayıp yok. Kalan tek risk `on_lut_spin_changed:374-376`'nın sabit ağaç derinliği varsayımı (hbox sarmalayıcı eklenirse sessizce kırılır) — bakım riski, aktif hata değil.

## 19) `tests/test_accel.cpp:4109-4112` — "BUG-7 fix" yorumu vs kod: clamp yoktu → ✅ DÜZELTİLDİ (aj1, madde 7 ile birlikte)

> **Zaman çizelgesi notu:** Bu bulgu, aj1'in düzeltmesi koda düşmeden ÖNCE (bu oturumun ilk taramasında) doğrulandı:
> `include/accel-classic.hpp:110-123` `cap_x`'i `input_offset`'e clamp ETMİYORDU; test assert'leri yalnızca `finite`+monoton+tanımlı tavan kontrol ettiğinden `cap_y·(1 − cap_x/x)` tesadüfen geçiyordu ve oracle bu bölgeyi (referans NaN üretir, `NaN > tol` asla doğru olmaz) test edemiyordu → **"regression koruması" fiilen yoktu.**

- **Mevcut durum (sonraki kontrol):** aj1 madde 7 kapsamında koda clamp ekledi:
  - `accel-classic.hpp:119` → `if (cap_x < args.input_offset) cap_x = args.input_offset;`
  - `config.cpp:398` → sanitize `if (a.cap.x < a.input_offset) a.cap.x = a.input_offset;`
  - Test yorumu ile gerçek kod artık UYUMLU; `test_classic_io_degenerate_cap` (R7) geçerli korumayı doğruluyor. **YENİDEN ARAMAYA GEREK YOK.**
- Kalıntı semantik not (hata değil): clamp sonrası `x >= input_offset` için eğri hâlâ `cap_y·(1 − input_offset/x)` — yani `base_fn(input_offset)=pow(0,exp)=0` olduğundan eğri kullanıcının tam `base_fn(x)` biçimi DEĞİL, tek parametreli asimptotik kuyruk. Bu io mode formülünün doğası (referansla aynı); güvenli ve monoton, negatif gain imkânsız — kabul edilebilir.

## 20) `tests/test_accel.cpp:5255-5260` — T24 sim "motion batch sonunda kaybolur" diyor; daemon sentetik SYN ile FLUSH ediyor (test modeli bayat) — ✅ DÜZELTİLDİ (aj1)

```cpp
void end_batch() {
    // Motion that never reached a SYN_REPORT is lost (daemon locals
    // dx/dy live inside one process_device() invocation).
    dx = dy = 0;  has_motion = false;
}
```

- Test bölümü (5362-5371): `[REL_X=8]` → `end_batch()` → `out` yalnızca sonraki SYN'i içerir, **motion kaybolur** der.
- Gerçek daemon (`daemon.cpp:1376-1380`): `if (!has_syn) { flush_pending_motion(); if (wrote_unsynced_event) uinput_write(SYN_REPORT); }` — yani **batch sonunda beklemede kalan hareket SENTETİK SYN ile KAYBEDİLMEZ**, ileriye yazılır.
- `t24syn::sim` bu kuyruk davranışını modellemiyor → **daemon'un sentetik-SYN kuyruğu birim testle hiç kapsanmıyor**; üstelik yorum ve test adı ("motion without SYN_REPORT is lost") sevk edilen gerçek davranışla (flush) çelişiyor. Test bayat (eski bir daemon sürümünü modellemiş) veya senaryo yanlış adlandırılmış; hangisi olursa olsun **daemon davranışı belgelenenden FARKLI** — regresyon riski: gelecekte biri kuyruğu kaldırırsa test yine "geçer" çünkü zaten kaybı bekliyor.
- Tespit: `daemon.cpp:1376-1380` satırları için gerçek-uyumlu test yok (yalnızca bazı "flushed on SYN" senaryoları var).
- **→ DÜZELTME (aj1):** `t24syn::sim` yeniden model aldı (daemon.cpp:1292-1396 ile birebir):
  - `has_syn` + `wrote_unsynced` sayacı eklendi; SYN yolu `wrote_unsynced=false`, diğer olay yolu önce `flush()` sonra forward + `wrote_unsynced=true`.
  - `end_batch()` artık bayat "motion lost" yerine **daemon tail'ini** yapıyor: `has_syn` yoksa `flush()` + `wrote_unsynced` ise sentetik SYN_REPORT (daemon.cpp:1391-1396).
  - Bölüm adı "motion without SYN_REPORT is lost" → "flushed at batch end (BUG-20: synthetic SYN)"; +3 yeni bölüm: button-only sentetik SYN, SYN'li temiz batch'te çift-flush olmaması, motion→button sırasının korunması. **YENİDEN ARAMAYA GEREK YOK.**

## 21) `AGENTS.md` — test sayacı sürüklenmesi (belge sapması, işlevsel değil) — ✅ DÜZELTİLDİ (aj1)

- AGENTS.md "33738 runtime assertion / 183 grup" der; `tests/run_tests.sh` çıktısı **33746/33746** geçti (183 grup doğrulandı). Logitech HID++ piller testleri (P131 dönemi) eklenmiş; AGENTS.md sayacı güncellenmemiş.
- **→ DÜZELTME (aj1):** BUG-20 sim yeniden modeli +13 assert ekledi; AGENTS.md sayacı **33759/33759** olarak güncellendi (`AGENTS.md:117` ve `:210`). **YENİDEN ARAMAYA GEREK YOK.**
- Kapsam: sadece belge.

## 22) `src/logitech_hidpp.cpp:626-632` — bekleme penceresindeki HID++ notification'ları sessizce düşürülür (LOW, tasarım notu)

```cpp
if (buf[1] != request_device || buf[2] != feature_index ||
    (buf[3] >> 4) != wire_function ||
    (buf[3] & 0x0F) != request_sw_id)
    continue;   // ← notification / eşleşmeyen paket buffer'a alınmadan yok edilir
```

- `send_feature_request` eşleşmeyen tüm paketleri yutar; notification'lar tamponlanmaz. `identify_logitech_device()` satır içi onlarca ardışık istek (her biri 500–900 ms timeout) yaptığı için bu pencerede gelen batarya/bağlantı notification'ları kaybolur. Aynı request_mutex_ drain'i de bloklar (drain `identify` bitmeden koşamaz).
- Pratik etki düşük: daemon worker'ı yine 1 s kadansında drain ediyor ve tanımlama kısa sürüyor. Ama istek-yanıt bekleyen bir cihaz takılıysa (her istek timeout'a kadar bekler) birikme olabilir.
- **Karar:** raporlanır — düzeltme gerektirmez; gelecekte notification'ları ayrı bir kuyrukta biriktirmek istenirse not düşülmüş olur.

## 23) `src/logitech_hidpp.cpp:1575-1593` — `get_polling_rate` önce legacy REPORT_RATE'i (0x8060) deniyor (LOW)

```cpp
if (auto index = resolve_feature_index(hidpp_feature_index::report_rate, ...)) {
    ... return rate_code_to_hz(false, (*reply)[0]);   // önce 0x8060 fn 0x1
}
if (auto index = ...extended_adjustable_report_rate...) { ... } // yedek
```

- Hem 0x8060 hem 0x8061'i ilan eden bir cihazda legacy okuma (period kodu; maks. 1000 Hz) önce gelir ve kazanır; cihaz 2000–8000 Hz'e extended feature üzerinden ayarlanmışsa legacy okuma yalnızca kod 1 (1000 Hz) döndürebilir → yanlış düşük rapor. Gerçekte >1000 Hz destekleyen cihazlar genelde 0x8060 ilan etmez; bu yüzden varsayımsal. Yine de sıra extended lehine çevrilebilir (Solaar extended'ı önce yoklar).
- **Karar:** raporlanır — düzeltme opsiyonel.

## 24) `daemon/daemon.cpp` — HID++ batarya **aktif sorgulanmıyor**, yalnızca notification + sysfs (MEDIUM, sensör izleme boşluğu)

- Daemon'un HID++ worker'ı (`poll_hidpp_notifications`, daemon.cpp:942-1019) yalnızca **notification drain** ediyor; test edilmiş ve tam donanımlı `HidppTransport::get_battery_status()` (0x1000/0x1004/0x1001 + legacy 0x0D/0x07) hiçbir yerde periyodik olarak çağrılmıyor (grep doğrulandı: daemon'da yalnızca sysfs `detect_battery_level` + batarya notification log'u var).
- Sonuç: pow_supply sysfs ağacı olmayan **ve** notification puslamayan Logitech kablosuz farede `detected_battery` kalıcı olarak `-1` ("unknown") kalır; oysa cihaz istek üzerine seviyeyi verebilir. CLI/GUI'deki bu cihazlar için batarya göstergesi boş görünür.
- **Karar:** gerçek bir kapsam boşluğu (crash değil). Öneri: worker drain döngüsünde her N saniyede bir `get_battery_status()` aktif sorgusu (not: Solaar da bataryayı notification + istekle besler).

## 25) `daemon/daemon.cpp:1256-1263` — telem `in_ips` "modifier ile aynı normalizasyon" değil (yeni bulgu; LOW)

```cpp
double ips_factor = dev.dpi_factor / time_ms;
double in_ips     = magnitude({ dx, dy }) * ips_factor;        // ÖN-dönüş, ham delta
double out_ips    = magnitude({ out_x, out_y }) * ips_factor;
```

- AGENTS.md claim'i: "`telem_in_ips` = |(dx,dy)| · dpi_factor / dt **using the same normalization as modifier::modify()**". Bu iddia yalnızca **euclidean + snap=0 + domain_weights=1 + smoothing=0** durumunda doğru. `modifier::modify()` (include/rawaccel.hpp:291-329) hızı şöyle türetiyor:
  - `distance_mode::max` → `max(|x|,|y|)`; `Lp (norm≠2)` → `lp_distance`; `separate` → eksen başına `|x·f|` — hepsi `magnitude` (hypot) **değil**.
  - `abs_vel = |in · ips_factor · domain_weights|` (domain ağırlığı çarpılır), döndürülmüş/snappelenmiş `in` üzerinden.
  - input smoothing açıksa hız EMU ile yumuşatılır.
- Telemetri ise **ham (döndürme öncesi) dx,dy** + `magnitude` + domain ağırlıksız + yumuşatmasız. Sonuç: max/lp(≠2)/separate modlarında, snap etkinken, weights ≠1 iken ve smoothing varken Mouse Test panelindeki In(ips) değeri, eğrinin gerçekte beslediği hızla örtüşmüyor (max'ta |dx|≠|dy| olan hareketlerde birebir yanlış; separate'te anlamsızlaşıyor). `gain = out_ips/in_ips` de bu yüzden eğri içi scale yerine "uçtan uca ekran çıktısı" oranı. **Hareket matematiği etkilenmez** — yalnızca telemetri görüntüsü + AGENTS iddiası.
- **Karar:** raporlanır — düzeltme opsiyonel (ya telemetriyi modifier'la aynı hesaba çekmek ya da AGENTS iddiasını "euclidean, weights=1, snap=0 için" diye sınırlamak).

## 26) `daemon/daemon.cpp:289-310` + `gui/devices.inl:91-107` — cihaz keşfi yalnızca REL_X+REL_Y bakar, isim/tipe göre dışlama yok (yeni bulgu; LOW, UX/politika)

```cpp
bool has_motion = libevdev_has_event_code(dev, EV_REL, REL_X) &&
                  libevdev_has_event_code(dev, EV_REL, REL_Y);
...
return has_motion && !is_virtual;   // virtual dışında HİÇBİR filtre yok
```

- TrackPoint / accelpoint, touchdown REL eksenli touchpad yok ama TrackPoint + bazı marka/tablet kalemleri REL_X/REL_Y ilan ediyor → varsayılan davranışta **"All devices" profili hepsine uygulanıyor** (dizüstü kullanıcısı için istenmeyen olabilir: TrackPoint'e de RawAccel accel uygulanır). GUI keşfi de aynı mantıkta (`REL mask & 0x3`). `is_rawaccel`/uinput fiziksel dışlama var ama "Touchpad/TrackPoint/Tablet/ThinkPad" gibi isim-tipi filtreler yok.
- Windows RawAccel cihaz listesi seçici (kullanıcı elle seçer); Linux tarafı otomatik-hepsini-alıyor.
- **Karar:** raporlanır — gerçek bir bug değil, politika/UX boşluğu; isim tabanlı dışlama (veya GUI'de cihaz bazlı etkinleştirme) önerisi not edilir.

---

### Test koşusu (2026-09-10) — tümü TEMİZ

| Süit | Sonuç |
|------|-------|
| `tests/run_tests.sh` | 33759/33759 PASS, CLI kapıları (P83/P99/P107) ✓ |
| `tests/run_tests_asan.sh` (ASan+UBSan) | 33759/33759 PASS, sanitizer hatası yok |
| `tests/run_tr_coverage.sh` | `Result: PASS` (eksik TR anahtar yok) |
| `tests/oracle/run_oracle.sh` | OK — 1047 satır / 45 bilinen sapma, sapma yok |
| `bash scripts/build.sh` (warning gate) | 0 warning, 0 error |
| `tests/run_fuzz.sh 30` | her iki harness (config+accel) crash yok |

**Yorum:** Tüm süitler yeşil ve aj1'in 1/2/7/16/67/20/21 düzeltmeleri koddan geçerli (son kontrol 2026-09-10'da doğrulandı). Açık kör nokta artık yok: madde 20 kapsamında `t24syn::sim` daemon'un sentetik-SYN flush kuyruğuyla (daemon.cpp:1391-1396) birebir uyumlu yeniden modellendi — batch sonu hareket KAYBEDİLMEZ, sentetik SYN ile flush edilir (+3 yeni bölüm).

---

### İlerleme notları
- [x] GUI `graph.inl` (LUT editör) — madde 12/18
- [x] daemon `run_loop` / epoll döngüsü + `daemon/main.cpp` PID/sinyal — madde 15
- [x] Test süitleri tam tur (unit / ASan / tr / oracle / build / fuzz) — maddeler 19-21, tablo
- [x] `src/logitech_hidpp.cpp` tam okuma (1865 satır; sensör/DPI/batarya/paket katmanı + `include/logitech_hidpp.hpp`) — maddeler 22-24; paket sınırları (from_bytes uzunluk kontrolleri), DPI step/last decode guard'ı (step != 0 + next<=last), batarya 0x0D+0x07 çifte fallback'i, replug sonrası `clear_feature_cache()`, ayrı hidpp worker thread (P171-BFIX) doğrulandı — bu yönlerisorunsuz.
- [x] `cli/main.cpp` set-param/validate/dispatch (420-2195) — **hata bulunamadı**: `cmd_set_param` tam anahtar doğrulaması + P107 domain kontrolü + BUG-10/11 (stod trailing-garbage & NaN/Inf) + P115-A5-01 (device_id 256 cap) + P156 (output_dpi fractional) + distance_mode sentinel (P115-A5-06) + sanitize sonrası gerçek stored değer basımı; `cmd_validate` missing-config hatasını rc=1 veriyor ve dosya oluşturmuyor (P120-FAZ2). CLI kapıları (P83/P99/P107) test turunda yeşildi.
- [x] `gui/widgets_sync.inl` (685 satır) — **hata bulunamadı**: R13 tek-kaynak `update_raw_sensitivity` (18 widget), P157 xy_linked yeniden-türetme koruması, notify::prop 3-arıkanlı imza güvenlik notu, lp_norm 9999 sentinel CLI ile tutarlı, LUT kapasite taşması uyarısı, on_perf Bug-02 aktif-profil cihaz dilimi.
- [x] `gui/profile_mgr.inl` (CRUD + preset önizleme) + `setup.sh` bağımlılık dalları (pacman/apt/dnf hepsi eksiksiz: g++/make/cmake/pkgconf/libevdev/gtk4/polkit/systemd/python3/qt6) — **hata bulunamadı**.
- [ ] `include/accel-lookup.hpp` derleme detayı (LUT sıralama, interpolasyon) — *önceki turda okundu, hata yok*.
- [x] daemon tam tur (tamamı 1907 satır): keşif 289-360, setup/hotplug 602-920, HID++ worker 922-1020, `run_loop` 1024-1143, `process_device`/`flush_motion` 1140-1397, lat dump 1399-1464, IPC server 1466-1907, `status_json` 1519-1664 — **bulgu 25/26**; IPC ağır korumalı (SO_RCVTIMEO/SNDTIMEO 2s + request deadline 10s + body 5s + 1MB cap + no-op guard + stale-socket probe ECONNREFUSED/ENOENT), uses `devices_mutex_` altında config_ yazımı, telem seqlock 8-denetmeli.
- [x] `gui/mouse_test.inl` (512 satır, P104) — **hata bulunamadı**: Tier-1 X11 grab / Tier-2 confine / Tier-3 Wayland degrade mantığı, BUG-09 dil yenileme, BUG-10 socket gate, Bug-02 field seçimi (aktif profil cihazı), ESC/focus/destroy teardown sırası güvenli.
- [x] `gui/devices.inl` REL mask ayrıştırma (LS word, bit 0+1) + `by-id` öncelik düzeni — **hata bulunamadı** (madde 26 kapsamı Hariç).
- [x] `include/presets.hpp` (8 preset, tek kaynak) + `gui/app_state.hpp` — **hata bulunamadı**; preset değerleri cap.x>=input_offset/limit kurallarına uygun, app_state salt veri.

**Durum:** İlk dalga + test turu + **ikinci detaylı analiz turu (hidpp/CLI/GUI/setup)** tamamlandı. Madde 1/2/7/16/19 **ve BUG-67 ile 20/21** aj1 tarafından düzeltildi ve koddan doğrulandı (20 artık AÇIK değil: `t24syn::sim` daemon'un sentetik-SYN flush kuyruğuyla birebir yeniden modellendi). Açık bulgular 22-26: 22-24 HID++/sensör turundan (LOW×2 + batarya aktif sorgu boşluğu — MEDIUM, madde 24), **Üçüncü sıkı tur** (daemon tam 1907 satır + IPC + mouse_test.inl + devices.inl + presets + app_state): 25 (telem in_ips "modifier ile aynı normalizasyon" değil, LOW) + 26 (keşif yalnızca REL_X/Y bakar, TrackPoint gibi cihazlar dışlanmıyor, LOW/UX). Geri kalan her şeyde → **yeni bug bulunamadı** — proje zaten ağır şekilde korunmuş ve tüm test kapıları yeşil (madde 20'ye özel: sentetik-SYN kuyruğu artık testte modelleniyor).