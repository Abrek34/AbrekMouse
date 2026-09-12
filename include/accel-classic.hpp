#pragma once
#include "rawaccel-base.hpp"
#include <cmath>
#include <cfloat>

namespace rawaccel {

/// Classic (linear raised to power) acceleration.
/// Two variants, selected by args.gain (reference classic<LEGACY> / classic<GAIN>):
///   LEGACY: multiplier cap applied directly (gain = cap-factor clamped).
///   GAIN:   output-speed integral form that asymptotically approaches the cap
///           instead of clamping — the default reference mode.
/// exponent_classic <= 1 keeps the port's documented "linear" constant-gain path.
struct classic {
    double accel_raised = 0;
    bool   gain_mode    = false;
    double cap          = DBL_MAX;    // LEGACY multiplier cap
    double cap_x        = DBL_MAX;    // GAIN input where the cap kicks in
    double cap_y        = DBL_MAX;    // GAIN output at/above cap
    double constant     = 0;          // GAIN integration constant
    double sign         = 1;

    classic() = default;

    classic(const accel_args& args) : gain_mode(args.gain) {
        // exponent == 1 → linear (acceleration * x / x == acceleration, constant gain)
        // Skip the normal setup; operator() will handle it via the exp==1 path.
        if (args.exponent_classic <= 1.0) {
            // Store acceleration directly; operator() returns args.acceleration + 1
            accel_raised = args.acceleration;
            // cap.y == 0 → no cap (DBL_MAX); cap.y > 0 → cap = cap.y - 1 (in gain units)
            // cap.y < 1 is degenerate, but guard: don't go negative → clamp to 0
            cap = args.cap.y > 0 ? std::max(0.0, args.cap.y - 1.0) : DBL_MAX;
            return;
        }

        if (args.gain) {
            init_gain(args);
        } else {
            init_legacy(args);
        }
    }

    double operator()(double x, const accel_args& args) const {
        if (x <= args.input_offset) return 1.0;
        // Linear path (exponent <= 1): constant gain = acceleration, capped at cap_y
        if (args.exponent_classic <= 1.0)
            return 1.0 + minsd(accel_raised, cap);

        // CUR-2 (deliberately NOT changed — reference behavior): cap.y < 1 in
        // io mode sets sign = -1, which yields gain < 1 (deceleration).  This
        // mirrors the official RawAccel classic curves and is locked in by the
        // P105/P106 cap-limit tests and the differential oracle — "fixing" it
        // would diverge from the reference.  Documented in Bug Hata Raporları
        // (TUR 18 CUR-2) as a deliberate non-change.
        if (!gain_mode)
            return sign * minsd(base_fn(x, accel_raised, args), cap) + 1.0;

        // GAIN mode (reference classic<GAIN>): output is the integral of the
        // gain curve; for x >= cap.x the curve continues as constant/x + cap.y
        // so gain asymptotically approaches the cap instead of clamping to it.
        double output;
        if (x < cap_x) {
            output = base_fn(x, accel_raised, args);
        } else {
            output = constant / x + cap_y;
        }
        return sign * output + 1.0;
    }

private:
    void init_legacy(const accel_args& args) {
        switch (args.cap_mode_val) {
        case cap_mode::io:
            cap = args.cap.y - 1;
            if (cap < 0) { cap = -cap; sign = -sign; }
            {
                // O5: if cap.x <= input_offset, base_accel returns 0 → pow(0, exp-1).
                // When exp < 1 this yields Inf. Degenerate config — guard with accel_raised = 0.
                double a = base_accel(args.cap.x, cap, args);
                accel_raised = (std::isfinite(a) && a > 0)
                    ? std::pow(a, args.exponent_classic - 1)
                    : 0.0;
            }
            break;
        case cap_mode::in:
            {
                double ar = std::pow(args.acceleration, args.exponent_classic - 1);
                accel_raised = std::isfinite(ar) ? ar : 0.0; // NaN guard: neg accel + non-int exp
            }
            // BUG-9: when cap.x <= input_offset, base_fn computes
            // pow(negative_base, exp) which yields NaN for non-integer
            // exponents.  That NaN then poisons every operator() call
            // (`min(finite, NaN)` returns NaN per IEEE 754 ordering).
            // Treat the degenerate config as "no input cap" so output
            // remains finite.
            if (args.cap.x > 0 && args.cap.x > args.input_offset) {
                cap = base_fn(args.cap.x, accel_raised, args);
                if (!std::isfinite(cap)) cap = DBL_MAX;
            }
            break;
        case cap_mode::out:
        default:
            {
                double ar = std::pow(args.acceleration, args.exponent_classic - 1);
                accel_raised = std::isfinite(ar) ? ar : 0.0; // NaN guard: neg accel + non-int exp
            }
            if (args.cap.y > 0) {
                cap = args.cap.y - 1;
                if (cap < 0) { cap = -cap; sign = -sign; }
            }
            break;
        }
    }

