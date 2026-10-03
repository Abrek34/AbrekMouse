### L04 | alt-ajan (subagent) | 2026-10-01
**LANE**: SIMD doğruluğu ve SIMD parity kapısının geçerliği.

```
KAPSAM   : include/simd_math.hpp  (426 satır)
           tests/simd_parity.cpp  (323 satır)
           tests/run_simd_parity.sh (287 satır)
           referans okuma: include/math-vec2.hpp, include/rawaccel.hpp:330-460,
                           CMakeLists.txt, scripts/build.sh:41-129,
                           .github/workflows/ci.yml, tests/run_tests.sh:719-753
⛔ HİÇBİR ÇALIŞMA AĞACI DOSYASI DEĞİŞTİRİLMEDİ. Tüm mutasyonlar /tmp/opencode/l04
           üzerinde, `include/`+`src/`+`tests/` in gerçek kopyası üzerinde yapıldı
           (simd_math.hpp:113-118'in istediği gibi tam kopya — alıntılı include
           yolu -I'yi yok sayar).
```

---

## KAPI — koşturulan kapı + ham çıktı

```bash
$ cd /home/a/Masaüstü/AbrekMouse-main && bash tests/run_simd_parity.sh ; echo "RC=$?"
```

**HAM ÇIKTI:**
```
=== SIMD backend parity ===
    envanter: 26 v2d_* · kapsamlı 16 · canlı 8 · beyan edilmiş ölü 10 (A canlı-özellik-ikizi 2 · B spekülatif 8)
    envanter denetimi: OK (her v2d_* kapsamlı, canlı ya da beyan edilmiş ölü)
--- avx2: backend AVX2 ---
--- sse2: backend SSE2 ---
--- scalar: backend SCALAR ---

Sonuç: PASS — 3 backend test edildi ve birebir aynı sonucu üretti
RC=0
```

**KARŞILAŞTIRILAN VAKA SAYISI = 47 `case` satırı × 3 backend = 141 gözlem.**
Düzen 24 + 5 + 18 olarak ölçüldü:
- `test_extreme_primitives()` → 24 satır
- `run_pipeline()` → 5 satır (`%.12g`, bkz. L04-04)
- `run_pipeline_extreme()` → 9 etiket × 2 eksen = 18 satır

`grep -c '^case '` = 47 (her backend), `grep -c '^FAIL'` = 0, toplam 49 satır.
AGENTS.md'deki "26 / 16 / 8 / 10 / 0 ihlal" rakamları **ölçümle doğrulandı** (üstteki envanter satırı).

---

## BULGULAR

### L04-01 | HIGH | `include/rawaccel.hpp:428,435,436,437`
**Kapı, ÜRETİMDEKİ CANLI SIMD YOLUNUN İKİ DALINA YAPISAL OLARAK KÖR.**

`v2d_*` çağrılarının **tamamı** `modify_separate_simd()` içindedir ve orada
`v2d_*` çağrısı **20 adettir**; kapının hiç çalıştırmadığı **6 çağrı** vardır.
Kapı 14 profil girdisinin **hepsinde** `scale_smooth_halflife = 0` ve
`output_speed_smooth_halflife = 0` bırakır, dolayısıyla
`should_smooth_scale` (rawaccel.hpp:425) ve `should_smooth_output` (:434)
her zaman `false` kalır.

**Kanıt 1 — gcov satır yürütme sayacı** (kapının AVX2 ikilisi, `-fprofile-arcs -ftest-coverage -O0`):
```
NOTEXEC   428          v_scale = v2d_set(scale_x, scale_y);
NOTEXEC   435          double ox = std
NOTEXEC   436          double oy = std
NOTEXEC   437          v_in = v2d_set(ox, oy);
```
(aynı dosyada `390,391,392,393,414,423,432,440,445,446,447,448` → `14` ile yürütülüyor;
yani test 14 kez girdi, o 4 satır **sıfır** kez girdi.)

