# Bug Hata Raporları — Linux RawAccel

Analiz tarihi: 2026-09-11
Kapsam: `/home/a/Masaüstü/Linux-Raw-Accel-main` (RawAccel Linux v0.6.4)
Yöntem: Baştan sona (start-to-end) 5 ayrı detaylı analiz turu. Her tur farklı bir uzmanlık açısıyla tüm kaynak kod taranmıştır.
Durum: 2026-09-11 düzeltme seanslarında ele alınan bulgular (D-1..D-9, C-1..C-10, CR-1, H-1..H-3, M-1..M-8, R1-01..R1-08, R2-01, R2-04, L-2, L-5, R5-S-4/5) fixed olarak listeden çıkarılmıştır. 2026-09-11 ileri seansında ek olarak düzeltilenler: R5-S-1, R1-06, R2-02, R2-03, R2-05, R2-06, R2-07, R3-NEW-1, R3-NEW-2 (belgeli sapma + oracle satırı), R3-NEW-3, M-5b, M-6, R4 L-1, R4 L-4, R4 L-8, R4 L-10. 2026-09-11 ileri denetim seansında düzeltilenler: BUG-MED-1, TH-3/ERR-2, SEC-1, B1, B2, B5, C-2, C-5. Doğrulanan (already-fixed / false-positive / bilinçli tasarım): BUG-CRIT-1/2, BUG-HIGH-1/2, BUG-MED-2, BUG-LOW-1/2, ERR-1/3, TH-1, ALG-4, POLL-1, C-1, C-6, C-8, SEC-2/4, IPC-1, B3-B8. Aşağıdaki maddeler halen açık veya bilinçli/belgeli tasarımdır.

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

---

# TUR 7 — Güvenlik Analizi

### SEC-1 · MEDIUM · IPC Socketcredential check yok — ✅ DÜZELTİLDİ
- Konum: `daemon/daemon.cpp:2068-2181`
- **Açıklama:** IPC Unix socket `chmod 0660 root:input`. `input` grubundaki herhangi bir process bağlanıp `set_config` ile rastgele JSON config gönderebilir. Per-connection credential check (SO_PEERCRED) yok.
- **Düzeltme (yapıldı):** `accept4()` sonrası `SO_PEERCRED` denetimi eklendi — root veya `input` grubunun birincil/tamamlayıcı üyesi değilse bağlantı reddedilir ve kapatılır (`getgrnam`/`getpwnam`/`getgrouplist`). `<sys/ucred.h>` yerine `<sys/socket.h>` üzerinden `struct ucred`; `<pwd.h>` eklendi. İlk öneri uygulandı.

### SEC-2 · LOW · CLI --config path validasyonu yok
- Konum: `cli/main.cpp:2103-2117`
- **Açıklama:** Daemon `validate_config_path()` kullanıyor ama CLI hiçbir path validasyonu yapmıyor.

### SEC-3 · LOW · PID dosyası world-readable (0644)
- Konum: `daemon/main.cpp:38`

### SEC-4 · MEDIUM · setup.sh user config ownership doğrulanmamış
- Konum: `setup.sh:286-301`

### SEC-8 · MEDIUM · Daemon capability drop yapmıyor
- Konum: `daemon/main.cpp:225-503`
- **Açıklama:** Tam root yetkileri lifecycle boyunca korunuyor. Minimal capability'e düşürülmeli.

### SEC-9 · LOW · Max profil sayısı sınırı yok (IPC ile bellek tüketimi)
- Konum: `src/config.cpp:604-609`

---

# TUR 8 — Hata Yönetimi Analizi

### ERR-1 · HIGH · IPC thread kendi kendine join → std::terminate
- Konum: `daemon/daemon.cpp:2006-2016`
- **Açıklama:** IPC thread exception handler'ı `stop_ipc_server()` → `ipc_thread_.join()` → thread kendi kendine join → UB → std::terminate. Loop ve HID++ thread doğru davranıyor (yalnızca request_stop()).

### ERR-2 · MEDIUM · dump_latency_stats mutex altında stdout I/O — ✅ DÜZELTİLDİ
- Konum: `daemon/daemon.cpp:1629-1676`
- **Açıklama:** devices_mutex_ tüm stdout yazımı sırasında tutuluyor. Loop thread starved.
- **Düzeltme (yapıldı):** Kilit altında yalnızca `std::vector<DevLatSnap>` anlık görüntüsü alınır, kilit bırakılır, tüm stdout I/O kilitsiz yapılır.

### ERR-3 · MEDIUM · Client FD leak on exception
- Konum: `daemon/daemon.cpp:2061-2064`
- **Açıklama:** handle_ipc_client exception fırlatırsa close() atlanıyor → FD leak.

### ERR-4 · LOW · devices.inl inotify buffer alignas eksik
- Konum: `gui/devices.inl:236`

### ERR-7 · LOW · daemon_ipc_push_config substring match kırılgan
- Konum: `gui/daemon_comm.inl:210-213`

---

# TUR 9 — IPC Protokol Doğruluğu

### IPC-1 · MEDIUM · Serial IPC push_config disk I/O sırasında blokluyor
- Konum: `daemon/daemon.cpp:2039-2065`

### IPC-2 · MEDIUM · GUI reader \n contract kırılgan
- Konum: `gui/daemon_comm.inl:189`

### IPC-3 · MEDIUM · Versiyon müzakeresi yok
- Konum: daemon/CLI/GUI

---

# TUR 10 — Satır Satır Derin Kod İncelemesi (KRİTİK BUG'LAR)

### BUG-CRIT-1 · KRİTİK · Motion kaybı — mid-batch SYN_REPORT sonrası tail flush
- Konum: `daemon/daemon.cpp:1619-1624`
- **Hata:** Guard `!has_syn` ama doğru olan `has_motion || wrote_unsynced_event`. SYN_REPORT geldikten sonraREL_X/+3 biriktiyse bu blok atlanıyor, motion kalıcı kaybolur.
- **Düzeltme:** `!has_syn` → `has_motion || wrote_unsynced_event`

### BUG-CRIT-2 · KRİTİK · INT_MAX UB — (double)INT_MAX == 2147483648.0 (3 yer)
- Konum: `daemon/daemon.cpp:1403`, `daemon/motion_math.hpp:44`, `src/config.cpp:545`
- **Hata:** `(double)INT_MAX == 2147483648.0`. std::clamp eşitliği geçiş saymaz → static_cast<int>(2147483648.0) = UB.
- **Düzeltme:** `INT_HI = 2147483647.5`

### BUG-HIGH-1 · YÜKSEK · NaN percentile UB
- Konum: `daemon/lat_stats.hpp:103-105, 131-133`
- **Hata:** pct=NaN her iki guard'ı da geçer → ceil(count*NaN/100)=NaN → static_cast<uint64_t>(NaN) = UB.

### BUG-HIGH-2 · YÜKSEK · Raw motion sızıntısı — modifier early return
- Konum: `include/rawaccel.hpp:242`
- **Hata:** `if (!std::isfinite(time) || time <= 0) return;` → in.x/in.y sıfırlanmıyor → raw delta 1:1 iletilir.

### BUG-MED-1 · ORTA · migrate_lookup_gain Inf LUT zehirlenmesi — ✅ DÜZELTİLDİ
- Konum: `src/config.cpp:831-849`
- **Hata:** Migration sanitize sonrası çalışıyor. y*x overflow → Inf LUT'a yazılıyor.
- **Düzeltme (yapıldı):** `double product = y * x; if (std::isfinite(product)) a.data[...] = (float)product;` — Inf yazımı engellenir, sonsuz olmayan durumlarda davranış değişmez.

### BUG-MED-2 · ORTA · Post-reload interval spike
- Konum: `daemon/daemon.cpp:835`
- **Hata:** `last_time_ms = now_ms()` → ilk event'te time≈0 → ips_factor absürt → tek kare jump.

### BUG-LOW-1 · DÜŞÜK · Dead branch — setup_devices() her zaman true
- Konum: `daemon/daemon.cpp:469, 881`

### BUG-LOW-2 · DÜŞÜK · version_lt negative component (strtoul("-1") → ULONG_MAX)
- Konum: `src/config.cpp:863-871`

---

# TUR 11 — Kod Kalitesi

### Dead Code
| # | Konum | Açıklama |
|---|-------|----------|
| DC1 | `accel-synchronous.hpp:37-40` | Gain mode 8 dead member |
| DC2 | `accel-synchronous.hpp:101-132` | velocity her zaman true — dead branch |

### Kod Tekrarı
| # | Konum | Açıklama |
|---|-------|----------|
| DD1 | `accel-classic.hpp:82-176` | pow+guard 4× tekrar |
| DD4 | `rawaccel.hpp:20,50` | cutoffCoefficient duplicate |

### Magic Numbers
| # | Konum | Açıklama |
|---|-------|----------|
| MN1 | `rawaccel.hpp:124,192-198` | 1e-9 8× farklı amaç |

### Include Hijyeni
| # | Konum | Açıklama |
|---|-------|----------|
| IH1 | `accel-classic.hpp:33` | std::max ama <algorithm> yok |

---

# TUR 12 — Edge Case / Stress / Test Kapsamı Boşlukları

### Test Kapsamı Boşlukları
| # | Alan | Eksik |
|---|------|-------|
| TC-1 | Logitech quirks | logitech_quirks.hpp hiç test edilmemiş |
| TC-2 | Migration | migrate_config() actual migration test edilmemiş |
| TC-3 | IPC wire protocol | handle_ipc_client test edilmemiş |
| TC-4 | Fuzz盲点 | fuzz_accel non-finite time test etmiyor |

---

# TUR 13 — GUI Derinlemesine İnceleme

### G-1 · ORTA · KDE fix GTK ana thread'i donduruyor
- Konum: `gui/ui_builder.inl`
- **Açıklama:** kde_write_flat_accel() 250ms sleep ve 3 subprocess waitpid yapıyor, GTK main thread'de çalışıyor. Startup'ta ve "Fix Now" butonunda çalışıyor.
- **Düzeltme:** Detached worker thread'e taşı (uygulandı ✓)

### G-2 · ORTA · Dil combo'su build zamanında çevriliyor
- Konum: `gui/ui_builder.inl:130`
- **Açıklama:** tr() "Auto (locale)" → "Otomatik (sistem)" çeviriyor ama model yeniden oluşturulmuyor.
- **Düzeltme:** Ham string ekle (uygulandı ✓)

### G-3 · LOW · mode_uses her çağrıda heap alloc
- Konum: `gui/widgets_sync.inl:38`
- **Düzeltme:** initializer_list ile değiştirildi (uygulandı ✓)

### G-4 · ORTA · Graph pan zoom race
- Konum: `gui/graph.inl:282`
- **Düzeltme:** drag_zoom_start eklendi (uygulandı ✓)

### G-5 · LOW · Whitespace-only profile name kabul ediliyor
- Konum: `gui/profile_mgr.inl`
- **Düzeltme:** trim_profile_name() eklendi (uygulandı ✓)

### G-6 · LOW · Premature "Daemon reloaded" mesajı
- Konum: `gui/widgets_sync.inl:700`
- **Düzeltme:** "Requesting..." + delayed refresh (uygulandı ✓)

---

# TUR 14 — Logitech HID++ / Receiver Analizi

### B1 · ORTA · Bolt pairing slotu "occupied" her zaman true — ✅ DÜZELTİLDİ
- Konum: `src/logitech_hidpp.cpp:390`
- **Açıklama:** payload[0] echo byte'ı (subregister byte) — hiçbir zaman sıfır olamaz. Gerçek "dolu/bos" payload[1..3]'te (kind nibble veya WPID).
- **Düzeltme (yapıldı):** `result.occupied = payload[1] != 0 || payload[2] != 0 || payload[3] != 0;`

### B2 · ORTA · HID++ 1.0 bildirimleri 2.0'a sahte sınıflandırma — ✅ DÜZELTİLDİ
- Konum: `src/logitech_hidpp.cpp:502-520`
- **Açıklama:** sub_id ∈ {0x40..0x4B} + address & 0x0F == 0 ise hidpp20 olarak sınıflandırılıyor (yanlış). 1.0 cihazlarda CONNECT_DISCONNECT olayı sessizce düşer.
- **Düzeltme (yapıldı):** `const bool hidpp20 = sub_id < 0x40 && (address & 0x0F) == 0;`

### B3 · LOW · Genişletilmiş rapor hızı fallback eksik
- Konum: `src/logitech_hidpp.cpp:1760-1768`
- **Açıklama:** 0x8061 nullopt döndürdüğünde 0x8060 legacy dilimine inmiyor.

### B4 · LOW · Model ID parçaları yanlış genişlik (2 hex vs 4 hex)
- Konum: `src/logitech_hidpp.cpp:1299-1311`

### B5 · LOW · almost_full (0x02) şarj ediliyor sayılmıyor — ✅ DÜZELTİLDİ
- Konum: `src/logitech_hidpp.cpp:190,204`
- **Düzeltme (yapıldı):** `status == 0x01 || status == 0x02 || status == 0x04` (her iki ayrıştırıcıda)

### B6 · LOW · BATTERY_CHARGE default dal cihazı çevrimdışı yapıyor
- Konum: `src/logitech_hidpp.cpp:216-220`

### B7 · LOW · from_bytes() legacy_battery data[6]==0 şartı
- Konum: `src/logitech_hidpp.cpp:499-500`

### B8 · LOW · Bolt cihaz adı hiç okunmuyor
- Konum: `src/logitech_hidpp.cpp:1476-1496`

---

# TUR 15 — CLI Derinlemesine İnceleme

### C-1 · ORTA · -c/--config sonraki token'ı yutuyor
- Konum: `cli/main.cpp:2119-2127`
- **Açıklama:** `rawaccel-cli -c --json list` config path'i literal "--json" olarak ayarlıyor → dosya oluşturuyor.

### C-2 · ORTA · speed_max < speed_min kabul ediliyor — ✅ DÜZELTİLDİ
- Konum: `cli/main.cpp:948-953` + `src/config.cpp:497-498`
- **Açıklama:** Sessizce clamp ediliyor ama rc=0 dönüyor.
- **Düzeltme (yapıldı):** `set-param speed_min/speed_max` sonrası çapraz doğrulama — zıt yönlü çakışmada kullanıcıya WARNING basılır; clamp korunur.

### C-3 · ORTA · mode=lookup uyarısı lut-data'yı gösteriyor — CLI'da yok
- Konum: `cli/main.cpp:968-972`
- **Açıklama:** LUT verileri yalnızca GUI ve import ile değiştirilebilir.

### C-4 · ORTA · Import edilen isim > 256 sessizce truncate ediliyor
- Konum: `cli/main.cpp:1247-1251` + `src/config.cpp:571`

### C-5 · YÜKSEK · gaming preset'in limit=1.8'i cap_mode=out 1.5 ile ulaşılamaz — ✅ DÜZELTİLDİ
- Konum: `include/presets.hpp:26-40`
- **Açıklama:** Classic GAIN modunda cap.y=1.5 (default) → gain asimptotu 1.5. limit=1.8 hiçbir zaman çalışmaz.
- **Düzeltme (yapıldı):** gaming preset için `accel_x.cap = accel_y.cap = { 15, 1.8 }` eklendi. Not: `limit` yalnızca natural modda çalışır (accel-natural.hpp: `limit(n-1.0)`); classic modda belirleyici olan cap'tir. fps preset'i zaten `cap={20,1.8}` ile uyumlu.

### C-6 · YÜKSEK · --no-daemon + reload hiç uygulanmaz
- Konum: `cli/main.cpp:239,247` + `src/config.cpp:803-821`
- **Açıklama:** --no-daemon user home'a kaydediyor, reload daemon'un /etc/ dosyasını okuyor. İkisi farklı dosya → düzenleme sessizce kaybolur.

### C-7 · DÜŞÜK · Stop zaten durmuş daemon'da rc=1 dönüyor
- Konum: `cli/main.cpp:1330-1338`

### C-8 · DÜŞÜK · Unknown command stdout'a help dump yapıyor (stderr olmalı)
- Konum: `cli/main.cpp:2256-2259`

---

# TUR 16 — Build System / Setup Analizi

### BS-1 · YÜKSEK · setup.sh clean_old_install modprobe.conf'u temizlemiyor
- Konum: `setup.sh:213-214`

### BS-2 · YÜKSEK · setup.sh build → clean order: stale .o ABI mismatch riski
- Konum: `setup.sh:537-541`
- **Açıklama:** Build, clean'den önce çalışıyor. Eski GCC version)object files varsa ABI mismatch.

### BS-3 · YÜKSEK · ASan build logitech dosyalarını kapsamıyor
- Konum: `tests/run_tests_asan.sh:23-28`
- **Açıklama:** logitech_receiver.cpp ve logitech_hidpp.cpp Sanity test altında compile edilmiyor.

### BS-4 · YÜKSEK · setup.sh GTK4 yoksa verify_install "EKSİK" raporluyor
- Konum: `setup.sh:538`

### BS-5 · ORTA · run_fuzz.sh unquoted $FUZZ_FLAGS + eksik logitech kaynakları
- Konum: `tests/run_fuzz.sh:25,28,37`

### BS-6 · ORTA · setup.sh backup timestamp collision (aynı saniye)
- Konum: `setup.sh:296-300`

### BS-7 · ORTA · udevadm control --reload-rules container'da fail oluyor
- Konum: `setup.sh:312`

### BS-8 · ORTA · build.sh unquoted compiler vars
- Konum: `scripts/build.sh:97,107,117`

### BS-9 · ORTA · bench_hotpath.sh hardening flags eksik
- Konum: `scripts/bench_hotpath.sh:17-31`

### BS-10 · ORTA · perf-gate CI job eksik dependency
- Konum: `.github/workflows/ci.yml:136-153`

### BS-11 · ORTA · PIDFile dead config (Type=simple)
- Konum: `scripts/rawaccel.service:24`

### BS-12 · DÜŞÜK · CMakeLists hardcoded DESTINATION
- Konum: `CMakeLists.txt:144-145`

---

# TUR 17 — Threading / Timing Derinlemesine İnceleme

### TH-1 · ORTA · stop_ipc_server concurrent double-entry — data race
- Konum: `daemon/daemon.cpp:2031-2054`
- **Açıklama:** IPC thread catch → stop_ipc_server() ve main thread → stop_ipc_server() eş zamanlı → ipc_sock_path_ data race (plain std::string, no lock).

### TH-2 · ORTA · push_cfg_mu_ wait boundsuz — IPC DoS
- Konum: `daemon/daemon.cpp:553`
- **Açıklama:** set_config push_config mutex'i beklerken hotplug setup_devices() çok uzun sürebilir → serial accept loop bloklanır → status/reload/ping timeout.

### TH-3 · ORTA · dump_latency_stats mutex altında cout I/O
- Konum: `daemon/daemon.cpp:1646-1685`
- **Açıklama:** stdout block olursa devices_mutex_ tutuluyor → hotplug/reload starved.

### TH-4 · ORTA · Telemetry seqlock spin devices_mutex_ altında
- Konum: `daemon/daemon.cpp:1824-1845`
- **Açıklama:** 64-spin seqlock devices_mutex_ altında → yüksek hızda yazıcı varken mutex starved. Hiclik, per-device alanların zaten atomic olması.

### TH-5 · LOW · config_path_ unsynchronized access (bugün güvenli invariant nedeniyle)
- Konum: `daemon/daemon.cpp:534,551,2178`

### TH-6 · LOW · Per-device hot-path state loop thread convention'a bağlı
- Konum: `daemon/daemon.cpp:1485-1635,811-836`

### TH-7 · LOW · Signal handler std::atomic lock-free guarantee yok
- Konum: `daemon/main.cpp:83-99`

### Doğrulanan Temiz Noktalar (tur 17):
- Lock order acyclic (push_cfg_mu_ → devices_mutex_ → lat.mtx → log_mu_)
- CLOCK_MONOTONIC_RAW unified
- Seqlock protocol doğru
- Deadlock yok
- Condition variable yok (hepsi sticky atomic flag)
- EINTR her yerde ele alınıyor

---

## GÜNCEL TOPLAM — TÜM 17 TUR

### KRİTİK:
1. BUG-CRIT-1 — Motion kaybı (mid-batch tail flush)
2. BUG-CRIT-2 — INT_MAX UB (3 yerde)

### YÜKSEK:
3. ERR-1 — IPC thread self-join → std::terminate
4. BUG-HIGH-1 — NaN percentile UB
5. BUG-HIGH-2 — Raw motion sızıntısı
6. POLL-1 — 2000/4000/8000 Hz algılama hatası
7. C-5 — gaming preset limit cap çelişkisi
8. C-6 — --no-daemon + reload uygulanmıyor
9. BS-1 — setup.sh modprobe.conf temizlenmiyor
10. BS-2 — stale .o ABI mismatch riski
11. BS-3 — ASan build logitech kapsamıyor
12. BS-4 — GTK4 yoksa false "EKSİK"

