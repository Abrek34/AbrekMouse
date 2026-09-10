# Numeric Precision — float vs double, ULP risks, math-library notes

> Author: Aj 8 (P88). Scope: how the accel formulas behave under IEEE-754
> double/float, where precision is lost, and what the build guarantees.
> References: IEEE 754-2008, Goldberg "What Every Computer Scientist Should
> Know About Floating-Point Arithmetic", glibc/libm accuracy notes, Arm
> Libamath ULP docs (see learning.md). Cross-references use `file:line`.

## 1. What the project uses

| Type | Used for | Where |
|------|----------|-------|
| `double` (53-bit, rel ε ≈ 1.11e-16) | All algorithm math, all config fields | everything in `include/accel-*.hpp` |
| `float` (24-bit, rel ε ≈ 5.96e-8) | Synchronous GAIN LUT cell storage only | `accel-synchronous.hpp:138` (`static_cast<float>`), array at `rawaccel-base.hpp:59` |
| `double` epsilon compares | Config equality, mode selection | `rawaccel-base.hpp:65` (`DBL_EPSILON_CMP = 1e-9`), `rawaccel.hpp:124,192-199` |

Good news: the official RawAccel reference itself also computes in `double` and
stores the synchronous LUT as `float` (`ref/accel-synchronous.hpp:143-145`), so
the port is **bit-for-bit faithful** in type choice. The oracle builds both
sides at `-O1` and still sees rel 1e-9 agreement on 884/915 rows (current tree).

## 2. Where precision is actually lost (hot path: `au.apply(v, args)` per event)

### 2.1 `pow` argument reduction — the dominant error source
`accel-classic.hpp:162-167` (`base_fn`):
```cpp
double p = std::pow(x - args.input_offset, args.exponent_classic); // :164
double r = ar * p / x;                                              // :166
```
- glibc's `pow` is good to ~0.5–1 ULP (relative ~1e-16), but for fractional
  exponents it internally computes `exp(y · log(x))`. The **absolute** error of
  `log` is multiplied by `|y|`; a 1-ULP error in `log` becomes |y|·ULP in the
  exponent, hence |y| ULP in the result (result relative error ≈ |y|·1.1e-16).
- With the sanitize ceiling `exponent_classic = 10` and a typical low `x − io`
  (≈0.001–1), the input to `pow` is tiny and the exponent large → worst-case
  relative error is a few ULP (≈1e-15). Gaining correctness beyond that
  requires `powl`/custom `exp(y·log x)` in long double or `fma`-compensated
  forms — **not worth it** for a mouse (see 4.1).
- Danger is not silent inaccuracy but **overflow**: at `exp = 10`,
  `pow(1e-3 − io, 10)` is fine, but `pow(x, 10)` for `x > 4.6` overflows;
  the guard at `:165` returns identity `0` instead of Inf — the right trade.

### 2.2 Division by small `x` (relative-error amplifier)
- `accel-classic.hpp:166` `ar · p / x`: at `x = 0.001` the dividend is scaled by
  1000. Relative error of `r` stays ≈ the relative error of `p` (division does
  not add relative error), but **absolute** gate-below speed gets noisy. This is
  the same amplification the classic `exp≤1` linear path avoids.
- `accel-power.hpp:106` `.. + constant / x` and `accel-classic.hpp:60`
  `constant / x + cap_y`: for huge `x` the `constant/x` term → 0 and adds
  nothing visible; for tiny `x` near 0 the term can dominate → the cap tail has
  relative error ≈ 1 ULP, no amplification beyond that.
- `accel-synchronous.hpp:151` `scalbn(x, −e) − 1`: this *extracts the mantissa*
  into [0,1). Exact except the final −1 (Sterbenz-lemma exact), so index
  computation is essentially exact; the precision concentration is all in the
  LUT itself (2.3).

### 2.3 The synchronous GAIN LUT — the only `float` path
`accel-synchronous.hpp:105-142`:
- Trapezoid integration with only **2 partitions per FP-range cell**
  (`interval = (b−a)/2`, `:117-124`) — same as the official reference
  (`ref/accel-synchronous.hpp:90-99`). The *numerical-integration* error of
  the sigmoid per cell is O(interval²)≈O(8e-3) relative locally, dominating all
  floating-point error combined. This is the true accuracy ceiling of the
  synchronous-gain mode — not precision, but **discretization**.
- `static_cast<float>` storage (`:138`) quantizes each cell to 24-bit → up to
  5.96e-8 relative error per cell, and `lerp` (`accel-lookup.hpp:34-40`)
  interpolates between two quantized endpoints. Summed with the trapezoid error,
  the final gain is accurate to ~1e-4 … 1e-3 relative in the worst (steepest)
  cells of the sigmoid — invisible to the mouse (which has integer-count output)
  and only marginally visible in the GUI curve.
- `data[idx]` is read back as `double` for lerp (`:158-159`); a `double` LUT
  would remove the quantization term but **not** the trapezoid term, so it
  would buy almost nothing while increasing memory traffic on the hottest array.

### 2.3.1 Measured N=2 vs N=4 vs exact integral (P102, Aj 8)