**Kanıt 2 — MUTASYON: `v2d_set(ox, oy)` → `v2d_set(oy, ox)` (X/Y EKSEN TRANSPOZİ)**
```
===== MUTATION [N1_output_smooth_xy_swap] rc=0 =====
  verdict: Sonuç: PASS — 3 backend test edildi ve birebir aynı sonucu üretti
  mismatch-pairs: 0  diff-rows: 0  backend-FAILs: 0
```
**Kapı YEŞİL.** Bu, `tests/simd_parity.cpp:5-9` ve `simd_math.hpp:147-152` ile
belgelenen **tarihsel üretim hatasının birebir aynı şekli** (düşey/yatay eksen
yer değiştirmesi → dikey fare hareketi sessizce ölür).

**Kanıt 3 — mutasyonun CANLI olduğunun pozitif kontrolü** (`/tmp` repro,
`output_speed_smooth_halflife = 20.0`, girdi `(100,50)`, `%.17g`):
```
                                       saf                        mutasyonlu
GATE-EQUIVALENT  (100,50) -> (103.125, 50.78125)          (103.125, 50.78125)   <- fark yok
OUTPUT-SMOOTH on (100,50) -> (43.895241070465332, 21.615080830153381)
                                                  -> (21.615080830153381, 43.895241070465332)
```
X ve Y **birebir yer değiştirmiş**. Mutasyon canlı, kapı kör.

Aynı dalın kardeşi: `:428  v_scale = v2d_set(scale_y, scale_x)` → **rc=0 (PASS)**
```
SCALE-SMOOTH on (100,50) saf -> (43.895241070465332, 21.615080830153381)
                       mutasyon -> (43.230161660306763, 21.947620535232666)   (~%1.5 kazanç hatası)
```

---

### L04-02 | HIGH | `include/rawaccel.hpp:445`
**`yx_output_dpi_ratio` — gerçek bir kullanıcı ayarı — tamamen yok sayılıyor, kapı yeşil.**

`v2d v_dpi = v2d_set(dpi_adj, dpi_adj * args.yx_output_dpi_ratio);`
→ `v2d v_dpi = v2d_set(dpi_adj, dpi_adj);` mutasyonu:
```
===== MUTATION [N3_drop_yx_dpi_ratio] rc=0 =====
  verdict: Sonuç: PASS — 3 backend test edildi ve birebir aynı sonucu üretti
```
**Pozitif kontrol** (`yx_output_dpi_ratio = 2.0`, girdi `(100,50)`):
```
YX-DPI-RATIO=2  saf -> (103.125, 101.5625)      <- Y ikiye katlanmış, doğru
                 mutasyon -> (103.125, 50.78125)  <- Y çarpanı YOK
```
Kapının 14 profil girdisi bu oranı varsayılan `1`'de bırakır
(`include/rawaccel-base.hpp:130`), dolayısıyla `:445`'teki çarpan hiçbir
 zaman farklı değerle sınanamaz. Bu dal CANLI bir üretim özelliğidir ve
 kapının kör noktasındadır.

---

### L04-03 | HIGH | `include/simd_math.hpp:159-160` (AVX2) ve `:365` (skaler)
**⭐ `v2d_max` envanter denetimince "KAPSAMLI" sayılıyor, ama kapı onu
`v2d_min`'den AYIRT EDEMİYOR.** — Bu, envanter denetiminin varlık nedeni
(bf. `run_simd_parity.sh:50-54`) olan şeyin tam tersi.

Maksimumla ilgili **yalnızca 3** `case` satırı var ve üçü de max↔min
ayrımı açısından **dejenere**:
```
case max(+0,-0) -0      case max(-0,+0) 0      case max(NaN,1) 1
```
- `max(+0,-0)`: MINPD de "her iki operand sıfırsa src2 döner" kuralıyla `-0` verir → aynı
- `max(-0,+0)`: aynı → `0`
- `max(NaN,1)`: MAXPD unordered durumda src2=1 döner; MINPD de aynı → `1`

