#pragma once
#include "rawaccel-base.hpp"
#include <cmath>
#include <cfloat>

namespace rawaccel {

/// Power acceleration: output = (scale * x)^n + C/x
/// Smooth curve that naturally approaches a cap.
/// Two forms (reference RawAccel power<GAIN>/<LEGACY>):
///   gain:   continuous gain curve, cap handled via constant_b/x tail
///   legacy: raw base curve hard-capped at cap.y (blocky tail)
/// For legacy + io cap mode the reference uses a special scale derivation
/// (scale_from_output_point) that ignores the output offset entirely.
struct power {
    bool   gain_mode    = false;
    vec2d  offset       = {};
    double scale        = 1;
    // BUG-02: the single shared exponent floor — scale derivation (io gain
    // point) and curve evaluation must use the SAME n = max(ep, 1e-3).
    double exponent     = 1;
    double constant     = 0;
    double cap_x        = DBL_MAX;
    double cap_y        = DBL_MAX;
    double constant_b   = 0;
    double legacy_cap   = DBL_MAX; // LEGACY hard cap

    power() = default;

    power(const accel_args& args) : gain_mode(args.gain) {
        // BUG-02: previously scale_from_gain_point floored the exponent at
        // 1e-3 while base_fn_impl evaluated the raw exponent_power, so for
        // ep ∈ [1e-4, 1e-3) gain(cap_x) < cap_y. Floor once here and share the
        // same n everywhere (offset, constant, cap, evaluation).
        double in_n = args.exponent_power > 0 ? args.exponent_power : 1e-4;
        exponent = in_n < 1e-3 ? 1e-3 : in_n;
        auto n = exponent;

        if (args.cap_mode_val != cap_mode::io) {
            scale = args.scale;
        } else if (args.cap.y <= 0 || (gain_mode && args.cap.x <= 0)) {
            // P155: io cap with cap.y <= 0 or (gain_mode && cap.x <= 0) is degenerate —
            // ref derives scale=0 from the "gain 0" output point, or produces a flat
            // zero-threshold clamp that freezes the cursor gain. Treat as "no cap
            // requested": identity scale, no legacy clamp, base curve active.
            scale = 1.0;
            legacy_cap = DBL_MAX;
            constant = 0;
            offset = {};
            return;
        } else if (gain_mode) {
            scale = scale_from_gain_point(args.cap.x, args.cap.y, n);
        } else {
            // reference special case: legacy + io — offset is ignored because of
            // the circular dependency (scale -> constant -> offset).
            offset = {};
            constant = 0;
            scale = scale_from_output_point(args.cap.x, args.cap.y, n, constant);
            legacy_cap = args.cap.y; // P81: cap the legacy curve at cap.y (io mode)
            return;
        }

        offset.x = gain_inverse(args.output_offset, n, scale);
        offset.y = args.output_offset;
        constant = offset.x * offset.y * n / (n + 1);

        if (!gain_mode) {
            switch (args.cap_mode_val) {
            case cap_mode::io:
                legacy_cap = args.cap.y;
                break;
            case cap_mode::in:
                if (args.cap.x > 0) legacy_cap = base_fn_impl(args.cap.x);
                break;
            case cap_mode::out:
            default:
                if (args.cap.y > 0) legacy_cap = args.cap.y;
                break;
            }
            return;
        }

        switch (args.cap_mode_val) {
        case cap_mode::io:
            if (args.cap.x <= 0) {
                cap_x = DBL_MAX;
                cap_y = DBL_MAX;
            } else {
                cap_x = args.cap.x;
                cap_y = args.cap.y;
            }
            break;
        case cap_mode::in:
            if (args.cap.x > 0) {
                if (args.cap.x <= offset.x) { cap_x = 0; cap_y = offset.y; return; }
                cap_x = args.cap.x;
                cap_y = gain_fn(args.cap.x, n, scale);
            }
            break;
        case cap_mode::out:
        default:
            if (args.cap.y > 0) {
                cap_x = gain_inverse(args.cap.y, n, scale);
                cap_y = args.cap.y;
            }
            break;
        }

        constant_b = integration_constant(cap_x, cap_y, base_fn_impl(cap_x));
    }

    double operator()(double speed, const accel_args&) const {
        if (speed <= 0) return 1.0;
        if (!gain_mode) {
            double out = base_fn_impl(speed);
            return minsd(out, legacy_cap);
        }
        // P155: match the reference GAIN order — the cap branch is checked
        // FIRST, offset handling lives inside base_fn_impl (returns offset.y
        // at/under offset.x).  Previously the offset check ran before the cap
        // check, so when output_offset > cap_y (offset.x > cap.x) the cap tail
        // was unreachable and the requested cap was silently ignored.
        if (speed < cap_x) {
            return base_fn_impl(speed);
        } else {
            return cap_y + constant_b / speed;
        }
    }

private:
    double base_fn_impl(double x) const {
        if (x <= offset.x) return offset.y;
        return std::pow(scale * x, exponent) + constant / x;
    }

    static double gain_fn(double input, double power, double sc) {
        return (power + 1) * std::pow(input * sc, power);
    }

    static double gain_inverse(double g, double power, double sc) {
        if (sc <= 0) return 0;
        // BUG-NEW-11/16 (aj4): with a tiny floored exponent (n=1e-3) and a
        // large requested gain the true inverse ~ g^(1/n) far exceeds DBL_MAX,
        // so pow() returns Inf and offset.x/constant become Inf.  Clamp to a
        // finite DBL_MAX: it is still larger than any reachable speed, so the
        // plateau (x <= offset.x → offset.y) is numerically identical, while
        // the struct stays free of Inf (scale_from_* keep their own guard).
        double r = std::pow(g / (power + 1), 1.0 / power) / sc;
        return std::isfinite(r) ? r : DBL_MAX;
    }

    static double scale_from_gain_point(double input, double gain, double power) {
        if (input <= 0) return 1.0; // guard: degenerate io cap → identity scale (no dead curve)
        // The caller already passes the shared floored exponent n = max(ep,1e-3)
        // (BUG-02), so 1.0/power ≤ 1000 cannot overflow to Inf; the isfinite
        // fallback below still protects a pathologically large requested gain.
        double sc = std::pow(gain / (power + 1), 1.0 / power) / input;
        return std::isfinite(sc) ? sc : 1.0;
    }

    static double scale_from_output_point(double input, double output, double power, double C) {
        if (input <= 0) return 1.0; // guard: degenerate io cap → identity scale (no dead curve)
        double diff = output - C / input;
        if (diff < 0) return 1.0;
        double sc = std::pow(diff, 1.0 / power) / input;
        return std::isfinite(sc) ? sc : 1.0;
    }

    static double integration_constant(double input, double gain, double output) {
        return (output - gain) * input;
    }
};

} // namespace rawaccel