#pragma once
#include "accel-union.hpp"
#include <cmath>
#include <algorithm>

namespace rawaccel {

// ── Smoothers ────────────────────────────────────────────────────────────────

/// Simple exponential moving average (EMA) smoother.
struct simple_ema_smoother {
    double windowCoefficient = 0;
    double cutoffCoefficient = 0;
    double windowTotal       = 0;
    double cutoffTotal       = 0;

    void init(double halfLife) {
        windowTotal = cutoffTotal = 0;
        windowCoefficient = halfLife > 0 ? std::pow(0.5, 1.0 / halfLife) : 0;
        cutoffCoefficient = 1.0 - std::sqrt(1.0 - windowCoefficient);
    }

    double smooth(double speed, milliseconds time) {
        double twc = 1.0 - std::pow(windowCoefficient, time);
        double tcc = 1.0 - std::pow(cutoffCoefficient, time);
        windowTotal += twc * (speed - windowTotal);
        cutoffTotal += tcc * (speed - cutoffTotal);
        return std::min(windowTotal, cutoffTotal);
    }
};

/// Linear (trend-aware) EMA smoother.
struct linear_ema_smoother {
    static constexpr double trendDampening = 0.75;

    double windowCoefficient      = 0;
    double cutoffCoefficient      = 0;
    double windowTrendCoefficient = 0;
    double cutoffTrendCoefficient = 0;

    double windowTotal      = 0;
    double cutoffTotal      = 0;
    double windowTrendTotal = 0;
    double cutoffTrendTotal = 0;

    void init(double halfLife, double trendHalfLife) {
        windowTotal = cutoffTotal = windowTrendTotal = cutoffTrendTotal = 0;
        windowCoefficient      = halfLife > 0      ? std::pow(0.5, 1.0 / halfLife)      : 0;
        windowTrendCoefficient = trendHalfLife > 0 ? std::pow(0.5, 1.0 / trendHalfLife) : 0;
        cutoffCoefficient      = 1.0 - std::sqrt(1.0 - windowCoefficient);
        cutoffTrendCoefficient = 1.0 - std::sqrt(1.0 - windowTrendCoefficient);
    }

    double smooth(double speed, milliseconds time) {
        double twc  = 1.0 - std::pow(windowCoefficient,      time);
        double tcc  = 1.0 - std::pow(cutoffCoefficient,      time);
        double twtc = 1.0 - std::pow(windowTrendCoefficient, time);
        double tctc = 1.0 - std::pow(cutoffTrendCoefficient, time);

        double oldW = windowTotal, oldC = cutoffTotal;

        windowTrendTotal *= trendDampening;
        cutoffTrendTotal *= trendDampening;
        windowTotal += windowTrendTotal * time;
        cutoffTotal += cutoffTrendTotal * time;

        windowTotal += twc  * (speed - windowTotal);
        cutoffTotal += tcc  * (speed - cutoffTotal);

        // don't let trend carry us below 0 (clamp AFTER the EMA update, like the
        // reference — trend adjustment below then measures the clamped state)
        windowTotal  = std::max(windowTotal, 0.0);
        cutoffTotal  = std::max(cutoffTotal, 0.0);

        double nwt = time > 0 ? (windowTotal - oldW) / time : 0;
        double nct = time > 0 ? (cutoffTotal - oldC) / time : 0;
        windowTrendTotal += twtc * (nwt - windowTrendTotal);
        cutoffTrendTotal += tctc * (nct - cutoffTrendTotal);

        return std::min(windowTotal, cutoffTotal);
    }
};

// ── Speed processor ──────────────────────────────────────────────────────────

enum class distance_mode : unsigned char {
    euclidean = 0,
    separate  = 1,
    max       = 2,
    Lp        = 3,
};

struct speed_processor_flags {
    bool           should_smooth_input  = false;
    bool           should_smooth_scale  = false;
    bool           should_smooth_output = false;
    distance_mode  dist_mode            = {};
};

struct axis_smoother {
    linear_ema_smoother input_speed_smoother  = {};
    simple_ema_smoother scale_smoother        = {};
    linear_ema_smoother output_speed_smoother = {};
};

struct speed_processor {
    speed_args           args         = {};
    speed_processor_flags speed_flags = {};
    axis_smoother        smoother_x   = {};
    axis_smoother        smoother_y   = {};

