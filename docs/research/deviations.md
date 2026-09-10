# Oracle Deviations — Root-Cause Report

> Author: Aj 8 (P88). Basis: `bash tests/oracle/run_oracle.sh` on the current
> tree → **1047 rows compared, 45 known deviations, RESULT OK** (rel tol 1e-9).
> Reference data: vendored RawAccelOfficial/rawaccel headers under
> `tests/oracle/ref/`. Grid: `tests/oracle/oracle_cases.hpp`.
> Known-deviation list: `tests/oracle/known_deviations.txt` (45 data rows).
>
> Historical note: this report was written when the oracle compared 915 rows /
> 31 deviations. The grid has since grown (esport speeds 2000/3000/4000, sync
> motivity/gamma variant cases, the P155 `io` cap.y=0 legacy case) and the
> deviation list now has **45 rows** — the reasoning below for the original
> classes still holds, and the newer rows are detailed in §Current (1047/45).

## TL;DR verdict

| Class | Rows | Verdict |
|-------|------|---------|
| **(A)** classic `exponent_classic ≤ 1` "linear path" — all speeds | 23 | **KEEP** — intentional port convention, unreachable in production (sanitize/GUI clamp exp to [1,10]); reference value is an out-of-domain artifact |
| **(B1)** power / synchronous at speed 0 | 9 | **KEEP** — uniform `x ≤ 0 → 1` guard; speed 0 is non-physical, reference artefacts (0 / 1/motivity) are meaningless. The original 7 rows grew to 9 when the sync motivity/gamma variants (`sync_gain_mot2_gam05`, `sync_legacy_mot05_gam2`) were added (P98/P163) |
| **(B2)** game_apex_power at speed 0 | 1 | **KEEP** — semantically matchable (ref = 0.9 offset floor) but not worth breaking the uniform guard; nil practical benefit |
| **(P155)** power `io` cap.y=0 identity guard | 12 | **KEEP** — local guards `cap.y≤0 → identity gain` (fix for the P96 silent-death bug); reference emits NaN/0 on that degenerate input. Not listable as a genuine deviation because `NaN > tol` is never true and `0` vs `1` would flag — these rows are documented here after the P155 fix |
| **Total (active)** | **45** | KEEP × 45 |
| **(P102, prospective)** synchronous GAIN N=4 → +69; float→double → +63 (3 × 21) | 0 (not applied) | **D: no change today** — sub-perceptual; see §P102 + precision.md §7 |

No deviation is recommended for reversal. Class A is a **deliberate algorithmic
convention**; B is a **domain guard**; the P155 class is a **defensive identity
guard** for a degenerate input that previously killed the mouse (P96-A). The
P102 class is prospective only (precision-enhancement), and is not applied.

---

## Breakdown by row

### Class A — `classic_gain_exp_le1` (23 rows, speeds 0.001 … 100000)

Grid case (`oracle_cases.hpp:71`): `classic, gain, acceleration=0.005,
exponent_classic=0.5, cap=(15,1.5), cap_mode=out`.

**Reference** (`ref/accel-classic.hpp:139-153`, GAIN]: `accel_raised =
pow(0.005, −0.5) = 14.142`; `cap.y = 0.5`, `cap.x = gain_inverse = 200`;
`constant = (base_fn(200) − 0.5)·200 = 100`. For `x < 200`:
`gain = 1 + 14.142/√x` — it **explodes** as x→0 (≈ 448 at 0.001) and decays
toward 1 as x→∞.

**Local** (`accel-classic.hpp:28-34,47-48`): constructor short-circuits on
`exp ≤ 1` to a constant-gain linear path → `gain = 1 + minsd(0.005, cap)=1.005`
at every speed.

| spd    | ref gain | local | spd     | ref gain | local |
|--------|----------|-------|---------|----------|-------|
| 0.001  | 448.2    | 1.005 | 250     | 1.90     | 1.005 |
| 0.005  | 201.0    | 1.005 | 500     | 1.70     | 1.005 |
| 0.01   | 142.4    | 1.005 | 1000    | 1.60     | 1.005 |
| 0.1    | 45.72    | 1.005 | 2000    | 1.55     | 1.005 |
| 0.5    | 21.00    | 1.005 | 3000    | 1.533    | 1.005 |
| 1      | 15.14    | 1.005 | 4000    | 1.525    | 1.005 |
| 3      | 9.165    | 1.005 | 5000    | 1.520    | 1.005 |
| 5      | 7.325    | 1.005 | 10000   | 1.510    | 1.005 |
| 7.5    | 6.164    | 1.005 | 100000  | 1.501    | 1.005 |
| 10     | 5.472    | 1.005 |         |          |       |
| 15     | 4.651    | 1.005 |         |          |       |
| 30     | 3.582    | 1.005 |         |          |       |
| 50     | 3.000    | 1.005 |         |          |       |
| 100    | 2.414    | 1.005 |         |          |       |

