# Bug Hata Raporları — Linux RawAccel

Analiz tarihi: 2026-09-11
Kapsam: `/home/a/Masaüstü/Linux-Raw-Accel-main` (RawAccel Linux v0.6.4)
Yöntem: Baştan sona (start-to-end) 5 ayrı detaylı analiz turu. Her tur farklı bir uzmanlık açısıyla tüm kaynak kod taranmıştır.
Durum: 2026-09-11 düzeltme seanslarında ele alınan bulgular (D-1..D-9, C-1..C-10, CR-1, H-1..H-3, M-1..M-8, R1-01..R1-08, R2-01, R2-04, L-2, L-5, R5-S-4/5) fixed olarak listeden çıkarılmıştır. 2026-09-11 ileri seansında ek olarak düzeltilenler: R5-S-1, R1-06, R2-02, R2-03, R2-05, R2-06, R2-07, R3-NEW-1, R3-NEW-2 (belgeli sapma + oracle satırı), R3-NEW-3, M-5b, M-6, R4 L-1, R4 L-4, R4 L-8, R4 L-10. Aşağıdaki maddeler halen açık veya bilinçli/belgeli tasarımdır.

---

# TUR 1 — Daemon / Olay Döngüsü / Eşzamanlılık / Hot-Path

Odak: `daemon/*`, `src/*`, `include/*`, `cli/main.cpp`, olay döngüsü, seqlock/telemetry, sinyal işleme, hot-plug, uinput yazımı, SYN_DROPPED, FD/hafıza sızıntıları, gecikme bütçesi.

**R1-09 · LOW · `lat_stats` içinde olay başına mutex**
- Konum: `daemon/lat_stats.hpp` (record)
- Kategori: Performans
- Açıklama: Hot path'te (olay başına) `lat_stats::record()` mutex alıyor; AGENTS.md bu tasarımı doğruluyor (R13 move-locking ayrı). Perf bütçesi için değerlendirilmeli.

**R1-11 · LOW · IPC döngüsünde senkron config kaydı**
- Konum: `daemon.cpp` (IPC thread config save)
- Kategori: Performans / bloklama
- Açıklama: `save_config` IPC thread üzerinde senkron çalışıyor; disk yavaşlığı IPC gecikmesi olarak yansır.
- Öneri: Async/worker save.

**R1-12 · LOW · Tamsayı bölmeli poll-rate geri dönüşü**
- Konum: `daemon`/`include` poll-rate hesaplama
- Kategori: Doğruluk (arithmetic)
- Açıklama: Poll-rate tamsayı bölmeyle hesaplanıyor; düşük DPI/poll kombinasyonlarında hassasiyet kaybı.
- Öneri: Ondalık bölme + ortalama.

**R1-13 · LOW · Başlat-durdur yarışı**
- Konum: `daemon/main.cpp` sinyal + başlatma
- Kategori: Yarış (race)
- Açıklama: Başlatma tamamlanmadan durdurma sinyali gelirse yarış; PID dosyası/thread temizliği belirsiz.

**R1-14 · LOW · PID-reuse canlılık kontrolü**
- Konum: `daemon`/`cli` (pid dosyası kontrolü)
- Kategori: Güvenlik / mantık
- Açıklama: PID reuse senaryosunda canlılık kontrolü yanıltıcı olabilir; `/proc/<pid>/comm` vs. kullanılmalı.

**R1-15 · INFO · `is_physical_mouse` üçüncü parti sanal fareyi kapar**
- Konum: `daemon.cpp` (cihaz filtreleme)
- Kategori: Politika
- Açıklama: REL_X+REL_Y + fiziksel bayrağa göre filtreleme, bazı sanal/kaydırıcı cihazları yanlışlıkla fiziksel fare sayabilir.
- Not: AGENTS.md "Known Limitations" içinde bilinçli politika olarak belgeli.

**R1-16 · INFO · Doğrulanan güvenli alanlar (bulgu değil)**
- FD/hafıza sızıntısı bulunamadı; sub-pixel accumulation doğrulandı; seqlock protokolü ve kilit sıralaması uygun.

---

# TUR 2 — GUI / IPC / Yerelleştirme / Cihaz Yönetimi

Odak: `gui/*.inl`, `gui/main.cpp`, `app_state.hpp`, GUI-daemon IPC, inotify hot-plug, profil CRUD, grafik/LUT, fare testi, HID++ paneli, dil değiştirme.

**R2-08 · INFO · HID++ panel thread sızıntısı + kalıntı yarış**
- Konum: `gui/hidpp_panel.inl:473-491,396-398`
- Kategori: Kaynak / yarış
- Açıklama: 1000 ms `hw_notification_tick` her tick'te hiçbir şey değişmese bile kısa ömürlü GThread doğuruyor; A1-17'deki idle-callback `S->hidpp_devs[r->idx]` yeniden okuması, tarama idle'ı yenilenmiş vektörde eski index'i yeniden okursa kalıntı riski taşır.
- Öneri: Tarama sonucunu version-tag ile işaretle, notify idle uyuşmazlıkta vazgeçsin; cihaz seçili değilse tick'i atla.

---

# TUR 3 — Matematik / Hızlandırma Algoritmaları / Config Serileştirme

Odak: `include/accel-*.hpp`, `include/rawaccel.hpp`, config sanite etme, JSON, presets, cap/legacy/gain varyantları, LUT, EMA, edge case'ler (NaN/Inf/0/sıfıra bölme), orakl (oracle) kapsamı.

**R3-NEW-4 · INFO · `modify()`'de `time <= 0` erken dönüşü ölçeklenmemiş 1:1 passthrough üretir**
- Konum: `include/rawaccel.hpp:242`
- Kategori: API davranış farkı
- Açıklama: `!isfinite(time) || time <= 0` → `in` değişmeden döner; referans formülü time→0+ için `ips_factor → ∞` → gain cap'te. Daemon `DEFAULT_TIME_MIN` klamplaması yaptığından (`daemon.cpp:1393`) gerçekte ulaşılamaz; yalnız doğrudan API/fuzz erişebilir. `known_deviations.txt`'te yok.
- Öneri: Sapmayı listeye ekle veya `modify()` içinde time→min normalize et.

