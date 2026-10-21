# opencode — BAĞIMSIZ 7 KAPI KOŞUMU (22 Ekim 2026, 00:11:23 → 00:15:20)

Ajanlardan bağımsız, kendi ölçümüm. Kaynak: git-yok (ağaç git deposu değil).
Önceki koşum (21 Ekim 23:48) L02-15 domain-weight kapısından önceydi ve
34417 assertion gösteriyordu; bu dosya en son koşumu gösterir.

| # | kapı | rc | süre | kanıt |
|---|---|---|---|---|
| 1 | build (native, 0 uyarı) | 0 | 91 s | olcum/analiz/gate-1.log |
| 2 | run_tests (34421/34421 + SIMD + HID++ 31/31 + config migration 32/32 + tracker) | 0 | 77 s | olcum/analiz/gate-2.log |
| 3 | oracle (1408 satır / 79 sapma) | 0 | 3 s | olcum/analiz/gate-3.log |
| 4 | simd_parity (envanter: 26 v2d_* · 16 kapsamlı · 8 canlı · 10 ölü) | 0 | 4 s | olcum/analiz/gate-4.log |
| 5 | tr_coverage (PASS) | 0 | 1 s | olcum/analiz/gate-5.log |
| 6 | cli_sanitized (ASan+UBSan, 34/34 komut gerçekten çalıştı) | 0 | 61 s | olcum/analiz/gate-6.log |
| 7 | tracker_bridge (17 kayıt kodda işaretli, AÇIK 0) | 0 | 0 s | olcum/analiz/gate-7.log |

**Sonuç: 7/7 yeşil.** Toplam süre 3 dk 57 s. AGENTS.md'deki süre iddiaları
(71.0 / 43.2 / 1.8 / 3.3 / 1.1 / 48.4 / 0.1) bu makinede 91 / 77 / 3 / 4 / 1 /
61 / 0 ile aynı sırayı koruyor.

## Bu turun bulgusu (L02-15)

`natural` GAIN dalı, `sanitize_accel_args()`'ın `decay_rate` tabanına rağmen
`domain_weights` ile küçülen etkin hızda sayısal olarak kararsızdı. 32000 DPI /
100 ms / 1 count + `domain_weight_x=1e-5` → x=3.125e-9; `limit=100`,
`decay=1e-4` için ham kazanç **5.76837158203125**, üretim çıktısı **0.18026161**
(beklenen **0.03125**). Başka yasal birleşimde negatif dip de vardı
(`limit=100`, `decay=1e-3`, `domain_weight=3e-5` → ham **0.80131785**).

Düzeltme `accel_union::apply()` içinde: `accel*x < 1e-9` ise natural GAIN
**1.0** döndürüyor. Doğrudan `natural::operator()` değişmedi; oracle bu yüzden
hâlâ 1408/79. Kanıt: `olcum/analiz/natural_small_speed_probe.cpp` (GCC ve
Clang probe derlemeleri rc=0; dört vaka da `OK`).

## Ek doğrulamalar (bu tur)

- `tests/run_tests_asan.sh` → **34421/34421** (ASan+UBSan).
- `tests/run_fuzz.sh 60` → config + accel harness'larında çökme yok
  (accel 3.991.933 run).
- `python3 olcum/aj2/prove_kod_ayni.py --sayi ...` → **14/14** sayısal iddia.
- `scripts/bench_hotpath.sh` → rc=0; tüm config'ler baseline içinde
  (noaccel 12.69, power-whole 21.91, classic 25.13, rot/snap/clamp 42.42,
  power-dual+4ema 100.77, apply_motion_math(full) 25.46 ns/olay).

## Doküman kayması (kapatıldı)

- `AGENTS.md` güncel sayıları: **153 `test_` fonksiyonu · 225 SECTION ·
  1653 EXPECT · 34421 runtime assertion**. Input-validation ve Key Design
  Decisions bölümlerine L02-15 kapısı eklendi.
- `olcum/aj2/sayisal_iddialar.txt` içindeki denetim regex'i 34417 → 34421.
- `CHANGELOG.md` L02-15 maddesi + güncel assertion sayısı; `FIX_LOG.md`
  round-21; `Bug Hata Raporları.md` TUR 57.
- `olcum/analiz/natural_small_speed_probe.cpp` yeni kanıt probu.

## "7 kapı yeşil"in kapsamı (değişmedi)

- `tests/test_accel.cpp` (34421 assertion) ana koşumda **sanitizer'sız**
  çalışıyor; sanitizer altında yalnız `tests/run_tests_asan.sh` ile koşar
  (yedi kapı dışı).
- `cli/main.cpp` → gate 6 ✓ (ASan+UBSan, 34/34 komut).
- E2E (`tests/run_e2e.sh`) root ve `/dev/uinput` gerektirir; bu ortamda
  çalıştırılamadı (sudo parolası yok), sonuç iddiası yok.
