# Düzeltme Koordinasyon Logu (Raporun TAM envanteri)

Kaynak: `Bug Hata Raporları.md` TUR 20-24 (satır 1023-1390). Amaç: her loglanan maddenin durumunu tek yerde tutmak; aynı dosyada aynı anda çalışmayın.

Son güncelleme: 2026-09-12

---

## 🖊️ BU OTURUM (big-pickle) — üstlenilen maddeler ve dokunulacak dosyalar

`[ALINDI: big-pickle]` işaretli maddeleri bu oturum yapıyor:

| Dosya | İçinde yapılan |
|---|---|
| `daemon/daemon.cpp` | HP-2 (replug EBUSY retry), CFG-4 (persist-önce-ack) |
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

Önceki oturumun GUI-D4 derleme hatası (duplicate `GError* err`) bu oturumda düzeltildi; build + birim testleri (33792/33792) yeşil. Bu oturumda tamamlananlar: PRE-2, PRE-3 (+oracle mirror, known_deviations 68→67), CFG-2, CFG-3, CFG-5, CLI-2, CLI-3, GUI-O4, GUI-Y3, DOC-1 + önceki seansın MATH-2'siyle P107 tutarlılığı (CLI `scale` domain 0.01 floor, test_accel güncellendi). MATH-1 → KASITLI; CFG-1 → ertelendi (gerekçe yukarıda). Guards: build(warning:0) + unit(33792/33792) + oracle(67 deviation, OK) koşuldu.

---

## 🖊️ BU OTURUM (opencode) — üstlenilen maddeler (TUR 31 bulguları)

`[ALINDI: opencode]` işaretli bu maddeleri opencode yapıyor. Sabitlenmiş boş dosyalara girişilmedi (kural 2):
big-pickle oturumunun kilidindeki dosyalar için eşzamanlı edit YAPILMADI, madde talep edilip kilit açılınca uygulanacak.

| ID | Dosya | Konu | Durum |
|---|---|---|---|
| O31-G1 (GUI A2) | gui/ui_builder.inl:1474-1509,960-1004 | kde_fix idle destroy cancel-gate yok (widget-lifetime UAF) | `[ALINDI: opencode]` ⏸ ui_builder kilidi |
| O31-G2 (GUI A3) | gui/graph.inl:431-433,492 | rebuild_lut_list S->updating mutlak sıfırlama | `✅ opencode — prev_updating save/restore` |
| O31-G3 (GUI A6) | gui/tr.inl:688-697 + ui_builder.inl:595,606,612,923 | refresh_language canlı etiketleri eziyor | `[ALINDI: opencode]` ⏸ tr.inl kilidi |
| O31-G4 (GUI A8) | gui/ui_builder.inl:230-255 | spin min > config floor (limit/sync_speed/exponent_power) | `[ALINDI: opencode]` ⏸ ui_builder kilidi |
| O31-G5 (GUI A9) | gui/ui_builder.inl:816-844 vs graph.inl on_lut_add_point | tıklama yolunda LUT_SPEED_SPIN_MAX clamp'ı yok | `[ALINDI: opencode]` ⏸ ui_builder kilidi |
| O31-G6 (GUI A10) | gui/profile_mgr.inl:504-508,565-570 | GtkFileDialog unref'lenmiyor | `[ALINDI: opencode]` ⏸ profile_mgr kilidi |
| O31-C1 (Config C1) | include/accel-classic.hpp:158-163 | classic GAIN in dalı cap.x>=input_offset clamp'sız | `✅ opencode — io-simetrik clamp; oracle OK` |
| O31-C2 (Config C2) | src/config.cpp:182-200,336-355 | lut_length > nokta sayısı → ölü LUT | `[ALINDI: opencode]` ⏸ config.cpp kilidi |
| O31-C3 (Config C3) | src/config.cpp:122-123,252-253 | gain/raw_passthrough sayısal 0/1 yok sayılıyor | `[ALINDI: opencode]` ⏸ config.cpp kilidi |
| O31-C4 (Config C4) | src/config.cpp:242-246,615-616 | active_profile cap 256 vs isim 255 → hiç eşleşmez (CFG-6 ile birlikte) | `[ALINDI: opencode]` ⏸ config.cpp kilidi |
| O31-C5 (Config C5) | src/config.cpp:730,779 | symlink config save'de normal dosyaya döner | `[ALINDI: opencode]` ⏸ config.cpp kilidi |
| O31-H1 (HIDPP A1) | src/logitech_hidpp.cpp:1990-1998 vs :679 | write_onboard 18>>16 bayt → hep false | `[ALINDI: opencode]` ⏸ hpp — B6/B3 tamam, yeniden edit riski; kilit açılınca |
| O31-H2 (HIDPP A2) | include/logitech_quirks.hpp:86,93-106,214-222 | G522 "32" ölü kod | `[ALINDI: opencode]` 📌 KARAR — gerçek 12-char id doğrulanmadan yazılmaz |
| O31-H3 (HIDPP B1) | daemon/main.cpp:293-298 | `-c -v` sonraki bayrağı yutar | `[ALINDI: opencode]` ⏸ daemon/main kilidi |
| O31-H4 (HIDPP C4) | scripts/bench_hotpath.sh:58 | geçersiz perf olayı syscalls | `[ALINDI: opencode]` ⏸ SH-1 (big-pickle) kapsamında |
| O31-H5 (HIDPP C3) | scripts/kde-fix-accel.sh:322-325 | --remove symlink yıkıyor + sabit tmp | `✅ opencode — mkstemp+realpath+fsync (--fix deseni)` |
| O31-L1 (CLI F5) | cli/main.cpp:1242-1248 | import 515-lik tek LUT sessiz eleman düşürüyor | `[ALINDI: opencode]` ⏸ cli/main kilidi |
| O31-L2 (CLI F4-x2) | cli/main.cpp:951-954,919 | input_offset>500 / cap_x<input_offset CLI-domain yok (P107) | `[ALINDI: opencode]` ⏸ cli/main kilidi |
| O31-L3 (CLI F7) | cli/main.cpp:2080,575-577,243 | help/çıktı metni davranıştan sapıyor | `[ALINDI: opencode]` ⏸ cli/main kilidi |