**R3-NEW-5 · INFO · Reload sonrası telemetri kısa süre bayat `speed_ips` sergileyebilir**
- Konum: `daemon.cpp` `status_json` (telemetry seqlock)
- Kategori: Tutarlılık (kozmetik)
- Açıklama: Reload profil yeniden uygular ama telemetri atomiklerini sıfırlamaz; bir IPC poll'un `telem_gain`'i eski eğriyi yansıtabilir. Seqlock örnek-içi tutarlılığı garanti eder.
- Öneri: Kabul edilebilir ise gerekmez.

**R3-NEW-6 · INFO · `cap_mode` alias'ları el ile yazılmış JSON'da reddediliyor**
- Konum: `cli/main.cpp:946` (önalias eşlemesi) vs `src/config.cpp` profile_from_json
- Kategori: Tutarlılık
- Açıklama: CLI "in_out"/"both", "off"/"none" aliaslarını canonical'e çevirip kaydeder; el ile yazılmış JSON'a bu stringler gelirse yükleme reddediyor. Normal CLI akışları güvenli.
- Öneri: Belgele; düzeltme gerekmez.

**R3 · Önceden belgeli ve hâlâ açık (doğrulandı)**
- **F-4 · INFO** — Canlı cihaz eşleşmeyen reload tam teardown+setup yapar (~100-150 ms mouse drop; sanal cihaz yeniden oluşturulur).
- **F-5 · INFO** — Üst düzey `use_raw_input` bayrağı saklanıyor ama etkin değil (raw profil bazlı).

**R3 · Doğrulanan düzeltmeler (2026-09-11 seanslarında fixed, listeden çıkarıldı)**
- BUG-02 (power üs tabanı), P155 io-dejenere korumalar, P150 virgül→nokta JSON, BUG-NEW-81 epoll streak→request_stop, D-1 config_path_ mutex, D-2/D-3 epoll_ctl DEL kontrolleri, D-5 seqlock 64-spin, D-6 PID stale-clear liveness, D-7 IPC bind EADDRINUSE, D-8 IPC deadline yanıtı, D-9 EINTR retry, C-1 aktif profil senkronu, C-2 import 256 isim, C-3 distance_mode max eşitlemesi, C-4 IPC 64 KiB tavan kaldırma (16 MB guard), C-5 validate 256, C-6 safe_save, C-7 boş profil mesajı, C-8 boş-LUT lookup uyarısı, C-9 config kaydedilemez ise ERROR, C-10 import boyut sınırı, BUG-20 boş/çift isim, N-atomik save (fsync dosya+ebeveyn), M-BUG-14 perm koruması, F-1 (HID++ identify bütçesi — kIdentifyBudget 20s + streak 3, ileri seansta teyit).
- İleri seans (R3-NEW serisi): R3-NEW-1 (cmd_import çoklu format: nesne / `{"profiles":[...]}` / satır akışı / array + batch içi isim kapısı), R3-NEW-2 (oracle grid'e `power_tinyexp_floor` ep=5e-4 + known_deviations'a 23 satır — 1071 satır / 68 sapma RESULT OK), R3-NEW-3 (`apply_profile` sonunda `last_time_ms` yeniden çapalanır — raw→accel geçişte ilk olay gain≈1 kaldırıldı).

---

# TUR 4 — Scripts / setup.sh / systemd / udev / polkit / Paketleme / CI

Odak: shell betikleri, systemd hardening, udev, polkit, packaging (PKGBUILD), CMake, KDE fix, CI, sürüm tutarlılığı, çapraz-dağıtım.

**R4 · LOW bulgular**
- **L-1** `tests/oracle/run_oracle.sh` — `--tolerance abc` / geçersiz env `TOL` artık parse SONRASI doğrulanıyor; python traceback yerine çıkış 2 ("Hata: TOL geçerli bir sayı değil"). FIXED.
- **L-3** `scripts/build.sh:64-68` — FORTIFY kontrolü yalnız derleyici *varsayılan* makrolarını (`$CXX -dM -E`) sınıyor, gerçek env `CFLAGS`'i değil; "CMakeLists'i yansıtır" yorumu abartılı. Şu an zararsız ama yanlış garanti.
- **L-4** `scripts/bench_hotpath.sh` — sabit `g++ -march=native`; artık `$CXX` onurlandırılıyor ve `RAWACCEL_PORTABLE=1` `-march=native`'i kapatıyor (build.sh ile ayna). FIXED.
- **L-6** `setup.sh:63-64` — pacman-conflict prompt `read -r _ || true`; kapalı stdin ile kullanıcıya sorulmaz, bilinen co-existence riskiyle kurulum ilerler.
- **L-7** `setup.sh:261` — backup adı 1-sn granülerliği (`date +%Y%m%d-%H%M%S`); aynı saniyede yeniden kurulum önceki backup'ı sessizce ezer.
- **L-8** `scripts/uninstall.sh` — artık `rawaccel-gui` de durduruluyor (daemon ile birlikte TERM/KILL) ve kwinrc temizliği hata çıktısını gizlemiyor. FIXED.
- **L-9** `scripts/bug_watch.sh:20,32` — `STATE_DIR` `/tmp/rawaccel-bug-watch`'e (başka kullanıcı önceden oluşturabilir) ve `printf >> $LOGFILE` — çok-kullanıcılı log sahtelemesi/symlink yüzeyi. Per-user dir veya `install -d -m 700`.
- **L-10** `ci.yml` — uyarı kapısı artık yalnız derleyici/linker tanı biçimlerini (`FILE:LINE[:COL]: warning|error`, `ld:/collect2`) arıyor; masum "warning:|error:" içeren çıktılar false-fail yapamaz. FIXED.
- **L-11** `gui/widgets_sync.inl` — pkexec çocuk çıkış kodu artık izleniyor (bkz. R2-06), 126/127/sinyal durumları status bar'da raporlanır. FIXED.

