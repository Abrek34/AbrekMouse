### L01 | alt-ajan (L01 — Classic + Power hızlandırma algoritmaları) | 2 Ekim 2026

KAPSAM   : `include/accel-classic.hpp` (277 satır), `include/accel-power.hpp` (223 satır).
           Referans (okuma): `docs/research/formulas.md`, `docs/research/deviations.md`,
           `tests/oracle/known_deviations.txt`, `tests/oracle/ref/accel-{classic,power}.hpp`,
           `tests/oracle/oracle_cases.hpp`, `src/config.cpp:400-514`, `include/config.hpp:18-70`,
           `include/math-vec2.hpp:186`, `include/rawaccel.hpp:327-328`, `gui/graph.inl:23,56`,
           `tests/test_accel.cpp` (monotonicity bölümleri).
           Hiçbir kaynak dosya değiştirilmedi. Başlangıç/son `sha256sum` aynı:
           `accel-classic.hpp 856fd1e1…7dad`, `accel-power.hpp 8454b631…353b` (3 sunucu yeniden
           başlatması boyunca `/tmp` silindi, `/tmp/l01final` altında yeniden kuruldu).

---

## YÖNETİCİ İÇİN 3 CÜMLE

1. `power` tarafında iki **ölü dal** var (koruma gibi görünüyor, hiç çalışmıyor):
   `accel-power.hpp:95-97` ve `accel-classic.hpp:236`/`:257`. Kanıtlandı, pozitif kontrolüyle.
