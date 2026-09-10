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