Reference params `sync_speed=5, smooth=0.5, motivity=1.5, gamma=1`, faithful
`fill_lut` reproduction (cross-checked against the real header — bit-equal to all
printed digits). "Exact" = 400k-point trapezoid of ∫ sigmoid over [0,x], i.e.
the ideal average-sensitivity the LUT approximates. `flt-ULP` = the gain error
expressed in float32 ULPs of the exact gain (float32 ULP at gain g = 2^(ilogb(g)−23)).

| ips | N=2 err% | N=4 err% | N=2 flt-ULP | N=4 flt-ULP | N2/N4 |
|-----|----------|----------|-------------|-------------|-------|
| 0.5 | 4.8e-05  | 5.5e-05  | 5           | 6           | 0.86  |
| 1   | 3.9e-04  | 1.7e-04  | 44          | 18          | 2.4   |
| 3   | 1.2e-01  | 6.0e-02  | 1.4e4       | 6.8e3       | 2.0   |
| 5   | 8.8e-01  | 4.4e-01  | 1.1e5       | 5.4e4       | 2.0   |
| 7.5 | 1.15     | 0.58     | 1.7e5       | 8.6e4       | 2.0   |
| 10  | 9.5e-01  | 4.8e-01  | 8.1e4       | 4.1e4       | 2.0   |
| 15  | 5.9e-01  | 3.0e-01  | 5.9e4       | 3.0e4       | 2.0   |
| 30  | 2.7e-01  | 1.3e-01  | 3.0e4       | 1.5e4       | 2.0   |
| 50  | 1.5e-01  | 7.7e-02  | 1.8e4       | 9.1e3       | 2.0   |
| 100 | 7.4e-02  | 3.7e-02  | 9.0e3       | 4.5e3       | 2.0   |
| 250 | 2.9e-02  | 1.5e-02  | 3.6e3       | 1.8e3       | 2.0   |
| 500 | 1.4e-02  | 7.2e-03  | 1.8e3       | 9.0e2       | 2.0   |
| 1e3 | 7.1e-03  | 3.5e-03  | 8.9e2       | 4.5e2       | 2.0   |
| 2e3 | 3.6e-03  | 1.8e-03  | 4.5e2       | 2.3e2       | 2.0   |
| 4e3 | 1.8e-03  | 9.3e-04  | 2.3e2       | 1.2e2       | 1.9   |

Readings:
- **The greedy ruler is % error, not ULP.** Discretization error (trapezoid vs
  exact) is a *bias*, not a rounding artefact: 10⁴–10⁵ float32 ULPs at the knee
  correspond to only 0.9–1.15% relative gain error. Float32 storage itself
  contributes ≤1 ULP (≤5.96e-8) per cell — invisible next to the trapezoid term.
- **Worst N=2 error is ~1.15% and only in the 5–15 ips knee** (sigmoid steepest,
  straddles `sync_speed`). Above ~250 ips (typical flick/track) it is <0.03%.
- **N=4 halves the error almost everywhere** (N2/N4 ≈ 2.0), i.e. one less halving
  of the `O(interval²)` trapezoid term. Single exception: the floor of the
  operating range (0.5 ips) where the cell is already flat (5–6 ULP) and N=4 is
  marginally *worse* (4.8e-05 vs 5.5e-05 %, ratio 0.86) — a measurement-level
  rounding artefact, far below perceptibility.
- Second-order nuance (measured, smooth=4/sync=0.05 test): when the operating
  speed is *far* from `sync_speed`, N=2 and N=4 converge (the curve is flat and
  the trapezoid is exact there); the halving is concentrated in the ips band near
  `sync_speed`, which is exactly where a synchronous user points their cursor.

### 2.3.2 float→double feasibility (P102) — what `data[514]` actually is

`accel_args::data` (`rawaccel-base.hpp:59`, `mutable float data[514]`, same in
the vendored ref `ref/rawaccel-base.hpp:65`) is consumed in three ways:

1. **Lookup mode (user-edited LUT):** GUI LUT editor `lut_get_points` /
   `lut_set_points` (`gui/graph.inl:307-343`) read/write `data[i]` directly and
   serialize `lut_data` to config JSON, **but only when `mode == lookup`**
   (`src/config.cpp:84`, gate `:84-90`, load `:124-150`). Lookup editor UI is
   hidden for synchronous mode (`update_lut_visibility`, graph.inl).
2. **Synchronous GAIN (internal LUT):** `fill_lut` overwrites `data` at
   construction from the params; `gain_apply` reads `data[idx]` per event. This
   is a *scratch buffer* — rebuilt on every `init_settings()` /
   `accel_union::init()` (SIGHUP reload, IPC push), **never persisted** (the
   `mode==lookup` gate above excludes it).
3. **Oracle/tests:** `const float*` reads in both lookup and synchronous paths;
   unit tests `memcpy` float arrays into `args.data`
   (`tests/test_accel.cpp:388,412,424,603`); the vendored reference reads it as
   `reinterpret_cast<const vec2<float>*>` (`ref/accel-lookup.hpp:57`).

ABI verdict:
- **There is no cross-process binary ABI.** All IPC is text JSON
  (`app_config_to_json`), and the struct is never memcpy'd/serialized by size
  across a boundary. `sizeof(accel_args)` growth (float 2056 B → double 4112 B)
  would not break any daemon/GUI/CLI channel.
- **There IS a source-level contract.** Every in-tree consumer assumes `float`
  elements; changing to `double` touches `accel-synchronous.hpp`,
  `accel-lookup.hpp`, `gui/graph.inl`, `src/config.cpp` (load casts to float),
  the tests listed above, and the oracle's ref-side `vec2<float>` cast. It is a
  mechanical ~7-file change, not a one-line edit.
