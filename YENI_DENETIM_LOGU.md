# Yeni Denetim Logu — 2026-09-12

Kaynak: `Bug Hata Raporları.md` (TUR 1–25) satır satır incelendi; her madde mevcut
kaynak ağaca karşı dosya+satır düzeyinde doğrulandı. Bu dosya **yeni açık
maddeleri**, **FIXED teyitlerini** ve **raporda olmayan yeni bulguları** toplar.

- FIX_LOG.md'ye dokunulmadı ("aynı dosyada aynı anda çalışmayın" kuralı).
- Build teyidi: `bash scripts/build.sh` → warning:0 error:0 exit:0;
  `build-manual/rawaccel-{daemon,cli,gui}` üretildi.
- Durum kolonu: **AÇIK** (düzeltilmesi önerilir), **KARAR** (bilinçli/iseyerek bırakılmış).

## 1) Doğrulanmış AÇIK maddeler

### Daemon
| ID | Öncelik | Yer | Özet |
|----|---------|-----|------|
| HP-2 | Yüksek | daemon/daemon.cpp (EBUSY → deny_reopen ~5s) | Kabloya tekrar takma: EBUSY → deny_reopen → cihaz "skipped"; takma sonrası yeniden tarama/retry tetikleyicisi yok. GUI eşzamanlıklık ve parmak taşıma testi yok. |
| POLL-1 | Orta | daemon/daemon.cpp (detect_polling_rate ~224-276) | Tespit salt sysfs (`bInterval`/`speed`) — evdev zaman damgası yok; High-speed dalında bInterval=1 → 8000Hz değil 1000Hz. |
| POLL-2 | Orta | daemon/daemon.cpp | USB SuperSpeed (5000) full-speed dalına düşüyor; 2000/4000/8000Hz ile 125–1000 ayrımı yok. |
| POLL-4 | Düşük | daemon/daemon.cpp | sysfs bilgisi yoksa 0 Hz gösterimi / realpath fallback. |
| POLL-5 | Düşük | daemon/daemon.cpp | (açık madde, satır teyidi ayrışık) |
| PERF-2 / PERF-5 | Orta | daemon/daemon.cpp (push_config) | Config JSON'u iki kez serileştiriliyor; push path'i tekkezîne indirilebilir. |
| PERF-1 / PERF-3 | Orta | daemon/lat_stats.hpp | Her motion kaydında mutex lock/unlock (hot path). |
| TH-2 | Düşük | daemon/daemon.cpp (push_cfg_mu_) | IPC thread push_cfg_mu_'da sınırsız bekler; daemon kilitlerse GUI/CLI asılı kalır. |
| TH-4 | Düşük | daemon/daemon.cpp (telem seqlock) | 64 spin × 6 double, devices_mutex_ altında; yazarlar arasında starve riski. |
| TH-5 | Düşük | daemon/daemon.cpp | config_path_ çekirdek/thread'ler arasında senkronize değil. |
| TH-7 | Düşük | daemon/main.cpp (signal) | Sadece atomic flag; "lock-free değil" notu hâlâ geçerli (davranış olarak güvenli). |
| ERR-3 | Düşük | daemon/daemon.cpp (unix client) | Exception yolunda client FD sızıntısı (RAII yok). |
| OPT-1 | Düşük | daemon/daemon.cpp:1497 | epoll_wait 10 ms sabit; koşullu/dinamik değil. |
| IPC-1 / TS-3 / R1-11 | Orta | daemon IPC handler | IPC thread'inde senkron save_config (disk I/O) — blokaj riski. |