**Ölçüm — AVX2 `v2d_max` → `v2d_min`:**
```
===== MUTATION [v2d_max  max->min] rc=0  GREEN  diffrows=0 =====
  verdict: Sonuç: PASS — 3 backend test edildi ve birebir aynı sonucu üretti
```
**Ölçüm — SKALER `v2d_max` → min semantiği** (`a.x > b.x` → `a.x < b.x`):
```
SCALAR v2d_max -> min   rc=0  GREEN  diffrows=0  backendFAILs=0
```
Karşılaştırma: `v2d_min`→`v2d_max` **YAKALANIYOR** (4 diff satırı) — ama
tek bir satır sayesinde: `min(den,0)` → `den` olurken `max`'da karşılığı
(`max(den,0)`) hiç yok. Yani `v2d_min` ve `v2d_max` arasındaki tek
ayrım **bir denormal satırına** dayanıyor.

**Envanter denetimi ad sayımı yapıyor, ayırt etme gücü değil.** 16 "kapsamlı"
fonksiyondan 15'i kırılmaya dayanıklı, 1'i (`v2d_max`) değil.

---

### L04-04 | MED | `tests/simd_parity.cpp:247` vs `:75`
**⭐ Tolerans sorusunun sayısal cevabı: kapının iddia path'inde tolerans
sıfırdır; TEK tolerans 47 satırın 5'inde `%.12g` baskısından gelir ve
gerçek bir SIMD hatasını gizleyecek kadar geniştir.**

**a) İddia yolu — tolerans = 0.** 12 `expect()` çağrısının **12'si de**
`eps = 0.0` (tam):
```
$ grep -cE '\bexpect\(.*,\s*0\.0\);' tests/simd_parity.cpp
12          # expect() çağrı yeri toplamı = 12 (tanım satırı :48 hariç), eps!=0.0 olan = 0
```
Pipeline iddiaları da tam: `in.y != 0.0` (`:238`), `in.x != 0.0` (`:242`),
`std::isfinite` (`:279,:281`). Backend'ler arası karşılaştırma da **byte-tam**
`diff` (`run_simd_parity.sh:271`).

**b) Baskı toleransı — asıl kör nokta.** İki farklı format:
```
:76   printf("case %s %s\n", label, buf);        // buf = %.17g   -> 42 satır
:247  printf("case %s %.12g %.12g\n", label, in.x, in.y);  //      -> 5 satır
```
`run_pipeline()`'ın ürettiği 5 satır (`y_only, x_only, both, both_rev, small`)
— yani **tam olarak üretim biçimindeki, tam sayı delta'larından geçen
pipeline satırları** — 12 basamağa yuvarlanıyor.

**Ölçüm — canlı `v2d_mul`'ü (1+1e-k) ile boz, hangi satır işaretleniyor:**
```
perturbation 1e-13 : y_only/x_only/both/both_rev/small  -> HEPSİ NOT FLAGGED
                     (toplam 40 satır değişti, 5'i 0 katkı verdi)
perturbation 1e-12 : y_only NOT FLAGGED, x_only NOT FLAGGED,
                     both FLAGGED(4) both_rev FLAGGED(4) small FLAGGED(4)
perturbation 1e-11 : 5/5 FLAGGED
```
**Tespit tabanı (relative):** 42 satır → `≤1e-13`; **5 satır → `≈5e-12 … 1e-11`**
(`y_only` değeri 103.125; `%.12g` çözünürlüğü 1e-9 mutlak → eşik ≈ 5e-10/103.125 ≈ 4.8e-12).
Yani bu 5 satır **diğer 42 satırdan ~2–4 mertebe daha kör**, ve
`run_simd_parity.sh:106-114`'ün bizzat belgelediği FMA sapması
(rel `7.77e-14` … `1.81e-16`) bu tabanın **altında** kalıyor.
`AGENTS.md`'nin "%.9g en kötü 4.5 milyon ULP yutar" argümanının
`%.12g` karşılığı ≈ 4.5e4 ULP — ölçülen taban.

---

