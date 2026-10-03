### L02 | alt-ajan (Natural/Synchronous/Lookup/Jump denetimi) | 2026-10-01

**KAPSAM** (kesin — `ls include/accel-*` ile doğrulandı, 8 dosya çıktı, 4'ü bana ait):
- `include/accel-natural.hpp` (171 satır)
- `include/accel-synchronous.hpp` (175 satır)
- `include/accel-lookup.hpp` (130 satır)
- `include/accel-jump.hpp` (85 satır)

Referans (salt okuma): `docs/research/formulas.md`, `deviations.md`, `parameter_index.md`,
`tests/oracle/ref/accel-{natural,synchronous,lookup,jump}.hpp`, `tests/oracle/oracle_cases.hpp`.
Ölçüm ağacı: `/tmp/opencode/l02/` + `/tmp/opencode/l02/repo/` (tam repo aynası).
**Çalışma ağacına hiçbir yazma yapılmadı** (aşağıdaki kanıt).

---

## SORU 1 — Lookup: LUT sıralaması, sırasız interpolasyon, bölme, boş LUT

### 1a. Sıralama sanitize SONRASINDA yapılıyor — **DOĞRU, ölçüldü**

```
$ grep -n 'sanitize_accel_args\|sort_lut_data' src/config.cpp
610:    sanitize_accel_args(p.accel_x);
611:    sanitize_accel_args(p.accel_y);
612:    // LUT data: sort by speed so binary search in lookup::operator() works correctly
613:    sort_lut_data(p.accel_x);
614:    sort_lut_data(p.accel_y);
```
Sıra doğru: önce clamp/finite, sonra sıralama. Pozitif kontrol (aşağıdaki 5b).

`src/config.cpp:371-390` insertion sort — stabil (`>` karşılaştırması), `n<=1` erken çıkar,
float aralığında. **NaN X sıralanmaz** ama JSON okuyucu veriyi zaten sonluya zorluyor
(`src/config.cpp:213` `if (!std::isfinite(v)) v = 0;`). Ölçüldü: sıralama 0/NaN üretmiyor.

### 1b. ⭐ Pozitif kontrol — "0 eşleşme" iddiaları bu projede iki kez YANLIŞ çıktı

```
$ grep -c 'sanitize' include/accel-*.hpp
include/accel-classic.hpp:5      <-- arama sağlam çalışıyor (pozitif kontrol)
include/accel-jump.hpp:0
include/accel-lookup.hpp:0
include/accel-natural.hpp:0
include/accel-noaccel.hpp:0
include/accel-power.hpp:0
include/accel-synchronous.hpp:0
include/accel-union.hpp:0
```
`accel-classic.hpp` 5 buldu → grep sağlam. Yani **bana ait 4 dosyanın hiçbirinde
`sanitize` çağrısı YOK.** Sadece `default:` dalında da değil — hiç yok. `sanitize`
`src/config.cpp:517-615` içinde yaşıyor ve `load_config`→`sanitize_profile` üzerinden
çağrılıyor. Bu, soru 5'in cevabının temeli.

### 1c. Sıralanmamış LUT ile interpolasyon ne döndürüyor — **TAMAMEN YANLIŞ, ölçüldü**

`lookup::operator()` (`:82-95`) ikili arama yapıyor; sırasız veride arama sonucu
anlamsız. Ölçüm (`q1_lut_sort.cpp`, sanitize YOK, doğrudan `lookup(args)`):

```
  before         len=8 : 20 2 5 1.2 10 1.5 0 1
  size=4 velocity=0
    lut(  0.00) = 0          <- referans x<=0 -> 0 (bilinçli)
    lut(  2.50) = 2          <- OLMAYAN 1.1 olmalıydı! (0,1.0)-(5,1.2) arası
    lut(  5.00) = 1.200000048 <- doğru (tam nokta)
    lut(  7.00) = 1.320000029
    lut( 10.00) = 1.5
    lut( 15.00) = 1.75
    lut( 20.00) = 2
    lut( 25.00) = 2.25       <- son noktadan SONRASI 4.0 değil 2.25: son segment (10,1.5)-(20,2.0)
```
`lut(2.5)=2.0` — `(0,1.0)` ve `(5,1.2)` arası olması gereken değer **2.0**. Yani
`hi = size-2` aralığının dışına (`lo=2` kalıyor, `pts[2*2]=10` üzerinden lerp) kayıyor.
Gain modunda daha vahşi:
```
    lut(  0.50) = 0.1 ; lut(  5.00) = 0.2400 ; lut( 10.00) = 0.15
```
sırasız LUT **monoton değil** ve tam noktalarda bile yanlış y değeri veriyor.

**Erişilebilirlik ölçüldü — `load_config` sıralıyor, yani JSON yolu GÜVENLİ:**
```
$ ./q1b   (load_config + sanitize_device_profile + modifier::modify)
  [UNSORTED-8   ] gain=0 len=8 sorted_x: 0 5 10 20  | y: 1 1.2 1.5 2
        accel gain(x) =  1.02 1.08 1.2 2 11
```
`UNSORTED-8` girdisi `[20,2, 5,1.2, 10,1.5, 0,1]` idi → yüklemeden sonra **sıralı** geldi.
**Doğru cevap: sıralanmamış LUT yalnızca sanitize'dan kaçarsa bozulur, ve iki yazma
yolu (JSON okuyucu, GUI `lut_set_points` `:368-372` std::sort) ikisini de sıralıyor.**

### 1d. ⭐ ÖLÇÜLEN ASIL BULGU — tek noktalı LUT, GAIN modunda **tam ölü imleç**

`lookup.hpp:121-127`, "x ilk noktanın altında" dalı:

```cpp
double y = static_cast<double>(pts[1]);
if (velocity) {
    double x0 = static_cast<double>(pts[0]);
    if (x0 <= 0) return 0.0; // port guard: avoid div-by-zero
    y /= x0;
}
```

`size == 1` ise `hi = size - 2 = -1` → ikili arama **hiç çalışmaz** → `lo = 0` kalır →
`lo > 0` false → **her x için** bu dal çalışır. `x0 <= 0` ise `0.0` döner.
GAIN modunda gain = 0 demektir. Ölçüldü:

```
=== Q1d: empty LUT (length=0 and length=1) ===
    len=2 -> size=1
      lut2(  0.00) = 0
      lut2(  1.00) = 0.3      <- pts[1]/x0 = 1.5/5, sabit
      lut2(100.00) = 0.3
=== Q1e: single point, velocity mode ===
    x0=0  size=1: lut(0.5)=0 lut(1)=0 lut(2)=0 lut(10)=0     <== ÖLÜ
    x0=-3 size=1: lut(0.5)=0 lut(1)=0 lut(2)=0 lut(10)=0     <== ÖLÜ
```

**Uçtan uca `load_config` ile erişilebilirlik ölçüldü** (salt JSON, sanitize'dan geçer):
```
  [single neg x ] gain=1 len=2 sorted_x: -5  | y: 2
        accel gain(x) =  0 0 0 0 0
        modify(counts) out.x =   raw=2->0  raw=20->0  raw=200->0  raw=2000->0   <<< ÖLÜ İMLEÇ
  [single x=0   ] gain=1 len=2 sorted_x: 0  | y: 2
        accel gain(x) =  0 0 0 0 0
        modify(counts) out.x =   raw=2->0  raw=20->0  raw=200->0  raw=2000->0   <<< ÖLÜ İMLEÇ
  [ALL ZERO x   ] gain=1 len=4 sorted_x: 0 0  | y: 0 0
        modify(counts) out.x =   raw=2->0 ... raw=2000->0                        <<< ÖLÜ İMLEÇ
```
`lut_length:2, lut_data:[0.0, 2.0]` — **meşru, sanitize'dan geçen, geçerli bir config →
her hızda gain 0 → imleç tamamen ölü.** `sort_lut_data` bunu engellemiyor çünkü
sıralama negatifi öne değil, zaten sıralı.

Referans davranışı farklı ve **daha kötü değil**: `tests/oracle/ref/accel-lookup.hpp:94-96`
`y /= points[0].x` — x0=0 ise ±Inf döner, sonra `modify()` sonundaki `isfinite` guard'ı
(`rawaccel.hpp:610-611`) 0'a çevirir → aynı ölü sonuç. Yani **referans-parite gerekçesi
bu guard'ı haklı çıkarmıyor: guard'ın KENDİSİ ölü imleç.** Doğrusu `1.0` (veya
`maximum_sens`) dönmeydi. Bulgu **HIGH** (kullanıcı bu profille fareyi kullanamaz,
tek bir JSON satırı, sessiz).

### 1e. Sıfıra bölme var mı? — **denetimli tarama: HAYIR, ama bir "ölü" yan yol var**

Dört dosyada tüm bölme noktaları elle denetlendi:
- `lookup:92,111,116,125` — `y /= x` üç yerde, hepsi `x > 0` garantili (`x<=0` → `:73` erken dönüş). `y /= x0` `:125` guard'lı (ama guard = 1e'de ölü sonuç, yukarıda).
- `lookup:114` `t = (x-ax)/denom` — `denom == 0` `:109`'da guard'lı (O2/P55, ölçüldü: duplicate X → sonraki noktanın `by`'si, sonlu).
- `synchronous:151-165` — `idx_safe = clamp(...)` `:158` var; **`t = idx_f - idx` `:164` clamp EDİLMEMİŞ `idx_f`'den hesaplanıyor** → aşağıda.
- `synchronous:171` `y /= x_start` — `x_start = scalbn(1,-3) = 0.125`, asla 0.
- `natural:50` `x < 1e-9` guard; `natural:163` `accel < 1e-12` guard; `natural:24` `abs_limit < 1e-9`.
- `jump:27` `rate_inverse < 1` guard; `jump:52` `x <= 0` guard; `jump:81` `isfinite(gain)` guard.

Denormal tarama (`q5_nan.cpp`, 13 girdi × 8 varyant):
```
  mode=5  gain=1 : nonfinite=4 of 13   first: x=NaN -> nan
     x=1e-320 (SUBNORMAL)       -> inf               *** NON-FINITE ***
     x=1e-310 (SUBNORMAL)       -> inf               *** NON-FINITE ***
```

### 1f. LUT boşken ne oluyor? — **temiz, ölçüldü**
```
    len=0 -> size=0 ; lut(5)=1 lut(0)=1 lut(-3)=1     <- noaccel
    len=1 -> size=0 ; lut(5)=1 lut(0)=1               <- tek çift yok, O6 guard'i
```
`size <= 0 → return 1.0` (`:62`). Doğru.

### 1g. LUT verisi içinde NaN → **binary search NaN karşılaştırmalarıyla çalışıyor**

```
=== Q1g: NaN/Inf inside LUT data ===
    lut(  5.00) = nan  finite=0
    lut( 10.00) = nan  finite=0
=== Q1h: NaN in the X column ===
    lut(  0.50) = 2 ; lut(5.0)=2 ; lut(10)=2 ; lut(19)=2 ; lut(25)=2   <-- hep aynı
```
X sütununda NaN → tüm karşılaştırmalar false → binary search `lo=0` dışına çıkamıyor →
**sabit 2.0** (tamamen sessiz, hiçbir hata değil). Erişilebilirlik: JSON okuyucu
`config.cpp:213` NaN'ı 0'a çeviriyor, GUI `lut_set_points` `safe_f` `:381-386` NaN→0.0f.
**yani üretimde erişilemez — MED (savunma derinliği boşluğu).**

**BULGULARIM bu blokta:**
| ID | dosya:satır | sınıf | kanıt |
|---|---|---|---|
| **L02-01** | `accel-lookup.hpp:121-127` | **HIGH** | Tek noktalı LUT + GAIN + `x0<=0` → `return 0.0` → **tüm hızlarda gain 0 = ölü imleç**. Uçtan uca ölçüldü: `load_config` JSON'u `[-5.0,2.0]` ve `[0.0,2.0]` → `modify(2000 counts)` → `out.x = 0` (normalde 2500). Tam çıktı yukarıda. `sort_lut_data` engellemiyor. Guard'ın kendisi ölü imleç; `modify()`'in `isfinite` guard'ı (`rawaccel.hpp:610`) zaten ±Inf→0 yapıyor, yani guard'ın tek yaptığı ölü imleci sabitlemek. |
| L02-02 | `accel-lookup.hpp:92-117` | MED | Sırasız LUT ikili arama sonucunu bozar: `lut(2.5)=2.0` (olması gereken 1.1). **Erişilemez** (iki yazma yolu da sıralıyor, ölçüldü: `load_config` sıralıyor). Ayrıca LUT içi NaN → X sütununda sabit 2.0 (sessiz). Erişilemez. |
| L02-03 | `accel-lookup.hpp` (genel) | INFO | Sıralama sanitize sonrası **doğru** (`config.cpp:610-614`). Boş LUT → 1.0. Denetimli taramada **sıfıra bölme yok**; `denom==0` ve `x0<=0` guard'ları yerinde. |

---

## SORU 2 — Synchronous: kare başına birden fazla motion event'i

### 2a. "synchronous" adı ne vaat ediyor, kod ne yapıyor — **UYUMLU, isim yanıltıcı değil**

`accel-synchronous.hpp:8-9`: *"Synchronous / activation-framework acceleration (reference
RawAccel `activation_framework`)"*. Yani "synchronous" = **activation framework**, yani
hız eşiğine (`sync_speed`) göre hassasiyetin kilitlenmesi — kare/zaman senkronizasyonu
DEĞİL. `docs/research/formulas.md:93` "### 2.4 synchronous (activation-framework)" ile
birebir örtüşüyor. Ölçüm — **durable, saf fonksiyon**:

```
=== Q2a: is synchronous STATEFUL? call it twice with the same x ===
    legacy s(10) x3 = 1.4619242895057598 1.4619242895057598 1.4619242895057598  (identical=1)
    seq after 10.0: 0.666859444 0.8162401232 1.317683162 1.46192429 1.489217296 1.49609694
    fresh object same seq: 0.666859444 0.8162401232 1.317683162 1.46192429 1.489217296 1.49609694   <== BIT-IDENTICAL
    gain  s(10) x2 = 1.0360553741455079 1.0360553741455079 (identical=1)
```
`operator()` (`:42-50`) hiçbir üye değiştirmiyor. Geçmiş çağrılardan bağımsız.
**VAAT/KOD UYUMLU.** (INFO)

### 2b. Kare başına birden fazla motion event'i nasıl işliyor — **daemon katmanı, lane dışı ama ölçüldü**

`synchronous` algoritması bunu **görmez**: daemon REL event'lerini **toplar** ve
accelerator'ı **kare başına BİR kez** çağırır. Ölçüm (aynı toplam 100 count):

```
  n=1   per-event=187.5        per-frame(summed)=187.5   ratio=1
  n=8   per-event=186.952      per-frame(summed)=187.5   ratio=1.00293
  n=16  per-event=172.947      per-frame(summed)=187.5   ratio=1.08415
  n=32  per-event=100.286      per-frame(summed)=187.5   ratio=1.86966
```
Kare başına 32 event'lik bir batch'te per-frame framing per-event'e göre **%87 farklı**.
Yani "synchronous" adının kare-içi anlamı **yok**; framing daemon'un kararı.
(`daemon/daemon.cpp:2573` `std::array<input_event,32> read_batch`, `:2670` `for (i<read_count)`,
`:2725` SYN_REPORT sınırı.) **Bu lane'in dışında — daemon sahibi bunu bilmeli.**

### 2c. ⭐ GAIN LUT `args.data`'ya yazılıyor — konfigürasyondan izole, AMA izolasyon yapısal

`fill_lut` (`:107-133`) → `fill` (`:136-144`) → `args.data[i] = ...`. `args.data` `mutable`
(`rawaccel-base.hpp:81`). Ölçüm:

```
=== Q2b: LUT is written into args.data by the ctor — shared mutable state ===
    nonzero LUT slots written = 97 of 514 (LUT_RAW_DATA_CAPACITY)
    fp_rep_range{-3,9,8}.size() = 97          <- capacity 514'e karşı 97 kullanılıyor
=== Q2k(2): does the synchronous internal LUT leak into the saved config? ===
  DEVICE settings data[0..3] AFTER init = 0.0833333 0.09375 0.104167 0.114583  (LUT written)
  CONFIG      accel_x.data[0..3] AFTER init = 0 0 0 0  (isolated=YES)
  saved JSON contains "lut_data"? NO (clean)
```
**Doğru**, ve `config.cpp:84-90`'ın iddiası doğrulandı. İzolasyon **yapısal** (apply'da
kopya), guard değil. Tek `accel_args` üzerinde mode geçişi LUT'u ezer:

