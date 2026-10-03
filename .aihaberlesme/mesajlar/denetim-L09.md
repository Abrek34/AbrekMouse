### L09 | subagent (daemon sıcak yol) | 2026-10-01

**KAPSAM** : `daemon/daemon.cpp` **1988–2893** (run_loop, uinput_write_retry,
write_batch, flush_motion, process_device) + `daemon/motion_math.hpp` (74) +
`include/rawaccel.hpp` smoother/modify (okuma) + `daemon/daemon.hpp:20–120` (okuma).
1–1987 ve 2894+ **okundu, değiştirilmedi**.

---

## BULGULAR

### L09-01 | `daemon/daemon.cpp:2213` | **HIGH** — Doygunluk hâlinde event yolunda **109 ms** blok

`flush_motion` içinde `sleep` **yok**; ama yazma yolu `flush_motion → out.add_rel
→ add_flush_if_full → flush → uinput_write_retry` zincirinden geçiyor ve
`uinput_write_retry` **EAGAIN'de `nanosleep` uyuyor** (`:2213`).

Kanıt — gerçek O_NONBLOCK fd, alıcı tamponu dolu (EAGAIN teyit edildi),
`:2198-2222` retry çekirdeği birebir çalıştırıldı:

```
$ ./retry_sat
nominal delay schedule sum over 32 attempts = 106350 us = 106.35 ms
saturation precondition: write() after 8128 bytes buffered -> got=-1 errno=11 [EAGAIN CONFIRMED]

SINGLE 24-byte frame, consumer saturated:
  attempts=32 nanosleeps=32 written=0 elapsed=109.835 ms  return_ok=1
TYPICAL 72-byte accel frame (REL_X+REL_Y+SYN), consumer saturated:
  attempts=32 nanosleeps=32 written=0 elapsed=109.010 ms
  >>> a 125 us frame budget blown by 872x
1000 Hz budget context:
  one saturated frame costs 109.0 ms
  => the loop emits 9.17 frames/s instead of 1000 while saturated
after consumer resumes: attempts=1 written=24 elapsed=0.0027 ms
```

Kod yorumu (`:2185-2192`) bunu **bilinçli** kabul ediyor: *"a fully-stuck consumer
costs at most ~120 ms of motor stutter"*. Ölçülen 109.8 ms yorumun sözünü tutuyor
(~120 ms). **Buldum diye sunmuyorum — teyit ediyorum.**

Ama asıl yük kaldırma yüzünden: bu **tek bir fare için** değil, `run_loop`'un
**tek döngü iş parçacığı** içindir (`:2119 process_device` tek thread). Bu
iş parçacığı `epoll_wait`'te diğer tüm cihazları da dinler. Yani **bir farenin
takılması ikinci fareyi de 109 ms susturur**, ve `run_loop` bu sırada
`epoll_wait(…, 10)` zaman aşımını da kaçırır → hot-plug/IPC/reload hepsi
gecikir. Doygunluk **bitene kadar değil, kare başına 109 ms** olarak tekrarlanır
(32 deneme her karede yeniden başlar).

---

### L09-02 | `daemon/daemon.cpp:2459` + `:2497` | **HIGH** — Cihaz yavaşladığında `real_polling_rate` **SESSİZCE ESKİ KALIYOR**

Bu, gözdeki "CS2'de fare 1000Hz sanırken 125Hz'e düştü" senaryosunun **ölçülmüş
hâli** — ve sonuç istenenden daha kötü.

İki bağlama var, ikisi de "makul aralık" diye adlandırılmış:

| satır | kapı | yorum |
|---|---|---|
| `:2459` | `interval_us >= 100 && interval_us <= 10000` | 100 Hz–10 kHz = **0.1 ms–10 ms** |
| `:2497` | `rate_hz >= 100 && rate_hz <= 10000` | aynı sınır |