- **It would break the oracle even at N=2.** Local double-storage vs ref
  float-storage makes LUT cells differ by up to 1 float ULP → applied gain
  drifts up to 5.0e-8 relative (measured, 21/23 grid speeds per sync_gain case
  > TOL 1e-9 → **63 rows across the 3 synchronous GAIN cases**). Same
  "drift-by-divergence" problem as N=4, at 50× smaller magnitude and ~zero
  perceptible benefit (≤1 ULP is rounding noise, already invisible).
- Config JSON: only lookup mode serializes `data`; a double array would dump
  more digits for lookup users but loads back as float today (`:148`), so the
  on-disk schema is unchanged. No version bump needed for the format — but the
  round-trip quieting that `LUT_EPSILON=1e-5` was tuned around
  (`rawaccel-base.hpp:66`) belongs to the float world; don't change it.

### 2.4 Construction-time solves (once per config load, not hot)
- `accel-classic.hpp:76-77,82,119`: `pow(a, exp−1)` — same argument-reduction
  concern as 2.1; a wrong `constant` here shifts the whole tail. Guarded by
  `isfinite` checks (`:75-77,83,118-120,156-158`).
- `accel-power.hpp:126`: `pow(gain/(n+1), 1/n)` for tiny `n` — exponent up to
  1e4 (sanitize floor `n=1e-4`); the reference overflows to Inf here, the port
  floors `n` to 1e-3 inside `scale_from_gain_point` (`:125`) and falls back to
  `scale=1` (`:127`). This is a **correctness winner** over the reference, not
  an approximation.

## 3. ULP risks near thresholds/branches

- **Cap-position branches** compare `x` against `cap_x`
  (`accel-classic.hpp:57`, `accel-power.hpp:96`, `:71`): these are computed
  from inverse-`pow` solves, so a speed equal to `cap_x` to within 1 ULP can
  land on either side. The two sides are **tangent-continuous by construction**
  (`constant = (base_fn(cap_x) − cap_y)·cap_x`), so a 1-ULP branch flip changes
  the gain by ~1e-15 — no visible discontinuity, no acceleration "pop". This is
  exactly why the GAIN caps feel smooth; the LEGACY hard caps
  (`accel-power.hpp:92`, `accel-classic.hpp:51`) do **not** have this property
  and a ULP-scale jitter around the cap is a genuine (inherited-from-reference)
  non-smoothness — inherent, not a port defect.
- **`x == syncspeed` branch** (`accel-synchronous.hpp:79` legacy): exact
  double equality is relied on the same way the reference does
  (`ref/accel-synchronous.hpp:53`); a ULP-off `x` takes the tanh path which is
  continuous there, so no discontinuity. Fine.
- **`idx < capacity−1`** (`accel-synchronous.hpp:157`): `clamp` to
  `range.size()−2` already bounds the index (P86, `:154`); the residual
  `idx_f − idx` ∈ [0,1) is the lerp fraction, exact to 1 ULP.
- **`minsd(base_fn, cap)` with NaN** (`accel-classic.hpp:51`): `min(·,NaN)`
  returns NaN per IEEE ordering rules, so any internal NaN would poison the gain
  (this is why the constructor NaN guards exist, `:75-77,83-84`). Same guard set
  exists in power (`:125-128`).

## 4. Math library / build-flag concerns

### 4.1 No `-ffast-math` anywhere — and that is correct
`scripts/build.sh:75` and `CMakeLists.txt` use `-std=c++20 -O3 -march=native`
(+ hardening). **No `-ffast-math`, no `-Ofast`, no `-funsafe-math-optimizations`.**
Consequences, all desirable for this code:
- No flush-to-zero: **subnormals survive**. The pipeline already guards the
  only subnormal hazard — `ips_factor = dpi_factor/time` overflowing to Inf for
  tiny `time` (`rawaccel.hpp:245-247`) — and the NaN/Inf escape check at
  `rawaccel.hpp:365-366`.
- NaN/Inf propagation stays IEEE-defined; every `isfinite` guard in the accel
  headers is meaningful.
- `-ffast-math` would have made `pow`/`exp` contraction unsafe and **broken the
  oracle guarantee** (the ref is compiled without it, so a fast-math local build
  could drift past TOL=1e-9). Keep it off.