    void init_gain(const accel_args& args) {
        switch (args.cap_mode_val) {
        case cap_mode::io:
            cap_x = args.cap.x;
            // BUG-7 fix: if cap.x < input_offset, base_fn(cap_x) receives a
            // non-positive base → pow(neg, frac) = NaN → base_fn = 0, and the
            // entire accelerated tail collapses to the degenerate constant
            // cap_y·(1 − cap_x/x).  Clamp the breakpoint up to input_offset
            // so the curve keeps its intended shape (no silent gain loss).
            if (cap_x < args.input_offset) cap_x = args.input_offset;
            cap_y = args.cap.y - 1;
            if (cap_y < 0) { cap_y = -cap_y; sign = -sign; }
            {
                double a = gain_accel(cap_x, cap_y, args.exponent_classic, args.input_offset);
                accel_raised = (std::isfinite(a) && a > 0)
                    ? std::pow(a, args.exponent_classic - 1)
                    : 0.0;
            }
            // ORTA-BUG-ALG-03/CUR-1: base_fn(cap_x) can trip its own overflow guard
            // (pow(·) → Inf → returns 0.0) even when accel_raised > 0, so the
            // analytic C1 break is unrepresentable at this point.  The OLD
            // guard then degraded cap_y=constant=0 — output snapped from the
            // real rising body to identity at cap_x (a C0 hard edge).  Instead
            // keep the analytic cap_y and anchor the tail to base_fn's guarded
            // value 0:  tail = cap_y·(1 − cap_x/x), which starts exactly at the
            // (also guarded) body end, is continuous, stays monotone non-
            // decreasing, never exceeds cap.y (P105: g <= cap.y + 1e-9), and
            // never collapses to 1:1.
            {
                const double bf = base_fn(cap_x, accel_raised, args);
                if (bf == 0.0 && accel_raised > 0.0) {
                    constant = (0.0 - cap_y) * cap_x;
                } else {
                    constant = (bf - cap_y) * cap_x;
                }
            }
            break;
        case cap_mode::in:
            {
                double ar = std::pow(args.acceleration, args.exponent_classic - 1);
                accel_raised = std::isfinite(ar) ? ar : 0.0;
            }
            if (args.cap.x > 0) {
                // BUG-7 symmetry (O31-C1): the io branch clamps the breakpoint
                // up to input_offset; without the same guard here, cap_x inside
                // the offset band gives base_fn a non-positive base → a finite
                // NEGATIVE cap_y (gain(cap_x) for exp integer) and a flipped
                // (reverse-direction) accelerated tail, while non-integer
                // exponents degrade to the NaN guard (cap_y=DBL_MAX, absürd
                // curve).  Config load is sanitized already, but the GUI live
                // preview runs init_gain() on unsanitized widget values.
                cap_x = args.cap.x;
                if (cap_x < args.input_offset) cap_x = args.input_offset;
                cap_y = gain(cap_x, args.acceleration, args.exponent_classic, args.input_offset);
                constant = (base_fn(cap_x, accel_raised, args) - cap_y) * cap_x;
            }
            break;
        case cap_mode::out:
        default:
            {
                double ar = std::pow(args.acceleration, args.exponent_classic - 1);
                accel_raised = std::isfinite(ar) ? ar : 0.0;
            }
            if (args.cap.y > 0) {
                cap_y = args.cap.y - 1;
                if (cap_y == 0) {
                    cap_x = 0;
                } else {
                    if (cap_y < 0) { cap_y = -cap_y; sign = -sign; }
                    cap_x = gain_inverse(cap_y, args.acceleration,
                                         args.exponent_classic, args.input_offset);
                    // ORTA-BUG-ALG-03/CUR-1: same guard as the io branch — the
                    // C0 identity collapse is replaced by a continuous tail
                    // anchored to base_fn's guarded 0 (cap_y·(1 − cap_x/x)),
                    // which is monotone, <= cap.y, and never 1:1.
                    const double bf2 = base_fn(cap_x, accel_raised, args);
                    if (bf2 == 0.0 && accel_raised > 0.0) {
                        constant = (0.0 - cap_y) * cap_x;
                    } else {
                        constant = (bf2 - cap_y) * cap_x;
                    }
                }
            }
            break;
        }
        // Defense: guard the GAIN constants (NaN from degenerate caps poisons
        // every sample at x >= cap_x).
        if (!std::isfinite(cap_x)) cap_x = DBL_MAX;
        if (!std::isfinite(cap_y)) cap_y = DBL_MAX;
        if (!std::isfinite(constant)) constant = 0;
    }