### L04-05 | MED | `tests/run_simd_parity.sh:214-218`, `:241-246`, `:259-263`
**Belgelenen "77 = konak AVX2 ikilisini ÇALIŞTIREMİYOR, zarifçe atlar"
sözleşmesine ulaşılamıyor.** `have_avx2` probu yalnızca **derleme** dener
(`:216` `echo 'int main...' | $CXX ... -mavx2 ... -o probe`), çalıştırmayı
denemez. Çalıştırma `:241`'de `if ! "$bin"` ile başarısızlık sayılır →
`failed=1` → `:255` **exit 1**. `ran < 2` koşulu (`:259`) ancak ikiden az
ikili derlendiyse tetiklenir.

**Ölçüm — AVX2 derlemesine SIGILL enjekte edildi** (AVX2'yi çalıştıramayan
konakların (pre-Haswell x86-64, hypervisor/OS maskeli AVX2) yaptığı tam olarak bu):
```
  injected SIGILL into the avx2 build only
  GATE RC=1
  --- avx2: TEST BAŞARISIZ ---
  --- sse2: backend SSE2 ---
  --- scalar: backend SCALAR ---
  Sonuç: FAIL (bkz. yukarı)
```
Belgelenen sözleşme `exit 77`, ölçülen `exit 1` — ve kullanıcıya boş bir
"TEST BAŞARISIZ" + `Sonuç: FAIL` gösterilir, "atlandı" değil.

---

### L04-06 | MED | `tests/run_simd_parity.sh:259-263` + `tests/run_tests.sh:733-744`
**Ters yön: AVX2 ** düşürülürken kapı 0 döner ve "PASS — 2 backend" yazar;
`run_tests.sh` yalnız rc==77'de uyarır, o yüzden belgelenen gürültülü
"DİKKAT: SIMD parity ATLANDI" hiç çalışmaz.**

