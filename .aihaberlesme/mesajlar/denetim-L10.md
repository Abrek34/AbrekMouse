### L10 | alt-ajan (denetim alt-ajanı) | 2026-10-01

**KAPSAM** (kesin):
- `daemon/daemon.cpp` **yalnız 2901–3601**: `dump_latency_stats`, `json_str`, `append_fixed`,
  `ipc_sock_path`, `status_json`, `start_ipc_server`, `stop_ipc_server`, `ipc_serve_loop`,
  `handle_ipc_client`, `MAX_CONFIG_PUSH_BYTES`, `IPC_REQUEST_DEADLINE_NS`, `CONFIG_BODY_DEADLINE_NS`
- `daemon/lat_stats.hpp` (195 satır, tamamı)
- `daemon/motion_math.hpp` (74 satır, tamamı)

**BULGULAR:**

---

#### ⭐ L10-01 | daemon/daemon.cpp:3046-3050 + 3284-3299 + 3387-3442 | **INFO — görevde varsayılan CRIT YOK, ÖLÇÜLDÜ**

Lane brief'i "`chmod` yoksa ve yol `/tmp` ise → CRIT: her kullanıcı erişir" diyordu. **Ölçtüm: o CRIT mevcut değil.**

```
$ grep -n 'chmod\|chown\|umask' daemon/daemon.cpp daemon/main.cpp
daemon/daemon.cpp:3285:    // chown root:input + chmod 0660 → only root and input-group members can connect.
daemon/daemon.cpp:3286:    // If chown fails (e.g. no input group), fall back to owner-only (0600).
daemon/daemon.cpp:3292:            if (chown(sock_path.c_str(), 0, grp->gr_gid) != 0)
daemon/daemon.cpp:3295:            chmod(sock_path.c_str(), 0660);
daemon/daemon.cpp:3297:            chmod(sock_path.c_str(), 0600); // fallback: owner only
daemon/main.cpp:61:    if (fchmod(fd, 0644) != 0) // L-BUG-3: keep the "running" probe readable —
```
```
$ grep -n '/tmp' daemon/*.cpp daemon/*.hpp
daemon/main.cpp:21:static constexpr const char* PID_FILE2  = "/tmp/rawaccel.pid"; // root-fallback
daemon/main.cpp:253:/// `/tmp/x/../../../dev/shm/a.json` passed.  Measured: that input was accepted
daemon/main.cpp:416:    // D5: Priority: $XDG_RUNTIME_DIR (user runtime), /run/ (root), /tmp/ (fallback).
daemon/main.cpp:420:    // old OR-chain (write_pid(xdg) || write_pid(/run) || write_pid(/tmp))
daemon/main.cpp:422:    // could win the /tmp fallback while a live daemon owned the XDG file —
```
`/tmp` **sadece PID dosyası** için (`PID_FILE2`, main.cpp:21), soket için **değil**.
`ipc_sock_path()` (daemon.cpp:3046-3050) yalnız `$XDG_RUNTIME_DIR/rawaccel.sock` ve `/run/rawaccel.sock` üretir.

