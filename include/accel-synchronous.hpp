#pragma once
#include "accel-lookup.hpp"
#include <cmath>
#include <algorithm>

namespace rawaccel {

/// Synchronous / activation-framework acceleration (reference RawAccel
/// `activation_framework`).
///   LEGACY (args.gain == false): multi-scale tanh activation in log space.
///   GAIN   (args.gain == true):  a precomputed LUT of the integrated LEGACY
///                                sensitivity (across [2^-3, 2^9]) held in this
///                                object's own `lut[]` member and returned as
///                                output-speed / x.
/// Guards (port convention): x <= 0 → 1.0; degenerate motivity/sync_speed
/// degenerate to identity so no NaN can escape.
struct synchronous {
    bool gain_mode = false;

    // A1 (state bleed): the GAIN integral table lives HERE, not in args.data.
    // The reference generator wrote through `const accel_args&` (mutable
    // args.data), which meant constructing the accelerator MUTATED the user's
    // config object — the GUI's compute_curve(args) passed the profile's own
    // accel_args and a mere graph repaint erased the stored lookup LUT, which
    // save_config then persisted.  args.data is now non-mutable and belongs
    // exclusively to the user (lookup curve); this array is private state
    // rebuilt by the constructor.
    float lut[LUT_RAW_DATA_CAPACITY] = {};

    // LEGACY variant members
    double log_motivity      = 0;
    double gamma_const       = 0;
    double log_syncspeed     = 0;
    double syncspeed         = 1;
    double sharpness         = 16;
    double sharpness_recip   = 1.0 / 16;
    bool   use_linear_clamp  = true;
    double minimum_sens      = 1;
    double maximum_sens      = 1;

    // GAIN variant members
    bool         velocity = false;
    fp_rep_range range    = { -3, 9, 8 };
    double       x_start  = 0;

    synchronous() = default;

    synchronous(const accel_args& args) : gain_mode(args.gain) {
        init_legacy(args);
        if (gain_mode) fill_lut(args);
    }

    double operator()(double x, const accel_args& args) const {
        if (x <= 0) return 1.0;
        // DÜŞÜK-BUG-ALG-04: guard NaN/Inf too — legacy_apply calls log(x),
        // and log(NaN)=NaN previously propagated downstream.  The gain path
        // already guards via gain_apply; make the legacy path consistent.
        if (!std::isfinite(x)) return 1.0;
        if (gain_mode) return gain_apply(x, args);
        return legacy_apply(x);
    }

private:
    static constexpr int capacity = LUT_RAW_DATA_CAPACITY;

    void init_legacy(const accel_args& args) {
        double m = args.motivity;
        log_motivity    = (m > 0) ? std::log(m) : 0.0;
        gamma_const     = (log_motivity == 0) ? 0.0 : args.gamma / log_motivity;
        double s        = args.sync_speed;
        syncspeed       = (s > 0) ? s : 1.0;
        log_syncspeed   = (s > 0) ? std::log(s) : 0.0;
        sharpness       = (args.smooth == 0) ? 16.0 : 0.5 / args.smooth;
        sharpness_recip = 1.0 / sharpness;
        use_linear_clamp = sharpness >= 16;
        if (m > 0) {
            minimum_sens = 1.0 / m;
            maximum_sens = m;
        } else {
            minimum_sens = 1;
            maximum_sens = 1;
        }
    }

    double legacy_apply(double x) const {
        // sharpness >= 16 → linear clamp: clamp(gamma_const*(log x - log sync_speed), -1, 1)
        if (use_linear_clamp) {
            double log_space = gamma_const * (std::log(x) - log_syncspeed);
            if (log_space < -1) return minimum_sens;
            if (log_space > 1)  return maximum_sens;
            return std::exp(log_space * log_motivity);
        }

        if (x == syncspeed) return 1.0;

        double log_diff = std::log(x) - log_syncspeed;
        // BUG-01: odd-symmetric tanh activation in log space z = gamma_const·(log x − log sync_speed):
        //   exponent = sign(z) · pow(tanh(|z|^sharpness), 1/sharpness)
        // For motivity<1, gamma_const is negative, so z — not log_diff — carries the
        // curve's sign (negative on the high-speed side). Branching on log_diff with a
        // |log_space| repair dropped the tanh's odd sign, inverting the curve relative
        // to the smooth=0 linear-clamp path (m=0.3/smooth=0.5: local m vs ref 1/m).
        // |z| keeps pow(·, fractional sharpness) finite while sign(z) restores the odd
        // symmetry that matches the clamp path. motivity>1 is bit-identical.
        double z        = gamma_const * log_diff;
        double exponent = std::pow(std::tanh(std::pow(std::abs(z), sharpness)),
                                   sharpness_recip);
        if (z < 0) exponent = -exponent;
        return std::exp(exponent * log_motivity);
    }