`POLL_RATE_MIN = 125` (`include/rawaccel-base.hpp:13`) — yani **projeye göre
geçerli en yavaş cihaz 125 Hz**. Ama `:2459` 100 Hz'nin **altını** reddediyor:
USB güç tasarrufu / sürücü düşüşüyle cihaz **100 Hz altına** indiğinde aralık
redd edilir → halkaya hiç girmez → `real_polling_rate` **hiç güncellenmez** ve
sönümlenen değer **sonsuza dek** kalır. Tek satır log yok, sayaç yok.

Kanıt — `:2456-2504` halka/medyan kodu ve `:2423-2430` SM-4 tüketicisi birebir
çalıştırıldı (`polling.cpp`):

```
gate daemon.cpp:2459  interval_us in [100,10000]
gate daemon.cpp:2497  derived rate_hz in [100,10000]

--- steady 1000 Hz ---                       stored real_polling_rate=1000 Hz  changed 1 time(s)
--- SLOWDOWN 1000 -> 125 Hz ---              stored real_polling_rate=125  Hz  changed 2 time(s)
--- SLOWDOWN 1000 -> 50 Hz (20000us) ---     stored real_polling_rate=1000 Hz  changed 1 time(s)   <-- 50 HZ AMA 1000 YAZIYOR
--- SLOWDOWN 1000 -> 10 Hz (100000us) ---    stored real_polling_rate=1000 Hz  changed 1 time(s)   <-- 10 HZ AMA 1000 YAZIYOR
--- STALL: 600 frames then ZERO frames 10 s -  stored real_polling_rate = 1000 Hz  (never updated, never invalidated)

  1000 Hz -> 1000 (correct)
   125 Hz ->  125 (correct, in range)
    50 Hz -> STALE (gate :2459 rejects interval 20000us)
    10 Hz -> STALE (gate :2459 rejects interval 100000us)
     0 Hz -> STALE (no frames at all, nothing feeds the ring)
```

**Doygunluk (0 Hz) en kötüsü**: kare gelmediği için *hiçbir kod çalışmıyor* —
ne halka beslenir ne `real_polling_rate` geçersiz kılınır. `status_json`
(`:3095`, `:3175-3176`) `1000`'i basmaya devam eder.

Bu değeri kim okuyor:
- `cli/main.cpp:1988` → `monitor` tablosu `real_polling_rate` sütunu.
- `gui/mouse_test.inl:301,305-307` → "Mouse Lock Test" canlı okuması.

`[stale]` işareti **var** ama **telemetriyi** (`telem_wall_ms`) izliyor, hızı
değil: `cli/main.cpp:1984` → `(now_ms - t_wall) > 2000.0`; GUI `:274-275` aynı.
`telem_wall_ms` yalnızca `flush_motion`'un **accel** yolunda dolduğu için
(`:2535`), **raw-passthrough'ta hiç dolmaz** ve — asıl önemlisi — **yavaşlayan
bir cihaz hâlâ `flush_motion` çağırır**, yani `telem_wall_ms` **taze kalır**
ve `[stale]` **yanmaz**. Yani 50 Hz'ye düşmüş fare "1000 Hz, (live)" olarak
görünür. **Sessiz yeşil.**

Eksik olan şey belirli bir eşik değil, **tescil**: `daemon/ src/ include/ cli/ gui/`
içinde ölçümlü arama (`slow|degrad|under.?rate|rate_drop|stall_count`) →
**hız düşüşüne dair hiçbir sayaç/uyarı yok**; eşleşmelerin hepsi `stale socket`
(`daemon.cpp:3249`, `main.cpp:475,520`) veya **telemetri** staleness'i
(`cli/main.cpp:1984`).

---

### L09-03 | `daemon/daemon.cpp:2244` + `:2253-2263` | **HIGH** — Bırakılan kare "başarılı" sayılıyor, ve **raw yolunda %100 sessiz**

`:2244` `return true;` — bütün bütçe tükendi, kuyruk boşaldı, kare **düştü**,
çağıran yine **başarı** alıyor. `flush()` (`:2311-2319`) `n=0` sıfırlayıp
`true` döndürüyor, `process_device` `dev.disconnected = false` bırakıyor.

