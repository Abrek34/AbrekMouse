# Düzeltme Koordinasyon Logu (Raporun TAM envanteri)

Kaynak: `Bug Hata Raporları.md` TUR 20-24 (satır 1023-1390). Amaç: her loglanan maddenin durumunu tek yerde tutmak; aynı dosyada aynı anda çalışmayın.

Son güncelleme: 2026-09-13

---

## 🖊️ BU OTURUM (big-pickle) — üstlenilen maddeler ve dokunulacak dosyalar

`[ALINDI: big-pickle]` işaretli maddeleri bu oturum yapıyor:

| Dosya | İçinde yapılan |
|---|---|
| `daemon/daemon.cpp` | HP-2 (replug EBUSY retry), CFG-4 (persist-önce-ack) — ❌ İPTAL (KARAR — kod değişmedi, daemon.cpp'ye dokunulmadı) |
| `gui/hidpp_panel.inl` | GUI-Y3 (pending seçim stash) |
| `gui/widgets_sync.inl` | GUI-O4 (pkexec child watch id + destroy removal) |
| `include/presets.hpp` | PRE-2 (precision cap), PRE-3 (apex output_offset) |
| `tests/oracle/oracle_cases.hpp` | PRE-2/PRE-3 oracle mirror |
| `cli/main.cpp` | CFG-2 (validate ham JSON), CFG-3 (MAX_PROFILES), CLI-2 (output_dpi), CLI-3 (mesaj), MATH-2/P107 (scale domain 0.01) |
| `src/config.cpp` | CFG-3 (MAX_PROFILES gate), CFG-5 (aşağı damgalama koruma), MATH-2 (scale floor — önceki seans) |
| `src/logitech_hidpp.cpp` | B6 (BATTERY_CHARGE default dal) — aj1 devralındı |
| `scripts/build.sh` | R4 L-3 (FORTIFY probe sırası) — aj1 devralındı; YENİ-1 (CFLAGS unbound) |
| `scripts/bench_hotpath.sh` | SH-1 |
| `scripts/rawaccel.service` | SVC-1 |
| `setup.sh` | SH-3 (kullanıcı unit uyarısı), SH-4 (udevadm selector), BS-7 (container fallback) — aj1 devralındı |
| `packaging/.SRCINFO` | PKG-1 |
| `.github/workflows/ci.yml` | CI-1, CI-2 |
| `AGENTS.md` | DOC-1 |
| `tests/test_accel.cpp` | MATH-2 P107 güncellemesi (scale 0→0.01) |
| `src/logitech_hidpp.cpp`, `gui/hidpp_panel.inl` | **P-HIDPP-1 (big-pickle, gerçek donanım doğrulandı):** G502 HERO SE'de HID++ ayarları düzeltildi — `ROOT.GetFeature`/`FEATURE_SET.GetCount`/`resolve_feature_index` `send_short` yerine `send_feature_request` (cihaz kısa isteğe uzun `0x11` cevap veriyor); `0x2201` SetDPI fn `0x01`(list okuyucu)→`0x03`(OpenLogi `set_sensor_dpi`) düzeltildi; `gui/hidpp_panel.inl` lambda kapanış derleme hatası onarıldı. Canlı: 2400→400→2400 DPI, 1000→500→125→1000 Hz yaz-oku teyitli. CHANGELOG 1.2.0'ya işlendi |

Önceki oturumun GUI-D4 derleme hatası (duplicate `GError* err`) bu oturumda düzeltildi; build + birim testleri (33792/33792) yeşil. Bu oturumda tamamlananlar: PRE-2, PRE-3 (+oracle mirror, known_deviations 68→67), CFG-2, CFG-3, CFG-5, CLI-2, CLI-3, GUI-O4, GUI-Y3, DOC-1 + önceki seansın MATH-2'siyle P107 tutarlılığı (CLI `scale` domain 0.01 floor, test_accel güncellendi). MATH-1 → KASITLI; CFG-1 → ertelendi (gerekçe yukarıda). Guards: build(warning:0) + unit(33792/33792) + oracle(67 deviation, OK) koşuldu.

**09-12 round-3 (big-pickle — taze denetim):** agent'lar (engine/daemon/cli/gui) + ASan + perf koşuldu.
ÖNEMLİ: ağaç `run_tests.sh`'de **7 FAIL** ile geldi (üstlenilenler işaretlenmemişti). Neden: BUG-HIGH-2 kaynağı sıfırlıyor ama 3 test çoktan geçersiz "unchanged" kontratını assert'liyordu. Şunlar DÜZELTİLDİ:
- `tests/test_accel.cpp` — BUG-HIGH-2 kontratına güncellendi (invalid-time → giriş sıfırlanır); 7 kansız assert.
- `cli/main.cpp` CLI-4 (Audit-A1): `-c/--config` sonraki OPTION token'ını path yutuyordu (O31-H3 parity — daemon'da var, CLI'de yoktu). `-c --json` / `-c --no-daemon` artık red.
- `cli/main.cpp` CLI-5 (Audit-C1): cap_x/input_offset guard'ları yalnız X ekseni; yazım her iki eksene → unlinked-Y sessiz mutasyon (P107). İki eksen de kontrol ediliyor.
- `cli/main.cpp` CLI-6 (Audit-D1): import `active_profile`'i doğrulamadan geri yüklüyordu → kayıttan sonra "Active profile not found"; ilk profile'e düş + uyarı.
- `cli/main.cpp` Audit-B: help domain metinleri kodla hizalandı (acceleration decel-notu, input_offset 0–500 ≤ cap_x, scale 0.01–100, cap_x ≥ input_offset, output_dpi {0}∪[1,32000], cap_mode/distance_mode alias, lp_norm→max).
- `gui/graph.inl` Audit-G2: on_graph_motion px clamp (BUG-NEW-52 X-aynası).
- `gui/daemon_comm.inl` + `gui/profile_mgr.inl` Audit-G1: update_daemon_status/export/import-done'a `window_destroyed` kapısı (izlenmeyen timer + async dialog UAF riski).
- `include/accel-power.hpp` Audit-POW-1: `constant` Inf guard (hardening, davranış değişmez, oracle aynı).

SKIP/KARAR: Audit-D2 (import clamp uyarıları) `validate` kapsamında — davranış değişikliği yapılmadı. D26-N2/N3 deliberate (bounded-stutter). GUI Finding-3 INFO. → Bu round'un koduna dokunuyor: `tests/test_accel.cpp`, `cli/main.cpp`, `gui/graph.inl`, `gui/daemon_comm.inl`, `gui/profile_mgr.inl`, `include/accel-power.hpp`.

---

## 🖊️ BU OTURUM (opencode) — üstlenilen maddeler (TUR 31 bulguları)

`[ALINDI: opencode]` işaretli bu maddeleri opencode yapıyor. Sabitlenmiş boş dosyalara girişilmedi (kural 2):
big-pickle oturumunun kilidindeki dosyalar için eşzamanlı edit YAPILMADI, madde talep edilip kilit açılınca uygulanacak.

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| O31-G1 (GUI A2) | gui/ui_builder.inl:1474-1509,960-1004 | kde_fix idle destroy cancel-gate yok (widget-lifetime UAF) | `✅ opencode — window_destroyed flag + kde_fix_finish bail` |
| O31-G2 (GUI A3) | gui/graph.inl:431-433,492 | rebuild_lut_list S->updating mutlak sıfırlama | `✅ opencode — prev_updating save/restore` |
| O31-G3 (GUI A6) | gui/tr.inl:688-697 + ui_builder.inl:595,606,612,923 | refresh_language canlı etiketleri eziyor | `✅ opencode — dynamic labels use gtk_label_new(tr()) not trlbl()` |
| O31-G4 (GUI A8) | gui/ui_builder.inl:230-255 | spin min > config floor (limit/sync_speed/exponent_power) | `✅ opencode — align spin mins with sanitize floors` |
| O31-G5 (GUI A9) | gui/ui_builder.inl:816-844 vs graph.inl on_lut_add_point | tıklama yolunda LUT_SPEED_SPIN_MAX clamp'ı yok | `✅ opencode — click-path clamp to 10000 like on_lut_add_point` |
| O31-G6 (GUI A10) | gui/profile_mgr.inl:504-508,565-570 | GtkFileDialog unref'lenmiyor | `✅ opencode — g_object_unref(dlg) in both done callbacks` |
| O31-C1 (Config C1) | include/accel-classic.hpp:158-163 | classic GAIN in dalı cap.x>=input_offset clamp'sız | `✅ opencode — io-simetrik clamp; oracle OK` |
| O31-C2 (Config C2) | src/config.cpp:182-200,336-355 | lut_length > nokta sayısı → ölü LUT | `✅ opencode — length pinned to 2*(n/2), unit test added` |
| O31-C3 (Config C3) | src/config.cpp:122-123,252-253 | gain/raw_passthrough sayısal 0/1 yok sayılıyor | `✅ opencode — accept 0/1 integers, unit test added` |
| O31-C4 (Config C4) | src/config.cpp:242-246,615-616 | active_profile cap 256 vs isim 255 → hiç eşleşmez (CFG-6 ile birlikte) | `✅ no-op — CFG-6 already resolved (both capped at MAX_NAME_LEN=256)` |
| O31-C5 (Config C5) | src/config.cpp:730,779 | symlink config save'de normal dosyaya döner | `✅ opencode — fs::canonical resolves symlink before atomic save` |
| O31-H1 (HIDPP A1) | src/logitech_hidpp.cpp:1990-1998 vs :679 | write_onboard 18>>16 bayt → hep false | `✅ opencode — chunk 16→14, params fit in 16-byte HID++ limit` |
| O31-H2 (HIDPP A2) | include/logitech_quirks.hpp:86,93-106,214-222 | G522 "32" ölü kod | `📌 KARAR — gerçek 12-char id doğrulanmadan yazılmaz` |
| O31-H3 (HIDPP B1) | daemon/main.cpp:293-298 | `-c -v` sonraki bayrağı yutar | `✅ opencode — reject arg starting with '-' for -c and -f` |
| O31-H4 (HIDPP C4) | scripts/bench_hotpath.sh:58 | geçersiz perf olayı syscalls | `✅ opencode — syscalls→syscalls:sys_enter (tracepoint)` |
| O31-H5 (HIDPP C3) | scripts/kde-fix-accel.sh:322-325 | --remove symlink yıkıyor + sabit tmp | `✅ opencode — mkstemp+realpath+fsync (--fix deseni)` |
| O31-L1 (CLI F5) | cli/main.cpp:1242-1248 | import 515-lik tek LUT sessiz eleman düşürüyor | `✅ opencode — reject odd n%2!=0, CLI gate added` |
| O31-L2 (CLI F4-x2) | cli/main.cpp:951-954,919 | input_offset>500 / cap_x<input_offset CLI-domain yok (P107) | `✅ opencode — upper bound + cross-constraint, CLI gate added` |
| O31-L3 (CLI F7) | cli/main.cpp:2080,575-577,243 | help/çıktı metni davranıştan sapıyor | `✅ opencode — daemon msg/receivers+hidpp/none+off alias` |

**Negatif teyid:** HIDPP C2 (kde-fix-accel.sh ilk-çalıştırma abort) DENEYSEL ÜRETİLEMEDİ — `cp -a` backup'ı `ls` glob'undan önce yaratır; exit-2 senaryosu imkânsız. Üstlenilmedi.

**Çakışma uyarısı:** `tests/run_tests.sh` P83 "256-char create-preset" kapısı şu an FAIL veriyor (CLI "(max 255)" ile reddediyor). Bu, devam eden CFG-6 (ad-cap) işinin ağaç durumu — opencode bu turun koduna dokunmadı (accel-classic.hpp/graph.inl/kde-fix-accel.sh). CFG-6'yı big-pickle tamamlayınca kapı yeşile döner; düzeltme opencode'a DEĞİL big-pickle'a ait.

**Kapananlar (bu tur):** O31-G1/G2/G3/G4/G5/G6, O31-C1/C2/C3/C4/C5, O31-H1/H3/H4/H5, O31-L1/L2/L3 → build(warning:0) + unit(33805/33805) + ASan/UBSan(clean) + tr_coverage(PASS).
**Round-3 (analiz turu):** N-RAW1/RAW2/RAW3, N-HID1, N-SH1, N-GUI1/GUI2/GUI3 → build(warning:0) + unit(33805/33805) + ASan/UBSan(clean) + tr_coverage(PASS) + CLI gate'leri EXIT=0.
**Round-4 (analiz turu):** N-SWID2, N-FDERR, N-EAGAIN, N-SAVEQ, N-PKEXEC, N-PANCAP → build(warning:0) + unit(33805/33805) + ASan/UBSan(clean) + tr_coverage(PASS) + CLI gate'leri EXIT=0.
**Round-5 (analiz turu):** N-DROPLOG, N-HPSCAN, N-RATEOK, N-FIRSTRUN, N-IDSERIAL, N-HLMAX, N-CMAKEUDEV, N-PKGARCH → build(warning:0) + unit(33805/33805) + ASan/UBSan(clean) + tr_coverage(PASS) + CLI gate'leri (P83/P99/P107/O31-L1/L2) EXIT=0. Detaylar aşağıda.
**Round-6 (analiz turu):** N-LUTSW, N-LCALE, N-BATTORD, N-NOTIF, N-DPIFB, N-DPICAP, N-UNISVC, N-LODSB → build(warning:0) + unit(33816/33816) + ASan/UBSan(clean) + tr_coverage(PASS) + CLI gate'leri EXIT=0. Detaylar aşağıda.

**09-12 checkpoint (big-pickle round-2):** `setup.sh` **şu an eşzamanlı düzenlemede (aj1 BS-7 guard yazdı; big-pickle SH-3:357-364 ve SH-4:330 ekledi — çakışma yok!).** Kod değişiklikleri (build/derleme bekliyor): `scripts/build.sh` YENİ-1 (`${CFLAGS:-}`/`${CXXFLAGS:-}`) + NEW-4 (GUI `$HARDENING`); `src/config.cpp`+`include/rawaccel-base.hpp` CFG-6 (**256'ya birleştirildi**: char[MAX+1] + strncpy 256 + dp.name cap 256 — CLI P83 kapısı korundu), `src/config.cpp` CFG-7 (XDG + ERANGE); `cli/main.cpp` CLI-1 (match_app notu). Yeniden doğrulama: YENİ-2, YENİ-3, PKG-1, CI-1, SH-1, SVC-1, B3, B6 zaten ağaçta ✅; HP-2 ve CFG-4 → KARAR (kod değişmedi, daemon.cpp'ye dokunulmadı). CFG-1 re-değerlendirildi → ERTELENDİ (profil JSON'unda version yok; global migrate double-scale riski geçerli). `daemon/daemon.cpp` satırındaki HP-2/CFG-4 üstlenmesi bu satırla iptal: **daemon.cpp başka düzenleme YOK.**

**09-12 checkpoint (opencode analiz turu round-3):** O31 turu kapandı, yeni araştırma/analiz turu başlatıldı. Tespit edilen ve DÜZELTİLEN maddeler (aşağıda "opencode round-3" tabloları); build(warning:0) + unit(33805/33805) + ASan/UBSan(clean) + tr_coverage(PASS) + CLI gate'leri tamam. **Eşzamanlı edit YAPMAYIN (bu turda açık olduğum dosyalar):** `daemon/daemon.cpp`, `daemon/daemon.hpp`, `include/logitech_hidpp.hpp`, `gui/ui_builder.inl`, `gui/daemon_comm.inl`, `setup.sh`.

---

## ✅ BENİM tarafımdan DÜZELTİLDİ (bu oturum) — bu dosyalara eşzamanlı edit YOK

| Yer | İçinde yapılan |
|---|---|
| `daemon/daemon.cpp` | HP-1, HP-3, SYN-1, FCNTL-1 |
| `daemon/main.cpp` | PID-1, PID-2, PID-3, SAVE-1, SIG-1 |
| `daemon/daemon.hpp` | `opened_device_ids_` (HP-3) |
| `gui/ui_builder.inl` | GUI-K1, GUI-O2, GUI-D1 |
| `gui/tr.inl` | GUI-O3, GUI-D2 |
| `gui/kwin_focus.inl` | GUI-Y2 |
| `gui/devices.inl` | GUI-Y1 |
| `gui/profile_mgr.inl` | GUI-D3, GUI-D4 |
| `tests/oracle/oracle_cases.hpp` | PRE-1 (cap_y 1.5→1.8) |
| `src/config.cpp` | MATH-2 (`scale<=0 → 0.01`) |

Not: GUI-D4 + PRE-1 + MATH-2 son edit'leri 2026-09-12 build'inde derlendi (warning:0, error:0) — build OK. (YENİ-3)

---

## ⬜ AÇIK MADDELER — tam liste (kimse başlamadı)

| ID | Öncelik | Dosya(lar) | Konu | Durum |
|---|---|---|---|---|
| HP-2 | ORTA | daemon/daemon.cpp (~1129-1210) | Replug by-id yolu değişince EBUSY → graspsız cihaz; removal sonrası yeniden denenmiyor | `✅ KARAR (09-12): deny_reopen + prune ağaçta (P121/P131); EBUSY-grab cihazlar deny'ye GİRMİYOR → her scan'de yeniden denenir. Kod değişmedi` |
| GUI-Y3 | ORTA | gui/hidpp_panel.inl:577-593 | HID++ busy-guard bekleyen seçimi sessizce düşürüyor → bayat DPI | `✅ big-pickle` |
| GUI-O1 | ORTA | gui/graph.inl:551 | LUT rebuild emission içinde — **değerlendirildi: GTK emit ref koruyor, KASITLI öneriliyor** | ⬜ (karar) |
| GUI-O4 | ORTA | gui/widgets_sync.inl:~448 | `g_child_watch_add` id iptal edilmiyor; pencere kapanışı UAF riski | `✅ big-pickle` |
| PRE-2 | ORTA | include/presets.hpp:60-72 | precision limit=1.2 ama cap.y=1.5, eğri 1.5'e zirve | `✅ big-pickle — cap={24,1.2}, oracle mirror güncel` |
| PRE-3 | DÜŞÜK-ORTA | include/presets.hpp:119-139 | apex output_offset=0.9 platosu kendi yorumuyla çelişiyor (sub-1:1) | `✅ big-pickle — output_offset=1.0, oracle+known_deviations güncel (67)` |
| MATH-1 | ORTA | include/accel-classic.hpp, src/config.cpp:398 | negatif acceleration + cap → dejenere (ıraksak/ters akış) | `✅ KASITLI (kod yok) — oracle classic_gain_negaccel referansla 1-1 eşleşiyor; test_accel.cpp:3182-3205 referans GAIN formunu assertliyor` |
| CFG-1 | ORTA | cli/main.cpp:1143-1296, src/config.cpp | import `migrate_config()` baypas ediyor → pre-0.4 lookup+gain yanlış | `⏸ ERTELENDİ (big-pickle): profil JSON'unda version işareti YOK → global migrate, güncel lookup+gain export'larını iki kez ölçekler. `profile_to_json`'a version eklenmeden güvenli fix yok.` |
| CFG-2 | ORTA | cli/main.cpp:586-709 | validate uyarıları ölü kod (sanitize çoktan clamp etmiş) → "All checks OK" yanıltıcı | `✅ big-pickle — ham JSON cross-check + clamp uyarıları` |
| CFG-3 | ORTA | cli/main.cpp:472,548,579,1291 | MAX_PROFILES sadece yüklemede; create/dup/import sınırsız → sessiz kesinti | `✅ big-pickle — create/duplicate/create-preset/import gate + smoke` |
| CFG-4 | ORTA | daemon/daemon.cpp:588-660, cli/main.cpp:197-212 | ack-before-persist + timeout→SIGHUP sahte rc=0 | `✅ KARAR (09-12): push_config save'i save_thread_'e kuyrukluyor (R1-11); ack='kuyruğa alındı'. IPC thread'ini yazma bitene kadar bloklamak R1-11 stall'ını geri getirir → tasarım gereği değişmedi` |
| CFG-5 | DÜŞÜK-ORTA | src/config.cpp:601-643 | gelecek version config ilk kayıtta aşağı damgalanıyor, bilinmeyen key düşüyor | `✅ big-pickle — save daha yeni version'ı koruyor; bilinmeyen key round-trip'i hâlâ açık` |
| CFG-6 | DÜŞÜK | src/config.cpp:242 | profil adı 256 vs iç içe 255 çelişkisi | `✅ big-pickle (09-12): yük tarafı 256'ya birleştirildi (char[256+1] + strncpy 256 + dp.name cap 256); CLI 256 kabul (P83 kapısı yeşil: 256 OK, 257 red)` |
| CFG-7 | DÜŞÜK | src/config.cpp:824-842 | XDG_CONFIG_HOME yok sayılıyor; getpwnam ERANGE retry yok | `✅ big-pickle (09-12): XDG_CONFIG_HOME önceliği + ERANGE büyüyen buffer; build + test yeşil` |
| CLI-1 | DÜŞÜK | cli/main.cpp:1378-1548 | status yerel config + canlı daemon verisini karıştırıyor; match_app yok sayılıyor | `✅ big-pickle (09-12): status'ta app-scoped notu (match_app); build + test yeşil` |
| CLI-2 | DÜŞÜK | cli/main.cpp:914-919, src/config.cpp:491 | set-param output_dpi 0.5 → sessizce 1, rc=0 (P107 ihlali) | `✅ big-pickle — domain {0}∪[1,32000] + smoke` |
| CLI-3 | DÜŞÜK | cli/main.cpp:495-501, 2344-2357 | "sonraki çalıştırmada default yeniden oluşur" mesajı yanlış | `✅ big-pickle — boş config yüklemesinde default yeniden oluşturuluyor + smoke` |
| PKG-1 | ORTA | packaging/.SRCINFO | bayat 0.6.4 (kod 1.1.0) → makepkg --printsrcinfo ile üret | `✅ ağaçta teyit (pkgver 1.1.0)` |
| CI-1 | ORTA | .github/workflows/ci.yml:31-43 | build adımında `set -e` yok → sessiz build hatası geçebilir | `✅ ağaçta teyit (set -e eklendi)` |
| SH-1 | DÜŞÜK | scripts/bench_hotpath.sh:42 | non-x86 `/proc/cpuinfo` 'model name' yokluğunda abort | `✅ ağaçta teyit (model name|Hardware|CPU implementer + || true)` |
| SH-2 | DÜŞÜK | setup.sh:296-315 | TOCTOU symlink penceresi (bilgilendirici) | `KARAR` |
| SH-3 | DÜŞÜK | setup.sh (install) | kullanıcı systemd unit'i sistem unit'ini gölgeler, uyarı yok | `✅ big-pickle — install_system'da uyarı eklendi (09-12) → bak setup.sh:357-364` |
| SVC-1 | INFO | scripts/rawaccel.service:22-23 | StartLimitIntervalSec/Burst açıkça yazılmamış | `✅ ağaçta teyit (60s/10 eklendi)` |
| SH-4 | INFO | setup.sh:324 | udevadm trigger selectorsuz tüm sistemi tetikliyor | `✅ big-pickle — input-scope trigger eklendi (09-12) → setup.sh:330` |
| DOC-1 | DÜŞÜK | AGENTS.md | polkit kaldırıldı ama doküman hâlâ "kurulan entegrasyon" diyor | `✅ big-pickle` |
| CI-2 | INFO | .github/workflows/ci.yml:86-90 | sanitizer job -Wpedantic yok (belge) | `[ALINDI: big-pickle]` |
| R4 L-3 | ORTA | scripts/build.sh:61-84 | BASE_CXXFLAGS tanımı FORTIFY probe'undan SONRA → probe gerçek -O3/MARCH ile çalışmıyor | `[ALINDI: aj1 → big-pickle (devralındı)]` |
| B3 | DÜŞÜK | src/logitech_hidpp.cpp:1767-1790 | extended 0x8061 geçersiz kod → nullopt, legacy 0x8060'a inmiyor | `✅ (ağaçta teyit edildi — aj1 kapsamı)` |
| B6 | DÜŞÜK | src/logitech_hidpp.cpp:210-223 | BATTERY_CHARGE default dal `online=false` — başarılı yanıt zaten online demek | `[ALINDI: aj1 → big-pickle (devralındı)]` |
| BS-7 | ORTA | setup.sh:323-324 | `udevadm control/trigger` container'da fail → set -e ile tüm kurulum abort | `✅ aj1+big-pickle (09-12): 2>/dev/null || warn guard + BS-7 yorumu setup.sh:323-331` |
| YENİ-1 | DÜŞÜK | scripts/build.sh:75 | `set -u` altında `$CFLAGS`/`$CXXFLAGS` unbound → her build'de hata + FORTIFY probe ölü | `✅ big-pickle (09-12): ${CFLAGS:-}/${CXXFLAGS:-}; CFLAGS/CXXFLAGS set edilmeden build OK (0 uyarı)` |
| YENİ-2 | DÜŞÜK | cli/main.cpp:351-352 | "disabled: dormant, hot path ignores" mesajı YANLIŞ — daemon disable'ı onurlandırıyor (PAS-2) | `✅ ağaçta teyit (C29-N6 mesajı var, dormant yok) — kod değişmedi` |
| YENİ-3 | SÜREÇ | FIX_LOG.md | "GUI-D4+PRE-1+MATH-2 derlenmedi" notu build ile geçersiz — güncellenmeli | `✅ güncellendi (satır 51 notu) — kod değişmedi` |
| NEW-4 | DÜŞÜK | scripts/build.sh:124 | GUI derleme satırı `$HARDENING` içermiyor (daemon:104, cli:114 var) → GUI `-fstack-protector`/`FORTIFY` SIZ | `✅ big-pickle (09-12): GUI satırına $HARDENING eklendi; build + test (33792/33792) yeşil` |

---

## 📌 KARAR VERİLDİ (kod değişmedi, rapora gerekçe yazılacak)

| ID | Gerekçe |
|---|---|
| SUB-1 | remainder `modify()` sonrası eklenme — motion_math.hpp yorumu + subpixel drift testleri bilinçli tasarımı doğruluyor |
| GUI-O1 | `rebuild_lut_list` emission içi — GTK `g_signal_emit` emit örneğine ref tutar (UAF yok); davranış değişikliği yeni risk getirir |
| MATH-1 | negatif `acceleration` + cap davranışı REFERANS-PARITY'dir: oracle `classic_gain_negaccel` (`oracle_cases.hpp:70`) known_deviations'da hiç listelenmemiş; `tests/test_accel.cpp:3182-3205` aynı parametrelerle referans formunu (c(10)=2.125) doğrular. Koruma `gain_inverse` içinde 0 rakarımıyla zaten var; davranış değişikliği referansla çakışır → rapora KASITLI kaydı yazıldı, kod dokunulmadı. |
| CFG-1 | import'ta migrate: profil JSON'unda `version` yok; global `migrate_lookup_gain`, 0.4.0+ export'larını yanlış ölçekler (çift). Önce `profile_to_json`'a version damgası + import'ta ona göre karar; bugünkü kodu değiştirmek geçerli export'larda veri kaybı riski taşır → ertelendi. |

---

## 📋 Kurallar
## ✅ BU TUR TAMAMLANANLAR (aj1) — 2026-09-12, build + unit(33792/33792) + bash -n yeşil

| ID | Dosya | Yapılan |
|---|---|---|
| R4 L-3 | scripts/build.sh:61-84 | `BASE_CXXFLAGS` tanımı FORTIFY probe'undan ÖNCEYE alındı (probe artık gerçek -O3/$MARCH ile çalışıyor; L-3 yorumu güncellendi) |
| B3 | src/logitech_hidpp.cpp:1767-1790 | 0x8061 extended rate kod çözülemezse / nullopt ise → 0x8060 legacy'ye düşüyor (fallback tam) |
| B6 | src/logitech_hidpp.cpp:210-223 | `parse_battery_charge` unknown status nibble'da `online=false` yapmıyor (başarılı yanıt zaten online demek) |
| BS-7 | setup.sh:321-334 | `udevadm control --reload-rules` + `udevadm trigger` `2>/dev/null || warn` koruması (container/CI'da kurulumu abort etmiyor); udev trigger input-scope'a daraltıldı (SH-4) |

Bu 4 madde artık DÜZELTİLDİ sayılır — bu dosyalara eşzamanlı edit YAPMAYIN: `setup.sh`, `scripts/build.sh`, `src/logitech_hidpp.cpp`.

---

## 📋 Kurallar
1. Açık maddeye başlamadan: bu satırı `[ALINDI: <isim>]` yapın.
2. Aynı dosyada eşzamanlı edit yapmayın; biri bitirmeden diğeri aynı dosyaya girmesin.
3. Ben bu oturumda kendi dosyalarımı derleyip kapıları koşacağım; sonra commit için bekleyeceğim.

---

## ✅ BU TUR TAMAMLANANLAR (opencode round-4) — 2026-09-12, build(warning:0) + unit(33805/33805) + ASan/UBSan(clean) + tr_coverage(PASS) + CLI gate'leri EXIT=0

Daemon/HID++/GUI üç kollu derin analiz turu (bir önceki round-3 maddeleri dışındaki alanlar): HID++ transport, pkexec watch, save queue, graph pan.

| ID | Dosya | Yapılan |
|---|---|---|
| N-SWID2 | src/logitech_hidpp.cpp:631-635 | `next_sw_id()` 15→0 wrap sonrası 0'ı atlıyor (0 = notification rezervi). Önceki round `init=1`'i çözmüş ama 16'da bir yine 0 üretiliyordu → her 16. HID++ isteği notification ile çakışabilirdi. Artık 1..15 döngüsü. |
| N-FDERR | src/logitech_hidpp.cpp:651-677 | `read_packet` POLLERR/HUP/NVAL + hard read hatası + EOF'da fd'yi kapatıp `fd_=-1` yapıyor. Öncesinde ölü fd path'te kaldıkça drain döngüsü (daemon.cpp:1416) transportı asla yeniden kurmuyordu → batarya/bildirim karartması restart'a kadar. Şimdi self-heal (drain yeniden kurar + clear_feature_cache). |
| N-EAGAIN | src/logitech_hidpp.cpp:637-649 | `write_packet` O_NONBLOCK fd'de geçici EAGAIN'i fatal sayıp isteği düşürüyordu → poll(POLLOUT, 100ms) + retry. |
| N-SAVEQ | daemon/daemon.cpp:613-624 | `save_q_` sınırsız deque idi (hızlı client fsync hızından hızlı push edince bellek şişerdi). Her giriş tam config snapshot olduğundan ve daemon yalnız EN SON configi uyguladığından backlog coalesce edilir (bekleyenler silinir; save'deki front asla dokunulmaz). |
| N-PKEXEC | gui/widgets_sync.inl + app_state.hpp:96-100 + ui_builder.inl:destroy | pkexec child-watch'ta ctx heap leak + abandon edilen çocuk zombie: `pkexec_watch_ctx`/`pkexec_watch_pid` AppState'te izleniyor; `pkexec_watch_detach(S, kill)` ile replace'te (kill=true, çift tıklamada yeni istek kazanır) ve destroy'da (kill=false, devam eden daemon start'ı yarıda kesilmez) ctx serbest + GLib one-shot reaper ile zombie yok. `pkexec_child_report` app-state ileri işaretçilerini temizler (çift-free yok). |
| N-PANCAP | gui/graph.inl:298-299 | `graph_pan_x` sadece alt sınır klamplıydı (sağ sürüklemeler max_speed'i sonsuza büyütürdü) → üst sınır 500 ips. |

Değerlendirilip DEĞİŞMEYENLER: `use_raw_input/whole/disable` sayısal 0/1 — P54-B4 tasarımı gereği katı `is_boolean` (yanlış tip default'a düşer); O31-C3 gain/raw_passthrough'a özeldir. `migrate_lookup_gain` taşması zaten satırda BUG-MED-1 isfinite guard ile kapalı (config.cpp:950-956). kde-fix-accel.sh `get_current_profile` global-only okuması yalnız display/erken-çıkış için; per-device override'lar write_kwinrc + --check (BUG-NEW-84) tarafından yazılıp doğrulanıyor → sorun değil.

**Eşzamanlı edit YAPMAYIN (round-4 açık dosyalarım):** `src/logitech_hidpp.cpp`, `gui/widgets_sync.inl`, `gui/ui_builder.inl`, `gui/app_state.hpp`, `gui/graph.inl`, `daemon/daemon.cpp`.

## ✅ BU TUR TAMAMLANANLAR (opencode round-3) — 2026-09-12, build(warning:0) + unit(33805/33805) + ASan/UBSan(clean) + tr_coverage(PASS) + CLI gate'leri EXIT=0

Yeni analiz turu: daemon + GUI + CLI/config/shell üç kollu tarama. Uygulanan düzeltmeler:

| ID | Dosya | Yapılan |
|---|---|---|
| N-RAW1 | daemon/daemon.hpp:316, daemon/daemon.cpp:490,835,1200,1096-1106 | `raw_input_enabled_` plain bool → `std::atomic<bool>`; `apply_new_config` artık config'teki `use_raw_input`'u takip ediyor (değişimde teardown → flag güncelle → setup). Öncesinde reload/push ile toggle sessizce yok sayılıyordu (cihazlar "raw" config'e rağmen grab'de kalıyordu). |
| N-RAW2 | daemon/daemon.cpp do_hotplug_scan | Yalnız `opened_paths_` kontrolü vardı; `opened_device_ids_` kontrolü eklenerek aynı fiziksel cihazın ikinci eventN düğümü (HID-composite) çift grab=kopya rapor çıktısı engellendi (setup_devices ile aynı guard). |
| N-RAW3 | daemon/daemon.cpp | Raw-passthrough/overflow tek-evt yazma yolları `libevdev_uinput_write_event` doğrudan çağırıyordu; O_NONBLOCK fd'de kompozitör takılması → -EAGAIN → sanal cihaz kopuyordu. Yeni `uinput_write_retry_ev()` (batched `uinput_write_retry`'nin tek-evt sürümü) ile 5 çağrı yeri (SYN-subtype 2072, raw REL 2121, taşma yolları 2141/2154, sentetik SYN 2184) aynı sınırlı backoff'unu kullanıyor; ölü `uinput_write` kaldırıldı. |
| N-HID1 | include/logitech_hidpp.hpp:434 | `next_sw_id_ = 0` → `1`. sw_id 0 cihazın notification-byte'ı (her notification sw_id 0), komut yanıtıyla zaten çakışıyordu. |
| N-SH1 | setup.sh:358 | `systemctl daemon-reload` `set -eo pipefail` altında systemd-less ortamda kurulumu abort ediyordu → `2>/dev/null || warn` guard (BS-7 deseni). |
| N-GUI1 | gui/ui_builder.inl:1289 | `kde_atomic_write`: `fflush||ferror||fsync||fclose` kısa devresi fflush/fsync hatasında `fclose`'u atlıyordu (FILE*/fd leak) → her durumda önce fclose. |
| N-GUI2 | gui/ui_builder.inl:882-891 | Sağ-tık LUT hit-test `px`/`py` draw'ın (graph.inl:244-245) clamp'larını kullanmıyordu → max_speed/max_gain üstü noktanın görsel konumu tıklanamıyordu. Draw ile birebir clamp (px plot kutusu, gain [0,1]). |
| N-GUI3 | gui/daemon_comm.inl:148-149 | `setsockopt` return'leri yok sayılıyordu → başarısız zaman-aşımı ayarı sonsuz blok riski. Başarısızsa fd kapatılıp sonraki aday deneniyor. |

İncelenip DEĞİŞMEYENLER (gerekçeyle):
- widgets_sync.inl tek-seferlik `g_timeout_add` (634/693/705/718/744): `update_daemon_status` başında `window_destroyed` guard'ı var (daemon_comm.inl:491) → UAF yok.
- GtkCssProvider unref: GTK4 stil bağlamı provider'ı ref'ler, widget destroy'da serbest bırakır → gerçek leak değil.
- apply_profile telemetri sıfırlama seqlock'u (BUG-LOW): samples en sonda güncellenir, okuyucu eşleşmeyen çifti retry ile düşürür — kabul edilebilir, retry eksikken önemli.

Bu 9 madde artık DÜZELTİLDİ sayılır — bu dosyalara eşzamanlı edit YAPMAYIN: `daemon/daemon.cpp`, `daemon/daemon.hpp`, `include/logitech_hidpp.hpp`, `gui/ui_builder.inl`, `gui/daemon_comm.inl`, `setup.sh`.
4. Bu dosyayı **commit'e dahil etmeyin**; iş bitince silinecek.

---

## ✅ BU TUR TAMAMLANANLAR (big-pickle round-3 taze denetim) — 2026-09-12

Kapılar: build(warning:0) + unit(**33805/33805**; P83/P99/P107/O31-L1/O31-L2 CLI kapıları dahil) + ASan/UBSan(**clean**) + oracle(**OK**, 1071/67) + tr_coverage(**PASS**). TUR 32 kaydı `Bug Hata Raporları.md` sonunda.

| ID | Dosya | Yapılan |
|---|---|---|
| TEST-1 | tests/test_accel.cpp | 7 FAIL kaynağı: BUG-HIGH-2 kontratı (geçersiz zaman → in.x/in.y SIFIRLANIR, rawaccel.hpp:321-325) test'lerde hâlâ "değişmeden dön" assert ediyordu. `test_modifier_end_to_end` (vaka 2 → {0,0}), `test_nonfinite_time_does_not_poison_smoothers` (NaN/Inf → ox==0&&oy==0), `test_modifier_zero_time` (time 0/-1 → sıfırlama) güncellendi. |
| CLI-4 | cli/main.cpp (~2405) | `-c/--config` sonraki OPTION token'ını path olarak yutuyordu (O31-H3'ün CLI aynası) → argüman '-' ile başlarsa "config path eksik" reddi. |
| CLI-5 | cli/main.cpp (~1091, ~1124) | cap_x/input_offset guard'ları yalnız X doğruluyor, iki eksene yazıyordu → Y için de aynı doğrulama. |
| CLI-6 | cli/main.cpp (cmd_import sonu) | import sonrası active_profile yoksa rc=0 + hatalı config kaydediliyordu → ilk profile'e düş + uyarı. |
| CLI-B | cli/main.cpp (domain tablosu) | help dokümanları kodla hizalandı: acceleration (cap yokken decel notu), input_offset 0-500 ≤ cap_x, scale 0.01-100, cap_x ≥ input_offset, output_dpi {0}∪[1,32000] + (0,1) red, cap_mode/distance_mode alias'ları, lp_norm→max notu. |
| GUI-5 | gui/graph.inl:337 | on_graph_motion hit-test px'i X ekseninde clamp'sızdı (BUG-NEW-52 Y aynası eksik) → [GRAPH_ML, GRAPH_ML+PW] clamp. |
| GUI-6 | gui/daemon_comm.inl:481+, gui/profile_mgr.inl | `window_destroyed` kapıları: tek-seferlik timer + async export/import callbacks pencere yıkımından sonra widget'a dokunamıyor. |
| AUDIT-POW-1 | include/accel-power.hpp:65+ | power constant DBL_MAX ile Inf olabiliyor (dormant) → `!isfinite(constant)` guard (classic mirror). Davranış değişmez, oracle OK. |
| T30-NX | tests/run_tests.sh | SEC-2 `.json` uzantı zorunluluğu (tree'ye eklenmiş) P83/P99/P107 seed'ini kırıyordu → config-path mktemp'leri `mktemp --suffix=.json`. |

Kararlar (kod DEĞİŞMEDİ): C29-N7 JSON int kesirli sessiz kesme → ERTELENDİ (JSON load belgelenmiş degrade yolu; CLI int_ok reddediyor; config.cpp iostream bağımlılığı istenmedi). D2 import clamp uyarıları → validate (CFG-2) zaten veriyor. D26-N2/N3 bounded-stutter → deliberate.

Kilitli (bu turda düzenlenenler — eşzamanlı edit YAPMAYIN): `tests/test_accel.cpp`, `cli/main.cpp`, `gui/graph.inl`, `gui/daemon_comm.inl`, `gui/profile_mgr.inl`, `include/accel-power.hpp`, `tests/run_tests.sh`.

---

## ✅ BU TUR TAMAMLANANLAR (big-pickle round-4 TAM DENETİM "her dosya her kod") — 2026-09-12

Kullanıcı komutu: her dosyayı/her kodu tek tek incele ve düzelt. 6 salt-okunan agent (engine/config, daemon, CLI, GUI, HID++, scripts/CI) çalıştırıldı; her bulgu kaynaktan doğrulandı, düzeltilenler aşağıda. Kapılar (tek sıralı koşu sonrası): build(**warning:0/error:0**, -Wpedantic dahil) + unit(**33805/33805** EXIT=0) + ASan/UBSan(**clean** EXIT=0) + oracle(**OK** 1071/67) + tr_coverage(**PASS**). TUR 33 kaydı `Bug Hata Raporları.md` sonunda.

| ID | Dosya | Yapılan |
|---|---|---|
| D1 | daemon/daemon.cpp + daemon.hpp | LIVE-DISABLE: reload/push/app-anahtarı `dev_cfg.disable`'ı canlı uygulamıyordu (cihaz replug'a dek grab+akselere ediliyordu). Yeni `release_device()` (epoll-del + map temizliği + grab bırak + uinput destroy + fd kapat) `apply_new_config` ve `apply_active_app`'te devreye alındı; fd_to_dev_ yeniden kuruluyor. |
| D2 | daemon/daemon.cpp ~2237 | `dump_latency_stats` stdout'u `log_mu_` DIŞINDA yazıyordu (R1-04 ihlali; döngü/hidpp/ipc thread'leri ile cout yarışı). Yazım bloğu `log_mu_` guard'ına alındı (sıralama: devices_mutex_ önce serbest, sonra log_mu_ — lock-order tutarlı). |
| D3 | daemon/daemon.cpp SM-4 | Idle sonrası ölçüm `dev.poll_rate` (profil nominali) kullanıyordu → 125 Hz cihaz + 1000 Hz profil = ilk flick'te gain sivri ucu. `detected_polling_rate > 0` ise fiziksel rate, değilse profile düş. |
| D4 | daemon/daemon.cpp IPC | SO_RCVTIMEO dolumunda istemci sessizce düşüyordu (D-8 ihlali) → `EAGAIN/EWOULDBLOCK` ayrı dalda `reply_timeout()` ile yanıtlanıyor (deadline yoluyla aynı). |
| D5 | daemon/daemon.cpp raw hot path | Raw modda REL_X/Y anında yazılıyor ama buton/tekerlek (SM-2) SYN'e kadar kuyrukta kalıyordu → kaynak [BTN, REL] sırası çıktıda [REL, BTN] oluyordu (bit-birebir vaadi ihlali). Raw modda tüm non-motion'lar da anında iletilir; accel modu değişmedi. |
| C1 | cli/main.cpp (daemon_apply_config) | Modern daemon "unknown command" (veya her JSON `error`) yanıtı verdiğinde SIGHUP fallback'e düşüp "Config applied to the daemon." rc=0 YALANI basıyordu → error yanıtı artık false (gerçek başarısızlık). Yalnız sessiz/eski daemon legacy SIGHUP yolu. |
| C3 | cli/main.cpp set-param | speed_min>speed_max yalnız UYARIYDI (kayıtta sessiz clamp) → cap_x/input_offset gibi P107 çapraz kısıt RED oldu (rc=1, config dokunulmaz). |
| C4 | cli/main.cpp parse | POSIX `--` ayracı yoktu → `--set` gibi token'lar seçenek sanılıyordu. `--` sonrası tüm token'lar positional. |
| C6 | cli/main.cpp status | "daemon unreachable" tanısı STDOUT'a basılıyordu → stderr (JSON/beton hattı kirletmiyor). |
| S1 | scripts/build.sh | FORTIFY probe `${CFLAGS:-} ${CXXFLAGS:-}` ile probe ediyor ama derleme siteleri (104/114/124) bu env flag'lerini kullanmıyordu → env -D_FORTIFY_SOURCE=3 probe'u susturup savunmasız binary üretiyordu. Probe == compile (env flag'leri probe'dan çıkarıldı). |
| S2 | scripts/build.sh | BASE_CXXFLAGS'a `-Wpedantic` (CMakeLists'te vardı, build.sh'te yoktu — CI warning gate'i uçuruyor). Build 0 uyarı. |
| S3 | .github/workflows/ci.yml | build-and-test + sanitizers işlerinde python3 yoktu; run_tests.sh:52 python3'süz ölüyor → iki apt listesine eklendi. |
| S4 | scripts/bench_hotpath.sh | perf bankacılık/perf_event_paranoid'de fail edince `set -euo pipefail` BÜTÜN bench'i abort ediyordu → `|| true` + açıklayıcı mesaj (zamanlama sonuçları geçerli). |
| S5/S6 | README.md | 88-89 yanlış önvarsayım ("setup.sh /usr/local/bin, paket /usr/bin") — setup.sh artık /usr/bin'e kuruyor, servis ExecStart=/usr/bin; metin düzeltildi. 108-110 manual install hâlâ /usr/local/bin'e kopyalıyordu → servis çalışmaz; /usr/bin'e çevrildi. |
| S7 | setup.sh verify_install | udev kuralı yalnız /etc/udev/rules.d kontrol ediyordu → yalnız PKGBUILD ile kurulmuş sistemler (kural /usr/lib'de) false-negative "EKSİK". İki yol da kabul + içerik denetimi. |
| S8 | tests/fuzz_config.cpp | Sabit /tmp yolundaki yazım `std::ofstream` ile symlink takip edebiliyordu (TOCTOU) → `open(O_NOFOLLOW|O_TRUNC|O_CLOEXEC)` + write/close; symlink'te o girdi atlanır. |
| S9 | tests/oracle/run_oracle_perf.sh | `^TOTAL` satırı yoksa python `float('')` traceback'i → açık hata mesajı + exit 1. |
| G2 | gui/widgets_sync.inl (unlinked) | Y unlinked + "Lookup" modunda ay lookup data/length'i hiç doldurulmuyordu (LUT editörü yalnız ax'e yazar) → boş/eski eğri. R13 deseniyle ax'in LUT'ı ay'a kopyalanıyor (memcpy+length). |

Doğrulama sonrası ZATEN DÜZELMİŞ (bu oturumda dokunulmadı): H2 sw_id wrap (N-SWID2, logitech_hidpp.cpp:631-639), E3 quirks short-key mantığı (find_logitech_quirks fallback, logitech_quirks.hpp:99-104), G3 pkexec watch overlap (pkexec_spawn:490-491 detach+kill+reap).

ERTELENDİ/DEĞİŞMEDİ (gerekçeyle — Bug Hata Raporları TUR 33'te detaylı): C2 import clamp uyarıları (validate CFG-2 kapsamı), E1 profile-import lookup+gain migrate (config dosyaları version damgalı, profil importları değil — belgeli degrade), E2 LUT wrong-type → 0 (documented), D6/D7 INFO mikro-iyileştirmeler, H1 bit-7 response mask + H3 legacy misdecode + H4 cross-process hidraw contend (donanım doğrulamasız spekülatif — SOLAAR raporu kapalı; gerçek cihaz yakalaması ister), G1 kwin focus senkron IPC (main-thread stall — GUI runtime testi bu ortamda yok).

Kilitli (eşzamanlı edit YAPMAYIN — bu dosyalar round-4'te değişti): `daemon/daemon.cpp`, `daemon/daemon.hpp`, `cli/main.cpp`, `gui/widgets_sync.inl`, `scripts/build.sh`, `.github/workflows/ci.yml`, `scripts/bench_hotpath.sh`, `README.md`, `setup.sh`, `tests/fuzz_config.cpp`, `tests/oracle/run_oracle_perf.sh`.

---

## round-5 (opencode — 12.09.2026) — agent'lar (daemon/tests/CMake + GUI davranış + çapraz-dosya) bulguları

Kıyas: 3 paralel explore agent'ı; çapraz-dosya değişmezleri (JSON status kontratı, device_id türetme, param bound'ları, orak, export/import) uyuşmazlık BULMADI.

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| N-DROPLOG (BUG-LOW) | daemon/daemon.cpp uinput_write_retry | EAGAIN bütçesi (32 deneme) tükenip SYN kuyruğu atıldığında `return true` → çift-burst sessiz; "all clear" yalanı | `✅ opencode — budget tükenince done<nbytes mesajı drop_report üzerinden (batch'te log(), raw path'i varsayılan no-op) 2s-throttled raporlanır; disconnect kontratı korundu (RAC-1 tasarımı)` |
| N-HPSCAN (BUG-LOW) | daemon/daemon.cpp:1594 | ~2s self-heal rescan yalnız `devices_.empty()` → ikinci fare, DIŞ cihaz varken transient hatayla kaçırılırsa bir daha denenmez | `✅ opencode — rescan_needed_ atomic bayrağı; open_input_device/reopen_denied/create_virtual_device/epoll_ctl(ADD) transient yollarında set, kalıcı kapılar (raw disabled/profil disabled/dup id) set ETMEZ; run_loop 2s ritmini bayrak açıkken sürdürür` |
| N-RATEOK (GUI F1) | gui/hidpp_panel.inl:283-284 | `cur.ok` yalnız DPI sorgusunda set → rate-only (DPI'sız) cihaz başarıyı "Could not query" diye raporluyor | `✅ opencode — get_polling_rate başarısında da cur.ok=true` |
| N-FIRSTRUN (GUI F2) | gui/main.cpp:160-186 | İlk çalıştırma (config yok) → fs::copy_file hata yolu → sahte "corrupt config bulunamadı/yedeklenemedi" alarmı | `✅ opencode — load_config yalnız fs::exists ise; eksik dosya normal first-run (empty-profiles guard default tetikler)` |
| N-IDSERIAL (GUI X1) | gui/devices.inl:145-147 | VID/PID'siz ama serili cihazda GUI fallback event_node; daemon ise 3-kademeli (usb:.... / seri-tek başına / event_node) → device_id eşleşmez | `✅ opencode — GUI daemon parity: seri varken m.uniq (event_node yok)` |
| N-HLMAX (GUI X2) | gui/ui_builder.inl:492-494 | HL spin'leri 0–200 ms; engine SMOOTH_HALFLIFE_MAX=10000 → hl=5000 yüklersen spin kaydederken 200'e clampi YAZIYOR (sessiz veri mutasyonu) | `✅ opencode — üç HL spin'i engine tavanı 10000'e genişletildi; tüm engine-legal değerler round-trip` |
| N-CMAKEUDEV (IMP) | CMakeLists.txt | make install 99-rawaccel.rules'ı kurmuyordu (setup.sh/PKGBUILD kuruyordu) | `✅ opencode — install(FILES scripts/99-rawaccel.rules → /usr/lib/udev/rules.d)` |
| N-PKGARCH (IMP) | packaging/PKGBUILD | arch yalnız x86_64 | `✅ opencode — +aarch64` |

SKIP/KARAR: GUI gauge'lerinin spin üst sınırları scale/exp_power/cap/input_offset/output_offset hariç engine'de üst clamp'sız; acceleration>20 gibi değerler "pratik dışı" — HL dışında sınır yükseltilmedi (envanter belgeli). lp_norm≥16→Max reclassification davranışsal eşdeğer (engine sentinel); dokunulmadı. e2e harness iyileştirmeleri (first-match exact assert'leri, RAII cleanup) deterministik 0/1/77 kontratına risk — koşulamadığı için (root+/dev/uinput) dokunulmadı. PKGBUILD check() ölü kod — zaten BUILT_TESTS=OFF, yorumda belgeli; değişmedi.

Doğrulama: BUILD CLEAN (warning:0) + unit 33805/33805 + ASan/UBSan clean (EXIT=0) + tr_coverage PASS + CLI gate'leri EXIT=0.

**Bu turda değişen dosyalar (kilit, round-6'da aynı dosyalara eşzamanlı edit YAPMAYIN):** `daemon/daemon.cpp`, `daemon/daemon.hpp`, `gui/hidpp_panel.inl`, `gui/main.cpp`, `gui/devices.inl`, `gui/ui_builder.inl`, `CMakeLists.txt`, `packaging/PKGBUILD`.

---

## round-6 (opencode — 12.09.2026) — agent'lar (engine/oracle + HID++/receiver/quirks + config/IPC/CLI/shell) bulguları

Kıyas: 3 paralel explore agent'ı. Engine/oracle agent'ı gerçek hata BULMADI (6 mod parity-clean, 67 deviation doğru, threading race-free); tek aksiyonları oracle NaN-bölgesi eşitsizliği (infra) + AGENTS.md belge sürüklenmesi. Config/IPC/CLI agent'ı: BUG-HIGH/MED yok; HID++ agent'ı 3 MED + 4 LOW buldu.

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| N-LUTSW (BUG-LOW) | src/config.cpp:91-101,189-191 | lookup profilde `cli set-param mode natural` + otomatik kayıt → lut_data JSON'dan DÜŞÜYOR (serileştirme yalnız lookup); tekrar lookup'a dönünce eğri boş (sessiz kullanıcı verisi kaybı) | `✅ opencode — yazar + okur: LUT her uzunluk>0'da serileştirilir/okunur (mod kapısı kaldırıldı; okuyucu yalnız JSON kullanıcı durumu tüketir, canlı cihaz args'ı değil — sync internal LUT riski yok; apply_profile cihaz args'ını değiştirir, config kopyasını değil). Unit test eklendi (33805→33816)' |
| N-LCALE (BUG-LOW) | gui/daemon_comm.inl:482 | GUI `setlocale(LC_ALL,"")` (i18n) → std::strtod LC_NUMERIC onurlandırıyor; `,` ondalıklı yerellerde daemon'un C-locale değeri (45.703) 45 okunuyor → telem/lat tam-sayı görünüyor | `✅ opencode — std::from_chars vur (locate-independent) + tam tüketim (`res.ptr==endp`) zorunlu` |
| N-BATTORD (BUG-MED) | src/logitech_hidpp.cpp get_battery_status | Prob sırası battery_status→unified→voltage; preferred_battery_source (quirks) unified→status→voltage → iki-feature'lı cihazda GUI ile daemon farklı feature'ı çözer | `✅ opencode — sıra unified→status→voltage ile hizalandı` |
| N-NOTIF (BUG-MED) | daemon/daemon.cpp HID++ drain callback | Pair-d receiver'larda her drain TÜM pair cihazların notification'larını görür; dev B iterasyonuna gelen A event'i B'nin feature map'iyle sınıflandırılıp B'nin piline yazılıyordu | `✅ opencode — callback kapısı: event.device_index != 0xFF ve != dev.device_index ise atla` |
| N-DPIFB (BUG-LOW) | src/logitech_hidpp.cpp:1590 | 0x2202 GetCaps başarılı ama GetDpi hata/kısa döndüğünde tüm sorgu nullopt (0x2201 legacy'ye düşmüyor; B3 polling-rate fallback'ten asimetrik) | `✅ opencode — query_ok bayrağı; GetDpi başarısızsa legacy 0x2201 dalına düşer` |
| N-DPICAP (IMP) | src/logitech_hidpp.cpp decode_dpi_levels:91-95 | step=1/last=0xFFFF marker çifti ~65k sentetik seviye şişirebilir | `✅ opencode — 2048 girişlik savunma tavanı` |
| N-UNISVC (BUG-LOW) | scripts/uninstall.sh:17,21 | `set -e` altında guard-then-stop arası unit durumu kıpırdarsa systemctl stop/disable başarısı BÜTÜN uninstall'ı abort eder (rm/udev/desktop temizliği çalışmaz); diğer tüm yıkıcı adımlar `|| true` idi | `✅ opencode — iki systemctl çağrısına da || true (komut, diğer adımlarla tutarlı)` |
| N-LODSB (GUI) | gui/hidpp_panel.inl:280-282 | capability biti set ama LOD baytı >2 (dolu olmayan 0xFF) → "High" gösterilir VE yazılır; transport get_lift_off_distance >2'yi reddediyor ama GUI ham baytı alıyordu | `✅ opencode — bayt <=2 ise kullan, değilse supports_lod=false (combo kapalı, sentinel yazılmaz)` |
| N-DOCR6 | AGENTS.md:294 | "ips_factor 0'a clamp" belgesi kodu yanlış anlatıyor (kod IPS_FACTOR_MAX=1e6'ya clamp ediyor) | `✅ opencode — metin koda hizalandı` |

SKIP/KARAR: Oracle NaN-comparator (P155 io-cap0 + sync fractional bölgeleri eşitlenmemiş) — known_deviations ~67 satır yeniden üretmeyi gerektirir; CI geçidini riske atmamak için ERTELENDİ. GUI rate-combo 333/166→250 ekran snap'i (legacy rate'ler HW_RATES'te yok) — görüntü kozmetiği, durum satırı gerçek rate'i gösteriyor; kombo dinamik modeli büyük değişiklik, dokunulmadı. hidpp_panel apply "her zaman 3 yazma" — unsupported yazma zaten "(rejected)" ile raporlanıyor (+N-LODSB garbage'ı engelledi); ek kapı gerekmedi. BATTERY_CHARGE 0x00→unknown değişikliği — bazı cihazların gerçek %0'ını bozma riski (SPECULATIVE donanım); yapılmadı. setup.sh verify_install çıkış kodu — container-friendly "0" davranışı korundu (marker yerine çıktı denetlenir); dokunulmadı. decode_dpi_levels 2048 tavanı + LOD bayt guard dahil HID++ değişiklikleri donanım doğrulamasız (kod-doğrulamalı) — FIX_LOG'da not edildi; gerçek cihaz ÜNİVERSAL testi önerisi.

Doğrulama: BUILD CLEAN (warning:0) + unit 33816/33816 (yeni N-LUTSW testi dahil) + ASan/UBSan clean (EXIT=0) + tr_coverage PASS + CLI gate'leri EXIT=0.

**Bu turda değişen dosyalar (kilit, round-7'de aynı dosyalara eşzamanlı edit YAPMAYIN):** `src/config.cpp`, `gui/daemon_comm.inl`, `src/logitech_hidpp.cpp`, `daemon/daemon.cpp`, `scripts/uninstall.sh`, `gui/hidpp_panel.inl`, `AGENTS.md`, `tests/test_accel.cpp`.

---

## round-7 (big-pickle/opencode — 12.09.2026) — agent'lar (GUI internals + daemon runtime edge + parity/coverage) bulguları

Kıyas: 3 paralel explore agent'ı. GUI internals agent'ı 1 MED (Y-LUT sessiz kaybı) + 2 LOW (dil geçişi kozmetikleri) + önceden ertelenen G1'i IMPROVEMENT olarak raporladı. Daemon runtime agent'ı: MED (raw çift-SYN, real_polling_rate data race, drain QoS) + 4 LOW. Parity agent'ı: 2 MED (CMake install prefix/settings clobber) + 4 LOW. Oracle/engine tarafı bu tur tekrar BUG bulmadı.

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| N-RAWSYN (BUG-MED) | daemon/daemon.cpp:2389-2400 | Raw passthrough'ta batch sonu sentetik SYN: raw REL'ler inline forward edilir (wrote_unsynced_event=true) ve gerçek SYN bir sonraki batch'te gelirse `[REL][SYN_sent][SYN_real]` çift-SYN / ekstra boş çerçeve — T-B1 byte-identical 1:1 kontratının ihlali (LOW-1'in accel tarafı düzeltilirken raw tarafı kaçmıştı) | `✅ opencode — synthetic SYN yalnız accel modda; raw'da gerçek SYN çerçeveyi kapatır` |
| N-PRATE (BUG-MED) | daemon/daemon.hpp:31-40, daemon.cpp | `real_polling_rate` plain int: loop thread yazar (işaret kapsamı yok), IPC thread devices_mutex_ altında okur → TSan/weak-memory C++ data race (diyğer status alanları mutex altında; bu tek istisna) | `✅ opencode — telemetry_state'e `std::atomic<int> real_polling_rate` (movability için unique_ptr içi atomics konvansiyonu); 3 okuma/yazma noktası store/load relaxed` |
| N-TELSR (BUG-LOW) | daemon/daemon.cpp:1061-1076 | TEL-1 reset: payload relaxed-zero + samples.store(0) — önceki even counter'ı yüklemiş reader, weak-memory'de kısmi sıfırlanmış payload ile s1==s2 eşleşmesi (telem_ok=true+çöp); nanosecond pencere, ARM'de gerçek | `✅ opencode — fetch_add(1, release) ODD işaret → zero payload → store(0, release) sırası (hot-path konvansiyonu; reader asla stale-even+dirik eşleştirmez, telem_ok=false korunur)` |
| N-UVIRT (BUG-LOW) | daemon/daemon.cpp:884-895,1309-1321 | create_virtual_device başarısızlığında deny yok: /dev/uinput yoksa/yetkisizse her tarama aynı node'u open/grab/release ediyor (2s churn); open hataları zaten deny'liydi | `✅ opencode — iki dalda da deny_reopen (path+device_id çift anahtar); DENY_REOPEN_MS=5s retry kapısı` |
| N-YLUT (BUG-MED) | gui/widgets_sync.inl:194-204 | Unlinked Y'de ay.length/data KOŞULSUZ X'ten kopyalanıyor (G2-FIX): CLI/JSON'dan gelmiş belirgin Y eğrisi ilk GUI edit+save'de sessizce SİLİNİYOR (Görünmez; daemon kullanınca ortaya çıkar) | `✅ opencode — kopya yalnız `ay.length==0` (Y hiç eğri almadıysa); mevcut Y LUT'u korunur` |
| N-LANGDM (BUG-LOW) | gui/tr.inl:674-692 | refresh_language cihaz combo modelini "(unplugged)" yer tutucusuz yeniden kurar → kayıtlı index out-of-range, GLib critical + geçici boş seçim | `✅ opencode — index model boyutuna clamp` |
| N-LANGSB (BUG-LOW) | gui/tr.inl:711-716 | Status bar "Ready." yalnız İngilizce string ile karşılaştırılıyor; tr→en geçişinde stale "Hazır." sonsuza dek kalıyor | `✅ opencode — iki dilin ready string'i de tanınır` |
| N-MTDEFG (INFO/def) | gui/mouse_test.inl:280-286 | mouse_test_escape set_status'u window_destroyed kapısız (2026'da erişilemez ama diğer tüm callback'ler kıyı kaplıyordu) | `✅ opencode — `window_destroyed` kapısı eklendi` |
| N-CMOME (BUG-MED) | CMakeLists.txt, README.md:58-70 | CMake `make install` default /usr/local prefix'e kurar → servis ExecStart=/usr/bin/... uyumsuz; ayrıca /etc/rawaccel/settings.json'u KOŞULSUZ üzerine yazar (setup.sh korur, PKGBUILD backup=); modules-load.d kurmaz; README install'a sessizdi | `✅ opencode — /usr/local prefix uyarısı; settings.json yalnız yoksa yazılır (install(CODE)); modules-load.d uinput.conf kurulur; README `cmake --install --prefix /usr` + notlar` |
| N-IDT0 (BUG-LOW) | gui/devices.inl:118-127,136-164, gui/app_state.hpp:50-53 | 3. kademe (vid/pid yok + serial yok) stable_id = by-id path (resolve edilmiş); daemon fallback'i ham `/dev/input/eventN` (dev.path) → cihaz-başına profil ataması farklı device_id'lere işaret edebilir (yalnız by-id'si olan, vid/pid'siz cihazlar — nadir) | ❌ YANLIŞ ÇIKTI — round-8'de GERİ ALINDI (N-IDT0-R): daemon tier-3 fallback'i ham eventN DEĞİL, resolve edilmiş dev.path'tir (`find_mice` satır 432 `resolve_stable_id` uygular). Round-7 GUI değişikliği uyumsuzluk yarattı; resolved path davranışı geri getirildi. |
| N-UNMLU (BUG-LOW) | scripts/setup.sh clean_old_install | uninstall, PKGBUILD'un kurduğu `/usr/lib/modules-load.d/rawaccel.conf`'u temizlemiyor (yalnız /etc/...); kurallarla modülü yükleyen makinede kalıntı | `✅ opencode — file listesine eklendi` |
| N-README (BUG-LOW) | scripts/README.md manual install | Manual kurulum quirks ve desktop dosyasını atlıyor → libinput çift hızlanma riski + GUI başlatıcı yok | `✅ opencode — quirks + desktop install komutları eklendi` |
| N-COMR7 (doc) | gui/widgets_sync.inl:377-381 | match_app yorumu kodla çelişiyor (re-entrancy S->updating guard ile zaten engelli) | `✅ opencode — yorum düzeltildi` |

SKIP/KARAR: Drain time-budget (kMaxDrainPerBatch 4096×32 — sürekli dolu cihaz loop thread'ini monopole edebilir, diyer cihazların çerçeve gecikmesi artar) — tasarım tercihi (düşürme yok, QoS eşleşmesi); epoll level-trigger her cycle re-fire eder, cihaz-bazlı round-robin yeniden tasarım gerektirir, ERTELENDİ. >16 button/wheel overflow'unda intra-frame [buttons…][motion…] sırası (accel modu, tek çerçeve, libinput sınıflandırmasını etkilemez, T-B1 bunu yakalayamaz) — kozmetik asimetri, dokunulmadı. Torn-read tail log'u yalnızca whole-read'de (read_count==0) — kernel evdev tam event döndürür, floor guard misalignment'ı zaten handle eder; savunmacı boşluk, dokunulmadı. Stale XDG pid dosyası reclaim (düşük öncelikli kazanan) — liveness gate tüm dosyaları inceler, en kötü senaryo koruyucu ret + dosya kalıntısı; kozmetik, dokunulmadı. uinput virtmouse filter policy (BUG-26/aj2) — belgeli. G1 kwin focus sync IPC stall (150ms/main-thread) — GUI-runtime testi bu ortamda yok; g_idle_add+debounce önerisi round-7'de de ertelendi (aynı gerekçe). N-DPICAP test ekleme — test hedefi logitech_hidpp.cpp link etmiyor; decode_dpi_levels pure-function taşınması ayrı iş (borç olarak kayıtlı).

Doğrulama: BUILD CLEAN (warning:0) + unit 33816/33816 + ASan/UBSan clean (EXIT=0) + tr_coverage PASS + CLI gate'leri EXIT=0 + oracle OK (1071 satır / 67 known deviation).

**Bu turda değişen dosyalar (kilit, round-8'de aynı dosyalara eşzamanlı edit YAPMAYIN):** `daemon/daemon.cpp`, `daemon/daemon.hpp`, `gui/widgets_sync.inl`, `gui/tr.inl`, `gui/mouse_test.inl`, `gui/devices.inl`, `gui/app_state.hpp`, `CMakeLists.txt`, `setup.sh`, `README.md`, `FIX_LOG.md`.

---

## round-8 (big-pickle/opencode — 12.09.2026) — agent'lar (evdev/uinput semantiği + HID++/receiver/quirks + per-app profil/IPC) bulguları

Kıyas: 3 paralel explore agent'ı. evdev agent'ı **1 BUG-MED (POLL-1 dead code)** + SYN_MT_REPORT accel sıralaması + vdev isim kesilmesi + INFO'lar raporladı. HID++ agent'ı **1 BUG-MED (ortak-hidraw drain açlığı)** + LOW'lar raporladı. Per-app agent'ı **2 BUG-MED (digit-drop normalizasyonu, daemon restart'ta focus kaybı)** + N-IDT0 reversion'ı tespit etti. Üç bulgu kod-doğrulandı: POLL-1 (daemon.cpp:2274 vs 2277 + flush_motion satır 2026 zaten anchor'u ilerletiyor → karşılaştırma hep eşit → ring hiç dolmuyor), HIDN (drain cihaz-başına aynı fd'den okur; 0xFF shell ilk tüketir), IDT0-R (daemon.cpp:432 tier-3 = by-id path).

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| N-POLL1 (BUG-MED) | daemon/daemon.cpp:2273-2277 (+2026) | real_polling_rate ölçümü ölü kod: `dev.last_frame_ev_us = frame_ev_us` KARŞILAŞTIRMADAN ÖNCE yazılıyor (flush_motion satır 2026 zaten aynı SYN değerine ilerletmişti) → `frame_ev_us > dev.last_frame_ev_us` hep YANLIŞ → interval 0 → ring asla dolmaz → `real_polling_rate` sonsuza dek 0; SM-4'ün "real-measured" önceliği sessizce sysfs/profile'e düşer (status_json "polling_rate": 0 dahil) | `✅ opencode — önceki anchor bir yerele alınır, ardından üzerine yazılır; empty-frame re-anchor'da (SM-4) interval ölçümü aktif` |
| N-SYNMT (BUG-MED) | daemon/daemon.cpp:2243-2252 | Accel modunda SYN_MT_REPORT inline forward edilirken eşlik eden ABS_MT_* slot verisi frame SYN'ine KUYRUKLANIYOR → çıktı [SYN_MT_REPORT…][ABS_MT…][SYN] sırası ters → libinput touch tracking bozulur (MT slot sınırlama hayatidir) | `✅ opencode — accel modda non-SYN_REPORT subtype'lar queued_events'e kuyruklanır (frame SYN'de kaynak sırası korunur); raw 1:1 inline kalır` |
| N-IDT0-R (REVERT) | gui/devices.inl:118-127,136-164, gui/app_state.hpp:53 | Round-7 N-IDT0 YANLIŞ yöne gitti: daemon `find_mice` satır 432 `resolve_stable_id` uygular → tier-3 fallback RESOLVE EDİLMİŞ dev.path (by-id), ham eventN değil. GUI raw_node kullanımı daemon'dan kopuk device_id'ler üretirdi | `✅ opencode — raw_node alanı/atamaları kaldırıldı; tier-3 stable_id = resolve edilmiş m.event_node (R7 öncesi davranışa dönüş)` |
| N-DIGIT (BUG-MED) | gui/daemon_comm.inl:214-229 | Focus normalizasyonu yalnız a-z/A-Z/nokta/alt-çizgi/binding koruyor, RAKAMLARI SİLİYOR → WM_CLASS "1password"→"password"; rakam içeren match_app profilleri asla eşleşmez | `✅ opencode — 0-9 korunur (küçük harfe çevrilerek)` |
| N-HIDN (BUG-MED) | daemon/daemon.cpp:1527-1546 | Unifying/Nano receiver TEK hidraw fd'yi tüm paired cihazlarla paylaşır; eski döngü fd'den cihaz-başına drain edip okuyordu → 0xFF "shell" (her zaman ilk) TÜM notification'ı tüketir, R6-4 filtre diğer device_index'leri atıyordu → paired mouse'lar CANLI pil/bağlantı bildirimi almıyor (yalnız 60s aktif sorgu — semptom "pil dakikada bir güncelleniyor") | `✅ opencode — transport başına BİR kez drain; her notification device_index ile SAHİBİNE yönlendirilir, o cihazın kendi feature map'iyle sınıflandırılır; 0xFF/unknown ilk transport cihazına (eski davranış)` |
| N-VNAME (BUG-MED) | daemon/daemon.cpp:767 | uinput ismi 79 baytta keser; uzun kaynak isminde "(RawAccel)" soneki klipleşir → is_physical_mouse self-ID filtresi vdev'i tanımaz → sıcak-eşleme kendi çıktısını grab eder → ÇİFT hızlanma | `✅ opencode — kMaxUinputName=79 içinde " (RawAccel)" işaretleyicisi GARANTİLİ kalacak şekilde taban isim kesilir (libevdev_uinput_set_phys 1.13.7'de yok — isim işaretleyicisi çözümü)` |
| N-RESEND (BUG-MED) | gui/daemon_comm.inl:499-515, gui/kwin_focus.inl, gui/app_state.hpp | KWin focus relay yalnız FOCUS DEĞİŞİKLİĞİNDE raporlar; daemon down/up döngüsünde (restart, crash, systemctl restart) app-scoped (match_app) profiller daemon yeni durumunu öğrenene kadar ATIL kalır — kullanıcı pencere değiştirene dek | `✅ opencode — GUI 3s poll'u daemon up-transition saptar (daemon_prev_running) ve kwin_focus_ctx son WM_CLASS'ı yeniden gönderir` |
| N-BAT100 (BUG-LOW) | src/logitech_hidpp.cpp:207 | parse_battery_status_feature level>100 clamp'siz (parse_battery_charge zaten clamp'lıyor); out-of-range bayt >%100 gösterebilir | `✅ opencode — >100 → 255 (unknown), 0→255 anlambilimi korunur` |
| N-CLASSNOT (doc) | daemon/daemon.hpp:212-213 | find_profile yorumu var olmayan "CLASSNOT matches" kavramını iddia ediyor; gerçek arama önceliği (device_id+app, all+app, device_id, all, active, first) farklı | `✅ opencode — yorum gerçek arama sırasına göre yeniden yazıldı` |

SKIP/KARAR: GUI vs daemon uinput filter phys sapması (GUI/devices ile is_physical_mouse filtreleri arasındaki görünüm farkı) — INFO, display-only; daemon tarafı phys+"uinput" zaten kontrol ediyor. pending_motion TTL — LOW-1 parkı tasarım gereği sınırlı/garantili serialize; dokunulmadı. set_device_index cache thrash (her drain feature cache temizliyordu) — R8-HIDN drenaj artık set_device_index çağırmadığından KENDİLİĞİNDEN çözüldü. BUG-24 specülasyonu (yanlış attribution) — R6-4 + R8-HIDN yönlendirmesi zaten kapsıyor, ek guard gerekmedi. quirks "32" G522 hiç eşleşmiyor — HP paneli görünümü, dokunulmadı. stop() join ≤20s — dakikalar içinde seyrek; dokunulmadı. Vdev self-ID için phys="uinput" (libevdev_uinput_set_phys) — kurulu libevdev 1.13.7 üstbilgisinde YOK; isim-işaretleyicisi garanti çözümü (N-VNAME) kullanıldı. Battery panel ikili sorgu asimetrisi (hidpp_panel.inl) — donanım doğrulamasız, erişilemez path; dokunulmadı.

Doğrulama: BUILD CLEAN (warning:0) + unit 33816/33816 + ASan/UBSan clean (EXIT=0) + tr_coverage PASS + CLI gate'leri EXIT=0 + oracle OK (1071 satır / 67 known deviation).

**Bu turda değişen dosyalar (kilit, round-9'da aynı dosyalara eşzamanlı edit YAPMAYIN):** `daemon/daemon.cpp`, `daemon/daemon.hpp`, `src/logitech_hidpp.cpp`, `gui/devices.inl`, `gui/app_state.hpp`, `gui/daemon_comm.inl`, `gui/kwin_focus.inl`, `FIX_LOG.md`.
---

## round-9 (big-pickle/opencode — 12.09.2026) — agent'lar (engine hot-path micro + config sanitize/default parity + GUI graph/LUT state machine) bulguları

Kıyas: 3 paralel explore agent'ı. Engine agent'ı **1 BUG-MED (classic GAIN cap_out + acceleration=0 → sabit ×cap.y boost)** doğruladı (referans `cap.x=+Inf` → identity; port `return offset` → tail aktif) + 1 INFO. Config agent'ı **1 BUG-MED (dpi spin min 100 vs veri alanı 1..32000 → CLI/JSON değeri load→save'de sessizce ikiye katlanır)** + BUG-LOW sınıfı raporladı; murat (defaults parity, serileşme, sanitize, migration, CLI domain) PASS. GUI agent'ı **1 BUG-MED (Max distance mode lp_norm=9999 sentinel round-trip → gerçek Lp değeri kalıcı silinir + Lp modu yapışkan)** + 2 INFO; LUT editör state machine / S->updating re-entrancy / Y-LUT koruması / teardown temiz.

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| R9-CAP0 (BUG-MED) | include/accel-classic.hpp:229 | Classic GAIN modu + `cap_mode=out` + `acceleration=0`: `accel_raised=pow(0,exp-1)=0` → base_fn≡0; eski guard `gain_inverse` `return offset` yapıyordu → `cap_x=input_offset`, kuyruk `cap_y·(1−offset/x)` tüm hızlarda AKTİF → sabit **×cap.y boost** (örn. cap{15,2.0} → her yerde gain 2.0). Referans accel=0'da `finite/0 → cap.x=+Inf` → kapsama sonsuzda → **identity** (inert config). Erişilebilir: GUI accel spin min 0; CLI "any finite"; sanitize accel=0'ı korur. Yalnız GAIN out-dalı; io/in dalı geometri-tabanlı (accel'siz) olduğundan etkilenmez | `✅ opencode — gain_inverse accel==0 → DBL_MAX (referans parity); oracle grid'i classic accel=0 içermez (deviations değişmez), test_accel:8680-8692 isfinite&&>0 hâlâ geçer` |
| R9-DPI (BUG-MED) | gui/ui_builder.inl:525 | `dpi_spin = make_spin(100, 32000, ...)` — veri alanı 1..32000 (sanitize config.cpp:361-362, CLI set-param gates). dpi_factor = 1000/dpi (daemon.cpp:1071): CLI/JSON `dpi:50` → GUI yüklerken spin 100'e clamp → kayıt 100 yazılır → dpi_factor 20→10 → **hassasiyet sessizce yarıya düşer** | `✅ opencode — make_spin(1, 32000, ...)`; R5-F halflife emsaliyle aynı sınıf |
| R9-LPNRM (BUG-MED) | gui/widgets_sync.inl:234,357-361, gui/app_state.hpp | dist mode "Max" `sp.lp_norm=9999` sentinel'ini profİL'e yazıyor; herhangi bir reload 9999'u `dist_idx=1` (Max) yapıyor VE spini (1.0..15.5) 9999 ile besliyor → tekrar "Lp" seçince spin >=16 okunur → daemon hâlâ Max → **gerçek Lp normu kalıcı silinir + Lp moduna dönüş yapışkan** | `✅ opencode — S->lp_norm_mem son gerçek normu hatırlar (write'ta dist!=1 iken yakalanır); read sentinel'i asla spinde göstermez (combo yine Max gösterir)` |

SKIP/KARAR: Classic linear path (exp≤1) negatif acceleration → negatif gain (imleç tersi) — GUI spin min 0 (erişilemez), CLI-dejenere; GUI negatifleri imleç inversiyonunu önlemek için BİLİNÇLİ reddediyor; belgeli, düzeltilmedi. Spin-envelope sınıfı (acceleration≤20, limit≤100, decay_rate≤10, smooth≤1, sync_speed≤100, motivity/gamma 0.01..10, speed_min/max≤500, lp_norm 1.0..15.5) — sanitize bu alanları BİLİNÇLİ ÜST SINIRSIZ bırakıyor (daemon asla reddetmez); GUI sayaçları ergonomik UX kapağı = tek uygulayıcı. Sessiz mutasyon yalnız CLI/direct-JSON güç-kullanıcı değerlerini yükleyip GUI'den kaydederken oluşur; zararlı üye (dpi min) düzeltildi; negatif-accel bilinçli reddedildi. GUI INFO'ları (tr.inl:683 unref-sonrası get_n_items kırılganlığı — g_tk_drop_down set_model referansı sayesinde bugün güvenli; kwin_focus bus_name teardown boşluğu; join ≤2-3s) — davranışsal defekt değil, dokunulmadı. Engine "temiz" alanları (sync GAIN LUT, lookup, power, natural/jump, modifier/EMA, daemon motion) — aksiyon yok.

Doğrulama: BUILD CLEAN (warning:0) + unit 33816/33816 + ASan/UBSan clean (EXIT=0) + tr_coverage PASS + CLI gate'leri EXIT=0 + oracle OK (1071 satır / 67 known deviation).

**Bu turda değişen dosyalar (kilit, round-10'da aynı dosyalara eşzamanlı edit YAPMAYIN):** `include/accel-classic.hpp`, `gui/ui_builder.inl`, `gui/widgets_sync.inl`, `gui/app_state.hpp`, `FIX_LOG.md`.

---

## round-10 (big-pickle/opencode — 12.09.2026) — agent'lar (CLI surface + daemon lifecycle/hot-plug/threading + deployment/E2E) bulguları

Kıyas: 3 paralel explore agent'ı. CLI agent'ı 7 hedefin TAMAMINDA **sıfır defekt** (flag parsing simetrisi CLI/daemon, 8 mutating komut save→push deseni, safe_save atomicity, latency IPC+SIGUSR1, IPC parsing guard'ları, exit-code disiplini, set-param domain'leri sanitize ile birebir — tüm sabitler aynı header). Daemon lifecycle agent'ı **2 BUG-MED + 1 BUG-LOW + 1 INFO** doğruladı. Deployment agent'ı **1 BUG-MED (E2E/systemd pid-kilit) + 4 BUG-LOW + 3 INFO**; kurulum/CI"checked-OK" kısımları: sürüm senkronu (1.1.0), udev/quirk eşleşmesi, systemd-enable vs GUI pkexec, CI concurrency/oracle/sanitizers/fuzz, E2E assertion gerçekçiliği, setup.sh dep gate'leri/idempotency.

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| R10-EIO (BUG-MED) | daemon/daemon.cpp | Geçici EIO ile cihaz grab kümesinden düştüğünde (disconnect cleanup, 1780-1782) `deny_reopen` çağrılıyor ama `rescan_needed_` asla set edilmiyordu; başka cihaz açıkken self-heal ~2s tarama koşmaz (1701 `devices_empty || rescan_needed_`) → 5s deny penceresi bittikten sonra cihaz replug edilmezse SONSUZA DEK yeniden grab edilmiyor (yorum 1690-1692 söz verilen davranışı vermiyor). Üstelik scan'deki deny-pencere skip'i (1280-1283) `missed_any` set etmiyor → tek taramada cadence ölüyor | `✅ opencode — (a) disconnect cleanup'e `rescan_needed_.store(true)`; (b) do_hotplug_scan deny-skip'ine `missed_any=true` → cadence deny penceresi BOYUNCA canlı kalır, pencere bitince open denenir` |
| R10-REGRB (BUG-MED) | daemon/daemon.cpp apply_active_app / apply_new_config | LIVE-DISABLE release geri alınamıyor: disabled profile `release_device()` ile devices_/opened_* kümesinden silinir; tek yeniden-grab yolu `!any_live` (tüm cihazlar kapalı) dalı. Başka cihaz açıkken profile yeniden enable edilince cihaz açık kümede OLMADIĞI için kayıp kalır → kullanıcı replug'a kadar RawAccel'sız kalır | `✅ opencode — iki fonksiyonun sonuna `rescan_needed_.store(true)`; do_hotplug_scan hpileri zaten disabled profile'i kalıcı-gate ile atlar (1334-1341) → disable'da churn yok, enable sonrası ~2s içinde yeniden grab` |
| R10-PUSHG (BUG-LOW) | daemon/daemon.cpp push_config (no-op guard ~604-620) | Guard yalnız UYGULANMIŞ config (`config_hash_`/`config_`) ile karşılaştırıyor; worker'ın arm ettiği ama loop henüz UYGULAMADIĞI `push_cfg_` hiç danışılmıyor → push A (applied) iken B push'u (pending) ortada, kullanıcı tekrar A gönderirse "hash unchanged" ile ateşlenmeden düşer, pending B uygulanır → **revert kaybı** | `✅ opencode — pending snapshot push_cfg_mu_ ile devices_mutex_ ÖNCESİ alınır (loop order'ı push_cfg_mu_→devices_mutex_, tersine dönmiyor); new_json == pending → skip + log` |
| R10-PUSHTC (BUG-LOW) | daemon/daemon.cpp run_loop push-apply (~1650-1657) | IPC push apply SIGHUP reload'un aksine try/catch'siz — uygulama sırasında fırlama (log/string alloc) loop thread'i std::terminate ile daemon'u öldürür | `✅ opencode — reload'u yansıtır try/catch; pending her iki yolda da false (takılı config her iterasyonda yeniden denenmez, diskte durur, sonraki start'ta etkin olur)` |
| R10-E2EX (BUG-LOW) | tests/run_e2e.sh:74 | `[[ $SKIP -gt 0 ]] && exit 77` FAIL kontrolünden ÖNCE → gerçek bir başarısızlık, başka phase skip edince 77 ile maskeleniyor (kontrat: 0/1/77) | `✅ opencode — FAIL önce kontrol edilir (exit 1), sonra SKIP (77)` |
| R10-UNMLL (BUG-LOW) | scripts/uninstall.sh:77 | modules-load.d temizliği yalnız /etc; CMakeLists:185 + PKGBUILD:74 `/usr/lib/modules-load.d` kopyasını kuruyor (setup.sh temizliyor) → pacman/CMake kurulumu kalıntı bırakıyordu | `✅ opencode — `rm -f /usr/lib/modules-load.d/rawaccel.conf` eklendi` |
| R10-CITRC (BUG-LOW) | .github/workflows/ci.yml | tr_coverage CI'da KOŞMUYORDU (AGENTS.md belgeliyor, exit-1-on-MISSING) → yeni tercümesiz GUI string yeşilde yayınlanıyordu | `✅ opencode — build-and-test'e `run_tr_coverage.sh` step'i eklendi (unit tests'ten sonra)` |
| R10-LDREG (INFO) | .github/workflows/ci.yml:41 | warning-gate regex ` (ld\|collect2):` önünde literal boşluk ister — gerçek `/usr/bin/ld:` / `collect2:` biçimlerini yakalamaz (set -e zaten build fail'i keser; yalnız uyarı kaçağı sorunu) | `✅ opencode — `(^|[^[:alnum:]_])` önek eklendi: `/usr/bin/ld:` eşleşir, `collect2_test.c:` eşleşmez` |
| R10-E2EBIN (INFO) | .gitignore | Derlenmiş `tests/e2e_harness` binary'si yoktu → yeniden derlemeden bayat harness çalışabilirdi (run_e2e.sh:35 only-nt guard'ına rağmen) | `✅ opencode — .gitignore'a `tests/e2e_harness` eklendi` |

SKIP/KARAR: **E2E/systemd pid-kilit (BUG-MED, ERTELENDİ)** — SIGSTOPlu daemon hâlâ `kill(pid,0)`'a yanıt verir + `/proc/comm` okunur → harness'ın spawnladığı daemon `pid_file_is_live`'da "Another instance..." ile exit 1 (main.cpp:371-409). SIGSTOP lock'u/grab'ı korur ama PID dosyasını asla boşaltmaz → E2E "servis kurulu makinede" tasarımının zıttına başarısız. Önerilen fix'ler belgelendi (servis etrafında stop/start; ya da SIGSTOP sonrası pid/sock artıklarını sil + EXIT trap'te SIGCONT + `systemctl restart rawaccel`; en temizi `systemctl stop`/`start`), root+/dev/uinput gerektirdiğinden DOĞRULANMADI → ERTELENDİ. setup.sh verify_install çıkış-0 (BUG-MED) — round-7 KARAR'ı ile çelişiyor: "container-friendly '0' davranışı korundu (marker yerine çıktı denetlenir)" (FIX_LOG:320) → SKIP. kde-fix-accel --undo/--remove yedek geri yüklemiyor + sert 2/-0.5 (BUG-LOW) — yedekler "kullanıcının kendi geri yükleyebileceği snapshot'lar" diye BELGELENDİ; en-yeni-yedeği geri yüklemek tekrarlı --fix sonrası zaten-flat yedeği geri koyar (belirsiz), KDE/Plasma davranışı burada doğrulanamaz → SKIP. CLI: CLI agent temiz raporu aksiyonsuz. Oracle: daemon.cpp/shell/CI değişiklikleri accel headers'a dokunmadı → deviations sabit 67.

Doğrulama: BUILD CLEAN (warning:0) + unit 33816/33816 + ASan/UBSan clean (EXIT=0) + tr_coverage PASS + oracle OK (1071 satır / 67 known deviation). `bash -n` testleri/scripts/uninstall ✓.

**Bu turda değişen dosyalar (kilit, round-11'de aynı dosyalara eşzamanlı edit YAPMAYIN):** `daemon/daemon.cpp`, `tests/run_e2e.sh`, `scripts/uninstall.sh`, `.github/workflows/ci.yml`, `.gitignore`, `FIX_LOG.md`. (Round-9 kilitleri — accel-classic.hpp, gui/ui_builder.inl, gui/widgets_sync.inl, gui/app_state.hpp — serbest.)

---

## round-11 (big-pickle/opencode — 12.09.2026) — agent'lar (engine math/smoother + GUI dialogs/LUT/hidpp + IPC protocol/socket security) bulguları

Kıyas: 3 paralel explore agent'ı. Engine agent'ı **1 BUG-LOW (POLL-1 feed hâlâ ölü: ring yalnız empty-frame'lerden besleniyordu)** + 1 INFO test defekti (TST-DPI) doğruladı; SM-6/REC-EMA/TEL-1/WALL-SM4 bilinçli sapmalar olarak INFO. GUI agent'ı **1 BUG-MED (MAX_PROFILES overflow — 4 push_back guard'sız) + 1 BUG-LOW (speed_min/max cross-constraint)** doğruladı (NORMALIZED_DPI=1000 teyidi, config.cpp:543-544 clamp'ı, rawaccel-base.hpp:28 MAX_PROFILES=256). IPC agent'ı **R11-PUSHOK (BUG-MED raporu, analizde BUG-LOW kıvamı — ok:false'da yine SIGHUP fallback + yanıltıcı "reload only") + BUG-LOW (FOCR: tek atımlık 150ms focus raporu seri IPC sunucusu meşgulken sessizce düşer)** + 4 INFO (seri accept-loop DoS tasarım notu, /tmp pid fallback'i socket yoluna yansımıyor, input-group trust sınırı kabul, socket protokolü için otomatik test yok).

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| R11-POLL1 (BUG-LOW) | daemon/daemon.cpp:2128-2129 + SYN handler 2393+ | round-8 POLL-1 feed'i motion path'te HÂLÂ ölüydü: feed SYN handler'ında, `flush_motion` anchor'ı frame_ev_us'a ilerlettikten SONRA örnekliyor (2402-2403) → motion frame'leri interval 0 ölçüp ring'den reddediliyor; ring yalnız empty-frame'lerden doluyordu → yalnız hareket eden cihazda `real_polling_rate` aç kalır, SM-4 (2104) ve status JSON (2833) sysfs/nominal'a düşer | `✅ opencode — ring feed flush_motion'a, anchor ilerlemesinden (2129) hemen ÖNCE taşındı (önceki base bozulmadan; SM-1 interval'ının birebir kendisi); empty-frame dalı yalnızca SM-4 anchor re-advance'ini tutar` |
| R11-MAXP (BUG-MED) | gui/profile_mgr.inl:194/:420/:598, gui/widgets_sync.inl:672 | MAX_PROFILES=256 tanımlı (rawaccel-base.hpp:28) ve load'da config.cpp:678-682 256'yı keser, ama GUI'nin New/Duplicate/Import/Save-As push_back'leri guard'sız → #257+ profil GUI'de var, kaydedilince **sessizce kaybolur**. CLI 4 yolu da guard'lıyor (cli/main.cpp:559-563,642-644,687-691,1493-1499) → GUI/CLI asimetrisi | `✅ opencode — 4 siteye `profiles.size() >= MAX_PROFILES` guard'ı + yeni tr anahtarı; tr_coverage PASS (CI'a bağlandığı için anahtarsız kırmızı kalırdı)` |
| R11-SPMIN (BUG-LOW) | gui/widgets_sync.inl:220-221 | speed_min/max aynı spin aralığında (make_spin(0,500)) — user Max<Min koyarsa GUI doğrudan kalıcılaştırır; daemon load sanitize (config.cpp:543-544) speed_max=speed_min'e clamp'lar → **GUI ile çalışan config sessizce sapar** | `✅ opencode — write_current'a sanitize-mirror clamp (her ikisi >0 ve max<min iken max=min) + spin anında yansıyacak şekilde snap` |
| R11-PUSHOK (BUG-LOW) | gui/main.cpp:52-60, gui/daemon_comm.inl:241-245, gui/app_state.hpp:303 | push `ok:false` (açık red) ile "yanıt yok" aynı `false`'a yapışıyor → red durumunda %SIGHUP fallback'i root systemd daemon'un KENDİ (bayat) config'ini yükletip "Saved locally & signaled daemon (reload only)" ile **başarı muamelesi** yapar; CLI'da P156 guard'ı (ok:false → SIGHUP YOK) GUI'de eksikti | `✅ opencode — daemon_ipc_push_config tri-state (1=ok, 0=rejected, -1=yanıt yok); red'de SIGHUP ATLANIR, daemon'un "error" mesajı durum çubuğuna taşınır; "yanıt yok" ise eski fallback'i (pre-RPC daemon/same-user) korur` |
| R11-FOCR (BUG-LOW) | gui/daemon_comm.inl:214-231, gui/kwin_focus.inl:75 | Focus raporu tek atımlık 150ms bütçeyle seri IPC sunucusuna gider; daemon meşgulse zaman aşımı → app-scoped profil anahtarı **sessizce kaybolur** (retry yok; R8-RESEND yalnız daemon restart'ını kapsar). set_active_app yanıtı zaten `{"ok":true}` (daemon.cpp:3173) | `✅ opencode — daemon_ipc_set_active_app bool (ok:true ack) döner; drop'ta ctx->last_app ile 1000ms sonra TEK sınırlı retry (g_timeout_add, best-effort kontratı korunur)` |
| TST-DPI (INFO) | tests/fuzz_accel.cpp:108,119 | fuzz dpi_factor = dpi/1000.0 — runtime NORMALIZED_DPI/dpi (daemon.cpp:1097, daemon.hpp:57-60) ile **ters** (800dpi → fuzz 0.8, runtime 1.25) → fuzz, gerçek çalışma etmediği girdi uzayını tarıyordu (sadece test defekti) | `✅ opencode — iki site NORMALIZED_DPI / dpi'ye çevrildi` |

SKIP/KARAR: **WALL-SM4** — idle re-measure yalnız kernel-timestamp path'inde (ilk frame/pre-grab-anchor'da wall-clock fallback) — belgeli, bilinçli. **SM-6** ek trend-damping + kMaxTrendSlope clamp, **REC-EMA** reconfigure'lerde EMA toplamlarını koruma, **TEL-1** telem_in_ips'in fiziksel input'u raporlaması — üçü de bilinçli tasarım sapması, dokunulmadı. **IPC INFO'ları**: seri accept-loop'un tek client'a içsel DoS toleransı (daemon SO_SNDTIMEO 2s + request deadlineli), /tmp pid fallback'inin socket path'e yansımaması (DIR altı) — davranışsal defekt değil; input-group trust sınırı kabul edildi (SO_PEERCRED gereği); socket protokolü için otomatik test yok — fuzz/E2E kapsamına ertelendi (root+uinput). POLL ring yeni besleme kuralı: yalnız motion frame'leri (empty-frame only cihazlar ring'i beslemez — doğru tasarım, gerçek poll period hareketle ölçülür; hâlâ 16 örnek kapak + 100..10000us filtre).

Doğrulama: BUILD CLEAN (warning:0) + unit 33816/33816 + ASan/UBSan clean (EXIT=0) + tr_coverage PASS (2 yeni anahtar) + oracle OK (1071 satır / 67 known deviation — accel headers değişmedi, deviations sabit).

**Bu turda değişen dosyalar (kilit, round-12'de aynı dosyalara eşzamanlı edit YAPMAYIN):** `daemon/daemon.cpp`, `gui/profile_mgr.inl`, `gui/widgets_sync.inl`, `gui/daemon_comm.inl`, `gui/app_state.hpp`, `gui/main.cpp`, `gui/kwin_focus.inl`, `gui/tr.inl`, `tests/fuzz_accel.cpp`, `FIX_LOG.md`. (Round-10 kilitleri — daemon/daemon.cpp, tests/run_e2e.sh, scripts/uninstall.sh, .github/workflows/ci.yml, .gitignore — serbest; daemon.cpp round-11 listesinde devam ediyor.)

---

## round-12 (big-pickle/opencode — 12.09.2026) — agent'lar (GUI↔daemon lifecycle + telemetry/latency/mouse_test + presets/migration/dialogs) bulguları

Kıyas: 3 paralel explore agent'ı. Lifecycle agent'ı **1 BUG-MED (R11-FOCR'IN KENDİ REGRESYONU: retry, fire anında ctx yerine çekilmiş WM_CLASS'ı tekrar gönderiyordu)** + 4 BUG-LOW (recv EINTR yok, GUI pid dosyası hidepid altında canlı daemon pid'ini silebilir, startup race, join ~9s) doğruladı. Telemetry agent'ı **2 BUG-LOW (raw'a geçişte stale accel-era lat_* yayınlanmaya devam ediyor; mouse_test telem_wall_ms'a bakmıyor + raw cihazı sonsuza dek "Awaiting motion")** doğruladı (AGENTS.md kontratı: raw passthrough lat_*/telem_* taşımaz). Dialog/migration agent'ı **2 BUG-LOW (256-char ad + " (copy)" → reload'da isim çakışması; GUI import sessiz LUT kırpıyor — CLI reddediyor)** + INFO'lar doğruladı (CLI import LUT reddi kalıbı cli/main.cpp:1420-1458).

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| R12-STALERT (BUG-MED) | gui/kwin_focus.inl:75-87 | R11-FOCR retry'i fire anında durumu YENİDEN okumuyordu: retry, capture edilmiş eski WM_CLASS'ı her durumda geri gönderiyor → arada başarılı YENİ bir rapor daemon'a ulaştıysa retry bunu GERİ ALIR (yanlış app-scoped profil, bir sonraki focus değişimine kadar). Üstüne yalnız non-empty sınıflar retry'liyordu — düşen CLEAR raporu app-scoped profili kalıcılaştırıyordu | `✅ opencode — RetryCtx{kwin_focus_ctx*, queued} + g_timeout_add_full + GDestroyNotify; fire'da ctx->last_app == queued ise gönder, değilse ATLA (yeni rapor asla revert edilmez); retry yalnız SEND FAIL'de planlanır (başarılı rapor için çift IPC yok); clear raporları da aynı yoldan "none" gönderir (asimetri kapandı)` |
| R12-EINTR (BUG-LOW) | gui/daemon_comm.inl recv döngüsü | daemon_ipc_send_raw recv() burada EINTR ile `n < 0` break'ine düşüyor (send döngüsü devam ediyor) → ipc oturum ortası sinyal (SIGCHLD pkexec reaping vb.) **geçerli yanıtı çöpe atar** | `✅ opencode — `n < 0 && errno == EINTR` → continue (send loop ile simetrik)` |
| R12-PIDUNL (BUG-LOW) | gui/daemon_comm.inl:269-333 | pid_is_rawaccel_daemon bool → read_daemon_pid "daemon değil" kabulüyle bayat sanıp **unlink ediyordu**; restricted /proc'ta (hidepid) canlı root daemon exe/comm okunamaz → GUI, daemon'un $XDG_RUNTIME_DIR'deki kendi kilidini **eterl** | `✅ opencode — pid_probe_rawaccel_daemon tri-state (1=daemon, 0=kesin olmayan/gitti → unlink, -1=doğrulanamaz → KORU); hem PID-dosya hem /proc tarama siteleri buna bağlandı; daemon PID-3 muhafazakarlığını yansıtır` |
| R12-LATRAW (BUG-LOW) | daemon/daemon.cpp apply_profile + daemon/lat_stats.hpp | TEL-1 reset'i yalnız telem_* seqlock'u sıfırlıyor; lat_* histogramı ayrı ve yalnız flush_motion'dan doluyor — raw passthrough'ta flush_motion HİÇ çalışmadığından status JSON **accel döneminden kalma lat_*'ları yayınlamaya devam ediyordu** (AGENTS.md: raw passthrough telemetri taşımaz) | `✅ opencode — lat_stats::reset() eklendi (mtx altında sıfırlama); apply_profile'ta YALNIZ `raw_passthrough` geçişinde `dev.lat.reset()` (accel tuning sanalı sıfırlamaz); status_json raw gate'i DEĞİŞMEDİ — ocak zaten boşalır` |
| R12-WALLMS (BUG-LOW) | gui/mouse_test.inl | Poll, daemon'un damgaladığı telem_wall_ms'a hiç bakmıyordu → 2s'den eski örnek "canlı" görünüyordu; raw passthrough cihazı (telemetri üretmez) sonsuza dek "Awaiting motion…" diyordu (raw bayrağı yok) | `✅ opencode — now_mono_raw_ms() (CLOCK_MONOTONIC_RAW, daemon ile aynı saat) + <2000ms tazelik kapısı `fresh`; cur_prof(S).prof.raw_passthrough → "—" + yeni durum `case 4` (tr anahtarı eklendi); stale örnek "awaiting"e düşer` |
| R12-DUPNM (BUG-LOW) | gui/profile_mgr.inl:404-413 | 256-char kaynak ad + " (copy)" → 263 → bellekte yaşıyor ama JSON round-trip (config yüklerken ad 256'ya kırpılır) kaynak ada GERİ DÖNER → bu uniquify döngüsünün önlemeye çalıştığı aynı-isim çakışması yeniden doğuyor | `✅ opencode — stem önceden küçültülür: `src.resize(256 - sufx.size() - 5)` (" 1001" = 5, döngünün en uzun nümerik soneki) → üretilen ad hiçbir zaman 256'yı aşamaz` |
| R12-IMPLUT (BUG-LOW) | src/config.cpp (yeni check_import_lut_size), include/config.hpp, gui/profile_mgr.inl import | GUI import profile_from_json üzerinden gidiyor; sanitize LUT'u sessizce kırpıyor/odd sayıda zemini indiriyor (O31-L1) → **GUI, dosyadan farklı bir eğriyi kabul ediyordu**; widgets_sync.inl:415-430 kırpma uyarısı ölü koddur (CLI zaten ~514/odd reddediyor) | `✅ opencode — config.cpp: ham JSON re-parse ile >514 ham eleman / tek sayı reddi (CLI'nin birebir kuralı); GUI import'ta `check_import_lut_size` + `set_status(trf(...))` (`"Import failed: %s. Fix the file and try again."` anahtarı); GUI'ye nlohmann çekilmedi (validator config.cpp tarafında)` |

SKIP/KARAR: **Lifecycle INFO'ları**: startup race (ilk focus raporu bus name sahiplenilmeden önce) — bus-name-owned guard'ı zaten var, tek raporluk boşluk zararsız. join ~9s (yorum "2-3s" yanlış) — blocked join'ler KWin'den bekleniyor; yorum düzeltilmedi. **Telemetry INFO'ları**: real_polling_rate GUI/CLI'de yüzeye çıkarılmıyor, raw inline write'lar drop loglamıyor — yeni yüzey değil, dokunulmadı (denetim turunda bilinecek INFO kuyruğu). **Dialog INFO'ları**: ölü kırpma uyarısı widgets_sync.inl:415-430 artık gerçek kullanımlı mı — R12-IMPLUT reddi import'u engellediği için iç dal çoğu zaman yürümez, guard olarak kalır; import sonrası profile switch'i (CFG-1), boş-ad uyarısı, migration-on-import — davranışsal iyileştirme, dokunulmadı. R5-S-2/STALERT kontratı notu: retry'ler artık daima bounded-1 ve stale replays imkânsız.

Doğrulama: BUILD CLEAN (warning:0) + unit 33816/33816 + ASan/UBSan clean (EXIT=0) + tr_coverage PASS (2 yeni anahtar: state-4 raw metni + import LUT reddi) + oracle OK (1071 satır / 67 known deviation — accel headers değişmedi) + `bash -n` scripts ✓.

**Bu turda değişen dosyalar (kilit, round-13'te aynı dosyalara eşzamanlı edit YAPMAYIN):** `daemon/daemon.cpp`, `daemon/lat_stats.hpp`, `src/config.cpp`, `include/config.hpp`, `gui/daemon_comm.inl`, `gui/kwin_focus.inl`, `gui/mouse_test.inl`, `gui/profile_mgr.inl`, `gui/tr.inl`, `FIX_LOG.md`. (Round-11 kilitleri — daemon/daemon.cpp, gui/profile_mgr.inl, gui/widgets_sync.inl, gui/daemon_comm.inl, gui/app_state.hpp, gui/main.cpp, gui/kwin_focus.inl, gui/tr.inl, tests/fuzz_accel.cpp — serbest; daemon.cpp, daemon_comm.inl, profile_mgr.inl, tr.inl, kwin_focus.inl round-12 listesinde devam ediyor.)

---

## round-13 (big-pickle/opencode — 12.09.2026) — agent'lar (GUI internals/GTK-race/Hidpp + daemon/setup/DE glue + CLI/tests/docs/INFO kuyruğu) bulguları

Kıyas: 3 paralel explore agent'ı. GUI agent'ı **2 BUG-LOW (kwin_focus_install başarısızlık yolunda ownership sızıntısı; /Focus nesnesi unregister edilmiyor)** doğruladı; tr kapsamı tüm GUI'de temiz (yeni anahtar yok), M/grafik/paneller "ok". Daemon/setup agent'ı **1 BUG-LOW (focus D-Bus handler'ı arg-tip/sender doğrulamasız)** + bilinen KARAR'ları teyit etti (setup.sh verify exit-0, udev Logitech-only kuralı, pkexec root config) — hepsi önceki turlarda belgelenmiş bilinçli davranış. CLI/'INFO kuyruğu agent'ı: **stale real_polling_rate raw passthrough'ta yayınlanmaya devam ediyor (R12-LATRAW'ın eksik ikizi)** + AGENTS.md CI bölümü bayat ("Three jobs" vs gerçek 4 job) + test sayaçları bayat (184/33764 vs 193/33816+) + reload/latency help metni yanıltıcı.

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| R13-REALRATE (BUG-LOW) | daemon/daemon.cpp apply_profile + cli/main.cpp status | R12-LATRAW yalnız lat_* histogramını sıfırladı; `real_polling_rate` (yalnız flush_motion ring feed'i üretir, raw'da HİÇ çalışmaz) ve frame-interval ring'ine dokunmadı → accel→raw geçişte status JSON accel döneminin ölçülen hızını yayınlamaya devam ediyordu (AGENTS.md ham telemetri kontratı). CLI insan okur `status` zaten `real_polling_rate`'i hiç göstermiyordu (JSON'da vardı) | `✅ opencode — apply_profile raw geçişinde `real_polling_rate.store(0)` + `frame_ev_us_count/next` sıfırlanır (raw→accel taze ölçer); CLI status cihaz bloğuna `live: N Hz` eklendi (yalnız mean>0 iken)` |
| R13-KWINOWN (BUG-LOW) | gui/kwin_focus.inl install/uninstall | `g_bus_own_name` başarılı ama `g_bus_get_sync(session)` başarısız → `org.rawaccel.Focus` oturum sonuna dek sahipsiz kalır (ikinci örnek name-owner'ı hep kaybeder); uninstall `/Focus` nesnesini `g_dbus_connection_unregister_object` etmiyordu | `✅ opencode — başarısızlık dalında `g_bus_unown_name` (+id sıfırlama); uninstall'a obj_reg_id unregister simetrisi` |
| R13-KWINARG (BUG-LOW) | gui/kwin_focus.inl focus_method_call | Handler `g_variant_get("(&s)")` öncesi tip doğrulaması yoktu (düşmanca imza UB), sender denetimi hiç yoktu — oturum veriyolundaki HERHANGİ bir app daemon'un aktif-app profilini spoof'layabiliyordu | `✅ opencode — `g_variant_is_of_type(params,(s))` kapısı + best-effort sender filtresi: GetConnectionUnixProcessID → /proc/<pid>/comm "kwin" önekli olmalı; kimlik çözülemezse eski davranış korunur (kırılma riski yok)` |
| R13-TESTS (INFO→FIX) | tests/test_accel.cpp | R12 yeni kodları başsızdı: lat_stats::reset() (raw-geçiş sıfırlaması), check_import_lut_size (514/516/515/çöp-GUI import kapısı), JSON yolunda 256/257 name cap (R12-DUPNM gerekçesi) — hepsi headless test edilebilir | `✅ opencode — 3 test grubu eklendi (+16 assert, toplam 33832/33832)` |
| R13-DOCS (BUG-LOW, docs) | AGENTS.md + cli/main.cpp help | AGENTS.md "Three jobs" diyor gerçek 4 job (build-and-test/sanitizers/fuzz-smoke/perf-gate; perf-gate hiç anlatılmıyor); test sayaçları (184/33764) bayat; `reload (SIGHUP)`/`latency (SIGUSR1)` help metni IPC-önce davranışını gizliyor; FIX_LOG üst tablo iptal edilmiş daemon.cpp satırını hâlâ aktif gösteriyor | `✅ opencode — CI bölümü 4 job + perf-gate ile güncellendi; sayaçlar 193/33832; help "(IPC, else SIGHUP/SIGUSR1)"; FIX_LOG tablo satırına ❌ İPTAL işareti` |

SKIP/KARAR: **real_polling_rate yüzeye çıkarma (GUI boyutu)** — CLI `live:` satırı eklendi; GUI cihaz etiketine taşımak yeni widget + tr kaydı açardı, değmez → KEEP (mesaj arayüzde zaten bayrak göstermiyor). **Raw inline write drop-log'u** — tek 8-baytlık yazının 32-deneme bütçesini tüketmesi pratikte imkânsız, bilinçli → KEEP. **RAW flush_motion telemetry dalı** — flush_motion raw'da provably unreachable (agent doğruladı), yorumlu ölü kod, kontrat belgesi → KEEP. **GUI import sonrası profile'e geç** (CFG-1) — CLI ile tutarlı davranış, iyileştirme tercihi → ertelendi. **setup.sh verify exit-0, udev Logitech-only, pkexec root config, pid-file 0600 (BUG-LOW-2)** — önceki turların belgeli KARAR'ları (container uyumu / güvenlik / primary-path socket), round-13 teyidi, aksiyon yok. **KWin join ~9s / "2-3s" yorumu** — bounded D-Bus timeouts + bilinçli, KEEP. Helper: CLI "latency" dump ↔ status JSON alanları uyumlu.

Doğrulama: BUILD CLEAN (warning:0) + unit 33832/33832 + ASan/UBSan clean (EXIT=0) + tr_coverage PASS (yeni tr anahtarı YOK — round-13 CLI/daemon yüzeyi) + oracle OK (1071 satır / 67 known deviation — accel headers değişmedi) + `bash -n` scripts ✓.

**Bu turda değişen dosyalar (kilit, round-14'te aynı dosyalara eşzamanlı edit YAPMAYIN):** `daemon/daemon.cpp`, `cli/main.cpp`, `gui/kwin_focus.inl`, `AGENTS.md`, `tests/test_accel.cpp`, `FIX_LOG.md`. (Round-12 kilitleri — daemon/daemon.cpp, daemon/lat_stats.hpp, src/config.cpp, include/config.hpp, gui/daemon_comm.inl, gui/kwin_focus.inl, gui/mouse_test.inl, gui/profile_mgr.inl, gui/tr.inl — serbest; daemon.cpp, kwin_focus.inl round-13 listesinde devam ediyor.)

---

## round-14 (big-pickle — 13.09.2026) — CANLI SİSTEM: IPC tamamen kilitli (ECONNRESET), staleness, e2e PID regresyonu

Kullanıcı raporu: "ayar yapılamıyor, çıkış DPI 400'de kilitli, program çalışmıyor". Canlı sisteme bakıldı (daemon v0.6.6 idi; repo HEAD 1.1.0'dı — sistem 11.09 kurulumundan beri bayattı, `ca163631`/`170c5e14` sistemde yoktu). Yüklü eski sürümün IPC'si her istemciye ECONNRESET verdiği için önce build-manual (1.1.0) yeniden kuruldu; sonra AYNI hata yeni daemon'da tekrar üredi → kodda gerçek bug.

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| R14-IPCGRL (BUG-CRIT) | daemon/daemon.cpp:3095 | SEC-1 SO_PEERCRED ek grup denetimi `getgrouplist(...) == 0` diyordu; glibc başarıda **grup sayısını** döndürür (0 ASLA), -1 = yalnız hata. Bu yüzden `input` grubunun tam üyesi olan normal kullanıcılar **her zaman reddediliyor**, bağlantı okunmadan kapatılıyordu → GUI/CLI tüm IPC çağrıları ECONNRESET/"daemon unreachable". Root (`uid==0` dalı) hep geçtiği için E2E/root testlerinde görünmüyordu | `✅ big-pickle — `>= 0` başarı denetimi (yorumlu). Doğrulama: user `ping`→`pong`, `status` canlı cihaz detayları, `set-param output_dpi 1200` daemon'a ulaştı (/etc'e kaydedildi)` |
| R14-E2EPID (BUG-LOW, tests) | tests/run_e2e.sh + tests/e2e_harness.cpp | ca163631 PID-1/3 canlılık kilidi, SIGSTOP'lu sistem daemonunun /run/rawaccel.pid'ini "canlı" sayıyor → clean-room test daemonu "Another instance may already be running" ile başlamıyor → e2e 2/2 FAIL (regresyon; ci'de root+uinput olmadığı için yakalanmadı) | `✅ big-pickle — run_e2e.sh duraklatılan daemonun kendi PID dosyalarını (eşleşen PID) test boyunca kaldırıp EXIT trap'inde geri yazıyor (3. daemonun kilidine dokunmaz); harness alt sürece özel XDG_RUNTIME_DIR (pid+soket ayrışır). e2e accel 5/5 + raw 2/2 PASS` |
| R14-STALE (ops) | /usr/bin/* | Sistemde v0.6.6 (11.09 kurulumu) çalışıyordu — `170c5e14` audit-sweep dahil tüm yeni IPC/davranış zaten sistemde yoktu; bayat ikili bu hataların görünümünü maskelemekteydi | `✅ big-pickle — build-manual (HEAD 1.1.0, warning:0) sistem ikililerine kopyalandı, servis yeniden başlatıldı; /etc config'e vm-gaming (classic, DPI 800, output_dpi 1200, raw_passthrough false) push edildi → kullanıcının "çıkış DPI 400 kilitli" durumu bitti` |

SKIP/KARAR: **config iki yerde** (daemon `/etc/rawaccel/settings.json`, GUI/CLI `~/.config/rawaccel/settings.json`) — tasarım gereği CLI/GUI kaydedince daemon'a push edilir, daemon kendi yoluna kalıcılar; push IPC'si artık çalıştığı için iki dosya push sırasında yakınsar, ayrı mekanizmaya gerek yok. Daemon, yeni save_config'in izin-koruma yoluyla /etc dosyasını 0600 yaptı (geçici; chmod 0644 ile geri alındı). E2E child'ı XDG_RUNTIME_DIR taşıdığından /run kilidine hiç dokunmadı.

Doğrulama: BUILD CLEAN (warning:0) + unit 33832/33832 + tr_coverage PASS + oracle OK (1071 / 67 known deviation) + e2e accel 5/5, raw 2/2 PASS + canlı sistem IPC uçtan uca (user ping/status/set-param).

**Değişen dosyalar:** `daemon/daemon.cpp`, `tests/run_e2e.sh`, `tests/e2e_harness.cpp`, `FIX_LOG.md`. (Round-13 kilitleri — daemon.cpp, cli/main.cpp, kwin_focus.inl, AGENTS.md, test_accel.cpp — serbest; daemon.cpp round-14'e girişiyle yeniden kilitli, round-15'te aynı dosyaya edit GEREKİRSE önce haberleşin.)
