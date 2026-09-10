#pragma once
#include "rawaccel-base.hpp"
#include "math-vec2.hpp"
#include <cmath>

namespace rawaccel {

/// Represents the range [2^start, 2^stop], with `num` - 1 elements linearly
/// spaced between each exponential step (reference RawAccel `fp_rep_range`).
/// Used by synchronous (GAIN) to build its floating-point-rep LUT.
struct fp_rep_range {
    int start;
    int stop;
    int num;

    template <typename Func>
    void for_each(Func fn) const {
        for (int e = 0; e < stop - start; e++) {
            double exp_scale = std::scalbn(1, e + start) / num;
            for (int i = 0; i < num; i++) {
                fn((i + num) * exp_scale);
            }
        }
        fn(std::scalbn(1, stop));
    }

    int size() const {
        return (stop - start) * num + 1;
    }
};

/// Reference RawAccel lerp with directional clamping: interpolating past the
/// endpoint (t outside [0,1]) clamps to `b` in the direction of travel.
inline double lerp(double a, double b, double t) {
    double x = a + t * (b - a);
    if ((t > 1) == (a < b)) {
        return maxsd(x, b);
    }
    return minsd(x, b);
}

/// Lookup table acceleration. User provides (speed, output) point pairs.
/// Gains mode (args.gain / `velocity`) half-open behavior: each point's y is
/// treated as an output-speed, so the effective gain is y / x.
struct lookup {
    static constexpr int capacity = LUT_POINTS_CAPACITY;

    int  size     = 0;
    bool velocity = false;

    lookup() = default;

    lookup(const accel_args& args) {
        // O6: if length is odd (incomplete point pair), count only complete pairs.
        // length=1 → 0 pairs → behaves like noaccel; silent data loss prevented.
        int pairs = (args.length >= 2) ? (args.length / 2) : 0;
        size      = (pairs <= capacity) ? pairs : capacity; // buffer boundary guard
        velocity  = args.gain;
    }

    double operator()(double x, const accel_args& args) const {
        if (size <= 0) return 1.0; // empty LUT → no acceleration (port guard)

        const float* pts = args.data; // pairs [x0,y0, x1,y1, ...]

        if (x <= 0) return 0.0; // reference: strictly positive domain

        int lo = 0;
        int hi = size - 2;

        // Binary search for the bracketing segment.
        // O3 (P55): the former `hi < capacity - 1` wrapper was always true
        // (hi <= size - 2 <= capacity - 2) and the fall-through path below
        // already handles the degenerate (size <= 1) tables via lo == 0.
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            double px = static_cast<double>(pts[2 * mid]);

            if (x < px) {
                hi = mid - 1;
            } else if (x > px) {
                lo = mid + 1;
            } else {
                double y = static_cast<double>(pts[2 * mid + 1]);
                if (velocity) y /= x;
                return y;
            }
        }

        if (lo > 0) {
            double ax = static_cast<double>(pts[2 * (lo - 1)]);
            double ay = static_cast<double>(pts[2 * (lo - 1) + 1]);
            double bx = static_cast<double>(pts[2 * lo]);
            double by = static_cast<double>(pts[2 * lo + 1]);

            // O2 (P55): a zero-width segment (duplicate X from hand-edited
            // JSON) made t = ±Inf → lerp produced ±Inf → modify() zeroed the
            // output (silent no-motion).  Return the later point's value —
            // bounded, deterministic, and identical to reference for valid
            // strictly-increasing tables.
            double denom = bx - ax;
            if (denom == 0) {
                double y = by;
                if (velocity) y /= x;
                return y;
            }
            double t = (x - ax) / denom;
            double y = lerp(ay, by, t);
            if (velocity) y /= x;
            return y;
        }

        // x below the first point (or degenerate table): constant first output.
        double y = static_cast<double>(pts[1]);
        if (velocity) {
            double x0 = static_cast<double>(pts[0]);
            if (x0 <= 0) return 0.0; // port guard: avoid div-by-zero
            y /= x0;
        }
        return y;
    }
};

} // namespace rawaccel