**R4 · Doğrulananlar (bulgu değil)**
- Sürüm tutarlılığı: `rawaccel-base.hpp:9 "0.6.4"` == `CMakeLists.txt` VERSION == `PKGBUILD:14` == `.SRCINFO` == çalışan binary'ler.
- CI perf-gate regex doğru (`$`-ankra, SUMMARY satırları); median-of-3 sağlam.
- Systemd hardening güçlü: `PrivateNetwork`, `SystemCallArchitectures=native`, `ProtectProc`, `UMask=0077`, `ReadWritePaths` tutarlı. (M-7: `CAP_DAC_READ_SEARCH`'ün bounding set'ten çıkarılmasıyla birlikte.)
- pacman dalı `-S` (kısmi yükseltme yok) + uyarı kapısı var (P121/P131).
- Dep paritesi üç dalda AGENTS.md politikasıyla eşleşiyor (apt qt6 sürüm kapısı M-6'da düzeltildi).
- udev kuralları bilinçli güven sınırı (F-2 INFO); setup.sh kullanıcıyı `input` grubuna ekler.

---

# TUR 5 — Güvenlik / Kaynak Sızıntıları / Erişim Kontrolü / Güven Kenarlıkları

Odak: ayrıcalık yükseltme, path traversal, symlink saldırıları, TOCTOU, dosya izinleri, root daemon config doğrulama, IPC kimlik doğrulaması (locals non-root inject edebilir mi?), socket izinleri, PID dosyası, sinyal sahteleme, FD/hafıza/mutex/thread/inotify sızıntıları, yok sayılan hata dönüşleri.

**R5-S-2 · MEDIUM · F-1 HID++ identify bütçesi iddiası doğrulandı (2026-09-11 ileri seans)**
- Konum: `src/logitech_hidpp.cpp` (FEATURE_SET / device_info / GetDpiList döngüleri)
- Kategori: Bloklama / tutarlılık
- Açıklama: `kIdentifyBudget` 20 s + `kIdentifyTimeoutStreak` 3 sabitleri F-1/BUG-01 için yerinde; tüm döngülerde `deadline` kontrolü VE ardışık timeout kırılması teyit edildi. F-1 artık "fixed" — listeden çıkarıldı.

**R5-S-6 · INFO · IPC kimlik doğrulaması yok — güven sınırı `input` grubu**
- Konum: `daemon/daemon.cpp` (IPC listening soketi, chmod 0660 root:input; 0600 fallback)
- Kategori: Güven sınırı (tasarım)
- Açıklama: Komut gövdeleri: 1-byte okumalar, 256-byte komut tavanı, `set_config` gövdesi ≤1 MiB, 10 sn/5 sn deadline, newline-terminated. Kimlik doğrulaması (PIN/cred) yok; `input` grubundaki herhangi bir kullanıcı config gönderebilir/servisi durdurabilir. Bu, AGENTS.md'nin belgeli güven sınırıdır (gruplar zaten evdev hidraw erişimine sahip olduğundan kabul edilebilir).
- Not: `SO_SNDTIMEO` + takılı peer'da drop var (`daemon.cpp:2037-2056`).

**R5-S-7 · INFO · Config save zinciri doğrulandı (bulgu değil)**
- Konum: `src/config.cpp` (atomic save)
- Kategori: Doğrulama
- Açıklama: `O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC` + pid-suffix tmp + fsync + rename + parent-fsync + timestamped backup; CLI `safe_save` `.bak` rotasyonlu. TOCTOU/symlink saldırılarına karşı sağlam doğrulandı.

**R5-S-8 · INFO · FD/hafıza/kilit/inotify sızıntısı bulunamadı (doğrulama)**
- Kategori: Doğrulama
- Açıklama: Daemon (evdev/uinput/inotify fd'leri), GUI (inotify watch, GThread'ler) ve CLI tüm incelenen yollarda sızıntı görülmedi. `lat_stats` move-locking (R13) ve seqlock telemetry tutarlı.

**R5-S-9 · INFO · Root daemon config path doğrulaması sınırı**
- Konum: `daemon/main.cpp:145-191` `validate_config_path()`
- Kategori: Güvenlik/zayıf
- Açıklama: `/proc/ /sys/ /dev/` önekleri YALNIZCA hedef dosya yokken reddediliyor; var olan ≤4 MB `.json` uzantılı düzenli dosya her yoldan root daemon config'i olarak kabul edilir. Ancak erişim polkit action'ı dorman (kaldırılmış) olduğundan bugün yalnız root/GUI-pkexec ile ulaşılabilir. (F-2 ile ilişkili.)

**R5 · Doğrulananlar (bulgu değil)**
- F-3/C-4 (IPC 64 KiB tavanı) 2026-09-11'de düzeltildi (sınırsız okuma + 16 MB guard).
- D-6 (PID stale-clear liveness re-check) ve D-7 (IPC bind EADDRINUSE TOCTOU penceresi) 2026-09-11'de düzeltildi.
- C-1..C-10 ve D-1..D-9 serilerinin güncel ağaçtaki durumu tur 1-4 bulgularıyla çapraz kontrol edildi; 2026-09-11 seanslarında fixed listesine alındı.

---

# ÖZET / ÖNCELİK SIRASI (güncel açık listesi)

**MEDIUM (önem sırası):** kalan acil MEDIUM yok (R5-S-1, M-6, R2-02, R1-06, R2-05, M-5b, F-1 2026-09-11 ileri seansında düzeltildi/teyit edildi).

**LOW/INFO:** R1-11…R1-14, R2-08, R3-NEW-4…6, R4 L-3, L-6, L-7, L-9, R5-S-6…R5-S-9 (R5-S-2: F-1 doğrulandı).

**Bilinçli / belgeli (düzeltme gerektirmez):** R1-09 (lat_stats mutex), R1-15 (is_physical_mouse politikası), R5-S-6 (IPC güven sınırı), F-4/F-5, R3-NEW-4/5/6.

**Notlar:**
- Ağaç aktif düzenleme altında; satır numaraları 2026-09-11 son haliyle kısmen eski olabilir.
- 2026-09-11 seanslarında düzeltilen tüm maddeler bu rapordan tamamen çıkarılmıştır; bunlar git geçmişinde ve çalıştırılan testlerde (33768 unit + ASan + TR coverage + oracle 1071/68) izlenebilir.
- İleri seans düzeltme kümesi (bu derleme): R5-S-1 (PID readlink), R1-06 (SYN_DROPPED yalnız SYN_REPORT ile temizlenir + T24 testi), R2-02 (kwinrc marker), R2-03 (unplugged girişi), R2-05 (üzerine yazma onayı), R2-06/R4-L-11 (pkexec çocuk çıkışı raporu), R2-07 (bayat etiketler), R3-NEW-1/2/3, M-5b (bug_watch retry), M-6 (apt qt6 fallback), R4 L-1/4/8/10.
- **Müstakil denetim turu (ürün kodu + betikler, 2026-09-11):** ürün kodunda tetiklenebilir hata yok (daemon, GUI, CLI, config, accel, HID++ doğrulandı); GUI'de yalnız latent JSON-anahtar alt-string eşleşmesi not edildi (mevcut şemada tetiklenmez). Düzeltilen betik bulguları: Yeni-R1 (`src/config.cpp:785` `profile_to_json` compact `.dump()` — çoklu profil `export > f.json && import f.json` round-trip artık çalışıyor), Yeni-R2 (`tests/run_tr_coverage.sh` sabit `/tmp/tr_coverage` → `mktemp`+trap — symlink race/TOCTOU), Yeni-L1 (`setup.sh` `/home/*` ve `/run/user/*` döngülerine scoped `nullglob`), Yeni-L2 (`scripts/uninstall.sh` `/run/user/*` nullglob + `set -o pipefail`), Yeni-L3 (`scripts/build.sh` `-I$ROOT/include` quote — boşluklu yollarda build kırılması).

---

# TUR 6 — Derinlemesine Yeniden Analiz (2026-09-11)
# Polling Rate / Sensör Tarama / Performans / Thread Safety / Yeni Bulgular

Analiz tarihi: 2026-09-11 (ikinci tur)
Kapsam: Tüm kaynak kodları (daemon, GUI, config, accel algoritmaları, scripts)
Yöntem: Polling rate mekanizması, hot-path latansı, thread yarışları, algısal hatalar
Durum: Yeni bulgular eklendi (PERF-1..PERF-8, POLL-1..POLL-5, TS-1..TS-4, ALG-1..ALG-6, CFG-1..CFG-3, GUI-1..GUI-4, OPT-1..OPT-5)

---

## POLLING RATE / SENSÖR TARAMA ANALİZİ (EN KRİTİK BÖLÜM)

### POLL-1 · HIGH · Mouse 2000 Hz ayarlı ama program daha düşük Hz'de tarama yapıyor olabilir
- Konum: `daemon/daemon.cpp` detect_polling_rate() ve epoll_wait()
- Kategori: Polling rate algılama
- **Açıklama:** Mouse 2000 Hz'e ayarlı olduğunda programın gerçek tarama hızı konusundabirden fazla sorun var:
  1. `detect_polling_rate()` yalnızca sysfs `bInterval` ve `speed` dosyalarından hız tahmini yapıyor — **gerçek evdev olaylarını zaman damgalayarak_measure etmiyor.** SYSFS trick'i ancak USB hızını gösteriyor, mouse'un kendi ayarladığı Ms cinsinden interval'ı göstermiyor.
  2. Birçok gaming mouse (Razer, Logitech, SteelSeries) USB descriptor'da `bInterval=1` (1000 Hz) gösterirken kendi yazılımlarıyla 2000/4000/8000 Hz'e ayarlanabilir. Bu durumda `detect_polling_rate()` **her zaman 1000 Hz döndürür**.
  3. `epoll_wait` 10 ms timeout ile çalışıyor (satır 1239). 2000 Hz = 0.5 ms aralıkla gelen olaylar için bile epoll timeout sorun değil (level-triggered), ancak **timeout value programın tarama hızını belirlemiyor** — olaylar evdev ring buffer'dan okunuyor.
  4. **Kritik:** Mouse 2000 Hz ise ve `poll_rate` config'de 1000 olarak ayarlıysa, `ips_factor = dpi_factor / time_ms` hesaplaması gerçek zamana dayalı olduğu için hızlandırılmış olaylar **daha yüksek IPS** hesaplar — bu doğru. Ama `detect_polling_rate()` sysfs'ten 1000 Hz okursa GUI'de看到 wrong value ve kullanıcı yanıltılır.
- **Öneri:** Gerçek polling rate'i time interval medyanı ile ölç (kernel'den gelen olay timestamps). sysfs bInterval'ı fallback olarak kullan ama GUI'de "Detected (estimated)" olarak göster.