### Config / CLI
| ID | Öncelik | Yer | Özet |
|----|---------|-----|------|
| CFG-1 | Orta | cli/main.cpp (import) | `import` migrate_config'ü bypass eder; eski sürüm config'i şemaya taşınmadan yüklenir. |
| CFG-2 | Düşük | cli/main.cpp:658-681 | `validate` içinde ulaşılamayan/dead dallar; uyarı dalı etkisiz. |
| CFG-3 | Orta | src/config.cpp:624-634 + cli/main.cpp:472,548,579,1291 | MAX_PROFILES yalnız LOAD'da zorlanıyor; create/dedup/create-preset/import append yolları kontrolsüz ekler. |
| CFG-4 | Orta | daemon/daemon.cpp:613-620 + cli | `push_config` cevabı "ok:true" persist edilmeden döner; CLI 2s timeout → SIGHUP fallback yanlış "success" basabilir. |
| CFG-5 | Düşük | src/config.cpp (save_config) | Kaydetme RAWACCEL_VERSION damgalar, bilinmeyen key'leri düşürür; downgrade veri kaybı. |
| CFG-6 | Düşük | src/config.cpp (name cap) | isim/device_id cap 256 vs sanitize tip-guard 255 çelişkisi. |
| CFG-7 | Düşük | src/config.cpp:828+ (find_config_path) | XDG_CONFIG_HOME okunmuyor; SUDO_USER + getpwnam dışında düz ~/.config. |
| CLI-1 | Düşük | cli/main.cpp | (açık madde) |
| CLI-2 | Düşük | cli/main.cpp | (açık madde) |
| CLI-3 | Düşük | cli/main.cpp | (açık madde) |
| C-7 | Düşük | cli/main.cpp:1353-1361 (cmd_stop) | Daemon zaten durmuşsa `stop` rc=1 döner (bilgi kokusu verilse iyi olur). Tartışılır. |
| C-8 | Düşük | cli/main.cpp:2306-2307 | Bilinmeyen komutta `print_help()` stdout'a basıyor (stderr olmalı). |

### GUI
| ID | Öncelik | Yer | Özet |
|----|---------|-----|------|
| GUI-Y3 | Orta | gui/hidpp_panel.inl:577 | HID++ sorgusu meşgulken cihaz seçimi sessizce düşürülüyor; kullanıcı geri bildirimi yok. |
| GUI-O4 | Düşük | gui/widgets_sync.inl:448 | `g_child_watch_add` return id kaydedilmiyor/iptal edilmiyor; teardown'da callback riski (düşük, pkexec hâlen koşarken pencere kapanırsa). |

### HID++ / Presets / Math
| ID | Öncelik | Yer | Özet |
|----|---------|-----|------|
| PRE-2 | Düşük | include/presets.hpp:60-72 | Preset değerleri 2 desimal precision'da. |
| PRE-3 | Düşük | include/presets.hpp:133-134 | Apex preset ±17 apt. |
| MATH-1 | Orta | src/config.cpp:398-399 + include/accel-classic.hpp | Neg accel serbest; cap ile düşük hızda ıraksak spike riski (init_gain). |
| B4 | Düşük | src/logitech_hidpp.cpp:1316 | ModelID 2-hex vs 4-hex (Logitech 2-byte). |
| B6 | Düşük | src/logitech_hidpp.cpp:~220 | Varsayılan dal çevrimdışı/etkisiz. |
| B7 | Düşük | src/logitech_hidpp.cpp:506-507 | `data[6]==0` şartı; bazı ürünlerde ad eksik. |
| B8 | Düşük | src/logitech_hidpp.cpp:1488 | Bolt cihaz adı hiç okunmuyor. |

