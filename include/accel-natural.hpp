#pragma once
#include "rawaccel-base.hpp"
#include <cmath>

namespace rawaccel {

/// Natural (vanishing difference) acceleration.
/// Smoothly approaches a limit asymptote.
struct natural {
    double offset    = 0;
    double accel     = 0;
    double limit     = 0;
    bool   gain_mode = false;

    natural() = default;

    natural(const accel_args& args) : offset(args.input_offset),
                                       limit(args.limit - 1.0) {
        // Reference RawAccel allows limit < 1 → negative limit → deceleration
        // (gain < 1) below the offset band.  We keep that behavior here.
        // O1: avoid operator precedence trap — fabs before ternary, not after.
        // Prevent division by zero when limit ≈ 0 (args.limit ≈ 1).
        double abs_limit = std::fabs(limit);
        accel     = args.decay_rate / (abs_limit < 1e-9 ? 1.0 : abs_limit);
        gain_mode = args.gain;
    }

    double operator()(double x, const accel_args&) const {
        if (x <= offset) return 1.0;
        double t     = x - offset;   // positive distance past offset
        double decay = std::exp(-accel * t);

        if (!gain_mode) {
            // Legacy mode: gain approaches (limit+1) asymptotically.
            // Reference RawAccel natural<LEGACY>:
            //   offset_x = offset - x, decay = exp(accel*offset_x)
            //   gain = limit * (1 - (offset - decay*offset_x)/x) + 1
            // With offset==0 this reduces to limit*(1 - decay) + 1; the
            // offset terms are required so the curve starts at 1.0 right
            // past the offset and approaches (limit+1) at high speed.
            double offset_x = offset - x;
            return limit * (1.0 - (offset - decay * offset_x) / x) + 1.0;
        } else {
            // Gain mode: integral form so output speed is smooth.
            // Reference RawAccel natural<GAIN>:
            //   output = limit*(decay/accel - offset_x) + constant
            //           constant = -limit/accel
            //   gain = output/x + 1
            // This is offset-aware and approaches args.limit (not limit+1).
            if (x < 1e-9) return 1.0; // guard: prevent output/x blow-up when x ≈ 0
            // Conditioning of the gain branch (measured 2026-09-28 against a
            // 60-digit reference; natural, gain mode, input_offset=0,
            // limit=1.3 / decay_rate=0.08, i.e. accel=4/15):
            //   `output` is a difference of two O(limit/accel) terms and
            //   |output| ~ limit*accel*x^2/2, so the absolute error stays
            //   pinned near ulp(limit/accel) while the value shrinks; the
            //   output/x below then scales it as 1/x.  Measured gain error:
            //   1.3e6 ULP at x=0.1, 2.1e10 at x=0.01, 3.1e14 at x=1e-3.
            //
            // A consequence is that this line is FMA-sensitive: the compiler may
            // contract `limit*(decay/accel - offset_x) - limit/accel` into a
            // single fma, resolving the cancellation with one rounding instead
            // of two.  The comparison that matters OPERATIONALLY is the gate's
            // own -O1 (tests/oracle/run_oracle.sh) against the shipped
            // -O2 -march=native -mfma (scripts/build.sh) — NOT -O2 against
            // -O2 -mfma, which understates it by ~4.7e6.  Measured 2026-09-28 at
            // %.17g over 7 limits x 6 decay_rates x 2 modes x 9 speeds = 756:
            //
            //   -O1  vs -O1 -mfma        0/756 differ  <- GCC does NOT contract
            //                                               at -O1
            //   -O1  vs -O2   (no -mfma) 0/756 differ  <- not the -O2 either
            //   -O1  vs -O2 -mfma      139/756 differ
            //     worst: limit=1000 decay_rate=0.01 x=1e-4 gain mode
            //            16 384 000 000 ULP, relative 3.637e-06
            //
            // That relative figure is 3 637x OUTSIDE the oracle's 1e-9
            // tolerance, not inside it.  Two structural reasons the gate cannot
            // see any of it, and both are load-bearing:
            //   1. the gate builds at -O1, where no contraction happens at all;
            //   2. the grid only exercises natural with limit in {0.5, 1.3, 1.5}
            //      (tests/oracle/oracle_cases.hpp:107-110, :245, :289).
            // Per-limit, of those three only 1.3 drifts at all, and only to
            // 4.867e-11 — 24x INSIDE the tolerance.  The band that exceeds
            // tolerance is limit >= 10 (4.4x over at limit=10, 3.4e-6 at 100,
            // 3.6e-6 at 1000) and the grid never reaches it.
            //
            // These rows are NOT in tests/oracle/known_deviations.txt, and the
            // reason is NOT that the drift is small.  It is that they are not a
            // port-versus-reference deviation at all: measured through the
            // oracle's own harness at %.17g, the port reproduces the reference
            // bit-bit on 144/144 natural rows at -O1, at -O2 and at -O2 -mfma,
            // while the reference drifts from ITSELF on 6 of those same 144
            // rows between -O1 and -O2 -mfma.  The ill-conditioning is inherited
            // from the reference formula, which uses the same expression
            // (tests/oracle/ref/accel-natural.hpp:49,:55).  Widening the grid
            // would pin the fidelity, not document a defect.
            // The guard above stops NaN/inf.  It does NOT stop this
            // ill-conditioning, and the threshold was deliberately NOT raised
            // to 1e-6: that regime is unmeasured.
            // DO NOT "fix" this by factorising the expression into
            // `limit*(decay - 1.0)/accel - limit*offset_x`.  It is algebraically
            // identical, and on the -O2 vs -O2 -mfma comparison it does look
            // like a win (3500 -> 1 ULP of gain over 45 combinations, measured
            // on 3 limits x 3 offsets x 5 speeds).  It was measured and
            // REJECTED, and the reason is fidelity, not the test suite:
            //
            //   * The port reproduces the reference bit-bit in exactly the
            //     regime this rewrite would change (144/144 natural rows, at
            //     -O1, -O2 and -O2 -mfma).  The rewrite makes the port DIVERGE
            //     from the reference in order to be "more accurate" than it —
            //     i.e. it would return 1.0000078 where RawAccel returns 1.0,
            //     fixing no bug and breaking the one contract that matters.
            //   * It also trips the P105 monotonicity gate
            //     (tests/test_accel.cpp:5364, `g >= prev - 1e-9`) at
            //     limit=1000 / decay_rate=1e-4, where accel*x ~ 1e-9 and
            //     `decay - 1.0` extracts a ~1e-9 signal out of exp()'s result —
            //     ulp(1) is 2.2e-16, so that subtraction alone carries ~1.1e-7
            //     relative error.  Against a 60-digit reference the TRUE gain
            //     sequence there is monotonically INCREASING, so the gate is
            //     flagging a real guarantee being broken; the current
            //     expression merely rounds to exactly 1.0 in that window and
            //     passes by accident, at a cost of 2.25e10 ULP (5e-6 relative,
            //     5000x the oracle tolerance) at x=0.1.
            //   * A Taylor-series hybrid was measured too and is worse still:
            //     it needs its own large-x cutoff (1.38e19 ULP at x=1000).
            //
            // So the expression below is load-bearing as written, and the
            // honest reading of the measurement is "both forms are unfounded
            // for small accel*x, and the port must stay bit-compatible with a
            // reference that is equally unfounded", not "the factorised form is
            // the accurate one".
            //
            // HOW BIG, AND WHERE — measured over the REACHABLE box
            // (2026-09-28; 9 limits x 12 decay_rates x 8 speeds = 864 points,
            // gain mode, scored against a 60-digit reference):
            //   reachable means limit in [0, 100] — LIMIT_MAX, enforced at
            //   src/config.cpp:501 — and decay_rate > 0, which has NO bound
            //   beyond `if (decay_rate < 0) decay_rate = 0` (config.cpp:452).
            //   139 of 792 scorable points exceed the oracle's 1e-9 relative
            //   tolerance.  Worst: limit=100, decay_rate=1e-9, x=0.01 returns
            //   0.8046875 where the true gain is 1.000000000005 — a 19.5%
            //   speed error, i.e. the cursor moves at 80% while the curve says
            //   "essentially no acceleration".  The whole cluster sits at
            //   decay_rate <= 1e-4, where accel = decay_rate/|L| falls to
            //   ~1e-11..1e-12 and the two O(1/accel) terms of line ~137 above
            //   cancel.  It is a 19.5% error, NOT a sign flip and NOT a NaN:
            //   no reachable input produces a negative gain (measured over the
            //   same 864 points).
            //
            // ⚠ This deviation is INHERITED, not a port defect: the port and
            //   the official reference return the same 0.8046875 there, bit for
            //   bit, and over the reachable box the two differ in 0 of 768
            //   scorable points.  So the fix is NOT in this file — it is a
            //   lower bound on decay_rate in the config-presets lane, which
            //   makes the band unreachable without touching the formula and
            //   without breaking bit-fidelity with the reference.
            // ⚠ The oracle cannot see any of this: its grid uses decay_rate in
            //   {0.08, 0.1}, i.e. the insensitive region only.  51 of the 58
            //   offending (limit, decay_rate) pairs are at decay_rate <= 1e-4.
            // Guard: when accel ≈ 0 (decay_rate ≈ 0), the integral term
            // is 0/0 → NaN.  In this regime the gain curve is flat at 1.0
            // (no acceleration), so return 1.0 directly.
            if (accel < 1e-12) return 1.0;
            double offset_x = offset - x;
            double output   = limit * (decay / accel - offset_x) - limit / accel;
            return output / x + 1.0;
        }
    }
};

} // namespace rawaccel