```
=== Q2k(3): a user LUT + mode switch in ONE object DOES get clobbered ===
  lookup LUT : 0 1 10 2 20 3 30 4 (length=8)
  after sync : 0.0833333 0.09375 0.104167 0.114583 0.125 0.135417 0.145833 0.15625 (length STILL=8)
  lookup now: lut(0)=0 lut(10)=1.00104 lut(20)=1.00052 lut(30)=1.00035
  (original intended curve was 1.0 / 2.0 / 3.0 / 4.0)
```
`args.length` 8 kalıyor, `data` ezilmiş → lookup sessizce **tamamen farklı** eğri
okuyor. Üretimde `apply_profile` kopyaladığı için **erişilemez** (L02-13, MED/INFO).

### 2d. ⭐ `gain_apply` interpolate parametresi clamp EDİLMEMİŞ — sessiz Inf kaçağı

`accel-synchronous.hpp:158-164`:
```cpp
double idx_safe = std::clamp(idx_f, 0.0, static_cast<double>(range.size() - 2));
unsigned idx = static_cast<unsigned>(idx_safe);
if (idx < static_cast<unsigned>(capacity - 1)) {
    double y = lerp(data[idx], data[idx+1], idx_f - idx);   // ← idx_f CLAMP EDİLMEDİ
```
`idx` clamp'li, **`idx_f` değil** → `t = idx_f - idx` tablo dışına kaçıyor. Ölçüm:

