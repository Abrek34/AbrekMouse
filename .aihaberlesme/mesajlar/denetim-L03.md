# L03 — Çekirdek tipler, varsayılanlar, vektör matematiği

### L03 | alt-ajan (denetim turu) | 1 Ekim 2026
KAPSAM   : `include/rawaccel.hpp`, `include/rawaccel-base.hpp`, `include/math-vec2.hpp`
           (salt okuma). Ölçüm için `/tmp/opencode/l03/**` kopyaları derlendi;
           çalışma ağacına hiçbir dosya yazılmadı, `git checkout/stash/reset` çalıştırılmadı.
           Referans (okuma): `tests/oracle/ref/rawaccel.hpp`, `tests/oracle/ref/rawaccel-base.hpp`,
           `tests/oracle/ref/math-vec2.hpp`. Karşılaştırma tabanı: `docs/research/parameter_index.md`.
           `sanitize_accel_args` lane'ın DIŞINDA (`src/config.cpp:400`) — soru 1/2 onu
           sorduğu için ÖLÇTÜM, ama dosya satırları config.cpp'ye aittir.

---

## BULGULAR

### ID | dosya:satır | CRIT/HIGH/MED/LOW/INFO | kanıt

---

#### L03-01 | `include/accel-synchronous.hpp:130` → `include/rawaccel-base.hpp:81` | **CRIT** | ⭐ STATE BLEED — GUI bir grafiği çizerek kullanıcının LUT eğrisini siliyor

**Bulgu:** `synchronous::fill_lut()` `args.data[]` tamponunu
`const accel_args&` üzerinden **yazıyor**. GUI'nin canlı önizleme yolu
(`gui/graph.inl:124 compute_curve` → `gui/graph.inl:56 au.init(args)`)
`args`'ı `cur_prof(S).prof.accel_x` **referansı** olarak veriyor, yani
doğrudan `S->config`'in içindeki nesneye. Bir grafiğin yeniden çizilmesi
`args.data[]`'yi değiştiriyor, bir sonraki `save_config(S->config, …)`
(`gui/main.cpp:84`) bunu diske yazıyor.

Kanıt (uçtan uca, `save_config` dahil):

```
$ /tmp/opencode/l03/probe_save
loaded lut_data : 1 2 10 5 100 20 1000 50
  lookup gains  : gain(5)=3.333333 gain(50)=11.666667 gain(500)=33.333333
after 1 repaint : 0.0833333 0.09375 0.104167 0.114583 0.125 0.135417 0.145833 0.15625
ON DISK         : "lut_data": [
                        0.0833333358168602,
                        0.09375,
                        0.104
reloaded gains  : gain(5)=5.010413 gain(50)=50.010381 gain(500)=500.010059
```

Kullanıcının 8 float'ı (4 noktalık eğri) **tek bir repaint** ile
synchronous'ın iç LUT sigmoid-integral değerlerine yazıldı ve diske
persiste edildi. Yeniden yüklemede gain `3.33 → 5.01` (+50%), `500 ips`'te
`33.3 → 500.0` (**15×**). Kayıp **sessiz**: GUI hiçbir uyarı göstermiyor,
`on_graph_draw` her karede çalışan bir cairo draw callback'i
(`gui/ui_builder.inl:812`), `on_param_changed` (`gui/widgets_sync.inl:561`)
mod açılır menüsünden `gtk_widget_queue_draw` tetikliyor.

