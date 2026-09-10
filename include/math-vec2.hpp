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
    // all practical mice produce speeds within a few × M, and sanitize clips
    // lp_norm to [1e-9, 16] so this path is only reached for p < 1 edge cases.
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
