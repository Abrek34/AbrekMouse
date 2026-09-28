# `olcum/aj2/` — AJ2 (space-bunny-free) ölçüm araçları

Bu dizin **yazan yol** kuralı için burada: araç repoda değilse, kimse
çalıştırmıyor ve ölçümün arkasındaki gerekçe kayboluyor. Her araç, kendini
doğrulayan bir **pozitif kontrol** ile birlikte gelir — PCsiz bir "temiz"
sonucu kanıt değildir.

## `prove_kod_ayni.py` — "kod değişmedi" iddiasını ölçümle kanıtlar

Yorum eklendiğini söylemek yetmez; derlenmiş artefaktı değiştirip
değiştirmediğini göstermek gerekir. Yorumlar ve boş satırlar soyulur, satır
içi `//` silinir, kalan çıktı bayt bayt karşılaştırılır.

```bash
python3 olcum/aj2/prove_kod_ayni.py --git include/math-vec2.hpp   # HEAD~1 -> HEAD
python3 olcum/aj2/prove_kod_ayni.py once.hpp sonra.hpp
python3 olcum/aj2/prove_kod_ayni.py --pc include/math-vec2.hpp    # ZORUNLU
```

`--pc` bilinen bir mutasyonla sayının değiştiğini gösterir. **Bunu
atlamadan "AYNI" sonucu kanıt değildir** — alet duyarsızsa daima 0 döner.

### `--dallar` — bir dalın sayacını **ölç**, elle sayaç koyma

```bash
python3 olcum/aj2/prove_kod_ayni.py --dallar include/math-vec2.hpp \
    --surucu olcum/aj2/dal_surucu.cpp --arama olcum/aj2/dal_arama.cpp
```

`--surucu` **zorunludur**: sayacı ölçecek çağrıyı kim yapıyor? Bilinmeden dal
sayacı boş kalır ve "erişilemez" sanılır — en sık yapılan hatadır.

Sayaç **elle konmaz**, derleyicinin `-fprofile-arcs` sayacı okunur; böylece
sayac bu dosyadan bağımsız olur. İki PC her çalıştırmada zorunludur:

| PC | ne doğrular | kalmazsa |
|---|---|---|
| **PC-1** | sayaç duyarlı mı (dolu dal üretiyor mu) | *hiç dal bulunamadı* → alet hatası, "erişilemez" **değil** |
| **PC-2** | sayaç doğru mu — her koşul bloğunda **çıkış sayacı = giriş sayacı** | blok veri akışı bozuk → alet hatası |

### ⛔ SIFIR SAYAN DAL **SİLME ÖNERİSİ DEĞİLDİR**

Sıfır, *"bu girdi kümesi o dala hiç girmedi"* demektir. *"bu dal imkânsız"*
demek için ya **erişilebilir girdi** bulunur, ya da **matematiksel** olarak
gösterilir. Araç girdi bulamazsa `"erişilebilir girdi bulunamadı"` yazar ve
**silme önermez** — bulamamak erişilemezliği kanıtlamaz. `--arama` sürücüsü
sıfırdan hareket eden bir dal bulursa o dal **erişilebilirdir** ve açıkça
"ERİŞİLEBİLİR" diye raporlanır.

**Bu kural, `math-vec2.hpp`'de bir kez canlı olarak işledi:** temel ızgara 6
dalı sıfır bıraktı; arama sürücüsü `L62`'nin üç dalını `0 → 972 / 99 592 /
100 564` ile **erişilebilir** buldu. Yani "sıfır" gerçekten "erişilemez"
demiyordu. Kalan üç dal (`L131`/`L132`/`L137`) bulunamadı ve **silme önerilmedi**;
mutasyon `L131`'in eşiğini `1e300` yapınca sayac `0 → 511 500` oldu, yani
sıfırlar **tutulmuş sayaç** değil, gerçek erişilemezlik ölçümü.

### Tuzaklar (bu modda yaşandı)

* **`gcov --json-format` `-b` olmadan branch YAZMAZ.** `-b` unutulursa her dal
  `0` görünür ve araç "erişilemez kod" der. PC-1 bunu yakalar — *kanıtı olmayan
  sıfır, önce alet hatası varsayılır.*
* **Satırdaki TÜM dallar toplanmaz.** `a && b` **iki** ayrı koşul bloğu
  üretir; ilk PC denemesi bu yüzden `L20: 392 != 343` ile **kendi kendini
  kırmış** (PC'nin kendisi yanlıştı). Doğru değişmez blok bazlıdır: satır
  sayacına girmeyen bloğun çıkış toplamı = satır sayacı, diğerlerinki = o
  bloğa gelenlerin toplamı.
* **`include` gölgelemesi:** tırnaklı `include` önce dosyayı **include eden**in
  dizinini arar; `-I` ile değiştirilemez. `include/` dizininin **tamamı** kopyalanır,
  ölçüm kopyada yapılır.


Bu projede yolun açtığı üç tuzak, aracın docstring'inde ve burada:

1. **grep locale tuzağı.** `LANG=tr_TR.UTF-8` altında `grep -oE "[A-Za-z0-9_]+"`
   eşleşmeyi sessizce keser (`_mm256_div_pd` → `_mm256_d`) ve **eşleşme
   sayısı doğru çıkar**, yani gözle kontrol eden "12 intrinsic var, tamam"
   der ve geçer. Her ölçümde `LC_ALL=C` ya da `[[:alnum:]]` ya da Python `re`.
2. **include gölgelemesi.** `simd_parity.cpp → rawaccel.hpp → "simd_math.hpp"`
   tırnaklı include; **önce include eden dosyanın dizinini** arar, `-I` sırası
   işe yaramaz. Başlığı değiştirmek için `include/` dizinin **tamamını**
   kopyala, sonra kopyada değiştir. (Symlink denemesi de başarısız:
   `#pragma once` ayrışmıyor, yeniden tanım hataları.)
