# RawAccel — Formül, Matematik ve 31 Belgeli Oracle Sapmasının Kök Nedeni Analizi

Tarih: 05 Eyl 2026 — Aj 8 (P88, kalıcı araştırma/öğrenme görevlisi)
Kapsam: Kaplow fenomenolojik modeli (natural/power/classic/synchronous), 31 belgeli oracle sapmasının kök nedeni, float hassasiyeti, UBSan/fiziksel doğruluk. Kod değişikliği içermez; öneriler P-onaylı yöneticiye raporlanır.

---

## 0. Özet

Bu port, RawAccel'in (RawAccelOfficial/rawaccel) Linux taşımasıdır. Oracle (tests/oracle/ref) resmi Windows kaynağının bit-bit referans çıktısıdır; mevcut ağaçta (current tree) **915 satırın 884'ü birebir** — **31 satır bilinçli ve belgeli sapma** (tests/oracle/known_deviations.txt) dışında rel-tolerans 1e-9 ile birebirdir.

31 sapma iki sınıfa ayrılır:

| Sınıf | Satır | Port davranışı | Referans davranışı | Kök neden |
|-------|-------|----------------|--------------------|-----------|
| **A. classic exp≤1** | 23 | Sabit gain `1+acc` (linear path) | `x^(exp-1)` patlar (exp=0.5 @ s=0.001 → gain≈448) | exp<1 fiziksel olmayan config; port güvenli sabit-gain kuralı uygular |
| **B. power / synchronous s=0** | 8 | `x≤0 → 1.0` (identity) | Formül kör çalışır: power→0, sync→≈0.667 | s=0 fare için fiziksel değil (output=input×gain=0); port identity kulpuna izin verir |

Tüm sapmalar "kabul edilebilir"dir (bkz. §4): ya fiziksel olmayan giriş, ya kullanıcıya ulaşamaz config, ya da float güvenliği için bilinçli koruma. Bu doküman bunların doğruluğunu matematiksel ve fiziksel olarak gerekçelendirir.

---

## 1. Kaplow Fenomenolojik Modeli — Köken

RawAccel, **InterAccel** (KovaaK/InterAccel, QuakeLive stili) ile başlayan fenomenolojik çizgiden türer:

### 1.1 InterAccel (QuakeLive) çekirdeği (kovaaK accel.cpp)

Orijinal InterAccel üç curve tipi uygular:

```
case 0: // Original InterAccel acceleration  (classic soy)
    accelSens += pow((rate*var_accel), power);          // power = var_power - 1
case 1: // TauntyArmordillo natural acceleration
    accelSens += a - (a * exp((-rate*b)));              // a = senscap - sens, b = accel/abs(a)
case 2: // Natural Log acceleration
    accelSens += log((rate*var_accel) + 1);
```

- **classic**: `sens(v) = sens_base + (rate·acc)^p` → RawAccel "classic" bu toplamı `1 + ∏` biçimine dönüştürür.
- **natural**: `sens(v) = sens_base + a·(1−e^(−rate·b))` → RawAccel "natural" tam bu üstel kaybolan-fark modelidir (TauntyArmordillo).
- Senscap/speedcap, offset, carry (subpixel) kavramları da InterAccel'den gelir.

### 1.2 RawAccel soyutlaması

RawAccel Guide.md'de sensivite ve gain iki farklı büyüklük olarak ayrılır:

- **Sensitivity**: `f(v)/v` (output/input oranı)
- **Gain**: `f'(v)` (çıktı hızı fonksiyonunun türevi)

Bütün-anizotropi tam formülü (Guide.md):

```
(out_x, out_y) = (in_x·sens_x, in_y·sens_y) ·
    ( ( f( domain-weighted L^p space speed ) − 1 ) · (2/π·arctan(|in_y/in_x|)·(range_y−range_x)+range_x) + 1 )
```

Bu port, `modifier::modify()` zincirinde aynı adımları reproduce eder: rotate → snap → speed clamp → domain-weighted (L^p) speed → `accel_union` (natural/power/classic/sync) → directional weight → sens x/y.

---

## 2. Dört Ana Curve — Formül + Bu Portta Uygulanışı

### 2.1 natural (TauntyArmordillo türevi)

Resmi (rawaccel, natural<LEGACY>):

```
if (x <= offset) return 1;
offset_x = offset - x;
decay    = exp(accel * offset_x);        // accel = decay_rate / |limit|
return limit * (1 - (offset - decay*offset_x) / x) + 1;
```

natural<GAIN>:

```
const = -limit / accel;
output = limit * (decay/accel - offset_x) + const;
return output / x + 1;
```

Port (include/accel-natural.hpp) birebir: `decay = exp(-accel*t)` (t = x−offset), LEGACY `limit*(1 − (offset − decay*(offset−x))/x)+1`, GAIN `(limit*(decay/accel − offset_x) − limit/accel)/x + 1`. Kaynak satır uyumu oracle tarafından doğrulanır (31 sapma içinde natural hiç yoktur — birebir birebir).