### POLL-2 · MEDIUM · bInterval high-speed exponent dönüşümü tam doğrulanmamış
- Konum: `daemon/daemon.cpp` detect_polling_rate() satır 203-234
- Kategori: Matematiksel hesaplama
- **Açıklama:** USB high-speed interrupt endpoint bInterval dönüşümü:
  ```cpp
  const double interval_us = 125.0 * (1u << (binterval - 1));
  rate_hz = static_cast<int>(1000000.0 / interval_us);
  ```
  Bu formül USB 2.0 spec'a göre doğru (`2^(bInterval-1) × 125µs`), ancak:
  - `binterval=1` → 125µs → 8000 Hz (doğru)
  - `binterval=2` → 250µs → 4000 Hz (doğru)  
  - `binterval=3` → 500µs → 2000 Hz (doğru)
  - `binterval=4` → 1000µs → 1000 Hz (doğru)
  - Ama **USB 3.x SuperSpeed** endpoint'lerde bInterval 125µs microframe'lerde değil, farklı-timescale'da çalışıyor. Kod `speed >= 480`'i tek bir "high-speed" kategorisinde birleştiriyor — SuperSpeed cihazlar için yanlış rate döndürebilir (çok nadir mouse durumu).
  - **USB low-speed/full-speed bInterval doğrudan 1ms frame cinsinden** — bu doğru ele alınmış.
- **Öneri:** SuperSpeed (10 Gbps+) için ayrı parametre gerekli (nadir, ama mevcut). Ya da SuperSpeed cihazlar için “detected rate unavailable” döndür.

