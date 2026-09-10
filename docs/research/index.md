# RawAccel Research — Index (team entry point)

> Aj 8 (P88) · maintained by the Research & Learning officer.
> All docs below are reference material; no code changes accompany them.

## Documents

| Doc | What you'll find |
|-----|------------------|
| [formulas.md](formulas.md) | Every mode's exact formula (classic/natural/power/synchronous/jump/lookup/noaccel), the GAIN vs LEGACY distinction, all three cap-mode branches, edge cases, and `include/accel-*.hpp` cross-references. **Start here.** |
| [deviations.md](deviations.md) | Root-cause of the 31 oracle rows (views: `tests/oracle/known_deviations.txt`), per-class KEEP/FIXABLE verdicts, reference values vs local, and why none should be "fixed". Plus the **P102 prospective** precision-enhancement drift class (N=2→4 / float→double), not applied. |
| [precision.md](precision.md) | float vs double, where ULP is lost in the hot path, `pow` argument-reduction error, the only `float` (synchronous LUT), measured N=2 vs N=4 vs exact-integral table, float→double ABI truth, `-ffast-math` analysis, accuracy recommendations + P102 ranked roadmap. **§8 (P110):** cross-parameter-family interaction research (natural↔classic, power cap hazards, sync×halflife latency, lookup×output_dpi) + interaction-aware defaults. |
| [learning.md](learning.md) | The RawAccel model origins (InterAccel, Quake/Source heritage), log-space sigmoid rationale, cap theory, and the literature + sources for education. |
| [parameter_index.md](parameter_index.md) | P103 (Aj 0): the single reference table of every configuration parameter — JSON key ↔ `set-param` key, stored type, post-sanitize domain, default, aliases, mode-applicability, and the not-settable / compile-time constants. Cross-referenced to `rawaccel-base.hpp`, `config.hpp`, `src/config.cpp`, `cli/main.cpp`. |

## One-line summary of findings

- The local math matches the official reference to rel 1e-9 on 884/915 oracle
  rows; the 31 remaining are **deliberate**: 23 from the classic `exp≤1`
  constant-gain convention (unreachable from any production config; reference
  behaviour is an out-of-domain artifact), 8 from the uniform `x≤0 → 1` guard
  (speed 0 is non-physical; reference yields 0 / 1÷motivity artifacts).
  → **Keep all 31.** No accuracy win is waiting on this surface.
- The two real accuracy ceilings are algorithmic, not precision:
  (1) synchronous GAIN LUT — 2-partition trapezoids + `float` storage;
  (2) nothing else — libm `pow/exp/log` are already 0.5–1 ULP on glibc.
- (P102) The synchronous LUT N=2 error is sub-perceptual: worst 1.15% at the
  sigmoid knee (5–15 ips), ≤0.074% above 100 ips; N=4 halves it. float→double
would save only ≤1 ULP. Neither is oracle-free (N=4 → 69 drift rows, double →
   63 rows = 3 × 21), so the recommendation is **D (no change)**; if ever wanted, only
  Option A (N=4 on both local and ref, with an explicit "forked reference"
  contract) is clean. See precision.md §7 and deviations.md §P102.
- (P110) Cross-parameter-family interactions: natural↔classic differ ≤2.3% in
  the 300–4000 ips band but up to **16% at 50 ips** (different cap/decay
  corners, same 1.5 asymptote). **Power is the only family with a real hazard:**
  `input_offset` is a NO-OP there (stored-only), a far-away cap + `scale≥50, n≥1`
  gives **10⁴× gain at 1 ips**, and `output_offset≥1` with the tiny default
  `n=0.05` pins gain at the floor for **all** speeds while silently disabling
  the cap — player-guidance in precision.md §8.2. sync×halflife: halflife is the
  only latency lever (10 ms ≈ one frame, 100 ms ≈ 190 ms of wrong gain);
  `sync_speed` is latency-neutral. lookup×output_dpi: no double normalization in
  the apply path (one input normalisation, one output rescale). See precision.md §8.

## Verification (current tree)

```
bash tests/oracle/run_oracle.sh   → 915 rows, 31 known, RESULT OK (rel 1e-9)
```

## Research agenda (open ideas, no commitments)

1. ~~Measure synchronous GAIN LUT accuracy (trapezoid vs exact integral) at 2 / 4
   / 8 partitions; quantify vs `float32`/`float64` storage.~~ **DONE (P102)**
   → knee error 1.15% (N=2) / 0.58% (N=4), halved everywhere; float storage
   ≤1 ULP; float→double is ABI-safe but not oracle-free. Ranked roadmap
   (A/B/C/D → recommend D, later A) in precision.md §7.
2. Compare glibc `pow` vs Intel/windows `pow` for the exact same formula —
   reproducibility across the Windows and Linux builds.
3. Document the classic `exp≤1` linear path in the CLI help text so raw-API
   users are not surprised.
4. Long-term: double-double (or `long double`) evaluation of the cap-tail
   constants — predicted gain ≤ 1 ULP, i.e. unobservable; only if profiling
   shows it free.
5. ~~Cross-parameter-family interaction research: natural↔classic effective-gain
   deltas, power cap/cap_mode∩input_offset hyper-sensitivity, sync×halflife
   latency, lookup×output_dpi double-normalization, interaction-aware
   per-mode defaults.~~ **DONE (P110, Aj 8)** → precision.md §8. Findings:
   natural↔classic ≤2.3% band (16% at 50 ips); power hazard real
   (10⁴×-and-up reachable, GUI-included; input_offset inert); sync latency
   lever = halflife (10 vs 100 ms: 19 vs 190 ms muddy window); lookup×output_dpi
   clean (single normalization each side). Interaction-aware defaults in
   precision.md §8.5.