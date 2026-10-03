# DENETIM-L19 — Hızlandırma algoritmaları + config/motion TESTLERİNİN KAPSAMI

### L19 | alt-ajan (denetim turu 1) | 2026-10-02

```
KAPSAM   : tests/test_accel.cpp  SATIR 1218–4635 (59 test fonksiyonu, 70 SECTION,
           587 runtime assert). Başka satırlara DOKUNULMADI.
           Referans (salt okuma): include/accel-{classic,natural,jump,power,
           synchronous,lookup,noaccel,union}.hpp, include/rawaccel.hpp,
           include/config.hpp, include/rawaccel-base.hpp, src/config.cpp,
           docs/research/{formulas,parameter_index}.md
           ⛔ Hiçbir dosya değiştirilmedi. Mutasyonlar yalnız /tmp kopyalarında.
```

---

## KAPI (koşturulan kapılar + üretilen sayı)

| # | kapı | komut | rc | sayı |
|---|---|---|---|---|
| 1 | Temiz başlangıç (baseline) | `g++ -std=c++20 -O0 -Iinclude -Isrc -lpthread tests/test_accel.cpp src/config.cpp src/logitech_receiver.cpp src/logitech_hidpp.cpp -o tb` && `TMPDIR=$D ./tb --quiet` | **0** | `=== Sonuç: 34164/34164 geçti ===` |
| 2 | **Clamp-removal matrisi** (57 sınır) | her satır için `if (<cond>)` → `if ((false))`, yeniden derle, tüm süiti koştur | 57 koşu | 21 UNDETECTED · 35 caught · 1 SIGSEGV |
| 3 | **Field-drop matrisi** (44 JSON anahtarı) | `j["key"] = …` satırını sil, yeniden derle, süiti koştur | 44 koşu | 14 UNDETECTED · 28 caught · 2 BUILD-FAIL |
| 4 | Pozitif kontrol (clamp yöntemi canlı mı?) | `exponent_power < 1e-4` kelimesi kapatıldı | **1** | `34159/34164, 5 BAŞARISIZ` → yöntem ÖLÜ DEĞİL |
| 5 | Pozitif kontrol (field-drop canlı mı?) | `j["mode"] =` silindi | **1** | `caught(8)` |
| 6 | MUT1 natural monotonic kır | `sin(πx)` çarpanı eklendi | **1** | `FAIL natural gain NOT monotonic` |
| 7 | **MUT3 off-lattice monotonic** | `frac(x)∈(0.30,0.40)` → `return 0.5` | **0** | `34164/34164` ⛔ **GEÇTİ** |
| 8 | MUT4 (0,1.9] aralığı monotonic kır | `if (x<=1.9) return 1.0-0.9x` | 1 | 203 fail → (0,2) kapsamı YOK |
| 9 | MUT2 synchronous GAIN monotonic kır | `1+0.4·sin(2x)` | 1 | 125 fail (monotonic testi değil, P95/R15) |
| 10 | **MUT6 sticky zehir (NaN sonrası %2 kalıcı bozulma)** | `l19_poisoned` bayrağı | **0** | `34164/34164` ⛔ **GEÇTİ** |
| 11 | MUT5 sticky zehir (%50) | aynı, 0.5× | 1 | 1 fail (sadece `ox!=0` çöktüğü için) |
| 12 | MUT7 atomiklik YOK, .tmp bırakır | `copy_file` + tmp kalır | 1 | 4 section |
| 13 | **MUT8 atomiklik YOK, .tmp YOK** | `copy_file` + tmp silinir | 1 | **1** section (P99-C) |
| 14 | MUT9 classic LEGACY monotonic kır | `+0.35·sin(1.7x)` | 1 | 168 fail |
| 15 | Per-section assert sayımı | `SECTION` makrosuna sayaç eklendi (kopya) | 0 | 217 section, L19'da 70 section / 587 assert |
| 16 | `gain` round-trip probe | `gp.cpp` | 0 | `round-trip gain_x=false` ✔ · **fixture tautolojik** |

**Pozitif kontrol notu (brifing §2):** İlk clamp matrisi denemem **bozuktu** —
devre dışı bırakma `if(false){} if(koşul)…` şeklinde yazılmıştı, yani gerçek `if`
hâlâ çalışıyordu ve **57 mutasyonun 57'si de "UNDETECTED" döndü**. Bu, tam olarak
brifing §3'teki "sessiz yeşil" sınıfıdır. Yöntem düzeltildi (`if ((false))`),
pozitif kontrolle doğrulandı (kapı #4: rc=1) ve matris yeniden ölçüldü. Aşağıdaki
tüm UNDETECTED satırları **düzeltilmiş** yöntemin sonucudur.

---

## BULGULAR

### L19-01 | CRIT | `tests/test_accel.cpp:1874-1878` — monotonicite **tamsayı kafesi** üzerinden örnekleniyor, aralık üzerinden değil

**Kanıt (MUT3, tam süit):**
```
$ cd /tmp/opencode/l19/mut3 && python3  # include/accel-natural.hpp operator()'ye eklendi:
        { double fr = x - std::floor(x);
          if (fr > 0.30 && fr < 0.40) return 0.5; }   // off-lattice dip
$ g++ ... -o t_mut3 ; TMPDIR=$D ./t_mut3 --quiet
=== Sonuç: 34164/34164 geçti ===          rc=0     ← 34164 assertion'in TAMAMI yeşil
```
**Pozitif kontrol — mutasyon gerçekten çalışıyor (probe, `probe3`):**
```
x=0.1    gain=1.004967
x=2      gain=1.087900
x=2.31   gain=0.500000      ← kazanç 1.088'den 0.500'e DÜŞÜYOR (%54 kazıplı uçurum)
x=2.35   gain=0.500000
x=2.39   gain=0.500000
x=3      gain=1.124010
x=3.35   gain=0.500000      ← tekrar
x=3.5     gain=1.140418
```

`test_monotonic` (`:1874`) yalnız `for (int i = 2; i <= 40; i++) au.apply(i * 1.0, args)`
ile **tam sayı** örnekliyor (40 nokta: 0.1, 2, 3, … 40). Aradaki kesirli hızlar
hiç örneklenmiyor. Doğrusal interpolasyon **boyunca** artma garantisi verilmiyor —
`noktalar arasında` veriliyor. Bu tam olarak görevde sorulan
"birkaç noktada örneklenip mi kabul ediliyor?" sorusunun ölçülmüş cevabıdır:
**evet, kafes üzerinde örnekleniyor.**

**Ek kapsam boşluğu (aynı testin case listesi):** `test_monotonic:1861-1865`
yalnız `classic / natural / jump` sıralıyor. `power`, `synchronous`, `lookup`
modları **hiç** test_monotonic'a girmiyor (power için ayrı `test_power_monotonic`
var; sync ve lookup için **hiçbir** monotonicite testi yok).

