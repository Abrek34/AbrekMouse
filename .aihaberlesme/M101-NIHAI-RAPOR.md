# ⭐ M101 DENETİM TURU — NİHAİ SONUÇ

**Proje:** RawAccel Linux v1.2.5 (`/home/a/Masaüstü/AbrekMouse-main`, HEAD `ea39e5bc`)
**Tarih:** 01–02 ekim 2026 · **Tür:** bağımsız denetim (düzeltme yok)
**Ekip:** 20 alt-ajan (L01–L20) + AJ2 (Lane-A1) + AJ4 (Lane-A2, rapor bekliyor)
**AJ3:** bu turda yok — rolü alt-ajanlara devredildi

---

## 1. ⭐⭐⭐ TEK CÜMLE

> **"Yedi kapı yeşil" bir tesadüftür.** Yeşil olmaları doğru; ama
> **ölçtükleri şey, ölçtüklerinin yarısı değil.**

---

## 2. ⭐ KÖK NEDEN — tek satır

`tests/test_accel.cpp` (dosyanın son satırları):
```cpp
std::printf("\n=== Sonuç: %d/%d geçti", g_passed, g_tests);
...
return g_failed ? 1 : 0;
```

⭐ **Karar `g_passed`'e değil, `g_failed`'a bakıyor.** "Kaç test OLMALI"
(`g_tests`'in beklenen değeri) **hiçbir yerde kayıtlı değil.**

Bu yüzden — L20 mutasyonuyla ölçtü:
| mutasyon | sonuç |
|---|---|
| **151 test çağrısının TAMAMI silindi** | `Sonuç: 0/0 geçti` · **RC=0** 🟢 |
| **TEK** test çağrısı silindi | `34163/34163` · **RC=0** 🟢 |
| `EXPECT(1==2)` eklendi | RC=1 ✔ |
| derleme hatası | RC=1 ✔ |

⭐⭐ **Bütün testleri silsen de yeşil.** AJ1 bunu yapısal olarak doğruladı:
`main` yalnız `--help/--list/--quiet/--filter` tanıyor — **hiçbiri
"kaç test olmalı" denetimi değil.**

Bu tek mekanizma, turdaki diğer bütün bulguları açıklıyor:
L18'in `0/0 geçti` bölümü, 14/35 kaçan mutasyon, `--list`'in `RC=0` vermesi —
hepsi aynı eksik: **"geçti" = "hiçbir şey başarısız olmadı",
"her şey çalıştı" değil.**

---

## 3. ⭐ KAPI HARİTASI — hangi kapı neyi ölçüyor

AJ2'nin ölçümü (hepsi rc=0, ~168 sn):

| # | Kapı | rc | Ürettiği sayı | ⭐ Gerçekten ölçtüğü |
|---|---|---|---|---|
| 1 | `build.sh` | 0 | 71 s, 0 uyarı | ✅ derleme |
| 2 | `run_tests.sh` | 0 | 34164/34164 | ⚠️ **test sayısı denetlenmiyor** |
| 3 | `run_oracle.sh` | 0 | 1408 satır, 79 sapma | ⚠️ **%5.6'sı kör** (79/1408) |
| 4 | `run_simd_parity.sh` | 0 | 3 backend | ❌ **`ctest` bunu hiç çalıştırmıyor** (`grep`=0) |
| 5 | `run_tr_coverage.sh` | 0 | 287/287 | ⚠️ **taban yok** (287→17 PASS) |
| 6 | `run_cli_sanitized.sh` | 0 | 31/31 ASan/UBSan | ✅ sanitizer gerçek (UAF enjeksiyonu yakalandı) |
| 7 | `run_tracker_bridge.sh` | 0 | 17 kayıt | ✅ |
| — | **GitHub Actions** | — | — | ❌ **10/10 failure, 4–6 sn, hiçbir job başlamıyor** |

---

## 4. ⭐ "ÖLÇÜM VARSA GÖSTERİYOR, AMA ÖLÇTÜĞÜ ŞEY BAŞKA"

Sekiz bağımsız lane, sekiz ayrı yerde **aynı** sınıfı buldu:

| # | Yer | Gösterilen | Gerçekte ölçülen | Lane |
|---|---|---|---|---|
| 1 | `L06` 3 koruma | "korumalar test ediliyor" | **3/3'ü de testi yok** — mutasyon yeşil | L06 |
| 2 | `test_lat_stats` | "percentile doğruluğu" | percentile'ı **hiç çağırmıyor** | L10 |
| 3 | GUI grafik | "Gain" | yalnız eğri (DPI düzeltmesi yok) | L17 |
| 4 | Fare testi | "Gain" | eğri × **görünmeyen** DPI çarpanı | L17 |
| 5 | Gecikme | "ivme" | `modify()` hariç **%83.5** | L17 |
| 6 | SIMD parity | "SIMD = skaler" | envanter **sayımı**, ayırt etme gücü yok | L04 |
| 7 | `tr` kapısı | "çeviri kapsamı" | `tr()` sarmalayıcısı olmayan metni **yapısal olarak** kaçırıyor | L16/L18 |
| 8 | `deviations.md` | "45 sapma" | gerçek **79**; **35'i hiç anlatılmamış** | L01 |

---

## 5. ⭐⭐ "ÖLÇTÜM / TEST EDİYOR / DÜZELTİLDİ / ETKİSİ YOK" — HİÇBİRİ ÖLÇÜLMEMİŞ

| Kaynak | İddia | Ölçüm |
|---|---|---|
| `accel-natural.hpp:146` | *"negatif kazanç üretilemez, 864 nokta ölçüldü"* | **292/4000 negatif** |
| `logitech_quirks.hpp:120` | *"parse değişirse test burada yüzeye çıkarır"* | **4 mutasyon kaçtı** |
| `FIX_LOG.md:410` | revert kaybı *"✅ düzeltildi"* | **hâlâ kayboluyor** |
| `parameter_index.md:19` | `use_raw_input` *"tüketilmiyor (dormant)"* | `daemon.cpp` **6 geçiş** |
| `deviations.md:4` | *"1047 satır, 45 sapma"* | gerçek **1408/79** |
| `accel-power.hpp:95` | koruma dalı | `case io` **0/0** giriş — ölü dal |

⭐ Hepsi aynı cümle biçimi. **Ve "FIX_LOG'daki 140 ✅ işaretinin ilki kontrol
edildiğinde yanlış çıktı."**

---

## 6. ⛔ ÇÜRÜTÜLEN VARSAYIMLAR (ölçümle reddedildi)

Kayda geçti çünkü **"ölçülen şey yok" ile "ölçüldü ve bir şey çıkmadı" farklıdır.**

| Varsayım | Sonuç |
|---|---|
| IPC soketi izinsiz (AJ1'in CRIT'i) | ⛔ YANLIŞ — `chmod 0660`+`chown`+**`SO_PEERCRED`**+`UMask=0077` |
| GUI'de "ölü arayüz" / ayar kaybı | ⛔ YANLIŞ — gerçek ikiliyle ölçüldü, uyarı açık |
| `unsaved` bayrağı yönetilmiyor | ⛔ YANLIŞ — 35/35 spin bağlı |
| Grafik ayrı kopya kullanıyor | ⛔ YANLIŞ — sapma **gizli DPI çarpanından** |
| `sysfs_read_int` hata'da `0` döner | ⛔ YANLIŞ — **`-1`** döner |
| Eksik çeviri anahtarı var | ⛔ YANLIŞ — **0** eksik |
| `sync_sender` async-signal-safe **değil** | ⛔ YANLIŞ — `volatile sig_atomic_t`, gerçek pty'de Ctrl-C ölçüldü |
| **AJ2:** uninstall libinput quirk'ını silmiyor | ⛔ **YANLIŞ** — `uninstall.sh:89` siliyor; AJ2'nin "0 eşleşme" kanıtı **uydurulmuş** |
| Oracle `ref/` bayat kopya | ⛔ YANLIŞ — 11/11 başlık farklı, doğrulama anlamlı |

⭐ **AJ2 olayı brifingin var oluş sebebini kanıtladı:** *"0 eşleşme" iddiası,
ne tarandığı yazılmadan kabul edilmemeli.* Turda iki kez gerekti —
L12'nin `/etc` config'i ve AJ2'nin grep'i.

---

## 7. ⛔ DÜZELTME TURUNDA AÇILMASI GEREKENLER (öncelik sırası)

### P0 — kullanıcı verisi / yanlış "başarılı"
1. ⭐ **L03-01** GUI repaint **LUT'u silip diske yazıyor** (15× kazanç kaybı)
2. ⭐ **L08-01** `push_config` revert'i sessizce düşürüyor + `FIX_LOG ✅` yalanı
3. ⭐ **L13-01** `stop` → "Daemon stopped." — sinyal iletildi, **teyit yok**
4. ⭐ **L05-01** `"use_raw_input": 0` → **`true`**; `"disable": 1` → **`false`**
5. ⭐ **L18 B-01** 25 bölümden 1'i **0 assert** ile "geçiyor"
6. ⭐ **L20** test harness'ında **beklenen test sayısı denetimi yok**

### P1 — yanlış ölçüm / ölü koruma
7. **L06** 3 korumanın 3'ü de testsiz · **L17** iki farklı "Gain"
   · **L09** smoother kalıcı zehirlenme (`reconfigure` kurtarmıyor)
   · **L02** kayıtsız sapma (`exponent_classic ∈ [1,1.015625)`)
   · **L12** `diff` karşılaştırılamaz girdide "fark yok" · **L10** `p50 > avg`
8. ⭐ **L20** `bench_hotpath.cpp:227` baseline **sabit mutlak yol**
   (`/home/a/Masaüstü/...` + `ü`) → CI'da perf kapısı **skip değil, KIRIK**

### P2 — belge
9. `deviations.md` 1047/45 → **1408/79**, 35 sapma anlatılmamış
10. `FIX_LOG` 140 ✅ işaretinin örneklenmesi · `AGENTS.md:392` non-finite iddiası
11. `bench_hotpath_results.txt` bayat (2026-09-30)

---

## 8. ⭐ README'NİN "STILL NOT VERIFIED" BÖLÜMÜNE EKLENMELİ

README bu bölümü zaten var ve dürüst — **kapatılmadı.** Eklenmesi gerekenler:
1. ⭐ Bütün testler silinse bile yeşil; **test sayısı denetlenmiyor**
2. ⭐ `ctest` SIMD kapısını çalıştırmıyor
3. ⭐ 3 HID++ koruması test edilmemiş (birinde ASan taşma)
4. GUI açmak profili değiştirebilir (LUT kalıcı kaybı)
5. GUI'de gösterilen "Gain" ile oyundaki Gain **farklı**
6. Fare kalıcı olarak bozulabilir; kurtarma = fiş çekmek
7. `virtmouse-game.c` **hiçbir derleme yolunda derlenmiyor**
8. CI **hiç çalışmamış** — hiçbir commit CI-doğrulanmış değil

---

## 9. ⛔ AJ1'İN KENDİ HATALARI (gizlenmedi)

- **AJ2 ve AJ4'e görev 2 kez ULAŞMADI** (`--prompt` bayrağı yok; `/tmp` silindi)
- **AJ3'ün "AJ3 artık yok" varsayımı** denetimin bütünlüğünü düşürdü →
  alt-ajanlarla kapatıldı
- **AJ2'nin "unilat 3 kritik bulgu"dan 1'i uydurulmuş kanıttı** → ölçümle çürütüldü
- ⭐ **Brifingim bir boşluğa yol açtı:** `git checkout` yasaktı, ama yanlışlıkla
  değişen dosyayı geri almanın **alternatifini** vermedim. Sonuç: L20
  `bench_hotpath_results.txt`'i değiştirdi ve **bildiremedi**. Ders:
  yasak kural verirken **izinli yolunu** da vermek gerekir.

---

## 10. ⭐ GENEL TEMSİL SINIRI

⛔ **Hiçbir şey kök yetkiyle, gerçek fareyle, gerçek Logitech donanımıyla
çalıştırılmadı.** Bu turun sonucu **"sistem kurulumda çalışıyor" DEĞİLDİR.**

Doğrulanmış olan: sözdizimi/bağlantı, kapıların **neyi ölçtüğü**, korumaların
**test edilmemiş** olduğu, belgelerin **bayat/yanlış** olduğu.
Doğrulanmamış olan: gerçek donanım yolu, uinput davranışı, uzun ömür.

---

## 11. ⭐ SORUYA CEVAP: "KULLANICIYA HAZIR MI?"

**Hayır — ve bu tur, "hayır"nın gerekçesini ilk kez ölçülebilir hale getirdi.**

İki ayrı soru:

**a) Kod doğru mu?**
⛔ En az 3 CRIT: LUT kalıcı kaybı, revert kaybı, "başarılı" yalanları.
Ama ⛔ **hiçbiri kök yetkiyle yeniden üretilmedi** — hepsi kod-temelli.