    double base_fn(double x, double ar, const accel_args& args) const {
        // Guard: extreme exponents overflow pow() to Inf (or degenerate 0·Inf).
        // Falling back to 0 keeps the identity output (gain 1.0) instead of NaN.
        double p = std::pow(x - args.input_offset, args.exponent_classic);
        if (!std::isfinite(p)) return 0.0;
        double r = ar * p / x;
        return std::isfinite(r) ? r : 0.0;
    }

    static double base_accel(double x, double y, const accel_args& args) {
        double power = args.exponent_classic;
        if (power <= 1.0 || x <= args.input_offset) return 0; // degenerate
        return std::pow(x * y * std::pow(x - args.input_offset, -power), 1.0 / (power - 1));
    }

    // Reference classic<GAIN> helpers
    static double gain(double x, double accel, double power, double offset) {
        return power * std::pow(accel * (x - offset), power - 1);
    }

    static double gain_inverse(double y, double accel, double power, double offset) {
        // R9-CAP0: accel == 0 makes the accelerated body identically 0
        // (pow(0, power-1) = 0 for power > 1), so the reference's inverse —
        // (accel·offset + pow(y/power, 1/(power-1))) / accel — divides by 0 and
        // yields cap.x = +Inf, i.e. the cap is reached only at infinite speed
        // and the whole curve collapses to identity.  The old guard returned
        // `offset` here, which made the GAIN out-branch (constant/x + cap_y)
        // activate at every speed x >= offset → a silent CONSTANT ×cap.y boost
        // (e.g. cap {15, 2} → gain 2.0 everywhere) where the reference is
        // inert.  Match the reference: unreachable cap.
        if (accel == 0)
            return DBL_MAX;
        if (power <= 1.0) return offset;
        return (accel * offset + std::pow(y / power, 1.0 / (power - 1))) / accel;
    }

    static double gain_accel(double x, double y, double power, double offset) {
        double denom = offset - x;
        if (denom == 0) return 0; // degenerate
        // BUG-NEW-71: for power ≈ 1 the exponent 1/(power-1) explodes
        // (power 1.001 → 1000) and pow(y/power, 1000) overflows to Inf
        // whenever cap_y-1 > power (y/power > 1).  The caller's isfinite()
        // guard then sets accel_raised = 0, silently collapsing the whole
        // IO+GAIN curve to identity.  Clamp the exponent so the result
        // stays finite and usable (64 keeps pow(y/power,·) finite for the
        // sanitized cap range y ≤ CAP_Y_MAX-1 = 99, power ≥ 1).
        double ex = 1.0 / (power - 1);
        if (!(ex > 0) || ex > 64.0) ex = 64.0; // !(ex>0) also catches NaN/Inf
        return -std::pow(std::fabs(y / power), ex) / denom;
    }
};

} // namespace rawaccel