```
=== Q5d(a): synchronous GAIN, replicate gain_apply() step by step ===
    x=1.79769e+308  ilogb(x)=1023  e=min(1023, 8)=8   e>=range.start(-3)? 1
    idx_safe=95  idx=95  branch=lerp
    *** lerp t = idx_f - idx = 5.61779e+306  (idx was CLAMPED, t was NOT) ***
    lerp raw = a + t*(b-a) = inf   (a=715.271 b=763.271)
    ACTUAL apply() = inf
```
`formulas.md:163` R43/P74-BULGU-1 "synchronous LUT index UB (float-cast-overflow)" —
**o düzeltme `idx`'i clamp'lemiş, `t`'yi clamp'lememiş.** Kalan yol sonsuz büyük `t`.

**Erişilebilirlik ölçüldü — ÜRETİMDE ERİŞİLEMEZ:**
```
  production max |speed| = 32*INT32_MAX*1e6*1e6 = 6.87195e+22
  sync GAIN first non-finite x between ... unreachable by a factor of 1.455e+285 x
```
1.455e285 kat emniyet. **MED** (bugün erişilemez, ama guard'ın niyeti `t`'yi de korumaktı —
`formulas.md:163`'teki iddia bu haliyle eksik).

| ID | dosya:satır | sınıf | kanıt |
|---|---|---|---|
| L02-04 | `accel-synchronous.hpp:164` | MED | `t = idx_f - idx` clamp'lenmemiş `idx_f`'den → `x=DBL_MAX` → `apply()` = **inf**. Tam çıktı yukarıda. Üretim erişilemez (1.455e285× emniyet, ölçüldü). |
| L02-05 | `accel-synchronous.hpp:107-144` | INFO | Ctor `args.data`'nın ilk 97 slotunu yazar; **config'e sızmıyor** (ölçüldü: kaydedilen JSON'da `lut_data` yok). Tek nesnede mode geçişi kullanıcı LUT'unu eziyor — üretim yolu (`apply_profile` kopyası) koruyor. |
| L02-06 | `accel-synchronous.hpp:8-9, 42-50` | INFO | İsim **vaat ettiği şey**: activation-framework, kare senkronizasyonu değil. Algoritma durable/pur (`operator()` üye yazmıyor; 3 ardışık çağrı ve taze nesne bit-aynı). **VAAT UYUMLU.** |

---

## SORU 3 — Natural: decay 0 ve 1 uçları + `test_natural_decay_zero`

### 3a. Uçlar kararlı mı — **EVET, ikisi de** (ilk ölçüm hatam vardı, düzeltildi)

İlk koşumda `nonfinite=140723330735841` çıktı; **bu benim printf format hatasıydi**
(`double bad` + `%ld`). `long bad` ile tekrar:

```
=== Q3: natural decay_rate 0 / 1 and limit edges ===
  decay=0 limit=1.5 gain           accel=0             nonfinite=0  g(0.5)=1 g(1)=1 g(10)=1 g(1e4)=1
  decay=0 limit=1.5 legacy         accel=0             nonfinite=0  g(0.5)=1 g(1)=1 g(10)=1 g(1e4)=1
  decay=1 limit=1.5 gain           accel=2             nonfinite=0  g(0.5)=1.1839 g(1)=1.2838 g(10)=1.475 g(1e4)=1.499975
  decay=1 limit=1.5 legacy         accel=2             nonfinite=0  g(0.5)=1.3161 g(1)=1.4323 g(10)=1.499999999
  decay=0 limit=0/1.0/1.5/10/100   accel=0             nonfinite=0  (hepsi tam 1.0)
  decay=1 limit=0   gain           accel=1             nonfinite=0  g(0.5)=0.7869 g(1)=0.6321 g(10)=0.09999546 g(1e4)=0.0001
  decay=1e-13 limit=1.5 gain       accel=2e-13         nonfinite=0  g=1 (guard: accel<1e-12)
  decay=1 limit=1.5 gain off=20    accel=2             nonfinite=0  g(0.5)=1 g(1)=1 g(10)=1 g(1e4)=1.498975
```
**decay=0 → tam olarak 1.0**, 12 x değeri × 2 mod × 6 limit = 144 örnek, `nonfinite=0`.
Sebep `natural:163` `if (accel < 1e-12) return 1.0;`. **decay=1 → sonlu, limit'e göre
limit≥1 artan / limit<1 azalan (bu kasıtlı, `natural:19-20` yorumu).** Kararlı.

### 3b. ⭐ `test_natural_decay_zero` gerçekten bu sınırı mı örüyor — **EVET, MUTASYONLA KANITLANDI**

Test `test_accel.cpp:2921-2959`. Korduğu guard `natural:163`. **Kopya üzerinde
mutasyon yaptım** (`/tmp/opencode/l02/repo`, çalışma ağacı korundu):

```
MUT1 applied: natural'dan `if (accel < 1e-12) return 1.0;` satırı silindi
  FAIL  test_accel.cpp:2938  std::isfinite(g5)
  FAIL  test_accel.cpp:2939  g5 >= 1.0 - 1e-9
  FAIL  test_accel.cpp:2940  std::isfinite(g50)
  FAIL  test_accel.cpp:2941  g50 >= 1.0 - 1e-9
  (section: natural — decay_rate=0 (accel=0) no NaN)
  FAIL  test_accel.cpp:5370-5371  (section: P105 — natural: decay_rate=0 kolu (flat))
MUT1 rc=141
```
**Test gerçekten sınırı örüyor** — guard'ı silmek onu kırmızıya döndürüyor, 3 ayrı
SECTION düşüyor. Bu bir **etkin kapı**, sessiz yeşil DEĞİL. (INFO — olumlu bulgu)

| ID | dosya:satır | sınıf | kanıt |
|---|---|---|---|
| L02-07 | `accel-natural.hpp:24,50,163` | INFO | decay=0 **kararlı**: 144 örnek, `nonfinite=0`, tam 1.0. `test_natural_decay_zero` (`:2921`) bu sınırı **gerçekten** örüyor — MUT1 (guard silindi) → `test_accel.cpp:2938-2941` kırmızı. **Etkin kapı.** |

---

## SORU 4 — Jump: eşik geçişinde discontinuity (ÖLÇÜLDÜ)

### 4a. SERT ADIM — discontinuity VAR, ölçüldü

`jump:45-47` LEGACY `is_smooth()==false` → `if (x < step.x) return 1.0; return 1.0+step.y;`
Kesintisiz geçiş **yok**, adım `cap.x`'te tam:

```
  LEGACY cap=(15,1.5) smooth=0
    g(14.99850)=1  ->  g(15.00000)=1.5   JUMP=0.5
  LEGACY cap=(15,5) smooth=0
    g(14.99850)=1  ->  g(15.00000)=5     JUMP=4
```
Δinput = 1e-4'te gain 1→5. Eğim ölçümü (Δx=1e-6, 14.9..15.1 taraması):
`max|Δg| = 4 → rate 4e+06` (Δx'in 4e6 katı). **Bu kasıtlı** — "jump"in tanımı.

### 4b. ⭐⭐ SESSİZ YEŞİL: `smooth` **sessizce yok sayılıyor**, GUI ayarı etkisiz

`jump:26-27`:
```cpp
double rate_inverse = args.smooth * step.x;
smooth_rate = (rate_inverse < 1) ? 0.0 : (2 * M_PI) / rate_inverse;
```
`smooth * cap.x < 1` → `smooth_rate = 0` → `is_smooth()` false → **SERT ADIM**.
`cap.x = 15` (DEFAULT!) iken **her smooth < 0.0667 yok sayılıyor.** GUI spin adımı 0.01
(`gui/ui_builder.inl:247` `make_spin(0,1,0.01,0.5)`) — yani kullanıcı **0.01 / 0.02 / 0.05**
yazıp **düz adım** alıyor, ekranda "Smoothing: 0.05" yazarken:

```
  cap_x  smooth  smooth*cap_x  is_smooth  g(cap_x-)   g(cap_x)   g(cap_x+)
  15     0.01    0.15          0          1          5          5    <== SMOOTH REQUESTED, HARD STEP DELIVERED
  15     0.05    0.75          0          1          5          5    <== SMOOTH REQUESTED, HARD STEP DELIVERED
  15     0.0666  0.999         0          1          5          5    <== SMOOTH REQUESTED, HARD STEP DELIVERED
  15     0.0667  1.0005        1          3          3          3
  5      0.1     0.5           0          1          5          5    <== SMOOTH REQUESTED, HARD STEP DELIVERED
  100    0.01    1             1          3          3          3
```
`cap.x=5`'te **0.0667'ye kadar** (GUI'nin neredeyse tamamı) yok sayılıyor. Sınır `cap.x`'e
bağlı olduğu için kullanıcı adımı değiştirdiğinde yumuşatma **görünmezce açılıp kapanıyor**.

**MUTASYONLA KANIT:** `smooth>0` iken her zaman sigmoid üreten düzeltme yazdım:
```
MUT6 applied: jump artık smooth*cap.x<1 iken de smooth'u yok saymıyor
=== Sonuç: 34164/34164 geçti ===   MUT6 rc=0
```
**Test paketi hiç tepki vermedi.** 34164/34164 yeşil. Kapsam dışı bırakılmış.
Sadece `smooth ∈ {0, 0.5, 1.0}` test ediliyor (`test_accel.cpp:1292,1315` yorumu
`2π/(0.5·15)` — sadece `smooth_rate≠0` olan durumu doğruluyor).

**Referans da aynı** — `tests/oracle/ref/accel-jump.hpp:20-25` birebir `if (rate_inverse<1)`.
Yani **INHERITED davranış**; port hatası değil. Ama kullanıcıya **hiçbir yerde söylenmiyor**
ve GUI'de "Smoothing" etiketiyle **yanlış bir vaat** gösteriliyor. **HIGH** (sessiz yeşil).

### 4c. Yumuşatılmış sigmoid'in geçiş değeri = adımın ORTASI, adımın kendisi değil

```
    cap.y=1.5    g(step)=1.25    expected midpoint=1.25    delta=0
    cap.y=5      g(step)=3       expected midpoint=3       delta=0
    cap.y=100    g(step)=50.5    expected midpoint=50.5    delta=0
```
`smooth → 0+` iken `g(step)` **5 değil 3'e** gidiyor. Yani `smooth=0.0666` (yok sayılan)
ile `smooth=0.0667` (uygulanan) arasında **g(15) 5 → 3 sıçraması** var — yumuşatma
 açma/kapama düğmesi **gain'in değerini de değiştiriyor**, sadece sürekliliği değil.
GAIN modunda aynı: `smooth=0.1 g(15)=1.044` → `smooth=0.01 g(15)=1.0`.

### 4d. GAIN modunda SERT ADIM sürekli (iyi haber)
```
    gain g(14.99999800) = 1
    gain g(15.00000000) = 1
    gain g(15.00000200) = 1.00000053333      <- sürekli, türev sınırlı
    gain g(15.00001800) = 1.00000479999
  GAIN smoothed: max|Δg| over Δx=1e-7 = 1.69063e-08 at x=17.4489 (rate 0.1691)
```
`jump:56` `1.0 + step.y*(x-step.x)/x` → eşikte C0. Mükemmel.

| ID | dosya:satır | sınıf | kanıt |
|---|---|---|---|
| **L02-08** | `accel-jump.hpp:26-27` (+ `gui/ui_builder.inl:247`) | **HIGH** | `smooth*cap.x < 1` → `smooth_rate=0` → **GUI'de "Smoothing: 0.05" yazarken SERT ADIM**. `cap.x=15`'te sınır 0.0667 (GUI adımı 0.01 → 0.01/0.02/0.05 yok sayılıyor), `cap.x=5`'te 0.2'ye kadar. **MUT6** (davranışı gerçek sigmoid yap) → `=== Sonuç: 34164/34164 geçti ===` rc=0 — **test paketi hiç tepki vermedi**. Referans da aynı (`ref/accel-jump.hpp:20-25`) → INHERITED; ama kullanıcıya söylenmiyor. |
| L02-09 | `accel-jump.hpp:45-47, 26-27` | MED | SERT ADIM discontinuity **ölçüldü**: `cap=(15,5), smooth=0` → `g(14.9985)=1 → g(15.0)=5`, eğim 4e6 (Δx=1e-6'da Δg=4). Kasıtlı. Ek: yumuşatma eşiğini geçerken `g(step)` adım değerinden (5) **sigmoid ortasına (3)** gidiyor — `delta=0` ile doğrulandı, yani açma/kapama düğmesi gain değerini de sıçratıyor. |
| L02-10 | `accel-jump.hpp:54-57, 80-82` | INFO | GAIN modu **sürekli**: `g(15.0)=1 → g(15.000002)=1.0000005`, max eğim 0.169. `jump:81` `isfinite` guard'ı denormal koruması olarak çalışıyor. |

---

## SORU 5 — Dördü de NaN/Inf/denormal girdide güvenli mi?

### 5a. `sanitize` çağrısı **gerçekten var mı, yoksa `default:` dalında mı?**

```
$ grep -n 'sanitize' include/accel-{natural,synchronous,lookup,jump}.hpp
(4 dosyada da 0 eşleşme)   -- pozitif kontrol: accel-classic.hpp → 5 eşleşme
```
**SESSİZ YANIT: DÖRT DOSYADA DA `sanitize` ÇAĞRISI YOK, HİÇ YERDE — `default:` dalında bile.**
`classic` 5 kez yapıyor; bu dördü **hiçbir şekilde**. Tüm güvenlik dış katmanda:
`src/config.cpp:517-615` (`sanitize_profile`) → `load_config` (`config.cpp:668`) /
`sanitize_device_profile` (`config.cpp:1078`).
**Bu bir sessiz-yeşil riski değil, aksine iyi haber**: sanitize merkezî ve yazma yolları
ona gidiyor. Ama algoritma dosyaları **kendi başına** savunmasız.

### 5b. ÖLÇÜM: non-finite girdi → çıktı matrisi (13 girdi × 8 varyant)

```
=== Q5-SUMMARY: count of NON-FINITE outputs ===
  mode=2  gain=0 : nonfinite=2 of 13   first: x=NaN -> nan       <- NATURAL legacy
  mode=2  gain=1 : nonfinite=2 of 13   first: x=NaN -> nan       <- NATURAL gain
  mode=3  gain=0 : nonfinite=0 of 13                            <- SYNCHRONOUS legacy  ✅
  mode=3  gain=1 : nonfinite=1 of 13   first: x=DBL_MAX -> inf  <- SYNCHRONOUS gain   (L02-04)
  mode=5  gain=0 : nonfinite=1 of 13   first: x=+Inf -> inf      <- LOOKUP legacy      (L02-11)
  mode=5  gain=1 : nonfinite=4 of 13   first: x=NaN -> nan       <- LOOKUP gain        (L02-11)
  mode=1  gain=0 : nonfinite=1 of 13   first: x=NaN -> nan       <- JUMP legacy        (L02-12)
  mode=1  gain=1 : nonfinite=0 of 13                            <- JUMP gain          ✅
```
(sayılar `make_args()` fixture'ı ile, yani `test_accel.cpp:144` — testlerin kendi
parametreleriyle yeniden üretildi)

### 5c. ⭐ NATURAL: **hiç girdi guard'ı yok** — NaN/Inf sızıyor

`natural:28-32`: `if (x <= offset) return 1.0;` — NaN her `<` karşılaştırmasını geçer
(`NaN <= 0` false), sonra `exp(-accel*t)` NaN verir. `isfinite` **hiç yok**.
```
  mode=2 gain=0 : nonfinite=2   x=NaN -> nan ;  x=+Inf -> -nan
```
**HIGH**: `+Inf` girdi `-nan` üretiyor, `natural` tek dosya.

### 5d. LOOKUP: velocity modunda 4 kaçak, legacy'de 1

```
=== Q5d(b): lookup leaks, exact path ===
    LEGACY lookup, x=+Inf:
      t = (x - ax)/(bx - ax) = (inf - 20)/(40-20) = inf
      lerp(3,4,Inf): ... maxsd(Inf,4)=Inf
      ACTUAL apply(+Inf) = inf
      (bracketing branch, t=+Inf; the denom==0 guard does NOT catch t)
    GAIN lookup, x=1e-320 (SUBNORMAL): apply = inf
      y=lerp(1,2,1e-321)=1 ; y/=x -> inf   <== denormal/denormal
```
`lookup.hpp:109` `denom==0` guard'ı `t`'yi yakalamıyor. Denormal/denormal bölme
**1e-320 → inf**, denormalı "denormal girdi" diye saymıyorum, `isfinite` dışına çıkıyor.

### 5e. JUMP: legacy açık, gain kapalı — **tutarsız**

`jump:81` `if (!std::isfinite(gain)) return 1.0;` **yalnız GAIN dalında** (`:52` sonrası).
LEGACY (`:43-47`) hiçbir koruma yok: `x=NaN` → `is_smooth()` true → `decay(NaN)=exp(NaN)=NaN`
→ `step.y/(1+NaN)=NaN` → `+1.0` → NaN. **HIGH-tutuarsızlık** (aynı dosyada iki dal,
biri korumalı biri değil).

### 5f. Peki bu sızıntılar ÜRETİMDE ne yapar? — `modify()` sonundaki guard söndürüyor

`rawaccel.hpp:610-611`:
```cpp
if (!std::isfinite(in.x)) in.x = 0;
if (!std::isfinite(in.y)) in.y = 0;
```
**Ölçtüm (mutasyonla):** bu guard'ı KOPYADA kaldırdım (MUT3) → hâlâ yeşil.
`modifier::modify` NaN'ı sıfırlıyor → **event sessizce kayboluyor** (o kare için
hareket yok). Yani `natural` NaN'ı üretirse imleç o event'te duruyor, ama sistem
çökmiyor. **Etki: görünmez hareket kaybı, çökme değil.** Bu yüzden HIGH değil
**MED-HIGH** seviyesinde raporluyorum; asıl bulgu **sessizlik**.

### 5g. ⭐ MUTASYONLA: korumanın gerçekten nerede olduğunu buldum

```
MUT3 applied: modifier::modify() NaN tail guard removed (test speeds still include NaN/Inf)
=== Sonuç: 34164/34164 geçti ===   MUT3 rc=0
```
Guard'ı kaldırdım, hâlâ yeşil → demek ki **başka bir yer** koruyor. Aradım:
```
MUT4 applied: motion_math guards ALSO removed; rawaccel tail guard already removed
  FAIL  test_accel.cpp:4051  bad_remainder == 0
  FAIL  test_accel.cpp:5063, 5069, 5078, 5079  (BUG-15 — motion_math)
  NaN/Inf in mode noaccel: speed=nan ...      <-- İLK KIRILAN mode noaccel!
MUT4 rc=1
```
`sorumluluk = daemon/motion_math.hpp:52-53` (`isfinite(tx)`) ve `:70-71`
(`isfinite(remainder)`). Yani **dört algoritmanın hiçbirinin kendi girdi koruması yok;
sistemin NaN'ı sönümlemesi tek bir yerde, ve o yer `test_accel.cpp:5739-5744`'te
`apply_motion_math` üzerinden dolaylı olarak test ediliyor.** `mode noaccel` (hiç
ivme yok!) ilk kırılan → koruma ivme algoritmasının değil, **hareket matematiğinin**.

| ID | dosya:satır | sınıf | kanıt |
|---|---|---|---|
| **L02-10** | `accel-natural.hpp:28-32` | **HIGH** | **Girdi guard'ı YOK** (`grep -n sanitize` → 0, pozitif kontrol `accel-classic.hpp` → 5). Ölçüldü: `x=NaN → nan`, `x=+Inf → -nan`, **her iki varyantta**. `if (x <= offset)` NaN'ı geçiriyor. `natural.hpp:97` yorumu guard'ı "stops NaN/inf" diye tanımlıyor — bu **yalnız GAIN dalında ve yalnız `x` için** doğru, **genel bir güvence değil**. |
| **L02-11** | `accel-lookup.hpp:114-117, 124-125` | **MED** | `x=+Inf → inf` (bracketing dalı, `denom==0` guard'ı `t`'yi yakalamıyor); GAIN'de `x=NaN→nan, x=+Inf→-nan, x=1e-320→inf, x=1e-310→inf` (**denormal/denormal**). `lookup.hpp:103-107` yorumu "Return the later point's value — bounded" diyor; bu **yalnız `denom==0` için** doğru, `t=Inf` için değil. |
| **L02-12** | `accel-jump.hpp:43-48` | **MED** | LEGACY dalda `isfinite` guard'ı **yok** (`:81` yalnız GAIN'de). `x=NaN → nan`. Aynı dosyada iki dal, biri korumalı biri değil — `jump.hpp:75-79` yorumu korumanın **bütün dosya** için olduğunu ima ediyor, ölçüm öyle değil. |
| L02-13 | 4 dosya geneli + `motion_math.hpp:52-53,70-71` | INFO | **Dört dosyada da `sanitize` çağrısı YOK** (`default:` dalında bile değil — hiç yok). Güvenlik tek yerden: `motion_math.hpp` sonu guard'ları. **MUT3** (guard kaldır) → hâlâ yeşil; **MUT4** (`motion_math` guard'ları da kaldır) → kırık, ve ilk kırılan `noaccel`. Yani ivme algoritmalarının hiçbiri kendini korumuyor; koruma hareket matematiğinde. Etki: **çökme değil, sessiz event kaybı**. |

---

## SORU 6 ⭐ — Monotonluk: output input ile aynı yönde mi? (SAPMA ÖLÇÜLDÜ, TAHMİN EDİLMEDİ)

4001 noktalık log-aralık taraması. **"Düşen adım" sayısı bile monotonluğun
kendisi değil — bir egri için doğru yön `cap.y>1` ise artan, `<1` ise azalan.
Önce yönü, sonra sapmayı ölçtüm.**

### 6a. LOOKUP GAIN — ⭐ "monotonluk" burada YANLIŞ SORU (bilinçli sapma)

```
=== Q6g: lookup monotonicity (sorted LUT, both variants) ===
  lookup gain=0 (velocity=0): decreasing steps=0 worst=0 at x=0
  lookup gain=1 (velocity=1): decreasing steps=8000 worst=2299.94 at x=1.00231e-06
```
**8000/8000 düşüş.** Ama bu **tanım**: `velocity`'de `y` bir **çıkış hızı**, gain = `y/x`.
Çıkış hızı sabit-türevli bir eğri ise gain `1/x` gibi azalır — **bu doğru davranış**,
`lookup.hpp:42-44` ve `parameter_index.md:87` bunu açıkça söylüyor. Bu **bulgu değil**.
`gain=0`'da 0 sapma → saf yön testi **temiz geçiyor**.

### 6b. JUMP LEGACY — sert adım nedeniyle "düşüş" sayılıyor, **ama 0.029'luk GERÇEK sapma var**

```
  jump LGCY     cap(15,3) sm0.5      dir=+1 : VIOLATIONS=0     worst=0
  jump LGCY     cap(15,0.5) sm0.5    dir=-1 : VIOLATIONS=0     worst=0
  jump GAIN     cap(15,3) sm0.5      dir=+1 : VIOLATIONS=0     worst=0
  jump LGCY     cap(15,3) sm0 (HARD) dir=+1 : VIOLATIONS=0     worst=0
  jump LGCY     cap(15,3) sm0.01 IGN dir=+1 : VIOLATIONS=0     worst=0
```
**Yön farkındalıklı test 0 sapma** — jump **mükemmel monoton**. (smooth=0.01'i "IGN" diye
etiketledim çünkü smooth_rate=0 → sert adım → zaten sığ durağan; 6b notu L02-08'e bakın.)
İlk (yön'süz) taramadaki 1524 "düşüş" **tamamen sert adımın kendisi** (`cap.y=1.5<step`
bölgesi) ve 0.029'luk sapma da `x=15.2757`'de sigmoid'in kuyruğunda, `dir` doğru
algılandığında **sıfırlanıyor**. **Bulgu yok.**

### 6c. NATURAL GAIN — ⭐⭐ EN CİDDİ SAPMA: **-352`ye kadar NEGATİF kazanç**

```
=== Q6d: natural GAIN, limit=100 decay=1e-9 — NEGATIVE gain, reachable? ===
  x:          1e-09     1e-08     1e-07     1e-06     5e-06     1e-05    0.0001     0.001      0.01       0.1         1        10     1e+02
  gain:           1          1          1          1          1          1         1         1    0.8047         1          1          1          1
  negative-gain points in x in [1e-9,100]: 292 of 4000,  min gain=-352.167 at x=5.53032e-06
      with w=1 -> scale=-352.167  (INVERTS the axis)
```
**292/4000 noktada kazanç NEGATİF.** Ölçek `1+(g-1)*w = -352` → **eksen ters dönüyor.**
`limit=100 decay=1e-9` → kazanç `0.8047` dönerken (`natural.hpp:140-141` yorumundaki
"19.5% hata" bandı) **-352**'ye düşüyor — yorumun tarif ettiği bandın **çok daha kötüsü**,
ve **yorum "NOT a sign flip" diyor.**

**Genişlik ölçüldü** (41 limit × 41 decay_rate × 600 hız):
```
  cells=1681  cells containing at least one NEGATIVE gain = 350 (20.8%)
  worst negative gain = -7.48957e+06 at limit=45 decay_rate=4.46684e-11 x=1.04312e-09
```
**1681 hücrenin %20.8'i negatif kazanç üretiyor.**

### 6d. ⭐ Başlığın KENDİ güvenlik iddiası yanlış — ölçtüm

`natural.hpp:146-148`:
> *"It is a 19.5% error, NOT a sign flip and NOT a NaN: **no reachable input produces a
> negative gain** (measured over the same 864 points)."*

Önce **başlığın kendi çözünürlüğünü** yeniden ürettim:
```
=== Q6h(1): the header's OWN grid — 9 limits x 12 decay_rates x 8 speeds ===
  points=864 scorable=864  |g-1|>1e-9: 479   NEGATIVE g: 1  worst=-8.76562 at x=0.0001
  -> at THIS resolution: negative-gain points = 1
```
Başlığın gridinde **gerçekten 1 negatif nokta var** — yani "no reachable input produces
a negative gain" cümlesi **kendi ölçümünde bile zaten yanlıştı.** Daha ince çözünürlükte:
```
=== Q6h(2): FINER grid — 41 limits x 41 decay_rates x 600 speeds ===
  cells=1681  cells containing at least one NEGATIVE gain = 350 (20.8%)
  *** The header's own sentence 'no reachable input produces a negative gain'
      is FALSE at 1.5x-finer limit spacing and a 12-decade speed sweep.
```
**HIGH — yanlış güvenlik beyanı.** `natural.hpp:97` "The guard above stops NaN/inf" ve
`:146` "no negative gain" → ikisi de ölçümle çürüdü. Bu, tam brifing §3'teki
"kod çalışıyor görünüyor" sınıfı.

### 6e. ⭐⭐ Uçtan uca ERİŞİLEBİLİRLİK ÖLÇÜLDÜ — `load_config` → `modify()` → EKSEN TERS

Bu kritikti: sanitize `limit ∈ [0,100]`, `decay_rate ∈ [0,10]` izin veriyor
(`config.hpp:58 LIMIT_MAX=100`, `:60 DECAY_RATE_MAX=10`). Negatif band `x≈1e-9..1e-5`.
**Üretimde `x` (hız) ne kadar küçük olabilir?**
```
  ips_factor range = [0.0003125, 16000]        (dpi 32000, t=100ms  ->  min)
  min non-zero speed = 1 count * min_ips_factor * domain_weight = 0.0003125 * w
  For the negative-gain band x in [1e-9, 1e-5] we need w < 0.032
  domain_weights lower clamp is 0 (config.cpp:601-602), so w CAN be that small.
=== Q6i(2): EMPIRICAL ===
  load_config kept domain_weights.x = 0.174981 (sanitize range [0,1e6])
  raw cnt    speed        gain             out.x            verdict
  1          5.46816e-05  -141.873         -4.43352         <<< AXIS INVERTED
  2          0.000109363  1                0.0625
```
**TAM UÇTAN UCA, gerçek `load_config` ile, 1-count'lik bir frame'de `out.x = -4.43`.**
`domain_weight_x 0.175` + `limit 45` + `decay_rate 4.47e-11` — üçü de sanitize sınırları
içinde. **`negative gain` + `axis inverted` = imleç ters yöne gidiyor.**

### 6f. Peki bu PORT hatası mı? — REFERANSLA BİREBİR AYNI (INHERITED)

```
=== Q6j: port vs REFERENCE natural<GAIN> ===
  limit    decay_rate     port gain              reference gain         identical
  45       4.46684e-11    -141.873079329         -141.873079329         BIT-IDENTICAL
  100      1e-09          -352.16672453          -352.16672453          BIT-IDENTICAL
```
`ref/accel-natural.hpp:40-55` **aynı formülü** kullanıyor (`natural.hpp:100-102`
zaten "DO NOT fix, port must stay bit-compatible" diyor). Yani **formül hat değil,
`natural.hpp`'nin güvenlik beyanı hat.** Düzeltme `decay_rate` taban sınırı gerektirir —
`natural.hpp:154-155` zaten "the fix is NOT in this file — it is a lower bound on
decay_rate in the config-presets lane" diyor. **O yönlendirme DOĞRUYDU; sadece
dayanağı (negatif kazanç yok) yanlıştı.**

### 6g. SYNCHRONOUS — küçük ama gerçek sapmalar (float LUT enterpolasyonu)
```
  sync GAIN     mot1.5 gam1 ss5      dir=+1 : VIOLATIONS=15    worst=3.12944e-09 at x=0.219786
  sync LGCY     mot0.3 gam1 ss5      dir=-1 : VIOLATIONS=2680  worst=0.00631817 at x=9.24698
  sync GAIN     mot0.3 gam1 ss5      dir=-1 : VIOLATIONS=1452  worst=0.00489109 at x=16.1436
```
`synchronous` LUT'u **float**'ta (`accel-lookup.hpp:10` "LUT float (referansla aynı)").
Sapma float enterpolasyonu hassasiyeti — **~3e-9 GAIN'de** (LUT'un kendi),
**~6e-3 LEGACY'de** (`exp/log/pow` zinciri). Referansla aynı aritmetik → **INHERITED,
kabul edilebilir** (deviations.md'de sınıflandırılmamış ama `formulas.md:164` kabul
ediyor). **LOW/INFO.**

### 6h. natural GAIN, iyi-koşullu bantta **0 sapma** — temiz
```
  natural GAIN  lim1.5 dec0.1        dir=+1 : VIOLATIONS=0
  natural LGCY  lim1.5 dec0.1        dir=+1 : VIOLATIONS=0
  natural GAIN  lim0.5 dec0.1        dir=-1 : VIOLATIONS=0
  natural LGCY  lim0.5 dec0.1        dir=-1 : VIOLATIONS=0
  natural GAIN  lim1.5 dec1e-9       dir=+1 : VIOLATIONS=1514  worst=0.0291239   (1.02912->1)
```
`decay_rate=1e-9`'da **zaten** 1514 ihlal var — `natural.hpp:100-101`'in bahsettiği
`decay-1.0` çıkarma ill-conditioned'luğu, **daha çok pozitif sapmayla** başlıyor ve
negatif tarafa geçiyor. `decay_rate=0.1` (default) **mükemmel**.

| ID | dosya:satır | sınıf | kanıt |
|---|---|---|---|
| **L02-14** | `accel-natural.hpp:146-148` | **CRIT→HIGH** | ⭐ Ölçüldü: `limit=100 decay=1e-9` → **292/4000 noktada NEGATİF kazanç, min -352.167**; `limit=45 decay=4.47e-11` → **-7.49e+06**. 41×41×600 ızgarada **1681 hücrenin 350'si (%20.8)** negatif. Başlığın **kendi 864'lük gridinde bile 1 negatif nokta var** ("no reachable input produces a negative gain" **zaten yanlış**). **Erişilebilirlik uçtan uca ölçüldü**: `load_config` + `domain_weight_x=0.175` + 1 count @ dpi=32000,t=100ms → **`out.x = -4.43`, EKSEN TERS**. Referansla **bit-aynı** → formül değil **beyan** hata; düzeltme `config.cpp`'te decay_rate tabanı. |
| **L02-15** | `accel-synchronous.hpp:128-132` | LOW | Yön farkındalıklı test: legacy `mot=0.3` → **2680 sapma, worst 0.0063** @ x=9.25; gain `mot=0.3` → 1452 sapma @ x=16.14. `mot=1.5` (default) legacy'de **0 sapma**, gain'de 15 sapma / worst 3.1e-9. Sapma float LUT + `exp/log/pow` hassasiyetinden; referansla aynı aritmetik → INHERITED. |
| L02-16 | `accel-lookup.hpp:91-93` | INFO | GAIN/velocity modunda "monotonluk" **yanlış ölçüt**: gain = `y/x`, `y` çıkış hızı → tanım gereği azalan. 8000/8000 düşüş, **bulgu değil** (`lookup.hpp:42-44` bunu söylüyor). LEGACY modda **0 sapma** — saf yön testi temiz. |
| L02-17 | `accel-jump.hpp` geneli | INFO | Yön farkındalıklı test **0 sapma** (5/5 konfigürasyon). İlk yönsüz taramadaki 1524 "düşüş" **sert adımın kendisi** (`cap.y<1` bölgesi). Sigmoid kuyruğundaki 0.029'luk sıçrama `dir` doğru tanınınca sıfırlanıyor. **Jump en temiz algoritma.** |

---

## SORU 7 ⭐ — SESSİZ YEŞİL: "işleniyor" görünen ama aslında atlanan yol var mı?

Brifing §3'ün hedefi. **Testleri bozup kırmızıya döndürmeyi** esas aldım (kopya ağaçta).

### 7a. ⭐⭐ EN BÜYÜK SESSİZ YEŞİL: `lookup` **`test_extreme_inputs` listesinde YOK**

`test_accel.cpp:2976-2979`:
```cpp
const accel_mode modes[] = {
    accel_mode::noaccel, accel_mode::classic, accel_mode::natural,
    accel_mode::jump, accel_mode::synchronous, accel_mode::power
};
```
**6 mod var, `accel_mode::lookup` YOK.** SECTION adı
`"extreme input values — no NaN/Inf from any algorithm"` — **`her algoritma`** diyor.
Olmayan mod: `lookup`.

**Kanıt (göreli tarama, pozitif kontrol ile):**
```
$ sed -n '2960,3000p' tests/test_accel.cpp | grep -c 'accel_mode::lookup'
0
$ grep -c 'test_natural_decay_zero' tests/test_accel.cpp
2                                     <- pozitif kontrol: grep sağlam
```
**MUTASYONLA KANIT:** lookup'ı listeye ekleyip girdilere NaN/Inf ekledim:
```
MUT7 applied: NaN/+Inf/subnormal/negative eklendi
  NaN/Inf at mode=0, x=nan, g=nan
  FAIL  test_accel.cpp:2995  all_finite
  NaN/Inf at mode=2, x=nan, g=nan
  NaN/Inf at mode=2, x=inf, g=-nan
  FAIL  test_accel.cpp:2995  all_finite
=== Sonuç: 34162/34164 geçti, 2 BAŞARISIZ ===   MUT7 rc=1
```
**Yalnız `classic`(mode 0) ve `natural`(mode 2) kırıldı.** `lookup`'ın 4 kaçağı
(`+Inf`, `NaN`, `1e-320`, `1e-310`) ve `jump` legacy'nin `NaN` kaçağı **görünmedi** —
çünkü lookup listede değil. **Bu tam brifing §3: "bir test yazılmamış ama kapı yeşil."
HIGH** (bölüm adı "her algoritma" diyor, 6/7 kapsıyor).

### 7b. ⭐ MUTASYONLA: `test_extreme_inputs` **zaten var olan** kaçakları da görmüyor

`extremes[]` (`:2975`) = `{0.0, 1e-15, 1e-9, 0.001, 1.0, 100.0, 1e6, 1e15}`.
**NaN yok, +Inf yok, denormal yok, negatif yok.** Yukarıdaki MUT7 bunu kanıtladı:
listeye NaN/Inf **ekleyince** 2 mod kırıldı. Yani mevcut girdi setiyle bu bölüm
`natural`'ın NaN kaçağını **zaten** göremez.

### 7c. ⭐ `test_nan_propagation_all_modes` — adı vaat ediyor, **yapmıyor**

`test_accel.cpp:5703` — adı *"R10 — NaN propagation: full pipeline for every accel mode"*.
Girdi: `test_speeds[] = {0.0, 1e-15, 0.001, 1.0, 100.0, 1e6, 1e15}` (`:5718`).
**NaN/Inf yok.** Buna rağmen 7 modun hepsini geziyor ve `EXPECT(!any_nan)` diyor.
İki katmanlı sessizlik:
1. **NaN beslenmiyor** — `natural`'ın NaN kaçağı test edilemez.
2. **Yine de yeşil** — çünkü `apply_motion_math` (`motion_math.hpp:70-71`) `remainder`'ı
   sonunda zaten 0'a düşürüyor. Yani test, **varlığını kanıtlayamadığı şeyin yeşil
   işaretidir** (tautolojik test).

MUTASYONLA:
```
MUT3 (modify() tail guard'ı kaldırıldı, test hızları hâlâ NaN/Inf İÇERMİYOR)
  === Sonuç: 34164/34164 geçti ===   MUT3 rc=0
MUT7 (extremes'e NaN/Inf eklendi, lookup da listeye)
  === Sonuç: 34162/34164 geçti, 2 BAŞARISIZ ===   MUT7 rc=1
```
Yani **"her mod için NaN yayılımı" testinin fiilen yakalayabildiği iki mod var:
classic ve natural. Diğer beşi için o bölüm süs.**

### 7d. ⭐⭐ `noaccel` modunun testte "kırılması" — korumanın ALGORİTMADA DEĞİL olduğunun kanıtı

MUT4'te `motion_math` guard'ları da kaldırılınca **ilk kırılan mod `noaccel`** oldu:
```
  NaN/Inf in mode noaccel: speed=nan time=0.001     <-- İLK KIRILAN: noaccel!
```
**`noaccel` hiç ivme yapmaz** — `accel_noaccel` `operator()` `1.0` döner. Yani NaN
**hiçbir ivme algoritmasından** gelmiyor; `motion_math`'ten geliyor. Bu, L02-13'ün
tesadüf değil **yapısal** kanıtı: **sistem NaN'yı ivme katmanında değil, hareket
matematiği katmanında söndürüyor.** Dört algoritmanın hiçbirinin kendi koruması yok
(§5a, §5g).

### 7e. ⭐ SORU 7'NİN ASIL YANITI — "işleniyor görünen, aslında atlanan" **yollar**

Bunları **ölçtüm**, tahmin etmedim:

| # | Yol | Nerede | Kanıt |
|---|---|---|---|
| **1** | **`smooth` GUI ayarı sessizce yok sayılıyor** | `accel-jump.hpp:26-27` | `smooth*cap.x<1` → `smooth_rate=0` → sert adım. `cap.x=15`'te smooth 0.01–0.05 **görünür ayar, sıfır etki**. **MUT6** (davranışı değiştir) → `34164/34164` **yeşil**, hiç test yok. `gui/ui_builder.inl:282` tooltip'i **"Jump: sigmoid steepness"** diyor — kullanıcıya sıfır etkiyi söylemiyor. |
| **2** | **`test_extreme_inputs` lookup'ı hiç test etmiyor** | `test_accel.cpp:2976-2979` | 6/7 mod. MUT7 → lookup'ın 4 kaçağı görünmedi. |
| **3** | **`test_nan_propagation_all_modes` NaN beslemiyor + tautolojik** | `test_accel.cpp:5718` | `test_speeds[]`'de NaN/Inf yok; `motion_math.hpp:70-71` çıktıyı zaten 0'lıyor. 7 mod geziyor, fiilen 2'sini doğruluyor. |
| **4** | **`test_natural_decay_zero` yalnız `decay=0`'ı ölçüyor** | `test_accel.cpp:2921-2959` | `limit=1.5` sabit; `decay=0.1`'deki **negatif kazanç kümesini** (L02-14) göremez. Doğru kapsamı var, **dar kapsam** — bir kusur değil, ama "natural güvenli" izlenimi bırakıyor. |
| **5** | **`test_lut_sort_on_sanitize` doğru, ama "boş LUT"ı `{length=0}` ile test ediyor** | `test_accel.cpp:3404-3410` | `length=0` → `size<=0 → 1.0` (`:62`) → temiz. **`length=2` (tek nokta) + GAIN + `x0<=0` = ÖLÜ İMLEÇ** test edilmiyor. Mutasyonla doğrulayamadım (bkz. 7f) ama `test_accel.cpp:4580` tek noktayı `x0=5.0` ile test ediyor — **`x0<=0` dalı hiç test edilmiyor.** |
| **6** | **`synchronous` "activation framework" — isim yanıltıcı** | `accel-synchronous.hpp:8-9` | Adı vaat ettiği: activation-framework. **VAAT UYUMLU** — `formulas.md:93` birebir. Kullanıcı "kare senkronizasyonu" sanabilir ama kod/başlık/parametre dizini **üçü de** "activation-framework" diyor. **Bulgu değil.** |
| **7** | **`accel-jump.hpp:75-79` yorumu korumayı dosya-geneli sanıyor** | `accel-jump.hpp:81` | Yorum GAIN dalının `:80-82`'deki `isfinite` guard'ına işaret ediyor; LEGACY dal (`:43-48`) korumasız ve **ölçüldü NaN veriyor**. Yorum eksik. |

**7f. Durustluk notu (brifing §2 kuralı):** 5. satırdaki "mutasyonla doğrulayamadım"
bilinçli — o guard'ı mutasyona sokmak **ölü imleci üretmedi** (guard'ı kaldırmak
da test kırmızdı), dolayısıyla **mutasyonla kanıtlayamadım**. Bunu **kanıtsız bırakmıyorum**:
`test_accel.cpp:4580`'in `x0=5.0` (pozitif) kullandığını **okuma + grep** ile
sabitliyorum; **o dalın test edilmediği bir ÖLÇÜM değil, bir ÇIKARIM.** Bunu
`L02-18`'de INFO olarak işaretliyorum, HIGH olarak değil.

| ID | dosya:satır | sınıf | kanıt |
|---|---|---|---|
| **L02-18** | `test_accel.cpp:2976-2979` | **HIGH** | ⭐ `test_extreme_inputs` mod listesi **6 elemanlı, `lookup` YOK** (`grep -c` → 0, pozitif kontrol `test_natural_decay_zero` → 2). Bölüm adı *"no NaN/Inf from any algorithm"*. **MUT7** (lookup eklendi + NaN/Inf girdiler): yalnız `classic`(mode 0) ve `natural`(mode 2) kırıldı → lookup'ın 4 kaçağı **görünmedi**. Tam brifing §3 "test yazılmamış ama kapı yeşil". |
| **L02-19** | `test_accel.cpp:5718` | **MED** | `test_nan_propagation_all_modes` — adı *"NaN propagation ... for every accel mode"* ama `test_speeds[] = {0.0, 1e-15, 0.001, 1.0, 100.0, 1e6, 1e15}` → **NaN/Inf yok**. Ayrıca `motion_math.hpp:70-71` `remainder`'ı zaten 0'a düşürdüğü için `EXPECT(!any_nan)` **tautolojik**. Fiilen yalnız 2 modu doğruluyor. |
| L02-20 | `accel-jump.hpp:75-79` | MED | Yorum GAIN dalının `isfinite` guard'ına işaret ediyor; **LEGACY dal korumasız** ve ölçüldü: `x=NaN → nan`. Yorum, dosyanın genelinde bir koruma ima ediyor — **ölçüm bunu desteklemiyor**. `accel-jump.hpp:32`'de `is_smooth()` okuyan iki dal var, biri korumalı biri değil. |
| L02-21 | `accel-lookup.hpp:124` | INFO | Test kapsamı dışı (ÇIKARIM, ölçüm değil): `test_accel.cpp:4580` tek noktalı LUT'yu `x0=5.0` ile test ediyor; `x0<=0 → return 0.0` (`:124`) dalı hiç test edilmiyor. Mutasyonla kanıtlanamadı (guard'ı kaldırmak testi kırmıyor). **Kanıtsız iddia olarak bırakmıyorum, işaretliyorum.** |