Bu *bilinçli* (`:2224-2227` "drop the tail, never disconnect" diye gerekçeliyor).
Sorun **raporlamada**:

`uinput_write_retry_ev` (`:2253-2263`) rapor kancasını **geçmiyor**:
```cpp
2253: static inline bool uinput_write_retry_ev(...)
2262:     return uinput_write_retry(fd, &ev, sizeof(ev));   // <-- 4. arg yok → {} → report boş
```
Bu tek-event varyantının **8 çağrı noktası** var ve hepsi 1:1 forward yolları:
`:2735 :2745 :2804 :2821 :2833 :2846 :2854 :2889`. Bunların **tamamı raw
passthrough'tur** (`:2734`, `:2803`, `:2820`, `:2845` koşulları + `:2888`).
Yani **raw modda bırakılan her REL = hiç görünmez.** Ölçüm:

```
batched flush (drop_report SET, process_device.cpp:2561):
    REPORT: uinput write stalled: dropped 72 tail byte(s) of a 72-byte frame after 32 attempts).
    -> flush() returned 1 ; reports emitted = 1
single-event path (uinput_write_retry_ev, report = {} default):
    -> flush() returned 1 ; reports emitted = 0  <<<< SILENT
```

Accel yolunda da throttle **tüm cihazlar arasında paylaşılıyor** —
`:2232 static double last_log_ms` fonksiyon yerelindeği:
```
4 simulated devices x 30 dropped frames -> 6 report(s) TOTAL
```
→ İki fare takılırsa 2 sn'lik pencerede **sadece birinin** düşüşü görünür.

---

### L09-04 | `daemon/daemon.cpp:2686` / `:2876` + `daemon/daemon.hpp:84` | **MED** — `pending_events` taşması **sessiz** ve **sayılmıyor**

Kapasiteler (kaynaktan): `write_batch.evs[16]` (`:2272`), `queued_events[16]`
(`:2585`), `pending_events[16]` (`daemon.hpp:84`), `read_batch[32]` (`:2573`).

`write_batch` ve `queued_events` taşmaları **bilinçli ve işleniyor** —
`:2293`, `:2610-2613`, `:2743-2747`, `:2831-2835` "flush in place / forward
inline, drop'ten iyi" diye açıkça yazıyor. `pending_events` taşması ise
**işlenmiyor**:
```cpp
2686:  for (size_t i = 0; i < queued_count && dev.pending_ev_count < dev.pending_events.size(); ++i)
2876:  for (size_t i = 0; i < queued_count && dev.pending_ev_count < dev.pending_events.size(); ++i)
```
Döngü kapasite dolunca **sessizce duruyor**. `grep -n pending_ev_count`
6 eşleşme veriyor; **hiçbiri tanı/sayaç/log değil**. Ölçüm:
```
park #1 (:2686): queued=16 -> pending_ev_count=16  (cap 16)
park #2 (:2876): queued=16 -> pending_ev_count=16  (cap 16)
>>> EVENTS SILENTLY DISCARDED = 16   counter? NO   log? NO
```
Not: `:2686` park'ı `pending_ev_count`'ı **önce sıfırlamıyor**; `:2875` sıfırlıyor
ama o yol `has_motion` için `:2875`'te sıfırlıyor. Pratikte aynı batch'te iki park
zor; **ASIL risk tek park'ta 16 kuyruk + 16 pending'i aşan birleşim** ve
oradaki artık sayaçsız. Etki: **kayıp tuş/wheel = hayalet takılma**, tam da
brifing'in tarif ettiği sınıf.

`kMaxDrainPerBatch = 4096` (`:2567`) kesilmesi **kayıpsız**: `epoll_ctl`
`EPOLLIN` **seviye tetikli** (`EPOLL_CTRL`'de `EPOLLET` yok — `:945`, `:1608`
ölçüldü), yani kalan veri bir sonraki dispatch'te gelir. Doğru.