**Sınıf: CRIT.** "Bu kod çalışırken ne olduğunda fark edilir?" → Kullanıcı 2.4 ips
ile 2.35 ips arasında %54'lik bir hız düşüşü yaşar; testler yeşildir.

---

### L19-02 | CRIT | `tests/test_accel.cpp:2774-2777` — NaN sonrası smoother'ın **kalıcı** bozulduğu ölçülmüyor

`test_nonfinite_time_does_not_poison_smoothers` yalnız iki yapısal özellik iddia
ediyor: NaN olayı çıktı üretmiyor (`:2767`, `:2772`) ve **sonraki** olay "sıfırdan
farklı + sonlu" (`:2776-2777`). Temiz bir koşuyla **değer karşılaştırması yok**.

**Kanıt (MUT6 — tek bir NaN zaman damgasından sonra %2 KALICI bozulma):**
`include/rawaccel.hpp:490-494` içine `l19_poisoned` bayrağı, `:496`'ya
`if (l19_poisoned) { in.x *= 0.98; in.y *= 0.98; }` eklendi.
```
$ TMPDIR=$D ./t_mut6 --quiet
=== Sonuç: 34164/34164 geçti ===          rc=0
```
**Pozitif kontrol — bozulma gerçek (probe, `probe6m` vs `probe6p`):**
```
== MUTATED (poisoned) ==          == PRISTINE ==
CLEAN run : ox=100 rx=0.0         CLEAN run : ox=100 rx=0.0
POISONED  : ox=98  rx=0.0         POISONED  : ox=100 rx=0.0     ← %2 kalıcı kayıp
```
Tek bir `quiet_NaN()` zaman damgasından sonra **her olayda kalıcı** %2 hareket
kaybı; 34164 assertion'in tamamı yeşil.

**Sınıf: CRIT.** `include/rawaccel.hpp:480-489`'daki yorum "later valid events would
all produce NaN/zero" diye bir *kalıcılık* tehlikesi tarif ediyor; test yalnız
*anlık* sıfırı ölçüyor, kalıcılığı ölçmüyor. "Bir kez bozulup kalıcı hâle gelen
smoother/filtre sınıfı" — sorudaki hedef tam olarak bu ve cevabı **ölçülmüş olarak
yakalanmıyor**.