- Note: GCC's default `-ffp-contract=fast` at `-O3 -march=native` may FMA-contract
  *arithmetic around* library calls in the hot binaries (`base_fn`'s `ar·p/x`,
  power's `scale·x`, cap tails). FMA keeps a full-precision product before the
  single rounding — strictly more accurate than separate mul+add — but can
  differ in the last ULP from the `-O1` oracle build. This is within noise
  (TOL 1e-9 » 1e-15) and **unobservable**; documented here for completeness.

### 4.2 libm functions used and their worst-case error
| Function | glibc/typical worst rel err | Used at |
|----------|-----------------------------|---------|
| `std::pow(x,y)` frac. y | ~0.5–1 ULP (1e-16…|y|·1e-16) | classic :164,173,178,182,188; power :106,115,126,132 |
| `std::exp` | ~0.5–1 ULP (1e-16) | natural :31,42,56; synchronous :76,90,95; jump :34,42 |
| `std::log` | ~0.5–1 ULP | synchronous :53,57,73,81 |
| `std::tanh` | ~1–2 ULP | synchronous :88,93 |
| `std::scalbn` / `std::ilogb` | exact (bit manipulation) | synchronous :147,151-154 |
| `std::hypot` | 1 ULP, overflow-safe | `math-vec2.hpp:12` (magnitude) |

No `sin/cos` in the hot path except GUI graph/rotation setup
(`math-vec2.hpp:40-41`) — not latency-critical.

## 5. Measured baseline (proof the port is not numerically worse)

- Oracle (current tree): 915 rows; 884 within rel 1e-9 of the official reference; 31 documented
  (see deviations.md). At `TOL=1e-11` the same 884 would still pass — the port
  does not add noise beyond libm ULP.
- All NaN/Inf guards are `isfinite`-based (no `x != x`), consistent with
  "-ffast-math would break" reasoning. `rawaccel-base.hpp:63-66` uses epsilon
  comparisons so JSON double→string→double round-trips (sub-ULP strings) cannot
  falsely unlink X/Y profiles.

## 6. Recommendations (accuracy, no deviation changes)

1. **(Optional, low cost)** in `classic::base_fn` rewrite `ar·p/x` (two
   roundings + div) as `ar·(p/x)` or `std::fma` chains only when the compiler
   hasn't already contracted — gains are ≤1 ULP, i.e. cosmetic. Do **not** add
   `-mfma`-specific math.
2. **(The real wins)** synchronous GAIN mode: (a) increase trapezoid
   partitions 2 → 4 (`accel-synchronous.hpp:117`), (b) optionally store the LUT
   as `double` (`:138`, `rawaccel-base.hpp:59`). Both are internal to the GAIN
   LUT and are *not oracle rows* (the LUT content is computed, not compared
   row-by-row) — they change no documented deviation.
   **P102 update:** (a) halves the error from 1.15% → 0.58% worst-case (knee) —
   below the ~1% perceptual threshold even before the change; (b) removes only
   ≤1 ULP of float noise. But neither is oracle-free: measured drift-vs-ref is
   69 rows/23 speeds (N=4, up to 5.7e-3 rel) and 21 rows/23 speeds *per case*
   (double storage, up to 5.0e-8 rel → 63 rows across the 3 synchronous GAIN
   cases). Execute only with a coordinated plan (Options
   A/C/D below; see deviations.md §P102).
3. Keep `-ffast-math` off; document it in CMake for future contributors.
4. If maximizer-level accuracy is ever needed for classic/power tails, the
   highest-leverage change is `pow(x,exp)·...` → `exp(exp·log(x))` with a
   compensated product — but measured benefit is ~1e-15 on a gain the mouse
   truncates to integers; not actionable today.

## 7. P102 decision — ranked roadmap for the synchronous GAIN LUT

Problem restated: the LUT uses 2-partition trapezoids (matches the vendored
official reference exactly); going local-only to N=4 feeds 69 drift rows to the
oracle; float→double is not oracle-free either (63 rows = 3 cases × 21). This ranks the escape
routes by risk ÷ benefit.

| Option | Change | Oracle impact | UX gain | Risk | Verdict |
|--------|--------|---------------|---------|------|---------|
| **D. no change** | none | green (31 known devs) | none (current truth) | none | **Recommended today** |
| **C. N=4 + documented deviation class** | local only, 2→4 | +69 known rows → ~100 total | knee err 1.15→0.58% | new deviation class to maintain; ref divergence grows over time; "what is the oracle verifying?" | viable, deferred |
| **A. N=4 on BOTH local + ref** | 2→4 in both | green; but **ref is no longer verbatim** | knee err 1.15→0.58% | loses the "matches the official reference" property; next `git clone` refresh silently reverts ref→N=2 and re-drifts 69 rows | viable only with a stamped "forked reference" marker + refresh guard |
| **B. keep N=2 + deviation rows** | none (list grows 31→~100) | green | none | deviation list balloons to ~100 rows with zero product benefit | worst option for zero gain |
| float→double | (independent of N) | +63 rows (3×21, 5e-8 each, ≈0 UX benefit) | ≤1 ULP, imperceptible | touches ~7 files + tests for nothing | **not worth it**; see §2.3.2 |

Decision logic:
1. **The error is sub-perceptual in every config.** Worst N=2 knee error 1.15%
   (relative gain); above 100 ips it is ≤0.074%; the mouse truncates to integer
   counts. Both N=2 and N=4 are far inside what a hand can resolve (sub-1% gain
   deltas are not adjustable in the GUI — spinners step 0.01 in gain, 1 free);
   the *shipped* Windows reference behaves exactly like N=2 today, so no user
   can perceive a "fix".
2. **Option D is therefore the honest recommendation:** no code change; document
   the measured surface so a future need (GUI Z-smoothness at smooth≈0, or a
   bug report requesting 1:1 parity with a hypothetical N=4 Windows build) has a
   ready plan.
3. **If/when a change is wanted, Option A is the cleanest** — but ONLY under a
   coordinated, explicit decision that the oracle's contract changes from
   "matches RawAccelOfficial/rawaccel" to "matches a precision-enhanced,
   MIT-forked reference". That requires a visible stamp (header note + LICENSE
   refresh caveat + CI guard that a `git clone` refresh fails loudly rather than
   silently re-drifting). N=4 was measured to halve the knee error to 0.58% —
   a genuine but imperceptible win.