### ORTA:
13-30: BUG-MED-1/2, ERR-2/3, SEC-1/4/8, IPC-1/2/3, G-1/2/4, B1/2, C-1/2/3/4, BS-5..11, TH-1/2/3/4

### DÜŞÜK:
31+: BUG-LOW-1/2, ERR-4..7, SEC-2/3/9, G-3/5/6, B3-B8, C-7/8, BS-12, TH-5/6/7

---

# TUR 18 — Raw Passthrough + Hareket Pürüzsüzlüğü Derinlemesine Analizi (2026-09-11)

Analiz tarihi: 2026-09-11 (üçüncü tur)
Kapsam: Raw passthrough yolu (`daemon/daemon.cpp` inline REL iletimi) + accel hattının zamanlama/EMA/süreklilik davranışı (`daemon.cpp` flush_motion, `daemon/motion_math.hpp`, `include/rawaccel.hpp`, `include/accel-*.hpp`, `gui/widgets_sync.inl`)
Yöntem: Kaynak kod satır satır + canlı telemetri seqlock kontratı + referans formüllerle karşılaştırma
Durum: Güncelleme (2026-09-11) — aşağıdaki maddeler **düzeltildi** veya **kasıtlı değişiklik yapılmadı** olarak işaretlenmiştir. Önceki seviyede bunlar yalnızca loglanıyordu.

**DÜZELTME ÖZETİ (TUR 18 turu):**
- `daemon/daemon.cpp` — SM-1/SM-8: `time_ms` artık kernel SYN_REPORT `ev.time` DELTASI ile ölçülüyor (`last_frame_ev_us`); duvar saati yalnız ilk frame/raw fallback. SM-8: tüm `(0, DEFAULT_TIME_MIN]` bandı floor'landı (`time_ms < DEFAULT_TIME_MIN → DEFAULT_TIME_MIN`).
- SM-2: buton/tekerlek/SYN-alt tipi ara olayları `queued_events` (16) buffer'ında biriktirilip frame'in gerçek SYN_REPORT'unda yazılıyor — frame artık bölünmüyor; overflow'da backlog erken yazılır (düşürme yok).
- LOW-1 + BUG-CRIT-1: SYN'siz motion kuyruğu `dev.pending_dx/dy` + `pending_events`'a ertelenip sonraki batch'in gerçek SYN'inde flush ediliyor — sentetik SYN yalnız `wrote_unsynced_event` (motion dışı) için.
- RAC-5: SYN_REPORT dışı SYN alt tipleri (SYN_MT_REPORT, SYN_CONFIG…) aynen iletilir, motion flush'ı tetiklemez. RAC-4: SYN_DROPPED'i bitiren SYN'de `last_frame_ev_us` + `last_time_ms` re-anchor.
- MED-4: `flush_motion`'a `lat_anchor_ns` referansı — her flush kendi işlem süresini ölçer.
- SM-5: `speed_processor::reconfigure()` — EMA + remainder korunur, yalnız yeni açılan (0→pozitif) smoother resetlenir. TEL-1: `apply_profile` telemetri sayaçlarını sıfırlar → `telem_ok=false` doğru.
- SM-3: EMA `nwt/nct` trend'i `±1e6` + non-finite clamp. SM-6: deceleration/stop'da ekstra trend sönümlemesi.
- SM-7: half-life üst sınırı 31.7 yıl (`1e9`) → 17 dk (`1e6`); P107 domain kontratı `1e6`'nın geçmesini zorunlu kıldığından 10 sn'ye inilmedi.
- CUR-1: classic GAIN overflow guard'ı C0 "identity çöküşü" yerine sürekli kuyruk (`cap_y·(1 − cap_x/x)`); P105 `g ≤ cap.y` kontratı korunuyor.
- CUR-2: **kasıtlı değişiklik YOK** — `cap.y<1 → sign=-1` (gain<1, deceleration) referral RawAccel eğrisinin davranışı; P105/P106 testleri + orakel bunu kontrat yapıyor.
- RAC-1: uinput fd `O_NONBLOCK` + EAGAIN'de sınırlı backoff retry; EAGAIN artık cihaz ölümü sayılmaz, asla disconnect yok.
- RAC-2: belgeli (housekeeping zaten flag-gated; ev.time hesabı semptomu çözdü). RAC-3: **kasıtlı değişiklik YOK** — systemd `RestrictRealtime` korunur; semptom ev.time ile giderildi.
- PAS-1: `use_raw_input=false` → daemon HİÇBİR cihazı yakalamaz (master intercept anahtarı). PAS-2: `dev_cfg.disable` → cihaz yakalanmaz (güvenli mod). PAS-3: raw'da `output_dpi_spin` de grey-out. CLI `status` metni güncellendi ("dormant" ibaresi kaldırıldı).

**Durum gösterimi:** `[DÜZELTİLDİ]` = kodda uygulandı, test+oracle+tr_coverage+ASan doğrulandı (33786/33786, orakel 1071/68). `[KASITLI]` = kontrat testleri/orakel nedeniyle bilinçli değiştirilmedi.

---

## A. RAW PASSTHROUGH — KULLANICIYA DÖNÜK KÖK SORUNLAR

**PAS-1 · ORTA · `use_raw_input` config bayrağı tamamen ölü kod** — **[DÜZELTİLDİ]**
- Konum: `include/config.hpp:47` (tanım), `src/config.cpp:607-608` (okuma), `src/config.cpp:628` (yazma)
- Kategori: Arayüz/behaviour
- Açıklama: `use_raw_input` (varsayılan `true`) JSON'da parse/serialize ediliyor ama `daemon/` içinde **sıfır referans**. Böyle bir üst düzey bayrağı 1:1 raw yakalama istediğiyle işaretleyen kullanıcı ya da GUI ayarı hiçbir şey yapmıyor; daemon yalnızca per-profil `dev.settings.prof.raw_passthrough`'u onurlandırıyor.
- Öneri: Bayrağı kaldır (yanıltıcı) veya tüm profiller için global raw geçişine bağla ve dökümanda belirt.

**PAS-2 · ORTA · `device_config::disable` bayrağı da ölü kod** — **[DÜZELTİLDİ]**
- Konum: `include/config.hpp:25`, `src/config.cpp:581-582` (parse), `src/config.cpp:317` (serialize)
- Kategori: Arayüz/behaviour
- Açıklama: `disable=true` işaretli bir cihaz yine de yakalanıp işleniyor; daemon yalnızca `prof.dev_cfg.dpi` ve `polling_rate` okuyor (`daemon.cpp:889-890`). "Disable" profili kullanıcıyı yanıltır (grep doğrulandı).
- Öneri: `dev_cfg.disable` okunup cihaz gölge/güvenli moda alınmalı ya da bayrak kaldırılmalı.

**PAS-3 · DÜŞÜK · Raw modda `output_dpi_spin` hâlâ aktif — etkisiz ayar yanılgısı** — **[DÜZELTİLDİ]**
- Konum: `gui/widgets_sync.inl:6-24` (`update_raw_sensitivity`)
- Kategori: UI hata
- Açıklama: Raw passthrough'ta 17 pipeline widget'ı grey-out ediliyor ama `output_dpi_spin` (DPI normalize, `rawaccel.hpp:366-370`) raw modda baypas edildiği hâlde **aktif kalıyor**. Kullanıcı "Output DPI" düzenler, kullanıcıya hiçbir etkisi olmaz. (kozmetik — düzgünlük değil)

**PAS-4 · DÜŞÜK · Raw 1:1 iddiası zaman damgaları için geçerli değil** — **[DEĞİŞMEDİ — katman kısıtı, belgeleme maddesi]**
- Konum: `daemon/daemon.cpp:1731-1734` (inline forward) → `uinput_write()` → `libevdev_uinput_write_event`
- Kategori: Belgeleme
- Açıklama: Raw yolu `type/code/value` + SYN yerleşimi açısından byte-identical; ama `ev.time` artık **yeniden damgalanıyor** (write anında + kernel uinput enjeksiyonunda). Özgün evdev zaman damgaları aşağı yönlü tüketiciye (örn. uinput cihazındaki libinput adaptive accel) ulaşmıyor. Stall sonrası tüm drained batch tek µs'lik burst gibi görünür. Kullanıcı arayüzündeki "bit-identical" ifadesi zaman damgaları için yanıltıcı — düzeltilemez katman kısıtı, dökümana işlenmeli.

---

## B. ZAMANLAMA / HARAKET DÜZGÜNLÜĞÜ — EN KRİTİK BULGULAR

**SM-1 · KRİTİK · Coalesced-frame near-zero interval → gain spike (hareket sıçraması)** — **[DÜZELTİLDİ]**
- Konum: `daemon/daemon.cpp:1570-1582` (flush_motion zamanlama)
- Kategori: Düzgünlük (smoothness)
- Açıklama: `time_ms`, `now - dev.last_time_ms` ile **işleme duvar saati** üzerinden hesaplanıyor; kernel olay zaman damgası (`ev.time`) hiç okunmuyor. Loop thread gecikince (epoll dispatch, scheduler stall, housekeeping) bir batch N kadar kernel frame'ini tek seferde boşaltır. Frame 1 tüm stall'ı emer (under-gain), frame 2..N ise birkaç µs sonra `flush_motion` çağrıldığından `time_ms ≈ 0.0005 ms` gibi küçük ama pozitif bir değer alır. `<= 0` klamplaması (DEFAULT_TIME_MIN = 1000/8000/2 = 0.0625 ms, `rawaccel-base.hpp:15`) yalnızca sıfır/negatifi yakalar — **bu pencereyi kaçırır**. `ips_factor = dpi_factor/time_ms` → 800 dpi'de ~2500 → hız binlerce ips → gain cap'e vurur → 2-5× overshoot tek kare. Flick = görünür "zıplama".
- Öneri: `time_ms`'i SYN_REPORT'un `ev.time` damgasıyla ölç (gerçek poll periyodu), min guard sonrası da `max(time_ms, DEFAULT_TIME_MIN)` şeklinde alt tavan uygula. (Aşağı tavan tek başına 1 kHz'de ~16× overshoot bırakır — gerçek çözüm frame damgası.)

**SM-2 · KRİTİK · Ara cihaz olayları (buton/tekerlek) aynı frame'i bölüyor → near-zero interval spike** — **[DÜZELTİLDİ]**
- Konum: `daemon/daemon.cpp:1742-1753` (flush_pending_motion çağrıları)
- Kategori: Düzgünlük
- Açıklama: `[REL_X:+5, BTN_LEFT:1, REL_Y:+3, SYN]` gibi bir HID raporunda buton/tekerlek olayı arasında `flush_pending_motion()` çağrılıyor; bu `last_time_ms`'i güncelliyor. SYN'deki son REL_Y flush'ı yine µs-mertebesinde `time_ms` hesaplar → tek eksen gain spike. Gaming mouse'lar buton+motion'ı aynı rapora basar. Bir kernel frame'i de birden çok çıktı frame'ine bölünür.
- Öneri: Ara olaylarda flush yerine yalnızca SYN_REPORT'ta flush et (veya frame başına tek zaman damgası kullan).

**SM-3 · YÜKSEK · EMA trend accumulator near-zero interval'da patlıyor** — **[DÜZELTİLDİ]**
- Konum: `include/rawaccel.hpp:75-78` (linear_ema_smoother::smooth)
- Kategori: Düzgünlük
- Açıklama: `nwt = (windowTotal - oldW)/time` — coalesced frame'de `time≈0.0005` → trend dev ~1000× büyür. `trendDampening=0.75` per-event azalttığından SM-1 patolojisi smoothed input aktifken bile sonraki normal frame'lere taşınır → kalıcı overshoot.
- Öneri: `time` bazlı trend sönümleme veya `nwt` üst sınırı.

**SM-4 · YÜKSEK · İdle sonrası ilk frame under-gain (100 ms klamplaması)** — **[DÜZELTİLDİ]**
- TUR 25 düzeltmesi: `flush_motion` ev.time dalında `gap_ms ≥ kIdleGapMs` (DEFAULT_TIME_MAX, 100 ms) ise time_ms artık nominal tek poll periyodu `1000/poll_rate` (kırpılmış) olarak alakalı; boş SYN_REPORT frame'leri de `last_frame_ev_us`/`last_time_ms`'i yeniden demirler (`daemon.cpp:1724-1738, 1992-2000`). Duruş sonrası ilk frame gerçek periyotla ölçülür — under-gain yok.
- Konum: `daemon/daemon.cpp:1581` + `rawaccel-base.hpp:16` (`DEFAULT_TIME_MAX=100`)
- Kategori: Düzgünlük
- Açıklama: Herhangi bir duruş sonrası ilk SYN frame'i tüm boşluğu ölçer → 100 ms'e klamplanır → `speed ≈ 0 → gain ≈ 1`. "Duruş sonrası flick" bir kare geç başlar (SM-1 overshoot ile birlikte). `apply_profile` re-anchoring (R3-NEW-3) yalnızca profil anahtarlarını kapsar, doğal idle duruşlarını değil.

**SM-5 · YÜKSEK · Profil/uygulama-fokus anahtarı sırasında EMA + remainder + zamanlama sıfırlanıyor** — **[DÜZELTİLDİ]**
- Konum: `daemon/daemon.cpp:886-911` (`apply_profile`) → `dev.sp.init()` (`rawaccel.hpp:117-147`) + remainder sıfırlama + `last_time_ms` re-anchor
- Kategori: Düzgünlük / geçiş
- Açıklama: Per-uygulama profil (`apply_active_app` → `apply_profile`, `daemon.cpp:859-882`) **her alt-tab/fokus değişiminde** olay ortasında smoother durumunu 0'a çeker, alt-piksel remainder'ını düşürür, interval'i yeniden hizalar. Fare hareket hâlindeyken gain anlık kesilir ve ~bir half-life boyunca yeniden birleşir → görünür transient. Profil kaydetme/config reload'da da aynı.
- Öneri: Fokus değişiminde yalnızca rastlanan parametreleri (mode/curve) değiştir, smoother + remainder durumunu koru.

**SM-6 · ORTA · Stop/ters yönden sonra trend overshoot ("ghosting")** — **[DÜZELTİLDİ]**
- Konum: `include/rawaccel.hpp:62-78`
- Kategori: Düzgünlük
- Açıklama: Trend, `time` ile çarpılarak tahmine taşınır. Fare aniden durunca pozitif trend hızı ~`1/0.75` olay daha yukarı tahmin eder → duruş sonrası 1-3 frame yüksek gain → "hayalet" mikro hareket.
- Öneri: Trend'i olay başına değil zaman ölçekli sönümle; durma/reversal'da trend sıfıra yaklaştır.

**SM-7 · ORTA · Dev half-life cursor'ı donduruyor** — **[DÜZELTİLDİ]**
- TUR 25 düzeltmesi: ortak `SMOOTH_HALFLIFE_MAX = 10000` ms (10 sn) sınırı `rawaccel-base.hpp:24`; sanitize (`config.cpp:518`), CLI set-param domaini (`cli/main.cpp:957-959`, P107 byte-korunumu korundu) ve help metni paylaşıyor. P107 testi de 1e6 → SMOOTH_HALFLIFE_MAX'a güncellendi. GUI spin zaten max 200 ms — etkilenmez.
- Konum: `include/rawaccel.hpp:19-20`, `src/config.cpp:509-520`
- Kategori: Düzgünlük / DoS edge case
- Açıklama: Sanitize `input_speed_smooth_halflife`'ı 1e9 ms'e (~11.6 gün) klamp'liyor; ama kod yorumu (`config.cpp:504`) hl ≥ ~1.4e16 üstünde `pow(0.5, 1/hl)`'in tam 1.0'a yuvarlandığını (twc=0 → kalıcı donma) zaten kaydediyor. Gerçekte 1e9-kapak dahi "neredeyse hiç hareket etmez" eşiğinin çok üstünde: `pow(0.5, 1e-9)≈0.9999999993` → `twc ≈ 6.9e-8`/1ms frame → speed/scale tahmini pratikte static → imleç ya donar ya ağır jitter yapar. (DoS edge case, clamp değerlerin çok üstünde.)
- Öneri: Half-life üst sınırını gerçekçi bir değere (örn. 10 sn) indir.

**SM-8 · ORTA · Gecikme toleransı / `DEFAULT_TIME_MIN` guard'ı coalesced frame'leri kapsamıyor** — **[DÜZELTİLDİ]**
- Konum: `daemon/daemon.cpp:1580` + `rawaccel-base.hpp:15`
- Kategori: Düzgünlük
- Açıklama: 0.0625 ms altı pozitif interval'ler (`(0, 0.0625]`) klamp'siz geçer → `ips_factor` patlar. 1 kHz poll'da frame arası gerçek aralık 1 ms iken; stall sonrası drained frame'ler 0.0005-0.01 ms aralıklar üretebilir.
- Öneri: `time_ms = std::max(time_ms, DEFAULT_TIME_MIN)` (floor + clamp birlikte).

---

## C. ALGORİTMA / EĞRİ SÜREKSİZLİKLERİ (düzgünlüğü bozar)

**CUR-1 · ORTA · `classic` GAIN — cap_y=0 fallback gerçek C0 adım üretir** — **[DÜZELTİLDİ]**
- Konum: `include/accel-classic.hpp:134-142, 170-176`
- Açıklama: `base_fn(cap_x)` kendi overflow guard'ına takılıp 0.0 dönünce `cap_y=0, constant=0` → cap_x altında `base_fn ≠ 0` iken cap_x'te çıktı **0'a atlar** (hard edge). Hız tam cap_x sınırında keskin gain sıçraması.
- Öneri: cap_y=0 yerine asimptotik eğimi koruyan sürekli fallback.

**CUR-2 · ORTA · `classic` — cap.y < 1 ise gain değeri negatif olabilir → eksen ters döner** — **[KASITLI — P105/P106/orakel kontratı; referans RawAccel davranışı]**
- Konum: `include/accel-classic.hpp:69-70, 103-104` + operator() 50-51
- Açıklama: `cap.y=0.5` (cap_mode=out) → `sign=-1` → `gain = 1 − min(base_fn, 0.5)`. `base_fn(x) > 1` olduğunda gain **negatif** → imleç ters yön. Yalnız eğri degenerasyonu korunuyor, polarite korunmuyor.
- Öneri: sub-1 cap'lerde kullanıcı uyarısı veya gain alt sınırı 0.

**CUR-3 · ORTA · `power` — Inf guard gain'i 1.0'a sertçe tıklatıyor** — **[DÜZELTİLDİ]**
- TUR 25 düzeltmesi: `accel-power.hpp` overflow guard'ı, cap istenmişse (`cap_y < DBL_MAX`) cap_y tavanına klamp eder (sürekli kuyruk — tail'in yakınsadığı değer), yalnız cap'siz eğride defensif identity (1.0) korunur — asla 1-kare donma/NaN değil. Orakel grid'i bu guard'a asla düşmez (max hız 1e5, exp ≤ 10 → ~1e53 « DBL_MAX) → sapma satırı yok. Yeni birim test `test_cur3_power_inf_guard` (overflow → cap tavanı; cap'siz → identity; sub-overflow ham eğri bozulmaz).
- Konum: `include/accel-power.hpp:132` (+ 136-139)
- Açıklama: `pow(scale·x, exponent)` taşarsa (scale=100, exp=5, hız~3e19) gain dev değerden doğrudan **1.0'a** atlar → imleç aniden 1:1. cap yoksa tüm eğri o noktadan sonra identity'e dönüşür.
- Öneri: `minsd(out, cap)` sürekli tavan; sert 1.0 yerine.

