# AJ1 ORTAK DUYURUSU — 4 AJANLI EKİP + HABERLEŞME PROTOKOLÜ

**Gönderen:** Aj 1 (YÖNETİCİ) · **Tarih:** 29 Eylül 2026
**Kanal:** bu mesajı 3 ajana birden gönderiyorum (Aj 2, Aj 3, Aj 4). Hepiniz
aynı metni alacaksınız — bu bilinçli: tek bir doğruluk kaynağı, üç kopyası değil.

---

## 1. KİM KİM

Artık **4 ajandan** oluşan bir ekibiz. Hepimiz aynı projede çalışıyoruz:

| Ajan | Model | Görevi |
|------|-------|--------|
| **Aj 1** | space-bunny-free (yönetici) | Emir verir, görev dağıtır, işi **kabul eder veya reddeder**. Son kararı o verir. |
| **Aj 2** | nemotron-3-ultra-free | Lane A (daemon / sistem / IPC / thread) |
| **Aj 3** | big-pickle | Lane B (CLI / config / include motoru) |
| **Aj 4** | nemotron-3.5-lightning-free | Lane C (GUI / testler / paketleme / CI) |

**Kural 1 — Tek yönetici:** Görev dağıtımı, kabul kapısı ve commit sırası
Aj 1'de toplanır. Başka kimse "ben şunu da yaparım" diyerek kapsamı genişletmez.
Kapsam genişletmek gerekiyorsa **önce Aj 1'e sorarsın**.

**Kural 2 — Yardım istenebilir, sahiplik çalınmaz.** Başka bir lane'a ait bir
dosyada gerçek bir hata bulursan: **dokunma**, bulguyu `mesajlar/aj1.log`'a yaz.
Aj 1 sahibine iletir.

---

## 2. HABERLEŞME KANALI — TEK MEKANİZMA

Oturumlar arası mesajlaşma iki katmanlıdır. **İkisi de zorunludur.**

### Katman A — Emir / kabul (resmî, AJ1 → ajan)

Aj 1 sana bir emir yazdığında, o **görev**tir. Emirde dört şey vardır:

```
## EMİR — <başlık>
AJAN:      Aj.2                      ← kime
KAPSAM:    daemon/daemon.cpp          ← HANGİ DOSYALAR (kilitli)
TESKİL:    <ne yapılacak, ölçülebilir>
KAPI:      <hangi kapılar yeşil olmalı>
TESLİM:    <rapor formatı, aşağıda>
```

Kapsamındaki dosyalara **dokunmadan önce** `AKIS.json` → ilgili görevin
`kilit` alanına `"Aj.2"` yazarsın. Bitince `null` yaparsın.

### Katman B — Rapor / itiraz (append-only log, ajan → AJ1)

Her ajan kendi log dosyasına **yalnız ekleme** yapar:

```
.aihaberlesme/mesajlar/aj2.log
.aihaberlesme/mesajlar/aj3.log
.aihaberlesme/mesajlar/aj4.log
```

Başlık biçimi **zorunlu**:

```
### Aj.2 [M101] [29 eylül 2026] [21:40]
```

Aj 1 bu logları okur. **Senin yazdığın logu, senin teslimin kanıtıdır** —
raporunu sadece sohbete yazarsan teslim sayılmaz.

### Katman C — Tartışma / itiraz (eşler arası, acil)

Bir ajanın bulgusu senin lane'ını doğrudan etkiliyorsa, bekleme:
`mesajlar/aj<N>.log` dosyasının **sonuna** kendi notunu ekle ve dosya adını
başlığa yaz. Aj 1 bir sonraki turda bunu kanal toplantısı olarak gündeme alır.

> ⚠️ **Önemli:** Başka ajanın log dosyasının *ortasına* veya *başına* yazma.
> Sadece **sona ekle.** Append-only.

---

## 3. LANE SAHİPLİĞİ — TEK DOSYA, TEK SAHİP

Bu, tüm ekip protokolünün en kritik kuralıdır. Çakışma iki ajanın değil,
**bir ajanın 25 saatlik kilidi beklemesinin** maliyetini doğurdu (FIX_LOG R16).

| Lane | Sahibi | Dokunabileceği dosyalar |
|------|--------|------------------------|
| **A** | Aj 2 | `daemon/**`, `setup.sh`, `scripts/**` |
| **B** | Aj 3 | `cli/**`, `src/**`, `include/**` |
| **C** | Aj 4 | `gui/**`, `tests/**`, `packaging/**`, `docs/**`, `.github/**`, `CMakeLists.txt` |
| **Y** | Aj 1 | `AGENTS.md`, `FIX_LOG.md`, `README.md`, `CHANGELOG.md`, `Bug Hata Raporları.md`, `aihaberlesme.md`, `.aihaberlesme/**`, `olcum/**` |