2. `accel-classic.hpp:272`'deki **BUG-NEW-71 clamp** (`ex>64 → 64`) erişilebilir bir üss
   bandı yaratıyor: `1 < exponent_classic < 1.015625` aralığında port referanstan **%99.8'e
   kadar** sapıyor, **bu bantta gain monotonikliğini kaybediyor**, ve ne oracle ne birim test
   görebiliyor (mutasyonla kanıtlandı: 1408 satırın **0**'ı değişiyor).
3. `docs/research/deviations.md` **bayat**: 1047/45 diyor, gerçek 1408/79. Ayrıca
   `power_tinyexp_floor` (23 satır) ve `p155_io_cap0_gain` (12 satır) sınıflarını hiç anmıyor.

---

BULGULAR :

### L01-B01 | `include/accel-classic.hpp:272` | **HIGH** — kayıtsız sapma + sessiz yeşil

**İddia.** `gain_accel()` içindeki `if (!(ex > 0) || ex > 64.0) ex = 64.0;` clamp'i, clamp
edildiği bandın TAMAMINDA (`1 < exponent_classic < 1.015625`) portu referanstan ayırıyor.
Bant **erişilebilir** (sanitize `[1,10]`), **oracle göremiyor**, **birim test göremiyor**,
**hiçbir araştırma belgesinde kayıtlı değil**.

**Kanıt 1 — bant ve büyüklüğü** (`/tmp/l01final/verify.cpp`, `-DL01_REF` ile iki derleme):
`classic GAIN + io, cap=(150,1.5)`; `1/(e-1)` clamp eşiğinin 64 olduğu noktada bitiyor:

```
LOCAL exp=            1/(e-1)=      s=1      s=100    s=149.9  s=150    s=1000
LOCAL      1           inf      1.005    1.005    1.005    1.005    1.005
LOCAL      1.00005     20000    1.99753  1.99776  1.99778  1.99778  1.57467
LOCAL      1.001       1000     1.95177  1.95616  1.95655  1.95655  1.56848
LOCAL      1.01        100      1.60648  1.63506  1.63764  1.63764  1.52065
LOCAL      1.0156      64.1026  1.45582  1.48977  1.49287  1.49288  1.49893
LOCAL      1.015625    64       1.45523  1.4892   1.4923   1.49231  1.49885
LOCAL      1.0157      63.6943  1.45503  1.48915  1.49227  1.49227  1.49884   <-- bitti

REF   exp=            1/(e-1)=      s=1      s=100    s=149.9  s=150    s=1000
REF        1           inf          2        2        2        2        1.575
REF        1.00005     20000        1        1        1        1        1.425
REF        1.001       1000     1.497    1.4993   1.4995   1.4995   1.49993
REF        1.01        100      1.47086  1.49305  1.49505  1.49505  1.49926
REF        1.0157      63.6943  1.45503  1.48915  1.49227  1.49227  1.49884   <-- birebir aynı
```
`exp=1.0157`'de (1/(e-1)=63.69 < 64) iki taraf **bit-bit aynı**; `exp=1.0156`'da (64.10 > 64)
ayrılıyor. Bant kenarı tam olarak `power = 1 + 1/64 = 1.015625`.
En kötü sapma **exp=1.00005, hız=150: local 1.99778 vs ref 1.0 → göreli hata 0.998 (%99.8)**.

**Kanıt 2 — clamp yük taşıyor (mutasyon).** `/tmp/l01final/mutant/include/accel-classic.hpp`
kopyasında clamp satırları silindi, 2562 satırlık sanitize-zarfı taraması koşuldu:
```
rows: 2562   rows CHANGED by removing the BUG-NEW-71 clamp: 413
   gain=1 cap_mode=0: 413 rows
exponent range of changed rows: [1.0002617, 1.0154383]
example gain=1 cm=0 exp=1.0002617 speed=150
   shipped (clamped)  : 1.9884548761200824
   clamp removed      : 1
```

**Kanıt 3 — ⭐ SESSİZ YEŞİL: oracle bu clamp'i göremiyor.** Aynı mutantla 1408 satırlık
oracle grid'i koşuldu (`/tmp/l01final/mut.cpp` → `oracle_cases.hpp` üzerinden):
```
$ diff mut_grid.out mut_grid_mutant.out | grep -c '^<'
0        (1408 satırın 0'ı değişiyor)
```
Nedeni ölçüldü: `tests/oracle/oracle_cases.hpp` içindeki **11** `mkclass(...)` çağrısının
tamamı `exponent_classic ∈ {0.5, 1.5, 2.0}` kullanıyor — bantta **0** satır:
```
classic grid rows inside the clamp band (1, 1.015625): 0
```

**Kanıt 4 — ⭐ SESSİZ YEŞİL: birim testler de göremiyor.** `test_accel.cpp`'in classic
bölümleri (`:8417` P91 exp≤1, `:8438` exp=1.01 "hızla değişmeli", `:5237` R9 monotonik,
`:1856` test_monotonic) mutant başlığa karşı birebir koşuldu:
```
=== SHIPPED ===                    [MUTANT: BUG-NEW-71 clamp removed]
SUBSET RESULT: PASS (fails=0)      SUBSET RESULT: PASS (fails=0)
```
`:8439`'daki tek `exponent_classic = 1.01` örneği `cap_mode_val = cap_mode::out` kullanıyor
(bant yalnız `io`'da biter), dolayısıyla göremez. `grep -cE "exponent_classic *= *1\.0[0-5]"
tests/test_accel.cpp` → **1** (`tests/test_accel.cpp:8439`).

**Kanıt 5 — erişilebilirlik.**
`src/config.cpp:420` → `if (a.exponent_classic < 1.0) a.exponent_classic = 1.0;`
`src/config.cpp:421` → `if (a.exponent_classic > 10.0) a.exponent_classic = 10.0;`
Yani `[1,10]` aralığı **korunuyor**; 1.0156 bir daemon config'i olarak yazılabilir.
GUI `make_spin(1,10,…)` aynı tabanı kullanıyor.

**Kanıt 6 — belgelerde kayıt YOK.**
```
$ grep -rn "BUG-NEW-71" --include=*.md --include=*.txt --include=*.cpp --include=*.hpp . \
    | grep -v "^./.aihaberlesme"
./include/accel-classic.hpp:264:        // BUG-NEW-71: for power ≈ 1 the exponent 1/(power-1) explodes
./olcum/aj5/shadow/include/accel-classic.hpp:264:  (satın alınmış kopya)
$ grep -rn "ex > 64\|BUG-NEW-71" docs/research/*.md
(rc=1 — 0 eşleşme)
```
`docs/research/formulas.md §5.1` "port korumaları" tablosunda da yok. Yani brifing §2
anlamında **kayıtsız sapma = bulgu**.

**Etki notu ( dürüst sınırlama ).** Yerel cevap referanstan daha "güvenli" görünüyor
(referans `exp=1.00005`'te gain=1'e düşüyor, port 1.998'de kalıyor) ama **bu bir tesadüf**:
port da referans da `exp=1`'de monotonikliği kırıyor, sadece **ayrık bantlarda**. Ayrıca
portun `1.0 + ...` lineer yolu BUG-7/CUR-1 korumalarını atladığı için bu bant *ayrıca*
B01-B02'deki monotonik kırığına yol açıyor. Düzeltme önerisi bu turda yok (salt okuma).

---

### L01-B02 | `include/accel-classic.hpp:162-167` + `:271-272` | **HIGH** — portun İÇERDİĞİ monotoniklik kırığı

**İddia.** Aynı clamp bandında classic GAIN+io eğrisi `cap_x` dizinden sonra **düşüyor**.
Referans aynı bantta **monotonik**. Yani port, referansın *ayrık* bir bantta kırıldığı yerde
kırılıyor — iki tarafın kırılma bantları örtüşmez.

**Kanıt** (`verify.cpp` F2 bölümü, 1201 log-aralıklı nokta, 1e-9 … 6.87e22 = üretim tavanı):
```
== F2 monotonicity, classic GAIN + io, cap=(150,1.5), 1201 log pts 1e-9..6.87e22 ==
LOCAL exp=1           violations=0     worst_drop=0
LOCAL exp=1.00005     violations=389   worst_drop=0.0281986   gain 1.9758525 -> 1.9476539 at x=166.798
LOCAL exp=1.001       violations=387   worst_drop=0.0258626   gain 1.9364321 -> 1.9105695 at x=166.798
LOCAL exp=1.005       violations=380   worst_drop=0.0169827   gain 1.7865839 -> 1.7696012 at x=166.798
LOCAL exp=1.01        violations=368   worst_drop=0.00779703  gain 1.6315752 -> 1.6237781 at x=166.798
LOCAL exp=1.015       violations=318   worst_drop=0.00038304  gain 1.5064638 -> 1.5060808 at x=166.798
LOCAL exp=1.0156      violations=0     worst_drop=0
LOCAL exp=1.02 … 10   violations=0     worst_drop=0

REF   exp=1           violations=389   worst_drop=0.0283241   gain 1.9779707 -> 1.9496465 at x=166.798
REF   exp=1.00005 …10 violations=0     worst_drop=0
```

**Kök neden (ölçüldü).** `accel-classic.hpp:166` → `constant = (base_fn(cap_x,…) − cap_y)·cap_x`.
Clamp `accel_raised`'ı değiştirdiği için `constant` bu bantta **pozitif** oluyor; GAIN kuyruğu
`:66` → `constant / x + cap_y` olduğundan `x` arttıkça **azalır**. Konstrüktör alanları
doğrudan okunarak ölçüldü (`classic` struct'i `public`):
```
FIELDS exp=1.001  accel_raised=0.95176605  cap_x=150  cap_y=0.5  constant= 68.482045157694614   <-- POZİTİF -> düşen kuyruk
FIELDS exp=1.01   accel_raised=0.60647683  cap_x=150  cap_y=0.5  constant= 20.645905397764452   <-- POZİTİF
FIELDS exp=1.05   accel_raised=0.37066036  cap_x=150  cap_y=0.5  constant= -3.5714285714285672  <-- negatif -> yükselen kuyruk
FIELDS exp=2      accel_raised=0.0016666667 cap_x=150 cap_y=0.5  constant=-37.5                 <-- negatif
```
`constant` işaretinin bandın içinde değiştiği nokta, monotonik kırığının da başladığı noktadır.

**"Sınır bir yerde ters yöne geçiyor mu?" — EVET, burada.** `cap.y` bir *tavan* olarak
istenmiş, `cap_x`'ten sonra eğri tavana **aşağıdan değil yukarıdan yaklaşıp sonra geri
düşüyor**; yani istenen sınırın yönü, istenen tavanın altına doğru çevriliyor.

**Sessiz yeşil boyutu.** `AGENTS.md` ve `tests/test_accel.cpp` "classic monotonicity"
kapsaması vaat ediyor, ama vaat edilen testler `cap_mode::out` dışında hiçbir şey kurmuyor
(`make_args` `tests/test_accel.cpp:157` → `cap_mode_val = cap_mode::out`). Aynı testin
kendi grid'i, `cap_mode`'u `out → io` yapmak dışında değiştirildiğinde **kırmızıya dönüyor**
ölçüldü (`/tmp/l01final` `covertest.cpp`):
```
test_monotonic (test_accel.cpp:1856) classic
    config   : exp=2.0 cap=(15,1.5) cap_mode=out  [make_args defaults]
    sweep    : 0.1, then i=2..40 step 1.0  (cap.x=15 IS crossed)
    violations seen by the test's own grid: 0

  SAME sweep + cap.x=15, cap_mode=io, exp=1.001  -> 25 violations <= test_monotonic WOULD FAIL
  SAME sweep + cap.x=15, cap_mode=io, exp=1.005  -> 25 violations <= test_monotonic WOULD FAIL
  SAME sweep + cap.x=15, cap_mode=io, exp=1.01   -> 25 violations <= test_monotonic WOULD FAIL
  SAME sweep + cap.x=15, cap_mode=io, exp=1.05   ->  0 violations
```

---

### L01-B03 | `include/accel-power.hpp:95-97` | **MED** — ⭐ ÖLÜ DAL (koruma görünüyor, hiç çalışmıyor)

**İddia.** `power` ctor'undaki LEGACY `switch (args.cap_mode_val)` içindeki
`case cap_mode::io:` **hiç girilemez**, çünkü `io + LEGACY` girdi `:81`'de `return` ile
ctor'dan çıkıyor.

**Kanıt 1 — komut + ham çıktı.** Başlığın byte-eşit mantıklı, `HIT()` sayaçlı kopyası
(`/tmp/l01final` `power_instr.hpp`, repo dosyası değişmedi) sanitize-zarfı grid'i ve
NaN/Inf/denormal enjeksiyonu ile koşuldu:
```
$ ./instr_power grid | grep "LEGACY switch"
GUARDFIRE	grid	LEGACY switch: case in       62
GUARDFIRE	grid	LEGACY switch: case out      62
$ ./instr_power grid | grep -c "LEGACY switch: case io"
0
$ ./instr_power all | grep "LEGACY switch"
GUARDFIRE	all	LEGACY switch: case in      118
GUARDFIRE	all	LEGACY switch: case out     118
$ ./instr_power all | grep -c "LEGACY switch: case io"
0
```
Yani **hijbir girdi kümesinde** — 373 config grid'i + 6 zehir × 8 alan × 6 mod/bölme
enjeksiyonu dahil — bir kez bile girilmiyor.

**Kanıt 2 — bağımsız akış kanıtı + pozitif kontrol** (sayacın bozuk olmadığını göstermek için):
```
POWER ctor control-flow sweep over 288 (gain x cap_mode x cap.x x cap.y) combos
  P155 identity guard returned early      : 19
  io+LEGACY early return (:81)            : 42
  LEGACY cap_mode switch REACHED          : 96
    case in                               : 48
    case out                              : 48
    case io                               : 0   <-- accel-power.hpp:95-97
  control: same input with the :81 early return REMOVED (hypothetical edit)
    case io counter = 48  => the counter mechanism is sound
```

**Etki.** Davranışsal değil (`:80` zaten `legacy_cap = args.cap.y`'i atıyor, aynı değer),
ama **aynı atamanın üçüncü kopyası** ve `io` yolunun *orada* işlendiği izlenimi veriyor —
brifing §3'teki "koruma mevcut deniyor, çalışmıyor" şekli.

---

### L01-B04 | `include/accel-classic.hpp:236` ve `:257` | **MED** — ⭐ İKİ ÖLÜ KORUMA

**İddia.** `base_accel`'in `if (power <= 1.0 || …)` ve `gain_inverse`'in
`if (power <= 1.0) …` dalları **hiç çalışmıyor**, çünkü ctor `:28`'de
`exponent_classic <= 1.0`'da `return` ediyor; onların tek çağıranları `init_legacy`/
`init_gain` o noktaya hiç ulaşamıyor.

**Kanıt (831 600 config'lik sanitize-zarfı taraması + pozitif kontrol):**
```
GUARD                                    ENTERED   CALLS   VERDICT
classic ctor exp<=1 short-circuit           59400    831600   LIVE (blocks init_* below)
base_accel  guard  power <= 1.0                 0    772200   ** DEAD — never entered **
base_accel  guard  x <= input_offset       424710    772200   live
gain_inverse guard accel == 0               70200    631800   live
gain_inverse guard power <= 1.0                 0    631800   ** DEAD — never entered **
gain_accel  guard  denom == 0              424710    772200   live

--- POSITIVE CONTROL (harness CAN see these guards) ---
base_accel(power=0.5)   guard power<=1 entered: 1   (counter moved 0 -> 1)
gain_inverse(power=0.5) guard power<=1 entered: 1   (counter moved 0 -> 1)
=> the mechanism is proven; the zeros above are real zeros.
```
Aynı sonuç bağımsız olarak, `classic_instr.hpp` sayaçlı kopyasıyla teyit edildi:
`base_accel: guard power<=1 -> 0` ve `gain_inverse: guard power<=1 -> offset` etiketleri
ne grid koşusunda ne de enjeksiyon koşusunda **hiç görünmüyor** (komut:
`./instr_classic grid | grep -c 'guard power<=1'` → 0).

**Etki.** Davranışsal değil, ama bu iki satır yanlış bir güvence izlenimi veriyor: bugün
`base_accel`'i `init_legacy` dışına taşıyan biri iki guard'dan birinin *hatta gerekli
olmadığını* varsayar. Ayrıca `base_accel`'in asıl koruması olan `x <= input_offset` (O5)
**canlı** (424 710 kez) — yani O5'in kendisi sorun değil.

---

### L01-B05 | `include/accel-power.hpp:140-143` | **MED** — legacy yolda local finiteness guard YOK

**İddia.** `power::operator()` legacy dalı, dosyanın kendi kabul ettiği "local guard"
sözleşmesini tutmuyor: GAIN dalı CUR-3 clamp'ine sahip (`:167-169`), `base_fn_impl`
`isfinite` içermiyor, ve legacy dalı da içermiyor.

**Kanıt (kod + ölçüm).**
`accel-power.hpp:140-143`:
```
if (!gain_mode) {
    double out = base_fn_impl(speed);
    return minsd(out, legacy_cap);
}
```
`accel-power.hpp:149-151` yorumu ise: *"GAIN path had no local Inf guard … **All other accel
modes guard locally; match them**."* Legacy alt-dalı bu sözleşmeye girmiyor.
Ölçüm (`/tmp/l01final` `verify.cpp` F4 + geniş enjeksiyon taraması):
```
LOCAL power  : tested=870  non-finite outputs=67
REF   power  : tested=870  non-finite outputs=303
LOCAL classic: tested=660  non-finite outputs=10
REF   classic: tested=660  non-finite outputs=157
```
Örnek sızıntı satırı (zehir `scale=+Inf`, power LEGACY, `cap_mode::in`):
`out = pow(Inf·x, n) = Inf`, `legacy_cap = base_fn_impl(15) = Inf`,
`minsd(Inf, Inf) = Inf`. Bugün bunu yalnızca `minsd`'nin karşılaştırma sıralaması
(`include/math-vec2.hpp:186` → `return a < b ? a : b`, yani NaN/±Inf'te `b`'yi döndürür)
maskeliyor — **tasarlanmış bir guard değil, karşılaştırma sırasının bir yan etkisi.**
Ayrıca 91 satırlık geniş tarama (`4608` deneme) içinde **yalnız 4 satır** yerelde sızıyor,
referansta sızmıyor; dördü de `exponent_power=+Inf` (sanitize `finite_or` ile 1'e çevirdiği
için erişilemez).

---

### L01-B06 | `docs/research/deviations.md:4,12,13,22,98,190-194` | **MED** — belge bayat / kendi kendisiyle çelişiyor

**Kanıt.**
```
$ bash tests/oracle/run_oracle.sh | tail -3
total rows compared : 1408
documented deviations: 79 (known_deviations.txt)
RESULT: OK … (79 rows).

$ grep -vc '^#' tests/oracle/known_deviations.txt
79
```
`deviations.md:4` → *"1047 rows compared, **45** known deviations"*; `:12` → *"**45** rows"*;
`:23` → toplam 45. Gerçek: **1408 / 79**.

Ek, ölçülmüş çelişkiler:
- `:13` → *"detailed in §Current (1047/45)"* — dosyada **`§Current` bölümü yok**
  (`grep -n "^## " docs/research/deviations.md` → yalnız Breakdown/Class/Cross-class/P102).
- B1 sınıfı kendi içinde tutarsız: TL;DR (`:20`) **9 satır** diyor, bölüm başlığı (`:98`)
  **"(7 rows)"** diyor, tablo (`:100-108`) **7 satır** listeliyor. Gerçek `known_deviations.txt`:
  `sync_* @0` = **7** (`sync_gain_p2/p1/p07`, `sync_legacy_p2/p1` + `sync_legacy_mot05_gam2`
  + `sync_gain_mot2_gam05`).
- **`power_tinyexp_floor` (23 satır) ve `p155_io_cap0_gain` (12 satır) hiç anılmıyor** —
  `known_deviations.txt` içinde mevcut (`grep -v '^#' … | cut -f1 | sort | uniq -c` →
  23 ve 12). Yani 79 satırın **35'i** (%44) hiçbir araştırma belgesinde gerekçelendirilmemiş.
- `:22` ve `:190-194` "P155 gain ailesi listelenemez, `NaN > tol` asla doğru olmaz" diyor —
  bu **yanlış**: o 12 satır `known_deviations.txt:114-125`'te listeli. `AGENTS.md` aynı
  hatayı "corrected" diye not almış; `deviations.md` hâlâ eski metni taşıyor.
- `known_deviations.txt:5` *"Deviation classes (five…)"* diyor ama numaralı sınıf
  yorumu **4** (`grep -nE '^#  [0-9]\.'` → satır 12, 23, 27, 32); `:6-8`'in hesap satırı
  beş sınıfı 79'a topluyor. Yani dosyanın kendi başlığı beş sınıfı sayıyor, dördünü
  numaralandırıyor.

---

### L01-B07 | `gui/graph.inl:23` ve `:56` | **LOW** — sanitize **sadece girişte** değil: sanitizasyonsuz bir tüketici var

**İddia.** `sanitize_accel_args` (`src/config.cpp:400`) config yükleme ve IPC push'ta
çağrılıyor, ama GUI eğri önizlemesi `accel_union::init()`'i **doğrudan widget değerleriyle**
çağırıyor. Bu, `accel-classic.hpp:182-183`'ün kendi notuyla uyumlu ("Config load is
sanitized already, but the GUI live preview runs init_gain() on unsanitized widget values")
— ama **`accel-power.hpp` aynı notu taşımıyor**, yani power'ın önizleme yolunda da
sanitize dışı girdi olduğu belgelenmemiş.

**Kanıt.**
```
$ grep -rn "accel_union\|\.init(" --include=*.cpp --include=*.hpp --include=*.inl \
      include/ src/ daemon/ gui/ cli/ | grep -v "^tests/"
gui/graph.inl:23:    accel_union au; au.init(args);
gui/graph.inl:56:    au.init(args);
include/rawaccel.hpp:327:  settings.data.accel_x.init(settings.prof.accel_x);
include/rawaccel.hpp:328:  settings.data.accel_y.init(settings.prof.accel_y);
```
Etki **sınırlı** ve bu da ölçüldü: `compute_max_gain` (`graph.inl:27`, `:36`) gain'i
`std::isfinite(g)` ile süzer, `compute_curve` (`:62-64`) NaN/Inf'i kasıtlı olarak ham
bırakıp kırmızı işaretliyor (L-BUG-12). Yani fareyi öldüremez — yalnız **görsel önizleme**
etkilenir.

**Ek, ilgili gözlem (lane'ım sınırında, uyarı olarak).** `classic::operator()` `:45`/`:47`
her çağrıda `args.input_offset` ve `args.exponent_classic`'i **yeniden okur**, ama tüm
`isfinite` guard'ı `:81,:89,:116,:123,:147,:173,:194,:220-222,:229-231,:272` satırlarında
**türetilmiş sabitlere** uygulanmıştır. Yani **sanitize sınırı ctor'dur**; `init()` sonrası
`args` mutasyona uğrarsa guard'lar yeniden çalışmaz. Üretimde reload `rawaccel.hpp:327-328`'de
yeniden `init()` çağırdığı için bu **latent**, canlı bir hata değil.

---

### L01-B08 | `include/accel-power.hpp:121` (`break` vs `return`) | **INFO** — şu an zararsız, ama tuzak

Referans `ref/accel-power.hpp:122-123` bu dalda `constant_b = 0; return;` der; port `:121`
`break` der ve `:135`'e düşer. Port yolunda `base_fn_impl(0) = offset.y` olduğu için
`integration_constant(0, offset.y, offset.y) = (offset.y − offset.y)·0 = 0` — yani bugün
**birebir aynı sonuç**. Ölçüldü: bu dal 9 kez çalıştı (grid) / 9 kez (enjeksiyon) ve
hiçbir sapma üretmedi. Yine de iki taraf aynı sonucu **iki farklı mekanizmayla** üretiyor;
`offset.y` mantığını değiştiren bir düzenleme sessizce ayrılır.

---

## SORULARA DOĞRUDAN CEVAP

**S1 — Kod `formulas.md`'deki formülle birebir uyuşuyor mu? Sapma `deviations.md`'de kayıtlı mı?**
Hayır, ve kayıtsız sapma var. `formulas.md §2.3`'ün dediği tek classic sapması (exp≤1 sabit
gain) **kayıtlı** (class A, 23 satır). Ama `accel-classic.hpp:272`'deki clamp'in yarattığı
`1 < exp < 1.015625` bandı **hiçbir yerde kayıtlı değil** ve oracle/birim test göremiyor →
**L01-B01 (HIGH)**. Ayrıca `formulas.md` sayı olarak da bayat ("31 sapma", gerçek 79).

**S2 — `cap_modes` (limit/direct/clamp) üç modda da sınır dışı girdide doğru mu? Sınır ters yöne geçiyor mu?**
Üç modun da sanitize-zarfı içinde **yönü doğru**, tek istisna B02'de ölçülen bant. Ayrıntı:
- `cap.y < 1` → dört yerde `sign = -1` (`:76`, `:127`, `:144`, `:201`) → gain hızla arttıkça
  **azalır**. Bu **kasıtlı** (CUR-2, `:50-55` yorumu) ve referansla aynı; sanitize grid'inde
  133 kez ölçüldü.
- `cap.y == 1.0` **tam** → `:198-199` `cap_x = 0` → cap erişilemez, gain = 1; referansla
  birebir aynı ölçüldü.
- `cap_mode::in` + `cap.x == input_offset` → `base_accel` O5 guard'ı (`:236`) canlı (424 710 kez);
  yerel gain 1.0 (kimlik), referans `cap.y`'ye donmuş değer. **Kayıtsız** ama yerel daha güvenli;
  MED değil, LOW/INFO. `k1_legacy_in_*` oracle vakaları `:114`'teki `>=` sınırını zaten pinliyor.
- **Ters yön: B02** — `1 < exp < 1.015625`, classic GAIN+io, `cap_x`'ten sonra düşüş.

**S3 — NaN/±Inf/denormal: her giriş yolunda sanitize var mı? Sadece girişte mi?**
İki düzeyde cevap:
- *Üretim konfigürasyonu üzerinden*: **evet, kapılı.** 39 105 satırlık sanitize-zarfı
  taramasında **yerel portun NaN/Inf ürettiği satır sayısı = 0**
  (`rows where LOCAL output is NaN/Inf: 0`). Bulunan 91 sızıntının tamamı zehirlenmiş bir
  alan gerektiriyor; `sanitize_accel_args` (`src/config.cpp:411-431`) hepsini önce `finite_or`
  ile sonluya çeviriyor.
- *Ama sanitize tek girişte değil ve her yol yok*: **B07** (GUI önizleme `graph.inl:23/:56`
  sanitize atlar) ve "sanitize sınırı ctor'dur" gözlemi. Header'ların içinde ise guard'lar
  **dağınık ve dala özgü**: `base_fn` iki tane (`:229`, `:231`), `base_fn_impl` **hiç**,
  power GAIN operatörü bir tane (`:167`), power legacy operatörü **hiç** → **B05**.
- Denormal: `base_fn_impl`'in `scale*x` çarpımı 5e-324 × 6.87e22 = ~3.4e-302 (normal) — sorun
  yok; ölçüldü, denormal `scale` tek başına non-finite üretmiyor (zincirin tek non-finite
  noktası `scale <= 0` idi, sanitize `:449` ile 0.01'e kaldırıyor).

**S4 — Power'da `0^negatif` veya `log(0)` alan hatası üreten yol var mı?**
**Hayır, ölçüldü.**
```
$ grep -nE "std::log|std::log2|std::log1p|std::exp" include/accel-classic.hpp include/accel-power.hpp
(0 eşleşme, rc=1)
$ grep -oE "std::[a-z0-9_]+" include/accel-classic.hpp | sort | uniq -c
     12 std::pow /  1 std::max /  1 std::fabs
$ grep -oE "std::[a-z0-9_]+" include/accel-power.hpp | sort | uniq -c
      5 std::pow /  1 std::fabs
```
Yani iki dosyada **hiç `log`/`exp` yok** → `log(0)` üretilemez. `pow` çağrılarının hepsinin
üssü **kesin pozitif**:
`scale_from_gain_point` `:200` `pow(g/(n+1), 1/n)` (n>0),
`scale_from_output_point` `:208` `pow(diff, 1/n)` (n>0),
`gain_inverse` `:191` `pow(g/(n+1), 1/n)` (n>0),
`base_fn_impl` `:176` `pow(scale*x, exponent)` (`exponent = max(ep,1e-3) > 0`, `:36`),
`gain_fn` `:180` `pow(input*sc, power)` (aynı n).
Ayrıca `0^0`/`0^pos = +0` (C99 6.2.6.3p5) — alan hatası değil. Overflow → Inf durumu
`isfinite` guard'larıyla yakalanıyor ve **doğrulandı**:
`gain_inverse(100, 1e-3, 1)` → `inf` → `:192` → `DBL_MAX`;
`scale_from_gain_point(100, …, 1e-3)` → `inf` → `:201` → `1.0`;
`scale_from_output_point(100, …, 1e-3)` → `inf` → `:209` → `1.0`.

**S5 — Monotoniklik.** 893 config × 1201 log-nokta = **1 072 493** komşu çift tarandı
(local) ve aynı grid referansta koşuldu. Sonuç:
```
cases violating monotonicity  LOCAL=75  REF=79
  VIOLATES ONLY IN LOCAL PORT : 4      <-- L01-B02 (HIGH)
  VIOLATES ONLY IN REFERENCE  : 8      (port iyileştirmiş)
  violates in BOTH            : 71     (referansın kendi tasarımı: negatif ivme ve cap.y<1)
LOCAL-ONLY (4 vaka, hepsi classic GAIN + io):
  exp=1.001  n=387  worst drop=0.0258626  x 156.913 -> 166.798  gain 1.93643 -> 1.91057
  exp=1.005  n=380  worst drop=0.0169827
  exp=1.01   n=368  worst drop=0.00779703
  exp=1.001 pnear1_capy1.5 (aynı config, ikinci isimlendirme)
  exp=1.01  pnear1_capy1.5 (aynı config)
```
Yani **monotoniklik genel olarak doğru değil**, ama portun *kendi* getirdiği kırık yalnız
B02 bandı; 71 vakada port referansla **aynı** davranıyor (referansın `cap.y<1` işaret
çevirmesi ve negatif ivme "yavaşlatma" tasarımı dahil).

**S6 — ⭐ SESSİZ YEŞİL.**
Evet, dört kalem:
1. `accel-power.hpp:95-97` — **ölü dal**, `io+LEGACY` `:81`'de erken çıkıyor (B03).
2. `accel-classic.hpp:236` `power <= 1.0` — **ölü guard** (B04).
3. `accel-classic.hpp:257` `power <= 1.0` — **ölü guard** (B04).
4. `accel-classic.hpp:272` clamp'i — **görünen koruma, ama etkisi ters yönde**:
   koruma "eğri kimliğe çöker" diye yazılmış, ölçümde ise **referanstan %99.8 sapma ve
   monotonik kırık** üretiyor; üstelik **hiçbir kapı onu görmüyor** (oracle 1408/1408 aynı,
   birim test alt kümesi mutantla da yeşil) → **B01/B02, HIGH**.

---

KAPI     :
| kapı | komut | rc | ÜRETİLEN SAYI |
|---|---|---|---|
| Oracle (resmi referans farkı) | `bash tests/oracle/run_oracle.sh` | **0** | `total rows compared : 1408` / `documented deviations: 79` / `known deviations seen: 79` / `RESULT: OK` |
| Birim + entegrasyon | `bash tests/run_tests.sh` | **0** | `=== Sonuç: 34164/34164 geçti ===` + `Sonuç: PASS — 3 backend test edildi` |
| L01 F1/F2 clamp bandı + monotoniklik | `cd /tmp/l01final && ./v_loc` / `./v_ref` (`verify.cpp`, iki derleme) | 0 | LOCAL F1 13 satır + F2 13 satır; REF aynı; 1201 nokta/vaka |
| L01 F3/F4 enjeksiyon | `./v_loc` / `./v_ref` F3+F4 | 0 | classic 660 deneme → LOCAL 10 / REF 157 non-finite; power 870 deneme → LOCAL 67 / REF 303 |
| L01 mutasyon (oracle görüşü) | `diff mut_grid.out mut_grid_mutant.out \| grep -c '^<'` | 0 | **0 / 1408** satır değişiyor |
| L01 mutasyon (birim test görüşü) | `./gm_ship` / `./gm_mut` (`gate_mut.cpp`, classic bölümleri) | 0/0 | ikisi de `SUBSET RESULT: PASS (fails=0)` |
| L01 ölü-dal kanıtı | `./instr_power grid\|all \| grep -c "LEGACY switch: case io"` | 0 | **0** (karşılaştırma: `case in` 62/118, `case out` 62/118) |
| L01 ölü-guard kanıtı | 831 600 config taraması + pozitif kontrol | 0 | `power<=1.0` guard'ları 0/772200 ve 0/631800; pozitif kontrol 0→1 |
| L01 clamp yük-taşıma kanıtı | `./mut_noclamp` vs `./mut_pristine` | 0 | 2562 satırın **413**'ü değişiyor (hepsi `gain=1, cm=io`) |

KAPSANMAYAN:
- `modifier::modify()` / `motion_math` / hız işleme katmanı — bu lane'ın dosyaları dışında;
  kazandığım gain sapmalarının son kullanıcıda ne kadar görüleceği (range/domain ağırlıkları,
  snap, EMA) ölçülmedi. **Özellikle B01/B02'nin gerçek fare etkisi bu lane'den ölçülemez.**
- `include/accel-union.hpp` (dispatch) yalnız okundu, ayrı denetim yok.
- GUI'nin önizleme yolu (`gui/graph.inl`) **çalıştırılmadı** (GTK runtime yok); B07 yalnız
  çağrı noktalarının ve `isfinite` süzgeçlerinin okunmasıyla kanıtlı.
- Oracle grid'inin **kapsam dışı** bölgeleri: hız 0 (kapsam içi ama ayrı sınıf), 1e5 üstü
  hızlar, `cap.y=0 & cap_mode=io & cap.x=0` (P155'in ikinci yarısı) — bu girdiler
  `known_deviations.txt`'te **yok**, grid'de de yok; L01 ölçümünde sapma üretiyorlar ama
  kapsam dışı bırakıldı.
- `tests/oracle/ref/**` — satın alınmış referans, yalnız karşılaştırma hedefi olarak kullanıldı.

TEMSIL SINIRI:
- **Fiziksel doğrulama yok.** Gerçek fare, `/dev/uinput`, daemon yok; yalnız gain fonksiyonları
  ölçüldü. B01/B02'nin "farede ne hissedilir"i iddia edilmiyor.
- **Optimizasyon koşulu: `-O1`.** Oracle'ın kendi koşulu bu. `-march=native` / `-mfma` ile
  B01 bandının **tam kenarı** (`1.015625`) kayabilir; ölçmedim.
- **`run_tests_asan.sh` koşmadım** — yedi kapıdan biri değil ve bu iki header'da işaretçi/dizi
  işi yok (yalnız `double` aritmetiği). UBSan burada yeni bir şey göstermez diye
  **varsayım olarak bırakıldı, ölçülmedi.**
- **GUI yok** (B07 çalıştırılmadı, okundu).
- Mutasyon deneyleri **yalnız `/tmp` kopyalarında** yapıldı; çalışma ağacı 3 sunucu yeniden
  başlatması boyunca `sha256sum` ile doğrulandı, değişmedi.
- `formulas.md`'nin §5.1 "port korumaları" tablosunu **tek tek doğrulamadım** (5 satır);
  B01 dışındakiler için "kayıtlı/ölü" ayrımı satır düzeyinde teyit edilmedi.
- `deviations.md` §P102 (senkron) bölümü bu lane'ın dışında — hiç ölçülmedi.

---

## AJ1 İÇİN NOT (yeniden doğrulama kolaylığı)

B01'i tek komutla doğrulamak için gerekenler (hepsi `/tmp/l01final`'de duruyor):
1. `cp include/accel-classic.hpp /tmp/x.hpp` → `:272`'deki `|| ex > 64.0` kısmını sil.
2. Oracle grid'i mutantla derle → `diff` → **0 satır değişiyor**.
   (Kendi komutum: `g++ -std=c++20 -O1 -I . -I tests/oracle mut.cpp` sonra
   `diff mut_grid.out mut_grid_mutant.out | grep -c '^<'` → `0`.)
3. `./v_loc | sed -n '/F1/,/F2/p'` → `exp=1.00005 … s=150 → 1.99778`;
   `./v_ref` aynı satırda `1`.
4. `./v_loc | sed -n '/F2/,/F3/p'` → `exp=1.00005 violations=389`;
   `./v_ref` aynı satırda `violations=0`.
