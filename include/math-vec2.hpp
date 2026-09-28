#pragma once
#include <cmath>

namespace rawaccel {

struct vec2d {
    double x = 0;
    double y = 0;
};

inline double magnitude(vec2d v) {
    return std::hypot(v.x, v.y);
}

inline double lp_distance(vec2d v, double p) {
    // R6: guard against pow(0, negative) = Inf when both components are zero.
    // The correct Lp distance for the zero vector is always 0.
    double ax = std::fabs(v.x);
    double ay = std::fabs(v.y);
    if (ax == 0 && ay == 0) return 0;
    // Factor out the larger component so the inner ratio is always ≤ 1,
    // preventing pow overflow for large inputs with high p norms.
    //   ||v||_p = M * (1 + (m/M)^p)^(1/p)   where M = max, m = min.
    // For p < 1 the outer pow(·, 1/p) can legitimately overflow to Inf
    // even for bounded inputs (e.g. p=0.1, x=y=1 → 1024).  This is mathematically
    // correct but impractical for downstream use (acceleration evaluation at 1024 ips
    // is meaningless on real hardware).  Falling back to M (L∞ norm) is the safest
    // bounded approximation: M ≤ true Lp for p ≥ 1; M < true Lp for p < 1, but
    // all practical mice produce speeds within a few × M.  NOTE: an earlier
    // version of this comment claimed "sanitize clips lp_norm to [1e-9, 16]".
    // MEASURED FALSE (2026-09-28): src/config.cpp:570-571 only does
    //   if (lp_norm <= 0) lp_norm = 2;
    // — there is no 1e-9 lower bound and no 16 upper bound in sanitize.  The 16
    // is a MODE SELECTOR, not a clamp: rawaccel.hpp:197 routes
    // lp_norm >= MAX_NORM || lp_norm <= 0 to max-norm without ever calling this
    // function.  So in production p is only guaranteed to be *positive*, and a
    // denormal (e.g. lp_norm = 1e-320 in a hand-edited config) passes both
    // finite_or() and the <= 0 test — measured, that reaches the L49 branch
    // below.
    double M = ax > ay ? ax : ay;
    double m = ax > ay ? ay : ax;
    double result = M * std::pow(1.0 + std::pow(m / M, p), 1.0 / p);
    if (std::isfinite(result)) return result;
    // ORTA-BUG-MOTION-03: for p < 1 the exponent 1/p is large and the factored
    // pow(1 + (m/M)^p, 1/p) can overflow to Inf even though the true Lp norm is
    // finite (and ≥ M, e.g. p=0.1, x=y=1 → 1024).  A blind fallback to M (L∞)
    // under-reports the speed, so acceleration is evaluated at the wrong point.
    // Recompute in log space — but the true norm may still exceed DBL_MAX
    // (p=1e-9 on (3,4) → 2^(1/p)), in which case exp() returns Inf and the norm
    // is simply not representable; only then clamp to M (max component, the
    // documented tiny-p behaviour) instead of propagating Inf downstream.
    const double log_inner = p * std::log(m / M); // ≤ 0 since m/M ≤ 1
    // MEASURED (2026-09-28): in the full 34 092-assertion suite this whole log
    // block is entered EXACTLY ONCE — test_accel.cpp:9043-9047, lp_norm=1e-9 on
    // (3,4) — and it leaves through the `else` below (L47), returning M.
    //
    // The two flanking branches are NOT dead code.  They are unreachable with
    // FINITE COMPONENTS, and reachable as soon as one component is infinite:
    //
    //  * Finite components.  Reaching either branch needs |log_inner| > 600,
    //    and |log(m/M)| is at most ~708 in double, so |p| > 0.85.  But ENTERING
    //    this block at all needs `result` non-finite, i.e. 1/p > 709, i.e.
    //    p < 1.41e-3.  Those intervals do not intersect, and p < 0 cannot help
    //    either (1/p < 0 makes result smaller, never non-finite).  Measured:
    //    34 092 assertions and a 128 480-call sweep leave both at 0 while their
    //    sibling L47 fires 61 760 times.  Positive control: changing L46's
    //    threshold from 600 to -600 moved its counter 0 -> 61 760 and L47 to 0,
    //    so the zeros are real, not a dead instrument.
    //
    //  * M = ±Inf.  Then m/M is exactly 0 (finite / Inf), so log(0) = -Inf and
    //    |log(m/M)| is unbounded — the 708 bound above no longer holds.  The
    //    block is entered because pow(0,p) contributes nothing, pow(1, 1/p) = 1,
    //    and M * 1 = Inf.  Measured: (Inf,3) p=2, (3,Inf) p=1 and (Inf,0) p=1
    //    all take L45 — with POSITIVE p, so L45 does not require p < 0 — while
    //    (Inf,3) p=-1 and (Inf,0) p=-1 take L46.
    //
    //  * In PRODUCTION the components are finite: daemon/daemon.cpp:2323-2324
    //    zeroes a non-finite dx/dy before anything else, and the subpixel
    //    remainder is added in motion_math.hpp:35-36 AFTER modify() returns, so
    //    it cannot reach this function.  So these branches are unreachable on
    //    the production path but live on the API surface — lp_distance is public
    //    and inline, and tests call it directly.  NOT dead code; do not delete.
    //
    // L49 below is likewise not dead: a denormal p reaches it (note above).
    double log_1pi;
    if (log_inner < -600.0)     log_1pi = 0.0;       // exp underflows → 1
    else if (log_inner > 600.0) log_1pi = log_inner; // exp overflows → term dominates
    else                        log_1pi = std::log1p(std::exp(log_inner));
    const double log_result = std::log(M) + log_1pi / p;
    if (!std::isfinite(log_result)) return M;
    const double expanded = std::exp(log_result);
    return std::isfinite(expanded) ? expanded : M;
}

inline double maxsd(double a, double b) {
    return a > b ? a : b;
}

inline double minsd(double a, double b) {
    return a < b ? a : b;
}

inline double clampsd(double val, double lo, double hi) {
    return val < lo ? lo : (val > hi ? hi : val);
}

inline vec2d rotate(vec2d v, vec2d dir) {
    return { v.x * dir.x - v.y * dir.y, v.x * dir.y + v.y * dir.x };
}

inline vec2d direction(double degrees) {
    double rad = degrees * M_PI / 180.0;
    return { std::cos(rad), std::sin(rad) };
}

} // namespace rawaccel