4. **float→double is a dead end** until Option A lands: it carries 63 rows of
   drift (3 synchronous GAIN cases × 21) for ≤1 ULP of benefit, and the moment local stores double, the oracle
   cannot compare against a float-storing reference at all.
5. **B is never worth it**: growing the known-deviations list by 69 rows to
   document a change that produces zero perceivable difference.

Recommended eventual action, if any: **A** (with a `ref` "fork stamp"), executed
with `double`-storage folded in **only if** the ABI source-contract ramification
is accepted at the same time. Until then, **D**.

---

## 8. Cross-parameter-family interaction research (P110)

> Aj 8 (P110, R47). DOC-ONLY — no code changes. Every number below was computed
> with the **real headers** (`include/accel-*.hpp` via `accel_union::apply`, or
> `speed_processor` EMA smoothers) compiled at `-O2`, not by hand. Parameter
> families (natural/classic sweeps, power cap, synchronous halflifes, lookup LUT,
> output_dpi) are tuned independently by different agents; this section maps the
> surfaces where two families *interact*, catches cross-family hazards, and ends
> with interaction-aware defaults for new players.

### 8.1 natural "smooth" ↔ classic — numeric difference at same params

Both modes honour `input_offset` identically (flat gain `1.0` below it,
`accel-classic.hpp:45` / `accel-natural.hpp:29`) — that surface is **safe**. The
*surface that differs* is how each mode climbs to its asymptote. At
**defaults** (natural: `limit=1.5, decay_rate=0.1`; classic:
`acceleration=0.005, exponent_classic=2, cap{15,1.5} cap_mode=out`) both
approach `args.limit=1.5`, but via different machinery:

| ips | natural GAIN | classic GAIN | Δ (nat−cls) | Δ rel | note |
|-----|-------------:|-------------:|------------:|------:|------|
| 0.5 | 1.024187 | 1.002500 | +0.0217 | +2.16% | both barely off 1.0 |
| 5   | 1.183940 | 1.025000 | +0.1589 | +15.5% | classic still in its `x²` ramp |
| 50  | 1.450002 | 1.250000 | +0.2000 | **+16.0%** | classic cap knee is at `cap_x=50` |
| 100 | 1.475000 | 1.375000 | +0.1000 | +7.3% | classic tail `constant/x+cap_y` engages |
| **300** | **1.491667** | **1.458333** | +0.0333 | **+2.29%** | |
| **1000** | **1.497500** | **1.487500** | +0.0100 | **+0.67%** | |
| **4000** | **1.499375** | **1.496875** | +0.0025 | **+0.17%** | |

- At the requested 300/1000/4000 ips the two curves are within **2.3% → 0.2%** of
  each other — *numerically interchangeable in the flick/track band*.
- The real difference is **low-mid band** (5–100 ips): natural reaches ~97% of
  its asymptote by 50 ips (exponential `exp(−accel·t)`), while classic climbs
  `(acc·x)^2/...` and only rounds off after its `cap_mode=out` knee at
  `cap_x = gain_inverse(cap.y) = 50`. At 50 ips the gains differ by **16%**.
- **Was it documented?** No. `formulas.md` documents each mode's formula but not
  the *pairwise* reading. This row fixes that: *"classic's cap point and
  natural's decay corner land at different speeds even when both 'limit' to the
  same asymptote."* A GUI curve comparison will show a visibly steeper early
  natural curve for the same `limit`.

Numeric takeaway for tuning agents: if a preset swaps natural↔classic "keeping
the same limit", the *high-speed* feel is preserved (≤2.3% above 300 ips) but the
*micro/mid-speed* feel changes a lot (up to 16% at 50 ips). Match by feel at the
operating band, not by `limit` alone.

### 8.2 power cap/cap_mode ∩ input_offset — hyper-sensitivity hazard (WORST)

