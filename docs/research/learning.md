# Learning Notes — The RawAccel Model (team education)

> Author: Aj 8 (P88). Study notes + sources so every agent on the team can
> reason about the accel math on equal footing. Not code-facing; pair with
> formulas.md for the exact expressions and precision.md for the numerics.

## 1. What RawAccel *is* (one paragraph)

RawAccel is a Windows 10/11 kernel-mode driver (MIT, started 2020 by a1xd,
now RawAccelOfficial; ~2.5k★, 707 commits, v1.7.1 released 2025-07) that
transforms raw mouse motion. It started as a replacement for
[KovaaK/InterAccel](https://github.com/KovaaK/InterAccel) (QuakeLive-style
accel). Its core math is a set of **velocity-curve shapes** — hence its
vocabulary: **sensitivity** `S(v) = f(v)/v` (output speed ÷ input speed),
**gain** `G(v) = f'(v)` (slope of the output-vs-input velocity curve),
and "acceleration" as a *non-linear* `f(v)`. Not "acceleration = speed change"
in the physics sense. [source: the official Guide, §Philosophy]

The official definitions, from the Guide:
> For input speed `v` and Output Velocity `f(v)`, Sensitivity is `f(v)/v` and
> Gain is `f'(v) = d/dv(f(v))`.

The **Gain switch** decides whether the chosen shape is applied to the
sensitivity graph or the gain graph; for Classic/Linear/Power the two are
equivalent (there exists a parameter set reproducing the same velocity curve —
the code realises this as the LEGACY vs GAIN *cap/tail* forms), while for
Natural/Jump/Synchronous they are genuinely different curves.

## 2. Model origins — why logarithmic/sigmoid shapes "feel" right

- **Classic**: Quake 3 / Quake Live / InterAccel heritage — `S(v) = 1 + a·v^exp`
  (in the port: `1 + a·(v−io)^exp / v`). Matches the 2026 empirical FPS data
  poorly (only ~1% of surveyed pros use accel), but is the historical baseline
  and what InterAccel users expect.
