# Bug Hata Raporları — Linux RawAccel

Analiz tarihi: 2026-09-11
Kapsam: `/home/a/Masaüstü/Linux-Raw-Accel-main` (RawAccel Linux v0.6.4)
Yöntem: Baştan sona (start-to-end) 5 ayrı detaylı analiz turu. Her tur farklı bir uzmanlık açısıyla tüm kaynak kod taranmıştır.
Durum: 2026-09-11 düzeltme seanslarında ele alınan bulgular (D-1..D-9, C-1..C-10, CR-1, H-1..H-3, M-1..M-8, R1-01..R1-08, R2-01, R2-04, L-2, L-5, R5-S-4/5) fixed olarak listeden çıkarılmıştır. Aşağıdaki maddeler halen açık veya bilinçli/belgeli tasarımdır.

---

# TUR 1 — Daemon / Olay Döngüsü / Eşzamanlılık / Hot-Path

Odak: `daemon/*`, `src/*`, `include/*`, `cli/main.cpp`, olay döngüsü, seqlock/telemetry, sinyal işleme, hot-plug, uinput yazımı, SYN_DROPPED, FD/hafıza sızıntıları, gecikme bütçesi.

**R1-06 · MEDIUM · SYN_DROPPED olay yapıştırması (event glue)**
- Konum: `daemon/daemon.cpp` (SYN_DROPPED işleme)
- Kategori: Mantık
- Açıklama: SYN_DROPPED sonrası SYN_REPORT'a kadar olaylar atılıyor; aracı durum (ara REL/X arasında drop) tam ele alınmamış olabilir.
- Not: Bu özellik AGENTS.md'de bilinçli tasarım olarak belgeli; dikkatle doğrulanmalı.

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

**R2-02 · LOW · Rıza olmadan kullanıcı config yazımı**
- Konum: `gui/ui_builder.inl:1458-1463` (+ `:1332-1343`, `:1362-1376`)
- Kategori: UX / config değişikliği
- Açıklama: `on_activate()` her GUI açılışında (`is_kde` ise) koşulsuz `kde_write_flat_accel()` çalıştırıyor; kwinrc+kcminputrc yazımı, Adaptive→Flat "toggle dance" (250 ms nanosleep), ardından `qdbus6`/`qdbus` ve `kcminit kcm_mouse` süreçleri başlatılıyor. Hiçbir şey değişmese bile her açılışta yazım/süreç çalışıyor.
- Öneri: Otomatik düzeltmeyi bir kez yap (`~/.config/rawaccel/` marker) veya "Fix Now" düğmesine bağla.

**R2-03 · LOW · Bağlı değilken cihaz gösterimi tutarsız**
- Konum: `gui/widgets_sync.inl:339-350` + `gui/devices.inl:186-198`
- Kategori: Görüntüleme
- Açıklama: Profilin bağlı olduğu cihaz takılı değilken `profile_to_widgets()` eşleşme bulamıyor ve "All devices (default)" gösteriyor; kalıcı bağlama yalnız kayıtlı saklanıyor (yıkıcı değil, yanıltıcı).
- Öneri: Bağlı `device_id` için soluk "(unplugged)" girişi ekle ve onu seç.

**R2-05 · LOW · Sessiz veri kaybı — isim çakışmasında üzerine yazma**
- Konum: `gui/widgets_sync.inl:457-485`
- Kategori: Veri kaybı
- Açıklama: Save-Profile-As geri çağrısı mevcut isimle eşleşirse onay almadan üzerine yazıyor; `on_apply_clicked` aynı diyalogu kullandığından çakışan isimle "Apply" ilgisiz bir profili diskten sessizce değiştirebilir.
- Öneri: İsim çakışmasında onay iste; veya Apply (bellek/reload) ile Save (kalıcı) ayrımı yap.