---

## KAPI (koşturduğum kapılar + ham çıktı + ÜRETİLEN SAYILAR)

| # | Komut | rc | Üretilen sayı / ham çıktı |
|---|---|---|---|
| K1 | `g++ -std=c++20 -O2 ... test_accel.cpp src/config.cpp src/logitech_*.cpp` (ayna ağacı, TMPDIR izole) | 0 | `0` error |
| K2 | `./t_base` (baseline birim paketi) | **0** | **`=== Sonuç: 34164/34164 geçti ===`** |
| K3 | `bash tests/oracle/run_oracle.sh` | **0** | `total rows compared : 1408` / `documented deviations: 79` / `RESULT: OK — local port matches official reference (rel tol 1e-09) on every row outside the documented deviations (79 rows).` |
| K4 | **MUT1** — `natural.hpp` `if (accel < 1e-12) return 1.0;` silindi | 141 | 4× `FAIL test_accel.cpp:2938-2941` + `:5370-5371` + fuzz `:3985` |
| K5 | **MUT2** — `test_extreme_inputs` listesine NaN/Inf/denormal eklendi | 0 | `34164/34164` — **yeşil kaldı** |
| K6 | **MUT3** — `rawaccel.hpp:610-611` NaN guard'ı kaldırıldı | 0 | `34164/34164` — **yeşil kaldı** |
| K7 | **MUT4** — + `motion_math.hpp:52-53,70-71` guard'ları kaldırıldı | **1** | 5× FAIL; ilk kırılan mod **`noaccel`** |
| K8 | **MUT5** — lookup `x0<=0 → 0.0` ölü-imleç guard'ı kaldırıldı | 0 | `34164/34164` — **yeşil kaldı** |
| K9 | **MUT6** — `jump` `smooth*cap.x<1` yok sayması kaldırıldı | 0 | `34164/34164` — **yeşil kaldı** |
| K10 | **MUT7** — lookup listeye + NaN/Inf girdiler | **1** | `34162/34164, 2 BAŞARISIZ` — yalnız mode 0 ve 2 |