**Finding 1 — `input_offset` is a hard NO-OP in power.** Neither this port
(`accel-power.hpp` never reads `args.input_offset`) nor the reference
(`tests/oracle/ref/accel-power.hpp`) consumes it. It is stored-only
(`parameter_index.md` #18). Consequence: a config that sets `input_offset=20` in
classic/natural (protecting a slow 1:1 micro band) **silently loses that band**
when switched to power — the power curve is active from its `offset.x`
(determined by `output_offset`, not `input_offset`). Measured:

| ips | classic `input_offset=20` | power `input_offset=20` (IGNORED) |
|----:|--------------------------:|------------------------------------:|
| 5   | 1.0000 (flat, protected)  | **5.0000** (scale·x)¹ |
| 15  | 1.0000 (flat, protected)  | **15.0000** |
| 25  | 1.0050 (just kicked in)   | **25.0000** |

¹ example uses `scale=1, exponent_power=1`, far `cap_mode=in` cap.

The shipped **Apex preset still carries a dead `input_offset = 0.02`** (power
GAIN; `include/presets.hpp:125-126`) — a classic-preset leftover with zero
effect in power. Keep for parity or drop it, but never read it as a slow 1:1
floor: in power the floor is `output_offset`, not `input_offset`.

**Finding 2 — a >50× pre-cap gain is reachable, and by the GUI alone.**
The pre-cap gain curve is `gain(x) = (scale·x)^n + C/x`, i.e. ~`scale^n·x^n`.
The sanitizer used to have **no upper bound** on `scale`, `exponent_power`,
`cap.x`, `cap.y`, or `output_offset` (only negative→0 and floors), so a
hand-edited JSON or CLI value could push `pow(scale·x, n)` astronomically.
**R48 update (P120 / Aj 8 BUG-3):** the unbounded-escape class is now closed —
`sanitize_accel_args` clamps these five gain-driving fields to exactly the GUI
gauge maxima (`config.hpp:18-22`: `SCALE_MAX=100`, `EXP_POWER_MAX=5`,
`CAP_X_MAX=500`, `CAP_Y_MAX=100`, `OUTPUT_OFFSET_MAX=100`; `src/config.cpp`
`sanitize_accel_args`), the CLI `set-param` domains use the same limits
(`cli/main.cpp` `cmd_set_param` `range_ok` caps), and the GUI spinners are
bounded at the same values (`gui/ui_builder.inl` `make_spin`: scale 0.01..100,
power_exp 0.01..5, cap_x 0..500, cap_y 0..100, output_offset 0..100). So no
consumer can exceed what the GUI gauge itself allows — **but every combo below
is still in-envelope (valid at the boundary values themselves) and remains
GUI-typed**: the 10⁴× class survives at the exact maxima. Measured worst
configurable combos:

| combo (power + GAIN) | gain @0.5 | gain @1 | gain @10 | verdict |
|----------------------|----------:|--------:|---------:|---------|
| scale=100, n=2, cap_mode=**in**, cap_x=500 (GUI maxes) | 2.5e3 | 1e4 | 1e6 | **>50× everywhere** |
| scale=100, n=5 (GUI max exp), cap_mode=in, cap_x=100 | 3.1e8 | 1e10 | — | **>50× everywhere** |
| scale=100, n=2, cap_mode=**out**, cap_y=100 (GUI max) | 92.3 | 96.2 | 99.6 | **~100× (at the cap)** |
| scale=50, n=1, cap far (min combo at threshold) | 25 | **50** | 500 | crosses 50× at 1 ips |
| scale=100, n=2, **default out cap_y=1.5** | 1.486 | 1.493 | 1.499 | **safe** (control) |

The guard that keeps power sane is **the cap tail**, not anything built into
`scale`. With the default `cap_mode=out, cap_y=1.5` the curve is clamped to
~1.5×; push `cap_x` (in) or `cap_y` (out) far away "to let the curve breathe"
and the pre-cap region becomes an **uncapped `scale^n` curve**.

**Finding 3 — `output_offset` can pin the gain AND silently disable the cap.**
With the default tiny `exponent_power=0.05`, `offset.x = gain_inverse(oo) =
(oo/(n+1))^(1/n)` is astronomically displaced: `output_offset=50 → offset.x =
3.6e+33`, while the `cap_mode=out, cap_y=1.5` cap sits at `cap_x=1253`. Since
`operator()` returns `offset.y` for every `x ≤ offset.x`
(`accel-power.hpp:95`), the result is **gain pinned at 50.0× for every speed
0..4000 ips and the cap never engages** (silent bypass). Measured:
`output_offset=10 → gain=10` flat at all speeds; `output_offset=50 → gain=50`
flat at all speeds; cap values are inert.

**Verdict:** the worst cross-family hazard exists. Exact parameters:
`power + GAIN, scale=100, exponent_power=2, cap_mode=in/out with a
far-away cap (cap_x≥~60 in, cap_y≥~50 out)` → gain **10⁴× @ 1 ips**; or
**`power + GAIN, exponent_power=0.05 (default), output_offset≥50` → constant
50× over the entire speed range with the cap bypassed**. Both are GUI-reachable
and sanitized (no error), and both mean *every micro-movement is a teleport* —
the mouse is unusable, not subtly wrong.

> **User-guidance paragraph (for docs/help):**
> *Power mode is the one curve whose gain is `(scale · speed)^n` — it will not
> cap itself unless you leave the gain-cap engaged. Three rules keep it sane:
> (1) treat `input_offset` as **inert in power** — the field is stored but never
> read by this mode, so a slow 1:1 offset band configured in classic/natural
> disappears the moment you switch to power; (2) keep `scale ≤ ~2` and `cap_mode
> = out` with `cap_y ≤ ~2`: that cap tail is the only thing standing between
> you and a `scale^n` blow-up, so never raise `cap_x`/`cap_y` "to let the curve
> breathe" — a far-away cap means an uncapped curve, and with scale ≥ 50 the
> gain crosses 50× at 1 ips; (3) if you set a positive `output_offset`, think of
> it as a *floor* (the Apex preset uses 0.9), not a gain — combined with the
> tiny default `exponent_power=0.05` a floor ≥ 1 displaces the curve's start to
> absurd speeds, pins the gain at that floor forever, and disables the cap
> entirely. When in doubt: `output_offset=0, scale=1, exponent_power≈0.5–1,
> cap_mode=out, cap_y=1.5–2` and verify the curve actually reaches its cap with
> `rawaccel-cli show` / the GUI graph.*

### 8.3 synchronous sync_speed ∩ smoothing halflifes — responsiveness table

Measured with the **real** `linear_ema_smoother` (`rawaccel.hpp:33-82`,
`input_speed_smooth_halflife`) driven by the natural SDK step 0 → sync_speed at
1 ms events (trend 1.25 ms). Two metrics: the smoother's own settle time, and
the *gain-tracking* "muddy window" (duration where the applied synchronous gain
is >5% off its steady-state target at the knee).

| input halflife (ms) | sync_speed (ips) | settle 95% (ms) | settle 99% (ms) | gain-err>5% window (ms) |
|--------------------:|------------------:|----------------:|----------------:|-------------------------:|
| 0 (off) | 0.5 / 0.9 / 5 | 0 | 0 | **0.0** |
| 2 | 0.5 | 4.8 | 7.2 | 3.5 |
| 2 | 0.9 | 4.8 | 7.2 | 3.8 |
| 2 | 5.0 | 4.8 | 7.2 | 3.8 |
| 10 | 0.5 | 23.5 | 35.5 | 18.8 |
| 10 | 0.9 | 23.5 | 35.5 | 19.0 |
| 10 | 5.0 | 23.5 | 35.5 | 18.8 |
| 100 | 0.5 | 249 | 382.5 | 189.2 |
| 100 | 0.9 | 249 | 382.5 | 192.5 |
| 100 | 5.0 | 249 | 382.5 | 191.2 |

Readings:
- **`sync_speed` is latency-neutral only off the operating point** (0.5 vs 0.9
  vs 5.0 differ by <2% in the muddy window of this step-to-own-knee test). The
  sigmoid is a log-space function of `x/sync_speed`; its knee slope is the same
  relative steepness regardless of where you place it. `sync_speed` positions
  the *knee*, it does not gate *lag* — but **parking the knee on your tracking
  speed multiplies the halflife cost ~3.5×** (P118 nuance below).
- **Halflife is the entire story.** Settling is ~3.5× the halflife (10 ms → 99%
  in ~35 ms; 100 ms → 99% in ~383 ms). Halving the halflife halves the muddy
  window: 100 ms ≈ **190 ms of wrong gain** (>3 frames — floaty/gliding);
  10 ms ≈ **19 ms** (≈ one 60 Hz frame — near-imperceptible); off = 0.
- Peak transient gain error is ≈10% in all smoothing configs; what changes with
  halflife is **how long you live with it**, which is the responsiveness that
  players feel.

**P118 nuance — `sync_speed` parked on the tracking speed multiplies the
halflife cost (~3.5×).** The step-to-own-knee test above always lands on the
knee, so the EMA lag converts to gain error at constant steepness and the
positioning effect stays hidden. Re-measured (P118,
`/tmp/opencode/p118/p118_sync_ema.cpp`) at a **fixed tracking speed of
7.5 ips**, comparing a near and a far placement:

| halflife | sync_speed | ±5% settle | ±1% settle | near/far ratio |
|---------:|-----------:|-----------:|-----------:|---------------:|
| 100 ms   | 5.0 (near) | **148 ms** | **243 ms** | 3.5× / 2.3× |
| 100 ms   | 0.5 (far)  | 42 ms      | 106 ms     | — |
| 10 ms    | 5.0 (near) | 14 ms      | 18 ms      | 2.3× / 1.6× |
| 10 ms    | 0.5 (far)  | 6 ms       | 11 ms      | — |

A sync_speed near the tracking speed turns the *same* halflife into a **~3.5×
longer** wrong-gain window: the sluggish EMA input hangs on the steepest gain
slope, so its latency becomes gain error at full knee steepness. "sync_speed is
latency-neutral" is therefore **only true away from your operating speed**; a
100 ms halflife that feels like ~42 ms at a far placement feels like ~190 ms
when the knee sits on your tracking speed. Tuning corollary: place `sync_speed`
slightly off your typical tracking band — or, if you deliberately park it for
accuracy, treat the halflife as ~3.5× more expensive.

**Latency-safest combo recommendation:** `input_speed_smooth_halflife = 0`
(smoothing off) is the only zero-lag config; if jitter demands smoothing use
**input halflife ≤ 10 ms** (**never 100 ms**), and prefer `scale_smooth_halflife`
or `output_speed_smooth_halflife` (simple EMA, no trend term) over
input-speed smoothing when a smooth feel is the goal. Keep `motivity ≤ ~2` to
keep the knee slope — and the smoothing-amplified gain error — gentle, and
**keep `sync_speed` off your typical tracking speed** (a knee parked on the
operating point multiplies the halflife cost ~3.5×; P118 nuance above).

### 8.4 lookup LUT ∩ output_dpi — no double normalization (verified code path)

The apply path (daemon → pure-math → modifier) normalizes **exactly once on the
way in** and **once on the way out**; the LUT sits between them and never sees
`output_dpi`.

1. `dev.dpi_factor = NORMALIZED_DPI / dev.dpi` — precomputed per device
   (`daemon/daemon.cpp:651`).
2. Event → `flush_motion` → `apply_motion_math` (`daemon/motion_math.hpp:19-33`)
   → `modifier::modify` (`rawaccel.hpp:231`).
3. **Input normalization (once):** `ips_factor = dpi_factor/time`
   (`rawaccel.hpp:242`); velocity magnitude
   `abs_vel = |in.x · ips_factor · domain_weights|` (`:287-288`) → speed in
   **normalized-ips** (the reference 1000-cpi count-rate frame). This is the
   LUT's x-domain: `lookup::operator()` (`accel-lookup.hpp:61-121`) consumes
   exactly this value; its x-axis and (in velocity/gain mode) y/x gain are
   expressed in that same normalized-ips unit.
4. **Gain applied once:** `scale = 1 + (accel−1)·range_weight`
   (`rawaccel.hpp:324`), `in.x *= scale` (`:330`).
5. **Output rescale (once):** `dpi_adjustment = (output_dpi/NORMALIZED_DPI) ·
   dpi_factor` (`rawaccel.hpp:348-352`), net `output_dpi/dpi`, applied to both
   axes (Y extra × `yx_ratio`) **after** the gain. `output_dpi` is not read
   anywhere in steps 3–4 (`accel-lookup.hpp`, `rawaccel.hpp:286-343`).

Numeric check (real header): for the same LUT, `gain(x)` is bit-identical for
`output_dpi ∈ {500, 1000, 2000, 8000}` — output_dpi only rescales counts on the
count-sink side. **No double normalization exists in the apply path.** The only
way to *get* a double normalization is user-side: hand-scaling the LUT's `y`
values by `output_dpi` (folding the count rescale into the gain) would double
it. Guidance: keep the LUT in *normalized ips* on both axes and treat
`output_dpi` as an orthogonal count rescale (set it to your native `dpi` for a
pure "inch-for-inch" feel, or 1000 for the reference frame); do **not** bake it
into the LUT points. The final truncation to integer counts happens once in
`apply_motion_math` (`motion_math.hpp:46`).

### 8.5 Interaction-aware default tuning picks (new players, per mode)

Defaults chosen so that no cross-family hazard above can bite. Each row is the
"interaction-safe" starting point; deviations are only safe along the stated
axis.

| mode | interaction-safe default | why (which hazard it avoids) |
|------|--------------------------|------------------------------|
| **classic** | `acceleration=0.005, exponent_classic=2, input_offset=0..20, cap_mode=out, cap_y=1.5–2, gain=true` | GAIN cap tail bounds gain (§8.2 control row = safe); `input_offset` is honored. The `out` cap at `cap_y≤2` keeps worst gain ~2×. |
| **natural** | `limit=1.3–1.8, decay_rate=0.08–0.12, input_offset=0..5, gain=true` | Bounded by `limit` asymptote (no cap needed); `input_offset` honored; no `scale^n` blow-up exists in natural. |
| **power** | `scale=1 (≤2 max), exponent_power=0.5–1.0, output_offset∈[0,1], cap_mode=out, cap_y=1.5–2, gain=true` | Avoids the §8.2 class immediately: far cap + scale≥50, and the tiny-`n` + `output_offset≥1` plateau/cap-bypass. Never the default `n=0.05` with a positive floor. **Do not set `input_offset` expecting it to work here (inert).** |
| **synchronous** | `smooth=0.25–0.5, motivity≤2, gamma=1, sync_speed slightly OFF your tracking band (or on it with halflife=0), input_speed_smooth_halflife≤10 (prefer 0), scale/output halflifes optional` | §8.3: halflife is the only latency lever (10 ms ≈ one frame, 100 ms ≈ floaty) and **a knee parked on your tracking speed multiplies it ~3.5×** (P118 nuance); motivity≤2 keeps the knee slope — and smoothing-amplified error — gentle. |
| **lookup** | points in normalized ips on both axes; velocity mode `gain=y/x`; `output_dpi=1000` (or native dpi), **never fold output_dpi into LUT y** | §8.4: keeps the single-normalization contract; user-side double normalization is the only failure mode. |
| **noaccel / disable** | `raw_passthrough=true` preset for true 1:1 (P101) | Default config ≠ raw 1:1 (it normalizes to 1000 dpi); the preset is the only byte-exact 1:1 path. |

These are *picks*, not claims of optimality: they are the smallest-config
destinations from which a new player can tune one param at a time without
double-normalizing, capping-away, or unlatching the power gain.

**P118 audit note — serialized-only ("dormant") config fields.** Two fields are
stored and round-tripped by the config layer but **never read by the daemon**,
so setting them alone has zero functional effect on the mouse:
- **`use_raw_input`**: serialized as an app-level bool (`src/config.cpp`
  `save_config`/`load_config`), but the daemon keys raw 1:1 passthrough off
  **`raw_passthrough`** instead (`daemon.cpp` `flush_motion` /
  `process_device` raw branches); `use_raw_input` is *ignored*. The byte-exact
  1:1 path is the `raw_passthrough=true` preset (P101), never this flag.
- **`disable`**: serialized per profile (`src/config.cpp`
  `save_config`/`load_config` → `device_config::disable`), but **no code path
  reads it** — the daemon only ever applies dpi/poll per device; a
  `disable=true` device still receives full processing.

**R48 admin decision (BUG-4, Aj 1):** both flags are **kept**, deliberately,
as documented "dormant/saklı" config fields — neither wired to the daemon nor
removed. Since R48 they are **surfaced in every CLI status/show output**
(P120/A5-09: `cli/main.cpp` status `disabled:` lines + `--json` `use_raw_input`
/ per-profile `disable`), so the config can no longer silently hide them.
**User guidance:** do not expect either flag to change cursor behaviour — the
true raw/1:1 line is the profile-level **`raw_passthrough=true`** (the
`disable` preset, or `rawaccel-cli set-param <profil> raw true` + reload).
Treat `use_raw_input`/`disable` as stored-only metadata.