### POLL-3 · LOW · `speed` sysfs okuması tampon taşması riski
- Konum: `daemon/daemon.cpp` sysfs_read_usb_speed() satır 155-170
- Kategori: Buffer overflow
- **Açıklama:** `char buf[64]` 64 byte'lık bir buffer ile `fread(buf, 1, sizeof(buf)-1, f)` yapılıyor. SYSFS dosyaları tipik olarak çok kısa ("12", "480", "5000" gibi), ancak norminal olarak `/sys/` dosyaları `(void *)` buffer'dan okunabilir. Bu durumda `fread` buffer'ı doldurur, `buf[63]` null terminator her zaman korunuyor — buffer overflow mümkün değil. Ama **fread hatası kontrol edilmiyor**: `n = 0` durumu kontrol ediliyor, ama `fread` kısmi okuma yaparsa (0 < n < strlen) `strtod` hala çalışır çünkü null-terminator korunuyor.
- **Not:** Gerçekçi risk düşük, ama `fread` return value'nun `n < 1` kontrolü zaten var.

### POLL-4 · LOW · `event_num_from_path` realpath felaket senaryosu
- Konum: `daemon/daemon.cpp` satır 110-126
- Kategori: Doğruluk
- **Açıklama:** `realpath()` başarısız olursa (dangling symlink, EACCES), `path.c_str()` kullanılıyor — bu durumda "event" substring'i `/dev/input/by-id/...` yolunda bulunamayabilir (örn: `usb-Logitech_Gaming_Mouse_X56_123456789-event-mouse`). `strstr(p, "event")` hala eşleşir çünkü "event" by-id adında var, ama parse edilen numara anlamsız olur.
- **Not:** `return -1` fallback'i doğru çalışır. Düşük risk.

### POLL-5 · INFO · `detect_dpi_sysfs` low fallback mekanizması eksik
- Konum: `daemon/daemon.cpp` satır 241-257
- Kategori: Eksik özellik
- **Açıklama:** `detect_dpi_sysfs()` yalnızca `/sys/class/input/eventN/device/resolution` okuyor. Çoğu mouse bu bilgiyi sunmuyor (0 döndürür). HID++ (Logitech) cihazlardan DPI okumak ayrı bir mekanizma gerektirir — `logitech_hidpp.cpp`'de `get_battery_status` var ama `get_dpi_list`/`get_current_dpi` henüzentegre edilmemiş.
- **Öneri:** `detect_dpi_sysfs` başarısız olursa HID++ device_info'dan DPI listesi okunabilir (mevcut `logitech_hidpp.hpp` içinde `DpiList` feature var).

---

## PERFORMANS ANALİZİ

### PERF-1 · MEDIUM · Hot-path'te her olay iki kez `clock_gettime` çağırıyor
- Konum: `daemon/daemon.cpp` process_device() ve flush_motion()
- Kategori: Hot-path latansı
- **Açıklama:** Her motion olayı için:
  1. `batch_start_ns = now_ns()` → `clock_gettime(CLOCK_MONOTONIC_RAW)` (process_device girişinde)
  2. `t_now = now_ns()` → `clock_gettime(CLOCK_MONOTONIC_RAW)` (flush_motion içinde)
  
  Toplam: **2× clock_gettime per motion batch** (AGENTS.md'de de belgelenmiş: "canonical: hot-path syscall = 3, i.e. 2×clock_gettime + 1 batched write"). 
  `clock_gettime(CLOCK_MONOTONIC_RAW)` vDSO üzerinden ~20-50ns sürüyor (x86_64). 8000 Hz'de 125µs aralıkla, bu ~0.04-0.08% bütçe. **Kabul edilebilir.**
  Ama `lat_stats::record()` her olayda **mutex alıyor** (lat_stats.hpp satır 76). Mutex unlock ~10-20ns, yani ~0.016% ek bütçe. 8000 Hz'de bile makul.

### PERF-2 · LOW · `append_fixed` JSON üretimi ~33% daha yavaş olabilir
- Konum: `daemon/daemon.cpp` append_fixed() ve status_json()
- Kategori: IPC performansı
- **Açıklama:** `status_json()` olay başına çağrılmıyor (yalnızca istek anında), ancak GUI 250ms'de bir sorguluyor (mouse_test.inl). Yalnızca ~10 cihaz varsa JSON ~2KB, 250ms'de bir ~8KB/s. **Önemli değil.**
  Ama `push_config()` `app_config_to_json(cfg) == app_config_to_json(config_)` ile no-op guard yapıyor — bu her IPC set_config'te **iki tam JSON serializasyonu** + string karşılaştırma yapıyor. Config büyükse (LUT verileri) maliyet artar.
- **Öneri:** no-op guard'ı hash-tabanlı yap (pre-computed config hash) veya son JSON string'ini sakla.

### PERF-3 · LOW · `lat_stats` histogram满了`(hist[BUCKETS] = {})` her `snapshot_and_reset`'te 8KB sıfırlama
- Konum: `daemon/lat_stats.hpp` satır 29, 170
- Kategori: Performans
- **Açıklama:** `uint64_t hist[BUCKETS] = {};` → 1000 × 8 = 8000 byte. `snapshot_and_reset()` her çağrıldığında `std::fill(hist, hist+BUCKETS, 0ULL)` 8KB sıfırlıyor. SIGUSR1 ile çağrıldığında sorun değil, ama IPC "latency" komutuylaher istekte çağrılsa (hassas değil) maliyet artar.
- **Not:** Gerçekçi senaryoda latency dump çok nadir, önemli değil.

### PERF-4 · INFO · `find_mice()` her hot-plug taramasında tüm `/dev/input/event*` dosyalarını açıyor
- Konum: `daemon/daemon.cpp` find_mice() satır 373-392
- Kategori: Hot-plug performansı
- **Açıklama:** `find_mice()` her çağrıldığında `glob("/dev/input/event*")` ile tüm event dosyalarını listeler, her birini `open()` + `is_physical_mouse()` + `close()` yapıyor. Sistemde 50+ input cihazı varsa (klavye, touchpad, tablet, vs.) her hot-plug taraması ~50× open/close yapıyor. Her open/close ~5-10µs, toplam ~250-500µs.
  **Epoll 10ms timeout** ile毎回>`run_loop` çağrıldığında hot-plug flag yoksa tarama yapılmıyor. Ama `pending_hotplug_` olduğunda veya `devices_empty` durumundaher 2sn'de bir tarama yapılıyor.
- **Öneri:** inotify IN_CREATE/IN_DELETE event'lerinden sadece eklenen/çıkarılan dosyaları işleyerek`find_mice()`'ı tam tarama yerine incremental yap.

### PERF-5 · INFO · `push_config` no-op guard iki tam JSON serialization gerektiriyor
- Konum: `daemon/daemon.cpp` push_config() satır 543
- Kategori: Performans
- **Açıklama:** `app_config_to_json(cfg) == app_config_to_json(config_)` karşılaştırması her IPC config push'ta iki kez tüm config'i JSON'a dönüştürüyor + string karşılaştırması yapıyor. Config 100KB JSON (büyük LUT'lu) ise her push ~200KB string üretimi + ~200KB karşılaştırma.
- **Öneri:** Basit bir hash (std::hash<string>) kullan veya config变更 flag ile\pare