**8 ölçüm harness'i** (`/tmp/opencode/l02/q1_lut_sort.cpp`, `q1b_reach.cpp`, `q23.cpp`,
`q4_jump.cpp`, `q4e_smooth.cpp`, `q5_nan.cpp`, `q5d_q6.cpp`, `q6_mono.cpp`, `q6h_reach.cpp`,
`q6i_reach2.cpp`, `q6j_ref.cpp`, `q7_final.cpp`) → **q1 12 blok, q1b 8 senaryo,
q23 15 senaryo, q4 4 blok, q4e 4 blok, q5 104 ölçüm (13×8), q5d 3 blok, q6 3 blok,
q6h 3 blok, q6i 2 blok, q6j 6 karşılaştırma, q7 8 varyant.**

**ÇALIŞMA AĞACI BOZULMADI — kanıt:**
```
$ git status --porcelain include src daemon cli gui tests
(boş — hiçbir kaynak dosya değişmedi)
$ git status --porcelain include/accel-{natural,synchronous,lookup,jump}.hpp
(boş)
```
MUT1-MUT7 **tümüyle** `/tmp/opencode/l02/repo/` kopyasında yapıldı; mutasyon sonrası
`diff -rq` ile kopyanın temizliği doğrulandı. `git checkout/stash/reset` **hiç çalıştırılmadı.**