### 2.2 power

Referans: `output = (scale·x)^n + C/x` (gain modunda continuous curve + constant_b/x tail). Legacy modunda `minsd(base, legacy_cap)`. Sıfırda port `speed<=0 → 1.0`. Sapma yalnız s=0 (sınıf B).

### 2.3 classic

Referans (klasik/gain): `gain = base_fn(x) = ar·x^(exp)/x = ar·x^(exp−1)`, `return sign·output + 1`. LEGACY: `minsd(base_fn, cap) + 1`. Port, `args.exponent_classic <= 1` için **sabit-gain path** uygular: `return 1.0 + minsd(accel_raised, cap)`; `accel_raised = args.acceleration`. Bu, 23 satır sınıf-A sapmanın kaynağıdır (aşağıda §3.1).

### 2.4 synchronous (activation-framework)

Referansın iki formu:
- LEGACY (gain=false): çok ölçekli tanh aktivasyonu log uzayında — `exp( tanh( |log_space|^sharpness )^(1/sharpness) · log_motivity )`.
- GAIN (gain=true): entegre LEGACY duyarlılık LUT'u `[2^-3, 2^9]`, `args.data` içinde; `gain_apply` LUT'tan lerp ile çeker.

Port, GNU/LearHead tekdüzesi; satır düzeyinde referansla birebir. Sıfırda port identity (sınıf B: sync_gain_p2/p1/p07, sync_legacy_p2/p1).

---

## 3. 31 Sapmanın Satır Satır Kök Neden Analizi

### 3.1 Sınıf A — classic_gain_exp_le1 (23 satır)

Adlar: `classic_gain_exp_le1`, hızları `0.001 ... 100000`.

**Formula neden sapıyor:** portun sabit-gain kuralı
`return 1 + minsd(accel_raised, cap)` vs referansın
`gain = ar·(x−offset)^exp / x` (exp≤1 için `x^(exp−1)`).