---

### L09-05 | `include/rawaccel.hpp:44-60` / `:109-151` + `daemon/daemon.cpp:1243` | **HIGH** — Smoother'da **kalıcı** zehirlenme; "bir ölçüm kurtarmaz" sendromu **ölçüldü**

Görevdeki yıldız sorusu. **evet, kalıcı.**

`simple_ema_smoother::smooth` girdiyi **hiç korumuyor** — koruma `:51`'de
sadece `time`'a:
```cpp
44: double smooth(double speed, milliseconds time) {
51:     if (!(time > 0)) return std::min(windowTotal, cutoffTotal);
57:     windowTotal += twc * (speed - windowTotal);
```
`speed` bir kez NaN/Inf ise → `windowTotal = NaN` → **bir daha hiç çıkmaz**.

Kanıt 1 — smoother'ın kendisi (gerçek başlıklar, `ema_probe`):
```
B0 warm  smooth(100,1.0)                   = 99.999905
B1 smooth(NaN, 1.0)                       = nan  finite=0
B2 state after NaN: windowTotal=nan cutoffTotal=nan
B3 after 10000x smooth(100,1.0): last=nan  non-finite count=10000
B4 >>> simple_ema_smoother POISONED-PERMANENTLY = 1
B7 >>> linear_ema_smoother POISONED-PERMANENTLY = 1
C1 after +Inf then 10000x smooth(100,1.0): last=-nan non-finite=10000 poisoned=1
B8 after reset(), smooth(100,1.0)          = 12.944944 finite=1
```

Kanıt 2 — **`dt` sıfır/çok büyük kalıcı bozmuyor** (görevdeki ikinci yarı, olumlu):
```
A2 after 50x dt=0 then smooth(100,1.0)     = 24.214172  finite=1
A3 after 200x smooth(50, 1.0)             = 50.000000  recovered=1
A4 smooth(100, 1e12)                      = 100.000000 finite=1
A5 after 200x smooth(50, 1.0)             = 50.000000 recovered=1
A7 linear: after 20x dt=0, smooth(40,1.0)  = 40.000000 recovered=1
```
→ `:51`/`:113` `time` kapısı işini görüyor; `exp(-k·dt)→0` smoother'ı **öldürmüyor**,
tam tersine tam benimseme yapıyor (D2: halflife=0 → `smooth(100,1)=100`). Sorun
tam olarak **`speed`** eksikliği.

Kanıt 3 — **`test_ema_extreme_time` bunu ÖLMEZ** (`tests/test_accel.cpp:4184`):
`.smooth(` çağrılarının **hepsi sonlu literal** — grep ile 52 çağrı:
```
15 .smooth(10.0   11 .smooth(5.0   4 .smooth(42.0   4 .smooth(20.0
 4 .smooth(0.0     3 .smooth(100.0 2 .smooth(7.0    2 .smooth(50.0
 2 .smooth(1e15    1 .smooth(v     1 .smooth((double 1 .smooth(999.0
 1 .smooth(99.0    1 .smooth(77.0
```
`v` de `:7920`'de `(i%2==0)?10.0:0.0` — sonlu. **Hiçbir test NaN/Inf *speed*
girmiyor.** Kapı yeşil kalırken tehlike canlı — brifing §3'ün tanımladığı
**sessiz yeşil**. Mutasyonla kanıtlandı:
```
CONTROL  healthy curve, 100000 frames  -> out_x=1  scale.w=1.005   mouse alive: 1
TEST     poisoning frame (degenerate)  -> out_x=0  scale.w=inf
         after reconfigure to HEALTHY curve, 100000 frames:
         out_x=0  zero-frames=100000/100000  scale.w=-nan
         >>> STILL DEAD after switching profile: 1
         after full sp.init() (device replug only): out_x=1 alive=1
```