**R2-06 · LOW · pkexec hata raporlaması**
- Konum: `gui/widgets_sync.inl:383-406` + `:542-571`
- Kategori: Hata raporlama
- Açıklama: `pkexec_systemctl_async`/doğrudan `pkexec` yolları exec hatasında `_exit(127)` yapıyor; ebeveyn çocuk çıkış kodunu hiç kontrol etmiyor. UI "Başlatılıyor…" başarı metnini pkexec eksik veya polkit iptal olsa bile gösterir.
- Öneri: `waitpid` kısa zaman aşımı veya `g_subprocess`; 127/iptal durumunu raporla.

**R2-07 · LOW · Fare testi etiketleri bayat kalıyor**
- Konum: `gui/mouse_test.inl:249-257`
- Kategori: Görüntüleme
- Açıklama: Daemon yanıt verdiğinde ama henüz hareket örneği yoksa (`in < 0`) sadece durum etiketi "Awaiting motion…" oluyor; in/out/gain etiketleri eski sayıları göstermeye devam eder.
- Öneri: `in < 0` dalında üç etiketi "—" yap.

**R2-08 · INFO · HID++ panel thread sızıntısı + kalıntı yarış**
- Konum: `gui/hidpp_panel.inl:473-491,396-398`
- Kategori: Kaynak / yarış
- Açıklama: 1000 ms `hw_notification_tick` her tick'te hiçbir şey değişmese bile kısa ömürlü GThread doğuruyor; A1-17'deki idle-callback `S->hidpp_devs[r->idx]` yeniden okuması, tarama idle'ı yenilenmiş vektörde eski index'i yeniden okursa kalıntı riski taşır.
- Öneri: Tarama sonucunu version-tag ile işaretle, notify idle uyuşmazlıkta vazgeçsin; cihaz seçili değilse tick'i atla.

---

# TUR 3 — Matematik / Hızlandırma Algoritmaları / Config Serileştirme

Odak: `include/accel-*.hpp`, `include/rawaccel.hpp`, config sanite etme, JSON, presets, cap/legacy/gain varyantları, LUT, EMA, edge case'ler (NaN/Inf/0/sıfıra bölme), orakl (oracle) kapsamı.

**R3-NEW-1 · LOW · `export` (isimsiz) çıktısı yeniden import edilemiyor (round-trip kırılması)**
- Konum: `cli/main.cpp:1051-1055` (export), `cli/main.cpp:1067-1091` (import)
- Kategori: Round-trip / mantık
- Açıklama: `cmd_export` boş isimle satır başına bir `profile_to_json` basar; `cmd_import` tüm içeriği TEK JSON nesnesi olarak ayrıştırır. `rawaccel-cli export > all.json && rawaccel-cli import all.json` `profile_from_json` ayrıştırma hatası verir.
- Öneri: Boyut != 1 iken `{"profiles":[...]}` sarmalı; veya import'a nesne akışı desteği.

**R3-NEW-2 · LOW · Power üs tabanı [1e-4, 1e-3) bandında referanstan ayrılıyor; oracle yakalayamıyor**
- Konum: `include/accel-power.hpp:35-36` (`n = max(ep, 1e-3)`); `cli/main.cpp:877-879`, `src/config.cpp` (sanitize aralığı `[1e-4, 5]`)
- Kategori: Diferansiyel doğruluk
- Açıklama: Sanitize `exponent_power ∈ [1e-4, 5]` izin verirken hesaplama 1e-3'e tabanlıyor; vendored referans ham değeri kullanıyor. Oracle grid'inde minimum exponent 0.3 (`oracle_cases.hpp`), yani band test edilmiyor ve `known_deviations.txt`'te satırı yok. Sayısal etki küçük ama sessiz, belgelenmemiş sapma.
- Öneri: Oracle'a ep=5e-4 satırı ekle veya known_deviations'a taban belgesi yaz.