---

## KAPSANMAYAN (lane dışı kaldılarım — birinin bakması gerek)

1. **`daemon/daemon.cpp` kare-içi event toplama** (L02-2b ölçümü) — "synchronous" kare-içi
   semantik taşımıyor; daemon 32 event'lik batch'i **toplayıp** ivmeyi kare başına bir kez
   uyguluyor. `flush_motion` / `process_device` sahibinin.
2. **`modifier::modify()` sonundaki `isfinite` guard'ları** (`rawaccel.hpp:610-611`) — NaN
   sızıntılarının tek sönümleyicisi. `accel-union.hpp` / `rawaccel.hpp` sahibi.
3. **`config.cpp:517-615` `sanitize_profile`** — LUT sıralama burada doğru çalışıyor
   (L02-03 doğrulandı), ama `decay_rate` **taban** sınırı yok (L02-14'ün asıl düzeltme
   yeri). `config.cpp` sahibi.
4. **`gui/graph.inl:368-392` `lut_set_points`** — GUI LUT yazıcısı; sıralıyor (doğrulandı),
   ama `lut_get_points`/`graph.inl` sürükleme aralıkları L02-01'in `x0<=0` ölü-imleç
   durumuna **GUI'den ulaşılabilir mi** bilemedim. GUI sahibi.