(all 23 speeds: 0.001, 0.005, 0.01, 0.1, 0.5, 1, 3, 5, 7.5, 10, 15, 30, 50, 100,
250, 500, 1000, 2000, 3000, 4000, 5000, 10000, 100000)

**Root cause:** the classic formula's *parameterization* assumes
`exponent ≥ 1`. For `exp < 1`, `base_fn(x) = a^(exp−1)·(x−io)^exp / x`
degrades to `(a·x)^(exp−1)` with a **negative exponent** → the "gain raised to
power" model references a ÷-by-speed relationship that no longer makes sense.
The reference computes it anyway, producing gain ≈ 448 at walking speed —
i.e. the cursor flies when stationary and flattens when fast, the inverse of
intended behavior. The port's documented convention treats `exp ≤ 1` as "linear
constant-gain" (a sound degraded mode).

**Reachability:** `sanitize_accel_args` clamps `exponent_classic` to `[1,10]`
(`src/config.cpp`) and the GUI spinner is `make_spin(1,10,…)`
(`gui/ui_builder.inl`) — **a real daemon/GUI/CLI config can never carry
exp < 1**. The 23 rows are only reachable through the raw algorithm API
(`accel_union::init/apply`), where the port keeps the safe behavior. There are
dedicated unit tests: "classic linear path (exp<=1) cap".

**Verdict: KEEP.**
- Intentional/accepted approximation: YES (documented port convention,
  AGENTS.md "known deviations" class 1).
- Implementation difference that could be fixed: technically yes
  (remove the short-circuit and let `base_fn` run), but that would
  *re-introduce* gain ≈ 448 at low speed and delete a tested behavior — a
  regression, not an "accuracy" gain.
- Rationale file:line — `accel-classic.hpp:28-34` (short-circuit),
  `:47-48` (constant-gain return).

---

### Class B1 — `power`/`synchronous` at speed 0 (7 rows)

| Row | ref gain | local | ref mechanism |
|-----|----------|-------|---------------|
| `power_gain_p1` 0    | 0.0   | 1.0 | `base_fn(0)` → `x ≤ offset.x` → `offset.y = 0` (`ref/accel-power.hpp:44-49`) |
| `power_legacy_p1` 0  | 0.0   | 1.0 | same, hard `minsd(0, cap)` |
| `sync_gain_p2` 0     | 0.667 | 1.0 | `data[0]/x_start` (`ref/accel-synchronous.hpp:128-130`) |
| `sync_gain_p1` 0     | 0.667 | 1.0 | idem (same params → same LUT) |
| `sync_gain_p07` 0    | 0.667 | 1.0 | idem |
| `sync_legacy_p2` 0   | 0.667 | 1.0 | `exp(−log(motivity)) = 1/motivity` from `log(0) → −∞` push (`ref/accel-synchronous.hpp:67-71`) |
| `sync_legacy_p1` 0   | 0.667 | 1.0 | idem |

**Root cause:** the reference blindly evaluates its formulas at `x = 0`:
- GAIN power `base_fn(0) = offset.y = 0` — the "output velocity 0" floor, so
  gain-measured = 0.
- synchronous GAIN falls below its LUT range (`ilogb(0) = −1023 < start`),
  extrapolates `data[0]/x_start = (1/1.5)`; the LUT cell trapezoid averages
  `1/motivity` over `[0, 2^−3]` → 0.667.
- synchronous LEGACY: `log(0) = −∞` forces the else-branch to a saturated
  tanh → `exp(−1·log(motivity)) = 1/motivity = 0.667`.

Local guards `x ≤ 0 → 1.0` (`accel-power.hpp:89`, `accel-synchronous.hpp:43`).
Note the sync rows p2/p1/p07 share identical parameters (the grid never sets
`motivity/gamma`; `exponent_classic` is unused by `activation_framework`), so
their ref values coincide.