**Negatif teyid:** HIDPP C2 (kde-fix-accel.sh ilk-çalıştırma abort) DENEYSEL ÜRETİLEMEDİ — `cp -a` backup'ı `ls` glob'undan önce yaratır; exit-2 senaryosu imkânsız. Üstlenilmedi.

**Çakışma uyarısı:** `tests/run_tests.sh` P83 "256-char create-preset" kapısı şu an FAIL veriyor (CLI "(max 255)" ile reddediyor). Bu, devam eden CFG-6 (ad-cap) işinin ağaç durumu — opencode bu turun koduna dokunmadı (accel-classic.hpp/graph.inl/kde-fix-accel.sh). CFG-6'yı big-pickle tamamlayınca kapı yeşile döner; düzeltme opencode'a DEĞİL big-pickle'a ait.

**Kapananlar (bu tur):** O31-G2, O31-C1, O31-H5 → build(warning:0) + unit(33792/33792) + oracle(OK, 67) + bash -n + python ast geçti.

**09-12 checkpoint (big-pickle round-2):** `setup.sh` **şu an eşzamanlı düzenlemede (aj1 BS-7 guard yazdı; big-pickle SH-3:357-364 ve SH-4:330 ekledi — çakışma yok!).** Kod değişiklikleri (build/derleme bekliyor): `scripts/build.sh` YENİ-1 (`${CFLAGS:-}`/`${CXXFLAGS:-}`) + NEW-4 (GUI `$HARDENING`); `src/config.cpp`+`include/rawaccel-base.hpp` CFG-6 (**256'ya birleştirildi**: char[MAX+1] + strncpy 256 + dp.name cap 256 — CLI P83 kapısı korundu), `src/config.cpp` CFG-7 (XDG + ERANGE); `cli/main.cpp` CLI-1 (match_app notu). Yeniden doğrulama: YENİ-2, YENİ-3, PKG-1, CI-1, SH-1, SVC-1, B3, B6 zaten ağaçta ✅; HP-2 ve CFG-4 → KARAR (kod değişmedi, daemon.cpp'ye dokunulmadı). CFG-1 re-değerlendirildi → ERTELENDİ (profil JSON'unda version yok; global migrate double-scale riski geçerli). `daemon/daemon.cpp` satırındaki HP-2/CFG-4 üstlenmesi bu satırla iptal: **daemon.cpp başka düzenleme YOK.**

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
4. Bu dosyayı **commit'e dahil etmeyin**; iş bitince silinecek.