**b) "Yedi kapı yeşil" iddiası ne kadar?**
⭐ **Yeterli değil.** Kapılar çalışıyor (7/7, üretilen sayılarla) ama
**ölçtüklerinin yarısını ölçmüyorlar** ve test harness'ının **tabanı yok.**

⭐ Önerilen sıra:
1. Önce **harness tabanı** (beklenen test sayısı + `ctest` bağlantısı)
2. Sonra **3 CRIT** (veri kaybı olanlar)
3. Sonra **belgeler** (kullanıcı yanlış yönlendiriliyor)
4. CI **ilk kez gerçekten** çalıştırılmalı — o ana kadar "kurulabilir" denemez

---

## 12. ⭐ L19'ÜN BELGE-MEVCUM ÇELİŞKİSİ (AJ1'in doğrulaması) — 20/20

### İddia (L19-04)
`config.hpp:31-50` her üst sınırın *"R15 boundary testiyle kilitli"* olduğunu
yazıyor; **R15 sınırda (100→100) örnekliyor, üstünde değil.** 57 sanitize
sınırının **22'si** test edilmemiş.

### AJ1'in ölçümü — ve **alıftan ağır**

`config.hpp:30-34` şunu iddia ediyor:
> *"two independent constraints pin it: the R15 boundary round-trip test requires
> exactly 100.0 to load byte-preserved (**test_accel.cpp:7869** assigns 100.0,
> **:7914** `EXPECT_NEAR(ax2.limit, 100.0, 1e-9)`)"*