**Verdict: KEEP.**
- Intentional/accepted approximation: YES — speed 0 is non-physical for a mouse
  (a zero-speed packet produces zero displacement regardless of gain; gain at
  v=0 is an undefined 0/0 limit).
- The reference's 0.0 / 0.667 values are artefacts of extrapolation and
  `log(0)`, and matching them would corrupt the `1.0` baseline the modifier
  relies on (`scale = 1 + (gain−1)·weight`, `rawaccel.hpp:296`).
- Dedicated tests: "synchronous power<1 guard", "power cap branch (all 3 cap modes)".

---

### Class B2 — `game_apex_power` at speed 0 (1 row)

Grid case (`oracle_cases.hpp:169-174`): `power, gain, scale=2.2,
exponent_power=0.8, input_offset=0.02, output_offset=0.9, cap=(28,2.2), out`.

| Row | ref gain | local |
|-----|----------|-------|
| `game_apex_power` 0 | 0.9 | 1.0 |

**Root cause:** for `x = 0`, local's uniform `x ≤ 0 → 1` guard bypasses power's
own offset floor. Reference `base_fn(0)` returns `offset.y = output_offset =
0.9` (`offset.x ≈ 0.191`, `offset.y = 0.9`).

**Verdict: KEEP (noted as the closest-to-reversible).**
- Unlike B1, the ref value 0.9 is *not* an extrapolation artefact — it is the
  legitimate output-offset floor of the apex preset's power curve.
- "FIXABLE" in the letter: one could special-case power modes to return
  `offset.y` at x=0. But (a) it breaks the uniform `x ≤ 0 → 1` exploit/NaN
  guard, (b) gain at v=0 multiplies zero displacement, so the 0.9 vs 1.0 delta
  has **zero physical effect** on output, (c) the oracle already accepts the
  row as documented. Cost > benefit.

---

## Cross-class summary (active classes)

- 23 rows = one grid case (classic A) × one formula convention.
- 9 rows = the single `0` speed value in `default_speeds()` applied across
  power (2 rows: gain+legacy × p1) + synchronous (7 rows: `sync_gain_p1` /
  `sync_gain_p2` / `sync_gain_p07` / `sync_gain_mot2_gam05` + `sync_legacy_p1` /
  `sync_legacy_p2` / `sync_legacy_mot05_gam2`).
- 1 row = the apex power preset @0 (`game_apex_power`).
- 12 rows = the P155 `io` cap.y=0 legacy identity-guard case
  (`p155_io_cap0_legacy` × 12 speeds, 0.0001 … 100000).
- All classes are *deliberate* and each has dedicated regression tests;
  none is a numerical drift or a porting omission. The oracle therefore
  measures what it should: **on every other row the local port matches the
  vendored official reference to rel 1e-9** (i.e. bit-for-bit modulo the last
  ULP of glibc `pow`/`exp`).

## "Which deviations are potentially FIXABLE?" — 1-2 sentences each

1. `classic_gain_exp_le1` (23 rows): FIXABLE only by deleting the exp≤1
   short-circuit — but the reference value at exp=0.5 is a physically absurd
   gain≈448 at 0.001 ips, so matching it is a regression; keep the port
   convention.
2. `power_gain_p1`/`power_legacy_p1` @0 (2 rows): FIXABLE trivially by removing
   the `x ≤ 0 → 1` guard, but that reintroduces the 0-gain artefact and opens a
   NaN path for the modifier; keep the guard.
3. `sync_*` @0 (7 rows): FIXABLE trivially, but you would be faithfully
   reproducing an extrapolation artefact (`data[0]/x_start`) born of `log(0)`;
   keep the guard.