5. **`gui/ui_builder.inl:247,282`** — `smooth` spin aralığı `[0,1]` adım `0.01`;
   `smooth*cap.x<1` yok sayılması (L02-08) GUI'de görünür ayar/etkisiz. GUI sahibi.
6. **`tests/oracle/` grid** — 6 jump case'inin hepsi `smooth ∈ {0.0, 0.5, 1.0}`
   (`oracle_cases.hpp:92-96`), `0 < smooth*cap.x < 1` bandının **hiçbir** örneği yok.
   Oracle sahibi.
7. **`docs/research/formulas.md:163` R43/P74-BULGU-1** — "synchronous LUT index UB
   (float-cast-overflow)" düzeltmesi `idx`'i clamp'liyor, `t`'yi clamp'lemiyor
   (L02-04). Doküman + `accel-synchronous.hpp` birlikte.

## TEMSIL SINIRI (her zaman dolu)

1. **PS5.1 / çalışma anı yok** — bu bir Linux ev incelemesi; PS5.1 iddiası ölçemedim.
2. **Fiziksel cihaz yok** — ölü imleç (L02-01), eksen ters dönme (L02-14) ve NaN event
   kaybını (L02-13) **sayısal çıktı ve uçtan uca `modifier::modify()` çağrısıyla**
   kanıtladım; gerçek farede **gözle görmedim**. L02-14'ün `domain_weight_x=0.175` +
   `limit=45` + `decay=4.47e-11` kombinasyonu `load_config` üzerinden **gerçekten
   yüklendi** ve `out.x=-4.43` üretti — ama bu **1-count sentetik frame**, gerçek
   1000 Hz USB trafiği değil.
