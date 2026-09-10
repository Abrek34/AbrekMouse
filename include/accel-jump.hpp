#pragma once
#include "rawaccel-base.hpp"
#include <cmath>

namespace rawaccel {

/// Jump acceleration — a hard "step" in gain that can be smoothed by an
/// increasing sigmoid (reference RawAccel jump).
/// Parameters (reference):
///   cap.x  → step position (input speed where the step occurs)
///   cap.y  → step amount: gain jumps from 1.0 to cap.y
///   smooth → smoothing width in [0, 1]; 0 forces a hard step.
/// args.gain selects LEGACY (direct multiplier / sigmoid) or GAIN (output-speed
/// integral form that keeps output smooth).
struct jump {
    bool   gain_mode   = false;
    vec2d  step        = {};
    double smooth_rate = 0;
    double smooth_log0 = 0; // GAIN: log1p(exp(-smooth_rate*step.x)), the
                            // saturation term of the antiderivative at 0

    jump() = default;

    jump(const accel_args& args) : gain_mode(args.gain) {
        step = { args.cap.x, args.cap.y - 1 };
        double rate_inverse = args.smooth * step.x;
        smooth_rate = (rate_inverse < 1) ? 0.0 : (2 * M_PI) / rate_inverse;
        if (gain_mode && smooth_rate != 0)
            smooth_log0 = std::log1p(std::exp(-smooth_rate * step.x));
    }

    bool is_smooth() const { return smooth_rate != 0; }

    double decay(double x) const {
        return std::exp(smooth_rate * (step.x - x));
    }

    double smooth(double x) const {
        return step.y / (1 + decay(x));
    }

    double operator()(double x, const accel_args&) const {
        if (!gain_mode) {
            // LEGACY: direct multiplier — sigmoid blend of the step.
            if (is_smooth()) return smooth(x) + 1.0;
            if (x < step.x)  return 1.0;
            return 1.0 + step.y;
        }

        // GAIN: integrate the multiplier so output speed is smooth, passing
        // through 1.0 at the origin.
        if (x <= 0) return 1.0;

        if (!is_smooth()) {
            if (x < step.x) return 1.0;
            return 1.0 + step.y * (x - step.x) / x;
        }

        // GAIN smooth: gain = (A(x) - A(0)) / x + 1 with
        //   A(x) = step.y * (x + log(1 + exp(y)) / smooth_rate),
        //   y    = smooth_rate * (step.x - x).
        // Computing A(x) and A(0) separately and subtracting cancels when both
        // are ~step.y*step.x while the meaningful difference is tiny, and
        // naive log(1 + exp(y)) overflows for extreme caps.  Instead evaluate
        // A(x) - A(0) directly from the stable saturation terms
        //   S(z) = log1p(exp(-|z|)),  Z(z) = max(z, 0) + S(z),
        // using Z(y) - Z(y0) = (y - y0) + S(y) - S(y0) and y - y0 = -rate*x.
        double y   = smooth_rate * (step.x - x);
        double S   = std::log1p(std::exp(-std::fabs(y)));
        double dA;
        if (y > 0) {
            dA = step.y * (S - smooth_log0) / smooth_rate;
        } else {
            dA = step.y * ((x - step.x) + (S - smooth_log0) / smooth_rate);
        }
        return 1.0 + dA / x;
    }
};

} // namespace rawaccel