3. **Ölü kod.** `return;`'den sonra yazılan mutasyon hiçbir şey yapmaz. Mutasyonu
   yazdıktan sonra **canlı olduğunu doğrula**.

## Ölçülen sonuçlar (2026-09-28)

| dosya | yorum eklendi | yorumsuz kod | sonuç |
|---|---|---|---|
| `include/accel-natural.hpp` | 109 satır | değişmedi | `+/0` |
| `include/accel-power.hpp` | 21 satır | değişmedi | `+/0` |
| `include/simd_math.hpp` | 56 satır | 241 → 241 | byte-level ayni |
| `include/math-vec2.hpp` | 35 satır | 45 → 45 | byte-level ayni |

Commit'ler: `b01342b1`, `207732a3`, `36a53c8c`.

**Sonraki tur (R16 guard'ı):** `include/math-vec2.hpp` artık 46 → 46 kod satırı
(byte-level ayni) ama **yorum sayısı 100 satır azaldı** — çünkü guard
gerekçesinin İKİ kopyası vardı ve biri yanlıştı (`daemon.cpp:2360-2361`
"girdiyi sıfırlıyor" iddiası). İkisi de silinip tek gerekçe bırakıldı;
`7c7b2702`. Kod değişmediği için `prove_kod_ayni.py` yine `+/0` der — yani
**bu satır "kod aynı" demek, "yorum doğru" demek DEĞİLDİR.** Yorumun doğruluğu
`guard_konumu.cpp` (S1) ve `run_guard_pc.sh` (PC1/PC2) ile ayrıca ölçülür.

## ⛔ Hangi dosyayı HANGİ KAPI doğruluyor — karıştırma

Bu ayrım ölçülerek konuldu (2026-09-28) çünkü "oracle'da gerçek fark yok"
gerekçesi **üç dosyanın ikisinde geçerli değil**:

| dosya | `run_oracle.sh` | hangi kapı gerçekten doğruluyor |
|---|---|---|
| `include/accel-*.hpp` | ✅ 1407 satır | `run_oracle.sh` **+** `run_tests.sh` |
| `include/simd_math.hpp` | ❌ hiç çağrılmıyor | `run_simd_parity.sh` (kendi envanter denetimli) |
| **`include/math-vec2.hpp`** | ❌ **hiç çağrılmıyor** | **yalnız `run_tests.sh`** |

Ölçüm — oracle harness'inde (`local.cpp` + `reference.cpp`) ve
`oracle_cases.hpp`'te geçen sayılar:

```
  lp_distance        harness 0   oracle_cases 0
  calc_speed_whole   harness 0   oracle_cases 0
  speed_processor    harness 0   oracle_cases 0
  rotate             harness 0   oracle_cases 0
  magnitude          harness 0   oracle_cases 2   (alan adi, cagri degil)
```

Yani oracle ızgarası hızı **skaler** verip doğrudan kazanç eğrisine sokuyor;
**vektör → hız** katmanı (`speed_processor`, `lp_distance`, `magnitude`,
`rotate`) ne yerelde ne referansta **hiç çalışmıyor**.

`simd_math.hpp` için bu boşluğu `run_simd_parity.sh`'in `BILINEN_OLUMLER`
envanteri kapatıyor. **`math-vec2.hpp` için hiçbir şey kapatmıyor** — onu
sadece `run_tests.sh`'in 34k iddiası doğruluyor, ve o iddialar
`lp_distance`'ı doğrudan, çoğu **sonlu** girdiyle çağırıyor.

**Bu yüzden:** `math-vec2.hpp` hakkındaki bir değişikliği gerekçelendirirken
*"oracle 1407 satırı yeşil"* demek **yanlış kapsamdır** — doğrusu
*"bu katman oracle'ın dışında; doğrulaması `run_tests.sh` + ölçülen
erişilebilirlik zinciri"*. Oracle yeşil kalması bu katmanda **bilgi
vermez**, çünkü katman orada çalışmıyor.

## `guard_konumu.cpp` — erişilebilirlik zincirinin NEREDE koptuğunu ölçer

AJ1 §2'deki boşluğu kapatır: "port ile referans ayrışıyor" iddiası **ne
zaman** geçerli? Üç vaka:

| vaka | ölçtüğü | sonuç |
|---|---|---|
| S1 | `daemon.cpp:2360-2361` guard'ı ivme yolunda çalışıyor mu? | **HAYIR** — `raw_passthrough` kolu 2352'de açılıp 2383'te `return` ediyor; ivme yolu 2510'dan devam ediyor |
| S2 | Üretimde `\|in\|` tavanı vs referansın taşma eşiği | 6.87e19 « 1.34e154 → referans üretimde **asla** taşmaz; 1e200 ayrımı 1.46e180 kat uzakta |
| S3 | S2'nin bir *sınır* olduğu duyarlılıkla doğrulanıyor mu? | eşiğin altı "güvenli", üstü "kırık" — ölçüm ayrımı yapıyor |

```bash
g++ -std=c++20 -I include -o /tmp/gk olcum/aj2/guard_konumu.cpp && /tmp/gk
```

**S1'in bulgusu bir hatayı da düzeltti.** `math-vec2.hpp` "üretimde
sonludur, çünkü `daemon.cpp` `dx/dy`'yi sıfırlar" diyordu; bu gerekçe
**yanlıştı** — guard'ın adı doğruydu (AJ1 §3 satırı düzeltti) ama **kolu**
yanlıştı. Doğru gerekçe **tavandır** (S2), koruma değil.

## `cmp_guard.cpp` + `run_guard_pc.sh` — guard'ın PC'leri

`run_guard_pc.sh` AJ1 §5'in üç PC'sini koşar (PC3 = oracle, ayrı kapı):

- **PC1 duyarlılık** — guard SONLU girdilerin sonucunu değiştirmiyor (7/7)
- **PC2 etki** — guard sonlu olmayan bileşende sonlu dönüyor (3/3)
- **PC2'nin PC'si** — guard'ı geri alan mutasyon 3 satırda da fark yaratıyor

```bash
bash olcum/aj2/run_guard_pc.sh
```

### ⛔ Bu turda ölçümü üç kez bozan tuzak — hepsi PC ile yakalandı

1. **Mutasyon fiilen NO-OP'tu.** `#ifdef` ile iki "yol" türetip
   karşılaştırmayı denedim; iki ikili **bayt bayt aynı** çıktı (16936 bayt).
   Sebep: fonksiyonu `#ifdef`'in *dışında* bırakmıştım, yani ikisi de aynı
   `lp_distance`'ı çağırıyordu. PC2 "3 satırda fark var" dediği halde
   gerçekte ölçüm yoktu. → Mutasyon artık `include/`'in **tamamının
   kopyasında** yapılıyor ve script **iki ikili bayt bayt aynıysa
   `exit 3` ile duruyor** ("mutasyon gerçekten kod değiştirdi" kontrolü).
2. **AJ1'in PC'si yanlış ölçüt verdi.** "Mutasyon `inf`e döndürmeli" dedi;
   ölçümde `(NaN,3)` guard'sızken `inf` değil **uydurma sayı `3`** dönüyordu
   (`NaN > 3` false olduğu için `M = 3` seçiliyor). Doğru ölçüt: guard'ın o
   satırda **değer değiştirmesi**. İlk yazım bu yüzden kırmızı geldi.
3. **Etiket boşluk içeriyordu.** `split(None, 4)` alanları yanlış kırptı,
   sayılar etikete kaydı; ölçüm "FARK" dedi, gerçekte fark yoktu. → `ROW`
   satırları TAB ile ayrılıyor (`rstrip().split("\t")`).

**Ve AJ1'in tarifinin kendisi bir boşluğa sahipti:** `!isfinite(M)` dedi,
yaptım, **PC2 kırmızı geldi** — `M` bir karşılaştırmayla seçildiği için
`(NaN,3)`'te `M = 3` (sonlu) oluyor ve guard hiç tetiklenmiyor. Dahası sonuç
**asimetrik**ti: `(3,NaN) → 0` iken `(NaN,3) → 3` — aynı vektör, iki farklı
hız. Guard **kaynak bileşenlere** (`ax`/`ay`) kondu. Regresyon kapısı
`test_lp_distance_nonfinite_components()`'in asimetri satırları.

