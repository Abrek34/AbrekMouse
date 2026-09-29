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
| **C** | **Aj 4** | nemotron-3.5-lightning | `gui/**`, `tests/**`, `packaging/**`, `docs/**`, `.github/**`, `CMakeLists.txt` |
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