---

## THREAD SAFETY / EŞZAMANLILIK ANALİZİ

### TS-1 · MEDIUM · `apply_hidpp_battery` ile `apply_new_config` arası cihazvektör yarışı
- Konum: `daemon/daemon.cpp` apply_hidpp_battery() satır 1042 vs apply_new_config() satır 838
- Kategori: Thread safety
- **Açıklama:** `apply_hidpp_battery()` (hidpp thread) `devices_mutex_` altında `devices_` vektörünü iterate ediyor. `apply_new_config()` (loop thread) aynı mutex altında `devices_`'i iterate edip `apply_profile()` çağrıyor. İkisi de `devices_mutex_` kullandığından **birbirine tampon yarışı yok**. Ancak:
  - `apply_profile()` `apply_new_config()` içinde çağrılıyor ve `apply_profile()` `dev.settings`, `dev.sp`, `dev.mod`, `dev.dpi_factor`, `dev.remainder_x/y` yazıyor.
  - `apply_hidpp_battery()` yalnızca `dev.detected_battery` yazıyor (int).
  - Aynı `mouse_device` üzerinde farklı field'lara yazıldığından **data race yok** (farklı member'lara yazma tanımsız davranış oluşturmayabilir — C++ standard'a göre farklı member'lara paralel erişim tanımlı, sadece aynısına eş zamanlı write race).
- **Durum:** Doğrulandı, thread safety uygun.

### TS-2 · LOW · `devices_mutex_` altında `apply_profile()` çağrılması Potansiyel deadlock
- Konum: `daemon/daemon.cpp` apply_new_config() satır 838-848
- Kategori: Deadlock riski
- **Açıklama:** `apply_new_config()` `devices_mutex_` altında `apply_profile()` çağrıyor. `apply_profile()` `dev.sp.init()` çağrıyor ki bu `speed_processor::init()` mutex almıyor — sadece EMA smoother'ları sıfırlıyor. `init_settings()` de sadece computation yapıyor. **Deadlock riski yok.**
  Ama `status_json()` `devices_mutex_` altında `dev.lat.copy()` çağrıyor ki bu `lat.mtx` alıyor. `dump_latency_stats()` da `devices_mutex_` altında `dev.lat.snapshot_and_reset()` çağrıyor — aynı lock order: `devices_mutex_ → lat.mtx`. **Deadlock yok.**

