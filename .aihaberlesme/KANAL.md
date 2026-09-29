# AJ1 YÖNETİCİ KANALI — v1

> Bu dosya, AJ1 (yönetici) ile diğer oturumlar arasındaki **tek yazılı emir/kabul
> kanalıdır**. Aynı bilgi `mesajlar/aj1.log` içinde de zaman-sıralı olarak tutulur.

## 1. AJAN KİMLİĞİ VE OTURUM EŞLEMESİ (AJ1 tarafından doğrulandı)

| Ajan | Model | opencode session id | Görev alanı |
|------|-------|---------------------|-------------|
| **Aj 1** | `space-bunny-free` (variant=max) | `ses_f1199eaf8ffe1sFoTv46EmtzBf` | YÖNETİCİ — emir, koordinasyon, kabul kapısı |
| **Aj 2** | `nemotron-3-ultra-free` | `ses_f1199d68dffeaLOGBRD7IAG2Ku` | Lane A |
| **Aj 3** | `big-pickle` | `ses_f1199c1bbffeuU5YLL8vCLDher` | Lane B |
| **Aj 4** | `nemotron-3.5-lightning-free` | `ses_f119964f7ffeuuWIQThJd3P3Mu` | Lane C |

**TÜM AJANLAR İÇİN GEÇERLİ 4 KURAL:**

1. **Proje kökü:** `/home/a/Masaüstü/AbrekMouse-main`
   (`~` = `/home/a`; oturum dizini `/home/a` olduğu için göreli yol KULLANMA).
2. **Sahiplik:** Aşağıdaki lane tablosundaki dosyalara **başka kimse dokunamaz**.
   Kendi lane'in dışına çıkma → önce AJ1'e bildir.
3. **Sıralama disiplini (FIX_LOG R17-INDEX):** commit öncesi
   `git diff --cached --stat` çalıştır; konu satırı index'teki HER dosyayı
   anlatmalı. `git add <dosya>` yetmez — `git commit -- <yol>` kullan.
4. **Kanıt kuralı (DOGRULAMA_DEFTERI §28):** hiçbir şey test etmeyen kapı yeşil
   değildir. PC listesi boşsa kapı geçersizdir.

## 2. LANE TABLOSU (tek-dosya-tek-sahip)

| Lane | Sahibi | Dosyalar |
|------|--------|----------|
| **A** | Aj 2 | `daemon/**`, `setup.sh`, `scripts/**` |
| **B** | Aj 3 | `cli/**`, `src/**`, `include/**` |
| **C** | Aj 4 | `gui/**`, `tests/**`, `packaging/**`, `docs/**`, `.github/**`, `CMakeLists.txt` |
| **Y** | Aj 1 | `AGENTS.md`, `FIX_LOG.md`, `README.md`, `CHANGELOG.md`, `Bug Hata Raporları.md`, `aihaberlesme.md`, `.aihaberlesme/**`, `olcum/**` |

> `olcum/**` AJ1'indir çünkü ölçüm araçları (PC üreten kanıt üreticileri) tüm
> turların ortak kanıt tabanıdır; diğer ajanlar **ölçüm isteyerek** üretir.

## 3. RAPOR FORMATI (her ajan, iş bitince `mesajlar/aj<N>.log` sonuna ekler)

```
### Aj.N [M<round>] [tarih] [saat]
KAPSAM: <dokunulan dosyalar>
BULGULAR: <her biri: ID | dosya:satır | hata sınıfı (CRIT/HIGH/MED/LOW) | kanıt>
DÜZELTME: <yapılanlar + commit sha>
KAPI: <hangi kapılar koşuldu, rc + ölçülen sayı>
KALAN: <çözülmeyenler ve gerekçesi>
```

AJ1 her raporu **bağımsız doğrular** ve `KARARLAR.md`'ye kabul/ret kararı yazar.
AJ1'in kabulü olmadan hiçbir düzeltme "tamam" sayılmaz.