Dört katmanlı savunma, hepsi koda bağlı:
1. `chmod 0660` + `chown root:input` (`:3292-3295`), `input` grubu yoksa `chmod 0600` (`:3297`)
2. `SO_PEERCRED` kimlik kapısı (`:3392-3438`) — DAC'tan bağımsız ikinci kontrol
3. Shipped unit `scripts/rawaccel.service` → `UMask=0077`: bind (`:3272`) ile chmod (`:3295`) arasındaki
   TOCTOU penceresi **shipped dağıtımda kapalı** (soket 0700 doğar, 0660'a daraltılır)
4. `99-rawaccel.rules` aynı `input` grubuna `/dev/uinput` + `/dev/input/event*` rw veriyor →
   **soket, grubun zaten sahip olduğu yetkiden fazlasını vermiyor.** `input` üyesi zaten
   keyfi sanal fare üretip keyfi REL_X/REL_Y gönderebiliyor.

CLI tarafı ayrı yolu kapatıyor: `cli/main.cpp:243-249` `daemon_sock_candidates()` her iki yolu da
deniyor (kullanıcı shell'inde `XDG_RUNTIME_DIR` **set**, sistem daemon'u `/run`'de dinliyor).
Ölçtüm: `daemon_ipc_query` sabit tampon kullanmıyor, 16 MB runaway korumasıyla döngüde okuyor
(`cli/main.cpp:288-300`) → cevap kırpılmıyor.

---

#### L10-02 | daemon/daemon.cpp:3295 ve :3297 | **MED — `chmod`'un dönüş değeri atılıyor, `chown`'unki atılmıyor**

```
3287:    {
3288:        struct group* grp = getgrnam("input");
3289:        if (grp) {
3292:            if (chown(sock_path.c_str(), 0, grp->gr_gid) != 0)      ← KONTROL EDİLİYOR
3293:                log("IPC: chown(input group) failed: " + ... );
3295:            chmod(sock_path.c_str(), 0660);                          ← DÖNÜŞ DEĞERİ ATILIYOR
3296:        } else {
3297:            chmod(sock_path.c_str(), 0600); // fallback: owner only   ← DÖNÜŞ DEĞERİ ATILIYOR
3298:        }
3299:    }
```
Güvenlik kontrolü olan tek çağrının hatası görünmez. `chmod` EROFS/EPERM dönerse soket umask'tan
gelen modda kalır ve daemon **"IPC socket: <path>"** diye başarı logları (`:3327`). Shipped unit'te
`UMask=0077` olduğu için bu **fail-closed** yönde çalışır (0700 kalır) — sömürülebilir bir açık
değil, savunma-derinliği. Ama `CAP_FOWNER` kaldırılmış bir unit varyantında sessizce
`/run` mount'ı salt-okunur olduğunda kontrolün tek satırı sessizce düşer.

---

#### L10-03 | daemon/daemon.cpp:3485-3501 + 3532-3539 | **INFO — uzunluk denetimi VAR ve doğru**

```
3537:            unsigned long long body_len = strtoull(sz, &end, 10);
3538:            if (errno != 0 || end == sz || body_len == 0 ||
3539:                body_len > MAX_CONFIG_PUSH_BYTES || *end != '\0') {
3540:                response = "{\"ok\":false,\"error\":\"invalid config payload size\"}\n";
```
- `MAX_CONFIG_PUSH_BYTES` = 1 MB (`:2982-2983`). `body.reserve(body_len)` yalnız doğrulanmış
  boyuttan sonra (`:3543`), yani bellek ayırma sınırı da 1 MB.
- `strtoull("-1")` → `ULLONG_MAX`, errno 0 → `> 1 MB` dalı ile yakalanır.
- `*end != '\0'` → `"set_config 5 6"` ve `"set_config 5x"` reddedilir.
- Komut satırı `:3485 while (line.size() < 256)` ile 256 bayta kırpılır.
- Ölüme zaman aşımları: `:3471` toplam 10 s, `:3546` gövde 5 s. Yavaş-loris yüzeyi kapalı.
**Beklenen taşma/bellek taşması yok.**

---

#### L10-04 | daemon/daemon.cpp:3215-3255 | **INFO — başka daemon'ın soketini ele geçirmeye çalışmıyor (ölçüldü, olumlu)**

```
3216:    if (lstat(sock_path.c_str(), &existing) == 0) {
3217:        if (!S_ISSOCK(existing.st_mode)) { ... return false; }
3235:        const bool live = connect(probe, ...) == 0;
3237:        const int probe_errno = errno;                 ← close()'ten ÖNCE okunuyor, doğru sıra
3239:        if (live) { log("...already owned by a running daemon"); return false; }
3243:        if (probe_errno != ECONNREFUSED && probe_errno != ENOENT) { ... return false; }
3248:        if (unlink(sock_path.c_str()) != 0 && errno != ENOENT) { ... return false; }
3252:    } else if (errno != ENOENT) { ... return false; }
```
`lstat` (symlink izlemez) + `S_ISSOCK` + canlı-bağlantı probu + **fail-closed** (EACCES vb.
`unlink`'e düşmez). `unlink`(3248) ile `bind`(3272) arasındaki yarışta bile AF_UNIX `bind`
var olan yolda `EADDRINUSE` verir (symlink izlemez) ve `:3276-3278` bunu ayrı mesajla bildirir.

---

#### L10-05 | daemon/daemon.cpp:3363-3454 ve 3576-3590 | **LOW — yanıt yazma döngüsünde toplam deadline yok, ama ÖLÇÜLDÜĞÜ için erişilemez**

`ipc_serve_loop()` **bloklamıyor** ve fare işlemesini **durdurmuyor**:
- Ayrı thread: `ipc_thread_ = std::thread(...)` (`:3316`), `poll()` 1000 ms (`:3372`),
  dinleme fd'si `SOCK_NONBLOCK` (`:3257`)
- **Hot path tamamen kilitsiz.** Ölçüm:
```
$ awk 'NR>=2320 && NR<=2600 && (/lock_guard|scoped_lock|unique_lock|mtx/)' daemon/daemon.cpp
(boş = flush_motion 2320-2547 içinde HİÇBİR mutex yok)
$ awk 'NR>=2547 && NR<=2893 && (/lock_guard|scoped_lock|unique_lock|devices_mutex_/)' daemon/daemon.cpp
(boş = process_device 2547-2893 içinde de HİÇBİR mutex yok)
$ grep -n 'std::thread' daemon/daemon.cpp
531: loop_thread_  544: hidpp_thread_  557: save_thread_  3316: ipc_thread_
```

Tek istisna paylaşılan kilit `lat_stats::mtx`. Ölçtüm (200k çağrı):
```
record()  (motion thread, 1 per event)          = 0.011 us/call
copy()    (status_json, 1 per device per query) = 0.013 us/call
-> worst-case stall injected into flush_motion by ONE status query = 0.013 us
   (a 125 us frame budget is 125.000 us)
```

**Ölçülen kusur:** `:3578 while (left > 0)` döngüsü yalnız *her `send()` başına* 2 s
`SO_SNDTIMEO`'ya (:3464-3466) dayanıyor — döngünün kendisinde toplam deadline yok. Doğru
çıktı/ölçüm (replica, `probe_resp`):
```
  devices=   1  resp=    579 B  ->  loop held      0.0 ms   [SO_SNDTIMEO=2 s]
  devices=   8  resp=   3939 B  ->  loop held      0.0 ms
  devices=  32  resp=  15481 B  ->  loop held      0.0 ms
  devices= 128  resp=  61685 B  ->  loop held      0.1 ms
  devices= 256  resp= 123381 B  ->  loop held      0.1 ms
  devices= 512  resp= 246773 B  ->  loop held   4063.2 ms ( 4.06 s)   [send() w<=0 -> drop]
```
Gerçek cevap boyutu: **100 B + ~482 B/cihaz**; bu hostta `SO_SNDBUF=SO_RCVBUF=212992`.
Yani sınır **~4.06 s'de** ve onu aşmak için ~512 fare gerekiyor → **fiziksel olarak erişilemez.**
Ayrıca `SO_SNDTIMEO` 2 s'yi değil **4.06 s** verdi (kısmi `send` + çoklu iç bekleme), yani
komentodaki "2 s sınır" tahmini de tam değil. Kod düzeyinde doğru, pratikte erişilemez → **LOW**.

Bağlanıp hiçbir şey göndermeyen istemci ölçüldü: **2.05 s** ile kesiliyor ve timeout yanıtı
alıyor (`:3476-3479`) — sınırsız değil.

---

#### L10-06 | daemon/daemon.cpp:3001-3016 | **MED — `json_str` 0x80 üstü baytları olduğu gibi geçiriyor; CLI'nin kendi parser'ı patlıyor**

`json_str` (`:3001-3016`) yalnız `" \ \n \r \t` ve `<0x20`'yi ele alıyor; `else` dalı
`:3012 out += static_cast<char>(c)` → **0x80–0xFF ham geçiyor.** Fonksiyonu `daemon.cpp:3001-3044`'ten
**birebir** çıkarıp projenin kendi `nlohmann/json.hpp` parser'ına karşı 256 tek-bayt girdiyle dövdüm:
```
=== L10-A: json_str exhaustive single-byte fuzz (0x00..0xFF) ===
  byte 0x00..0x7F  -> 128/128 parse OK   (tüm ASCII güvenli: tırnak, backslash, NUL, DEL, hepsi)
  byte 0x80..0xFF  -> 0/128 parse OK      (128/128 FAİL)
exhaustive 1-byte: 128 parsed, 128 failed
```
Somut kırılma (sonda üretilen GERÇEK `status_json` şekli):
```
  name = "bad \xff\xfe seq"
  doc  = {...,"name":"bad �� seq",...,"lat_avg_us":1.50,...}
  nlohmann: [json.exception.parse_error.101] ... invalid string: ill-formed UTF-8 byte
```
`dev.name` ham kernel baytıdır, UTF-8 doğrulaması yok:
```
$ sed -n '711,717p' daemon/daemon.cpp
    char name[256] = {};
    if (ioctl(dev.fd_in, EVIOCGNAME(sizeof(name) - 1), name) < 0)
        snprintf(name, sizeof(name), "unknown(%s)", dev.path.c_str());
    name[sizeof(name) - 1] = '\0';
    dev.name = std::string(name);
```
(`EVIOCGNAME` sürücünün koyduğu serbest bayt dizisi — geçersiz UTF-8 mümkün.)

**Ölçülen etki zinciri (tüketiciler ayrı ayrı):**
- **GUI bağışık.** `gui/daemon_comm.inl:403-470` nlohmann kullanmıyor; elle yazılmış bayt-seviyesi
  tarayıcı (`json_skip_string`/`json_object_end`) kullanıyor.
- **`rawaccel-cli status` bozuluyor.** `cli/main.cpp:2161` `nlohmann::json::parse` → throw →
  `:2242-2248` stderr'a **"(daemon unreachable for live device details: ...)"** basıyor.
  ⛔ **Bu teşhis YANLIŞ:** daemon'a ulaşılabiliyor, sadece cevabı UTF-8 temiz değil.
  Cihaz listesi tamamen kayboluyor ve **exit 0**.
- **`rawaccel-cli status --json`**: `:2086-2091` → `out["device_error"]`, `devices` dizisi hiç
  konmaz, config OK ise **exit 0**.
- **`rawaccel-cli monitor`** `:1952-1955` en iyisi: "Daemon returned unparseable status".

Yani **tek bir tuhaf cihaz adı, bütün `rawaccel-cli status` çıktısını sessizce boşaltır** ve
exit kodu yeşil kalır. `daemon/main.cpp:557`'deki log `json_escape` de 0x80'i geçiriyor (aynı
tasarım), ama log tüketicisi JSON ayrıştırmıyor.

---

#### L10-07 | daemon/daemon.cpp:3024-3044 | **INFO — sessiz snprintf kırpılması VAR ama ERİŞİLEMEZ (ölçüldü)**

`:3033 snprintf(nb, sizeof nb, "\"%s\":%.*f", key, prec, v)` + `:3042 o.append(nb, min(n, 63))`
→ `n >= 64` olduğunda sayı **ondalısız ve kapanmadan** kesilir → geçersiz JSON. İlk kıran büyüklük:
```
key=lat_avg_us      first truncating v = 1e+46   -> JSON BROKEN
   emitted: ,"lat_avg_us":10000000000001772106937309424173505338740310016.00
key=telem_wall_ms   first truncating v = 1e+43   -> JSON BROKEN
key=telem_speed_ips first truncating v = 1e+41   -> JSON BROKEN
key=telem_in_ips    first truncating v = 1e+44   -> JSON BROKEN
```
**Üretim tavanı ölçüldü** — `telem_in_ips = magnitude(dx,dy) · dpi_factor / time_ms`
(`daemon.cpp:2525-2526`), `time_ms ≥ DEFAULT_TIME_MIN` (`rawaccel-base.hpp:15` = 0.0625 ms),
`dpi_factor ≤ 1000` (dpi ≥ 1, `src/config.cpp:361`), `|(dx,dy)| ≤ 32·INT32_MAX·√2` (P93-BATCH):
```
max reachable telem_in_ips = 1.55494e+15  -> snprintf n=38 (buffer 64, kırpılma n>=64 gerektirir)
rendered: "telem_speed_ips":1554944255263660.250
=> append_fixed truncation is UNREACHABLE from any valid config
```
**26 büyüklük mertebesi pay var.** Ölçülmüş güvenlik payı — CRIT değil, ama savunmasız: `assert`
veya genişletilmiş tampon yok, sadece "bugün erişilemez" gerçeği.

---

#### L10-08 | daemon/lat_stats.hpp:111-119 ve 132-147 | **MED — `percentile()` taşma örneklerinde sessizce `max_us` dönüyor**

`record()` `:81-87` >`RANGE_US` örneği `hist[]`'ye **girmiyor**, `over`'a gidiyor; ama `:77 count++`
**ikisini de sayıyor**. Tarama `:111-118` `cum >= target`'a ulaşamayınca `:119 return max_us` → **o
percentile, dağılım ne derse desin tek en büyük örneğe eşitleniyor.** Yorum satırı yalnız
`// all overflow` diyor, karışık durumu kapsamıyor.

Ölçüm (1×2.00 µs + 99 örnek 600…5000 µs'e doğru yayılmış):
```
  count=100  over=99  min=2.00  max=5000.00  avg=2772.02
  pct    lat_stats    EXACT        error
  p25    5000.00      1632.65      3367.35     <== WRONG by >1us
  p50    5000.00      2755.10      2244.90     <== WRONG by >1us
  p75    5000.00      3877.55      1122.45     <== WRONG by >1us
  p95    5000.00      4775.51       224.49     <== WRONG by >1us
  p99    5000.00      4955.10        44.90     <== WRONG by >1us
```
Kullanıcıya görünen hali: `dump_latency_stats` (`:2957-2964`) Avg ve p50/p95/p99'u **yan yana**
basıyor → **"Avg 2772.02 µs / p50 5000.00 µs"**, yani **p50 > avg**, gerçek bir dağılımda imkânsız.
`rawaccel-cli monitor` ve GUI mouse-test aynı `lat_p50_us`/`lat_p95_us` alanlarını gösteriyor.

---

#### ⭐ L10-09 | tests/test_accel.cpp:2826-2835 | **HIGH — SESSİZ YEŞİL, MUTASYONLA KANITLANDI**

Overflow bloğu bir taşma örneği kaydediyor ama **`percentile()`'ı hiç çağırmıyor**:
```
2826:    // ── Overflow bucket: sample > RANGE_US ───────────────────────────────
2829:        ls.record(1.0);                              // normal
2830:        ls.record(lat_stats::RANGE_US + 100.0);      // overflow (> 500µs)
2831:        EXPECT(ls.count == 2);
2832:        EXPECT(ls.over == 1);
2834:        EXPECT(ls.max_us > lat_stats::RANGE_US);
2835:    }
$ awk 'NR>=2826 && NR<=2836 && /percentile/' tests/test_accel.cpp
(boş = o blokta percentile HİÇBİR YERDE çalışmıyor)
```

**Mutasyon kanıtı** (brifing §4.1 — `/tmp/opencode/L10/mut` kopyası üzerinde, çalışma ağacına
dokunulmadan; `md5sum` ile kopyanın birebir olduğu doğrulandı):
`lat_stats.hpp:119` **ve** `:146` → `return max_us;` yerine `return 0.0;`
```
$ ./bin/test_accel_mut
=== Sonuç: 34164/34164 geçti ===
=== EXIT CODE = 0 ===
```
**Pozitif kontrol** (kanıt yönteminin çalıştığını gösterir — aynı baytlar, aynı bayraklar):
`lat_stats.hpp:93` `avg_us()` → `sum_us / (count + 1)`
```
$ ./bin/test_accel_pc
  FAIL  tests/test_accel.cpp:2823  |ls.avg_us() - 2.0| = 0.019802 > 1e-09  (section: lat_stats — histogram, percentile, snapshot_and_reset)
=== Sonuç: 34162/34164 geçti, 2 BAŞARISIZ ===
=== EXIT=1 ===
```
Yani harness bu başlığı **görüyor** (pozitif kontrol kırmızı), `percentile` overflow yolunu
**görmüyor** (mutant yeşil). Baseline aynı bayraklarla: `34164/34164, exit 0`.

---

#### ⭐ L10-10 | AGENTS.md:392 ↔ daemon/lat_stats.hpp:74-75 | **HIGH — dokümante edilmiş KAPSAMA İDDİASI ÖLÇÜLDÜĞÜ KADAR YANLIŞ**

`AGENTS.md:392` kapsananlar listesinde sayıyor:
```
392:  lat_stats non-finite/negative-sample guard
```
Koruduğu kod:
```
daemon/lat_stats.hpp:74:        if (!std::isfinite(lat_us)) return;
daemon/lat_stats.hpp:75:        if (lat_us < 0) lat_us = 0;
```
**Mutasyon:** `:74` isfinite koruması **tamamen silindi** (`// MUTANT` bırakıldı)
```
$ ./bin/test_accel_mut2
=== Sonuç: 34164/34164 geçti ===
=== EXIT=0 ===
```
**Nedeni, pozitif kontrollü sayım:** suite'teki **18 `record()` çağrı noktasının tamamı** sonlu,
negatif olmayan bir literal geçiriyor — hiçbir yerde NaN/Inf/negatif yok:
```
$ grep -n 'record(' tests/test_accel.cpp
2800,2811,2812,2829,2830,2840,2861,2862,2873,2884,2897,2911,2912,7655,7656,7657,7675,8110
$ grep -n 'quiet_NaN\|infinity()\|NAN\|INFINITY' tests/test_accel.cpp | grep -i 'record\|lat'
(boş)
```
`:75` (`lat_us < 0`) de aynı şekilde testsiz — hiçbir `record(-x)` çağrısı yok.
Ayrıca `olcum/aj2/sayisal_iddialar.txt` bu iddiayı denetlemiyor:
```
$ grep -n 'non-finite\|lat_stats' olcum/aj2/sayisal_iddialar.txt
(boş)
```
`prove_kod_ayni.py --sayi` bu satırı kapsam dışı, yani AGENTS.md'deki bu madde
`--sayi`'nin yakalayamayacağı türden bir çürüme.

---

#### L10-11 | daemon/daemon.cpp:3560-3562 + save_worker `:663-698` | **MED — SESSİZ YEŞİL: `ok:true` ama config diske hiç yazılmayabilir**

`push_config`'in kendi yorumu (`:592-594`) söylüyor:
```
    // change: true now means "accepted and queued for save", not "saved"; a
    // save failure is logged by the worker and the config is not applied.
```
İstemci `:3561-3562`'de `{"ok":true,"config":"..."}` alıyor. Sonrasında `save_worker()`:
```
void AccelDaemon::save_worker() {
            try {
                save_config(item->first, item->second);
                ... push_cfg_pending_ = true; ...          // yalnız BAŞARILI yazımda uygulanır
            } catch (const std::exception& e) {
                log("Config save failed: " + std::string(e.what()));   ← SADECE LOG
            } catch (...) {
                log("Config save failed: unknown exception");         ← SADECE LOG
            }
```
`/etc` diskdolu veya salt-okunur → **istemci `ok:true` gördü, config ne yazıldı ne uygulandı**,
tek sinyal journald'da bir satır. Aynı desen `reload`'da (`:3508-3510` flag + `ok:true`;
fiili reload `:2026-2032` başarısız olup sadece loglar).
Bu tam olarak brifing §3'ün "sessiz yeşil"i: *düzeltme yolu kayıt tutuyor ama geri almıyor.*

---

#### L10-12 | daemon/daemon.cpp:3360 | **LOW — `stop_ipc_server` sahiplik/tip denetimi olmadan `unlink`**

```
3359:    if (!path_to_unlink.empty())
3360:        unlink(path_to_unlink.c_str());        ← dönüş değeri atılıyor, lstat/sahiplik yok
```
`start_ipc_server`'ın `unlink`(3248)/`bind`(3272) penceresi güvenli (AF_UNIX bind symlink
izlemez → EADDRINUSE). Buraya ulaşmak için saldırganın zaten o dizinde yazma yetkisi olması
gerekir, yani **yeni bir yetki kazandırmıyor** — kayda geçti, yükseltilmedi.

---

#### L10-13 | daemon/daemon.cpp:3046-3591 | **INFO — yanlış pozitifleri önlemek için açıkça "TEMİZ" işaretlediklerim**

- **`config_path_` yarışı YOK.** `:3562`, `:653`, `:655` kilitsiz *görünüyor*, ama tek yazıcı
  `start()` `:469`; o `running_.store(true)` `:525`'ten (seq_cst) önce çalışıyor ve
  `push_config` `:601`'de `running_.load(acquire)` ile kapıya giriyor → happens-before var.
  `:2029`'daki okuyan loop thread'i `:531`'de doğuyor, yani `:525`'ten sonra.
  → **BULGU DEĞİL.** (Bu, ilk okumada CRIT görünüyordu; ölçülerek düşürüldü.)
- **Hot path kilitsiz**, yukarıda ölçüldü.
- **Kötü isteğe "tamam" yanıtı YOK** (brifing madde 7). Her hata yolu okunabilir olmayan yanıt
  döndürüyor: bilinmeyen `:3568`, geçersiz boyut `:3540`, eksik gövde `:3559`, reddedilen config
  `:3564`, timeout `:3477`. Ölçüm: hiçbir malformed istek `ok:true` üretmiyor.
  Sessiz-ok yalnızca **sonraki** aşamada (L10-11).
- **`lat_stats` 0/1/N sınırları temiz** (ölçüm):
```
  count=0 : avg=0 p0=0 p50=0 p99=0 p100=0 min=1e+09 max=0   (bölme sıfırı YOK, saçma değer YOK)
  count=1 : avg=10 p50=10.25 p99=10.25 p100=10 min=10 max=10
  count=100 uniform 1..100us: p50=50.25 p95=95.25 p99=99.25 max=100.00 avg=50.50
     (tam p50 50.50 / p95 95.05 / p99 99.01 → hata = bir kova genişliği 0.5 µs, DOKÜMANLI)
  korumalar: record(-5.0) → 0.00 ; record(NaN) düşürülüyor ; record(+Inf) düşürülüyor ; count=1
```
`test_lat_stats` sıfır/tek/çok örneği **kapsıyor** (`:2788-2795`, `:2797-2805`, `:2807-2824`) —
bulgum kapsam değil, **taşma + percentile bileşimi**.

---

#### KAPI (koşturduğum kapı/tampon komutları)

| # | Komut | rc | Üretilen sayı |
|---|---|---|---|
| K1 | `/tmp/opencode/L10/probe_json` — `json_str`+`append_fixed` (3001-3044'ten birebir) × nlohmann | 1 | 256 tek-bayt girdi: **128 pass / 128 fail**; 12 adlandırılmış kaçış girdisi: 11 pass / 1 fail; 5 anahtar için kırpılma eşiği |
| K2 | `/tmp/opencode/L10/probe_resp` — cevap boyutu + accept-loop tutma süresi | 0 | 9 boyut ölçümü (100 B … 246773 B); 2 senaryo × 6 nokta; `SO_SNDBUF/RCVBUF=212992` |
| K3 | `/tmp/opencode/L10/probe_starve` — bloklayan-istemci topolojisi | 0 | senaryo A: 3 ölçüm; B: 6 ölçüm; C: 2 ölçüm |
| K4 | `/tmp/opencode/L10/probe_lat` — `lat_stats` kilit maliyeti + percentile + sınırlar | 0 | 200k çağrı × 2; 6 overflow senaryosu; 6 sınır senaryosu |
| K5 | `/tmp/opencode/L10/probe_pct` — yayılmış overflow percentile hatası | 0 | 6 percentile × (rapor/kesin/hata) |
| **K6** | **`tests/test_accel.cpp` GERÇEK ikilisi, baseline + 3 mutant, `-I copy/include -I copy/src`** | | |
| K6a | baseline (başlıksız kopya) | **0** | **34164/34164 geçti** |
| K6b | **M1**: `:119`+`:146` `max_us`→`0.0` | **0** | **34164/34164 geçti** ⛔ sessiz yeşil |
| K6c | **M2**: `:74` isfinite koruması silindi | **0** | **34164/34164 geçti** ⛔ sessiz yeşil |
| K6d | **pozitif kontrol M3**: `:93` `avg_us` → `/(count+1)` | **1** | 34162/34164, 2 FAIL — harness çalışıyor |

Tüm derlemeler `/tmp/opencode/L10/`. Çalışma ağacı **dokunulmadı**:
```
$ git diff --stat HEAD -- daemon/ tests/
(boş)
$ md5sum daemon/lat_stats.hpp
70aa98583556db0ef130871c46a5c2c3   ← başlangıçtaki hash ile aynı
```

---

**KAPSANMAYAN** (lane dışı, birinin bakması gereken):
1. **`push_config` (`:590-698`) ve `save_worker` (`:663-698`)** L10-11'in *sonuçları*, ama
   `:2901` öncesi → **başka lane'in sahipliği.** `ok:true` semantiğinin kaydı orada.
2. **`daemon/main.cpp:557` `json_escape`** L10-06 ile aynı sınıf (0x80'i geçiriyor), main.cpp
   başka lane'in.
3. **`cli/main.cpp:2086 / 2161 / 1952`** nlohmann ile ayrıştırıyor → L10-06'nın *etki* ucu.
   Ayrıca `cli/main.cpp:2242-2248`'in **yanlış teşhisi** ("daemon unreachable") CLI'de düzeltilmeli.
4. **`gui/daemon_comm.inl:403-470`** elle JSON tarayıcısı — L10-06'dan bağışık ama kendi
   ayrı riski var (escape'li string'lerde `json_skip_string` doğru mu? ayrı denetim gerekir).
5. **`daemon.cpp:2320-2547` (`flush_motion`) ve `:2547-2893` (`process_device`)** — L10-05'i
   doğrulamak için *okudum* (kilit sayımı), değiştirmedim. Hot-path'in kendi denetimi ayrı lane'de.
6. **`scripts/rawaccel.service` / `scripts/99-rawaccel.rules`** L10-01/L10-02'nin çalışma zamanı
   koşulları; packaging lane'inin.

---

**TEMSİL SINIRI** (neyi doğrulayamadım ve neden):
1. **Gerçek daemon'ı ÇALIŞTIRAMADIM.** `id` → `uid=1000(a)`, `sudo -n true` → "a password is
   required". Kök yok ⇒ `/dev/uinput` grab yok, soket `bind`/`chmod`/`chown` gerçek dosya
   üzerinde denenemedi, **`SO_PEERCRED` reddi canlı olarak gözlemlenemedi.** L10-01/02/04/05'in
   sokut izni kanıtı **kod + unit dosyası + ayrı soket semantiği ölçümü**; canlı uçtan ucan saldırı
   değil. Bunu AJ1'in kendi komutuyla (köklü ortamda) yeniden ölçmesini öneriyorum.
2. **GUI derlemedim** (gtk4 dev başlıkları doğrulanmadı) → L10-06'daki GUI bağışıklığı
   `gui/daemon_comm.inl` okumasına dayanıyor, çalıştırılmış kanıta değil.
3. **L10-07'nin erişilemezliği** sabitlerden türetildi (`rawaccel-base.hpp:15`, `src/config.cpp:361-362`),
   canlı 8 kHz fareyle ölçülmedi. Bant aralığının dışında bir `dpi_factor`/`time_ms` yolu varsa
   (ör. `flush_motion` dışı bir telemetry yazarı) hesap geçerli olmaz — `grep` telemetry
   yazıcılarını listeledi, hepsi `flush_motion`/`apply_profile` içinde.
4. **`overflow bucket hatasının canlıda görülmesi** ölçülmedi: 500 µs üstü gecikme üreten bir
   makine/sürücü yok. Hata `lat_stats` matematiği üzerinde birebir gösterildi, üretim
   verisiyle teyit edilmedi.
5. **PS5.1/çalışma anı yok** — tüm mutasyonlar x86-64 Linux, `-O1` ile derlendi
   (kanonik `-O2` yerine; karşılaştırma aynı bayraklarla mutant/baseline arasında yapıldı ki
   fark mutanttan kaynaklanıyor olsun).
6. **`save_config` disk-dolu senaryosunu canlı üretmedim**; L10-11 kod yolu + kendi yorumuyla
   (`push_config` `:592-594` "not saved") kanıtlı, ama `ok:true` yanıtını gözlemleyen bir
   istemci testi yazmadım.
7. **`response` boyutu ölçümü** benim `status_json` emisyon kalıbımı kullandı (gerçek kod
   satırlarının kopyası). `mouse_device` alanlarından biri beklenmedik uzunsa (ör. 256
   karakterlik `device_id`) B/device 482'yi biraz aşabilir — hata payı 512-fare eşiğinde,
   yani bu bile sonucu değiştirmiyor.

---

**AJ1'E ÖNERİ (yeniden ölçülsün diye, ölçüm komutlarıyla):**
- `L10-01`'i **köklü** ortamda doğrula: daemon'ı başlat, `ls -l $sock`, `sudo -u <input-grubu-dışı>
  python3 -c 'socket connect'` → `EACCES` beklenir. Benim ölçtüğüm hâliyle sokut bir "herkes erişir"
  bulgusu **değil**.
- `L10-09`/`L10-10` sessiz yeşilleri: mutantları `/tmp` kopya üzerinde tekrarla; **pozitif
  kontrolü atlamadan** (aksi halde "yeşil"in harness ölü olduğuyla karışır). `M3` pozitif
  kontrolü çıktısı burada: `2 FAIL, exit 1`.
- `L10-10` dokümantasyon iddiası `--sayi` kapsamı dışında; AGENTS.md:392'yi ya testle kapat ya da
  listeden düşür.