**R3-NEW-3 · LOW · Bağlantıdan / raw→accel reload'dan sonra İLK etkinlerce olay gain≈1'de bastırılıyor**
- Konum: `daemon/daemon.cpp:1388-1395`; `daemon.hpp:48` (`last_time_ms`); raw passthrough `daemon.cpp:1525-1535`
- Kategori: Mantık / doğruluk
- Açıklama: `last_time_ms` başlangıçta 0 ve raw passthrough `flush_motion`'ı atladığı için güncellenmiyor. İlk hızlandırılmış olayda `time_ms = now − stale` → `DEFAULT_TIME_MAX` (100 ms) klamplaması → `ips_factor = dpi_factor/100` → hız ~0 → gain ≈ 1 (yalnız bir olay). Bağlantı durumu "correct" yorumlu; raw→accel geçişi ele alınmamış.
- Öneri: Profil uygulandığında ve raw moddan çıkarken `last_time_ms`'i tazele/başlat.

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
- **F-1 · MEDIUM** — HID++ identify döngüleri toplam bütçe yok (`src/logitech_hidpp.cpp`): FEATURE_SET ≤256×900 ms ≈ 230 s, ardışık, tek worker'da. (= BUG-01)
- **F-4 · INFO** — Canlı cihaz eşleşmeyen reload tam teardown+setup yapar (~100-150 ms mouse drop; sanal cihaz yeniden oluşturulur).
- **F-5 · INFO** — Üst düzey `use_raw_input` bayrağı saklanıyor ama etkin değil (raw profil bazlı).

**R3 · Doğrulanan düzeltmeler (2026-09-11 seanslarında fixed, listeden çıkarıldı)**
- BUG-02 (power üs tabanı), P155 io-dejenere korumalar, P150 virgül→nokta JSON, BUG-NEW-81 epoll streak→request_stop, D-1 config_path_ mutex, D-2/D-3 epoll_ctl DEL kontrolleri, D-5 seqlock 64-spin, D-6 PID stale-clear liveness, D-7 IPC bind EADDRINUSE, D-8 IPC deadline yanıtı, D-9 EINTR retry, C-1 aktif profil senkronu, C-2 import 256 isim, C-3 distance_mode max eşitlemesi, C-4 IPC 64 KiB tavan kaldırma (16 MB guard), C-5 validate 256, C-6 safe_save, C-7 boş profil mesajı, C-8 boş-LUT lookup uyarısı, C-9 config kaydedilemez ise ERROR, C-10 import boyut sınırı, BUG-20 boş/çift isim, N-atomik save (fsync dosya+ebeveyn), M-BUG-14 perm koruması.

---

# TUR 4 — Scripts / setup.sh / systemd / udev / polkit / Paketleme / CI

Odak: shell betikleri, systemd hardening, udev, polkit, packaging (PKGBUILD), CMake, KDE fix, CI, sürüm tutarlılığı, çapraz-dağıtım.