Kanıt 4 — **ÜRETİMDE ERİŞİLEBİLİR Mİ? evet.** `scale_smoother`'ın girdisi
`rawaccel.hpp:568` → `1.0 + (accel.apply(...) - 1.0)*range_weight`. sanitize
**serbest bıraktığı** parametrelerle `apply()` +Inf dönüyor:
```
=== non-finite apply() within the PRODUCTION speed domain ===
speed domain: 0 .. 6.87e+22  (32*INT32_MAX * IPS_FACTOR_MAX * dw_max)
  -> power         274628 non-finite / 36000000 evals
     [power] speed=0.01 gain=0 cap=in accel=1e+30 scale=1e+30 exp_pow=1e+22
             cap.x=1e+18 cap.y=1e-09 in_off=1e+30 out_off=5.6e-49 limit=0 sync=0.0001 -> inf
  -> synchronous   493456 non-finite / 36000000 evals
     [synchronous] speed=0.01 gain=1 cap=io accel=1 scale=2.76e-32 exp_pow=0.0001
             cap.x=1 cap.y=1e-30 in_off=0 out_off=4.6e-25 limit=1e+22 sync=0.0001 -> inf
TOTAL=216000000 non-finite=768084 (0.355594%)
```
`src/config.cpp:400 sanitize_accel_args()` bu değerlerin **hepsini geçiriyor**
(diziye transkripsiyon yapıp birebir uyguladım; `reproduce check: inf finite=0`).

Uçtan uca (`decisive.cpp`, gerçek `apply_motion_math`):
```
CONTROL  healthy curve, 100000 frames  -> out_x=1  scale.w=1.005   mouse alive: 1
TEST     poisoning frame (degenerate)  -> out_x=0  scale.w=inf
         after reconfigure to HEALTHY curve, 100000 frames:
         out_x=0  zero-frames=100000/100000  scale.w=-nan
         >>> STILL DEAD after switching profile: 1
         after full sp.init() (device replug only): out_x=1 alive=1
```

**En ağır kısım:** kurtuluş **profil değiştirmek değil, fareyi fişini çekmek.**
Çünkü daemon'ın canlı-uygulama yolu `sp.reconfigure()`
(`daemon/daemon.cpp:1243`), ve `reconfigure` (`rawaccel.hpp:242-258`) **sadece
ilk kez açılan** smoother'ı sıfırlıyor — zaten açık olanı **kasten koruyor**
(SM-5). Zehirlenmiş smoother o korumanın altında kalıyor. Kullanıcı GUI'de
sağlam profile geçer, fare hâlâ ölü, sebebi görünmez.

*Ölçmediğim*: bu arg setinin **gerçek bir GUI/CLI kullanıcısının kaydedebileceği**
bir profille gelip gelmediği — `sanitize`'i ölçtüm, GUI spin aralıklarını
ölçmedim. Erişilebilirlik **aşama-kapısı** (bkz. KAPSANMAYAN).

---

### L09-06 | `daemon/daemon.cpp:2196-2245` | **INFO** — `sleep` envanteri (görev 6, ham)

`grep -n 'sleep\|usleep\|nanosleep' daemon/daemon.cpp`:
```
664:    // Sticky-flag + sleep-poll loop (the codebase avoids condition variables).
698:        std::this_thread::sleep_for(std::chrono::milliseconds(10));
1485:        pending_hotplug_.store(true); // defer actual scan to run_loop (no usleep)
1692:        // The codebase uses no condition_variable (sticky-flag + sleep-poll
1699:        std::this_thread::sleep_for(std::chrono::milliseconds(20));
1701:        std::this_thread::sleep_for(std::chrono::milliseconds(200));
2213:                nanosleep(&ts, nullptr);
```
Lane'ime düşen **tek** eşleşme `:2213`. Amaçları:
- `:664`/`:698` — yorum + IPC dinleme iş parçacığı, fare yolunda değil.
- `:1485` — yorum (hot-plug erteleme, **uyumadan** tarama yapılıyor).
- `:1692`/`:1699`/`:1701` — yorum + `hidpp_thread_` iş parçacığı, HID++ kuyruğu
  boşken 20 ms / iş varken 200 ms. `AGENTS.md`'ye göre `hidpp_thread_`'de,
  döngü iş parçacığında **değil** (P171-BFIX) — benim lane'imdeki
  `run_loop`'dan farklı iş parçacığı.
