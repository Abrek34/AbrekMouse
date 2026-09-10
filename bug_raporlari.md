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

Doğrulama (BUG-67 fix sonrası): 33746/33746 birim test ✓, oracle (1047 satır / 45 bilinen sapma) ✓, tr coverage PASS ✓, build 0 uyarı ✓.

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

## 20) `tests/test_accel.cpp:5255-5260` — T24 sim "motion batch sonunda kaybolur" diyor; daemon sentetik SYN ile FLUSH ediyor (test modeli bayat)

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

## 21) `AGENTS.md` — test sayacı sürüklenmesi (belge sapması, işlevsel değil)

- AGENTS.md "33738 runtime assertion / 183 grup" der; `tests/run_tests.sh` çıktısı **33746/33746** geçti (183 grup doğrulandı). Logitech HID++ piller testleri (P131 dönemi) eklenmiş; AGENTS.md sayacı güncellenmemiş.
- Kapsam: sadece belge.

---

### Test koşusu (2026-09-10) — tümü TEMİZ

| Süit | Sonuç |
|------|-------|
| `tests/run_tests.sh` | 33746/33746 PASS, CLI kapıları (P83/P99/P107) ✓ |
| `tests/run_tests_asan.sh` (ASan+UBSan) | 33746/33746 PASS, sanitizer hatası yok |
| `tests/run_tr_coverage.sh` | `Result: PASS` (eksik TR anahtar yok) |
| `tests/oracle/run_oracle.sh` | OK — 1047 satır / 45 bilinen sapma, sapma yok |
| `bash scripts/build.sh` (warning gate) | 0 warning, 0 error |
| `tests/run_fuzz.sh 30` | her iki harness (config+accel) crash yok |

**Yorum:** Tüm süitler yeşil ve aj1'in 1/2/7/16 düzeltmeleri koddan geçerli (son kontrol 2026-09-10'da doğrulandı). Açık kalan kör nokta: **madde 20** — daemon'un sentetik-SYN kuyruğu (daemon.cpp:1390-1391) gerçek-uyumlu birim testle kapsanmıyor ve T24 sim testi/dokümantasyonu sevk edilen davranışla çelişiyor.

---

### İlerleme notları
- [x] GUI `graph.inl` (LUT editör) — madde 12/18
- [x] daemon `run_loop` / epoll döngüsü + `daemon/main.cpp` PID/sinyal — madde 15
- [x] Test süitleri tam tur (unit / ASan / tr / oracle / build / fuzz) — maddeler 19-21, tablo
- [ ] `cli/main.cpp` set-param/PRESET yolları (satır 420-2195)
- [ ] `include/accel-lookup.hpp` derleme detayı (LUT sıralama, interpolasyon)
- [ ] `src/logitech_hidpp.cpp` (sensör/hidraw iletişimi; 1865 satır)
- [ ] `gui/widgets_sync.inl` kalan bölümler + `profile_mgr.inl` (madde 18 qdata doğrulandı ✓)
- [ ] `setup.sh` / `scripts/` kurulum bağımlılıkları

**Durum:** İlk dalga + test turu tamamlandı (denetim devam ediyor). Madde 1/2/7/16 **ve BUG-67 (LUT velocity hit-test)** aj1 tarafından düzeltildi ve koddan doğrulandı (yukarıdaki tablo). Yeni bulgular 19-21 eklendi; 19 (clamp) aj1'in düzeltmesiyle çözüldü, **20 açık** (T24 sim ↔ daemon sentetik-SYN çelişkisi). BUG-01 "hata değil" (referansla birebir + oracle temiz).