### Scripts / Paketleme / CI
| ID | Öncelik | Yer | Özet |
|----|---------|-----|------|
| BS-2 | KARAR | setup.sh:546-549 | build-before-clean M-BUG-20 yorumuyla bilinçli bırakılmış — düzeltme GEREKMEZ. |
| BS-3 | Düşük | tests/run_tests_asan.sh | (açık madde) |
| BS-4 | Düşük | setup.sh:296-324 | dnf/apt kol bakımı (paket listesi). |
| BS-5 | Düşük | tests/run_fuzz.sh | (açık madde) |
| BS-6 | Düşük | setup.sh | explicit-deny toka koruması. |
| BS-7 | Düşük | setup.sh | (açık madde) |
| BS-8 | Düşük | scripts/build.sh | Alıntısız CFLAGS/CXXFLAGS — bkz. YENİ-1. |
| BS-11 | Düşük | scripts/rawaccel.service | (açık madde) |
| SVC-1 | Düşük | scripts/rawaccel.service | systemd birim eksiklikleri (hk.). |
| PKG-1 | Düşük | packaging/.SRCINFO | Sürüm/optdepends eşleşmesi — FIX_LOG açık listesinde; son grep teyidi henüz tamamlanmadı. |
| CI-1 | Düşük | .github/workflows/ci.yml | (açık madde) |
| CI-2 | Düşük | .github/workflows/ci.yml | warning-olarak-hata kapısı `grep`e bağlı (kırılgan). |
| SH-1..4 de dahil | Düşük | setup.sh | (açık maddeler, FIX_LOG listesinde) |
| DOC-1 | Düşük | AGENTS.md | Doküman ile gerçek davranış uyumu (bkz. YENİ-3). |
| R4 L-3 | Düşük | scripts/build.sh (FORTIFY probe) | Probe env CFLAGS'a bağlı — YENİ-1 ile birleşti. |

## 2) FIXED teyidi (çalışma ağacında zaten düzeltilmiş)

- HP-1, HP-3 — daemon.cpp (opened_device_ids bekçisi), SYN-1, FCNTL-1 (O_NONBLOCK / EVIOCGRAB)
- PID-1/2/3 (comm_state tri-state), SAVE-1, SIG-1 — daemon/main.cpp
- GUI-K1, GUI-O2, GUI-D1 — gui/ui_builder.inl; GUI-O3, GUI-D2 — gui/tr.inl;
  GUI-Y1 (inotify alignas) — gui/devices.inl; GUI-Y2 (kwin joinable thread) — gui/kwin_focus.inl;
  GUI-D3/D4 — gui/profile_mgr.inl
- PRE-1 (cap_y 1.5→1.8) — tests/oracle/oracle_cases.hpp
- MATH-2 (scale≤0→0.01) — src/config.cpp:408
- ALG-1 / CUR-1 (classic GAIN sabiti) — include/accel-classic.hpp
- B3 — src/logitech_hidpp.cpp:1767-1790 (report-rate fallback legacy)
- BS-1 — setup.sh:220-221 (clean_old_install modprobe.conf temizliği)

## 3) YENİ bulgular (Bug Hata Raporları.md'de YOK — bu denetim)

- **YENİ-1 (Düşük):** `scripts/build.sh:75` — `set -u` altında `$CFLAGS`/`$CXXFLAGS`
  bağlı değilse her build'de `satır 75: CFLAGS: bağlanmamış değişken` çıkar ve
  FORTIFY probe pipeline'ı genişletme sırasında ölür (grep boş stdin → FORTIFY
  hep `-D_FORTIFY_SOURCE=2` kalır; makepkg CXXFLAGS'ındaki 3 zorlanamaz).
  Build yine exit 0 + 0 uyarı veriyor; düzeltme: `${CFLAGS:-}` / `${CXXFLAGS:-}`.
- **YENİ-2 (Düşük):** `cli/main.cpp:351-352` — "disabled: true (stored flag —
  **dormant: daemon hot path ignores it**...)" çıktısı artık YANLIŞ. Daemon
  delete/disable'ı onurlandırıyor (daemon.cpp:857-865, PAS-2: cihazı hiç yakalamıyor,
  "safe mode"). CLI kullanıcıyı "gerçek 1:1 = raw true" diye yanlış yönlendiriyor.
- **YENİ-3 (süreç):** FIX_LOG.md notu "GUI-D4 + PRE-1 + MATH-2 henüz yeniden
  derlenmedi" → 2026-09-12 build'i (warning:0, error:0) ile geçersiz; not güncellenmeli.

## 4) Doğrulama sonucu özet

- İncelenen madde sayısı: ~90 (TUR 1–25); dosya+satır ile teyit edilen: ~55;
  FIXED teyit: 18; KARAR (bilinçli): GUI-O1, BS-2; YENİ bulgu: 3.
- Build: başarılı, 0 uyarı, 0 hata.