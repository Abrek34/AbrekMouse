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
    // Defense-in-depth for a non-finite COMPONENT, deliberately placed on the
    // inputs and NOT on M: with ax = NaN the comparison `ax > ay` is false, so
    // M silently becomes ay — a finite number — and a `!isfinite(M)` guard
    // downstream can never fire.  Measured 2026-09-28: lp_distance({NaN,3}, 2)
    // returned 3 with an M-based guard (the reference returns NaN there, so the
    // port was fabricating a speed from an unmeasurable vector), and returns 0
    // with this one.  The Inf cases already returned 0 either way.
    //
    // This DIVERGES from the reference on purpose: tests/oracle/ref/
    // math-vec2.hpp:34-37 is a different algorithm entirely
    // (pow(pow(|x|,p) + pow(|y|,p), 1/p)) and propagates NaN/Inf, where the
    // port folds "unmeasurable input" to "no motion" — the same policy
    // modifier::modify() already applies to its output at rawaccel.hpp:612-613.
    //
    // Unreachable in production — but NOT for the reason an earlier revision of
    // this comment gave.  It claimed daemon.cpp:2360-2361 "zeroes a non-finite
    // dx/dy long before this".  MEASURED, that is wrong twice over (AJ1 §3
    // flagged the line; the claim itself did not survive checking):
    //   1. That guard is inside flush_motion()'s raw_passthrough column, which
    //      opens at 2352 and RETURNS at 2383.  The accel path never runs it —
    //      it continues to apply_motion_math() at 2510.
    //   2. modifier::modify() has no input-side guard either: 612-613 runs at
    //      the END, whereas calc_speed_whole() → lp_distance() is called at 555.
    // The real reason is the CEILING.  dx/dy sums the kernel's int32 ev.value
    // (daemon.cpp:2808/2810, __s32 at linux/input.h:44) into a double over a
    // 32-event read_batch (daemon.cpp:2573), so |dx| <= 32*INT32_MAX = 6.87e10.
    // Then rawaccel.hpp:548 multiplies by `ips_factor`, NOT by dpi_factor:
    //   rawaccel.hpp:350   ips_factor = dpi_factor / time
    //   rawaccel.hpp:351   IPS_FACTOR_MAX = 1e6
    //   rawaccel.hpp:352-353  if (!isfinite(ips_factor) || > 1e6) -> 1e6
    // That is an ENFORCED clamp and it absorbs the `/time` (including time=0,
    // which would be inf), so ips_factor <= 1e6 unconditionally.  The tempting
    // "dpi_factor <= 1000" (NORMALIZED_DPI, rawaccel-base.hpp:45, with dpi >= 1
    // at config.cpp:361) is only a DERIVED bound and is not what the code
    // multiplies by — an omitted upper bound is an unproven one.
    // So:  |abs_vel| <= 6.87e10 * 1e6 * 1e6 = 6.87e22,
    // which is ~1.3e131 orders below the reference's sqrt(x*x+y*y) overflow
    // threshold (1.34e154).  The subpixel remainder is added at
    // motion_math.hpp:37-38, AFTER modify() returns, so it cannot reach here.
    // Evidence: olcum/aj2/guard_konumu.cpp (S1 branch, S2/S3 ceiling) and
    // olcum/aj2/run_guard_pc.sh (PC1 sensitivity, PC2 effect).
    if (!std::isfinite(ax) || !std::isfinite(ay)) return 0;
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
    // finite_or() and the <= 0 test — measured, that reaches the
    // `isfinite(expanded) ? expanded : M` branch at the end of this function
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
    // (3,4) — and it leaves through the `else` below, returning M.
    //
    // The two flanking branches are NOT dead code — do not delete them.  They
    // are unreachable with FINITE COMPONENTS.  An earlier revision of this
    // comment also claimed they were reachable as soon as one component is
    // infinite; that half was true then and is FALSE NOW, because the R16
    // component guard returns 0 first.  Both readings are kept below so the
    // change is legible, but only the second one is current:
    //
    //  * Finite components.  Reaching either branch needs |log_inner| > 600,
    //    and |log(m/M)| is at most ~708 in double, so |p| > 0.85.  But ENTERING
    //    this block at all needs `result` non-finite, i.e. 1/p > 709, i.e.
    //    p < 1.41e-3.  Those intervals do not intersect, and p < 0 cannot help
    //    either (1/p < 0 makes result smaller, never non-finite).  Measured
    //    with a COMPILER counter, not a hand-placed one —
    //    olcum/aj2/prove_kod_ayni.py --dallar (gcov -fprofile-arcs; the counter
    //    is therefore independent of this file):
    //      The three branches below are named by their CODE, never by line
    //      number: a line citation inside the file it cites breaks the moment
    //      anything is inserted above it, and that happened twice here.
    //        A = `if (log_inner < -600.0)  log_1pi = 0.0;`
    //        B = `else if (log_inner > 600.0)  log_1pi = log_inner;`
    //        C = `return std::isfinite(expanded) ? expanded : M;`
    //      driver        A        B        C
    //      grid          0        0        0
    //      search        0        0        0
    //    The search driver covers both signs, ±Inf, NaN, denormals and a
    //    logarithmic m/M × p cross-sweep (>100 000 calls) and leaves all three
    //    at 0.  POSITIVE CONTROL: relaxing A's threshold -600 -> 1e300 (one
    //    token) moved A's taken-count 0 -> 68 on the grid and took the file
    //    from 6 zero branches to 8, so the zeros are a real reachability
    //    result and not a stuck instrument.  The component guard's own three
    //    branches went 0 -> 972 / 99 592 / 100 564 under the search driver,
    //    which is what makes the "M = ±Inf" case below impossible.
    //    Cite-check: `prove_kod_ayni.py --atif` flags any self-citation whose
    //    target line is itself a comment.
    //
    //  * M = ±Inf.  SUPERSEDED by the component guard at the top of the
    //    function (R16), which returns 0 before this block: an infinite
    //    component can no longer reach here, so the 708 bound above holds
    //    unconditionally again.  The reachability that used to be listed here
    //    ((Inf,3) p=2 / (3,Inf) p=1 / (Inf,0) p=1 → the `log_inner < -600.0`
    //    branch, (Inf,3) p=-1 / (Inf,0) p=-1 → the `log_inner > 600.0` one)
    //    is now dead.  Kept as a record of the change.
    //
    //  * In PRODUCTION the components are finite — the reason is the CEILING
    //    (int32 events into a double over a 32-event batch), not a guard; see
    //    the measurement at the top of the function, which also records why
    //    the daemon.cpp guard an earlier revision cited does not apply here.
    //    The subpixel remainder is added in motion_math.hpp:37-38 AFTER
    //    modify() returns, so it cannot reach this function either.  So the
    //    branches stay unreachable in production but live on the API surface
    //    — lp_distance is public and inline, and tests call it directly.
    //    NOT dead code; do not delete.
    //
    // The `isfinite(expanded) ? expanded : M` line below is likewise not dead:
    // a denormal p reaches it (note above).
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