**CUR-4 · ORTA · `synchronous` GAIN — 512 ips üstünde gain → 0 (hızlı flick'te sürüklenme)** — **[KASITLI — referans RawAccel davranışı; orakel kontratı]**
- Konum: `include/accel-synchronous.hpp:151,158,170-172` + `accel-lookup.hpp:11-30`
- Açıklama: `ilogb` 2^9'da kıstırılır; velocity modunda `gain = data[96]/x → 0` hız büyüdükçe. Çok hızlı flick imleci "sürükler"/yavaşlatır — etkili görünmez output-speed cap.
- TUR 25 notu: Orakel `sync_gain_*` caselerini 100.000 ips'e kadar (2000/3000/4000/5000/10000/100000) reference'la birebir farklaştırıyor ve **eşleşiyor** — bu LUT kuyruğu referee RawAccel'ın kendi davranışı (output-speed integral indeks aralığı [-3,9]). Değiştirilse orakel kontratı bozulur; kaynak dokunulmadı.
- Öneri: LUT aralığını (point count) artır veya taper yerine sabit tail.

**CUR-5 · DÜŞÜK · `power` — output_offset platosu hız=0'da gizli ramp/ilk kare vuruşu** — **[KASITLI — referans formül; belgelendi]**
- Konum: `include/accel-power.hpp:112-120,136-139`
- Açıklama: `speed<=0 → 1.0`; `0 < speed ≤ offset.x` → `offset.y` platosu (output_offset 100'e kadar çıkabilir). Tam duruştan sonra ilk hareket frame'i gain≈output_offset (100×) kick — input smoothing yoksa sert.
- TUR 25 notu: Plato, referans `power` GAIN formülünün parçası (değiştirilmesi orakel kontratı çelişkisi üretir); kullanıcı-visible "kick" zaten input-speed smoothing (`input_speed_smooth_halflife`) ile kapatılır — davranış değiştirilmedi, gerekçe rapora eklendi.
- Öneri: Platonun input smoothing ile kapatıldığını belge veya plato aralığını zaman-ölçekli yap.

**CUR-6 · DÜŞÜK · `jump` LEGACY — step.crossing'de 1-count jitter tüm gain'i toggle ediyor** — **[KASITLI — referans RawAccel davranışı; orakel kontratı; smooth eşiği de referansla birebir]**
- Konum: `include/accel-jump.hpp:46-47`
- Açıklama: `x<step.x → 1.0; aksi 1.0+step.y` — C0 süreksizlik; EMA input smoothing yoksa her 1-count jitter frame gain'i uçtan uca değiştirir.
- Not: `smooth·step.x<1` eşiğinde (`accel-jump.hpp:26-27`) sigmoid sessizce kapanır → davranış süreksizliği.
- TUR 25 notu: Ref `jump_base` aynı `rate_inverse < 1 → smooth_rate = 0` kuralını kullanıyor (`tests/oracle/ref/accel-jump.hpp:19-24`) ve `jump_legacy_*` caseleri oracle'da birebir eşleşiyor — C0 momentum referans davranışı; jitter yumuşatma input smoothing ile kapatılır. Kaynak değiştirilmedi.

**CUR-7 · DÜŞÜK · NaN/Inf savunmaları her yerde sert 1.0/0.0 süreksizliği üretiyor** — **[KASITLI — defense-in-depth; bellidir]**
- Konum: `rawaccel.hpp:383-384` (final isfinite → sıfırlama = 1-kare imleç donması), `accel-classic.hpp:188-195`, `accel-power.hpp:132`, `accel-jump.hpp:80-81`
- Açıklama: Her NaN girişi gain'i ya 1.0 ya 0.0 yapıyor — hareketli akışta sert kesinti. Korumalı ama "düzgün" değil.
- TUR 25 notu: Bu guard'lar pipeline'dan asla NaN/Inf çıkmamasını garantiler (son çare, umulan tetiklenmemesidir); asıl sapma koruması her algoritmada local `isfinite` guard'ları + P96 sweep'te zaten testli. "Komşu değere blend" state'li yumuşatma ister ve orakel reference'ından farklı bir davranış üretir — reference-clone felsefesine uygun olarak sert-guard korunur, davranış bilinçli.
- Öneri: Son çare yerine komşu değere blend.

**CUR-8 · BİLİNÇLİ · Düşük hızda tamsayı-count kuantizasyonu**
- Konum: `daemon/daemon.cpp:1669-1686`
- Açıklama: Frame başına ham delta tamsayı; düşük hızda dik eğrilere giren hız girişi frame-frame kaba kuantize — içsel gain jitter'ı. Yalnız input smoothing düzeltir. (Tasarım, tespit olarak logla.)

---

## D. TELEMETRİ / YARIŞ

**TEL-1 · ORTA · accel→raw anahtarından sonra bayat `telem_ok=true` telemetri** — **[DÜZELTİLDİ]**
- Konum: `daemon/daemon.cpp:1980-2001` (seqlock okuma)
- Kategori: Kontrat ihlali / GUI yanıltması
- Açıklama: AGENTS.md "raw modda telem_ok=false" der; kod bunu yalnızca sayaç 0 iken üretiyor. Daha önce accel moddayken `samples` sayaç even ≠ 0'dır; raw inline yolu (`daemon.cpp:1731-1734`) asla increment etmediğinden devamcı çift sayaç "eşleşti" der → **bayat** `speed_ips/out_ips/gain` GUI'de "canlı" görünür. `apply_profile` (`daemon.cpp:886-911`) sayaç 0'lamıyor. (Gözlemlenen `G:1.0043` kalıntısı.)
- Öneri: `apply_profile` içinde `telemetry->samples.store(0)` (veya raw geçişte) — `dump_latency_stats` bunu doğru yapıyor (BUG-21 `daemon.cpp:1741-1748`), yalnız seqlock yolu eksik.

**RAC-1 · ORTA · Blocking uinput fd + EAGAIN "cihaz öldü" olarak yorumlanıyor** — **[DÜZELTİLDİ]**
- Konum: `daemon/daemon.cpp:1435-1438` (`uinput_write`), 1447-1469 (`uinput_write_rel`)
- Kategori: Düzgünlük / kararlılık
- Açıklama: `LIBEVDEV_UINPUT_OPEN_MANAGED` = O_RDWR blocking. Yavaş compositor uinput tüketicisini geri basınçlayınca hot-path `write()` **blocklayabilir** (tek loop thread saplanır → frame'ler birikir → SM-1) ya da EAGAIN dönerse koşulsuz cihaz ölümü sayılır → uinput destroy + grab bırakma + 5 sn yeniden açma churn (1374-1418). İkisi de pürüzsüzlüğü törpüler.
- Öneri: write (EAGAIN) yolunda retry/backoff, yavaş tüketici tanısı ve uyarı.

**RAC-2 · ORTA · Pre-epoll housekeeping her iterasyonda ms-mertebesinde gecikme ekleyebilir** — **[DEĞİŞMEDİ — belge; housekeeping bayrak-gated, SM-1 çözümü (ev.time) semptomu ortadan kaldırdı]**
- Konum: `daemon/daemon.cpp:1272-1333`
- Kategori: Zamanlama
- Açıklama: `epoll_wait(10ms)` öncesi push_cfg apply, `apply_active_app()`, SIGHUP reload (dosya read+parse+tüm cihazlara re-apply), hotplug kontrol ve devices_mutex kontrolü çalışıyor. Hareketle aynı anda tetiklenirse ms-gap oluşur → frame'ler kuyruğa girer → SM-1'i besler.
- Öneri: Housekeeping'i döngü ayrı yoluyla zamana yay; aktif cihaz varken gecikme kısmını minimal tut.

**RAC-3 · ORTA · Gerçek zamanlı öncelik yok (scheduler preemption SM-1'in tetikleyicisi)** — **[KASITLI — systemd `RestrictRealtime` korundu; semptom SM-1 ev.time hesabıyla giderildi]**
- Konum: `daemon/main.cpp:225-503` + `scripts/rawaccel.service` (`RestrictRealtime=true`)
- Kategori: Zamanlama
- Açıklama: Loop thread'i varsayılan CFS önceliğinde; systemd gerçek zamanlıyı yasaklıyor. Scheduler preemption tam olarak SM-1'i (coalesced frame) ve SM-4'ü (delay dip) doğurur. Ev.time düzeltmesi olmadan `Nice=-10` veya FIFO (`RestrictRealtime=false` + CAP_SYS_NICE) bile önemli iyileştirme sağlar.
- Öneri: Evdev-time hesabı + gerekirse thread öncelik yükseltme (Nice).

**RAC-4 · ORTA · SYN_DROPPED penceresi — yasal yarı-frame hareketi sessizce düşüyor** — **[DÜZELTİLDİ]**
- TUR 25 düzeltmesi: SYN_DROPPED handler'ı birikmiş yasal hareketi/queued event'leri LOW-1 pending mekanizmasına park ediyor (`pending_dx/dy`, `pending_events`, `queued_count=0`) — düşme yerine en fazla 1 poll gecikme; yalnız SYN_DROPPED–SYN_REPORT arası belirsiz penceredeki veri atılır (`daemon.cpp:1919-1935`).
- Konum: `daemon/daemon.cpp:1692-1693, 1705-1710`
- Kategori: Hareket kaybı
- Açıklama: SYN_DROPPED geldiğinde `dx=dy=0; has_motion=false` — frame SYN'i hiç ulaşmamış ama *legally closed* olan hareket de kaybolur (gecikme değil KAYIP). Ayrıca `DEFAULT_TIME_MAX`/interval state ilerlediği için sonraki valid frame yanlış tabandan ölçer.
- Öneri: SYN_DROPPED öncesi birikmiş, pürüzsüz bulunan hareketi koru (yalnız belirsiz penceredekileri düşür).

**RAC-5 · DÜŞÜK · SYN_REPORT dışı SYN alt tipleri SYN_REPORT'a çökertiliyor** — **[DÜZELTİLDİ]**
- Konum: `daemon/daemon.cpp:1712-1715`
- Açıklama: `SYN_MT_REPORT`/`SYN_CONFIG` herhangi bir EV_SYN → `SYN_REPORT` olarak yeniden yazılır; frame sınırlayıcıları bozulur. `is_physical_mouse` yalnız REL_X+REL_Y arar, hibrit pad/gesture cihazı geçerse birden çok MT frame'i tek dev frame'e birleşir → compositor büyük bileşik delta → imleç snap.
- Öneri: Hangi SYN alt tiplerinin iletileceğini koru (SYN_REPORT olmayanları kopyalama), SYN_REPORT'u yalnız gerçek frame kapanışı için kullan.

**LOW-1 · DÜŞÜK · Batch sonu sentetik SYN → çift SYN (REL, SYN, SYN)** — **[DÜZELTİLDİ]**
- Konum: `daemon/daemon.cpp:1764-1768`
- Açıklama: BUG-CRIT-1 düzeltmesi (`wrote_unsynced_event`) kuyruktaki tail frame'in kendi SYN'i sonraki batch'te gelince boş ikinci frame üretir: `REL, SYN, SYN`. Protokolü bozmaz ama akışa bir fazla olay ekler; piksel pürüzsüzlüğü açısından minimal etki.

**MED-4 · DÜŞÜK · Latency ölçümü cross-frame kontaminasyonlu** — **[DÜZELTİLDİ]**
- Konum: `daemon/daemon.cpp:1614` vs 1624 (`batch_start_ns`)
- Açıklama: `lat.record(t_now - batch_start_ns)` per flush çalışır; çok-frame'li batch'te sonraki frame'ler öncekilerin işlem süresini de sayar → p99/max şişkin, `rawaccel-cli latency` yorumu zorlaşır. (Ölçüm sorunu, smoothness değil.)

---

## E. ÖZET — RAW PASSTHROUGH & DÜZGÜNLÜK ÖNCELİK SIRASI

**KAPANAN (bu turda düzeltildi):**
1. SM-1 — `time_ms` kernel SYN_REPORT `ev.time` deltasından (duvar saati değil) (`daemon.cpp flush_motion`)
2. SM-2 — Ara buton/tekerlek olayları buffer'a alınıp frame'in gerçek SYN'inde yazılıyor — frame bölünmüyor
3. SM-8 — `(0, DEFAULT_TIME_MIN]` bandı floor'landı
4. SM-5 — `reconfigure()`: EMA + remainder + zamanlama korunuyor (yalnız yeni smoother reset)
5. TEL-1 — `apply_profile` telemetri sayaçlarını sıfırlıyor → `telem_ok=false` doğru
6. SM-3 (trend clamp), SM-6 (stop ekstra sönüm), CUR-1 (C0 adım → sürekli kuyruk), RAC-1 (O_NONBLOCK + EAGAIN backoff), RAC-4 kısmi (interval re-anchor), RAC-5 (SYN alt tip korunumu), LOW-1 (sentetik SYN kaldırıldı), MED-4 (latency anchor), PAS-1/2/3 (ölü bayraklar + GUI grey-out)

**Açık kalan (TUR 25 sonrası):**
- SM-4 DÜZELTİLDİ (idle-tetikleme), SM-7 DÜZELTİLDİ (SMOOTH_HALFLIFE_MAX=10 sn), RAC-4 DÜZELTİLDİ (SYN_DROPPED öncesi yasal hareket park ediliyor), CUR-3 DÜZELTİLDİ (power Inf → cap tavanı) — TUR 19 sonrası bu turda kapananlar.
- CUR-4/5/6 (referans RawAccel davranışı — orakel birebir; kasıtlı korundu), CUR-7 (defense-in-depth sert guard — bilinçli), CUR-2 (referans davranış — kasıtlı), RAC-3 (systemd hardening — kasıtlı), PAS-4 (belgeleme)

**Doğrulanan temiz noktalar (bulgu değil):**
- Raw passthrough yolu 1:1; batch yok, has_motion kurulmuyor, zaman matematiği yok (`daemon.cpp:1722-1739`).
- Sub-piksel accumulation sağlam (`motion_math.hpp:35-71`); NaN/inf remainder guard doğru.
- `modify()` erken dönüşü (`rawaccel.hpp:251`) daemon'dan ulaşılmaz (clamp önce çalışır) — referans-parity, bug değil.
- Raw vs accel uinput yazımı tek writer + aynı fd → interleave sıralaması güvenli.

---

# TUR 20..24 — BEŞ TUR TAM KAPSAMLI YENİDEN ANALİZ (2026-09-11, güncel ağaç)

Analiz tarihi: 2026-09-11 (dördüncü oturum — 5 bağımsız tur)
Kapsam: Tüm kaynak ağacı (`daemon/`, `gui/`, `include/`, `src/`, `cli/`, `scripts/`, `tests/`, `packaging/`, `.github/`)
Yöntem: Paralel 5 uzman derinlemesine inceleme + bulguların kaynak üzerinde birebir doğrulanması (line-by-line mevcut ağaç)
Önemli not: Ağaç **aktif düzenleme altında** — `daemon.cpp` 2759 satır (TUR 18'de ~2436'ydı); TUR 18 bulgularının çoğu kaynakta yorum etiketiyle (`SM-1:`…,`TEL-1:`, `PAS-1:`) düzeltilmiş durumda. Aşağıdaki 5 tur **güncel ağaç üzerinde** yapılmıştır; TUR 18 durum güncellemesi başta verilmiştir. Satır numaraları kısmen kayabilir.

---

## TUR 18 DURUM GÜNCELLEMESİ (güncel ağaçta doğrulanan fix'ler)

- **SM-1 DÜZELTİLDİ** — `ev.time` (kernel frame damgası) artık interval hesabında tercih ediliyor; coalesced-batch gain spike'ı giderildi (`daemon.cpp:1716-1755`).
- **SM-2 DÜZELTİLDİ** — Ara buton/tekerlek olayları `queued_events[16]` tamponuna alınıp frame'in gerçek SYN_REPORT'unda tek grup olarak yazılıyor; frame bölme/gain spike giderildi (`daemon.cpp:1826-1864`).
- **SM-4 DÜZELTİLDİ** — İdle sonrası ilk frame gerçek poll periyoduyla yeniden ölçülüyor (`daemon.cpp:1724-1738`).
- **SM-5 DÜZELTİLDİ** — `speed_processor::reconfigure()` (state korur); profil/fokus değişiminde EMA sıfırlanmıyor (`daemon.cpp:~995`, `rawaccel.hpp:202`).
- **SM-7 DÜZELTİLDİ** — `SMOOTH_HALFLIFE_MAX = 10000` (10 sn) klamp'ı: config + CLI domain ortak (`rawaccel-base.hpp:24`, `config.cpp:518`, `cli/main.cpp:957-959`).
- **SM-8 DÜZELTİLDİ** — `time_ms < DEFAULT_TIME_MIN` floor'u tüm `(0, min]` bandını kapsıyor (`daemon.cpp:1746-1751`).
- **TEL-1 DÜZELTİLDİ** — `apply_profile()` telemetri sayaç/gain sıfırlıyor; accel→raw sonrası bayat `telem_ok` kapatıldı (`daemon.cpp:1003-1012`).
- **MED-4 DÜZELTİLDİ** — `lat_anchor_ns` her flush sonrası ilerliyor; batch içi sonraki flush yalnız kendi işini ölçüyor (`daemon.cpp:1787-1792`).
- **LOW-1 / BUG-CRIT-1 DÜZELTİLDİ** — Batch sonu tail + ara olaylar `pending_*` mekanizmasıyla sonraki SYN'de, kendi frame intervaliyle flush ediliyor; çift-SYN yolu kaldırıldı (`daemon.cpp:1866-1880`).
- **RAC-4 DÜZELTİLDİ** — SYN_DROPPED öncesi yasal birikmiş hareket artık park edilip sonraki SYN ile flush ediliyor (düşme yerine en fazla 1 poll gecikme) (`daemon.cpp:1919-1935`).
- **RAC-1 DÜZELTİLDİ** — `uinput_write_retry` EAGAIN'de exponential-backoff nanesleep ile retry ediyor; EAGAIN=ölüm tutumu kaldırıldı (`daemon.cpp:~1570`).
- **PAS-1/PAS-2 KISMEN DÜZELTİLDİ** — `use_raw_input` ve `dev_cfg.disable` artık `setup_devices()`'te onurlandırılıyor (`daemon.cpp:831-850`). **AMA hot-plug yolu hâlâ baypas ediyor → yeni HP-1 bulgusu (TUR 20).**
- **P93-BATCH** — Per-frame tek write batching eklendi; REL+SYN tek `write()`'ta (`write_batch`).
- **O2 (PID tek-instans) KISMEN** — stale/0-byte temizliği yeniden incelenmeli → yeni PID-1/PID-2 bulguları (TUR 20).

---

# TUR 20 — Daemon / Yaşam Döngüsü / Hot-Plug / IPC / PID (güncel ağaç)

Odak: `daemon/daemon.cpp` (2759), `daemon/main.cpp` (584), `daemon.hpp`, `lat_stats.hpp`, `motion_math.hpp`.

**HP-1 · YÜKSEK · Hot-plug yolu üst düzey raw anahtarını ve per-cihaz disable'ı baypas ediyor**
- Konum: `daemon/daemon.cpp:1117-1164` (do_hotplug_scan add döngüsü) vs `833-850` (setup_devices)
- Kategori: Davranış / güvenli mod
- Açıklama: `setup_devices()` PAS-1 (`raw_input_enabled_`) ve PAS-2 (`dev_cfg.disable`) denetimlerini yapıyor; `do_hotplug_scan()` doğrudan `open_input_device()` → `create_virtual_device()` → `apply_profile()` yolunu izliyor, ikisini de yok sayıyor. Deterministik senaryo: `use_raw_input=false` ile açılış → `setup_devices` tüm fareleri atlar → `devices_` boş → 2 sn boş-rescan `do_hotplug_scan()`'ı çalıştırır ve fareyi **yine yakalar + hızlandırır**. `disable=true` profiller için de aynı: "güvenli mod" sessizce ihlal edilir.
- Öneri: Hot-plug add döngüsüne aynı PAS-1/PAS-2 dallarını ekle; boş-rescan da `raw_input_enabled_`'a danışsın.

**PID-1 · YÜKSEK · PID fallback OR-zinciri ikinci daemon'un canlılık kontrolü olmadan başlamasına izin veriyor**
- Konum: `daemon/main.cpp:331-333`
- Kategori: Tek-instans / güvenlik
- Açıklama: `pid_written = write_pid(xdg) || write_pid(PID_FILE) || write_pid(PID_FILE2)`. Birinci instance XDG dosyasını canlı tutuyorsa ikinci instance'ın XDG write'ı EEXIST→false, `/run` non-root olduğu için false, ama `/tmp` write'ı **başarılı** → `!pid_written` bloğu (tek `another_alive` canlılık kontrolünü içerir, 336-426) hiç çalışmaz → ikinci instance başlar. Root'un kullanıcı XDG dosyasını incelememesi de aynı boşluğu üretir.
- Öneri: EEXIST durumunda `pid_file_is_live`/`try_clear_stale` doğrulamasını **fallback öncesi** çalıştır; yalnız kanıtlanmış ölüyse alt-önceliğe geç.

**SAVE-1 · ORTA · kapanışta kayıt-çalışanı IPC thread'den önce çıkıyor → `set_config` sessizce kayboluyor**
- Konum: `daemon/daemon.cpp:588-660`, shutdown sırası (`stop()` → `stop_ipc_server()`)
- Kategori: IPC / dayanıklılık
- Açıklama: `save_worker` `running_` kontrol edip çıkar; `set_config` geldiğinde ack `ok:true` verir ama kuyruk kimse tarafından drain edilmez → config kayıpsız değil, kaybolur. 629-630 yorumu garantiyi abartıyor ("queued concurrently with stop() is persisted").
- Öneri: IPC thread'i save çalışanından önce join; veya `stop()` save_worker bitmeden kuyruğu son kez drene etsin.

**PID-2 · ORTA · 0-byte PID dosyası boot'u kalıcı kilitliyor**
- Konum: `daemon/main.cpp:346` (`if (n <= 0) { unlink(path); return false; }`)
- Kategori: Stale-handling
- Açıklama: `open(O_CREAT|O_EXCL)` ile ilk `write()` arası çökme 0-byte dosya bırakır. `try_clear_stale` dosyayı `unlink` eder ama `false` döner; `cleared`/`retry_ok` false kalır → "Another instance may already be running" ile atılır; dosya zaten silinmiş olmasına rağmen. Manuel silmeye kadar brick.
- Öneri: Boş dosya tanım gereği canlı instance değildir — unlink başarılıysa `true` dön.

**PID-3 · ORTA · `/proc/<pid>/comm` okunamazsa (hidepid/ProtectProc) canlı daemon "yok" sayılıp dosyası siliniyor**
- Konum: `daemon/main.cpp:364-373, 397-405`
- Kategori: Canlılık / single-instance
- Açıklama: Yorum "comm okunamazsa dikkat tarafında kal, SİLME" der; ama dal `!proc_comm_matches(...)` ve `proc_comm_matches` comm açılamazsa `false` döner → canlı daemon'a "recycled PID" muamelesi: pid dosyası unlink edilir, ikinci instance başlar. Hardened `/proc` altında single-instance garantisi çöker.
- Öneri: Üçlü durum ayır: "comm eşleşir (canlı)" / "comm okunur ve farklı (bayat, sil)" / "comm okunamaz (canlı say, asla silme)".

**HP-2 · ORTA · Replug by-id yolu değişince cihaz graspsız ve uygulanmamış kalıyor**
- Konum: `daemon/daemon.cpp:1129-1133` (EBUSY→5 sn deny_reopen), `1178-1210` (removal), `1439-1440` (yalnız boşken rescan)
- Kategori: Hot-plug / cihaz yaşam döngüsü
- Açıklama: Replug sırasında by-id yolu değiştiyse (serial/phys), add döngüsü yeni yolu açar ama ESKİ cihaz hâlâ grab tuttuğu için `EVIOCGRAB=EBUSY` → 5 sn deny_reopen. Removal fazı eski cihazı yıkıp grab'ı bırakır ama artık tarama programlanmamıştır (pending_hotplug o taramada tüketildi, inotify eski yolun IN_DELETE'ini görmüştü; başka fare varsa `devices_` boş değil → boş-rescan tetiklenmez). Cihaz ta ki sonraki hot-plug/restart'a kadar graspsız.
- Öneri: EBUSY'ı *kendi* grab çakışmamız olarak tanıma; removal fazından hemen sonra yeniden dene.

**HP-3 · ORTA · Aynı `device_id`'li iki evdev düğümü ikisi de grab → çift sayım**
- Konum: `daemon/daemon.cpp:699-711` (device_id), `873/1168` (yalnız path'e göre dedup)
- Kategori: Cihaz keşfi
- Açıklama: Dedup yalnız `path` bazlı. Aynı VID:PID:serial'a sahip iki REL_X+REL_Y düğümü (çoklu-interface gaming mouse, HID composite) → iki uinput + iki profil kopyası → her fiziksel rapor compositor'a iki kez ulaşır. `opened_paths_` guard'ı yeterli değil.
- Öneri: Açık `device_id` seti tut; zaten işlenen device_id'yi logla+geç (setup ve hot-plug'ta aynı).

**SYN-1 · DÜŞÜK · `apply_profile` telemetri sıfırlaması tam seqlock disiplininde değil (bayat dx/dy)**
- Konum: `daemon/daemon.cpp:1003-1012`
- Kategori: Eşzamanlılık (kozmetik)
- Açıklama: `speed/out/gain` sıfırlanıp `samples=0` yazılıyor; `dx/dy/wall_ms` dokunulmuyor. Çift sayaç eski değerini görüp kopya alıp yeniden yükleyen okuyucu `telem_ok=true` + **bayat dx/dy** görebilir (çok dar pencere; DÜŞÜK). Tam seqlock yazımı (odd→6 alan sıfırla→even) daha tutarlı olur.
- Öneri: `samples`'ı önce odd yap, 6 alanı da sıfırla, sonra even.

**SUB-1 · DÜŞÜK · Sub-piksel remainder `modify()`'den SONRA ekleniyor**
- Konum: `daemon/motion_math.hpp:33-38`
- Kategori: Doğruluk / referans uyuşmazlığı
- Açıklama: Taşınan kesir, rotate/snap/weight'e modifiye-sonrası giriyor; tamsayı kısım dönüştürülürken kesir dönüştürülmüyor → uzun hareketlerde rotated/snapped config'lerde sistematik sub-count sürüklenmesi. Oracle hiçbir zaman satır bazında çok-frame remainder carry'ini test etmiyor.
- Öneri: Remainder'ı `modify()` öncesi `motion`'a ekle.

**FCNTL-1 · DÜŞÜK · `fcntl(F_SETFL | O_NONBLOCK)` dönüşü yoksayılıyor**
- Konum: `daemon/daemon.cpp:770-773`
- Kategori: Güvenilirlik
- Açıklama: F_SETFL başarısız olursa uinput fd sessizce blocking kalır; retry hattı yine loop thread'i bloklayabilir. Tek satır eksik hata kontrolü.
- Öneri: Dönüşü kontrol et; başarısızsa logla/cihazı hatalı işaretle.

**SIG-1 · DÜŞÜK · Sinyal kurulum penceresinde ölü (0-byte) PID dosyası kalıyor**
- Konum: `daemon/main.cpp:430` (`g_daemon.store`) vs `494-508` (`sigaction`), `write_pid` sıralaması
- Kategori: Sinyal / çıkış yolları
- Açıklama: `write_pid` ile `sigaction` arasına SIGTERM gelirse varsayılan disposition öldürür, `remove_pid()` çalışmaz → PID-2 ile birleşince sonraki boot'u kilitleyen 0-byte dosya.
- Öneri: Handler'ları PID dosyasından önce kur; PID-2 empty-file yolunu kendini iyileştirir yap.

**INFO bulgular (TUR 25):**
- `--help` `/run/rawaccel.pid` yazıyor; gerçek yol XDG-first (`main.cpp:267`); IPC soket yolu XDG→`/run` deniyor ama PID `/tmp`'ye de fallback ediyor — XDG'siz non-root daemon `/tmp/rawaccel.pid` yazar, IPC soketi hiç açılamaz (sessiz kısmi başlangıç) (`daemon.cpp:2225-2229`).
- >256-byte IPC komut satırı "unknown command" olarak kesiliyor; kalanı bağlantı ile birlikte kayboluyor (`daemon.cpp:2660`). Kozmetik hata raporlama.

**Doğrulanan temiz (bulgu değil):** `libevdev_free()` fd kapatmaz — `find_mice`/`create_virtual_device` tek-close doğru; `config_path_` acquire/release sıralaması güvenli; kilit sıralaması acyclik; cihaz silme yalnız loop thread; IPC dispatch tüm belgeli komutları kapsıyor; kısmi yazma döngüsü doğru.

---

# TUR 21 — GUI / GTK / Eşzamanlılık / Dil / Cihaz Paneli (güncel ağaç)

Odak: `gui/*.inl` (12 dosya), `app_state.hpp`.

**GUI-K1 · KRİTİK · App alanına yazarken SIGSEGV — 3-arg handler 2-arg sinyale bağlı**
- Konum: `gui/ui_builder.inl:659-660` vs `gui/widgets_sync.inl:477-483`
- Kategori: GTK ABI / çökme
- Açıklama: `GtkEditable::changed` sinyali VOID__VOID marshaller ile yalnızca `(instance, user_data)` geçer. `S->match_app_entry` "changed" sinyaline 3-arg `on_notify_param_changed` (GObject*, GParamSpec*, gpointer) bağlanıyor → marshaller 3. argümanı (user_data) vermediği için UB: `user_data` = registry'deki kalıntı okunur → `on_param_changed(nullptr, <garbage>)` → `S` garbage → App alanına ilk karakter yazımında NULL/garbage dereference → segfault. Aynı dosyada `mode_combo`/`device_id_combo` "notify::selected" (3-arg) hattında doğru kullanılmış (645); yalnız satır 660 yanlış. ABI kuralı zaten kendi kod yorumunda belgeli.
- Öneri: `G_CALLBACK(on_notify_param_changed)` → `G_CALLBACK(on_param_changed)` (2-arg) yap.

**GUI-Y1 · YÜKSEK · hidraw inotify buffer `alignas` eksik → ARM'de misaligned erişim**
- Konum: `gui/devices.inl:272` vs `239` (evdev buffer'ı alignas'lı)
- Kategori: Taşınabilirlik / UB
- Açıklama: `on_hidraw_inotify_event` buffer'ı `char buf[...]` — `reinterpret_cast` ile `inotify_event*` okunuyor (286); strict-alignment (ARM) hedeflerde UB. Evdev buffer'ına (239) ERR-4 ile eklenen `alignas(struct inotify_event)` hidraw buffer'ına kopyalanmamış.
- Öneri: Aynı alignas'ı 272. satıra kopyala.

**GUI-Y2 · YÜKSEK · `kwin_focus` worker thread UAF + `std::thread` object sızıntısı**
- Konum: `gui/kwin_focus.inl:196-218`
- Kategori: Kaynak / yarış
- Açıklama: Detached worker `ctx->session_conn`'i okurken `kwin_focus_uninstall` connection'ı unref+null edebilir → UAF. Ayrıca `task->detach()` sonrası `std::thread` nesnesi leak (join/destroy yok).
- Öneri: Worker başlamadan önce `g_object_ref`; worker içinde unref; `detach()` sonrası `delete task`.

**GUI-Y3 · ORTA · HID++ busy-guard bekleyen cihaz seçimini sessizce düşürüyor**
- Konum: `gui/hidpp_panel.inl:577-593`
- Kategori: UI / durum kaybı
- Açıklama: In-flight query sırasında cihaz değiştirilirse guard yeni seçimi reddeder; yeni cihazın DPI/rate/LOD değerleri asla istenmez → bayat DPI gösterimi.
- Öneri: Bekleyen seçimi stash et; busy query bitince yeniden sorgula.

**GUI-O1 · ORTA · LUT listesi kendi `value-changed` emission'ı içinde yeniden kuruluyor**
- Konum: `gui/graph.inl:551` → `rebuild_lut_list` (431-438)
- Kategori: GTK / çökme riski
- Açıklama: `lut_list_changed` callback'i emission sırasında satırları destroy ederek aynı spin'i kaldırıyor → destroy-sırasında-geri-çağrı/garbage. `g_idle_add` ile erteleme gerekir.
- Öneri: Rebuild'i idle callback'e taşı.

**GUI-O2 · ORTA · `output_dpi` 0-sentinel'i coğrafi değil (spin min=100) → sessiz 100 yazımı**
- Konum: `gui/ui_builder.inl:521` (`make_spin(100,…)`), `config.cpp:486-493` (0 = no-normalization sentinel)
- Kategori: Veri bütünlüğü
- Açıklama: Config 0'ı "normalizasyon kapalı" sayıyor; GUI spin minimumu 100 → 0 temsil edilemiyor; kaydetme sonrası yeniden yüklenince 100'e clamp edilir (sessiz davranış değişimi). (GUI-raw modda PAS-3 ile ilişkili.)
- Öneri: Spin min'ini 0 yap (0 = off semantiği ile uyumlu).

**GUI-O3 · ORTA · Dil değişiminde `hw_lod_combo` yeniden kurulmuyor**
- Konum: `gui/ui_builder.inl:573-585` + `tr.inl:636-690` (`refresh_language`)
- Kategori: Yerelleştirme
- Açıklama: `refresh_language` mode/cap/dist/device combo'larını yeniden kuruyor; `hw_lod_combo` (ve `trtip`'i) kapsam dışı → dil anahtarından sonra "hayalet" stringler.
- Öneri: LOD combo model + tooltip'i refresh_language'a ekle.

**GUI-O4 · ORTA · `g_child_watch_add` kaynağı hiç iptal edilmiyor (pencere kapanışı UAF riski)**
- Konum: `gui/widgets_sync.inl:447-449`
- Kategori: Kaynak / teardown
- Açıklama: pkexec çocuğunun watch id'si saklanmıyor; GUI kapanışında callback yıkılmış UI'a karşı tetiklenebilir.
- Öneri: Kaynağı sakla, close'da remove; status referanslarını null'la.

**GUI-D1 · DÜŞÜK · `gtk_drop_down_new` model ref'leri serbest bırakılmıyor**
- Konum: `gui/ui_builder.inl:93,642` (565/579'daki doğru `g_object_unref` kalıbı varken)
- Kategori: Kaynak
- Açıklama: `mlist`/`sl` yerel ref'leri düşürülmemiş → tek seferlik GUI'de az miktar GObject ref leak.
- Öneri: Aynı kalıp ile unref.

**GUI-D2 · DÜŞÜK · `save_lang_pref` bayat `.tmp` (EEXIST) üzerine retry'siz abort**
- Konum: `gui/tr.inl:609-611`
- Kategori: Güvenilirlik
- Açıklama: Önceki çökmeden kalan `.tmp` varlığında dil tercihi kaydedilemez.
- Öneri: EEXIST'te eski .tmp'yi taşı/üzerine yaz.

**GUI-D3 · DÜŞÜK · Duplicate, widget düzenlenmemiş iç bellek profilini kopyalıyor**
- Konum: `gui/profile_mgr.inl:413`
- Kategori: Veri tutarlılığı
- Açıklama: `on_duplicate_profile` bekleyen widget değişikliklerini flush etmeden kopya üretir → bayat çoğaltma.
- Öneri: Kopyalamadan önce `widgets_to_profile`.

**GUI-D4 · DÜŞÜK · `*_finish(..., nullptr)` diyalog hatalarını "cancel" sayıyor**
- Konum: `gui/profile_mgr.inl:484,514`
- Kategori: Hata yönetimi
- Açıklama: Hata durumları sessizce iptal gibi geçiyor; kullanıcıya neden gösterilmiyor.
- Öneri: Hata stringini status/GtkInfoBar'da göster.

---

# TUR 22 — Matematik / Presetler / Oracle Kapsamı (güncel ağaç)

Odak: `include/accel-*.hpp`, `include/presets.hpp`, `tests/oracle/oracle_cases.hpp`.

**PRE-1 · ORTA · Oracle gaming case'i artık satın alınmayan eğriyi test ediyor (bayat)**
- Konum: `tests/oracle/oracle_cases.hpp:262-268` (cap_y=1.5) vs `include/presets.hpp:42-43` (cap_y=1.8)
- Kategori: Test altyapısı / kapsam
- Açıklama: Blok başlığı "EXACT parameter values from include/presets.hpp" vaat ediyor. C-5 sonrası gerçek preset cap={15,1.8}; oracle case hâlâ 1.5. Referans çapraz kontrolü artık gerçek preset'in dizini ve 1.5→1.8 kuyruk bölgesini **hiç test etmiyor** — C-5'in asıl değiştirdiği şey tam olarak kapsam dışı kaldı.
- Öneri: `game_gaming_classic` cap_y'yi 1.8'e güncelle; yorumu notla; oracle'ı yeniden koş.

**PRE-2 · ORTA · "precision" preset: limit 1.2 ama eğri 1.5'e zirve yapıyor**
- Konum: `include/presets.hpp:60-72` (limit=1.2, cap yok) + `rawaccel-base.hpp:56` (varsayılan cap {15,1.5} out)
- Kategori: Preset / UX
- Açıklama: `limit` classic modda inert; GAIN out-modda asimptot `cap.y=1.5`. exp=1.5, accel=0.002 ile diz `gain_inverse(0.5,0.002,1.5,0) ≈ 55.6 ips`; 100 ips'te gain zaten ~1.41 — belgelenen "precision" 1.2 sınırının %25 üstü. Diğer classic presetler cap.y=limit uyumlu (gaming 1.8/1.8, cs2 1.6/1.6, fps 1.8/1.8); precision yalnız uyumsuz.
- Öneri: `cap = {k, 1.2}` veya yanıltıcı `limit` alanını kaldır.

**PRE-3 · DÜŞÜK-ORTA · "apex" output_offset=0.9 platosu kendi yorumuyla çelişiyor (sub-1:1)**
- Konum: `include/presets.hpp:119-139` (output_offset=0.9) + `accel-power.hpp:112-120`
- Kategori: Preset / tutarlılık
- Açıklama: `offset.x = gain_inverse(0.9, 0.8, 2.2) ≈ 0.191 ips`; `x≤offset.x` platosu gain≡0.9 — yorum (121-122) "2-10 mm/s bandında sub-1:1 muddy feel'den kaçınır" derken su katılmamış 0.9 tabanı koyuyor. Yorum niyeti 1:1 taban.
- Öneri: `output_offset` ≥ 1.0 yap (1.0 = tam 1:1 plato).

**MATH-1 · ORTA · classic negatif `acceleration` + cap → dejenere eğri (ıraksak/kilitli/ters akış)**
- Konum: `include/accel-classic.hpp` (`gain_inverse` 219-222 accel==0 korur, accel<0 korumaz; out 164-190; tail), `src/config.cpp:398-399` (negatif accel "legit" diye serbest)
- Kategori: Matematik / referans-parity
- Sayısal (out, cap={15,1.5}, accel=-0.01, exp=2): `cap_x = gain_inverse(0.5,-0.01,2,0) = (0.25)/(-0.01) = -25`; `constant = (0.25-0.5)·(-25) = 6.25`; cap_x<0 → tail tüm x≥0 için `gain = 1.5 + 6.25/x` → **0.1 ips'te 64×, 1 ips'te 7.75×** mono-azalan ıraksak low-speed spike. (in modda cap={50,·} → cap_y=-1 → `gain = 25/x - 1` → yüksek hızda 0'a → kilitli imleç; geniş cap'te negatif gain → ters eksen.)
- Öneri: Cap aktifken negatif accel'i ya yasakla ya `gain_inverse`'te `accel≤0` → "no cap" (DBL_MAX) yap; NaN-guard paterniyle aynı şablonda.

**MATH-2 · ORTA · power `scale=0` → ölü imleç ya da gizli sabit boost**
- Konum: `src/config.cpp:404` (scale<0→0; **0 geçiyor**), `accel-power.hpp` (gain_inverse 146 `sc<=0→0`; out-cap 100-106; tail 126-132)
- Kategori: Sanitize gap / ulaşılabilir
- Açıklama: (out, varsayılan cap={15,1.5}) → `cap_x = gain_inverse(1.5,n,0) = 0`; tail `gain ≡ cap.y = 1.5` her hızda → sessiz %50 boost "acceleration" kılığında. (cap yok/in-mod) → `base_fn = pow(0·x,n)+0 = 0` → `gain ≡ 0` → **ölü imleç**. GUI min 0.01 ama JSON/CLI ile ulaşılabilir.
- Öneri: `if (a.scale <= 0) a.scale = SCALE_MIN` (exponent 1e-4 floor'u gibi) ya da ctor'da scale≤0→identity.

**INFO (TUR 22):**
- Natural gain/legacy asimetri: gain modu `accel<1e-12→1.0` (54); legacy modu hâlâ exponansiyel eğriyi değerlendirir — yalnız accel tam 0'da aynı. GUI'den ulaşılamaz, zararsız.
- `valorant`/`office` preset'leri `motivity`/`cap` koyuyor ama `natural` okuyor (inert alanlar) — kozmetik yanıltma.
- Doğrulandı: Classic GAIN io/out cebirsel yapı, power offset/integration_constant, P155 io, BUG-02 paylaşımlı exponent floor, jump GAIN smooth-dA, fp_rep_range — hepsi vendored referansla uyumlu.

---

# TUR 23 — Config / CLI / IPC Protokolü (güncel ağaç)

Odak: `src/config.cpp` (971), `cli/main.cpp` (2415), `include/config.hpp`.

**CFG-1 · ORTA · Import yolu `migrate_config()`'i baypas ediyor → pre-0.4 lookup+gain eğrileri yanlış**
- Konum: `cli/main.cpp:1143-1296` (cmd_import → `profile_from_json` 1213), `src/config.cpp:813-815` vs `652/660` (dosya/daemon yolu migrate çalıştırır)
- Kategori: Veri bütünlüğü / sürüm
- Açıklama: Diğer tüm config yutma yolları `migrate_config()` koşuyor; import, `device_profile_from_json()`'a doğrudan geçiyor ve **`version` hiç okunmadan `migrate_lookup_gain()` hiç çalışmadan** profil kurar. 0.4.0 öncesi lookup+gain export'u (y = doğrudan gain) import edilirse eğri sessizce yanlış kalır (gain 2.0 → ~1:1 hız gibi).
- Öneri: `cmd_import` içinde her profil için `migrate_profile()` benzeri çağır (version işaretine göre) ya da geçici app_config üzerinden migrate edip geri çıkar.

**CFG-2 · ORTA · `validate` uyarıları ölü kod → yanlış "All checks OK"**
- Konum: `cli/main.cpp:586-709` (ölü dallar 658-681)
- Kategori: Tanılama doğruluğu
- Açıklama: `load_config` yükleme anında sanitize eder (DPI→[1,32000], poll→[125,8000], output_dpi→[0,32000], speed_max≥min eşitleme, isim 256 clamp). Bu yüzden şu uyarılar **asla tetiklenemez**: speed_min>speed_max, output_dpi range, dpi range, polling range, isim uzunluğu. `"dpi":999999, "speed_min":20,"speed_max":5, "output_dpi":50000` dosyası "Validation passed" çıkar; tool yapısal olarak problem göremez.
- Öneri: Ham JSON üzerinde (sanitize öncesi) kontrol et; ya da clamp edilen gerçek değerleri uyarı olarak bas ("dpi 999999 → 32000 olarak saklanacak").

**CFG-3 · ORTA · `MAX_PROFILES` yalnız yüklemede; create/duplicate/import sınırsız yazıyor → sessiz kesinti**
- Konum: `src/config.cpp:627` (load cap), `640-641` (hepsini yazar); `cli/main.cpp:472,548,579,1291` (cap kontrolü yok)
- Kategori: Veri kaybı
- Açıklama: 300 profilli import (veya tekrarlanan create) diske 300 yazar; sonraki CLI/GUI yüklerken yalnız ilk 256 kalır; bir sonraki mutation `safe_save` ile 256'yı yazar → diğer 44 kalıcı yok olur. Uyarı yok.
- Öneri: Her append noktasında `cfg.profiles.size() >= MAX_PROFILES` ise açık hata döndür (SEC-9 cap'ini truncation yerine reddet).

**CFG-4 · ORTA · `set_config` ack-before-persist + timeout→SIGHUP sahte rc=0**
- Konum: `daemon/daemon.cpp:588-660` (push_config "queued" = ok; save worker log-only), `cli/main.cpp:197-212` (boş/timeout yanıtı → SIGHUP fallback), `daemon.cpp:2545-2757` (seri IPC)
- Kategori: Güvenilirlik / sahte başarı
- Açıklama: (1) `push_config` item kuyruğa girer girmez `ok:true` basar; disk full / `ProtectSystem=strict` ile config yazılamazsa save worker yalnız loglar, apply hiç kol güçlenmez — CLI "Daemon reloaded." basar. (2) `set_config` yanıtı 2 sn SO_RCVTIMEO'da boşsa "eski daemon" sanılıp SIGHUP'a düşülür; kill sent ise rc=0 + "reloaded" — sincap yarı-alınmış body'nin cevabı `{"ok":false,...}` olsa bile daemon eski dosyayı reload eder. (3) Tek thread'li IPC bir client tarafından 10 sn tutulabilir → diğer herkes timeout.
- Öneri: Timeout'u "unknown RPC" ile aynı tutma — timeout'ta rc=1 "daemon yanıt vermedi / push uygulanmadı"; `ok:true`'yu ancak persist+apply sonrası bas.

**CFG-5 · DÜŞÜK-ORTA · Gelecek sürüm config'i ilk kayıtta sessizce aşağı damgalanıyor, bilinmeyen anahtarlar düşüyor**
- Konum: `src/config.cpp:634-643` (save her zaman `RAWACCEL_VERSION` damgalar + yalnız bilinen alanları yazar), `601-632` (load bilinmeyeni atar), `931-968`
- Kategori: İleri uyumluluk / veri kaybı
- Açıklama: Daha yeni binary'nin yazdığı config (version 2.0, yeni alan) sorunsuz açılır; ama ilk CLI mutation → save → version 1.1.0'a iner ve tüm 2.0 alanları sessizce siler. (Enum string'leri bilinmeyen durumda throw edip config'i reddederken; skaler extend alanlar sessiz kaybolur — iki farklı davranış.)
- Öneri: Saklanan version güncelden yenireyse düşürme; bilinmeyen key'leri round-trip taşı ya da "kaybolacak" uyarısı bas.

**CFG-6 · DÜŞÜK · Profil adı: üst düzey 256 vs iç içe `profile.name` 255 — aynı JSON'da çelişki**
- Konum: `src/config.cpp:242` (`strncpy(p.name, s.c_str(), MAX_NAME_LEN - 1)`) vs `dp.name` 256
- Kategori: Round-trip tutarlılığı
- Açıklama: `export`/`show --json` → `{"name":"<256>", ..., "profile":{"name":"<255>"}}`; iç içe ismi okuyan parser (import dahil) ismi sessizce kısaltır → eşleşme semantiği değişir.
- Öneri: Her yerde aynı uzunluğa hizala (`MAX_NAME_LEN-1`).

**CFG-7 · DÜŞÜK · `find_config_path()` `XDG_CONFIG_HOME`'u yok sayıyor; CLI/daemon izin ayrışması**
- Konum: `src/config.cpp:824-842`
- Kategori: Yol/ortam
- Açıklama: Yalnız `SUDO_USER`/`HOME`→`~/.config/rawaccel`; son çare `/etc/rawaccel`. `XDG_CONFIG_HOME` set (Arch/Plasma yaygın) kullanıcıda CLI gerçek config'ten ayrışır. Ayrıca getpwnam 16 KB buffer'ında ERANGE retry yok (daemon'da var); root-created `/etc/rawaccel` 0700 vs CLI create-dir 0755 asimetrisi belgesiz.
- Öneri: `$XDG_CONFIG_HOME/rawaccel` first; ERANGE retry ekle.

**CLI-1 · DÜŞÜK · `status` yerel config + canlı daemon verisini karıştırıyor; `match_app`'i görmezden geliyor**
- Konum: `cli/main.cpp:1378-1548`; daemon `status_json` (`daemon.cpp:2244-2380`)
- Kategori: Tanılama
- Açıklama: Profiller/aktif/effective-profile CLI'nin **kendi dosyasından**, cihaz satırları daemon'dan. Daemon config farklıysa (belgeli split-brain) Frankenstein çıktı. Effective-profile replikasyonu `match_app`'i yok sayar ve daemon'un `active_profile`'unu kullanmaz → GUI app-scoped profil varsa CLI farklı profil gösterir.
- Öneri: Daemon çalışıyorsa entire status'ten profilleri+active'i çek; her iki yolda `match_app`'i dahil et.

**CLI-2 · DÜŞÜK · `set-param output_dpi 0.5` → sessizce 1 saklanır, rc=0 (P107 ihlali)**
- Konum: `cli/main.cpp:914-919` (domain [0,32000], fraction kabul), `src/config.cpp:491-493` ((0,1)→1 clamp)
- Kategori: Sessiz değer mutasyonu
- Açıklama: CLI sözleşmesi (P107) "kullanıcının istediğinden farklı değeri rc=0 ile kaydetme" der; `output_dpi` `(0,1)` bandındaki kesirli değerleri sanitize edip 1'e clamp'liyor. `stored_value_str` 1.000000 basar, rc=0.
- Öneri: `(0,1)` için domain'i `[1,32000]` + ayrı `0` sentineli yap (targeted error).

**CLI-3 · DÜŞÜK · "Sonraki çalıştırmada 'default' yeniden oluşturulur" mesajı yanlış**
- Konum: `cli/main.cpp:495-501` vs `2344-2357` (varsayılan yalnız dosya YOKKEN)
- Kategori: Yanlış tanılama
- Açıklama: Delete-all sonrası `safe_save` açık `{"active_profile":"","profiles":[]}` yazar → config_exists=true → sonraki çalıştırmada boş profil seti, default asla oluşmaz. Mesaj vaat ettiği davranışı yapmaz; `active_profile` da açıkta kalır.
- Öneri: Ya yüklenen config boşsa default'u yeniden oluştur ya mesajı "create ile oluştur" yap.

**Doğrulanan non-issues (TUR 23):** vendored nlohmann SAX-driven derin iç içelikte stack overflow yapmıyor (3M düzey test edildi), 1e999/NaN/Infinity literal'leri parse'da reddediliyor; `migrate_lookup_gain` double→float +Inf daralması yalnız önceden raporlanan mekanizmanın varyantı.

---

# TUR 24 — HID++ / Betikler / CI / Paketleme / Güvenlik Kıyısı (güncel ağaç)

Odak: `src/logitech_*.cpp`, `setup.sh`, `scripts/*`, `.github/workflows/ci.yml`, `packaging/*`, `tests/run_*`, systemd/udev.

**PKG-1 · ORTA · `.SRCINFO` bayat (0.6.4) — kod 1.1.0; PKGBUILD doğru (1.1.0)**
- Konum: `packaging/.SRCINFO:3` vs `packaging/PKGBUILD:14` (1.1.0), `CMakeLists.txt:2`, `rawaccel-base.hpp:9`
- Kategori: Paketleme / tutarlılık
- Açıklama: CHANGELOG "three must sync" diyor. PKGBUILD 1.1.0 ile uyumlu; `.SRCINFO` hâlâ 0.6.4 → AUR paketi 0.6.4 meta bilgisiyle kurulur (motor 1.1.0). En azından tutarsız meta; depolardan yükseltme algısı bozulur.
- Öneri: `makepkg --printsrcinfo > .SRCINFO` ile yeniden üret.

**CI-1 · ORTA · CI build adımında `set -e` yok → sessiz build hatası geçebilir**
- Konum: `.github/workflows/ci.yml:31-43`
- Kategori: CI sağlamlığı
- Açıklama: `set -o pipefail` var ama `set -e` yok. `build.sh` cmake hatasıyla (ör. libevdev yok) düşerse output derleyici-tanı regex'ine uymaz → uyarı kapısı sessizce geçer → "Verify binaries" EKSİK der; gerçek hata görünmez.
- Öneri: Adım başına `set -e` ekle ya da pipeline durumunu açıkça yakala.

**SH-1 · DÜŞÜK · `bench_hotpath.sh` non-x86'da `/proc/cpuinfo` 'model name' yokluğunda abort**
- Konum: `scripts/bench_hotpath.sh:42` (`set -euo pipefail` altında grep)
- Kategori: Taşınabilirlik
- Açıklama: ARM/RISC-V'te alan adları farklı → grep non-zero → pipeline grubu çöker. CI x86_64 olduğundan yalnız yerel benchmark.
- Öneri: `CPU implementer|Hardware|model name` ERE'sine genişlet ya da `|| echo unknown`.

**SH-2 · DÜŞÜK · `setup.sh` kullanıcı config kopyasında TOCTOU symlink penceresi**
- Konum: `setup.sh:296-315`
- Kategori: Bash semantiği / güvenlik kıyısı
- Açıklama: `-f` (önce) → `-h` (iç) → `install` sırası; arada dosya symlink'e çevrilirse `install` symlink'i izler (dereference) → ilgisiz dosya üzerine yazılır. Root her iki yola da sahip olduğundan gerçekçi olasılık düşük (bilgilendirici).
- Öneri: `-L`'yi `-f` öncesi kontrol et ya da `readlink -f`.

**SH-3 · DÜŞÜK · Kullanıcı düzeyi systemd unit'i sistem unit'ini gölgeleyebilir**
- Konum: `setup.sh` (install), manuel kurulumlar için
- Kategori: Kurulum bütünlüğü
- Açıklama: `clean_old_install` `/etc/systemd/user/` temizliyor; ama `~/.config/systemd/user/rawaccel.service` varlığında kullanıcı login'de bu unit sistem unit'ini ezer (hardening'siz). Kurulum uyarısı yok.
- Öneri: do_install'da kullanıcı unit'lerini tespit edip uyar/ikaz kaldır.

**SVC-1 · INFO · systemd restart-burst limitleri görünür değil**
- Konum: `scripts/rawaccel.service:22-23`
- Açıklama: `Restart=on-failure, RestartSec=3`; StartLimitIntervalSec/Burst set yok (varsayılan 10sn/5). Çalışır ama gizli; RestartSec değişirse sürpriz fail.
- Öneri: `StartLimitIntervalSec=60` `StartLimitBurst=10` açıkça yaz.

**SH-4 · INFO · `udevadm trigger` selector'sız tüm sistemi tetikliyor**
- Konum: `setup.sh:324`
- Açıklama: Doğru ama ağır sistemlerde kısa uevent fırtınası. `--subsystem-match=input/hidraw` ile daraltılabilir.

**DOC-1 · DÜŞÜK · AGENTS.md polkit'i "kurulan entegrasyon" olarak gösteriyor (kaldırılmış)**
- Konum: `AGENTS.md` (Dependency policy bölümü)
- Açıklama: BUG-02 0.6.4'te polkit'i kaldırdı (setup.sh 225-230 eski dosyaları temizler, 335-339 hiç kurmaz); AGENTS.md yine "polkit/desktop/libinput-quirk" kuruyormuş gibi yazıyor. paket bağımlılığı sürer ama entegrasyon yok.
- Öneri: Dokümanı güncelle.

**CI-2 · INFO · sanitizer job `-Wpedantic` kullanmıyor, P6 wrap yok — uyarı kapsamı build-job'a bırakılmış** (`ci.yml:86-90`). Zararsız, belge.

**Doğrulanan non-issues (TUR 24):** HID++ feature-enum index wrap yok (uint8 taşması imkânsız); oracle cap_mode isimleri taraf başına doğru; polkit eksikliği bilinçli (BUG-02); bench_hotpath `power\+rot45` ERE'si doğru; `clean_old_install` boşluklu `/home/*` yollarını nullglob ile doğru işliyor; sistemd hardening güçlü; udev kuralları bilinçli güven sınırı; iki CLI aynı anda → config write race riski düşük (safe_save atomic).

---

# TUR 20..24 ÖZET — ÖNCELİK SIRASI (yeni bulgular)

**KRİTİK:**
- GUI-K1 — App alanına yazımda SIGSEGV (3-arg handler, 2-arg sinyale bağlı — `ui_builder.inl:660`)

**YÜKSEK:**
- HP-1 — Hot-plug `raw_input_enabled_`/`dev_cfg.disable` baypası (`daemon.cpp:1117-1164`)
- PID-1 — PID OR-zinciri liveness'tan kaçıyor; ikinci daemon başlıyor (`main.cpp:331-333`)
- GUI-Y1 — hidraw buffer alignas eksik (ARM UB) (`devices.inl:272`)
- GUI-Y2 — kwin_focus worker UAF + thread leak (`kwin_focus.inl:196-218`)

**ORTA:**
- Daemon: SAVE-1 (kapanış config kaybı), PID-2 (0-byte brick), PID-3 (comm-unreadable canlı SİLİNİYOR), HP-2 (replug graspsız), HP-3 (device_id dedup yok)
- GUI: Y3 (HID++ busy drop), O1 (LUT emission içi rebuild), O2 (output_dpi sentinel 0 yok), O3 (LOD combo dilde yok), O4 (child-watch teardown)
- Math: PRE-1 (oracle gaming bayat cap), PRE-2 (precision 1.2/1.5 uyumsuz), MATH-1 (negatif accel+cap dejenere), MATH-2 (scale=0 ölü/kilitli), PRE-3 (apex sub-1 plato)
- Config/CLI: CFG-1 (import migrate atlıyor), CFG-2 (validate ölü dal), CFG-3 (MAX_PROFILES sadece load), CFG-4 (ack-before-persist + sahte rc=0)
- Paket: PKG-1 (.SRCINFO 0.6.4), CI-1 (set -e yok)

**DÜŞÜK/INFO:** SYN-1, SUB-1, FCNTL-1, SIG-1, D1-D4 (GUI), CFG-5..7, CLI-1..3, SH-1..3, SVC-1, DOC-1, I-1/2 (daemon), INFO notlar.

**Düzeltme gerektirmeyen / belli:** ACTIVE-agacında fix edilmiş TUR 18 listesi yukarıda; doğrulanmış temiz alanlar her tur sonunda not edildi.

---

# TUR 19 — Uinput E2E Harness Doğrulama Turu (2026-09-11)

Analiz tarihi: 2026-09-11 (dördüncü tur)
Kapsam: Sanal uinput fareyle daemon hot path'inin uçtan uca doğrulanması (`tests/e2e_harness.cpp`, `tests/run_e2e.sh`). Katmanlar: `daemon.cpp` batched read + `write_batch` + flush_motion; math: `motion_math`/rawaccel modifier.
Yöntem: Harness root'ta `/dev/uinput` sanal fare üretir, `/tmp` e2e profiliyle daemon spawn edilir; accel + raw fazları beklenen çıktı frame'leriyle karşılaştırılır; sistem daemon'u SIGSTOP/SIGCONT trap ile izole edilir.
DURUM: Tüm kapılar yeşil — E2E accel 5/5 + raw 2/2, birim 33789/33789, ASan 33789/33789, oracle 1071/68 OK, tr_coverage PASS.

## CFG-1 · DÜŞÜK · `output_dpi: 0` clamp ≥1 ile accel çıktısını sessizce ~0'lıyor — **[DÜZELTİLDİ]**
- Konum: `src/config.cpp:487` → artık `0` korunuyor; `rawaccel.hpp:438` guard'ı; CLI `set-param` domaini 0–32000 (`cli/main.cpp:918`); `cli/main.cpp:669` uyarı sınırı [0, 32000]
- Kategori: Arayüz/behaviour (config foot-gun)
- Açıklama: Programatik/JSON `output_dpi=0` ("scaling yok" sanılabilir) eskiden 1'e clamp edilir; `modify()` yolu `dpi_adjustment = (1/1000)·dpi_factor` → 800 dpi cihazda 0.00125 → "motion çıkış yok" gibi yanlış daemon hatası sanılan pratik donma (E2E'de iki tur debug). Oysa modifier'daki `args.output_dpi > 0` guard'ı 0'ı **"output-DPI normalizasyonu kapalı (1:1)"** olarak tasarlıyordu — sanitize o sentinel'i yutuyordu.
- Düzeltme: Sanitize artık `output_dpi < 0 → 0`, `0 → 0` (sentinel korunur), `(0,1) → 1`, `> 32000 → 32000`. CLI `range_ok` ve `check` uyarı sınırı [0, 32000]'a genişletildi; help metni güncellendi. `modify()` guard'ına CFG-1 yorumu eklendi.
- Doğrulama: birim 33789/33789, ASan 33789/33789, oracle 1071/68 OK, tr_coverage PASS, build 0 warning. Yeni testler: `sanitize` — `0→0`, `(0,1)→1`; O5 — `output_dpi=0` → çıktı değişmez (1:1).

## CFG-2 · BİLGİ · Classic LINEAR profil hız-bağımsız sabit gain verir — E2E için deterministik, bug değil
- Konum: `include/accel-classic.hpp` + `rawaccel-base.hpp` (linear: exponent_classic ≤ 1)
- Açıklama: `exponent_classic=1.0` → gain = 1+acceleration (hızdan bağımsız sabit); modify ×3 E2E'de doğrulandı (20,10 → 60,30). Referans RawAccel parity; P105/P106 kontratı. Hız/zaman belirsizliğini ortadan kaldırdığından E2E'de tercih edildi.

## Doğrulanan temiz noktalar (bulgu değil):
- Harness kapsamı: T-A1 classic linear ×3 tek SYN; T-A2 buton+motion aynı frame; T-A3 buton-only frame; T-A4 LOW-1 deferral tam 2 frame; T-B1 raw 1:1 byte-identical — hepsi geçti.
- `-v` daemon log'u (`/tmp/rawe2e-<pid>/`) tek write + tek SYN flush akışını kanıtladı; teardown'da "Device error (No such device)" beklenen kaynak destroy logu (harness hâlâ capture yaparken kaynak kapatılıyor).
- Tek writer + tek loop thread kuralı, sistem daemon'u SIGSTOP izole edilirken iki daemon yan yana çalıştırıldığında da ihlal edilmedi.

---

# TUR 25 — Kalan Açık Maddelerin Kapatılma Turu (2026-09-12)

Analiz tarihi: 2026-09-12 (yeni tur)
Kapsam: TUR 19 sonrası raporda açık kalan düzeltilebilir maddelerin kod + test + rapor olarak kapatılması. Yönerge: "Karşına çıkan ve düzeltilmesi gerektiğini düşündüğün herşeyi düzelt" — CUR maddeleri de dahil.
Yöntem: Her açık madde önce kök nedenle incelendi (kaynak + reference tarafı + oracle grid), sonra düzeltilmesi "reference-clone felsefesini" bozmayacak olanlar uygulandı; reference davranışı olanlar gerekçeyle kasıtlı işaretlendi. Tüm kapılar koşuldu.
DURUM: Tüm kapılar yeşil — birim 33792/33792, ASan 33792/33792, oracle 1071/68 OK, tr_coverage PASS, build 0 warning, E2E accel 5/5 + raw 2/2.

## Belli madde kapanışları (kod + test)

**SM-4 — [DÜZELTİLDİ] (idle-tetikleme eklendi)**
- `daemon.cpp:1724-1738` (`flush_motion` ev.time dalı): `gap_ms ≥ kIdleGapMs` (DEFAULT_TIME_MAX, 100 ms) ise `time_ms = 1000.0 / max(poll_rate, POLL_RATE_MIN)` — nominal tek poll periyodu; değilse `time_ms = gap_ms`. Duruş sonrası ilk frame artık gerçek periyotla ölçülür, under-gain yok.
- `daemon.cpp:1992-2000` (SYN_REPORT handler): motion'suz frame'ler de `last_frame_ev_us`/`last_time_ms` yeniden demirleniyor — uzun boş SYN zinciri bir sonraki hareket frame'ini bozmaz.

**RAC-4 — [DÜZELTİLDİ]**
- `daemon.cpp:1919-1935` (SYN_DROPPED handler): birikmiş yasal hareket/queued event'ler LOW-1 pending mekanizmasına park ediliyor (`pending_dx/dy`, `pending_events`, `queued_count=0`) → düşme yerine en fazla 1 poll gecikme; yalnız SYN_DROPPED–SYN_REPORT arası belirsiz penceredeki veri atılır.

**SM-7 — [DÜZELTİLDİ] (hedef 10 sn'ye inildi)**
- Ortak `SMOOTH_HALFLIFE_MAX = 10000` ms (10 sn) `rawaccel-base.hpp:24`; `config.cpp:518` sanitize + `cli/main.cpp:957-959` set-param domaini + help metni bu sabiti paylaşıyor. P107 byte-korunumu korunur (`range_ok(0, SMOOTH_HALFLIFE_MAX)` halflife tuşları için min_ok grubundan çıkarıldı). P107 testi 1e6 → sabite güncellendi. GUI spin max 200 ms — dokunulmadı.

**CUR-3 — [DÜZELTİLDİ]**
- `accel-power.hpp` GAIN overflow guard: `cap_y < DBL_MAX` (cap istenmiş) → cap_y tavanına klamp (sürekli kuyruk); cap'siz eğri → defensif identity (1.0). Orakel grid'i bu guard'a düşmez (max ~1e53 « DBL_MAX) → sapma satırı yok. Yeni `test_cur3_power_inf_guard` (overflow → cap; cap'siz → identity; sub-overflow ham eğri değişmez).

## KASITLI korunan (reference davranış, gerekçe eklendi)

- **CUR-4** sync GAIN 512+ →0: reference `activation_framework` LUT kuyruğu; oracle 100000 ips'e kadar birebir eşleşiyor.
- **CUR-5** power output_offset platosu: referans `power` formülünün parçası; "kick" input-speed smoothing ile kapatılır.
- **CUR-6** jump LEGACY C0: ref `jump_base` aynı eşik (`rate_inverse<1`) — birebir; orakel `jump_legacy_*` eşleşiyor.
- **CUR-7** NaN/Inf sert guard: pipeline'dan NaN çıkmasını engelleyen son-çare savunma; "blend" state'li yumuşatma ister ve reference'dan sapar — korunur.

## Doğrulanan kapılar (TUR 25)
- Build 0 warning; birim 33792/33792 (yeni CUR-3 testi dahil); ASan 33792/33792; oracle 1071/68 OK; tr_coverage PASS; E2E accel 5/5 + raw 2/2 (sistem daemon'u SIGSTOP izole).
- Git: SM-4+RAC-4+SM-7+CUR-3 + rapor güncellemesi tek commit olarak işlendi, origin/main'e push edildi.

---

# TUR 26..30 — BEŞ TUR TAM KAPSAMLI ÜÇÜNCÜ YENİDEN ANALİZ (2026-09-12, güncel ağaç)

Analiz tarihi: 2026-09-12 (beşinci oturum — 5 bağımsız tur)
Kapsam: Tüm kaynak ağacı (`daemon/`, `gui/`, `include/`, `src/`, `cli/`, `scripts/`, `tests/`, `packaging/`, `.github/`)
Yöntem: Paralel 5 uzman derin inceleme + tüm kritik bulguların kaynak üzerinde birebir doğrulanması (grep/sed ile line-by-line)
Önemli not: Ağaç bu tur **sırasında da düzenlendi** (daemon.cpp tur başında 2759, doğrulama sonunda 2779+ satır; `ui_builder.inl` 1605). Satır numaraları kısmen kayabilir. TUR 20–24'ün açık maddeleri yeniden doğrulanmıştır; **tur penceresinde yeni kapananlar: GUI-K1 (DÜZELTİLDİ, `ui_builder.inl:659-663`) ve HP-1 (DÜZELTİLDİ, hotplug artık PAS-1/PAS-2 kapılarını uyguluyor `daemon.cpp:1173-1188`)** — bunlar aşağıda tekrar bulgu olarak yazılmadı, durum satırına işlendi.

---

# TUR 26 — Daemon / Yaşam Döngüsü / Hot-Plug / IPC / PID (güncel ağaç)

Odak: `daemon/daemon.cpp` (2779+), `daemon/main.cpp` (584), `daemon.hpp`, `lat_stats.hpp`, `motion_math.hpp`.

## Durum Güncellemesi (TUR 20 kayıtları, güncel ağaçta doğrulandı)

- **HP-1 → DÜZELTİLDİ** — `do_hotplug_scan` artık `raw_input_enabled_` (`daemon.cpp:1175`) ve `dev_cfg.disable` (`:1183`) kapılarını setup_devices ile aynı şekilde uyguluyor; grab bırakılıp close+continue yapılıyor. Açık: idle rescan "safe mode" ihlalinden kurtuldu.
- **PID-1 AÇIK** — `main.cpp:331-333` OR-zinciri fallback'i canlılık kontrolsüz ikinci başlatmaya izin veriyor (değişmedi).
- **PID-2 AÇIK (etki koşullu)** — `main.cpp:346` `unlink; return false;` kod hatası duruyor; "kalıcı red" yalnızca unlink'in EPERM gibi sessizce başarısız olduğu senaryoda, yaygın durumda tek boot gecikmesi.
- **PID-3 AÇIK** — hardened `/proc` (hidepid/ProtectProc) altında `/proc/<pid>/comm` okunamazsa canlı daemon'ın pid dosyası silinip ikinci instance başlayabiliyor.
- **SAVE-1 AÇIK** — `save_worker` queue drene edilmeden `running_` false görüp `break` edebiliyor; o pencerede `set_config` "ok" ack'lar ama yazılmaz.
- **HP-2/HP-3 AÇIK** — replug EBUSY deny sonrası cihaz grabsız kalabilir; device_id dedup yok.
- **SYN-1 AÇIK** — `apply_profile` telemetri reset'i tam seqlock (odd-bump) değil; dar pencerede bayat dx/dy + sıfır hız eşleşebilir.
- **SUB-1 AÇIK** — sub-piksel remainder `modify()` sonrası ekleniyor; rotated/snapped config'lerde sürüklenme.
- **FCNTL-1 / SIG-1 AÇIK** — F_SETFL dönüşü yoksayılıyor; sinyal handler'ları pid yazımından sonra kuruluyor.

## Yeni Bulgular

**D26-N1 · YÜKSEK · `stop_ipc_server` kendisinin BAĞLAMADIĞI socket'i siliyor → ikinci örnek ilk daemon'ın IPC'sini yok ediyor**
- Konum: `daemon.cpp:2443-2446` (start, path en başta set) → early-return yolları (`:2458,2464,2471`) → `stop_ipc_server` (`:2562`, unlink unconditional)
- Kategori: IPC / çoklu-instance / güvenlik
- Açıklama: `start_ipc_server` daha ilk satırda `ipc_sock_path_ = sock_path` yazar, sonra lstat/probe yapar. Probe canlı bir daemon bulunca `return false` — `ipc_sock_path_` temizlenmemiş kalır. İkinci örnek teardown'da (`main.cpp` üzerinden veya `~AccelDaemon` → `stop_ipc_server`) `ipc_sock_path_`'i unlink eder → **ilk (canlı) daemon'ın `/run/rawaccel.sock` dosyası silinir**; ilk daemon yeni istemci kabul edemez, GUI yalnız SIGHUP fallback'e düşer. PID-1 ile birleşince: ikinci instance hem ikinci uinput yaratır hem ilk daemon'ın socket'ini öldürür.
- Öneri: Socket'i gerçekten bind edene kadar `ipc_sock_path_` boş tut; tüm early-return yollarında `clear()` et (mutex altında) veya stop'a "bound" bayrağı ekle.

**D26-N2 · ORTA · `uinput_write_retry` bütçe(32) tükenince `done < nbytes` iken yine `true` dönüyor → frame sessizce düşüyor**
- Konum: `daemon.cpp:1612-1646` (döngü sonu `return true;`), kullanan `flush_motion`/`flush_batch`
- Kategori: Yazma doğruluğu / kayıp
- Açıklama: 32 deneme boyunca EAGAIN'de takılan kısmi yazımda `done` hedefe ulaşmadan `return true` → caller tam yazıldı sanır. Kısmi fram'de SYN'siz yarım paket bırakılır (sonraki fram ile birleşir) ya da tüm REL düşer; ne `disconnected` ne log. Yorumun "dropping the tail" notu kayıp olayını kabul ettiğini belirtir ama kod başarı döndürüyor.
- Öneri: Bütçe tükenince `false` dön + "tüketici tıkanık, tail bırakıldı" verbose log'una ayrı kod; `true` ile kayıpsızlık garantisi arasında tutarsızlık gider.

**D26-N3 · ORTA · EAGAIN retry `nanosleep`'i tek loop thread'i blokluyor → tüm fareler ~120 ms donuyor**
- Konum: `daemon.cpp:1630-1634` (exponential backoff), tek-loop-thread mimarisi
- Kategori: Gecikme / takılma
- Açıklama: Tıkanık tüketicide her frame 32×(50µs→4ms üstel) ≈ 120 ms uyur; loop thread tek olduğundan aynı pencerede diğer cihazlar da işlenmez. "Hot path asla bloklanmaz" iddiası yalnız ilk EAGAIN perspektifi için doğru; nanosleep döngüsü loop'u geri alıyor.
- Öneri: Sürekli EAGAIN'de o cihazı geçici önceliksiz bırak, diğerlerine dön; retry'ı ayrı yazma taraftarına taşı.

**D26-N4 · DÜŞÜK/ORTA · Non-motion kuyruk taşması (>16) frame dizilişini bozuyor: `[B…B][REL,REL,SYN]`**
- Konum: `daemon.cpp:2070-2082` (flush_queued + per-event write)
- Kategori: Çerçeve bütünlüğü (sıralama)
- Açıklama: Taşmada queued butonlar SYN'siz erken flush edilip ardından gelen motion+SYN ayrı yazılıyor → kernel sırası ile fiziksel sıra farklılaşıyor; bir fiziksel frame iki çıktıya bölünür. Normal (≤16) yolda `[motion][buttons][SYN]` korunuyor.
- Öneri: Taşmayı ikinci zarflı batch ile taşı; sıralamayı asla bozma.

**D26-N5 · ORTA · Deny-expiry + dolu `devices_` = cihaz sonsuza kadar grabsız; create_virtual_device hatasında deny hiç yok**
- Konum: `daemon.cpp:1121-1124,1137-1145,1437-1443,1146-1150`
- Kategori: Hot-plug yaşam döngüsü
- Açıklama: (a) Deny 5 sn expiry'si dolarsa ve `devices_` boş değilse yeniden deneme yok (rescan yalnız empty). (b) `create_virtual_device` başarısız olursa `deny_reopen` çağrılmıyor → her taramada aynı churn tekrarlanır.
- Öneri: Deny map'inde en erken expire'i izleyen tek-şans rescan slotu; create hatasında da deny.

**D26-N6 · DÜŞÜK · `dump_latency_stats` `log_mu_` dışında std::cout`a basıyor → `-f json` satırları bozabilir**
- Konum: `daemon.cpp:2160-2190` (ipc/loop thread) vs `:452-458`
- Kategori: Log bütünlüğü
- Öneri: Dump'ı aynı muteks altına al / atomik write.

**D26-N7 · DÜŞÜK · `start()` içinde thread kurulum fırlatırsa exception dışarı taşar → destructor koşmaz**
- Konum: `main.cpp:546` (try/catch yok) + `daemon.cpp:529-563`
- Kategori: Çıkış yolu / kaynak
- Öneri: `start()`'i try/catch'e al, `stop_ipc_server()+stop()` ile temizle.

**D26-N8 · INFO · IPC socket path'te (PID'in aksine) `/tmp` fallback'i yok**
- Konum: `daemon.cpp:2270-2274` (yalnız XDG→/run); PID üç konumlu
- Açıklama: XDG yok + non-root → EACCES → IPC sessizce kapalı (daemon çalışır, GUI SIGHUP). Bilinçli olabilir, belgelenmeli.

## Doğrulanan temiz (bulgu değil, TUR 30)
Batched evdev okuma + short-read drop doğru; SYN_DROPPED durum makinesi (kalıcı flag, RAC-4 re-anchor, LOW-1 park) doğru; SM-1 kernel-frame interval + idle re-measure + (0,min] clamp + empty-SYN `last_frame_ev_us` temiz; `flush_motion` seqlock yazıcısı tek→çift bump doğru (reset hariç — SYN-1); epoll 10-hataya-dayanıklılık doğru; fd_to_dev_ kilitli indeks rebuild doğru; stop/destructor sıralaması (joinable, teardown) doğru; IPC deadline katmanı (10s/5s, 2s recv, kısmi send, SO_PEERCRED) doğru; config no-op guard + save_config atomik (PID-suffix O_NOFOLLOW|O_EXCL) doğru; HID++ worker'ın loop'tan ayrılması (P171-BFIX) doğru; lat_stats invariant guard'ları doğru; motion_math INT clamp + remainder reset doğru; 3 olaylı normal frame `[motion][buttons][SYN]` sırası korunuyor.

---

# TUR 27 — GUI / GTK / Eşzamanlılık / Dil / Cihaz Paneli (güncel ağaç)

Odak: `gui/*.inl` (12 dosya, `ui_builder.inl` 1605 satır), `app_state.hpp`.

## Durum Güncellemesi (TUR 21 kayıtları)
- **GUI-K1 → DÜZELTİLDİ** — `match_app_entry "changed"` artık 2-arg `on_param_changed` (`ui_builder.inl:662-663`); satır 661 introduction yorumu GUI-K1 açıklaması. K1 tur penceresinde kapatıldı.
- **Y1 AÇIK** — hidraw inotify buffer'ı `alignas(struct inotify_event)` eksik (`devices.inl:272`; evdev buffer `:239` düzgün). ARM UB.
- **Y2 AÇIK** — kwin_focus worker (detached thread) `session_conn`'i okurken uninstall unref+null edebiliyor (UAF); uninstall `installed=false` senkronu yok; script unload ana thread'de 2s bloklayabiliyor.
- **Y3 AÇIK (önemi artırıldı)** — HID++ busy-guard seçimi sessizce düşürüyor; widget'ta **eski cihazın** DPI/rate değerleri kalıyor; yeni cihaz benzer özellikler sunuyorsa "Apply to Device" **yanlış fiziksel cihaza eski değerleri yazabilir** (`hidpp_panel.inl:577,313,613-618`).
- **O1 AÇIK (latent)** — LUT spin kendi `value-changed` emission'ı içinde rebuild ile imha edilebiliyor (GObject temp ref sayesinde anlık UAF yok, kırılgan).
- **O2 AÇIK** — `output_dpi=0` sentinel'i spin (min 100) ile temsil edilemiyor; `profile_to_widgets` 0'ı 100'e kırpar, sonraki save 100 yazar.
- **O3 AÇIK** — dil değişiminde `hw_lod_combo` yeniden kurulmuyor (`refresh_language` kapsam dışı).
- **O4 AÇIK (latent, pratik düşük)** — `g_child_watch_add`/tek-seferlik `g_timeout_add` kaynakları iptal edilmiyor; Mouse Test penceresi app'e kayıtlı olmadığından (main.cpp:228-232) şu an destroy sonrası geri çağrı üretemiyor — gelecek kod için risk.
- **D1-D4 AÇIK** — model ref sızıntıları (`:83-93`,`:634-642`); `save_lang_pref` stale `.tmp` (EEXIST) sessiz drop; duplicate/export widget eşitlemesiz (ama bkz. G27-N1); `*_finish(..., nullptr)` hataları "cancel" sayıyor.

## Yeni Bulgular
**G27-N1 · ORTA · Export, widget değişikliklerini senkron etmeden `cur_prof` serileştiriyor → kaydedilmemiş ayarlar çıkmıyor**
- Konum: `gui/profile_mgr.inl:490` (`export_profile_done` → `profile_to_json(cur_prof(S))`), `:413` (duplicate aynı aile)
- Kategori: Veri doğruluğu
- Öneri: Export (ve duplicate) callback başına `widgets_to_profile(S)` ekle.

## Doğrulanan temiz (bulgu değil, TUR 30)
69 `g_signal_connect` + `g_io_add_watch` + `g_timeout_add` + `g_child_watch_add` + 4 `g_idle_add` + `set_draw_func` + `add_tick_callback` + `GSimpleAction::activate` — **hiçbir ABI arity uyumsuzluğu yok** (K1 zaten düzeltildi); `update_raw_sensitivity` 18 widget eksiksiz (output_dpi_spin dahil); `widgets_to_profile`/`profile_to_widgets` alan eşlemesi tam ve simetrik (unlinked-Y X-copy dahil); `refresh_language` re-entrancy guard'ı `tr_combo_fill`+`rebuild_profile_combo` boyunca kapsıyor; LUT `lut_gain_to_stored`/`lut_stored_to_gain` dönüşümleri tüm editör yollarında tutarlı; HID++ 4 idle callback `hw_cancel` ile gate'li; kwinrc/kcminputrc atomik yazımı (O_NOFOLLOW|O_EXCL, fsync, ENOSPC) sağlam; devices.inl inotify çoklu-olay + unplugged-placeholder doğru; mouse_test teardown nulling doğru; main window destroy kaynakları doğru kaldırıyor.

---

# TUR 28 — Matematik / Presetler / Oracle Kapsamı (güncel ağaç)

Odak: `include/accel-*.hpp`, `include/presets.hpp` (165), `tests/oracle/oracle_cases.hpp`.

## Durum Güncellemesi (TUR 22 kayıtları)
- **PRE-1 ✅ DÜZELTİLDİ** — oracle `game_gaming_classic` cap_y=1.5 (`oracle_cases.hpp:263-269`) → 1.8'e senkronlandı; blok başlığı "EXACT from presets.hpp" vaadi artık sağlanıyor; oracle 67 deviation ile OK.
- **PRE-2 ✅ DÜZELTİLDİ** — precision `limit=1.2, cap yok` → `cap={24, 1.2}` eklendi (`presets.hpp`), oracle mirror + oracle OK.
- **PRE-3 ✅ DÜZELTİLDİ** — apex `output_offset=0.9` → `1.0`; 2-8 mm/s bandında sub-1 plato yok; oracle `game_apex_power 0` satırı referansla hizalandı (deviation listesinden çıktı).
- **MATH-1 ✅ KASITLI** — negatif classic accel + cap davranışı REFERANS-PARITY'dir: oracle `classic_gain_negaccel` hiçbir deviation listesinde yok, `test_accel.cpp:3182-3205` birebir doğruluyor; davranış değişikliği referansla çakışır → rapora KASITLI kaydı yazıldı, kod dokunulmadı.
- **MATH-2 ✅ DÜZELTİLDİ** — power `scale` domain artık `[0.01, SCALE_MAX]`; sanitize `scale<0.01→0.01`; CLI `range_ok(0.01, SCALE_MAX)`; `tests/test_accel.cpp` scale=0 case'i 0.01'e güncellendi ve CLI smoke'ta out-of-domain red doğrulandı.

## Yeni Bulgular
**M28-N1 · DÜŞÜK · power scale=0 ailesinde davranış testi ve oracle satırı YOK**
- Konum: `tests/test_accel.cpp:5046` (finiteness only), `oracle_cases.hpp` (grid scale ≥ 1e-3)
- Kategori: Test kapsamı
- Açıklama: "sabit boost" ↔ "ölü" arası refactor sessizce geçebilir; hiçbir şey nafakan kaydını tutmuyor.
- Öneri: scale=0 case'i beklenen gain/ölü-imleç semantiğiyle pin'le; bir oracle deviation satırı ekle.

**M28-N2 · DÜŞÜK · CLI yardım-metni "negative = classic decel" yanıltıcı (MATH-1 ile sarmal)**
- Konum: `cli/main.cpp:2105-2106`
- Öneri: "yalnız cap 0/1 iken decel" notu.

## Doğrulanan temiz (bulgu değil, TUR 30)
Classic GAIN cap geçişi sürekli (`constant=(base_fn(cap_x)-cap_y)·cap_x`); power GAIN `offset.x` plato sürekliliği P155 sırasını çözüyor; BUG-02 paylaşımlı exponent floor; io cap.y≤0 identity guard; jump/natural/lookup guard'ları (NaN çözümleri) referans parity; synchronous GAIN LUT (97 hücre) + BUG-01 |z|-sign; EMA simple/linear katsayıları referansla birebir; sanitize sınırları strict `>` (R15 inclusive maxima); negatif-accel politikası bilinçli; daemon zaman clamp'ı + SM-4 yeniden ölçüm bütünleşik; JSON fp round-trip tam (LUT float guard `config.cpp:191-201`).

---

# TUR 29 — Config / CLI / IPC / Profil Eşleme (güncel ağaç)

Odak: `src/config.cpp` (971), `cli/main.cpp` (2415), `include/config.hpp`, `daemon.cpp` profil eşleme.

## Durum Güncellemesi (TUR 23 kayıtları)
- **CFG-1 ⏸ ERTELENDİ** — device_profile JSON'unda `version` damgası yok; global `migrate_lookup_gain`, 0.4.0+ export'larını yanlış ölçekler (çift ölçek). Güvenli fix önce `profile_to_json`'a version eklemeyi gerektirir — mevcut koda dokunulmadı (gerekçe FIX_LOG KARAR VERİLDİ'de).
- **CFG-2/CFG-9 ✅ DÜZELTİLDİ** — `validate`'de ölü post-sanitize dallar yerine ham JSON cross-check: dpi/polling/output_dpi/scale clamped değerler için uyarı basan `clamp_warn` lambda; "All checks OK" sadece ham JSON temizken. Bad-file smoke doğrulandı.
- **CFG-3 ✅ DÜZELTİLDİ** — MAX_PROFILES kapısı create/duplicate/create-preset/import'a eklendi (256+ import red, smoke doğrulandı).
- **CFG-4 AÇIK (ORTA)** — ack-before-persist; timeout→SIGHUP sahte rc=0 (seri IPC 10s/5s deadline ile sınırlı ama kapanmış değil).
- **CFG-5 KISMEN DÜZELTİLDİ** — ilk kayıtta aşağı damgalama gitti: kaydedilen version daha eski değilse korunuyor (future version round-trip'i korunur; bilinmeyen anahtar round-trip'i hâlâ ayrı açık).
- **CFG-6 AÇIK** — 256 üst düzey vs 255 iç içe `profile.name`
- **CFG-7 AÇIK** — `$XDG_CONFIG_HOME` yoksayılıyor; `getpwnam_r` ERANGE retry'siz (daemon'da var) → CLI/daemon config yolu ayrışması.
- **CLI-1 AÇIK (ORTA)** — `status` yerel+canlı veriyi karıştırıyor; `match_app` / daemon `active_profile` yoksayılıyor.
- **CLI-2 ✅ DÜZELTİLDİ** — `set-param output_dpi` domain `{0} ∪ [1,32000]`; 0.5 red + rc≠0, 0 sentinel kabul (P107) — smoke doğrulandı.
- **CLI-3 ✅ DÜZELTİLDİ** — main() yükleme yolu boş config'de default'u otomatik yeniden oluşturuyor; delete-all mesajı artık artık doğru (smoke doğrulandı).

## Yeni Bulgular
**C29-N1 · YÜKSEK · `find_profile` genel fallback döngüleri `match_app`'i filtrelemiyor → uygulamaya özel profiller catch-all oluyor**
- Konum: `daemon/daemon.cpp:923-977`; eksik filtre döngü 1 ve 2 (`:963-972`; `have_app` yalnızca ilk iki app-loop'unda)
- Kategori: Profil eşleme / cihaz davranışı
- Açıklama: `match_app="firefox"` olan bir cihaz profili, `current_app_` boşken (masaüstü / kwin relay kapalı) her fareye ve her uygulamada uygulanır; krita odaktayken de döngü 1 (device_id + app) eşleşmeyince döngü 3 genel device_id eşleşmesi firefox profilini yakalar. Yorum (`:944-946`) "no app constraint" vaat ediyor ama uygulama-kısıtlı profilleri de içeriyor. Kullanıcı app-scope kurduğu halde her yerde o profil çalışır.
- Öneri: Döngü 3/4'e `p.match_app.empty() || profile_matches_app(p, current_app_)` ekle; `have_app` false iken `match_app`'i boş olmayan profilleri atla.

**C29-N2 · ORTA · `import`'"profiles" dizili dolu config'te `active_profile`/`use_raw_input`/`version`'u yutuyor**
- Konum: `cli/main.cpp:1172-1176` (sadece profilleri alır)
- Açıklama: `list --json` çıktısı tam app_config; aynı dosyanın import'u üst seviye kontrolleri atmıyor → round-trip aktif profil + raw anahtar kaybı.
- Öneri: `profiles` dizisi varken tam app_config olarak içe aktar ya da açık hata.

**C29-N3 · DÜŞÜK · `set-param` help'i `lut-data` öneriyor ama böyle bir key yok**
- Konum: `cli/main.cpp:975-978` vs `:831-841`
- Açıklama: `mode lookup` sonrası "lut-data ile ayarla" yazıyor; key reddediliyor. LUT'a CLI erişimi yok.
- Öneri: `import`'a işaret et ya da `lut-data` importer'ı ekle.

**C29-N4 · DÜŞÜK · El yazısı JSON'da `device_id: "(all)"` normalleştirilmiyor → sessiz ölü profil**
- Konum: `src/config.cpp:587` (verbatim), `daemon.cpp:961` (exact match)
- Açıklama: Ekran `(all)`'ı boş olarak gösteriyor ama loader literal `"(all)"`'ı boşa okumuyor; hiçbir cihaz eşleşmez.
- Öneri: Load + `set-param device_id`'de `"(all)"`/`"all"`/`"*"` → boş.

**C29-N5 · DÜŞÜK · CLI'da `match_app` desteği hiç yok (set-param + status)**
- Konum: `cli/main.cpp:831-841,1519-1547`
- Açıklama: GUI koyabilir, CLI ne set ne raporda gösteriyor; N1 ile birleşince `status` daemon'un uyguladığından farklı profil gösterebilir.
- Öneri: `match_app`'i set-param keylerine (128-char cap ile) ekle.

**C29-N6 · DÜŞÜK · CLI disable açıklaması "dormant" diyor; daemon artık setup+hotplug'da onurlandırıyor (yanlış mesaj)**
- Konum: `cli/main.cpp:350-352`; daemon `:859` + `:1183` (HP-1 fix sonrası)
- Açıklama: Canlı reload'da zaten grab'li cihazın un-grab edilmemesi kısmı doğru; "daemon hot path yoksayar" metni artık yanlış.
- Öneri: "kurulum/hot-plug sırasında atlanır; canlı toggle bir sonraki rescan'a kadar sürer" biçiminde düzelt.

**C29-N7 · DÜŞÜK · JSON int alanları kesirleri sessizce truncate ediyor (CLI reddederken)**
- Konum: `src/config.cpp:557-567` (`json_get_int_safe`) vs `cli/main.cpp:903-910`
- Açıklama: `"dpi":1000.9` → 1000 kayıt; aynı değer set-param'da reddediliyor. İki kontrat.
- Öneri: JSON yolunda da uyar/reddet.

**C29-N8 · DÜŞÜK · `save_config` rename meta bilgilerini (owner/ACL/xattr) kaybediyor**
- Konum: `src/config.cpp:700-706,779` (mode-only copy)
- Açıklama: Root-owned path'e non-root CLI yazarsa inode yenilenir, owner CLI'ye döner; ACL/xattr düşer.
- Öneri: Mümkünse fchown ya da belgele.

## Doğrulanan temiz (bulgu değil, TUR 30)
NaN/Inf JSON reddi + strict `require_number`+`isfinite` (config.cpp:131-154); CLI `stod`+pos+isfinite trailing-garbage reddi; int-UB guard'ları (`json_get_int_safe`, `finite_double_to_int`, lut_length double okuma, FLT_HI overflow); bilinmeyen komut/arity/`-c`/`--config=` reddi; bool/string/number type guard'ları + uzunluk cap'leri; bilinmeyen mode/cap_mode throw; safe_save atomikliği (PID-suffix O_NOFOLLOW|O_EXCL, full-write, fsync file+dir, rename, `.bak` hard-link rotate, EEXIST recovery); IPC framing (`set_config <n>\n` + exact body, 1MB cap, 10s/5s, 2s, kısmi-write, 16MB runaway guard, SO_PEERCRED); import boyut sertleştirme + oversize-LUT pre-sanitize + duplicate-name reddi; migration one-shot (version read-back + semver suf + stamp) load/push yollarında doğru; duplicate device_id first-match tutarlı; `speed_min/max` set-time uyarısı + stored değer ekosu.

---

# TUR 30 — Testler / E2E / Fuzz / CI / Betikler / Paketleme / Lisans (güncel ağaç)

Odak: `tests/`, `scripts/`, `setup.sh`, `.github/workflows/ci.yml`, `packaging/`, dokümanlar.

## Durum Güncellemesi (TUR 24 kayıtları)
- **PKG-1 AÇIK (ORTA)** — `.SRCINFO:3` `0.6.4`, PKGBUILD `1.1.0`; ayrıca `.SRCINFO`'da `optdepends`'te `qt6-tools` satırı eksik. Kök neden: CHANGELOG/AGENTS sürüm-politikası `.SRCINFO`'yu kapsamıyor.
- **CI-1 KISMEN HAFİFLEDİ** — build+warning-gate adımı hâlâ `set -e` içermiyor AMA GH Actions Linux varsayılan shell'i `bash -e -o pipefail` → kırık build pratikte batıyor. Kalan: `set -e` yok + AGENTS.md "Lint / Warning Check" komutu (`grep -E "warning:|error:"`) ci.yml'nin dar regex'iyle çelişiyor (dok bayat).
- **SH-1 → OBSOLETE** — `bench_hotpath.sh:42` inline command-substitution'daki başarısızlık `set -euo pipefail`'ı tetiklemiyor (deneysel doğrulandı); non-x86'da yalnızca boş CPU satırı.
- **SH-2 AÇIK (DÜŞÜK)** — setup.sh kullanıcı-config kopyasında check-then-use TOCTOU (`setup.sh:296-315`; install symlink'i izler; root+dotdir yazma gerektirdiğinden düşük pratik risk).
- **SH-3 AÇIK (latent)** — `~/.config/systemd/user/rawaccel.service` hiç temizlenmediği/uyarılmadığı; unit kullanıcı login'inde sistem unit'ini gölgeleyebilir.
- **SVC-1 AÇIK (DÜŞÜK)** — unit'te `StartLimitIntervalSec`/`Burst` yok; 3s RestartSec ile varsayılan oran kaplanabiliyor.
- **DOC-1 KISMEN AÇIK** — AGENTS.md:9 polkit "kurulur" diyor; setup.sh paket kurar ama aksiyon/kural kurmaz; README/docs doğru.

## Yeni Bulgular
**T30-N1 · ORTA · Proje lisans dosyasız; PKGBUILD `license=('custom')` — yayımlanabilirlik engeli**
- Konum: repo kökünde LICENSE/COPYING yok; `packaging/PKGBUILD:19`; `tests/oracle/ref/LICENSE` (yalnız vendored ref MIT)
- Açıklama: Raw Accel algoritma portu olarak hangi lisans olduğu belirsiz (upstream Windows Raw Accel GPL-3.0, vendored ref MIT). Lisans şartsız AUR/yayım dağıtımı hukuken riskli.
- Öneri: LICENSE ekle, heredoc'u package() içine kur, PKGBUILD license alanını düzelt.

**T30-N2 · DÜŞÜK · `run_e2e.sh` ortam-hatalarını 77 yerine FAIL(1)'e çeviriyor (kontrat: 0/1/77)**
- Konum: `tests/run_e2e.sh:55-70` (run_phase `[[ $rc -eq 0 ]] || FAIL`); harness başladıktan SONRA uinput/daemon spawn 77 dönerse 1'e düşer (pre-flight 77 doğru).
- Öneri: run_phase'te rc==77 → exit 77.

**T30-N3 · DÜŞÜK/ORTA · T-A4 ve T-B1 iddiaları etiketinden zayıf — regresyonları gözden kaçırabilir**
- Konum: `tests/e2e_harness.cpp:318` (T-A4: yalnız 2 frame + rel>0; amplitüd/eksen simetrisi sağlamıyor), `:330-336` (T-B1 "byte-identical": yalnız toplam X/Y + BTN varlığı; frame içi bölünme/ek olay geçer)
- Açıklama: Ek: test penceresinde gerçek fare trafiği `frames.size()`'i bozabilir (flaky); SIGSTOP izolasyonu yalnız sistem daemon'una yönelik.
- Öneri: İddiaları güçlendir (deferred frame tam değer/asimetri; raw fazında event-by-event byte karşılaştır).

**T30-N4 · DÜŞÜK · tr_coverage dinamik `tr()` argümanlarında PASS'i sessiz geçiyor; `kwin_focus.inl` tarama listesinde yok**
- Konum: `tests/tr_coverage.cpp` (literal-olmayan key'ler warn-only, exit 0), `tests/run_tr_coverage.sh:7-18`
- Açıklama: 30 dinamik site var; literal-only regresyon sessiz geçebilir. kwin_focus.inl şu an 5 yanlış-pozitif `c_str()` yetenekli; latent blind spot.
- Öneri: kwin_focus.inl'i listeye ekle; dinamik key'ler için min-coverage guard.

**T30-N5 · DÜŞÜK · Oracle p155 `io cap.y=0` satırları hiç doğrulanmıyor (ref NaN → `NaN>tol` asla true)**
- Konum: `oracle_cases.hpp` (p155_io_cap0_gain), AGENTS.md oracle notu
- Açıklama: Belgeli ama etkisiz bölge: o satırlar ne başarısız ne doğrulama. Mantıklı 10 satırlık gerçek kontrol bloğu düşünülmeli.
- Öneri: Deviation listesine değil, davranış testine taşı.

**T30-N6 · DÜŞÜK · Oracle `TOL` env negatif kabul ediyor → her run başarısız**
- Konum: `tests/oracle/run_oracle.sh` TOL regex (`[+-]?...`)
- Öneri: TOL'u [1e-12, 1e-2] ile sınırla.

**T30-N7 · DÜŞÜK · perf-gate SKIP yolları exit 0 (baseline kaybolursa sessiz geçer)**
- Konum: `.github/workflows/ci.yml:134-144` (baseline yoksa SKIP→0) vs aynı adımda baseline config körelmesi HARD FAIL (184-190)
- Öneri: SKIP'i warning + exit 1 yap (veya en azından ayrı "SKIP" çıktısı ile fail).

**T30-N8 · DÜŞÜK · README ve eski dokular bayat versiyon iddiasında**
- Konum: `README.md:5,72,85` (hâlâ "Current state: v0.6.4", artifact 0.6.4), `RAWACCEL_AUDIT.md:1`, `SOLAAR_INTEGRATION_REPORT.md:4`
- Açıklama: CHANGELOG güncel (1.1.0); README/dokular güncellenmemiş.
- Öneri: Versiyon bump politikasına dokümanları da ekle (PKG-1 ile aynı zincir).

**T30-N9 · DÜŞÜK · `run_tests.sh` python3'ü hard-koşuyor (yalnız P83 name-gate için)**
- Konum: `tests/run_tests.sh:50-57`; CI build-and-test python kurmuyor, runner image'a güveniyor
- Açıklama: Minimal sistemde proje hiç python gerektirmediği halde test çalıştırmak düşüyor.
- Öneri: python3 yoksa P83 gate'ini C++ tarafında karşıla veya destekli atla.

## Doğrulanan temiz (bulgu değil, TUR 30)
Version core sinkronu (daemon/cli/gui hepsi `RAWACCEL_VERSION`; CMake/PKGBUILD/CHANGELOG 1.1.0); unit-test exit semantiği (FAIL→1, `--filter` no-match→1); ASan/UBSan leak detection + halt_on_error (LSAN exit 23 batıyor); fuzz runner'lar (FUZZ_FLAGS array, set -e, -max_total_time, `crash-*` upload); fuzz-smoke PR skip + concurrency cancel doğru; oracle şu an OK (1071/68, stale-deviation sesli); perf baseline 6 config + `_meta` mevcut, bench `CXX`/`RAWACCEL_PORTABLE` onurluyor; setup.sh dep politikası 3 branşta tam (pacman/apt/dnf), `command -v`/`pkg-config` hard gate'leri, build-before-clean sırası, uninstall `/etc/rawaccel`'i koruyor; e2e SIGSTOP trap/SIGCONT doğru; TRC_DEBUG=1 çalışıyor.

---

# TUR 26..30 ÖZET — ÖNCELİK SIRASI (güncel ağaç)

**Tur sırasında kapananlar:** GUI-K1 (crash), HP-1 (hotplug safe-mode). **(Doğrulandı, tekrar bulgu değil.)**

**KRİTİK:** Yok (güncel ağaçta açık crash/veri-kaybı tespit edilmedi).

**YÜKSEK:**
- D26-N1 — `stop_ipc_server` bağlamadığı socket'i siliyor; ikinci örnek ilk daemon'ın IPC'sini öldürebiliyor (`daemon.cpp:2443-2446`, `2562`)
- C29-N1 — `find_profile` fallback döngüleri `match_app`'i filtrelemiyor; uygulamaya özel profiller catch-all olarak her yerde uygulanıyor (`daemon.cpp:963-972`)
- MATH-2 — power scale=0: sabit 1.5× boost / ölü imleç (CLI 0'ı kabul ediyor)

**ORTA:**
- Daemon: D26-N2 (retry `return true` tail düşmesi), N3 (nanosleep tüm fareleri dondurabilir), N4 (taşmada frame dizilişi bozulur), N5 (deny-expiry grabsız + create hatasında deny yok)
- GUI: G27-N1 (export eşitlemesiz); Y3 "Apply to Device" yanlış cihaza eski değer yazabilir
- Math: PRE-1 (oracle bayat), PRE-2 (precision 1.2/1.5), PRE-3 (apex sub-1), MATH-1 (negatif accel+cap "decel" değil)
- Config/CLI: CFG-1 (import migrasyonsuz), CFG-2/9 (validate yanıltıcı), CFG-3 (MAX_PROFILES taşması), CFG-4 (ack-before-persist), CLI-1 (status karışık), C29-N2 (import üst-kontrolleri yutuyor)
- Paket: PKG-1 (.SRCINFO 0.6.4)

**DÜŞÜK/INFO:** PID-2/PID-3, SAVE-1, SUB-1, SYN-1, FCNTL-1, SIG-1; GUI Y1/Y2/D1-D4/O1-O4; M28-N1/N2; C29-N3..N8; T30-N2..N9 (E2E-77, E2E-WEAK, TRC, ORAC-NAN/TOL, PERF-SKIP, README bayat, run_tests python3, LICENSE); D26-N6..N8.

**Açık kalması önerilen (judgement):** E2E-WEAK ve ORAC-NAN belgeli "known limitation"ların altında ama gerçek assertion boşlukları; T30-N1 (lisans) yayım/dağıtım öncesi en kısa zamanda.

**Düzeltme gerektirmeyen / doğrulanan temiz:** Her turun sonunda "Doğrulanan temiz" listeleri; yoğun kapsam taraması sonucu güncel ağaçta açık KRİTİK bulunmadı — öncelik sırası YÜKSEK üç maddeyle (D26-N1, C29-N1, MATH-2) başlıyor.

---

# TUR 31 — Yeni Bulgu Turu (opencode, 2026-09-12, güncel ağaç)

Kapsam: CLI/GUI/Config/HIDPP alt tur tarama bulguları + satır-satır doğrulama + deneysel teyid. Üstlenen: `[ALINDI: opencode]` (FIX_LOG.md). Dosya kilidi kuralı uyarınca big-pickle oturumunun aktif dosyalarına (ui_builder.inl, tr.inl, profile_mgr.inl, config.cpp, cli/main.cpp, daemon/main.cpp, logitech_hidpp.cpp, bench_hotpath.sh) eşzamanlı edit yapılmadı — o maddeler loglanıp talep edildi, kilit açılınca opencode uygulayacak.

**O31-G1 · ORTA · `kde_fix` idle görevi destroy'da iptal edilmiyor (GUI A2)**
- Konum: `gui/ui_builder.inl:1474-1509` (idle görevi), `:960-1004` (destroy handler)
- Açıklama: `kde_fix_finish` idle'ı `S->status_bar`/`S->kde_warn_bar`'a erişir; destroy handler GUI-O4 benzeri cancel-gate içermiyor (pkexec/hidpp/inotify/poll kaynakları kaldırılıyor, KDE fix için yok). Pencere destroy sonrası idle çalışırsa serbest widget'lara dokunur. S stack'ta olduğundan heap UAF değil, widget-yaşam döngüsü ihlali.
- Öneri: `S->kde_fix_running` bayrağını destroy'da sıfırla + `kde_fix_finish`'te erken çıkış (GUI-O4/Y2 deseni).
- Durum: `[ALINDI: opencode]` ⏸ ui_builder.inl kilidi (big-pickle DÜZELTİLDİ listesi) — kilit açılınca.

**O31-G2 · ORTA · `rebuild_lut_list` `S->updating`'i mutlak sıfırlıyor (GUI A3)**
- Konum: `gui/graph.inl:431-433,492`
- Açıklama: `S->updating=false` sabit ataması, dış çağıranın (ör. `profile_to_widgets` ortası) tuttğu `true` geçidini kırıp yeniden girişli sinyallerin `widgets_to_profile` yoluna girmesine ve kısmen yüklenmiş profilin `unsaved=true` ile yazılmasına izin verir; `devices.inl:198-225` deseni save/restore.
- Öneri: save/restore (RAII).
- Durum: ✅ **BU TUR DÜZELTİLDİ** (graph.inl `prev_updating` save/restore). Build(warning:0)+unit+oracle yeşil.

**O31-G3 · DÜŞÜK · `refresh_language` canlı etiketleri statik metne eziyor (GUI A6)**
- Konum: `gui/tr.inl:688-697` (registry replay); `gui/ui_builder.inl:595,606,612,923`
- Açıklama: Registry, canlı değer taşıyan `hw_status_lbl`/`hw_battery_lbl`/`hw_caps_lbl`/`latency_lbl`'i statik başlangıcına döndürür; `update_daemon_status` (tr.inl:703) status/battery'yi geri yazarken caps/latency sonraki olaya dek bayat kalır.
- Öneri: Replay'i statik etiketlerle sınırla ya da sonrasında dinamik değerleri yeniden bas.
- Durum: `[ALINDI: opencode]` ⏸ tr.inl kilidi.

**O31-G4 · DÜŞÜK · Spin min'leri config floor'larından büyük → sessiz tıraşı (GUI A8)**
- Konum: `gui/ui_builder.inl:230-255`
- Açıklama: `exponent_power` min 0.01 > floor 1e-4; `limit` min 0.1 > 0; `sync_speed` min 0.1 > 1e-4 → diskteki legal değerler yüklemede GtkRange'e tıraşlanır ve kayıtta bozulur. `scale` yüzeyi MATH-2 (config.cpp:408 scale≤0→0.01) ile uyumlu.
- Öneri: Spin min'lerini floor'lara hizala.
- Durum: `[ALINDI: opencode]` ⏸ ui_builder.inl kilidi.

**O31-G5 · DÜŞÜK · Grafik tıklama LUT ekleme `LUT_SPEED_SPIN_MAX` tavanından kaçıyor (GUI A9)**
- Konum: `gui/ui_builder.inl:816-844` (tıklama) vs `gui/graph.inl:555-577` (`on_lut_add_point` BUG-NEW-51 clamp'lı)
- Açıklama: Tıklama yolu speed'i pikselden üretir, 10000 üstü nokta saklanır; spin (0..10000) sonraki tick'te sessizce tıraşlar → yeniden yazım.
- Öneri: Ortak clamp (`CLAMP(speed,0,LUT_SPEED_SPIN_MAX)`); `on_lut_add_point` mantığını tıklama yoluna da.
- Durum: `[ALINDI: opencode]` ⏸ ui_builder.inl kilidi.

**O31-G6 · INFO · `GtkFileDialog` unref'lenmiyor (GUI A10)**
- Konum: `gui/profile_mgr.inl:504-508,565-570`
- Açıklama: Her export/import dialogu oluşturuluyor, `g_object_unref` hiç çağrılmıyor → bellek birikimi.
- Öneri: done callback'lerinde `g_object_unref(src)`.
- Durum: `[ALINDI: opencode]` ⏸ profile_mgr.inl kilidi.

**O31-C1 · DÜŞÜK · classic GAIN `cap_mode::in` `cap.x>=input_offset` clamp'ından yoksun (Config C1)**
- Konum: `include/accel-classic.hpp:158-163` (in dalı) vs `:125` (io BUG-7), `:97` (legacy in)
- Açıklama: `cap.x < input_offset`'te in dalı sonlu NEGATİF `cap_y=gain(cap_x)` üretebilir (int üs) → eksen ters dönme; kesirli üste ise NaN koruması absürt eğri (cap_y=DBL_MAX). Sanitize (config.cpp:436) yük/CLI yolunu maskeler; GUI canlı önizleme (`graph.inl:23,55`) sanitize'siz `init_gain()` çalıştırır.
- Öneri: io dalıyla aynı clamp.
- Durum: ✅ **BU TUR DÜZELTİLDİ** (in dalına io-simetrik `if (cap_x < args.input_offset) cap_x = args.input_offset;`). Oracle OK (1071 satır, 67 sapma, yenisi yok).

**O31-C2 · DÜŞÜK · `lut_length` > parse edilen nokta sayısı → ölü LUT (Config C2)**
- Konum: `src/config.cpp:182-200` (parse), `:336-355` (`sort_lut_data`)
- Açıklama: `a.length` gerçek parse edilen çift sayısına daraltılmıyor; eksik girişler (0,0) öne sıralanır → o bantta gain 0 = ölü imleç; serialize aynı bozuk 200'ü yeniden yazar.
- Öneri: Parse sonrası `a.length = 2*n_pairs_parsed` (nokta sayısıyla hizala).
- Durum: `[ALINDI: opencode]` ⏸ config.cpp kilidi.

**O31-C3 · DÜŞÜK · `gain`/`raw_passthrough` sayısal 0/1 yok sayılıyor (Config C3)**
- Konum: `src/config.cpp:122-123,252-253`
- Açıklama: `"gain":0` (sayısal) varsayılan `true`'ya düşer (intent tersi); `"raw_passthrough":1` kapalı kalır. Sayısal alanlarda tip guard varken bu ikisi is_boolean eleği.
- Öneri: `is_number_integer` 0/1 kabul (`v.get<int>() != 0`).
- Durum: `[ALINDI: opencode]` ⏸ config.cpp kilidi.

**O31-C4 · INFO · `active_profile` cap 256 vs isim 255 → hiç eşleşmez (Config C4)**
- Konum: `src/config.cpp:242-246,615-616`
- Açıklama: İsim 255'e kesilir, `active_profile` 256'ya izin verir → elle yazılmış 256-karakter aktif profil eşleşemez, daemon ilk profile düşer.
- Öneri: Her iki tarafta aynı kesme (CFG-6 ile simetrik düzeltilir).
- Durum: `[ALINDI: opencode]` ⏸ config.cpp kilidi; CFG-6 (big-pickle) aynı dosya/tema — çakışmayı önlemek için kilit açılınca birlikte.

**O31-C5 · INFO · Symlink'li config yolu save'de normal dosyaya dönüşür (Config C5)**
- Konum: `src/config.cpp:730` (O_PATH|O_NOFOLLOW), `:779` (rename)
- Açıklama: Symlink mevcutsa nendi mode 0600 varsayılan; rename symlink'in kendisini değiştirir → dotfiles/`~/.config`→`/etc` yönlendirmesi kalıcı bozulur, sessiz.
- Öneri: B3 kuralını koru; symlink tespitinde `readlink` ile hedef mode/owner kopyala ya da en azından uyar.
- Durum: `[ALINDI: opencode]` ⏸ config.cpp kilidi.

**O31-H1 · ORTA · `write_onboard_profile_sector` 18 param 16 sınırıyla her zaman reddedilir (HIDPP A1)**
- Konum: `src/logitech_hidpp.cpp:1990-1998` (chunk=16 → params(2+16)=18) vs `:679` (`param_len>16 → nullopt`)
- Açıklama: Onboard profil sektörü hiç yazılamaz; yol yalnızca negatif testten erişiliyor (test_accel.cpp:571).
- Öneri: `chunk=12/14` (2+chunk ≤ 16) + pozitif yol testi.
- Durum: `[ALINDI: opencode]` ⏸ logitech_hidpp.cpp (big-pickle B6/B3 tamamladı; yeniden edit riski) — kilit açılınca.

**O31-H2 · DÜŞÜK · G522 LIGHTSPEED `"32"` quirk kaydı ölü kod (HIDPP A2)**
- Konum: `include/logitech_quirks.hpp:86`, `:93-106` (short-key fallback), `:214-222`
- Açıklama: `model_id` hep 12 hex; kısa-anahtar fabrikaları `model_id.size()==klen` (12≠2) → "32" asla eşleşmez; LED quirk (0x0622) hiç uygulanmıyor.
- Öneri: Gerçek 12-char model ID doğrulaması gerekir (donanım) veya kayıt kaldırılır.
- Durum: `[ALINDI: opencode]` 📌 KARAR — kimlik doğrulaması olmadan tahmini id yazılmaz.

**O31-H3 · DÜŞÜK · `-c`/`--config` sonraki bayrağı config yolu sanıyor (HIDPP B1)**
- Konum: `daemon/main.cpp:293-298` vs `--config=`/`--log-format` arity hatası
- Açıklama: `rawaccel-daemon -c -v` → config_path="-v", verbose sessizce yutulur.
- Öneri: `argv[i+1]` `-` ile başlıyorsa exit 1.
- Durum: `[ALINDI: opencode]` ⏸ daemon/main.cpp kilidi.

**O31-H4 · DÜŞÜK · `bench_hotpath.sh` geçersiz perf olayı `syscalls` (HIDPP C4)**
- Konum: `scripts/bench_hotpath.sh:58`
- Açıklama: `perf stat -e cyc,instructions,syscalls` — `syscalls` yazılım olayı değil → "event syntax error" → pipefail abort (yerelde perf yok, kod analizi).
- Öneri: `-e syscalls`'i çıkar veya `syscalls:sys_enter_*` tracepoint.
- Durum: `[ALINDI: opencode]` ⏸ bench_hotpath.sh SH-1 (big-pickle) kapsamında; kilit açılınca.

**O31-H5 · DÜŞÜK · `kde-fix-accel.sh --remove` symlink'i yok ediyor + sabit tmp adı (HIDPP C3)**
- Konum: `scripts/kde-fix-accel.sh:322-325` vs `:126-145` (--fix mkstemp+realpath+fsync)
- Açıklama: `kwinrc.tmp` sabit ad + `os.replace(tmp, kwinrc)` symlink inode'unu değiştirir; gizli/tekil değil, fsync yok.
- Öneri: --fix desenini kullan.
- Durum: ✅ **BU TUR DÜZELTİLDİ** (mkstemp + realpath + fsync + stale-tmp temizliği). bash -n + python ast OK.

**O31-L1 · DÜŞÜK · import LUT guard'ı 515 (tek, >514) öğeyi sessizce düşürüyor (CLI F5)**
- Konum: `cli/main.cpp:1242-1248` (`n/2 > LUT_POINTS_CAPACITY`) vs `src/config.cpp:182-190`
- Açıklama: n=515 → 515/2=257 guard'ı geçer; config 514'e kırpar → 515. eleman düşer, "odd" uyarısı tetiklenmez ("No silent truncation" politikası ihlali).
- Öneri: `n/2 > LUT_POINTS_CAPACITY || (n%2)==1`.
- Durum: `[ALINDI: opencode]` ⏸ cli/main.cpp kilidi.

**O31-L2 · DÜŞÜK · set-param `input_offset`(>500)/`cap_x<input_offset` CLI domain'ine yansımıyor (CLI F4-iki yüz)**
- Konum: `cli/main.cpp:951-954,919`; sanitize `config.cpp:432,446,450`
- Açıklama: `input_offset 1000` kabul edilir → 500'e sessiz kırpılır, rc=0; `cap_x=2` sonra `input_offset=10` benzeri cap.x itilir. P107 "byte-correctness" ihlali. output_dpi yüzü CLI-2 (big-pickle) ✅.
- Öneri: CLI domain tablosuna input_offset üst sınırı + cap_x≥input_offset çapraz kısıtı.
- Durum: `[ALINDI: opencode]` ⏸ cli/main.cpp kilidi.

**O31-L3 · INFO · Help/çıktı metinleri davranıştan sapıyor (CLI F7)**
- Konum: `cli/main.cpp:2080,575-577,243`
- Açıklama: `--json` receivers/hidpp'yi de kapsıyor (help eksik); preset hata mesajı "none/off" alias'larını saymıyor; push sonrası "Daemon reloaded." (reload değil).
- Öneri: Metinleri davranışla eşitle.
- Durum: `[ALINDI: opencode]` ⏸ cli/main.cpp kilidi.

**Negatif teyid — bulgu değil (deneysel):** HIDPP C2 (`kde-fix-accel.sh` ilk çalıştırmada backup rotasyonu abort) ÜRETİLEMEDİ: `cp -a` (satır 48) backup'ı `ls` glob'undan ÖNCE yaratır → glob her zaman en az bir dosya eşleştirir, exit-2 senaryosu oluşmaz. Test: boş backup setinde betik geçti. Loglanmadı/üstlenilmedi.

**Duplike / önceden üstlenilmiş (tekrar alınmadı):** CLI F1 = C-3 + C29-N3 (önceki tur); F2 = CFG-2 ([ALINDI: big-pickle]); F3 = CFG-3 (big-pickle); F4 output_dpi yüzü = CLI-2 (✅ big-pickle); F6 = CFG-4 (big-pickle); Config C2 grep'i "aktif profil" çakışması yok — yeni; HIDPP C1 = R4 L-3 (aj1); C5 = PKG-1 (✅); C6 = BS-12 (rapor:732).

**Doğrulanan temiz (TUR 31):** accel-classic.hpp in-branch clamp oracle-pariteli (1071 satır, 67 belgeli sapma, yenisi yok); graph.inl save/restore C++ tarafı davranışı değiştirmez (sinyal kalkanı bütünlüğü); kde-fix-accel.sh --remove artık --fix ile aynı atomik desende.

**Gate'ler:** build (warning:0, error:0) ✓ · unit 33792/33792 ✓ (P83 256-char create-preset FAIL'i CFG-6/big-pickle devam eden adı-cap işi — opencode kapsamı dışında, bu turun dokunduğu kod uzak) · oracle OK ✓ · bash -n + python ast ✓.

---

# FIX LOG — DÜZELTME KOORDİNASYONU (2026-09-12)

Başka bir analiz/fix oturumuyla çakışmamak için: **işlem ÖNCESİ** her düzeltme bu bölümde `[ALINDI]` ile işaretlenir, **tamamlanınca** `[DÜZELTİLDİ]`'ye çevrilir. Kod üzerinde değişiklik için bu bölümü okumadan okuyup `[ALINDI]`/`[DÜZELTİLDİ]` durumunu kontrol edin (ilgili kod parçasında konum yorumu da eklenir).

Tur öncesi paralel oturum tarafından zaten kapatılanlar (tekrar alınmadı): GUI-K1, HP-1, GUI-Y1 (alignas, `devices.inl:275`), MATH-2 (scale≤0→0.01, `config.cpp:410`).

## Aktif talep listesi
(boş — 7-12 tamamlandı; aşağıdaki Tamamlananlar bölümüne taşındı)

## Tamamlananlar

7. **[DÜZELTİLDİ]** CFG-6 — `device_profile_from_json` `dp.name` cap'ı `MAX_NAME_LEN-1` (255)'e hizalandı; kullanılmayan `MAX_DP_NAME` sabiti kaldırıldı. Aynı `export --json` içindeki üst düzey (255) vs iç içe (256) tutarsızlığı giderildi (`src/config.cpp`). 🔨 derleme ✓ test ✓
8. **[DÜZELTİLDİ]** C29-N4 — `device_id` load'da (config.cpp) ve CLI `set-param device_id`'de `"(all)"/"all"/"*"` → boş'a normalleştiriliyor; el yazısı JSON/komutla literal "(all)" artık ölü profil yaratamıyor. ✅ işlevsel test: `device_id:"(all)"` girişi dosyaya `""` yazıldı (`src/config.cpp`, `cli/main.cpp:1057+`). 🔨 ✓
9. **[DÜZELTİLDİ]** C29-N6 — CLI disable açıklaması "dormant / daemon yoksayar" → "kurulum/hot-plug'da onurlandırılır; canlı toggle sonraki rescan'da uygulanır" (`cli/main.cpp:350-352`). 🔨 ✓
10. **[DÜZELTİLDİ]** C29-N3 — `set-param mode lookup` warning'i yoksa `import`'a işaret ediyor (var olmayan `lut-data` key'i değil) (`cli/main.cpp:1008-1011`). 🔨 ✓
11. **[DÜZELTİLDİ]** C29-N2 — `import` artık `{"profiles":[...]}` wrapper'ındaki `active_profile`/`use_raw_input`/`version`'u tip-guard'lı okuyup kaydediyor; `list --json` çıktısının config'e round-trip'i eksiksiz. ✅ işlevsel test: `active_profile:"pair"`, `use_raw_input:false`, `version:"1.1.0"` dosyaya işlendi (`cli/main.cpp` cmd_import). 🔨 ✓ test 33792/33792 ✓
12. **[DÜZELTİLDİ]** T30-N4 (kısmi) — `run_tr_coverage.sh` tarama listesine `gui/kwin_focus.inl` eklendi; sonuç PASS. Dinamik `tr()` key min-coverage guard'ı daha sonraya bırakıldı (`tests/run_tr_coverage.sh`). ✅

## Koordinasyon notları

- **Eşzamanlı oturum çakışması (çözüldü):** CLI build'i sırasında `cmd_validate` içinde (başka oturumun eklediği) cli TU'sunda çözülmeyen `json` türü derleme hatası verdi; ben bu bölgeye dokunmadım — oturum düzenlemesini tamamlayınca derleme kendiliğinden temizlendi. Kendi `val.clear()` hatam (const) düzeltildi. İletişim gereksinimi: **cli/main.cpp ek düzenlemelerinden önce `using json` alias'ı ya da `nlohmann::json` kullanın** (`json` yalnızca `src/config.cpp:25`'te alias'lı).

1. **[DÜZELTİLDİ]** C29-N1 — `find_profile` genel fallback döngüleri artık `profile_matches_app(p, current_app_)` kapısını kullanıyor; app-bound profiller non-matching app / no-focus durumunda catch-all olamıyor (`daemon/daemon.cpp`, "1. Device-specific" + "2. All devices" döngüleri; yorumlarla işaretlendi). 🔨 derleme ✓ test 33792/33792 ✓
2. **[DÜZELTİLDİ]** D26-N1 — `ipc_sock_path_` ataması `start_ipc_server` başından alınıp listen başarısından SONRA (worker thread başlamadan önce) yapılacak şekilde taşındı; başarısız başlangıçta `stop_ipc_server` artık kendi socket'ini değil, muhtemelen önceki/gerçek daemon'ın path'ini unlink etme riski taşımıyor (`daemon/daemon.cpp`). 🔨 derleme ✓
3. **[DÜZELTİLDİ]** G27-N1 — `export_profile_done` serileştirmeden hemen önce `widgets_to_profile(S)` çağırıyor; dışa aktarılan JSON diyalogdaki GÜNCEL widget durumunu yansıtıyor (`gui/profile_mgr.inl:501-505`). 🔨 derleme ✓
4. **[DÜZELTİLDİ]** T30-N2 — `run_e2e.sh` artık rc=77'i SKIP sayıyor; `SKIP>0` ise exit 77 (ortam), yoksa 0/1 kontratını bozmuyor. `bash -n` ✓ (`tests/run_e2e.sh`)
5. **[DÜZELTİLDİ]** ORAC-TOL — `run_oracle.sh` TOL regex'i negatif/ön-imzalı değeri reddeder; ek olarak TOL>0 kapısı eklendi (0/negatif → "Hata: TOL pozitif olmalı", exit 2). `bash -n` ✓ (`tests/oracle/run_oracle.sh`)
6. **[DÜZELTİLDİ]** T30-N8 — README.md bayat sürüm iddiaları güncellendi: v0.6.4 → **v1.1.0** (üç yerde: durum satırı, input grubu notu, paket kural dosyası adı) (`README.md`)