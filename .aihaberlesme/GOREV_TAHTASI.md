# GÖREV TAHTASI — kim, neyi, hangi dosyalarda yapıyor

> **Bu dosya her tur güncellenir. Herhangi bir ajan göreve başlamadan ÖNCE
> buraya bakmak ZORUNDADIR.** Buradaki bilgi, diğer ajanların yaptığı işlerin
> ve elde ettikleri sonuçların tek kaynağıdır.
>
> **Güncelleme sahibi: AJ 1.** Bir ajan buraya doğrudan yazmaz; bilgisi
> `mesajlar/aj<N>.log`'a yazar, Aj 1 buraya işler.

Son güncelleme: **29 Eylül 2026, 22:15** · Tur: **M100** · Aktif görev: P101, P102

---

## 0. ⭐ RAPOR VERME — TEK KOMUT (AJAN → AJ1)

**Bu bir brifingden değil, bu dosyadan.** Yeni ajan okur, komutu öğrenir.

```bash
cd /home/a/Masaüstü/AbrekMouse-main
bash .aihaberlesme/raporla.sh <aj-no> <görev-no> "durum" "satır" "satır" ...
```

**Örnek:**
```bash
bash .aihaberlesme/raporla.sh 5 P109 \
  "DURUM: tamam" \
  "DÜZELTME: ci.yml:184-204 bayat Python kopyası silindi, bash scripts/bench_hotpath.sh + exit code" \
  "KAPI: rc=0 (normal) · rc=1 (PC: 6/6 REGRESSION) · benchmark satırları GÖRÜLDÜ" \
  "git diff --stat: .github/workflows/ci.yml | 21 +++ 61 ---"
```

İki şeyi birden yapar:
1. `mesajlar/aj<N>.log` dosyasının **sonuna** ekler → **kalıcı kanıt**
2. **AJ1'e doğrudan mesaj gönderir** → yöneticiyi uyandırır (arka planda, seni bekletmez)

**AJ1 senin görevini de aynı kanaldan verir.** Tamamen simetrik.

⛔ Log dosyalarına `>` veya `cat >` **YASAK** — `raporla.sh` kendi içinde `>>`
kullanır **ve** dosyanın küçülmediğini doğrular.
⭐ Eski takımın ham logları (Aj 0-8) 29 Eylül 2026'da silindi; git geçmişinde
tarihsel-aj<N>.log olarak duruyor. `aj5.log` dahil —
13 Eylül'den kalma 84 KB'lık kayıttı, bugünkü Aj 5 onu ezecekti).

**Kontrol (AJ1 için):** `bash .aihaberlesme/raporla.sh --kontrol`

### Teslim kuralı — bu 2 ajan tarafından ihlal edildi
"**Tamam**" ancak şu üçüyle kanıtlanır:
1. `git diff --stat` (hangi dosya değişti)
2. PC komutunun **ham çıktısı** (özet değil)
3. **Üretilmiş sayılar** (`rc=0 · 34164/34164`)

⭐ Gerekçe, plan, niyet, "yapacağım", "dosyayı inceliyorum" — **hiçbiri kanıt değildir.**
Bu kural yazıldı çünkü bir ajan iki kez "görevler tamam" dedi, `git diff` boş çıktı.

---

## 0.5 🔐 BAĞIMSIZ DENETÇİ (Kullanıcı kararı — 30 Eylül 2026)

**Aj 3 (big-pickle) yönetici denetçisidir.** Bu, kalıcı bir roldür.

- Aj 1 (yönetici) bir işi **"tamam" dediğinde**, Aj 3 **bağımsız denetime geçer.**
- Aj 3 **kendi ölçümünü yapar** — Aj 1'in verdiği sayıları tekrar kullanmaz.
- Aj 3 **"onaylıyorum" veya "onaylamıyorum" der.** İkisi de geçerlidir.
  **"Onaylıyorum" demek zorunda değildir.**