    void init(const fp_rep_range& r, bool vel) {
        velocity = vel;
        range    = r;
        x_start  = std::scalbn(1, range.start);
    }

    void fill_lut(const accel_args& args) {
        init({ -3, 9, 8 }, true);

        // Temporary LEGACY sigmoid with the same parameters (no recursion:
        // gain_mode defaults false and init_legacy fills the sigmoid fields).
        synchronous sig;
        sig.gain_mode = false;
        sig.init_legacy(args);

        double sum = 0;
        double a   = 0;
        auto sigmoid_sum = [&](double b) {
            int partitions = 2;
            double interval = (b - a) / partitions;
            for (int i = 1; i <= partitions; i++) {
                sum += sig.legacy_apply(a + i * interval) * interval;
            }
            a = b;
            return sum;
        };

        // A1: fill the object's OWN lut[] (not args.data).  fn receives only
        // the grid point x — the destination is the member array below.
        fill([&](double x) {
            double y = sigmoid_sum(x);
            if (!velocity) y /= x;
            return y;
        }, range);
    }

    template <typename Func>
    void fill(Func fn, const fp_rep_range& range) {
        int i = 0;
        range.for_each([&](double x) {
            if (i < capacity) {
                lut[i] = static_cast<float>(fn(x));
                ++i;
            }
        });
    }

    double gain_apply(double x, const accel_args& args) const {
        const float* data = lut; // A1: table is per-instance state

        if (!std::isfinite(x) || x <= 0) return 1.0;

        int e = std::min(std::ilogb(x), range.stop - 1);

        if (e >= range.start) {
            int    idx_int_log_part  = e - range.start;
            double idx_frac_lin_part = std::scalbn(x, -e) - 1;
            double idx_f = range.num * (idx_int_log_part + idx_frac_lin_part);

            double idx_safe = std::clamp(idx_f, 0.0, static_cast<double>(range.size() - 2));
            unsigned idx = static_cast<unsigned>(idx_safe);

            if (idx < static_cast<unsigned>(capacity - 1)) {
                // L02-04: idx_safe clamps the INDEX, but idx_f — hence
                // t = idx_f - idx — was left raw.  At x = DBL_MAX the huge t
                // overflows lerp's t*(b-a) to ±Inf and the old code returned
                // that Inf.  NON-FINITE t is repaired to the left endpoint.
                // A FINITE t > 1 is deliberately kept: past the 2^9 grid the
                // reference extrapolates the last segment, and clamping t to
                // [0,1] (the literal L02-04 instruction) was MEASURED to
                // change every sync GAIN row at speed >= 1e3 by 49..99.5%
                // relative — a new oracle deviation class.  Reference parity
                // wins over the over-broad clamp.
                double t = idx_f - idx;
                if (!std::isfinite(t)) t = 0.0;
                double y = lerp(static_cast<double>(data[idx]),
                                static_cast<double>(data[idx + 1]),
                                t);
                if (velocity) y /= x;
                if (!std::isfinite(y)) {
                    // The finite t itself overflowed t*(b-a).  Fall back to
                    // the curve's own saturated high-speed multiplier: finite,
                    // monotone, and continuous with the true tail (the
                    // extrapolated gain converges to the same value as
                    // x → Inf, so no step is introduced at the overflow edge).
                    y = (gamma_const > 0) ? maximum_sens
                        : (gamma_const < 0 ? minimum_sens : 1.0);
                }
                return y;
            }
        }

        double y = data[0];
        if (velocity) y /= x_start;
        return y;
    }
};

} // namespace rawaccel