- `:2213` — **teknik olarak event yolunda**, ama yalnızca EAGAIN/backoff'ta
  (L09-01).

`flush_motion` (2344–2547) içinde: **`sleep` yok, `std::mutex`/`lock_guard`
yok, `malloc`/`std::string` geçici yok** — grep ile doğrulandı (`grep -n
'malloc\|new \|std::string\|std::function\|std::vector\|to_string\|log('`
2344–2547 aralığında → **boş çıktı**). `std::atomic` seqlock telemetrisi
(`:2529-2537`) kilit içermiyor. Ölçülen tahsis:

```
  iterations                        = 1000000
  operator new() calls during run    = 0
  mallinfo2 uordblks before/after    = 77856 / 77856 bytes  (delta +0)
  >>> ALLOCATION-FREE hot path: 1
```
`write_batch out` (`:2560`), `read_batch[32]` (`:2573`), `queued_events[16]`
(`:2585`) — hepsi stack.

Lane'imdeki **tek** mutex'ler `run_loop`'ta ve **event'in dışında**:
`:2001 push_cfg_mu_`, `:2060`/`:2113`/`:2125 devices_mutex_`. Bunlar fare
olayının işlenmesinden **önce/sonra** (fd→devi eşleme ve temizlik), sıcak yolda
değil.

---

## ⭐ SESSİZ YEŞİL ENVANTERİ (brifing §3)

| # | "Çalışıyor" görünen | Gerçekte | Kanıt |
|---|---|---|---|
| 1 | `:2244 return true` — yazma başarılı | Bütçe tükendi, kare **düştü** | L09-01/03, `written=0 … return_ok=1` |
| 2 | Durum JSON'unda `real_polling_rate` var | <100 Hz'de **eskisi** | L09-02, 50 Hz→1000 yazıyor |
| 3 | CLI/GUI `[stale]` işareti var | **hızı** izlemiyor, telemetriyi izliyor; yavaş akışta yanmaz | L09-02, `cli/main.cpp:1984` |
| 4 | `test_ema_extreme_time` yeşil | NaN *speed* girdisini **hiç denemiyor** | L09-05, 52 `.smooth(` çağrısı sonlu |
| 5 | R5-A "sessiz düşüş görünür hale getirildi" | `uinput_write_retry_ev` kancayı **geçmiyor** → raw yolunda **hiç görünmez** | L09-03, `reports emitted = 0` |
| 6 | `pending_events` taşması diğer iki taşmayla aynı sınıf | O ikisi işleniyor, bu **sessiz** | L09-04, sayaç/log yok |

---

## KAPI