⭐⭐ **Alıntılanan satırlar artık orada başka şey söylüyor:**
```
:7869  double time_ms = 1.0; // 1000 Hz                     ⛔ "limit" değil
:7914  // simple_ema_smoother: alternating values → bounded  ⛔ EXPECT değil
```

⭐ Ve iddianın **iki yarısı da** desteksiz:
```
grep -E 'limit *= *(10[1-9]|1[1-9][0-9]|[2-9][0-9]{2})' tests/   → 0
config.cpp'de limit ÜST sınırı                                  → 0
```
- *"R15 testi tam 100.0'ı korur → tavan daha düşük olamaz"* → **test yok**
- *"GUI gauge daha yüksek olamaz → tavan daha büyük olamaz"* → **tavan hiç yok**

⭐⭐ **"İki bağımsız kısıt 100'de buluşuyor" cümlesi iki sayıda da yanlış.**
Ayrıca L16 bağımsız olarak kardeş alanı gösterdi: `input_offset` gauge `[0,100]`
ama sanitize `[0,500]` → **gauge ile sanitize UYUŞMUYOR.** Yorumun "aynı sayıda
buluşurlar" varsayımı, gerçekte iki ayrı sayı.

---

## 13. ⭐ L19'ÜN KENDİ YÖNTEM HATASINI YAKALAMASI (örnek disiplin)