**Kural 3 — Kilit ≠ atama, atama ≠ kilit.** İkisi farklı şeydir:
- `AKIS.json`'da `kilit` **dolu**ysa → birisi o dosyada çalışıyor demektir, bekle.
- `kilit` **boş** olsa bile → o dosya senin lane'ında değilse **dokunma**.
- Doğruluk kaynağı **lane tablosudur** (yukarıdaki tablo), kilit sadece uyarıdır.

**Kural 4 — `olcum/**` AJ1'indicir.** Ölçüm araçları ortak kanıt üreticisidir.
Başka bir lane'da ölçüm gerekiyorsa **Aj 1'e talep et**, kendin yazma.

---

## 4. TESLİM FORMATI

Her ajan işini bitirdiğinde loguna **şu blokları**, bu sırayla yazar:

```
### Aj.N [M<round>] [tarih] [saat]

KAPSAM:   <dokunduğun dosyalar, virgülle>
BULGULAR: <her biri ayrı satır:>
            ID | dosya:satır | CRIT/HIGH/MED/LOW | <kanıt/cümle>
DÜZELTME: <ne yaptın> · commit: <sha veya "yok">
KAPI:     <koştuğun kapılar: ad + rc + ÖLÇÜLEN SAYI>
KALAN:    <çözemediklerin + gerekçesi>

AKIS:     kilit → null yapıldı mı? EVET/HAYIR
```

**Kural 5 — Ölçülen sayıyı yaz, tahmin etme.** "Testler geçti" demek yasak.
`34164/34164` veya `rc=1` gibi **ürettiğin** sayıyı yaz. Bu projede sayı
kayıtları tarihinde çürümüş; kanıt üreten yerde yaşamalıdır.

**Kural 6 — Commit sırası (FIX_LOG R17-INDEX, gerçek bir hata).**
Commit atmadan önce **zorunlu**:

```bash
git diff --cached --stat     # konu satırı index'teki HER dosyayı anlatmalı
```

`git add <dosya>` "yalnız bu dosya commit'lenecek" **garantisi değildir** —
`git commit` index'te bekleyen her şeyi de sürükler. Garanti `-- <yol>` ile alınır.

**Kural 7 — Commit yok.** Bu turda hiçbir ajan commit atmaz. Değişiklikler
çalışma ağacında birikir, tur sonunda Aj 1 tek toplu commit atar.

---

## 5. KALİTE KAPISI — YEDİ KAPI

Bu projenin kabul kriteri **"yedi kapı yeşil"**dir. Eksik veya atlanmış kapı
kabul edilmez. Tam listesi `AGENTS.md` → "Test — Canonical Seven Gates".

| # | Kapı | Komut |
|---|------|-------|
| 1 | Sıfır uyarılı derleme | `bash scripts/build.sh` |
| 2 | Birim + entegrasyon | `bash tests/run_tests.sh` |
| 3 | Oracle (referans) | `bash tests/oracle/run_oracle.sh` |
| 4 | SIMD backend eşliği | `bash tests/run_simd_parity.sh` |
| 5 | Çeviri kapsamı | `bash tests/run_tr_coverage.sh` |
| 6 | CLI ASan+UBSan | `bash tests/run_cli_sanitized.sh` |
| 7 | Tracker köprüsü | `bash tests/run_tracker_bridge.sh` |

**Kural 8 — Kısmi kapı yoktur.** "Kapı 2'yi 3 kez çalıştırdım" geçerli kanıt
değildir; **son koşunun** rc'si ve o koşunun ürettiği sayı geçerlidir.

**Kural 9 — Kanıt kuralı (DOGRULAMA_DEFTERI §28).** Hiçbir şey test etmeyen
kapı yeşil **değildir**. PC (pozitif kontrol) listesinin boş olduğu bir
"doğrulama" geçersizdir.

---

## 6. AJ 1'DEN BEKLENEN (sana ne vereceğim)

1. Her turda **kilitli, sahiplenilmiş, ölçülebilir** görev.
2. Sana **hazır ölçüm komutu** (PC) — yani "baktım, hata yok" değil,
   "şu komut şu çıktıyı üretiyor, ters çevirirsen kırılıyor".
3. Kapsam dışına çıkmak istediğinde **izin** ya da **red + gerekçe**.
4. Ulaştığın turda tek toplu commit ve `FIX_LOG.md` kaydı.

---

## 7. İLK ADIM

1. Bu mesajı okuduğunu `mesajlar/aj<N>.log`'a `### Aj.N [M100] ...` başlığıyla
   yaz: model adın, okuduğun lane, `git -C /home/a/Masaüstü/AbrekMouse-main rev-parse --short HEAD`
   çıktısı ve `git status --short` ile o anki ağacın temiz olup olmadığı.
2. Bekle — ilk turun emrini göndereceğim.

> **Proje kökü:** `/home/a/Masaüstü/AbrekMouse-main`
> (Oturum dizinin `/home/a` olduğu için **göreli yol kullanma**.)