**M-5b · MEDIUM · bug_watch başarısız fix'i yeniden tetiklemiyor (MARK ilerletme)**
- Konum: `scripts/bug_watch.sh:75` (opencode'dan ÖNCE MARK yazımı), `:118-124`
- Kategori: Betik mantığı
- Açıklama: Lock PID-reuse kısmı (M-5) 2026-09-11'de düzeltildi. Kalan açık yan: `md5sum … > "$MARK"` opencode'dan ÖNCE yazılıyor; başarısız çalışma (exit≠0, "DÜZELTİLEMEDİ" raporu saklı) işaretlenmiş sayılır, watcher bir daha denemez. (Sonsuz 2 sn retry infiloop'una düşmeden "denendi/retry sayacı" ile sınırlı yeniden deneme gerekir.)
- Öneri: MARK'ı yalnız başarılı çalışma sonrası ilerlet, "denendi" + retry sayacı.

**M-6 · MEDIUM · setup.sh apt dalı Ubuntu < 24.04'te sert çöküyor**
- Konum: `setup.sh:113-121`
- Kategori: Çapraz-dağıtım
- Açıklama: `apt-get install … qt6-tools-dev-tools` yalnız Ubuntu ≥ 24.04/Debian ≥ 13'te var; 20.04/22.04'te "Unable to locate package" → betik ölür; `:141-154` doğrulama bloğu hiç çalışmaz. Doğrulama ayrıca `qdbus6` kontrol etmiyor.
- Öneri: `qdbus6`/`qdbus-qt6` varlığını net mesajla doğrula; `qt6-tools-dev-tools`'u dağıtım sürümüne göre kapıla (fallback `qt6-tools`/eski paket veya "≥24.04 gerektirir" uyarısı).

**R4 · LOW bulgular**
- **L-1** `tests/oracle/run_oracle.sh:24-31` — `--tolerance <val>` parse sonrası yeniden doğrulanmıyor; `--tolerance abc` python traceback ile çöker. Regex'i option parse sonrası yeniden çalıştır.
- **L-3** `scripts/build.sh:64-68` — FORTIFY kontrolü yalnız derleyici *varsayılan* makrolarını (`$CXX -dM -E`) sınıyor, gerçek env `CFLAGS`'i değil; "CMakeLists'i yansıtır" yorumu abartılı. Şu an zararsız ama yanlış garanti.
- **L-4** `scripts/bench_hotpath.sh:19` — `g++ -O3 -march=native` sabit; `$CXX` ve `RAWACCEL_PORTABLE` görmezden geliniyor. `$CXX` kullan, `-march=native` bırak.
- **L-6** `setup.sh:63-64` — pacman-conflict prompt `read -r _ || true`; kapalı stdin ile kullanıcıya sorulmaz, bilinen co-existence riskiyle kurulum ilerler.
- **L-7** `setup.sh:261` — backup adı 1-sn granülerliği (`date +%Y%m%d-%H%M%S`); aynı saniyede yeniden kurulum önceki backup'ı sessizce ezer.
- **L-8** `scripts/uninstall.sh:24-26` — yalnız `rawaccel-daemon` pkill ediliyor; çalışan `rawaccel-gui` hayatta kalır. `:91`'de `>/dev/null 2>&1` fix hatalarını gizliyor.
- **L-9** `scripts/bug_watch.sh:20,32` — `STATE_DIR` `/tmp/rawaccel-bug-watch`'e (başka kullanıcı önceden oluşturabilir) ve `printf >> $LOGFILE` — çok-kullanıcılı log sahtelemesi/symlink yüzeyi. Per-user dir veya `install -d -m 700`.
- **L-10** `ci.yml:35` — uyarı kapısı tüm logda `grep -E "warning:|error:"`; o stringleri içeren masum çıktı false-fail yapar. Yalnız derleyici satırlarını filtrele.
- **L-11** `gui/widgets_sync.inl` `pkexec_systemctl_async` exec hata yüzeyinde `pid > 0` döner (`:394-398,405`); G1 double-start düzeltildi ama "exec hatasında başarı raporu" kenar durumu kaldı (systemd yolu `has_systemd_rawaccel_unit` ile kapılı, küçük maruziyet).

**R4 · Doğrulananlar (bulgu değil)**
- Sürüm tutarlılığı: `rawaccel-base.hpp:9 "0.6.4"` == `CMakeLists.txt` VERSION == `PKGBUILD:14` == `.SRCINFO` == çalışan binary'ler.
- CI perf-gate regex doğru (`$`-ankra, SUMMARY satırları); median-of-3 sağlam.
- Systemd hardening güçlü: `PrivateNetwork`, `SystemCallArchitectures=native`, `ProtectProc`, `UMask=0077`, `ReadWritePaths` tutarlı. (M-7: `CAP_DAC_READ_SEARCH`'ün bounding set'ten çıkarılmasıyla birlikte.)
- pacman dalı `-S` (kısmi yükseltme yok) + uyarı kapısı var (P121/P131).
- Dep paritesi üç dalda AGENTS.md politikasıyla eşleşiyor (apt sürüm açığı M-6 hariç).
- udev kuralları bilinçli güven sınırı (F-2 INFO); setup.sh kullanıcıyı `input` grubuna ekler.

---

# TUR 5 — Güvenlik / Kaynak Sızıntıları / Erişim Kontrolü / Güven Kenarlıkları

Odak: ayrıcalık yükseltme, path traversal, symlink saldırıları, TOCTOU, dosya izinleri, root daemon config doğrulama, IPC kimlik doğrulaması (locals non-root inject edebilir mi?), socket izinleri, PID dosyası, sinyal sahteleme, FD/hafıza/mutex/thread/inotify sızıntıları, yok sayılan hata dönüşleri.

**R5-S-1 · MEDIUM · GUI PID keşfi `/proc/<pid>/comm` güveniyor (sahtelenebilir)**
- Konum: `gui/daemon_comm.inl` (daemon PID kimlik doğrulaması)
- Kategori: Güvenlik / sahte kimlik
- Açıklama: GUI, hedef daemon'ın kimliğini `/proc/<pid>/comm` == `rawaccel-daemon` ile doğruluyor. Herhangi bir süreç `prctl(PR_SET_NAME)` ile ismini `rawaccel-daemon` yapabilir; `kill`/sinyal iletiminde yanlış hedefine gönderilebilir. IPC data yolu ayrıca socket izni (0660 root:input) ile korunuyor.
- Öneri: PID dosyasının sahipliğini/start-time'ı (`/proc/<pid>/stat` field 22) ve gerçek binary'yi (`/proc/<pid>/exe` readlink) kontrol et.

**R5-S-2 · MEDIUM · F-1 HID++ identify bütçesi iddiası doğrulanmadı; güncel durum kayıt altına alındı**
- Konum: `src/logitech_hidpp.cpp` (FEATURE_SET / device_info / GetDpiList döngüleri)
- Kategori: Bloklama / tutarlılık
- Açıklama: `Bug Hata Raporları.md` geçmiş bölümleri "HID++ bütçesi uygulandı" iddiasında (F-1 fixed). Tur 2, `kIdentifyBudget` 20 s + `kIdentifyTimeoutStreak` 3'ü `src/logitech_hidpp.cpp:29,32` ve döngülerde TEYİT etti (1122-ish, 1430-ish satırlar). Tur 5 bu iddiayı mevcut ağaca karşı yeniden doğrulama sürecini gerektirir; rapor turu içinde kodda bütçe sabiti görüldü. Durum: kısmen doğrulandı, dosyada kalıcı kayıt önerilir.

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

**MEDIUM (önem sırası):** R5-S-1 (PID comm spoof), M-6 (apt <24.04 çöküşü), R2-02 (rızasız kwinrc yazımı), R1-06 (SYN_DROPPED glue), R2-05 (isim çakışması üzerine yazma), M-5b (bug_watch retry eksikliği), F-1 (HID++ bütçesi — S-2 doğrulama sürecinde).

**LOW/INFO:** R1-11…R1-14, R2-03, R2-06…R2-08, R3-NEW-1…6, R4 L-1, L-3, L-4, L-6…L-11, R5-S-2, R5-S-6…R5-S-9.

**Bilinçli / belgeli (düzeltme gerektirmez):** R1-09 (lat_stats mutex), R1-15 (is_physical_mouse politikası), R5-S-6 (IPC güven sınırı), F-4/F-5, R3-NEW-4/5/6.

**Notlar:**
- Ağaç aktif düzenleme altında; satır numaraları 2026-09-11 son haliyle kısmen eski olabilir.
- 2026-09-11 seanslarında düzeltilen tüm maddeler bu rapordan tamamen çıkarılmıştır; bunlar git geçmişinde ve çalıştırılan testlerde (33766/33766 + ASan + TR coverage + oracle) izlenebilir.