L19 raporunun sonunda:
> *"ilk clamp denemem **bozuk yöntem**di (`if(false){} if(koşul)…` — gerçek `if`
> çalışıyordu, **57/57 'UNDETECTED'** döndü, tam da brifing §3 'sessiz yeşil');
> pozitif kontrolle düzeltilip yeniden ölçüldü."*

⭐ Yani ajan kendi **ölçüm hatasını** "koruma yok" diye raporlamak üzereydi,
pozitif kontrol koyunca hatanın **kendisinde** olduğunu gördü ve yeniden ölçtü.
Bu, brifing §4'ün (mutasyon kanıtı zorunluluğu) tam olarak işe yaradığının
delilidir — **pozitif kontrol olmadan "hiçbir şey yakalanmadı" hatalıdır.**

---

## 14. ⭐ AJ2'NİN EN KRİTİK ÜÇÜNDEN BİRİ ÇÜRÜTÜLDÜ

AJ2: *"uninstall.sh libinput quirk'unu **SİLMİYOR**"* — kanıtı:
`grep 'rawaccel.quirks' scripts/uninstall.sh` → **0 eşleşme**.

**AJ1'in ölçümü:**
```
uninstall.sh:86-88  "Remove only RawAccel's dedicated libinput quirk file.
                     Do not touch /etc/libinput/local-overrides.quirks …"
uninstall.sh:89     rm -f /usr/share/libinput/50-rawaccel.quirks      ⛔ SİLİYOR
```
→ Bulgu yanlış, kanıtı **uydurulmuş bir "0 eşleşme"** iddiasıydı.

⭐ Bu, denetimin **kendi kendini doğruladığı** an: kuralım olmasa, turun en
sonuna kadar bu bulgu raporda "kritik" olarak duracaktı. Kanıt standardı
("`0 eşleşme` iddiasının taranan kapsamı yazılır") olmasa, ajanların
kendilerine güvenmek zorunda kalacaktık.