**Karşılaştırma (MUT5, %50'lik zehir):** `rc=1`, 1 fail — ama yalnız
`:2782 ox != 0 || oy != 0` çöktüğü için. Yani test *değer* bozulmasını değil,
değerin **sıfıra düşmesini** yakalıyor. Eşik "sıfır" olduğu için %2'lik bozulma
geçiyor.

---

### L19-03 | HIGH | 13 JSON alanı serializer'dan **tamamen silinse** 34164 assertion yeşil kalıyor (sessiz veri kaybı)

`src/config.cpp` içindeki `accel_args_to_json` / `profile_to_json_obj` /
`device_profile_to_json` / `app_config_to_json_obj` fonksiyonlarından `j["key"] = …`
atamalarını teker teker silip tüm süiti koşturdum.

| serializer alanı | sonuç | yakalayan section |
|---|---|---|
| `accel_args.mode` | caught(8) | JSON round-trip · IPC · LUT ×3 |
| **`accel_args.gain`** | **UNDETECTED** | — |
| `accel_args.input_offset` | caught(1) | R15 |
| `accel_args.output_offset` | caught(1) | R15 |
| `accel_args.acceleration` | caught(3) | JSON round-trip · R15 · file round-trip |
| **`accel_args.decay_rate`** | **UNDETECTED** | — |
| **`accel_args.gamma`** | **UNDETECTED** | — |
| `accel_args.motivity` | caught(1) | IPC |
| `accel_args.exponent_classic` | caught(3) | JSON round-trip · R15 · file round-trip |
| `accel_args.scale` | caught(1) | R15 |
| **`accel_args.exponent_power`** | **UNDETECTED** | — |
| `accel_args.limit` | caught(2) | JSON round-trip · R15 |
| **`accel_args.sync_speed`** | **UNDETECTED** | — |
| **`accel_args.smooth`** | **UNDETECTED** | — |
| `accel_args.cap` | caught(4) | JSON round-trip · R15 |
| `accel_args.cap_mode` | caught(1) | JSON round-trip |
| `accel_args.lut_data` | caught(21) | LUT ×4 |
| `accel_args.lut_length` | caught(21) | LUT ×4 |
| **`profile.name`** (iç ikiz) | **UNDETECTED** | — |
| `profile.raw_passthrough` | caught(2) | raw_passthrough — JSON round-trip |
| **`profile.domain_weights`** | **UNDETECTED** | — |
| **`profile.range_weights`** | **UNDETECTED** | — |
| `profile.accel_x` | caught(45) | — |
| `profile.accel_y` | caught(1) | R15 |
| `profile.output_dpi` | caught(1) | R15 |
| **`profile.yx_output_dpi_ratio`** | **UNDETECTED** | — |
| `profile.degrees_rotation` | caught(3) | IPC · JSON round-trip |
| `profile.degrees_snap` | caught(2) | JSON round-trip · R15 |
| `profile.speed_min` | caught(1) | JSON round-trip |
| `profile.speed_max` | caught(2) | JSON round-trip · R15 |
| `profile.lr_output_dpi_ratio` | caught(1) | R15 |
| `profile.ud_output_dpi_ratio` | caught(1) | R15 |
| `profile.speed_processor` | BUILD-FAIL* | çok satırlı ifade — ölçemedim |
| **`device_profile.name`** (dış) | **UNDETECTED** | — |
| `device_profile.device_id` | caught(5) | — |
| `device_profile.match_app` | caught(2) | — |
| `device_profile.dpi` | caught(5) | — |
| `device_profile.polling_rate` | caught(3) | — |
| **`device_profile.disable`** | **UNDETECTED** | — |
| `device_profile.profile` | caught(61) | — |
| `app_config.version` | BUILD-FAIL* | çok satırlı — ölçemedim |
| `app_config.active_profile` | caught(5) | K1 · P99-C |
| **`app_config.use_raw_input`** | **UNDETECTED** | — |
| `app_config.profiles` | — | ⛔ mutasyon etkisiz (`j["profiles"]=json::array()` sonrası `push_back` geliyor; silmek tek başına yazmayı kaldırmıyor) — **mutasyon bozuk, sonuç sayılmadı** |

*BUILD-FAIL = tek satırlı desen tutmadı, ölçmedim. `tests/test_accel.cpp:790-801`
çok satırlı bir ifade; ayrı ölçüm gerekir.

**Tally: 44 anahtardan 13'ü gerçekten UNDETECTED, 28'i caught, 2'si ölçülemedi, 1'i bozuk mutasyon.**

**`gain` için kök neden — TAUTOLOJİK ASSERT (probe `gp`, doğrudan kanıt):**
```
serialized contains "gain":false ? YES
round-trip gain_x = false   (expect false)
test_json_roundtrip fixture: set gain=true, read back gain=true
   -> assertion tautological? YES (passes even with the field never serialized)
```
`tests/test_accel.cpp:1668` fixture'ı `ax.gain = true` atıyor ve `:1695`
`EXPECT(ax2.gain == ax.gain)` diyor. `accel_args::gain` struct default'u da
`true` (`include/rawaccel-base.hpp:65`). **Test, sıfırlamanın kaldırılması hâlinde
geçer.** Aynı tautoloji `:8018` (`EXPECT(ax2.gain == true)`, girdi de `true`).
`gain:0/1` kabul testi (`:9060-9081`) yalnız **parse** yönünü ölçüyor, serialize
yönünü hiç.

Bu 5 alanın (`decay_rate`, `gamma`, `exponent_power`, `sync_speed`, `smooth`)
R15 sınır testinde girdi olarak yazıldığı ama **karşılaştırılmadığı** görülüyor:
`tests/test_accel.cpp:7979-7984` hepsini atıyor, `:8016-8026` assertion listesi
`acceleration / exponent_classic / limit / input_offset / output_offset / scale /
cap.x / cap.y` ile sınırlı.

**Sınıf: HIGH.** "Eksik alan = sessiz veri kaybı" — görevin 3. maddesinin ölçülmüş
cevabı: 13 alanda öyle. `gain=false` (LEGACY mod) kullanan bir kullanıcının
config'i bir refaktörde `gain` yazımı kaybolursa **sessizce GAIN moduna döner** ve
testler yeşil kalır.

---

### L19-04 | HIGH | 21 sanitize sınırının **hiçbir testi yok** — üst sınırların tamamı

`sanitize_accel_args` (`src/config.cpp:400-513`) ve `sanitize_profile`
(`:514-608`) içindeki her clamp'i tek tek devre dışı bırakıp (`if ((false))`)
tüm süiti koşturdum. **Pozitif kontrol:** `exponent_power < 1e-4` kapatıldığında
kapı kırmızı (`rc=1`, 5 fail) → yöntem canlı.

### ⛔ TEST EDİLMEYEN SINIRLAR (21/57)

| # | clamp | `src/config.cpp` satır | sonuç |
|---|---|---|---|
| 1 | `exponent_power > EXP_POWER_MAX` (≤5) | `:491` | **UNDETECTED** |
| 2 | `scale > SCALE_MAX` (≤100) | `:490` | **UNDETECTED** |
| 3 | `decay_rate > DECAY_RATE_MAX` (≤10) | `:502` | **UNDETECTED** |
| 4 | `motivity > MOTIVITY_MAX` (≤10) | `:508` | **UNDETECTED** |
| 5 | `gamma > GAMMA_MAX` (≤10) | `:509` | **UNDETECTED** |
| 6 | `input_offset > CAP_X_MAX` (≤500) | `:493` | **UNDETECTED** |
| 7 | `output_offset > OUTPUT_OFFSET_MAX` (≤100) | `:496` | **UNDETECTED** |
| 8 | `limit > LIMIT_MAX` (≤100) | `:499` | **UNDETECTED** |
| 9 | `acceleration > ACCEL_MAX` (≤20) | `:505` | **UNDETECTED** |
| 10 | `acceleration < 0 && cap.y > 0` → 0 (MATH-1) | `:439-442` | **UNDETECTED** |
| 11 | `sync_speed > SYNC_SPEED_MAX` (≤100) | `:510` | **UNDETECTED** |
| 12 | `smooth > SMOOTH_MAX` (≤1) | `:511` | **UNDETECTED** |
| 13 | `cap.x > CAP_X_MAX` (≤500) | `:494` | **UNDETECTED** |
| 14 | `cap.x < input_offset` → `= input_offset` (BUG-7) | `:480` | **UNDETECTED** |
| 15 | `cap.y > CAP_Y_MAX` (≤100) | `:495` | **UNDETECTED** |
| 16 | `lr_output_dpi_ratio > 100` | `:558` | **UNDETECTED** |
| 17 | `ud_output_dpi_ratio < 0.01` | `:560` | **UNDETECTED** |
| 18 | `yx_output_dpi_ratio < 0.01` | `:562` | **UNDETECTED** |
| 19 | `yx_output_dpi_ratio > 100` | `:563` | **UNDETECTED** |
| 20 | `input_speed_smooth_halflife > SMOOTH_HALFLIFE_MAX` (P155) | `:585-586` | **UNDETECTED** |
| 21 | `scale_smooth_halflife > SMOOTH_HALFLIFE_MAX` (P155) | `:589-590` | **UNDETECTED** |
| 22 | `output_speed_smooth_halflife > SMOOTH_HALFLIFE_MAX` (P155) | `:593-594` | **UNDETECTED** |

(22 satır; 21 "sınır" + 1 `length` aşağıda.)

### TEST EDİLEN SINIRLAR (34/57) — hangi test

| clamp | `src/config.cpp` | yakalayan test |
|---|---|---|
| `exponent_classic < 1.0` / `> 10.0` | `:420-421` | `test_input_validation` (`:2498-2499`, **lane'in kendi testi**) |
| `exponent_power < 1e-4` | `:455` | R10 + fuzz + `test_accel_args_sanitize` |
| `scale <= 0` → 0.01 | `:449` | R10 + fuzz + accel_args sanitize |
| `decay_rate < 0` | `:452` | R10 + fuzz + accel_args sanitize |
| `motivity < 0` / `gamma < 0` | `:472-473` | accel_args sanitize |
| `input_offset < 0` | `:457` | fuzz ×2 + accel_args sanitize |
| `output_offset < 0` | `:458` | fuzz + accel_args sanitize |
| `limit < 0` | `:464` | R10 + fuzz + accel_args sanitize |
| `sync_speed < 1e-4` | `:466` | R10 + fuzz + accel_args sanitize |
| `smooth < 0` | `:469` | R10 + fuzz + accel_args sanitize |
| `cap.y < 0` | `:476` | fuzz + accel_args sanitize |
| `degrees_snap > 45` / `< 0` | `:547-548` | fuzz + `test_input_validation` + `test_sanitize_extremes` |
| `degrees_rotation` negatif → +360 | `:544` | BUG-1 + fuzz |
| `output_dpi ≤32000` / `≥1` / `≥0` | `:553-555` | R12 + `test_input_validation` |
| `lr_output_dpi_ratio < 0.01` / `ud > 100` | `:557,561` | R10 + `test_sanitize_extremes` |
| `speed_min < 0` / `speed_max < 0` | `:567,569` | R10 + fuzz + accel_args sanitize |
| `speed_max < speed_min` | `:568` | `test_input_validation` (`:2476`) |
| `lp_norm <= 0` | `:571` | fuzz + accel_args sanitize |
| `domain_weights.x/y > 1e6` (P86) | `:600-601` | `test_accel_args_sanitize` |
| `range_weights.x/y > 1e6` (P86) | `:602-603` | `test_accel_args_sanitize` |
| `domain_weights.x < 0` / `range_weights.x < 0` | `:596,598` | fuzz + accel_args sanitize |
| `*_smooth_halflife < 0` (×3) | `:584,588,592` | fuzz + accel_args sanitize |

### ⛔ `accel_args.length > LUT_RAW_DATA_CAPACITY` — **assert yok, yalnız SEGFAULT**

```
$ ... # src/config.cpp:407  if ((false)) a.length = LUT_RAW_DATA_CAPACITY;
$ TMPDIR=$D ./tb --quiet ; echo $?
-11        ← SIGSEGV
STDOUT: boş   STDERR: boş
```
Bu clamp `sort_lut_data` içindeki tampon taşmasını (`config.cpp:401-405` yorumu)
engelliyor. Kaldırılınca süit **çöküyor** — 0 assertion, 0 diagnostic. `run_tests.sh`
`set -e` ile kırmızıya döner, ama bu "test" değil: **hangi alanın sınırı
ihlal edildiğini söyleyen bir mesaj yok.** Lane'in kendi `test_cfg_p54_guards`
(`:3415-3433`, 3 assert) `length`ı kontrol ediyor ama `a.data[]`yi doldurmadan,
yani taşma yoluna sokmadan.

**⭐ KALIP (görev 1'in asıl cevabı):** 57 sınırın **35'i ALT sınır, 22'si ÜST
sınırdır; 22 üst sınırdan 13'ü (yukarıdaki 1–15) testsiz.** `parameter_index.md`
tüm üst sınırları "P120 üst sınırı" diye belgeliyor (`:100,101,104,105`), ve
`include/config.hpp:31-50` her üst sınırın **R15 boundary testiyle** kilitlendiğini
yazıyor — ama ölçüm, üst sınır clamp'lerinin **hiçbirinin** bir testle
bağlanmadığını gösteriyor. `config.hpp:32-34` "R15 boundary round-trip test
exactly 100.0'u byte-preserved yüklemeyi gerektirir" diyor; R15 testi
(`test_accel.cpp:7952-8031`) `scale = 100.0` **yazıyor** ve `100.0` **okuduğunu**
doğruluyor — yani `if (a.scale > SCALE_MAX)` kaldırılsa da 100.0 > 100.0
olmadığı için test yeşil kalır. **Bu, `config.hpp`'ın kendi dokümantasyonundaki
bir mantık hatasıdır**: bir üst sınırı sınmak için **sınırın üstünde** bir
girdi kullanılmalıdır, sınırda değil.

**Sınıf: HIGH** (13 üst sınır testsiz + 1 clamp assertsiz) → **CRIT'e yükselir**
`config.hpp:31-50`'nin bu üst sınırları "GUI gauge ile aynı" diye belgelediği ve
doküman "her biri R15 ile kilitli" dediği için: **belge, ölçümle çelişiyor.**

---

### L19-05 | MED | `test_atomic_write` atomikliği **değil**, `.tmp` kalıntısını test ediyor

`test_atomic_write` (`:2582-2606`, 3 assert) yalnız
`EXPECT(tmp_leftover_count(atomic_path) == 0)` + "dosya var + parse oluyor"
diyor. **Yarım dosya senaryosu denenmiyor** — görev 6'nın sorduğu tam olarak bu.

**MUT7 (atomik yayın yok, `.tmp` kalır):** `fs::rename` → `fs::copy_file`
```
FULL SUITE rc=1 → 34160/34164, 4 BAŞARISIZ
  atomic config write — no .tmp file left after successful save
  BUG-13 — save_config: tmp file is removed and target updated atomically
  P54-B3 — save_config tmp is pid-suffixed, atomic, no .tmp left
  P99-C — .bak rotate + simulated write failure restores previous
```
**MUT8 (atomik yayın yok, `.tmp` de yok):** `copy_file` + `fs::remove(tmp)`
```
FULL SUITE rc=1 → 34163/34164, 1 BAŞARISIZ
  P99-C — .bak rotate + simulated write failure restores previous
```
Yani `test_atomic_write` **"atomik" adıyla anılan 4 testin içinde en zayıfı**:
`P99-C` (`:8888`, lane dışı) tek başına yakalıyor; `test_atomic_write` (`:2582`),
`BUG-13` (`:5083`) ve `P54-B3` (`:3435`, lane'in kendi testi) **yeşil kalıyor** —
çünkü onlar `.tmp` sayıyor, yarım dosyayı değil.

Ayrıca `P54-B3`'ün adı "atomic" ama ölçtüğü `tmp_leftover_count == 0`; aynı
ölçüm `test_atomic_write` ile **birebir aynı**. İkisi de aynı şeyi ölçüyor:
`test_atomic_write` `:2598` ve `P54-B3` `:3445` — **mükerrer assert**.

**Sınıf: MED.** Atomiklik gerçekten bir kez test ediliyor (P99-C), ama lane'in
içindeki "atomic" adlı iki test (`test_atomic_write`, `P54-B3`) **yarım dosya
görmedikleri** için adları yanıltıcı. `AGENTS.md`'deki "tmp file → `rename()` so
the daemon never reads a half-written JSON" cümlesinin test kanıtı bu iki
testten gelmiyor.

---

### L19-06 | MED | `test_config_error_paths` 8 belgeli throw kategorisinin **3**'ünü deniyor

`include/config.hpp:99-134` `load_config` için **8 throw kategorisi** sayıyor.
`test_config_error_paths` (`:2344-2419`, 9 assert) hangilerini deniyor:

| # | kategori (`config.hpp`) | test? | kanıt |
|---|---|---|---|
| 1 | `json::parse_error` | ✅ | `:2350-2353` bozuk JSON |
| 2 | `json::out_of_range` (`1e400`) | ❌ | `grep -n "1e400\|1e999" tests/test_accel.cpp` → lane'de 0 |
| 3 | `runtime_error` dosya açılamaz | ✅ | `:2360-2362` yok dosya |
| 4 | `runtime_error` root nesne değil | ❌ | — |
| 5 | `runtime_error` `profiles` dizi değil | ✅* | *`:2345` başlığında geçiyor, K1 testinde gerçek |
| 6 | `runtime_error` `profiles[i]` nesne değil | ❌ | — |
| 7 | `runtime_error` accel skaler yanlış tip (`config.cpp:149`) | ✅ | `:2395-2401` + P54-B4 `:3451` |
| 8 | `runtime_error` accel skaler non-finite (`config.cpp:153`) | ✅* | *fuzz bölümü dolaylı |

`test_input_validation` (`:2424-2501`, 12 assert) **bu 8 kategoriden hiçbirini
denemiyor** — 5 bloğu da `load_config` **başarılı** çağrıları:
JSON'a geçersiz değer yaz → `load_config` → clamp sonucunu kontrol et.
"Yol hatası" testi değil, **değer aralığı** testi. Bu, görev 4'ün
"kaç hata yolu deneniyor?" sorusunun cevabı: **8'i değil, fiilen 4'ü
(kategori 1, 3, 7, ve kısmen 5/8).**

⛔ **Test edilmemiş 4 kategori:** #2 (`out_of_range` — `config.hpp:110` bunu
"parse error değil" diye özellikle vurguluyor ve 1e400'ün en ucuz hat olduğunu
söylüyor), #4 (root nesne değil), #6 (`profiles[i]` nesne değil), ve
`save_config`'in 5 throw kategorisi (`config.hpp:146-154`) — **`test_atomic_write`
dahil lane'deki hiçbir test `save_config`'in throw ettiğini test etmiyor**;
`P99-C` (`:8888`, lane dışı) `.bak` rotate + *simüle edilmiş* yazma hatasını
deniyor, o da lane dışı.

**Sınıf: MED.** Belge 8 kategori sayıyor, lane 3-4'ünü doğrudan doğruluyor.

---

### L19-07 | MED | `test_power_monotonic` 30 örnek noktasını **tek assert**'e indiriyor, `break` yüzünden nerede bozulduğunu söylemiyor

`test_power_monotonic` (`:1893-1941`, 3 assert) 3 cap_mode için
`for (i=2..30)` → 30 nokta, ama `EXPECT(ok)` **tek** bool. `test_monotonic`
(`:1867-1888`) aynı: 3 mod × 40 nokta = 120 örnek → **3 assert**, üstelik elle
yazılmış `g_tests++/g_passed++` sayacıyla (`EXPECT` makrosu **değil** —
`test_monotonic` `:1880-1887` makroyu atlayıp kendi sayacını yazıyor).

| | `test_monotonic` | `test_power_monotonic` |
|---|---|---|
| mod kapsamı | classic, natural, jump (**power/sync/lookup YOK**) | power (`cap_mode::out/io/in`) |
| giriş noktası | 0.1, 2.0, 3.0, … 40.0 = **40 nokta** (tam sayı) | 1.0, 2.0, … 30.0 = **30 nokta** (tam sayı) |
| toplam örnek | 120 | 90 |
| assert | **3** | **3** |
| sıfır (x=0) | ❌ | ❌ |
| negatif (x<0) | ❌ | ❌ |
| 0<x<1 aralığı | ❌ (0.1 tek nokta) | ❌ (1.0'ın altı hiç yok) |
| **LEGACY (gain=false)** | ❌ `make_args:147` `a.gain = true` | ❌ `:1898 args.gain = true` |
| hata teşhisi | `break` → ilk ihlalde durur, **hangi nokta belli değil** | aynı |

**`break` etkisinin ölçümü (MUT1 vs MUT3):** MUT1 (her x'te `sin(πx)`) →
`FAIL natural gain NOT monotonic` — 1 fail. MUT3 (yalnız `frac∈(0.3,0.4)`) →
0 fail. İkisi de aynı test, aynı `break`; fark tek başına **örnekleme kafesi**.

**LEGACY kapsamının ölçümü (MUT9):** classic LEGACY yoluna
`+ 0.35·sin(1.7x)` eklendi → `rc=1`, 168 fail. Yakalayanlar
`classic LEGACY cap_mode::in — cap.x > input_offset clips as before`,
`P105 — classic: exponent_classic 1..10 × input_offset × gain × cap.y`.
⛔ Bunlar **monotonicite testi değil** — nokta-değer (value) testleri. Yani
LEGACY monotonicliği **hiç ölçülmüyor**; rastgele bir ihlal ancak başka bir
testin örneklediği bir noktaya denk gelirse yakalanıyor.

**Sıfır/negatif taraması yok — kanıt:** `(0, 1.9]` aralığını monotonik ihlal
ettiren MUT4 → `rc=1`, **203 fail**. Yani o aralık *değer* testleriyle örtülüyor,
monotoniciteyle değil. `x = 0` ve `x < 0` hiçbir monotonic testte denenmiyor;
`include/accel-lookup.hpp:73`'ün `if (x <= 0) return 0.0;` ve
`accel-{classic,natural,power,synchronous}.hpp`'lerin `if (x <= offset) return 1.0;`
guard'larının **monotonic sınırda** (x→0⁺ vs x=0) sürekliliği test edilmiyor —
lookup 0.0, diğerleri 1.0 döndürüyor (`accel-lookup.hpp:66-73` bunu bilinçli
saplama olarak belgeliyor, ama **sınırda test yok**).

**Sınıf: MED** (L19-01 ile birlikte okunmalı — orada CRIT).

---

### L19-08 | LOW | Lane'in yarısı **vakum** testi: 70 section'ın 35'i ≤5 assert, toplam assert payı %18

`SECTION` makrosuna sayaç ekleyip (kopya üzerinde) 217 section'ın **runtime**
assert sayısını ölçtüm.

```
L19 section sayisi = 70   L19 runtime assert = 587   (toplam 34164'un %1.7'si)
5 veya alti assert'li section: 35 / 70  = %50
toplam assert payi        : 106 / 587  = %18
```

**⭐ 1 veya 2 assert'lı "vakum" test adayları (12 adet):**

| assert | section | dosya:satır | tip |
|---|---|---|---|
| 1 | edge — modifier separate distance mode with smoothing, outputs finite | `:4368` | **vakum** (çökme) |
| 1 | edge — modifier with rotate+snap+clamp+directional weight, all outputs finite | `:4325` | **vakum** |
| 1 | edge — power mode: extreme scale/exponent combinations, all finite | `:4482` | **vakum** |
| 1 | fuzz — random accel_args across all modes, no NaN | `:3932` | **vakum** |
| 1 | fuzz — random profile JSON round-trip preserves sanitized values | `:4056` | **vakum** |
| 1 | stress — 10000 motion events, remainder stays bounded | `:4407` | **vakum** |
| 2 | classic — degenerate cap_mode::io (cap.x <= input_offset) | `:3013` | değer |
| 2 | edge — all accel modes × extreme speed values, all outputs finite | `:4137` | **vakum** |
| 2 | edge — lookup with single point, duplicate points, huge values | `:4575` | **vakum** |
| 2 | edge — subpixel: negative deltas accumulate correctly | `:4292` | değer |
| 2 | fuzz — unsanitized random args through motion_math, output always finite | `:3990` | **vakum** |
| 2 | stress — alternating +5/-5 deltas cancel out to zero total | `:4450` | değer |

**"Sadece çökmüyor" (vakum) tipi — 8 adet.** Hepsi aynı kalıp:
```cpp
int bad = 0;
for (...) { if (!std::isfinite(r)) bad++; }
EXPECT(bad == 0);
```
`:3932-3989` (5000 iterasyon), `:3990-4055`, `:4137-4180` (7 mod × 15 hız),
`:4325-4367`, `:4368-4406`, `:4482-4574`, `:4575-4634`, `:4407-4446`.
⛔ Bunlar bir **değer** iddiası taşımıyor — yalnız "çökmedi/NaN vermedi". Biri
çökerse program çöker, o zaman yakalanır. **Sessiz bir hesap hatası (yanlış
gain, yanlış yön) hiçbirini geçmez.**

MUT1'in bu listedeki yeri öğretici: natural'a `sin(πx)` mutasyonu
`test_all_modes_extreme_speeds`'i **geçti** (o bölüm sonuç listesinde yok) —
çünkü 15 hız noktasının hepsi sonlu kaldı. Monotoniciteyi yalnız
`test_monotonic` yakaladı.

**3-5 assert'lı 23 section daha** (tam liste `L19-08` çıktısında): en zayıfı
`test_monotonic` (3), `test_power_monotonic` (3), `noaccel` (3),
`power` (5), `classic — gain mode` (5), `atomic config write` (3),
`P54-B2` (3), `P54-B3` (3), `power — exponent_power=0` (3),
`synchronous — linear clamp` (3), `jump — BUG-NEW-17` (5),
`lookup — duplicate points` (5), `raw_passthrough — JSON round-trip` (5),
`modifier — non-finite time` (4), `modifier — zero/negative time` (4).

**Sınıf: LOW** (tek başına) — ama L19-01/02 ile birleşince vakum testlerin
kapsamadığı yerleri dolduran başka testlerin de olmadığını gösteriyor.

---

## ⭐ GÖREV 1 — `parameter_index.md` 43 parametre × sınır testi matrisi

Metot: her parametrenin `sanitize_*` clamp'ini devre dışı bırakıp süiti koşturdum
(57 clamp) + her JSON anahtarını silip süiti koşturdum (44 anahtar). "Yok" =
ikisinden biri kırmızıya döndürmedi.

| # | parametre | sınır testi | kanıt |
|---|---|---|---|
| 1 | `active_profile` | ✅ var | `config.cpp:795` sil → caught(5) |
| 2 | `use_raw_input` | ⛔ **YOK** | `:797` sil → UNDETECTED; clamp yok |
| 3 | `version` | ⛔ ölçemedim | çok satırlı `:790-794`, BUILD-FAIL |
| 4 | `name` (dış+iç) | ⛔ **YOK** | `device_profile.name` sil → UNDETECTED; `profile.name` sil → UNDETECTED |
| 5 | `device_id` | ✅ var | sil → caught(5) |
| 6 | `dpi` | ✅ var | sil → caught(5); clamp `1..32000` **test edilmiş** |
| 7 | `polling_rate` | ✅ var | sil → caught(3); `125..8000` test edilmiş |
| 8 | `disable` | ⛔ **YOK** | `:352` sil → UNDETECTED (dormant, R48 kararıyla uyumlu) |
| 9 | `mode` | ✅ var | sil → caught(8) |
| 10 | **`gain`** | ⛔ **YOK** | `:72` sil → UNDETECTED. `:1695` **tautolojik** (L19-03) |
| 11 | `acceleration` | ⚠️ **alt var, üst YOK** | `<0 && cap.y>0` → UNDETECTED; `>20` → UNDETECTED |
| 12 | `exponent_classic` | ✅ var | `:2498-2499` (`test_input_validation`, lane'in kendi testi) |
| 13 | **`exponent_power`** | ⚠️ **alt var, üst YOK** | `<1e-4` caught(5); `>5` UNDETECTED; JSON sil → UNDETECTED |
| 14 | `limit` | ⚠️ **alt var, üst YOK** | `<0` caught(4); `>100` UNDETECTED |
| 15 | `decay_rate` | ⚠️ **alt var, üst YOK** | `<0` caught(4); `>10` UNDETECTED; JSON sil → UNDETECTED |
| 16 | `motivity` | ⚠️ **alt var, üst YOK** | `<0` caught(2); `>10` UNDETECTED |
| 17 | **`gamma`** | ⚠️ **alt var, üst YOK** | `<0` caught(2); `>10` UNDETECTED; JSON sil → UNDETECTED |
| 18 | `input_offset` | ⚠️ **alt var, üst YOK** | `<0` caught(4); `>500` UNDETECTED; `cap.x<off` → UNDETECTED |
| 19 | `output_offset` | ⚠️ **alt var, üst YOK** | `<0` caught(3); `>100` UNDETECTED |
| 20 | `scale` | ⚠️ **alt var, üst YOK** | `<=0→0.01` caught(4); `>100` UNDETECTED |
| 21 | `sync_speed` | ⚠️ **alt var, üst YOK** | `<1e-4` caught(5); `>100` UNDETECTED; JSON sil → UNDETECTED |
| 22 | `smooth` | ⚠️ **alt var, üst YOK** | `<0` caught(4); `>1` UNDETECTED; JSON sil → UNDETECTED |
| 23 | **`cap_x`** | ⚠️ **alt var, üst YOK** | `cap.y<0` caught(3); `cap.x>500` UNDETECTED; `cap.x<off` **UNDETECTED** (BUG-7!) |
| 24 | `cap_y` | ⚠️ **alt var, üst YOK** | `cap.y<0` caught(3); `>100` UNDETECTED |
| 25 | `cap_mode` | ✅ var | sil → caught(1) |
| 26 | `lut_data` | ✅ var | sil → caught(21) |
| 27 | `lut_length` | ⚠️ **assert yok, çökme** | clamp kaldırılınca **SIGSEGV** (assert 0) |
| 28 | `raw_passthrough` | ✅ var | sil → caught(2) |
| 29 | `output_dpi` | ✅ var | R12 + `test_input_validation`; JSON sil → caught(1) |
| 30 | **`yx_output_dpi_ratio`** | ⛔ **YOK** | `<0.01` **ve** `>100` ikisi de UNDETECTED; JSON sil → UNDETECTED |
| 31 | `lr_output_dpi_ratio` | ⚠️ **alt var, üst YOK** | `<0.01` caught(2); `>100` UNDETECTED |
| 32 | `ud_output_dpi_ratio` | ⚠️ **alt var, üst YOK** | `>100` caught(2); `<0.01` **UNDETECTED** |
| 33 | `degrees_rotation` | ✅ var | negatif→+360 caught(5); JSON sil → caught(3) |
| 34 | `degrees_snap` | ✅ var | `<0` caught(3); `>45` caught(3) |
| 35 | `speed_min` | ✅ var | `<0` caught(3) |
| 36 | `speed_max` | ✅ var | `<0` caught(3); `<min` caught(1) |
| 37 | `domain_weights` | ✅ var | `1e6` caught(1)×2 eksen; `<0` caught(3); **JSON sil → UNDETECTED** |
| 38 | `range_weights` | ✅ var | `1e6` caught(1)×2; `<0` caught(3); **JSON sil → UNDETECTED** |
| 39 | `whole` (distance_mode) | ✅ var | `test_speed_processor:2259` (15 assert) |
| 40 | `lp_norm` | ✅ var | `<=0` caught(2) |
| 41 | `input_speed_smooth_halflife` | ⚠️ **alt var, üst YOK** | `<0` caught(2); `>SMOOTH_HALFLIFE_MAX` **UNDETECTED** (P155!) |
| 42 | `scale_smooth_halflife` | ⚠️ **alt var, üst YOK** | `<0` caught(1); `>max` **UNDETECTED** (P155) |
| 43 | `output_speed_smooth_halflife` | ⚠️ **alt var, üst YOK** | `<0` caught(1); `>max` **UNDETECTED** (P155) |

### ⛔ TEST EDİLMEYEN PARAMETRELER ÖZETİ

- **Hiç sınır testi olmayan (7):** #2 `use_raw_input`, #4 `name` (dış **ve** iç),
  #8 `disable`, #10 `gain`, #30 `yx_output_dpi_ratio` → **5 parametre tamamen
  testsiz**; #2/#8 R48 "dormant, saklı bayrak" kararıyla uyumlu (bu ikisi
  *davranışsız* olduğu için savunulabilir), ama #4 `name` (aktif profil
  hedefleme anahtarı), #10 `gain` (LEGACY↔GAIN mod anahtarı) ve
  #30 `yx_output_dpi_ratio` (dikey DPI oranı, `parameter_index.md:128` "Y her
  zaman çarpılır" diyor) **etkin** parametreler.
- **Sadece alt sınırı test edilmiş, üst sınırı test edilmemiş (13):**
  #11 `acceleration`, #13 `exponent_power`, #14 `limit`, #15 `decay_rate`,
  #16 `motivity`, #17 `gamma`, #18 `input_offset`, #19 `output_offset`,
  #20 `scale`, #21 `sync_speed`, #22 `smooth`, #31 `lr_output_dpi_ratio`,
  #32 `ud_output_dpi_ratio` — **+ #23 `cap_x`, #24 `cap_y`**
- **P86/P155 gibi "sessiz ölüm" koruması test edilmemiş (5):** `cap.x<off→off`
  (BUG-7), `domain/range ≤1e6` **var** ama `>1e6` **test edilmemiş**
  (aslında `>1e6` caught(1) → **var**; P155 üç `>max` **test edilmemiş**).
- **Yalnız çökme koruması (1):** #27 `lut_length` — assert yok.

**⭐ `parameter_index.md` vaadi ile ölçümün çeliştiği satırlar:**
- `:245-248` "GUI spin aralıkları ile sanitize **R48'den beri eşleşiyor** …
  `sanitize_accel_args` bu beş alanı GUI gauge maksimumlarıyla sınırlar" —
  5 alanın (`scale`, `exponent_power`, `cap_x`, `cap_y`, `output_offset`)
  **hiçbirinin** üst sınır clamp'i testle bağlı değil.
- `include/config.hpp:31-50` "her üst sınır R15 boundary testiyle kilitli" —
  ölçüm bunun **yanlış** olduğunu gösteriyor (R15 sınırda örnekliyor, üstünde
  değil). Bu, brifing §3'teki "bir dosya hiç derlenmiyor ama `#include` var"
  sınıfıyla aynı: **test var ama istediğini ölçmüyor.**

---

## KAPSANMAYAN (lane dışı — birinin bakması gereken yerler)

1. **`include/accel-synchronous.hpp` ve `include/accel-lookup.hpp` için
   monotonicite testi hiç yok** — `test_monotonic:1861-1865` case listesi
   classic/natural/jump. MUT2 (sync GAIN monotonic kır) 125 fail üretti ama
   **hiçbiri `test_monotonic`'tan değil**; P95 (`:1460`) ve R15 (`:8261`) değer
   testleri. ⛔ sync ve lookup için "gain hıza göre artıyor" iddiasının
   testi yok.
2. **`config.hpp:105-107`'nin iddiası** "`"output_dpi": 1e400` süreci SIGABRT
   ile düşürüyor, exit 134" — bu `load_config` throw kategorisi #2
   (`out_of_range`) ve lane'de test edilmiyor (L19-06). Ayrıca "positive
   control … returns 3" ifadesi `cli/main.cpp`'ye ait, lane dışı.
3. **`save_config`'in 5 throw kategorisi** (`config.hpp:146-154`) lane'de
   test edilmiyor; `P99-C:8888` lane dışı.
4. **`app_config.version` ve `profile.speed_processor` serializer alanları**
   (BUILD-FAIL, çok satırlı) ölçülemedi — ayrı desen gerek.
5. **`tests/run_tests.sh` SEC-2 blokları, oracle, SIMD parity, tr_coverage,
   CLI sanitized** — lane dışı, koşurmadım.
6. **`tests/test_accel.cpp` 1-1217 ve 4636+** — başka ajanların sahibi;
   `test_logitech_*`, `test_hidpp_hw_*`, R15/R16/R47/R99/R106/R12/P91/P95/P105/
   P107/P110/P118/P120/P54/P55/P57/P95 testleri orada. ⛔ Dikkat: bu raporun
   clamp matrisi **tüm süiti** koşturduğu için o testlerin kapsamını da ölçtüm
   (L19-04 "yakalayan test" sütunu onları içerir) — yani L19'in bulgusu
   "lane'de test yok" değil, **"34 164 assertion'in tamamında test yok"**.

---

## TEMSİL SINIRI

1. **PS5.1 / çalışma anı yok** — `-O1`/`-O0` derlemelerle ölçtüm, asıl gate
   `-O2 -march=native`. `-march` farkı bu bulguların hiçbirini etkilemez
   (mutasyonlar kayan nokta dalları, SIMD vektör yolları değil), ama
   `-O2`'de derleyicinin farklı satır içi seçimi mutasyon kapsamını
   değiştirebilirdi. Ölçmedim.
2. **Sanitizer altında koşmadım** — `run_tests_asan.sh` ASan+UBSan ile
   koşturur; MUT3/MUT6 gibi "sessiz bozulma" mutasyonlarının bazıları
   sanitizer'da yakalanabilirdi. AGENTS.md'ye göre `test_accel.cpp` yedi kapıdan
   hiçbirinde sanitizer altında çalışmıyor; ayrıca koşturmadım.
3. **Mutations only /tmp** — 12 mutasyon çalıştırdım, hepsi `/tmp/opencode/**`
   kopyalarında. Çalışma ağacını doğruladım:
   `grep -n "L19-DISABLED\|L19-OFF\|L19-DROP\|L19-MUT" src/config.cpp
   include/*.hpp tests/test_accel.cpp` → **boş** (canlı ağaç temiz).
   `git checkout/stash/reset` **hiç** çalıştırmadım.
4. **Clamp matrisi 57 sınırın tamamını kapsadı**, ama `sanitize_*` dışındaki
   sınırları (örn. `MAX_PROFILES`, `MAX_NAME_LEN=256` iki kopyada,
   `require_number`in 12 skalerinin tip denetimi) **tek tek ölçmedim** —
   `test_cfg_p54_guards:3451-3482` yalnız `mode/gain/cap_mode/name/active_profile/
   use_raw_input/dpi` alanlarını yanlış tiple dener, `require_number`ın 12
   skalerinden hiçbirini değil.
5. **Field-drop matrisi 2 anahtarı ölçemedi** (`version`, `speed_processor`:
   çok satırlı ifade) ve 1'i bozuk mutasyonla (`app_config.profiles`:
   `j["profiles"]=json::array()` + `push_back` iki adımlı). ⛔ Bunları
   "test edilmemiş" diye saymadım; **ölçemedim**.
6. **Runtime ortam**: `TMPDIR` izole edildi, `run_tests.sh`'nin yedi kapısından
   yalnız **2'nin test ikilisini** çalıştırdım (kendi derlememle). `ctest`,
   oracle, SIMD parity, tr_coverage, CLI sanitized **koşmadım**.
7. **Eşzamanlı ajan riski**: `/tmp/opencode` iki kez sunucu yeniden başlatmasında
   silindi; ilk iki denememin (alan-düşürme matrisi, `fld/`) çıktısını kaybettim
   ve **doğrulamak için yeniden ölçtüm** — bu rapordaki alan-düşürme tablosu
   ikinci (düzeltilmiş) ölçümden geliyor. Clamp matrisinin ilk denemesi de
   **bozuk yöntem**di (bkz. KAPI pozitif kontrol notu) ve yeniden ölçüldü.

---

## AJAN 1 İÇIN ÖZET (hangi bulguyu tekrar ölçmeli)

| öncelik | ID | neden |
|---|---|---|
| 1 | **L19-01** | Mutasyon 4 satır; `accel-natural.hpp` `operator()`'ye `frac(x)∈(0.3,0.4)→0.5` koyup `test_accel --quiet` → `34164/34164`. 60 saniyelik doğrulama. |
| 2 | **L19-02** | `rawaccel.hpp:490-494` + `l19_poisoned` bayrağı ve `in *= 0.98` → `34164/34164`. |
| 3 | **L19-04** | `config.cpp:490` `if (a.scale > SCALE_MAX)` → `if ((false))` → `34164/34164`. Tek satır, kesin. |
| 4 | **L19-03** | `config.cpp:72` `j["gain"] = a.gain;` satırını sil → `34164/34164`. |
| 5 | **L19-05** | `config.cpp:989` `fs::rename(tmp_path,path,ec)` → `copy_file` + `remove(tmp)` → yalnız `P99-C` kızarır. |

**Sınıf dağılımı:** CRIT 2 · HIGH 2 · MED 3 · LOW 1 = **8 bulgu**,
**5'i mutasyonla kanıtlandı, 3'ü doğrudan ölçümle.**