**Bu kod çalışırken ne olduğunda fark edilir?** Kullanıcı lookup profilinde
çalışıp mode'u synchronous'a çevirip (GUI'de olağan bir işlem) geri
dönünce **eliyle çizdiği eğri kaybolmuş** olarak döner. 34 164 iddialı test
suite'i yeşil çünkü testler `compute_curve`'ı hiç çağırmıyor:
`grep -rn "compute_curve" tests/` → **0 sonuç**.

**Sorumlu satır — değişiklik `rawaccel-base.hpp:81`'deki `mutable`:**
`mutable float data[LUT_RAW_DATA_CAPACITY] = {};` — `mutable` olması
`const&` üzerinden yazmaya izin veriyor; `synchronous.hpp:130`
(`args.data[i] = static_cast<float>(fn(x));`) bunu kullanıyor. `const`
kaldırılsa derleme hatası verir, yani sessiz-yeşil değil **sessiz-çöp**
üreten bir tasarım kararı.

**Kontrast — daemon ETKİLENMİYOR (ve neden etkilenmiyor, ölçüldü):**
`daemon/daemon.cpp:1237` `dev.settings.prof = prof.prof;` ile **kopyalıyor**,
sonra `init_settings` kopya üzerinde çalışıyor; daemon `dev.settings`'i
hiçbir yerde serileştirmiyor (`grep -n "save_config|json" daemon/daemon.cpp`
içinde `settings` ile eşleşen satır yok). Yani `src/config.cpp:96-98`'in
yorumu ("apply_profile mutates device.args, not the config copy the save
path serializes") **daemon için doğru** — ama aynı yorum GUI'yi kapsamıyor,
çünkü GUI'de kopya yok. `config.cpp:96-98` yorumu yanlış bir genelleme.

---

#### L03-02 | `include/rawaccel-base.hpp:78` | **HIGH** | `cap.y` struct default `0`, doküman/ oracle/ JSON default `1.5`

`vec2d cap = { 15, 0 };` — `tests/oracle/ref/rawaccel-base.hpp:61` →
`vec2d cap = { 15, 1.5 };`, `docs/research/parameter_index.md:85` → Varsayılan `1.5`,
`config/default.json:35` → `"cap": [15.0, 1.5]`.

Kanıt — gerçek `profile_from_json` üzerinden, `"cap"` anahtarı **yok**:

```
$ /tmp/opencode/l03/probe_capjson
accel_x.cap = {15, 0}   <-- 'cap' key was ABSENT from the JSON
docs/research/parameter_index.md:85 says cap_y default = 1.5
tests/oracle/ref/rawaccel-base.hpp:61 says cap = { 15, 1.5 }
gain(1000) = 6.000000  (doc default cap.y=1.5 would give 1.4875)
control with "cap":[15,1.5] -> cap={15,1.5} gain(1000) = 1.487500
```

Etkisi ölçüldü (classic, `cap_mode=out`, `acceleration=0.005`, `exp=2`):

```
                        struct default (cap.y=0)   doc/oracle/json (cap.y=1.5)
  GAIN  speed=100           1.5                        1.375
  GAIN  speed=1000          6.0                        1.4875
  GAIN  speed=100000      501.0                        1.499875
  LEGACY speed=1000          6.0                        1.5
  LEGACY speed=100000      501.0                        1.5
```

`cap.y = 0` "cap yok" demek (`accel-classic.hpp:161 if (args.cap.y > 0)`),
yani **cap'sız eğri sınırsız büyür** — 100 000 ips'te 501× kazanç.
Referans/dokuman/json default `1.5` iken ≈1.5×'te asimptotik. **4–334× sapma.**

**CLI ile üretilebilir, varsayılan config gerekmiyor:**
`set-param mode classic` sıradan bir komut ve cap'i yazmıyor:

```
$ echo '{...,"accel_x":{"mode":"noaccel"},"accel_y":{"mode":"noaccel"},...}' > m.json
$ rawaccel-cli -c m.json --no-daemon show default | grep -i cap
    cap:              [15.0000, 0.0000]
$ rawaccel-cli -c m.json --no-daemon set-param default mode classic
Set mode = classic in profile 'default'
$ python3 -c "...print(accel_x)"
  accel_x.mode = classic   cap = [15.0, 0.0]
  --> daemon fills the ABSENT key from the STRUCT default cap = {15, 0}
```

**Neden kapılar görmüyor:**
- Oracle **kendi struct'ını** kullanıyor: `tests/oracle/oracle_cases.hpp:35`
  `double cap_y = 1.5;` ve `tests/oracle/local.cpp:43`
  `a.cap = { k.cap_x, k.cap_y };` — yani grid'in her satırı cap'i ** açıkça
  atar; struct default'una hiç düşmez. Ölçüldü: 1408 satır, 79 sapma, rc=0.
- `tests/test_accel.cpp:144-160 make_args()` de `a.cap = {15.0, 1.5}` atar.
  Yani **hiçbir test `accel_args{}` bare default'unu classic'e sokmuyor.**

---

#### L03-03 | `cli/main.cpp:1172-1174` ↔ `src/config.cpp:501,508-513` | **MED** | 5 alanda CLI tavanı yok, sanitize var → P107 byte-correctness sözleşmesi boş

CLI bu 5 alanda yalnızca **alt** sınırı kontrol ediyor (`min_ok(key,0)`),
üst sınırı yok; sanitize `LIMIT_MAX / DECAY_RATE_MAX / MOTIVITY_MAX /
GAMMA_MAX / SMOOTH_MAX` uyguluyor. Ölçüm:

```
$ /tmp/opencode/l03/probe_cli_dom
limit             0  1e+300 | 0    100  | HI DIFFERS -> CLI accepts what sanitize rewrites
decay_rate        0  1e+300 | 0    10   | HI DIFFERS -> CLI accepts what sanitize rewrites
motivity          0  1e+300 | 0    10   | HI DIFFERS -> CLI accepts what sanitize rewrites
gamma             0  1e+300 | 0    10   | HI DIFFERS -> CLI accepts what sanitize rewrites
smooth            0  1e+300 | 0    1    | HI DIFFERS -> CLI accepts what sanitize rewrites
(aynı 5'i scale/output_offset/cap_x/cap_y/input_offset/exponent_power/exponent_classic/sync_speed → MATCH)
```

Gerçek binary ile uçtan uca — **rc=0, sessiz clamp**:

```
$ rawaccel-cli -c t.json --no-daemon set-param g smooth 16
Set smooth = 1.000000 in profile 'g'          rc=0
$ rawaccel-cli -c t.json --no-daemon show g | grep smooth
    smooth:          1.0000
$ rawaccel-cli -c t.json --no-daemon set-param g limit 5000
Set limit = 100.000000 in profile 'g'        rc=0
```

Diske yazan sayı 16 değil 1.0, 5000 değil 100. Kullanıcı "smooth = 16"
istedi, "1.0" aldı, **rc=0** — `tests/run_tests.sh:293`'ün P107 kapısı
(`for BAD in "snap 90" "dpi 999999" "exponent_classic 0.5" ...`) bu 5 alanı
**test etmiyor**; `grep -n "set-param limit\|set-param smooth\|..." tests/` → **0**.

Not: `src/config.cpp:96-98`'in "bu blok CLI ile paylaşılıyor" yorumu
`config.hpp:11-22`'deki ilk 5 sabit için doğru; AJ4-K6/K7'nin eklediği
6 sabit (`LIMIT_MAX`…`SMOOTH_MAX`) CLI'ya taşınmamış.

---

#### L03-04 | `include/rawaccel.hpp:224` | **HIGH** | ⭐ SESSİZ YEŞİL — `reset_smoothers()` **hiçbir yerden** çağrılmıyor (mutasyonla kanıtlandı)

Lane'ın 6. sorusunun doğrudan cevabı. `speed_processor::reset_smoothers()`
tanımlı, **0 üretim çağrısı, 0 test çağrısı**. Tanım burada:

```cpp
// include/rawaccel.hpp:224
void reset_smoothers() {
    smoother_x.input_speed_smoother.reset();   // …6 çağrı
}
```

`grep -c` sayıları (brifing istediği açık rapor):

| fonksiyon | tanım (rawaccel.hpp) | **üretim çağrısı** | test çağrısı |
|---|---|---|---|
| `reset_smoothers` | **1** | **0** | **0** |
| `reconfigure` | 1 | **1** (`daemon/daemon.cpp:1243`) | **0** |
| `init_coeff` | 3 | 0 (hepsi aynı dosyadan) | 0 |
| `calc_speed_whole` | 1 | 0 (aynı dosya `:557`) | 13 |
| `calc_speed_separate` | 1 | 0 (aynı dosya `:417`) | 3 |
| `init_settings` | 1 | 1 (`daemon/daemon.cpp:1238`) | 53 |
| `modify_separate_simd` | 1 | 0 (aynı dosya `:553`) | 0 |

**Pozitif kontrol (yöntemin çalıştığının kanıtı):** aynı grep deseni
`include/rawaccel.hpp` içinde `reconfigure` → 4 satır, `maxsd` → 3 satır
buluyor. `grep -c "v2d_mul" include/rawaccel.hpp` → 3, `lp_distance` → 1.
Sıfırlar gerçek sıfır.

**Mutasyon kanıtı (`/tmp` kopyası, çalışma ağacı untouched):**
`reset_smoothers()` gövdesine `abort()` + `MUT-HIT` eklendi, daemon'ın
gerçek çağrı sırası (`daemon.cpp:1238,1243` + 100 motion eventi +
smoothing'i aç/kapa) taklit edildi:

```
$ ./probe_daemon
after 1st reconfigure: smooth_input=0 coeff=0
after 100 events: windowTotal=0
after 2nd reconfigure (halflife now 10/5/5):
  windowTotal=0  cutoffTotal=0   (SM-5: must be 0)
DONE (no MUT-HIT above => reset_smoothers() never fired)
rc=0
```

**Pozitif kontrol — aynı mutasyon yöntemi `reconfigure`'da ÇALIŞIYOR:**

```
$ ./pc          # reconfigure()'a aynı abort eklendi
MUT-HIT reconfigure()
done
rc=0
```

Yani alet kırık değil; `reconfigure` tetikleniyor, `reset_smoothers` hiç
tetiklenmiyor.

**Bu kod çalışırken ne olduğunda fark edilir?** `SM-5`'in (`daemon.cpp:1239`
yorumu) "running smoother keeps its EMA state" sözleşmesi **korunuyor** —
çünkü `reconfigure()` (`:242`) aynı reset mantığını **kendi içinde**
tekrarlıyor (`:247-258`, altı `reset()` çağrısı), `reset_smoothers`'ı
çağırmadan. Yani **işlevsel bir kayıp yok**; bulgu `HIGH` değil `MED`'e
iner. Ama bugün iki kopya var ve SM-5'in tek doğruluk kaynağı olan
`reset_smoothers()` **hiç çalışmıyor** — SM-7/SM-5'i doğrulayan test de yok
(`grep -rn "reconfigure(" tests/` → **0**). Kim `reset_smoothers`ı
"temizle" derse SM-5 sessizce bozulur ve 34 164 iddia yeşil kalır.

**SESSİZ YEŞİL YÖNTEM BULGUSU:** `reconfigure()` — prodüksiyonun **tek**
speed_processor giriş noktası — **hiçbir test tarafından çağrılmıyor**
(0 test çağrısı, yukarıdaki tabloda). Yani SM-5'in canlı yolu test
kapsamı dışında.

---

#### L03-05 | `include/math-vec2.hpp:182-192` | **MED** | `maxsd`/`minsd` NaN'i sessizce yutar; `clampsd` NaN'i **geçirir**

Ölçüldü (`/tmp/opencode/l03/probe_math`):

```
  maxsd(NaN,3)   = 3     <-- NaN swallowed
  maxsd(3,NaN)   = NaN
  minsd(NaN,3)   = 3     <-- NaN swallowed
  minsd(3,NaN)   = NaN
  clampsd(NaN,0,10) = NaN   <-- NaN passes straight through
  clampsd(5,10,0)   = 10   <-- lo>hi: returns hi (no guard)
```

`math-vec2.hpp:183` `return a > b ? a : b;` — NaN karşılaştırması false
verir, `b` döner. Bu **referansla aynı davranış** (`ref/rawaccel.hpp` de
kendi `max`/`min`ini kullanıyor) ve `lp_distance`'ın `:22-27`'deki
"NaN bileşeni 0'a katla" politikasıyla **tutarlı** ("ölçülemez girdi →
hareket yok"). Bu yüzden ayrı bir bulgu değil, **bilinçli ve ölçülmüş bir
sapma** — kayıt altına alıyorum ki bir sonraki ajan "bugün buldum" demesin.

`clampsd(lo>hi)` korumasız: `rawaccel.hpp:541`'deki tek çağrı
`clampsd(speed, args.speed_min, args.speed_max)`; `modifier_flags:301`
`clamp_speed = speed_max > 0 && speed_min <= speed_max` ile **`lo ≤ hi`
garanti ediyor**, yani üretimde erişilemez. Ölçüldü ve doğrulandı — bu
**bir savunma hattı değil, invariant'tır**; invariant'ı bozan tek yol
`modifier_flags`'ı atlamak.

---

#### L03-06 | `include/math-vec2.hpp:15-180` | **MED** | `lp_distance` sıfır vektörde **NaN üretmiyor** — ama negatif `p`'de sessizce YANLIŞ değer veriyor

Soru "NaN üretiyor mu?" — **hayır**, ve bu ölçülmüş bir tasarım kararı
(`:16-17 R6` yorumu, `:66` bileşen koruması):

```
  lp_distance({0,0}, p=2      ) = 0        lp_distance({0,0}, p=-0.5) = 0
  lp_distance({0,0}, p=1      ) = 0        lp_distance({0,0}, p=1e-09) = 0
  lp_distance({0,0}, p=-1     ) = 0        lp_distance({0,0}, p=16    ) = 0
  lp_distance({0,0}, p=100    ) = 0        lp_distance({0,0}, p=1e-320)= 0
  lp_distance({NaN,3}, p=2)   = 0   <-- NOT NaN (documented divergence from ref)
  lp_distance({Inf,3}, p=2)   = 0
```

**Ama negatif `p` sessiz-yanlış:** `lp_distance({0,3}, -1.0) = 0` ve
`lp_distance({3,4}, -1.0) = 1.7142857` — matematiksel Lp normu `p<0`'da
tanımsızdır, referans `pow(pow(|x|,-1)+pow(|y|,-1), -1)` ile aynı sayıyı
verir (oracle'da sapma yok). Yani **referansla birebir uyum**, sorun yok.

**Kutuplaştırıcı uç:** `lp_distance({3,4}, p=0)` = **4** (= L∞). `p=0`
`1/p = Inf` yapar; `:99 isfinite(result)` → false → log-space → `:179`
`expanded` yine Inf → `return M`. Doğru L∞ yanıtı, sezgisel olmayan yoldan.
`rawaccel.hpp:197` `lp_norm <= 0 → max-norm`'a yönlendiriyor, yani `p=0`
zaten Lp yoluna **giremiyor**; bu dal ölü-üretim, canlı-API.
`rawaccel.hpp:197` **kanıtı**: `sanitize` de (`config.cpp:571`)
`if (lp_norm <= 0) lp_norm = 2;` ile negatif/0'ı engelliyor → `p<0`
üretimde erişilemez. `math-vec2.hpp:85-95`'teki yorum bunu zaten doğru
belgelemiş ("MEASURED FALSE … sanitize [1e-9,16] sınırlamıyor").

---

#### L03-07 | `include/rawaccel.hpp:538-545` vs `:379-385` | **MED** | İki ayrı hız-clamp uygulaması; rotasyon+snap altında **farklı sonuç** veriyor

`modifier::modify` içinde **iki** speed-clamp kopyası var: skaler
(`:538`, `clampsd`) ve `modify_separate_simd` (`:379`, `simd_clamp`). Aynı
profil, `whole` açık/kapalı — ölçüldü:

```
                      whole(scalar)          separate(SIMD)
  +rot30/snap10:
  100,0                {116.71,  67.38}       { 64.94, 115.58}
  70,70                { 34.50, 128.77}       {-32.67, 128.46}
  0,100                {-67.38, 116.71}       {-115.58, 64.94}
  100,1 (near-axis)    {116.04,  68.55}       { 63.78, 116.29}
```

Bu **beklenen** (whole vs separate farklı ivme matematiği) — referans da
ayrım koyar (`ref/rawaccel.hpp:358` vs `:380`). Ayrıntılı test: scalar
yolun `if (speed > 0)` koruması (`:540`) SIMD yolunda da var (`:381`), yani
`0/0` yok. **Bunu bulgu olarak değil, çift-yapı kaydı olarak** yazıyorum:
aynı clamp iki yerde, iki farklı yardımcı (`clampsd` vs `simd_clamp`).

---

#### L03-08 | `include/rawaccel.hpp:490-494` | **INFO** | `time<=0` koruması smoother'ları **dokunulmadan** bırakıyor — doğrulandı

BUG-HIGH-2'nin "smoother'lar zehirlenmesin" vaadi ölçüldü:

```
  modify(time=0):   in={100,100} -> out={0,0}
  smoother windowTotal before=0 after=0  (untouched)
  modify(time=NaN): out={0,0}
  modify(time=8):   out={125,125}  windowTotal now=1.19197
```

`0/0` de `NaN` de dışarı sızdırmıyor, smoother durumu bit bütün korunuyor.
Doğru davranış.

---

#### L03-09 | `include/rawaccel.hpp:351-353` vs `:502-504` | **INFO** | `IPS_FACTOR_MAX` **iki kez** tanımlanmış (iki ayrı `constexpr`)

```
$ grep -rn "IPS_FACTOR_MAX" include/ src/ daemon/ cli/ gui/ | grep -v shadow
include/rawaccel.hpp:351:    constexpr double IPS_FACTOR_MAX = 1e6;
include/rawaccel.hpp:352:    if (!std::isfinite(ips_factor) || ips_factor > IPS_FACTOR_MAX)
include/rawaccel.hpp:353:        ips_factor = IPS_FACTOR_MAX;
include/rawaccel.hpp:502:        constexpr double IPS_FACTOR_MAX = 1e6;
include/rawaccel.hpp:503:        if (!std::isfinite(ips_factor) || ips_factor > IPS_FACTOR_MAX)
include/rawaccel.hpp:504:            ips_factor = IPS_FACTOR_MAX;
```

İki bağımsız `constexpr` — biri `modify_separate_simd` içinde, biri
`modify` içinde. Değerler aynı, ama **adlandırılmış sabit paylaşılmıyor**.
Birinin değişmesi diğerini sessizce yanlış bırakır (derleme hatası vermez).
`math-vec2.hpp:51-54` yorumu `:350-353`'e atıf yapıyor, yani üçüncü bir
kopya varsayıyor.

---

#### L03-10 | `include/rawaccel.hpp` (tümü) | **INFO** | Soru 6'nın **doğrudan** cevabı: bir sanitize/clamp fonksiyonu **YOK**

Brifing 6. sorusu "tanımlı ama hiç çağrılmayan bir sanitize veya clamp
fonksiyonu var mı?" diye soruyor. Cevap: **`rawaccel.hpp` içinde hiçbir
sanitize/clamp fonksiyonu tanımlı değil** — tüm sanitize `src/config.cpp:400`'te.

```
$ grep -nE '\b[a-z_]*(sanitiz|clamp|guard|gate)[a-z_]*\b' include/rawaccel.hpp | grep -vE '//|\*'
293:    bool clamp_speed              = false;                       ← bir ALAN, fonksiyon değil
301:        clamp_speed              = args.speed_max > 0 && ...;   ← atama
379:    if (flags.clamp_speed) {                                     ← KULLANIM (2 yerde)
538:        if (flags.clamp_speed) {
541:                double ratio = clampsd(speed, args.speed_min, args.speed_max) / speed;
```

`clamp_speed` bir **bool alan** (`:293`), iki yerde okunuyor (`:379`, `:538`) —
ölü değil. `clampsd` `math-vec2.hpp:190`'da, `rawaccel.hpp:541`'de **bir kez**
çağrılıyor. **Pozitif kontrol:** aynı desen `smooth|reconfigur` için 64
satır buluyor.

**Dolayısıyla `rawaccel.hpp`'in sessiz-yeşili `reset_smoothers()`'tır
(L03-04)** — o bir clamp değil ama aynı sınıf: "var sanılan, çalışmayan".

---

#### L03-11 | `include/math-vec2.hpp:198-201` | **INFO** | `direction(0)` = `{1, 0}` ✓ — ilk ölçümüm bunu **yanlış** gösterdi, düzeltiyorum

İlk probum `%s` formatını iki argümanda kullandığı için tampon paylaşımı
yaptı ve `direction(0)` `{1, 1}` göründü. **Bu benim ölçüm hatalımdı, kod
değil.** Ayrı tamponlu yeniden ölçüm:

```
  direction(0)   x=1                        y=0
  direction(90)  x=6.123233995736766e-17    y=1
  direction(360) x=1                        y=-2.4492935982947064e-16
  rotate({3,4},d(90))  x=-4                 y=3.0000000000000004
  rotate({0,0},d(37))  x=0                  y=0
  rotate({1e300,1e300},d(45)) x=1.487e284   y=1.414e300
```

`direction(0)` doğru. `rotate({1e300,1e300}, 45°)` **taşma yok** (çapraz
terimler birbirini götürüyor) — `vec2d`'nin `rotate`'i overflow'a karşı
`hypot` kullanmıyor ama burada da sorun yok; ölçüldü.

---

#### L03-12 | `include/rawaccel-base.hpp:63-109` | **INFO** | `accel_args::operator==` **prodüksiyonda hiç çağrılmıyor** (CLI `diff` yolu dışında)

```
$ grep -rn "\boperator==\|accel_x ==\|args ==" include/ src/ daemon/ cli/ gui/ (shadow hariç)
cli/main.cpp:641:    if (cfg.active_profile == name && !cfg.profiles.empty()):   ← std::string ==
cli/main.cpp:1844:            if (cfg.active_profile == old_name)                ← std::string ==
gui/profile_mgr.inl:333:            if (S->config.active_profile == old_name)     ← std::string ==
```

`accel_args::operator==` (`rawaccel-base.hpp:94`) ve `operator!=` (`:108`)
**hiçbir prodüksiyon satırında** görünmüyor; `cli/main.cpp`'deki `diff`
komutu epsilon karşılaştırmasını elle yapıyor. Fonksiyon doğru ve test
kapsamında, ama `==` **ölü üretim kodu** — `deq` (`:90`) yalnız onun içinde.
Düşük önem: `DBL_EPSILON_CMP = 1e-9` sabiti buradan besleniyor ve `diff`
aynı sabiti ayrıca tanımlıyor olabilir; birinin değişmesi diğerini
etkilemez.

---

#### L03-13 | `docs/research/parameter_index.md:88` ↔ `src/config.cpp:406-407` | **LOW** | `lut_length` "çift" (even) değil — tek sayı sanitize'dan **geçiyor**

```
  length=-1       -> 0      <-- CLAMPED
  length=1        -> 1      <-- ODD SURVIVES (docs say 'çift'/even)
  length=513      -> 513    <-- ODD SURVIVES
  length=515      -> 514    <-- CLAMPED
```

Doküman #27 "0–514, **çift**" diyor. `sanitize_accel_args` yalnızca
`[0, 514]`'e kırpıyor, tekliği düzeltmiyor (asıl düzeltme **parse**
tarafında: `config.cpp:225 a.length = (n/2)*2`). Yani bir **programatik**
yol (`sanitize_device_profile`, GUI) tek sayıyı geçirir ve
`sort_lut_data` (`config.cpp:372 const int n = a.length / 2;`) son elemanı
sessizce düşürür. Yalnız JSON yolunda düzeltiliyor. Etki düşük (son float
kullanılmaz), ama "zaten iki yerde" sınıfı.

---

## SORU 1–2 İÇİN ÖZET TABLO (ölçülmüş `sanitize_accel_args`)

| alan | doküman (#satır) | ÖLÇÜLEN sanitize | sapma |
|---|---|---|---|
| acceleration | reel (#72) | `[−∞ … 20]` | ⛔ **tavan belgelenmemiş**; negatif **serbest** (ölçüldü: `f(-1e12) = -1e12`) |
| exponent_classic | 1–10 (#73) | `[1, 10]` | — |
| exponent_power | 1e-4–5 (#74) | `[0.0001, 5]` | — |
| scale | 0–100, negatif→0 (#81) | `[0.01, 100]` | ⛔ **alt sınır 0 değil 0.01** (GUI ile eşleme, config.cpp:449) |
| decay_rate | ≥0 (#76) | `[0, 10]` | tavan belgelenmemiş |
| limit | ≥0 (#75) | `[0, 100]` | tavan belgelenmemiş |
| motivity | ≥0 (#77) | `[0, 10]` | tavan belgelenmemiş |
| gamma | ≥0 (#78) | `[0, 10]` | tavan belgelenmemiş |
| input_offset | ≥0 (#79) | `[0, 500]` | tavan belgelenmemiş (config.hpp:66-70 bunu **bilinçli** sayıyor) |
| output_offset | 0–100 (#80) | `[0, 100]` | — |
| sync_speed | ≥1e-4 (#82) | `[0.0001, 100]` | tavan belgelenmemiş |
| smooth | ≥0 (#83) | `[0, 1]` | tavan belgelenmemiş |
| cap.x | 0–500 (#84) | `[0, 500]` | — |
| cap.y | 0–100 (#85) | `[0, 100]` | — |

**Q2 — Simetri:** **14 alanın 13'ü tek taraflı `[0, M]`** — `|alt| = 0 ≠ M`.
Yalnız `acceleration` çift taraflı (`[-∞, 20]`, negatif serbest, ölçüldü).
Bu **bilinçli** bir politika ("hız daima ≥ 0", `config.cpp:456` yorumu),
ama brief'in "`|min| == max`" kuralına göre **13 alan asimetrik**.
Doküman bunu `≥ 0` yazarak zaten söylüyor; **sapma değil, kural ihlali** —
raporlanıyor ki bir sonraki ajan "buldum" demesin.

---

## KAPI

```
$ bash tests/run_tests.sh
=== Sonuç: 34164/34164 geçti ===
Sonuç: PASS — 3 backend test edildi ve birebir aynı sonucu üretti
SIMD parity kapısı: AVX2/SSE2/skaler birebir aynı ✓
rc=0

$ bash tests/oracle/run_oracle.sh
total rows compared : 1408
documented deviations: 79 (known_deviations.txt)
known deviations seen: 79
RESULT: OK — local port matches official reference (rel tol 1e-09) on every row
        outside the documented deviations (79 rows).
rc=0
```

Ek olarak 9 probe programı `/tmp/opencode/l03/` altında derlendi ve
çalıştırıldı (tam listesi: `probe_bounds`, `probe_sym`, `probe_sym2`,
`probe_math`, `probe_math2`, `probe_defaults`, `probe_capdefault`,
`probe_capjson`, `probe_bleed`, `probe_gui_bleed`, `probe_save`,
`probe_cli_dom`, `probe_docs`, `probe_clampdiv`, `probe_reset`).
Bunlar **çalışma ağacının dışında**, `g++ -std=c++20 -O2 -I include -I src`
ile derlendi; hiçbiri proje dosyasına yazmadı.

**ÜRETİLEN SAYILAR:** 34164/34164 assertion · 1408 oracle satırı, 79 sapma ·
26 `v2d_*` envanteri (16 kapsamlı / 8 canlı / 10 ölü, 0 ihlal).

---

## KAPSANMAYAN

- `src/config.cpp` **başka lane'ın** — `sanitize_accel_args` (sorular 1-2) orada
  olduğu için ÖLÇTÜM ama satır atıfları `config.cpp`'ye ait. Bir başkası
  `config.cpp`'yi sahipleniyorsa L03-03 (CLI/sanitize tavan uyuşmazlığı)
  ve L03-13 (lut_length tekliği) onun lane'ına devredilmeli.
- `cli/main.cpp` L03-03'ün **yarısı** — CLI domain bloğu orada; ben yalnızca
  davranışını ölçtüm, satırları okumadım/düzeltmedim.
- `gui/**` L03-01'in **tetikleyici** tarafı; lane sahibi `compute_curve`'ın
  `accel_args` **kopyası** üzerinden çalışmasını sağlayarak ya da
  `rawaccel-base.hpp:81`'deki `mutable`'ı kaldırarak kapatabilir. Karar
  L03 sahibi + core-types lane'ının.
- `accel-*.hpp` algoritma matematiği (classic/power/natural/jump/synchronous/
  lookup) lane'ın dışında; yalnız `synchronous`'ın `data[]` **yazımı** Q3'ün
  kanıtıydı.
- `include/simd_math.hpp` — başka lane.
- `tests/**` yazımı yok; testlerin neden görmediğini grep'le belgeledim.

---

## TEMSİL SINIRI

1. **Çalışma anı yok — GUI'yi çalıştırmadım.** L03-01'i `gui/graph.inl`'in
   çağrı zincirini (`on_graph_draw:812` → `compute_curve:124` → `au.init:56`,
   `args = dp.prof.accel_x` referansı, `cur_prof(S)` = `S->config.profiles[idx]`)
   okuyarak **ve aynı çağrı zincirini `save_config`'a bağlayan probeyla**
   kanıtladım. Gerçek GTK döngüsünde `gtk_widget_queue_draw`'ın ne zaman
   tetiklendiğini (idle/throttle davranışı) ölçmedim; "bir repaint yeter"
   iddiası zincir-mantığına dayanıyor. **GTK'sız** kanıt.
2. **Donanım yok — `/dev/uinput`, root, gerçek fare yok.** Hiçbir bulgu
   fiziksel girdi üzerinde doğrulanmadı; hepsi hesap düzeyinde.
3. **`sanitize_accel_args`'ın "hangi aralık" sorusu için ölçtüğüm şey
   `static` fonksiyonun davranışı**, `sanitize_device_profile` üzerinden.
   Doğrudan `sanitize_accel_args` çağıramadım (`static`, `config.cpp:400`);
   `sanitize_device_profile` (`config.cpp:1078`) onu çağıran tek public
   sarmalayıcı, dolayısıyla bu bir **beyaz-kutu değil, siyah-kutu** ölçüm —
   avantajı: varsayım yok, dezavantajı: `config.cpp`'de başka bir yol
   (`sanitize_accel_args`'ı atlayan çağrı zinciri) varsa onu görmedim.
   `AGENTS.md`'de geçen "B2: programmatically-built profiles … skip
   sanitize_accel_args" notu bu yüzden önemli, ama o yolun varlığını
   **ölçemedim**.
4. **Oracle'ın 1408 satırının `cap.y = 0`'ı hiç sarmadığını** yorumdan
   değil, `tests/oracle/oracle_cases.hpp:35` + `local.cpp:43`'ün grid'i
   açıkça atadığını okuyarak **çıkardım**; grid'de `cap_y = 0` satırı olup
   olmadığını `grep` ile saymadım. L03-02'nin "kapı görmüyor" hükmü bu
   çıkarıma dayanıyor.
5. **PS5.1/çalışma anı yok** — GUI dışında hiçbir yol etkileşimli
   çalıştırılmadı.
6. `direction()`/`rotate()` ölçümlerim ilk turda `%s` tampon paylaşımı
   nedeniyle **yanlıştı**; L03-11'de düzelttim. Aynı hatanın diğer
   probumlarda kalmadığını kontrol etmedim (`probe_math`'teki tek argümanlı
   çağrılar etkilenmiyor).

---

## AJ1 İÇİN NOT

`CRIT`/`HIGH` dediklerimi **kendi komutunla tekrar ölç** (brifing §8):

- **L03-01** için kritik satır `include/accel-synchronous.hpp:130`
  (`args.data[i] = static_cast<float>(fn(x));`) + `include/rawaccel-base.hpp:81`
  (`mutable float data[...]`) + `gui/graph.inl:124` (`compute_curve(args,…)`
  referans alıyor) + `gui/main.cpp:84` (`save_config(S->config, …)`).
  Doğrulama: `/tmp/opencode/l03/probe_save` tam çıktısı.
- **L03-02** için `include/rawaccel-base.hpp:78` tek satır
  (`vec2d cap = { 15, 0 };`) — `tests/oracle/ref/rawaccel-base.hpp:61`,
  `config/default.json:35`, `docs/research/parameter_index.md:85` ile karşılaştır.
  Doğrulama: `/tmp/opencode/l03/probe_capjson`.
- **L03-04** için `include/rawaccel.hpp:224`; doğrulama `/tmp/opencode/l03/mut/probe_daemon`
  (mutasyon `MUT-HIT` basmıyor) + `/tmp/opencode/l03/mut/pc` (pozitif kontrol basıyor).
  ⛔ Bunlar `/tmp` **kopyaları** üzerinde; `include/rawaccel.hpp`'ye dokunulmadı.

**Sessiz-yeşil sınıfı (brifing §3):** L03-01 (34 164 iddia yeşil, GUI
kullanıcının eğrisini siliyor) ve L03-04 (SM-5'in tek kaynağı hiç çalışmıyor).
İkisi de **bu lane'in kendi dosyalarının** sorumluluğunda — `rawaccel-base.hpp`'deki
`mutable` ve `rawaccel.hpp`'deki `reset_smoothers`.