**Ölçüm — `have_avx2=0` (77'nin var olmak için tasarlandığı konak sınıfı,
yani x86 dışı / `-mavx2`'nin derlenemediği her konak):**
```
  forced have_avx2=0
  GATE RC=0
  --- avx2: atlanıyor (derleme/calistirma desteklenmiyor) ---
  --- sse2: backend SSE2 ---
  --- scalar: backend SCALAR ---
  Sonuç: PASS — 2 backend test edildi ve birebir aynı sonucu üretti
```
`ran=2 < 2` değil → 77 yolu tetiklenmez → rc=0 → `run_tests.sh:740`'taki
`77)` dalına girmez → kanonik boru hattı **sessizce** "N/N geçti" der,
oysa `-march=native` ile sevk edilen **AVX2 backend'i hiç test edilmedi**
(`CMakeLists.txt:69`, `build.sh:72`). `AGENTS.md`'in
"loud DİKKAT ... rather than passing quietly" cümlesi bu yolda yanlış.

---

### L04-07 | MED | `include/simd_math.hpp:48`, `:54-58`
**AVX-512 tespiti yapısal olarak erişilemez ve hiç test edilmiyor.**

**Ölçüm — ön işleyici durumu:**
```
  flags='-mavx2              ' -> defines: __AVX2__ __SSE2__
  flags='-mavx512f           ' -> defines: __AVX2__ __AVX512F__ __SSE2__     <- AVX2 da var
  flags='-mavx512f -mno-avx2 ' -> defines: __SSE2__                          <- AVX512F de YOK
  flags='-mno-sse2           ' -> defines: <none>
```
**Seçilen backend (ölçüm, `RAWACCEL_HAVE_*` + `sizeof(v2d)`):**
```
  -mavx2              -> AVX2   sizeof(v2d)=32
  -mavx512f           -> AVX2   sizeof(v2d)=32     <- AVX512 YOLU YOK
  -mavx512f -mno-avx2 -> SSE2   sizeof(v2d)=16
  -march=native       -> AVX2   sizeof(v2d)=32
```
Yani `:48`'deki `|| defined(__AVX512F__)` kolu hiçbir GCC bayrak kombinasyonunda
belirleyici olamaz (AVX512F tanımlıysa AVX2 de tanımlıdır), ve
`RAWACCEL_HAVE_AVX512` **proje genelinde sıfır tüketiciye** sahip:
```
$ grep -rn 'RAWACCEL_HAVE_AVX512' --include=*.hpp --include=*.cpp --include=*.sh --include=*.inl \
      include/ src/ daemon/ cli/ gui/ tests/ scripts/
include/simd_math.hpp:55:#   define RAWACCEL_HAVE_AVX512 1
include/simd_math.hpp:57:#   define RAWACCEL_HAVE_AVX512 0
```
`run_simd_parity.sh:41-45`'teki `BACKENDS` listesinde AVX512 varyantı yok.
Bu konak (`i9-9900K`, `/proc/cpuinfo`'da `avx512f` **yok**) üzerinde kapıyı
`-mavx512f` ile derlemek ölçüldü: **SIGILL, rc=132** — yani erişilebilir olsaydı
kapının hiçbir güvencesi yok.

---

### L04-08 | MED | `include/simd_math.hpp:168-171` (AVX2) vs `:382-385` (skaler)
**Maske döndüren 4 op yapısal olarak backend'ler arasında UYUŞAMAZ** ve
kapı bunu ölçmez — `v2d_blend`'in silinme nedeni olan "üç maske sözleşmesi"
(`simd_math.hpp:30-35`) aynı şekilde bugün de duruyor, sadece "ölü" ilan
edilerek ölçülemmez hâle getirildi.

**Ölçüm — 16 değerlik IEEE kenar ızgarası, 26 op, 396 gözlem/backend:**
```
  avx2 vs sse2 : 0 farklı satır
  avx2 vs scalar: 60 farklı satır
  ayrışan op'lar (avx2 vs scalar): clt 24 · cle 24 · cgt 6 · cge 6   (60 = 30 satır × iki yön)
```
Somut:
```
  < clt   0 1 -nan      (AVX2)     > clt   0 1 -1     (skaler)
  < clt   2 1 -nan                  > clt   2 1 -1
```
AVX2/SSE2 `all-ones` bit örüntüsü döner; bu, bir `double` olarak okunduğunda
**`nan`**'dır, `-1.0` değil. Skaler `{a.x < b.x ? -1.0 : 0.0}` döner.
Yani karşılaştırma **doğru** olduğunda ayrışma var (12/16, 12/16, 3/16, 3/16),
yanlış olduğunda üçü de `0.0` verip örtüşüyor. Bu dört op `BILINEN_OLUMLER`'de
(`run_simd_parity.sh:61`), dolayısıyla kapının ölçtüğü 47 satırın **hiçbiri**
bu ayrışmaya dokunmuyor. İlk çağıran kişi AVX2'de `NaN`, skaler derlemede
`-1.0` alacak.

---

### L04-09 | LOW | `include/simd_math.hpp:159-160`
**Kapsam matrisi (AVX2, `tests/run_simd_parity.sh` koşturularak ölçüldü) —
16 "KAPSAMLI" fonksiyonun 15'i kırılmaya dayanıklı, 1'i değil.**

| fonksiyon | mutasyon | rc | sonuç |
|---|---|---|---|
| `v2d_load` | lane takası | 1 | RED |
| `v2d_set1` | negatifleme | 1 | RED |
| `v2d_set` | lane takası | 1 | RED |
| `v2d_store` | **lane 2'ye yaz (tarihsel hata)** | 1 | RED |
| `v2d_add` | → mul | 1 | RED |
| `v2d_mul` | → add | 1 | RED |
| `v2d_div` | → mul | 1 | RED |
| `v2d_min` | → max | 1 | RED (4 diff satırı) |
| **`v2d_max`** | **→ min** | **0** | **GREEN (L04-03)** |
| `v2d_sqrt` | → kimlik | 1 | RED |
| `v2d_abs` | işaret maskesi kırık | 1 | RED |
| `v2d_hmin` | **lane 2,3 katlanıyor (tarihsel hata)** | 1 | RED |
| `v2d_hmax` | → min | 1 | RED |
| `v2d_all_finite` | → true | 1 | RED |
| `v2d_get_x` | → Y lane'i | 1 | RED |
| `v2d_get_y` | **→ lane 0 (tarihsel hata)** | 1 | RED |
| SSE2 `v2d_set` | lane sırası ters | 1 | RED (4 backend assert) |
| SKALER `v2d_abs` | `x<0?-x:x` (tarihsel skaler hata) | 1 | RED (2 diff satırı) |

`v2d_min`↔`v2d_max` tek ayrımının `min(den,0)` satırına dayandığı notu
L04-03'te ölçüldü.

---

### L04-10 | LOW | `tests/run_simd_parity.sh:61`, `:41-45`
**Beyan edilmiş 10 ölünün hepsi yapısal olarak algılanamaz; ayrıca kapı
x86-64'te hiçbir sevk derlemesinin SEÇEMEYECEĞİ bir backend'in üçte birini harcıyor.**

10 ölünün 10'unda da kapı **rc=0**:
```
v2d_sub→add · v2d_cmp_lt/le/gt/ge→always-TRUE · v2d_fast_exp→kimlik ·
v2d_fast_exp2→kimlik · v2d_fast_pow→+e · v2d_direction cos=1 · v2d_rotate→kimlik
   => hepsi rc=0 "Sonuç: PASS"
```
Bu **tasarım gereği** (envanter denetimi onları "hiç çağrılmıyor" diye
beyan ediyor, dolayısıyla hiçbir test onları yakalayamaz). Ancak
`SCALAR` backend'inin ulaşılabilirliği ölçüldü:
```
  -mno-sse2 -> SCALAR sizeof(v2d)=16
```
`scripts/build.sh:124/126` ve `CMakeLists.txt:69` hiçbir yerde SSE2'yi
kapatmıyor; SSE2 x86-64 ABI tabanı. Dolayısıyla **SCALAR backend hiçbir
x86-64 sevk derlemesinde seçilemez** — kapının 3 backend'inden 1'i,
üretimde var olmayan bir kod yolu.

---

### L04-11 | INFO | `CMakeLists.txt`
**SIMD kapısı CTest'e bağlı değil.**
```
$ grep -cE 'simd_parity' CMakeLists.txt
0
$ grep -n 'add_test' CMakeLists.txt
207:    add_test(NAME rawaccel-unit-tests COMMAND rawaccel-tests)
```
Tek `add_test` birimi. `ctest` tek başına SIMD kapısını **çalıştırmaz**;
yalnız `ci.yml:75` ve `run_tests.sh:733` üzerinden erişilebilir.

---

## ⭐ SORULARA DOĞRUDAN CEVAP

**1. Ham çıktı / rc / kaç vaka?** → Yukarıdaki KAPI bölümü. `RC=0`,
**47 `case` satırı × 3 backend = 141 gözlem**, 0 FAIL.

**2. Mutasyon — kapı kırmızıya dönüyor mu?** → **EVET, 26 fonksiyonluk tam
matriste 15/16 kapsamlı op kırıldığında kırmızıya döndü.** En güçlü kanıt:
tarihsel üretim hatalarının üçü de yakalandı —
`v2d_store`→lane 2 (rc=1), `v2d_get_y`→lane 0 (rc=1, 2 backend assert),
`v2d_hmin`→lane 2,3 katlama (rc=1), skaler `v2d_abs`→`x<0?-x:x` (rc=1).
**AMA** 4 mutasyon kapıyı **yeşil bıraktı**: `v2d_max`→`min` (L04-03),
`rawaccel.hpp:437` X/Y takası (L04-01), `:428` X/Y takası (L04-01),
`:445` `yx_output_dpi_ratio` düşürme (L04-02). Yani kapı **bazı sınıf
gerçek üretim hatalarına karşı kör** ve hangi sınıf olduğu ölçülmüş değil.

**3. Hangi tolerans?** → **İddia yolunda tolerans YOK**: 12/12 `expect()`
çağrısı `eps=0.0` (tam), pipeline iddiaları `!= 0.0` ve `isfinite`,
backend'ler arası karşılaştırma byte-tam `diff`. Tek "tolerans" 47 satırın
**5'inde** `%.12g` (`simd_parity.cpp:247`) baskısıdır. **Ölçülen gizleme
eşiği: bu 5 satırda ≈5e-12 … 1e-11 relative**, diğer 42 satırda ≤1e-13 —
yani **2–4 mertebe daha kör**, ve `run_simd_parity.sh:106-114`'ün
belgelediği FMA sapması (7.77e-14) bu tabanın altında.

**4. SIMD hangi girdi kümesinde sapıyor (NaN/Inf)?** → **396 gözlemlik
tarama ile ölçüldü: 16 KAPSAMLI op'ın hiçbiri NaN/Inf/denormal/±0'da
backend'ler arasında ayrışmıyor.** AVX2↔SSE2 = **0** ayrışma; AVX2↔skaler
= **60** ayrışma ve **60'ın 60'ı da** `v2d_cmp_lt/le/gt/ge` maske
op'larında (L04-08). Korkulan "`max`/`min` intrinsikleri NaN'da scalar'dan
sapıyor" sınıfı **gerçekleşmiyor** — çünkü MINPD/MAXPD unordered durumda
`src2` döner ve skaler `a<b?a:b` de yanlışsa `b` döner; iki kural örtüşüyor
(ölçüm: `min(nan,nan)=nan`, `minb(nan,nan)=nan`, `max=3.5`, `maxb=nan`,
her iki backend'de birebir aynı). Ayrışma yalnızca **maske** döndüren
4 op'ta ve bu op'lar "ölü" ilan edildiği için kapı onları hiç ölçmüyor.

**5. Tüm girdi aralığında mı, alt kümede mi?** → **Alt kümede.**
Ölçülen kapsam: **47 satır**, dağılımı 24 primitive + 5 pipeline + 18
extreme-pipeline. Bunların **tamamı tek bir profil ailesinden**:
`accel_x/y = classic`, `output_dpi = NORMALIZED_DPI`, `domain_weights={1,1}`,
`range_weights={1,1}`, `whole=false`, ve **her iki smoother halflife'si 0**.
0 girdileri `{0,100,50,3,7,-0,den,1e200,dmax,inf,nan}` — 5 taneli normal
pipeline + 9 taneli uç girdi. Kapsanmayanlar (ölçülmüş, kanıtsız bırakılmadı):
`power`/`natural`/`lookup`/`synchronous`/`jump` modları, `domain_weights`≠1,
`range_weights`≠1, `yx/lr/ud_output_dpi_ratio`≠1, **her iki smoother**,
`degrees_snap`, `apply_rotate`, `speed_min/max` clamp. Kapının karşılaştırdığı
"aralık" 26 `v2d_*`'nin **16**'sıdır; 10'u hiç çağrılmıyor.

**6. `-mavx2`/`-mavx512` bayrakları?** → **Numerik sonucu DEĞİŞTİRMİYOR**
(bağımsız yeniden ölçüm, 7 bayrak kümesi, hepsi `result PASS`):
```
  gate_avx2  (-O2 -mavx2)                                            47 satır
  gate_sse2  (-O2 -mno-avx2)                                         47 satır
  gate_scalar(-O2 -mno-sse2)                                         47 satır
  fma        (-O2 -mavx2 -mfma)                                      47 satır
  o3         (-O3 -mavx2)                                            47 satır
  shipping   (-O3 -march=native -mfma -msse3 -mssse3 -msse4.1
              -msse4.2 -mavx)          == build.sh:126 SIMD_FLAGS     47 satır
  portable   (-O3 -mfpmath=sse -msse2) == build.sh:124 / CMakeLists   47 satır

  pairwise vs gate_avx2: 0 / 0 / 0 / 0 / 0 / 0  farklı case satırı
  -mavx512f ile derleme: select edilen backend AVX2, çalıştırma SIGILL (rc=132)
```
`simd_math.hpp:78-87`'deki iddia **bağımsız olarak yeniden üretildi ve doğru
çıktı**. CI'nin derlediği bayrak: `ci.yml:19` → `RAWACCEL_PORTABLE: "1"`
→ `build.sh:124` → `-mfpmath=sse -msse2` → **SSE2** backend. Yani **CI hiç
bir zaman AVX2'yi sevk bayraklarıyla derlemiyor**; AVX2'yi yalnız
`run_simd_parity.sh`'in `-mavx2` bayrağıyla, `ci.yml:75`'ten test ediyor —
ve bu, `simd_math.hpp:66-126`'ın kendi kabul ettiği boşluktur.
AVX-512 tarafı: **yol yok** (L04-07).

---

## KAPSANMAYAN (lane dışı — birinin bakması gereken yer)

- **`tests/run_tests.sh` ve `tests/oracle/run_oracle.sh` aynı kör noktaları
  paylaşıyor mu?** L04-01/02'nin mutasyonlarını (smoother + `yx_output_dpi_ratio`)
  bu iki kapı da kırmızıya döndürüyor mu, **ölçmedim** — lane dışı.
  Ölçüm, `simd_parity` dışındaki tüm gate'lerde aynı sed ile tekrarlanabilir.
- **`include/math-vec2.hpp` (referans)** — `:66 rotate()` / `:70 direction()`
  skaler ikizlerinin canlılığı (`rawaccel.hpp:329,357,508`) envanter denetiminin
  dayandığı varsayımdır; bağımsız doğrulamadım.
- **`accel-*` modlarının SIMD yolu** — `power`/`natural`/`lookup` v2d kullanmıyor
  gibi görünüyor; kapının profil ailesiyle sınırlı olmasının accel tarafındaki
  sonucunu ölçmedim.
- **`%.*g` baskı seçimi** `run_oracle.sh`'ta da ayrı bir kör nokta olarak
  `simd_math.hpp:120-127`'de işaretlenmiş; o lane'ın konusu.

## TEMSIL SINIRI

- **PS5.1 / çalışma anı yok** — tüm ölçümler bu makinede (Intel Core i9-9900K,
  `avx2`+`fma` VAR, `avx512f` **YOK**; GCC 16.2.1). `-mavx512f` yalnızca
  derleme zamanı ölçüldü; **AVX-512 donanımında hiçbir şey koşturulmadı**
  (L04-07 bir varsayıma dayanır: `-mavx512f`'in gerçek bir AVX-512 CPU'da
  yine AVX2 backend'i seçmesi, GCC bayrak modeli üzerinden ölçüldü).
- **L04-05'i gerçek pre-Haswell donanımda ölçmedim** — SIGILL'i AVX2 derlemesine
  `__builtin_trap()` ile taklit ettim. Bu, `run_simd_parity.sh`'ın gördüğü
  kabuk çıkış durumu (132) açısından birebir aynı yoldur ama gerçek bir
  donanım SIGILL'i değildir.
- **`-mfma` sapmasının canlı pipeline'daki 140/16539 izole yüzey sayımını
  yeniden üretmedim** — o, `simd_math.hpp`'ın kendi kaydı; benim ölçtüğüm
  yalnız 47 satırlık gate çıktısı üzerindeki tespit tabanı (L04-04).
- **Sanitizer altında SIMD kapısını koşturmadım** (ASan/UBSan `test_accel.cpp`'i
  kapsıyor, `simd_parity.cpp`'yi kapsadığı `run_tests_asan.sh` kapsam dışı bırakılmış
  olabilir — **doğrulamadım**).
- **FMA/ters-hata mutasyonlarının hepsi `v2d_mul` üzerinden tek bir yüzeyde
  ölçüldü**; 42 satırın "≤1e-13" tabanı bu tek mutasyonun sonucu, her satırın
  kendi mutasyonu değil.