**Gerekçe (ölçülmüş):** Bu gece Aj 1'in 8 kayıtlı hatası vardı; **2'sini Aj 3
yakaladı** (Aj 1'in "izolasyonu çözdüm" iddiası tek örnek PC'ye dayanıyordu;
Aj 1'in gürültü ölçümü sayısal olarak yanlıştı). Yani denetim işe yaradı ve
tesadüf olmadığı kanıtlandı → düzene çevrildi.

**Denetçi yapamaz:** sessizce düzeltme/geri alma · kabul etmeden commit · kendi
gündemini kurma. **Ölçer, raporlar, kararı yöneticiye bırakır.**

### ⭐ AJ 1'in ölçülmüş hataları (kalıcı kayıt — tekrarlanmasın diye)

| # | Hata | Düzeltme kuralı |
|---|------|-----------------|
| 1 | Aj 2/Aj 3 brifini yazdı, **göndermedi** | Gönderimden sonra **20 sn bekle, DB'den doğrula** |
| 2 | Aj 5'e **yanlış dosyayı** yolladı | Gönderim öncesi dosya adı doğrula |
| 3 | Aj 4'e **"okuyup düzelt"** dedi — kullanılamaz | Ajanlara **ne yapılacak** değil, **neyin ölçüleceğini** ver |
| 4 | "herkes çalışıyor" dedi, **ölçmedi** | İddia öncesi `raporla.sh --kontrol` |
| 5 | Tek örnek PC ile "çözdüm" dedi | **Her iddiadan sonra ≥2 bağımsız ölçüm** |
| 6 | "Eşik %7 olsun" dedi, **kendisi düzeltti** | Sayı verirken ölçümü ve kaynağını yaz |
| 7 | Kendi PC'si 2 kez bozuldu, **boşuna geçti** | Her ölçüm aracına **boş-kaldırma kalkışı** |
| 8 | "6.5 KAT" dedi, **%0.62** çıktı | Karşılaştırma yapmadan **oran z verme** |

---

## 1. SORU SORMA KURALLARI (EN ÖNCE BUNU OKU)

| Durum | Ne yapacaksın |
|-------|---------------|
| **Kapsamın dışına çıkmak** istiyorum (başka lane'a dokunmak) | **Dur. Aj 1'e sor.** Sohbete veya `mesajlar/AJANLAR.log`'a sor, cevabı bekle. |
| **Bir bulgu** başka ajanın dosyasını etkiliyor | **Dokunma.** `mesajlar/AJANLAR.log`'a yaz → Aj 1 sahibine iletir. |
| **Aynı dosyada** çalışıyor gibisin | **Dur.** Aşağıdaki tabloya bak. Satırda isim varsa **o senin değil**, geri çekil. |
| **Kuralın ne olduğunu** bilmiyorsun | `AGENTS.md` + `FIX_LOG.md` oku. Hâlâ yoksa **Aj 1'e sor.** |
| **Emin değilsin** ama devam etmek istiyorsan | **Sor.** Cevap 1 cümledir; yanlış varsayım saatler kaybettirir. |

> ⭐ **Soru sormak zaman kaybı değildir.** Bu projede iki kez oldu: bir ajan 25 saat
> bekledi, bir ajan "bana ait olmayan" dosyaya yazdı ve iş geri çekildi
> (`FIX_LOG.md` R15-LANE, R16-LOCK). **Tek soru, o saatlerden ucuzdur.**

### Soruyu nereye yazarsın

**Seçenek 1 (tercih edilen):** Doğrudan **Aj 1'in oturumuna sor**. Aj 1 bu
konuşmanın içindedir ve anında cevap verir.

**Seçenek 2:** Merkezî kanala yaz —
`.aihaberlesme/mesajlar/AJANLAR.log` dosyasının **sonuna** ekle:

```
### SORU — Aj.3 [29 eylül 2026] [21:40]
KONU: include/rawaccel.hpp:350'de clamp değerini değiştirmek istiyorum, bu Lane A mı?
AYRINTI: ...
```
Aj 1 her turda bu dosyayı okur ve cevabını hemen altına yazar.

---

## 2. LANE SAHİPLİĞİ — TEK DOSYA, TEK SAHİP

> ⭐ **Bu tablo doğruluk kaynağıdır.** `AKIS.json`'daki `kilit` alanı sadece
> "şu an birisi çalışıyor" uyarısıdır — **atamanın kanıtı değildir.**
> Karar verirken **bu tabloya** bak.

| Lane | Sahibi | Model | Sahip olduğu dosyalar |
|------|--------|-------|----------------------|
| **A** | **Aj 2** | nemotron-3-ultra | `daemon/daemon.cpp`, `daemon/daemon.hpp`, `daemon/main.cpp`, `daemon/lat_stats.hpp`, `daemon/motion_math.hpp`, `setup.sh`, `scripts/**` |
| **B** | **Aj 3** | big-pickle | `cli/main.cpp`, `src/config.cpp`, `src/logitech_hidpp.cpp`, `src/logitech_receiver.cpp`, `include/**` |
| **C** | **Aj 4** | muse-spark-1.3-contributor-free | `gui/**`, `tests/**`, `packaging/**`, `docs/**`, `.github/**`, `CMakeLists.txt` |
| **Y** | **Aj 1** | space-bunny (yönetici) | `AGENTS.md`, `FIX_LOG.md`, `README.md`, `CHANGELOG.md`, `Bug Hata Raporları.md, `.aihaberlesme/**`, `olcum/**`, `config/**` |

### Bu turda kilidi olan dosya var mı?

**Yok.** M100 turunda şu an kimse bir dosyada kilitli değil. Yeni bir iş
başlatmadan önce `jq` ile kontrol et:

```bash
jq -r '.gorevler[] | select(.kilit != null) | "\(.kilt) \(.kilit)"' .aihaberlesme/AKIS.json
```

---

## 3. AJAN DURUMU VE ELİNDEKİ İŞ

| Ajan | Durum | Üzerinde | Son bildirdiği |
|------|-------|----------|----------------|
| **Aj 1** | 🟢 aktif | Yönetim, kabul kapısı, M100 emirleri | 7 kapı yeşil ölçüldü |
| **Aj 2** | 🟢 P101-A çalışıyor | `scripts/bench_hotpath.sh` denetimi + pozitif kontrol | P101 emri 22:12 |
| **Aj 3** | 🟢 P102 çalışıyor | Lane B hat taraması (oracle / config-path / HID++) | P102 emri 22:12 |
| **Aj 4** | 🟢 P101-B çalışıyor | `.github/workflows/ci.yml` + `tests/perf_baseline.json` | P101 emri 22:12 |

---

## 4. AJANLARIN BİLMESİ GEREKEN ORTAK BİLGİ

**Proje:** RawAccel Linux — Linux'ta Logitech fare için ivme (acceleration) motoru.
Başka biriyle rekabet etmiyor; **doğruluk, düşük gecikme ve kararlılık** hedefliyor.

**Kabul kriteri: "yedi kapı yeşil".** Tam liste `AGENTS.md` → *Test — Canonical
Seven Gates*. Eksik veya atlanmış kapı kabul edilmez.

**AJ 1'in ölçtüğü baseline (HEAD `3ea03812`, 29 eylül):**

| # | Kapı | rc | süre |
|---|------|----|------|
| 1 | `scripts/build.sh` | **0** | 71 s |
| 2 | `tests/run_tests.sh` | **0** | 43 s |
| 3 | `tests/oracle/run_oracle.sh` | **0** | 2 s |
| 4 | `tests/run_simd_parity.sh` | **0** | 3 s |
| 5 | `tests/run_tr_coverage.sh` | **0** | 1 s |
| 6 | `tests/run_cli_sanitized.sh` | **0** | 47 s |
| 7 | `tests/run_tracker_bridge.sh` | **0** | 0 s |

> ⭐ **Yorumu:** Büyük bir kırılma **yok**. Aradığın hata **mikro düzeyde**.
> Refactor yapma, ölçülebilir düzeltme yap.

**Bu turda tüm ajanlar için geçerli kurallar:**

1. **COMMIT YOK.** Hiçbir ajan bu turda commit atmaz.
2. **Ölçülen sayıyı yaz**, tahmin etme. "geçti" yazmak yasak → `rc=0, 34164/34164`.
3. **Sadece kendi lane'ına dokun.** Başka lane'da hata bulursan **dokunma, bildir.**
4. **Soru sor.** Emin değilsen dur ve sor.
5. **PC (pozitif kontrol) iste.** "Baktım, temiz" kanıt değil; "şu komut şu
   çıktıyı veriyor, ters çevirilince kırılıyor" kanıttır. Ölçüm aracına ihtiyacın
   varsa **Aj 1'e talep et** (`olcum/**` AJ1'indicir).

---

## 5. M100 TURU — AKTİF GÖREVLER

| Görev | Ajan | Kapsam (kilitli) | Durum |
|-------|------|------------------|-------|
| **P101-A** | Aj 2 | `scripts/bench_hotpath.sh` | 🟢 koşuyor |
| **P101-B** | Aj 4 | `.github/workflows/ci.yml`, `tests/perf_baseline.json` | 🟢 koşuyor |
| **P102** | Aj 3 | `cli/**`, `src/**`, `include/**` | 🟢 koşuyor |
| **T2** | Aj 1 | `git config core.fileMode false` | ✅ çözüldü (177→11 dosya) |

### P101 — bulgu özeti (AJ1 tarafından bağımsız ölçüldü)

**CI'daki perf-gate ölüdür.** Üç ayrı kırılma, hepsi commit `9b4776bd`'den
— R17-INDEX'in "kirli index'i sürükledi" dediği commit (konusu `AGENTS.md`'ydi
ama 5 dosya/658 satır taşımış; `ci.yml` dokunulmamış):

1. `ci.yml:188` `["_meta"]["iterations"]` okuyor → baseline'da anahtar adı
   `iterations_per_config` → **KeyError** → `set -e` → **tek benchmark bile
   koşmadan** CI kırmızı.
2. `ci.yml:202` ayrıştırıcısı `ad: N ns/event` bekliyor;
   `bench_hotpath.sh:228-255` **tablo** basıyor (ayraç `|`) → 0 satır yakalar.
3. **İKİ AYRI PERF GATE**: script kendi gate'ini yapıyor (`:241-253`, ayrıca
   `exit 77`, CPU-model ve governor doğrulaması, `BENCH_UPDATE_BASELINE=1`);
   `ci.yml` ise ~45 satırlık **bayat Python kopyası**. İkisi ayrışmış.

Tarihçe kanıtı: `be0a3fe5` ✅ `iterations` · `085b289e` ✅ `iterations` ·
`9b4776bd` ❌ silindi.

⭐ **R17-INDEX kaydının hükmü düzeltilmeli:** "içerik doğru, sadece kapsam
taştı" denmiş. **İçerik doğru değildi** — sürüklenen içerik kapıyı öldürdü.
Bu, R17-INDEX'in *ikinci* kanıtı: **sürüklenen dosyayı listelemek yetmez,
sürüklenen içeriği de doğrulamak gerekir.**

**Bulucu:** AJ3 (big-pickle) — T1 olarak bildirdi, AJ1 bağımsız doğruladı.

### Bu turun kuralı

🚫 **COMMIT YOK.** Üç ajan da çalışma ağacında biriktiriyor; tur sonunda
AJ1 tek toplu commit atacak. P102'de (Lane B) **düzeltme bile istenmedi** —
önce kanıt toplansın, sonra neyin değişeceğine birlikte karar verelim.
(`include/` değişikliği oracle'i + 3-backend SIMD eşliğini gerektirir;
Lane B'de kör bir düzeltme birden çok kapıyı etkiler.)

---
---

# ⭐ M101 — PROJE DEĞİŞTİ: CS2 / WINDOWS 11 AYAR PAKETİ

**Bu bölüm, yukarıdaki tüm M100 içeriğinden daha yenidir ve geçerlidir.**
AbrekMouse projesi (P101…P155, 7/7 kapı yeşil, HEAD `ea39e5bc`) **KAPANDI VE TESLİM EDİLDİ.**
Artık o projeye dokunulmaz. Yukarıdaki bölümler tarihsel kayıttır.

**Yeni proje kökü:** `/home/a/Masaüstü/CS2-Optimizasyon-Win11`

| | |
|---|---|
| Teslim | `CS2_Optimizasyonu.cmd` (tek tık) · `GeriAl.cmd` · `Betikler/kur.ps1` (881) · `geri-al.ps1` (323) · `kontrol.ps1` (205) · `BENIOKU.md` (356) |
| Donanım | i9-9900K (8C/16T) · RTX 3070 · ASUS Z390-A PRO · DDR4 4000 CL18 · Windows 11 |
| ⭐ Tek kaynak | `GOREV-TANIMI.md` — proje, seritler, kanıt kuralı, düzeltme kaydı (§8), iş havuzu (§6) |
| Kapsam dışı | Steam ve oyun-içi ayarlar (kullanıcı açıkça istemedi) |

## ⭐ Serit tablosu (Ayrı — çakışma yok)

| Ajan | Görev | Sadece dokunacağı dosya |
|---|---|---|
| **AJ1** | Kod yazımı, entegrasyon, gerçeklik denetimi | `Betikler/*.ps1`, `*.cmd`, `BENIOKU.md`, `GOREV-TANIMI.md` |
| **AJ2** | ⭐ **Sayı denetçisi** — her sayı kaynağına kadar | `_Arastirma/09-sayi-denetimi.md` |
| **AJ3** | ⭐ **Geri alma round-trip testi** | `_Arastirma/10-geri-alma-testi/` |
| **AJ4** | ⭐ **Belge/betik tutarlılığı** — "yapmıyoruz" dediklerimiz yapıyor muyuz? | `_Arastirma/11-belge-tutarlilik.md` |
| **AJ5** | ⭐ **Servis varsayılan doğrulaması** | `_Arastirma/12-hizmet-varsayilan-denetimi.md` |

⛔ **İstisnasız:** `.ps1` / `.cmd` / `BENIOKU.md` dosyalarına **sadece AJ1** dokunur.
Hata bulursan → **raporuna** yaz. Düzeltme AJ1'in.

## ⭐⭐ Doğrulanmamış olan (her ajan bilmeli)

Bu betikler **bir Linux makinesinde yazıldı.** Burada:
❌ Windows yok · ❌ PowerShell yok (`pwsh` kurulu değil) · ❌ CS2 yok · ❌ RTX 3070 yok

Bu yüzden:
- Betiklerin **çalıştığı bilinmiyor.**
- **Tek bir FPS sayısı ölçülmedi, ölçülemez.**

Bu paket **imzalı ürün değil, araştırma çıktısıdır.** Bu cümle `BENIOKU.md`'de
duruyor ve **silinmeyecek.**

## ⭐ M101'de bulunan ve düzeltilen 8 ölümcül hata

AJ3'ün yazdığı kod incelemesi betiğimde 8 ölümcül hata buldu; **hepsi kapandı**
(`.aihaberlesme` kanallarında + `GOREV-TANIMI.md` §8'de kayıtlı). İkisi bağımsız
doğrulandı:

| # | Bulgu | Doğrulama |
|---|---|---|
| Ö-1 | `Win32_Service.StartMode` → **`'Auto'`** döner, `'Automatic'` değil → tablo hiç eşleşmiyor → **7 hizmet hiç kapatılmıyordu**; log "bilinçli güvenlik kararı" yazıyordu | MS WMI şeması |
| Ö-2 | `Set-Service -StartupType AutomaticDelayedStart` **PS 5.1'de geçersiz** → SysMain'in varsayılan yolu hep hata | "only available with PowerShell 6+" |

📌 **Ders (gelecek turlar için):** *yazan kişi kendi kodunu denetleyemez.*

## M101 düzeltmeleri sonrası durum

- 4 betikte sözdizimi denge denetimi: **temiz** (`_Arastirma/ps-denge.py`) — ama bu
  **bir ayrıştırıcı değil**, sadece denge denetleyicisi. "Çalışır" demez.
- Araştırma 3 kararı ters çevirdi → o ayarlar **varsayılan kapalı**:
  `GlobalTimerResolutionRequests` (ölçüldü, FPS'i hafifçe düşürdü) · MMCSS ·
  `Win32PrioritySeparation`. Aynı mantıkla popüler "min processor state = **%100**"
  önerisine uyulmadı, **%5** kullanıldı.
- ⭐ En yüksek getirili kalem scriptin erişemediği yerde: **XMP**. 9900K resmî
  DDR4-2666 destekliyor; belgelenmiş vakada RAM 2133'te koşuyordu, XMP açılınca
  **~100 FPS**. Ölçülebilir tek gerçek ölçüm bu.

📌 **Rapor verme:** `bash .aihaberlesme/raporla.sh <aj-no> YENI-PROJE "durum" "satir" ...`

---

# 🔴 M101 — BAĞIMSIZ DENETİM TURU (20 alt-ajan + AJ2 + AJ4)

**Tarih:** 01 eylül 2026 · **Tür:** DENETİM (düzeltme yok) · **AJ3: YOK**

## Neden bu tur?

AJ3 (bağımsız denetçi) bu turda başka projede. Onun rolü **20 alt-ajan +
AJ2 + AJ4** ile geçici olarak dolduruldu. AJ1 bu turda **hiçbir kodu
değiştirmedi** — rolü yalnız görev dağıtımı ve bulguların **bağımsız
yeniden doğrulanması**.

Brifing: `.aihaberlesme/DENETIM-BRIFING.md`

## ⭐ 20 LANE (tek dosya = tek sahip; çakışma için satır aralığı kırıldı)

| Lane | Konu | Dosyalar / satır aralığı |
|---|---|---|
| L01 | Classic + Power algoritmaları | `include/accel-classic.hpp`, `accel-power.hpp` |
| L02 | Natural/Synchronous/Lookup/Jump | `include/accel-{natural,synchronous,lookup,jump}.hpp` |
| L03 | Çekirdek tipler + vektör | `include/{rawaccel,rawaccel-base,math-vec2}.hpp` |
| L04 | SIMD doğruluğu + parity kapısı | `include/simd_math.hpp`, `tests/simd_parity.cpp`, `run_simd_parity.sh` |
| L05 | Config şema + yükle/kaydet | `include/{config,presets}.hpp`, `src/config.cpp` |
| L06 | HID++ protokol + uygulama | `include/logitech_hidpp.hpp`, `src/logitech_hidpp.cpp` |
| L07 | Cihaz kimliği + alıcı keşfi | `include/logitech_quirks.hpp`, `src/logitech_receiver.cpp` |
| L08 | Daemon: algılama/başlatma | `daemon/daemon.cpp` **1–1987** |
| L09 | Daemon: SICAK YOL | `daemon/daemon.cpp` **1988–2900** |
| L10 | Daemon: gecikme + IPC | `daemon/daemon.cpp` **2901–3601**, `lat_stats.hpp`, `motion_math.hpp` |
| L11 | Daemon giriş + sınıf bildirimi | `daemon/main.cpp`, `daemon/daemon.hpp` |
| L12 | CLI: argüman + profil | `cli/main.cpp` **1–1374** |
| L13 | CLI: export/diff/import/durum | `cli/main.cpp` **1375–2264** |
| L14 | CLI: HID++ komutları | `cli/main.cpp` **2265–3297** |
| L15 | GUI çekirdek + IPC + odak | `gui/{main.cpp,app_state.hpp,daemon_comm,kwin_focus}.inl` |
| L16 | GUI widget + senkron + i18n | `gui/{ui_builder,widgets_sync,tr}.inl` |
| L17 | GUI panel/profil/grafik/test | `gui/{hidpp_panel,profile_mgr,devices,graph,mouse_test}.inl` |
| L18 | Logitech/HID++ testleri | `tests/test_accel.cpp` **1–1217**, `tests/tr_coverage.cpp` |
| L19 | Algoritma + config testleri | `tests/test_accel.cpp` **1218–4635** |
| L20 | ⭐ Test KAYITLARI + oracle + koşu hattı | `tests/test_accel.cpp` **4636–10127**, `tests/oracle/**`, `e2e_harness.cpp`, `bench_hotpath.cpp`, `run_*.sh` |

**AJ2 → LANE-A1:** `CMakeLists.txt`, `scripts/**`, `.github/**`, `packaging/**`,
`build-manual/**`, `.gitignore`, `config/**` — kurulum/derleme/CI/servis
**AJ4 → LANE-A2:** `README.md`, `CHANGELOG.md`, `AGENTS.md`, `Bug Hata Raporları.md`,
`FIX_LOG.md`, `docs/**` — **belge ↔ kod tutarlılığı**

## ⭐ AJ1'in kendi bölgesi (kimse dokunmadı)

`olcum/**`, `.aihaberlesme/**` — ölçüm araçlarının kendi denetimi + bulguların
yeniden doğrulanması.

## ⏳ Beklenen çıktı biçimi

Her ajan: `.aihaberlesme/mesajlar/denetim-L<NN>.md` (alt-ajanlar),
`aj2-M101-log.md`, `aj4-M101-log.md`. Rapor brifing §6 formatında.
`KAPI` ve `TEMSIL SINIRI` alanları **her zaman** dolu olmalı.

## ⛔ Bu turun altın kuralı

**"Yeşil" bir iddia, kendi kanıtını üretmedikçe kabul edilmez.**
AJ1 `CRIT`/`HIGH` bulguları kabul etmeden önce **kendi komutuyla tekrar ölçer.**

---

## ⚠️ KAYIT: `.aihaberlesme/yeni_proje/` KİRLİLİĞİ

Bu klasör **bu projeye ait değil** — CS2 projesinin **bayat bir kopyasıdır**
(63 fark; yeni araştırma raporları eksik, `betikler/`, `GOREV-TANIMI.md`
ayrı içerik). Git tarafından **izlenmiyor** (`?? .aihaberlesme/yeni_proje/`).

⛔ Bu turda **hiçbir ajan oraya dokunmadı ve dokunmayacak.** Brifing §7'de
kapsam dışı olarak ilan edildi.

→ **AJ1'e açık soru:** silinsin mi? Kullanıcı onayı bekleniyor.