4. `game_apex_power` @0 (1 row): the ONLY deviation that is semantically
   matchable (ref 0.9 is the curve's legit output-offset floor); still not
   worth it — uniform guard is more valuable than a meaningless-to-output 0.9.
5. `p155_io_cap0_legacy` (12 rows): NOT FIXABLE — the reference emits NaN/0 on
   `cap.y=0` with `io`; local guards it to identity gain (P155/P96-A fix for
   silent mouse death). These rows can never appear as "genuine deviations"
   (`NaN > tol` is never true, `0` vs `1` would), so they are documented here
   and asserted by the dedicated unit section.

**Conclusion for the manager (Aj 1):** all 45 deviations are intentional
safety/convention choices; none should be "fixed" toward the reference. If the
goal is *accuracy*, the actionable surface is not these rows but (a) tighter
expression of the classic/power base functions to reduce intermediate rounding
(see precision.md), and (b) increasing the synchronous LUT sample density (2
trapezoid partitions per cell and `float` storage are the dominant accuracy
limits of that mode) — neither touches the documented deviations.

---

## P102 (Aj 8) — prospective precision-enhancement drift class (not active)

This is **not** a current deviation. It documents what *would* happen to the
oracle if the synchronous GAIN LUT local-side accuracy were improved (N=2→4
partitions, or `float`→`double` storage) while the vendored official reference
(tests/oracle/ref/, verbatim, commit 53a7213, N=2 + float) stays untouched.
Keep this section until a coordinated decision is made (see precision.md §7).

### N=4 partitions — measured drift: **69 rows across the 3 sync GAIN cases**

Grid cases affected: `sync_gain_p2`, `sync_gain_p1`, `sync_gain_p07`
(`oracle_cases.hpp:115-117`) — all three share identical parameters (synchronous
GAIN ignores `exponent_classic`, so the LUT is identical), so they drift in lock
step. **23 of the 24 grid speeds** exceed TOL 1e-9 (all except speed 0, which is
already a documented deviation): 0.001 … 100000. Measured maximum relative
N2-vs-N4 delta: **5.68e-3** (0.57%) at 7.5 ips (the steepest sigmoid knee).

| speed band | N2-vs-N4 rel delta | note |
|------------|--------------------|------|
| 0.001–0.1 | 2e-9 (just over tol) | LUT nearly flat → both ≈ 1/1.5 |
| 0.5–1     | 8e-8 … 2.3e-6 | transition to knee rising |
| 3–50      | 6.2e-4 … 5.7e-3 | **sigmoid knee — the accuracy win lives here** |
| 100–100000| 3.5e-7 … 3.7e-4 | high-speed decay |

Per-case row count = 23; total = 3 × 23 = **69**. The deviation list would grow
31 → 100 rows.

Significance: N=4 is a strictly better approximation to the exact integral
(measurement in precision.md §2.3.1 — error halves everywhere), so these 69 rows
would be a *precision-enhancement* class, all of one kind, of the same
"documented port convention" family as `classic_gain_exp_le1` — **but with a
different root cause**: those 23 rows are an intentional safety convention,
these would be an intentional accuracy upgrade.

Assessment as a deviation class:
- **Acceptability (Option C):** mechanically trivial — one comment class + the
  row list. Maintains a green oracle (TOL 1e-9) while local becomes the *more
  accurate* side. Risk: the class is *large* (69 rows), the divergence is *new*
  (reference no longer represents the local product's behavior — one more step
  toward "the oracle verifies a fork", currently documented as verbatim),
  and the benefit is **imperceptible** (0.57% → 0.29% knee error, below hand
  resolution).
- **Option B** (keep N=2, document the *potential* delta): pointless — documents
  a nonexistent deficit with no benefit; not recommended.
- **Option A** (N=4 on BOTH local and ref): oracle stays green AND the deviation
  list stays at 31, but the vendored ref is no longer verbatim MIT upstream —
  a `git clone` refresh (LICENSE "Refresh procedure") silently reverts it to N=2
  and re-introduces 69 rows of *unknown* drift (CI failure — loud, good) unless
  someone pre-adds them (silent, bad). Requires a "forked reference" stamp and a
  refresh guard. See precision.md §7 for the ranking and recommendation.

### float→double storage — measured drift: **63 rows (3 × 21, ≥1 float ULP each)**

Even with N=2 kept on both sides, a local `double data[]` (ABI analysis in
precision.md §2.3.2) differs from the float-storing reference by ≤1 float ULP
per LUT cell → applied gain drifts up to **5.0e-8** (50× TOL) on 21 of the 23
grid speeds per sync_gain case (all but 0.001/0.005, which round identically).
All three synchronous GAIN cases share the identical LUT (synchronous GAIN
ignores `exponent_classic`), so they drift in lock step → **63 rows would join
the deviation list (31 → 94)**. This route is *also* not oracle-free. Benefit of
double storage is ≤1 ULP of rounding noise — the smallest acceptable gain
change on the board. Not recommended independently of Option A.

**Verdict:** do nothing today (Option D). If a change is ever justified
(perceptible-mode regression call, or parity with a hypothetical enhanced
Windows build), the only clean path is Option A with an explicit "forked
reference" contract. Details and table: precision.md §7.