    static constexpr double input_trend_halflife  = 1.25;
    static constexpr double output_trend_halflife = 0.70;

    speed_processor() = default;

    void init(const speed_args& in_args) {
        args = in_args;

        if (!in_args.whole) {
            speed_flags.dist_mode = distance_mode::separate;
        } else if (in_args.lp_norm >= MAX_NORM || in_args.lp_norm <= 0) {
            speed_flags.dist_mode = distance_mode::max;
        } else if (std::fabs(in_args.lp_norm - 2.0) > 1e-9) {
            // D1: use epsilon for float comparison — instead of "!= 2"
            speed_flags.dist_mode = distance_mode::Lp;
        } else {
            speed_flags.dist_mode = distance_mode::euclidean;
        }

        speed_flags.should_smooth_input  = in_args.input_speed_smooth_halflife  > 0;
        speed_flags.should_smooth_scale  = in_args.scale_smooth_halflife         > 0;
        speed_flags.should_smooth_output = in_args.output_speed_smooth_halflife  > 0;

        if (speed_flags.should_smooth_input) {
            smoother_x.input_speed_smoother.init(in_args.input_speed_smooth_halflife, input_trend_halflife);
            smoother_y.input_speed_smoother.init(in_args.input_speed_smooth_halflife, input_trend_halflife);
        }
        if (speed_flags.should_smooth_scale) {
            smoother_x.scale_smoother.init(in_args.scale_smooth_halflife);
            smoother_y.scale_smoother.init(in_args.scale_smooth_halflife);
        }
        if (speed_flags.should_smooth_output) {
            smoother_x.output_speed_smoother.init(in_args.output_speed_smooth_halflife, output_trend_halflife);
            smoother_y.output_speed_smoother.init(in_args.output_speed_smooth_halflife, output_trend_halflife);
        }
    }

    double calc_speed_whole(vec2d in, milliseconds time) {
        double speed;
        if (speed_flags.dist_mode == distance_mode::max) {
            speed = maxsd(std::fabs(in.x), std::fabs(in.y));
        } else if (speed_flags.dist_mode == distance_mode::Lp) {
            speed = lp_distance(in, args.lp_norm);
        } else {
            speed = magnitude(in);
        }
        if (speed_flags.should_smooth_input) {
            speed = smoother_x.input_speed_smoother.smooth(speed, time);
        }
        return speed;
    }

    void calc_speed_separate(vec2d& in, milliseconds time) {
        if (speed_flags.should_smooth_input) {
            in.x = smoother_x.input_speed_smoother.smooth(std::fabs(in.x), time);
            in.y = smoother_y.input_speed_smoother.smooth(std::fabs(in.y), time);
        } else {
            in.x = std::fabs(in.x);
            in.y = std::fabs(in.y);
        }
    }
};

// ── Modifier flags ────────────────────────────────────────────────────────────

struct modifier_flags {
    bool apply_rotate             = false;
    bool compute_ref_angle        = false;
    bool apply_snap               = false;
    bool clamp_speed              = false;
    bool apply_directional_weight = false;
    bool apply_dir_mul_x          = false;
    bool apply_dir_mul_y          = false;

    modifier_flags() = default;