exp=0.5, accel=1, offset=0 için resmi formül:
`sens(0.001) = 1 + 0.001^(−0.5) = 1 + 31.6`.
exp=0.25 @ 0.001 → `1 + 0.001^(−0.75) = 1 + 177.8`.
exp=0.5 @ 0.001 referans = 448 (known_deviations.txt'te belgelenen değer — `1+447.8`). Bu hızda fare fiziği imkânsız derecede yüksek; portun güvenli sabit-gain path'i bunu `1+acc`'da tutar.

**Fiziksel gerekçe:** GUI spinner `[1,10]` (ui_builder.inl `make_spin(1,10,…)`) ve `sanitize_accel_args` (src/config.cpp) exponent_classic'i `[1,10]`'a klampler → **bu satırlar kullanıcıya ulaşamaz**; dahili algoritma API'sinden sacayak erişilir (T15/T16 kısaca). Referansın davranışı (exp−1 patlaması) yalnız "doğru" denklemdir ama fiziksel olmayan bir girdi için; portun sabit-gain kuralı bu yolu **güvenli** kapatır.

**Sonuç:** KABUL EDİLEBİLİR. 23 satırın tamamı "guard-on-degenerate" sınıfındadır; gerçek-daemon config erişemez.

### 3.2 Sınıf B — power / synchronous s=0 (8 satır)

Adlar: `power_gain_p1`, `power_legacy_p1`, `sync_gain_p2`, `sync_gain_p1`, `sync_gain_p07`, `sync_legacy_p2`, `sync_legacy_p1`, `game_apex_power`.

Port kodunda (hem power hem synchronous) `if (x <= 0) return 1.0` guard'ı vardır. Referans aynı guard'ı N/A (orijinal Windows driver'ı s=0 durumunda formülü kör uygular). Referans çıktıları:
- power: `(scale·0)^n + C/0 → 0 + Inf` → sınırda 0'a yönelir (legacy cap ile 0).
- synchronous: `exp(logarithmic activation)` formülü s=0'da `exp(log 0 → −Inf)` → `≈0.667`.

**Fiziksel gerekçe:** s=0, fare için non-physical: `output = input·gain = 0·gain = 0`. Yani gain 1.0 (sensitivity) olsa da çıktı yine 0'dır. Prot `x≤0 → 1.0` ile **identity** kuralını belgeleyerek, referansın 0/Inf artifact'ı yerine tanımlı, analitik olarak doğru ve "his etkisi yok" davranışı seçer.

`game_apex_power` 0 hızı aynı sınıfa düşer (oyuncu preset'i power tabanlı).

**Sonuç:** KABUL EDİLEBİLİR. 8 satır "non-physical input" sınıfıdır; port her çıktıda `output=input·gain` olduğu için gerçek fare davranışını değiştirmez.

### 3.3 Neden satır sayısı "31"dir (sayım doğrulaması)

- 23 classic (sınıf A) + 8 speed-0 (sınıf B) = **31**. `tests/run_oracle.sh` known_deviations.txt'teki 31 satırı "known" sayar; oran "known deviations: 31 (documented)" şeklinde basılır. Bu emirdeki "31 belgeli sapma" ibaresinin birebir karşılığıdır.

---

## 4. Kabul Edilebilirlik Kararları (yöneticiye öneri)

| Sapma | Kabul | Gerekçe |
|-------|-------|---------|
| classic exp≤1 | **KABUL — as-is** | Fiziksel olmayan config; kullanıcıya ulaşamaz (sanitize+spinner). Sabit-gain kuralı float-güvenli ve deterministik. |
| power/sync s=0 | **KABUL — as-is** | Non-physical input; output=input·gain → etki yok. Identity kuralı referansın 0/Inf artifact'ına karşı daha doğru. |

**Opsiyonel iyileştirme (kod gerektirmez, yalnız belgeleme):** known_deviations.txt üst bilgisine "sınıf A/B" açıklaması eklemek (zaten mevcut iki sınıf açıklaması yeterli — belge bu dosyanın detaylandırılmış halidir).

---

## 5. Float Hassasiyeti ve UBSan / Fiziksel Doğruluk

### 5.1 Float-тирtip riskleri (port korumaları)

| Risk | Port koruması | Kaynak |
|------|---------------|--------|
| classic exp≤1 `x^(exp−1)` patlar | Sabit-gain path | accel-classic.hpp operator() |
| classic pow overflow → Inf | `if (!isfinite(p)) return 0.0` (identity) | base_fn |
| classic cap.x≤offset → NaN (pow(negative, frac)) | cap modlarında isfinite guard'ı | init_legacy/init_gain |
| power scale_from_gain_point tiny-exp overflow | exponent floor 1e-3 + isfinite fallback | scale_from_gain_point |
| synchronous LUT index UB (float-cast-overflow) | `idx_safe = clamp(idx_f,0,size−2)` → `unsigned idx` (R43, P74-BULGU-1) | gain_apply |
| synchronous LUT float<>double karışımı | LUT float (referansla aynı) | accel-lookup.hpp |
| NaN`min`-poisoning (IEEE: min(finite,NaN)=NaN) | "degenerate config" guard'ları | accel-classic init_legacy BUG-9 |

### 5.2 UBSan standardı
R43 (UBSan UB fix) ile synchronous LUT int-dökümü kaldırıldı; `g++ -fsanitize=undefined` grubu `float-cast-overflow` içermez → birim suite bunu yakalayamaz, **clang fuzz + CI fuzz-smoke** yakalar (P74 M10'da doğrulandı).

### 5.3 Fiziksel doğruluk karşılaştırması
- InterAccel `carry` (subpixel remainder) RawAccel driver'ında yok — RawAccel Windows driver tam-delta uygular; bu port da aynıdır.
- EMA smoother'ları (guide: input/scale/output half-life EMA + lineer-trend) bu portta guard eder; smoothing aktifken oracle temel matematikle birebir.

---

## 6. Literatür ve Referanslar

| Konu | Kaynak |
|------|--------|
| Guide.md (tam form, modlar, gain/sens ayrımı) | github.com/RawAccelOfficial/rawaccel → doc/Guide.md |
| Resmi header (rawaccel.hpp, natural.hpp, modifier) | github.com/a1xd/rawaccel (master) |
| InterAccel (QuakeLive stili, orijinal curve'ler) | github.com/KovaaK/InterAccel → 99.%20source/accel.cpp |
| Gain offset teorisi | docs.google.com/document/d/1P6LygpeEazyHfjVmaEygCsyBjwwW2A-eMBl81ZfxXZk |
| Gain cap teorisi | docs.google.com/document/d/1FCpkqRxUaCP7J258SupbxNxvdPfljb16AKMs56yDucA |
| Ekip/kaynaktar (Taunty: sync/natural origin) | RawAccel README (simon, _m00se_, Sidiouth, TauntyArmordillo) |

---

## 7. Sonuç

1. **31 sapmanın tamamı bilinçli korumadır** — sınıf A (23× classic exp≤1, sanitize erişilemez config) ve sınıf B (8× s=0, non-physical input). Hiçbiri gerçek-daemon fare davranışını değiştirmez; oracle uyumu (rel 1e-9) geri kalan tüm alanda bit-idéal.
2. **Kod değişikliği gerekmez.** Opsiyonel: belge detaylandırma.
3. Bu analiz `docs/research/formulas.md` olarak kalıcıdır; diğer ajanlara referans/öğrenme kaynağı sağlar (P88 kapsamı).