### TS-3 · LOW · IPC thread `push_config` → `save_config` disk I/O阻塞
- Konum: `daemon/daemon.cpp` push_config() satır 551 ve ipc_serve_loop()
- Kategori: Performans / blocking
- **Açıklama:** `save_config()` disk I/O yapıyor (fsync dahil). IPC thread üzerinde çalıştığından, disk yavaşsa (HDD, NFS) bu _serial accept loop_'u blokluyor — diğer IPC istekleri bekler. Ama SO_RCVTIMEO (2sn) + IPC_REQUEST_DEADLINE (10sn) ile sınırlı.
- **Öneri:** Config save'i background thread'e taşı (fire-and-forget, hata log'a).

### TS-4 · INFO · `ipc_serve_loop` poll()-dan accept4() arasındaki TOCTOU
- Konum: `daemon/daemon.cpp` ipc_serve_loop() satır 2022-2049
- Kategori: Race condition
- **Açıklama:** `ipc_sock_fd_.load()` ile local `fd` kopyası alınıyor. `stop_ipc_server()` bu fd'yi `-1` exchange ile kapatıyor. `poll()` return ettikten sonra, `accept4(fd)` çağrısına kadar fd kapanmış olabilir — `accept4` EBADF ile başarısız olur. Bu beklenen bir durum (döngü `ipc_running_` kontrol eder). Ama:
  - `accept4()` başarısızsa `continue` ile tekrar poll'a dönüyor.
  - `ipc_running_` false olduğunda döngü kırılıyor.
- **Durum:** Doğru çalışır, edge case ele alınmış.

---

## ALGORİTMA / MATEMATİK HATALARI

### ALG-1 · LOW · Classic gain mode `constant = 0` olduğunda C1 sürekliliği kırılıyor
- Konum: `include/accel-classic.hpp` init_gain() satır 134-142
- Kategori: Algoritmik
- **Açıklama:** ORTA-BUG-ALG-03`te açıklandığı gibi, `base_fn(cap_x, accel_raised, args)` overflow guard tetiklendiğinde `cap_y = 0, constant = 0` ayarlanıyor. Bu durumda gain eğrisi `cap_x`'de C1 kırılıyor: sol taraf `base_fn(x)` (continuously differentiable), sağ taraf `1.0` (sabit). Bu escinme (kink) normalde fark edilmez (çok dar hız bandında) ama hassas oyuncular hissedebilir.
- **Öneri:** cap_y=0 yerine asimptotik eğimi koruyan bir fallback kullan (örn: cap_y ≈ 1.01).

### ALG-2 · LOW · `power::base_fn_impl` `x <= offset.x` durumunda `offset.y` döndürüyor — instant jump
- Konum: `include/accel-power.hpp` base_fn_impl() satır 136-139
- Kategori: Algoritmik
- **Açıklama:** `x <= offset.x` → `offset.y` (çıkış ofseti). offset.x > 0 ise, hız offset.x'in altına düştüğünde gain `1 + offset.y/x` yerine doğrudan `offset.y`'ye atlıyor — bu C0 süreksizlik (ani sıçrama). offset.x=0 ise (varsayılan) sorun yok.
- **Not:** Varsayılan output_offset=0 olduğundan normalde ulaşılmaz.

### ALG-3 · LOW · `natural` gain mode `x < 1e-9` guard'ı çok dar
- Konum: `include/accel-natural.hpp` operator() satır 50
- Kategori: Edge case
- **Açıklama:** `if (x < 1e-9) return 1.0` — bu 0.001 IPS hızın altındaki olayları 1:1 olarak geçiriyor. Gerçek mouse sensörleri bu hızı nadir üretir, ama sub-pixel accumulation smooth hareketlerde `ips_factor` çok küçük hızlara düşebilir. Guard yeterli.
- **Not:** Doğrulandı, sorun yok.

### ALG-4 · LOW · `synchronous::fill_lut` integral hassasiyeti düşük
- Konum: `include/accel-synchronous.hpp` fill_lut() satır 107-133
- Kategori: Algoritmik / Hassasiyet
- **Açıklama:** `sigmoid_sum` trapezoidal integral kullanıyor ama `partitions = 2` sabit. Her segment 2 parçaya bölünüyor — low-resolution. Bu, gain LUT'unun hassasiyetini düşürüyor (özellikle steep sigmoid regions'da). Referans RawAccel muhtemelen daha yüksek partition count kullanıyor.
- **Öneri:** `partitions = 4` veya `16` yap; hesaplama maliyeti LUT doldurma sırasında (tek seferlik) çok artmaz.

### ALG-5 · INFO · `accel_args::operator==` epsilon karşılaştırması LUT float verisi için çok dar
- Konum: `include/rawaccel-base.hpp` satır 64-85
- Kategori: Doğruluk
- **Açıklama:** `LUT_EPSILON = 1e-5f` float epsilon. JSON'dan yükleme round-trip'inde `float → string → float` dönüşümü ~1e-6 hata üretebilir. `1e-5` eşiği dar kalabilir — iki farklı LUT相同的 ama round-trip farklılıkları nedeniyle `!=` döndürebilir, bu da no-op guard'ı atlatır.
- **Not:** Gerçekçi risk düşük (nlohmann JSON full precision kullanıyor), ama `1e-4f` daha güvenli olur.

### ALG-6 · INFO · `noaccel` modunda `lookup` ve `noaccel` arasındaki fark
- Konum: `include/accel-lookup.hpp` satır 73 ve `include/accel-noaccel.hpp`
- Kategori: Tasarım
- **Açıklama:** `lookup::operator()` boş LUT'ta `return 1.0` döndürüyor (aynı noaccel). Ama `noaccel` modunda `1.0` döndürülüyor, `lookup` modunda boş LUT'ta `1.0`. Bu doğru — ikisi de identite.

---

## CONFIG / VALIDASYON HATALARI

### CFG-1 · LOW · `migrate_config` herhangi bir version farkında `migrate_lookup_gain` çalışıyor
- Konum: `src/config.cpp` migrate_config() satır 892-930
- Kategori: Migration mantığı
- **Açıklama:** `version_lt(cfg.version, "0.4.0")` kontrolü, cfg.version boşsa da `true` döndürüyor (satır 877: "corrupt lhs → older than everything"). Bu doğru tasarım — eski config'leri migrate et. Ama:
  `cfg.version == RAWACCEL_VERSION` olduğunda (`migrate_config` hiç çalışmıyor — satır 896-898: early return). Bu durumda `migrate_lookup_gain` de çalışmıyor — doğru.
  Ama `cfg.version` "0.5.0" gibi mevcut version'dan büyükse (gelecek version'dan yüklenmiş), `version_lt("0.5.0", "0.6.4")` = true → `migrate_lookup_gain` çalışır. Bu **beklenen davranış** — eski config'leri yenile.
- **Not:** Doğru çalışır.

### CFG-2 · LOW · `save_config` backup hard link oluştururken EEXIST race
- Konum: `src/config.cpp` save_config() satır 731-755
- Kategori: Race condition
- **Açıklama:** `fs::create_hard_link(path, bak_tmp_path)` çağrısı `EEXIST` hatası alırsa retry yapıyor (satır 737-743). Bu, iki concurrent save'in aynı backup dosyasını oluşturması durumunda oluşuyor. `O_NOFOLLOW|O_EXCL` kullanılmadığından symlink attack mümkün — ama config dizini root'un mülkiyetinde (0700 izinli).
- **Not:** Gerçekçi risk düşük (root daemon çalışıyor).

### CFG-3 · INFO · `json_get_int_safe` `INT_MIN` değerinde sorunlu
- Konum: `src/config.cpp` satır 545-552
- Kategori: Edge case
- **Açıklama:** `d < (double)INT_MIN` → `INT_MIN` döndürüyor. Bu negatif DPI/polling rate'lere yol açar, ama `sanitize_device_config()` bunları `POLL_RATE_MIN`'e kampaçlıyor. Sorun değil.

---

## GUI HATALARI

### GUI-1 · LOW · `update_mode_sensitivity` Y ekseni için bazı parametreleri eksik gösteriyor
- Konum: `gui/widgets_sync.inl` update_mode_sensitivity() satır 86-95
- Kategori: UI eksiklik
- **Açıklama:** Y ekseni için `cap_mode`, `cap_x`, `sync_speed`, `smooth`, `motivity`, `gamma`, `output_offset`, `scale` satırları hiç `row_set_visible` ile kontrol edilmiyor. Bunlar Y ekseni için de görünmeli (X/Y unlinked modda).
  Şu an kod:
  ```cpp
  row_set_visible(S->y_row_label[5], S->cap_y_spin_y,    ...);
  ```
  Ama `cap_mode_combo_y`, `sync_speed_spin_y` gibi widget'lar hiç touch edilmiyor — bunlar zaten yok (Y ekseni için ayrı widget yok, X'ten kopyalanıyor: `widgets_to_profile` satır 177-189).
- **Not:** Y ekseni sadece `acceleration`, `exponent_classic`, `limit`, `input_offset`, `cap.y` widget'larına sahip. Diğer alanlar X'ten kopyalanıyor. Bu bilinçli tasarım — Y ekseni için tüm parametrelerin widget'ını göstermek yerine X'ten kopyalama.

### GUI-2 · LOW · `daemon_device_field`炊き出し`active_profile`'a bağlı cihazı bulamazsa -1 döndürüyor
- Konum: `gui/daemon_comm.inl` (daemon_device_field fonksiyonu)
- Kategori: Hata yönetimi
- **Açıklama:** `daemon_device_field()` status JSON'undanaktif profile ait cihazı bulmaya çalışıyor. Cihaz bağlı değilse veya status'ta yoksa -1 döndürüyor. Mouse test penceresinde bu durumda "—" gösteriliyor — doğru davran

### GUI-3 · INFO · `on_inotify_event` her callback'te en fazla bir kez refresh yapıyor
- Konum: `gui/devices.inl` on_inotify_event() satır 231
- Kategori: Performans
- **Açıklama:** `return TRUE;` ile ilk eventten sonra callback çıkıyor. Diğer pending event'ler bir dahaki GLib main loop iterasyonunda işleniyor. Bu "batch" davranışı doğru — birden fazla hot-plug olayı hızlı sıra가ysahepsini ayrı ayrı triggerlamaz.

### GUI-4 · INFO · `mouse_test_poll` 250ms interval'i daemon status sorgusuylaçakışabilir
- Konum: `gui/mouse_test.inl` mouse_test_poll() satır 224-258
- Kategori: Performans
- **Açıklama:** Her 250ms'de IPC status sorgusu gönderiliyor. daemon IPC_MAX 10sn deadline ile sınırlı, ama GUI 150ms socket timeout kullanıyor. 250ms interval > 150ms timeout olduğundan, daemon yanıt vermezse GUI timeout ile dönüyor ve "—" gösteriyor. Doğru çalışır.
  Ama **daemon stop edildiğinde** (`daemon_socket_exists()` false döner) expensive IPC sorgusu atlanıyor — doğru optimizasyon.

---

## OPTİMİZASYON ÖNERİLERİ

### OPT-1 · HIGH · epoll_wait timeout'u mouse polling rate'e göre ayarlanmalı
- Konum: `daemon/daemon.cpp` run_loop() satır 1239
- Kategori: Performans optimizasyonu
- **Açıklama:** Şu an 10ms sabit timeout. 8000 Hz mouse için olaylar 125µs aralıkla geliyor. 10ms timeout her 80 olayda bir "boş döngü" anlamına geliyor (housekeeping: hot-plug, IPC, sinyal flag'leri). Bu zaten doğru — timeout housekeeping için, olaylar level-triggered epoll ile bildiriliyor.
  Ama **daha düşük latency** istenirse timeout'u 1ms'e düşürmek housekeeping'i daha sık çalıştırır (maliyet: ~1µs ek per iteration). Bu durumda hot-plug deferred scan daha erken çalışır.
  **Öneri:** Cihaz aktifken timeout'u 1ms'e, cihaz yokken 100ms'e çıkar.

### OPT-2 · MEDIUM · `find_mice()` glob + open/close maliyeti azaltılabilir
- Konum: `daemon/daemon.cpp` find_mice()
- Kategori: Performans
- **Açıklama:** Her hot-plug taraması tüm `/dev/input/event*` dosyalarını open/close yapıyor. 50 cihaz × ~10µs = ~500µs. Hot-plug çok nadir olduğundan sorun değil, ama empty-rescan (2sn) durumunda faydalı olabilir.
- **Öneri:** Yalnızca yeni eklenen/çıkarılan dosyaları tara (inotify ile zaten var).

### OPT-3 · MEDIUM · `uinput_write_rel` write() batch boyutu optimize edilebilir
- Konum: `daemon/daemon.cpp` uinput_write_rel() satır 1353-1375
- Kategori: Hot-path optimizasyonu
- **Açıklama:** P93 ile iki REL_X+REL_Y tek write()'a birleştirildi (2 syscall → 1). Ama `while (written < want)` loop'u short write durumunda tekrar write yapıyor — gerçekçi senaryoda tek write tüm veriyi yazar. **Zaten optimize.**

### OPT-4 · LOW · `status_json()` string build缀bitsizincremental
- Konum: `daemon/daemon.cpp` status_json() satır 1738-1886
- Kategori: Performans
- **Açıklama:** Her status sorgusunda tüm JSON yeniden build ediliyor. `reserve(1024)` iyi bir tahmin ama cihaz sayısına göre artmalı.
- **Not:** 250ms'de bir sorgulandığında ~8KB/s JSON build — önemsiz.

### OPT-5 · INFO · Benchmark sonuçları (bench_hotpath_results.txt)
- Konum: `bench_hotpath_results.txt`
- Kategori: Referans
- **Açıklama:** Hot-path benchmark sonuçları mevcut: flush_motion latency median'ı tipik olarak <1µs (x86_64, -O3 -march=native). 8000Hz'de 125µs bütçe ile ~0.8% kullanımı — bol margin var.

---

## ÖZET — YENİ BULGULAR LİSTESİ

### Yüksek Öncelik (gerçek sorun):
1. **POLL-1**: Mouse 2000/4000/8000 Hz ayarlıyken sysfs bInterval trick'i yanlış Hz algılayabilir (USB descriptor'da 1000Hz olarak görünür)
2. **ALG-4**: synchronous gain LUT integral hassasiyeti düşük (2 partition)

### Orta Öncelik (iyileştirme):
3. **PERF-2**: push_config no-op guard iki tam JSON serialization yapıyor
4. **TS-3**: IPC thread üzerinde disk I/O阻塞
5. **CFG-2**: backup hard link race condition
6. **OPT-1**: epoll timeout polling rate'e göre dinamik olmalı

### Düşük Öncelik (edge case / nadir):
7. **PERF-1**: Hot-path 2× clock_gettime (zaten optimal)
8. **ALG-1**: Classic gain mode constant=0 C1 kırılması
9. **ALG-5**: LUT epsilon karşılaştırması çok dar olabilir
10. **GUI-1**: Y ekseni widget eksiklikleri (bilinçli tasarım)

### Bilinçli Tasarım (değişiklik gerekmez):
11. **POLL-2**: USB SuperSpeed edge case (çok nadir)
12. **POLL-3**: Buffer overflow riski yok (guard'lar doğru)
13. **TS-1**: Thread safety doğrulandı
14. **TS-4**: IPC TOCTOU ele alınmış
15. **PERF-4**: find_mice() maliyeti kabul edilebilir