- **Synchronous**/**Natural** (TauntyArmordillo, per credits): built on the idea
  that humans perceive speed and sensitivity **proportionally (logarithmically)**,
  not linearly — consistent with Weber–Fechner psychophysics
  (logarithmic perception of intensity) and with Fitts's law
  (logarithmic speed–accuracy relation in motor control). A
  logarithmically-symmetric sigmoid around one anchor ("synchronous speed")
  keeps the mapping "in sync" with natural estimation. `gamma` plays the role
  of the exponent (a synchronous curve with infinite motivity ≈ Power), and
  `smooth=0.5` corresponds to the `tanh()` activation (hence the code's
  `sharpness = 0.5/smooth`).
- **Power**: CS:GO / Source `m_customaccel 3` (`m_customaccel_exponent 1.05` =
  RawAccel exponent_power 0.05); CS:GO applies accel fps-dependently, so
  RawAccel simulates at a chosen fps with `scale = 1000/(in-game fps)` and
  output offset 1.
- **Natural**: sensitivity version found in Diabotical.

The "log-log sigmoid" phrasing in the Guide ("Sigmoid function on a log-log
plot") is the mathematical heart of the synchronous mode and is worth teaching
verbatim: in log(v) space the sensitivity is a symmetric S-curve from
`1/motivity` to `motivity`.

## 3. The cap theory (GAIN vs LEGACY caps)

Two official design docs are referenced in the Guide:
- *Gain offsets* (shift the gain graph without a discontinuity):
  https://docs.google.com/document/d/1P6LygpeEazyHfjVmaEygCsyBjwwW2A-eMBl81ZfxXZk
- *Gain caps* (cap the gain, smooth, for a given speed): the tail hyperbola
  `constant/x + cap_y` in the code is the realisation.
  https://docs.google.com/document/d/1FCpkqRxUaCP7J258SupbxNxvdPfljb16AKMs56yDucA

Key idea the port exploits (`formulas.md` §cap summary): every GAIN tail is
made **tangent-continuous** at `cap_x` by choosing
`constant = (base_fn(cap_x) − cap_y)·cap_x`, so value *and* derivative (gain)
match. This is why the GAIN caps "feel smooth" and why a ULP-scale branch flip
at `cap_x` is invisible (precision.md §3).

## 4. How official RawAccel (Windows) computes the same modes

The vendored reference under `tests/oracle/ref/` is the verbatim
`RawAccelOfficial/rawaccel` `common/rawaccel-base.hpp` + per-mode headers,
adapted with only a minimal g++ shim (`refcompat.hpp`). Facts:
- All math in **double**; the synchronous GAIN LUT stored as **float**
  (`ref/accel-synchronous.hpp:143-145`) — identical types as the local port.
- `minsd`/`maxsd`/`clampsd` and utility `min/max/clamp` are *order-dependent*
  templates using `(b < a) ? a : b`, i.e. NaN-aware ordering; the local
  `math-vec2.hpp:22-32` copies these semantics (`minsd = a < b ? a : b`).
- `lerp` in the reference is the *directional-clamping* lerp
  (`ref/accel-lookup.hpp:35-43`): `x = a + t·(b−a)` then clamped toward `b`:
  `(t>1) == (a<b) ? max(x,b) : min(x,b)`. The port copies it exactly
  (`accel-lookup.hpp:34-40`).
- Reference `ilogb`/`scalbn` are bit-shift based
  (`ref/utility.hpp:39-51`); the port uses `std::ilogb`/`std::scalbn` which are
  semantically identical for the LUT index math — verified by the oracle on the
  current tree (884/915 rows rel-1e-9 identical).
- Official limits: `POLL_RATE_MIN/MAX = 125/8000`, `LUT_RAW_DATA_CAPACITY = 514`,
  `NORMALIZED_DPI = 1000`, `MAX_NORM = 16` — mirrored exactly in
  `rawaccel-base.hpp`.

## 5. Numeric-precision literature (used in precision.md)

- D. Goldberg, *What Every Computer Scientist Should Know About Floating-Point Arithmetic* — ULP definition and error analysis:
  https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html
- IEEE 754-2008 semantics; **argument reduction** of `pow/exp/log` (the
  `result rel error ≈ |y|·ULP` rule for `pow(x,y) = exp(y·log x)`):
  classic references are the fdlibm/e_pow.c comments and glibc's `e_pow.c`.
- *ULP error and accuracy* (Arm learning path, gives the metric formula):
  https://learn.arm.com/learning-paths/servers-and-cloud-computing/multi-accuracy-libamath/ulp-error/
- V8 `pow` fidelity note (correctly-rounded vs fdlibm ~0.5–1 ULP):
  https://github.com/martinsbruveris/antimatter-dimensions-rust/blob/master/docs/worklog/2026-07-12-pow-fidelity-investigation.md
- MathWorks "ULP Considerations of Native Floating-Point Operators"
  (0.5 ULP for correctly-rounded basic ops; ~1 ULP for exp/log/pow):
  https://www.mathworks.com/help/hdlcoder/ug/ulp-considerations-of-native-floating-point-operators.html
- FastMath (hijimasa) measured table — `std::pow` fractional ≈ 5.9e-8 on
  float, double ~1 ULp; *"do not hand-roll `pow` as `exp(y·log x)` — glibc
  wins"* unless you also do double-double compensation:
  https://github.com/hijimasa/fast_math_cpp

Take-away for the team: the library functions the port calls are already
effectively optimal (0.5–1 ULP); any real accuracy gain is in **algorithmic**
choices (LUT discretization, trapezoid partitions) not in re-inventing `pow`.

## 6. Empirical/business context (why any of this matters)

- 2026 aggregated pro-settings survey: ~1% of CS2/Valorant/Apex pros use accel;
  the accel users are mostly Quake-rooted with accel-trained muscle memory
  (https://fpstrain.us/aim-training-mouse-acceleration-debate-2026.html). This
  frames our preset work (P60) — the shipped presets are *player choice*
  conveniences, not "correctness".
- Two-thirds power law / isochrony (movement-speed vs curvature + distance)
  is real published motor-behavioural work
  (https://www.sciencedirect.com/science/article/pii/S0166432824002183) and is
  the closest academic anchor to "speed shapes the response" — RawAccel's
  log-space design is consonant with it, though RawAccel itself is purely
  empirical.

## 7. Where to point a new teammate

1. `docs/research/formulas.md` — exact math, code cross-references.
2. `docs/research/precision.md` — float/double + ULP + build flags.
3. `docs/research/deviations.md` — why the oracle tolerates 31 rows.
4. Official Guide (vendored/online): https://github.com/RawAccelOfficial/rawaccel/blob/master/doc/Guide.md
5. `tests/oracle/ref/` — the reference implementation to diff against.
6. `tests/test_accel.cpp` — the 183 groups that pin all of the above down.