    modifier_flags(const profile& args) {
        clamp_speed              = args.speed_max > 0 && args.speed_min <= args.speed_max;
        // N13/N14: use epsilon for float comparisons — exact == 0 / == 1 are unreliable
        // for values that may have been serialized/deserialized through JSON floats.
        apply_rotate             = std::fabs(args.degrees_rotation) > 1e-9;
        apply_snap               = std::fabs(args.degrees_snap)     > 1e-9;
        apply_directional_weight = args.speed_processor_args.whole &&
                                   std::fabs(args.range_weights.x - args.range_weights.y) > 1e-9;
        compute_ref_angle        = apply_snap || apply_directional_weight;
        apply_dir_mul_x          = std::fabs(args.lr_output_dpi_ratio - 1.0) > 1e-9;
        apply_dir_mul_y          = std::fabs(args.ud_output_dpi_ratio - 1.0) > 1e-9;
    }
};

// ── Modifier settings ─────────────────────────────────────────────────────────

struct modifier_settings {
    profile    prof;
    struct data_t {
        modifier_flags flags;
        vec2d          rot_direction = { 1, 0 };
        accel_union    accel_x;
        accel_union    accel_y;
    } data = {};
};

inline void init_settings(modifier_settings& settings) {
    settings.data.accel_x.init(settings.prof.accel_x);
    settings.data.accel_y.init(settings.prof.accel_y);
    settings.data.rot_direction = direction(settings.prof.degrees_rotation);
    settings.data.flags         = modifier_flags(settings.prof);
}

// ── Core modifier ─────────────────────────────────────────────────────────────

class modifier {
public:
    /// Apply acceleration to an input delta (counts/ms).
    /// @param in      Raw mouse delta [counts]; modified in place.
    /// @param sp      Per-axis speed processor (stateful).
    /// @param settings Precomputed modifier settings.
    /// @param dpi_factor  NORMALIZED_DPI / device DPI (reference input_dpi_normalization_factor)
    /// @param time    Time since last event [ms]
    void modify(vec2d& in, speed_processor& sp,
                const modifier_settings& settings,
                double dpi_factor, milliseconds time) const
    {
        auto& args  = settings.prof;
        auto& data  = settings.data;
        auto& flags = settings.data.flags;

        // A non-finite interval is just as unusable as zero: allowing NaN or
        // infinity into the stateful smoothers permanently poisons their
        // accumulators, so later valid events would all produce NaN/zero.
        if (!std::isfinite(time) || time <= 0) return;

        double reference_angle = 0;
        double ips_factor      = dpi_factor / time; // counts/ms -> inches/s

        // Guard: subnormal / extremely small time values can produce Inf ips_factor
        // which cascades into NaN through the accel pipeline.  Clamp to a sane max
        // (equivalent to ~0.001 ms poll interval, i.e. 1 MHz — well beyond any real hardware).
        constexpr double IPS_FACTOR_MAX = 1e6;
        if (!std::isfinite(ips_factor) || ips_factor > IPS_FACTOR_MAX)
            ips_factor = IPS_FACTOR_MAX;

        // 1. Rotation
        if (flags.apply_rotate)
            in = rotate(in, data.rot_direction);

        // 2. Angle snap
        // O2: also include the in.y==0 case (pure horizontal movement) in snap.
        // Previous code skipped snap for pure-X movement due to the "in.y != 0" guard.
        if (flags.compute_ref_angle && (in.x != 0 || in.y != 0)) {
            if (in.x == 0) {
                reference_angle = M_PI / 2;
            } else if (in.y == 0) {
                reference_angle = 0.0;  // pure horizontal
            } else {
                reference_angle = std::atan(std::fabs(in.y / in.x));
            }
            if (flags.apply_snap) {
                double snap = args.degrees_snap * M_PI / 180.0;
                if (reference_angle > M_PI / 2 - snap) {
                    reference_angle = M_PI / 2;
                    in = { 0, std::copysign(magnitude(in), in.y) };
                } else if (reference_angle < snap) {
                    reference_angle = 0;
                    in = { std::copysign(magnitude(in), in.x), 0 };
                }
            }
        }

        // 3. Speed clamp
        if (flags.clamp_speed) {
            double speed = magnitude(in) * ips_factor;
            if (speed > 0) {
                double ratio = clampsd(speed, args.speed_min, args.speed_max) / speed;
                in.x *= ratio;
                in.y *= ratio;
            }
        }

        // 4. Domain-weighted speed
        vec2d abs_vel = { std::fabs(in.x * ips_factor * args.domain_weights.x),
                          std::fabs(in.y * ips_factor * args.domain_weights.y) };

        if (sp.speed_flags.dist_mode == distance_mode::separate) {
            // Per-axis acceleration.  Reference callback weight convention:
            //   gain = 1 + (accel_fn - 1) * range_weight
            // so a 0 weight disables acceleration (returns 1), not the mouse.
            sp.calc_speed_separate(abs_vel, time);

            double scale_x = 1.0 + (data.accel_x.apply(abs_vel.x, args.accel_x) - 1.0) * args.range_weights.x;
            double scale_y = 1.0 + (data.accel_y.apply(abs_vel.y, args.accel_y) - 1.0) * args.range_weights.y;

            if (sp.speed_flags.should_smooth_scale) {
                scale_x = sp.smoother_x.scale_smoother.smooth(scale_x, time);
                scale_y = sp.smoother_y.scale_smoother.smooth(scale_y, time);
            }

            in.x *= scale_x;
            in.y *= scale_y;

            if (sp.speed_flags.should_smooth_output) {
                in.x = std::copysign(sp.smoother_x.output_speed_smoother.smooth(std::fabs(in.x), time), in.x);
                in.y = std::copysign(sp.smoother_y.output_speed_smoother.smooth(std::fabs(in.y), time), in.y);
            }
        } else {
            // Whole (combined) acceleration
            double speed = sp.calc_speed_whole(abs_vel, time);

            // Reference directional weight: linear interpolation in angle from
            // range_weights.x (0°, horizontal) to range_weights.y (90°, vertical):
            //   weight = rwx + (rwy - rwx) * (2/π) * reference_angle
            double range_weight = args.range_weights.x;
            if (flags.apply_directional_weight) {
                double diff = args.range_weights.y - args.range_weights.x;
                range_weight += 2.0 / M_PI * reference_angle * diff;
            }

            double scale = 1.0 + (data.accel_x.apply(speed, args.accel_x) - 1.0) * range_weight;

            if (sp.speed_flags.should_smooth_scale) {
                scale = sp.smoother_x.scale_smoother.smooth(scale, time);
            }

            in.x *= scale;
            in.y *= scale;

            if (sp.speed_flags.should_smooth_output) {
                // Reference smooths the magnitude and rescales both components by
                // the same ratio, preserving the cursor direction.
                double mag = magnitude(in);
                if (mag > 0) {
                    double smoothedMag = sp.smoother_x.output_speed_smoother.smooth(mag, time);
                    in.x *= (smoothedMag / mag);
                    in.y *= (smoothedMag / mag);
                }
            }
        }

        // 5. Output DPI normalization + directional DPI multipliers
        // Reference: dpi_adjustment = (output_dpi / NORMALIZED_DPI) * dpi_factor;
        // applied to X, and to Y scaled further by the Y/X output-DPI ratio.
        if (args.output_dpi > 0 && dpi_factor > 0) {
            double dpi_adjustment = (args.output_dpi / NORMALIZED_DPI) * dpi_factor;
            in.x *= dpi_adjustment;
            in.y *= dpi_adjustment * args.yx_output_dpi_ratio;
        }
        // RawAccel applies lr/ud ratios only in the *negative* (left/down) direction.
        // R6: guard against division-by-zero — here only a plain multiplication.
        if (flags.apply_dir_mul_x && args.lr_output_dpi_ratio > 0 && in.x < 0) {
            in.x *= args.lr_output_dpi_ratio;
        }
        if (flags.apply_dir_mul_y && args.ud_output_dpi_ratio > 0 && in.y < 0) {
            in.y *= args.ud_output_dpi_ratio;
        }

        // Defense-in-depth: ensure no NaN/Inf escapes the pipeline.
        // This catches edge cases from subnormal inputs, extreme parameter
        // combinations, or algorithm-specific numerical quirks.
        if (!std::isfinite(in.x)) in.x = 0;
        if (!std::isfinite(in.y)) in.y = 0;
    }
};

} // namespace rawaccel