```
$ ./l09_test_accel --filter 'EMA smoothers with extreme time'
[edge — EMA smoothers with extreme time values, no divergence]
  PASS  bad == 0
  PASS  bad == 0
  PASS  bad == 0
=== Sonuç: 3/3 geçti (1 section eşleşti, 216 atlandı) ===
RC=0        <-- ÜRETİLEN SAYI: 3 assertion, 1 section
```
Derleme: `g++ -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Iinclude -Isrc`
+ `test_accel.cpp` + `src/{config,logitech_receiver,logitech_hidpp}.cpp`
→ **`/tmp/opencode/l09/l09_test_accel`** (`build-manual/`'ı diğer ajanlar
kullandığı için **bilerek /tmp'ye** derledim; çalışma ağacına dokunulmadı).

Ölçüm binary'leri (hepsi /tmp): `retry_sat`, `polling`, `queue`, `pending`,
`alloc`, `sweep3`, `decisive`, `ema_probe`, `mut_gate`, `sweep`, `sweep2`.
`/tmp/opencode/l09/` altında duruyor; sunucu restart'ı bir kez sildi, yeniden
üretildi.

**Çalışma ağacı temizliği**: `git status --porcelain` → kaynak dosyalarda
`daemon/`, `include/`, `src/`, `tests/` altında **hiçbir `M` yok**; görünen
değişiklikler `.aihaberlesme/` altında ve **başka ajanların**. Mutasyon
kopyaları `/tmp/opencode/l09/mut*/` içinde:
`grep -c MUTATION include/rawaccel.hpp` → **0**.

---

## KAPSANMAYAN

- **`daemon/daemon.cpp` 1–1987 ve 2894+** — başka ajanların sahibi, dokunulmadı.
- **`uinput_write_retry`'nin gerçek uinput fd'si üzerinde ölçümü** — kök/`<dev/uinput>`
  yok; doygunluk **gerçek O_NONBLOCK socketpair** ile taklit edildi (EAGAIN teyitli).
  Gerçek uinput kuyruk derinliği farklıysa *blok süresi değil*, *hangi kare düşer*
  değişebilir.
- **L09-05 erişilebilirlik zinciri**: `sanitize`'i ölçtüm (geçiriyor). Ama
  `gui/*.inl` spin aralıkları ile `cli set-param` alan adanları **ölçülmedi** —
  yani "gerçek bir kullanıcı bu profili kaydedebilir mi" aşaması açık. L09-05'in
  şiddeti bu aşamaya bağlı; mekanizma ve kalıcılık **aşamasız** kanıtlandı.
- **`status_json`'ın kendisi (`:3072-3200`)** — okudum, publish edilen alanları
  listeledim; IPC eşzamanlılığını (seqlock) ölçmedim.
- **`lat_stats` histogramı** — `flush_motion:2545 record()` çağrısını gördüm,
  kilit maliyetini ölçmedim (brifing §8: kilit var mı sorusu `lat.record`'a
  uzanmadı; `record` muhtemelen kendi mutex'ini alıyor — **doğrulamadım**).
- **`hidpp_thread_` (`:1692-1701`) ve IPC (`:698`)** — sleep'leri listeledim,
  fare yolunda **olmadıklarını** koddan çıkardım, **zamanlamayı ölçmedim**.

## TEMSİL SINIRI

- **Donanım yok.** CS2'de 1000→125 Hz düşüşünü, USB güç tasarrufu davranışını
  veya gerçek fare takılmasını **gözlemledim yok** — L09-02 tamamen kod
  yürütmesinin ölçümü; `:2459`/`:2497` kapılarının gerçekte ne sıklıkta
  tutulduğunu bilmiyorum. 100 Hz altı **mümkün** (USB autosuspend, sürücü
  düşürme) ama **o sıklığı ölçemedim**.
- **`/dev/uinput` yok / root yok.** `run_e2e.sh` koşmadım. L09-01'in 109 ms'i
  gerçek kernel uinput doygunluğu değil, aynı EAGAIN koşulunun ölçülmüş
  karşılığı.
- **`tests/run_tests.sh`'in tamamını koşmadım** (diğer ajanlar `build-manual/`
  kullanıyor). Kendi temiz build'imle **tek bölümü** koştum: 3/3 yeşil.
  L09-05'in "kapı yeşil kalırken tehlike canlı" iddiası, o bölümün
  **34.164 assertion'ının tamamını koşmadığım** için bölüm-ve-mutasyon
  düzeyindedir; 8.349.916 mutasyonlu build'i derledim (`mut2_test`) ama
  **koşturmadım** — sunucu restart'ı kesti.
- **`run_tests_asan.sh` koşmadım** — `test_accel.cpp`'a dokunmadım, bu lane
  için gerekli değil.
- **Zamanlama ölçümleri tek koşu.** `retry_sat` 109.0–109.8 ms; yük
  değişimiyle sapabilir. Kod yorumunun "~120 ms" iddiasıyla uyumlu.