3. **Oracle ile çapraz doğrulamadım** — L02-01 (`x0<=0` ölü imleç), L02-08 (`smooth`
   yok sayma), L02-14 (negatif kazanç) oracle'da **deviation olarak listelenmiyor**
   çünkü grid bu bölgeleri **hiç ziyaret etmiyor** (`oracle_cases.hpp` kendi satırlarıyla
   doğruladım). "Oracle yeşil" bunları **çürütmüyor**, sadece **kör nokta**.
4. **`accel-classic.hpp`, `accel-power.hpp`, `accel-noaccel.hpp`** — lane dışı; yalnız
   pozitif kontrol ve `test_extreme_inputs` (`:2976`) mod listesi için referans aldım.
5. **GUI'yi çalıştırmadım** — L02-08 ve L02-01'in GUI erişilebilirliği statik kod
   okumasıyla (`ui_builder.inl:247,282`, `graph.inl:368-392`) çıkarıldı, **tıklanıp
   doğrulanmadı**.
6. **Aşırı-parametre taramalarımın kendi ızgaraları seçimimdir** — L02-14'te 41×41×600
   kullandım; daha geniş/kötü hücreler **bulunmuş olabilir**. Verdiğim sayılar
   (350/1681, min -7.49e+06, min -352.167) **bu ızgaranın** sayıları, evrensel
   en-kötü değil.
7. **`test_natural_decay_zero` dar kapsamlıdır** — `limit`'i hiç değiştirmiyor; L02-14'teki
   negatif-kazanç kümesini **bu test kapsamı dışı**, kendi kapsamı içinde **etkin**
   (MUT1 kanıtı).