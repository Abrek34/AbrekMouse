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
    // Unreachable in production either way: daemon.cpp:2360-2361 zeroes a
    // non-finite dx/dy long before this, and the subpixel remainder is added in
    // motion_math.hpp:35-36 AFTER modify() returns.
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
    // finite_or() and the <= 0 test — measured, that reaches the L49 branch
    // below.
    double M = ax > ay ? ax : ay;
    double m = ax > ay ? ay : ax;
    // AJ1 §5 (R16, savunma-derinligi — HATA DUZELTMESI DEGIL): anlasilamayan
    // bir bileseni "hareket yok" say.  Buradan Inf yaymak, onu motion_math'in
    // trunc -> isfinite(tx) -> 0 zincirinden gecirip TAMAMEN SESSIZCE yutuyor:
    // fare bir an duruyor, sonra normale donuyor, hicbir yerde kayit yok.
    // Proje karari zaten bu: rawaccel.hpp:612-613 girdinin sonunda
    // `if (!isfinite(in.x)) in.x = 0;` — olculemez girdi = hareket yok.
    //
    // ⚠️ AJ1'in TARIHINDEN AYRILDIK — ve sebebi olcum. AJ1 "M hesaplandiktan
    // hemen sonra `if (!std::isfinite(M)) return 0;`" dedi; ONCE YAPTIM, ve
    // PC2 KIRMIZI GELDI: iki bilesenden biri NaN ise guard HIC DEVREYE
    // GIRMiyor.  Sebep: M bir KARSILASTIRMALLA seciliyor ve NaN her
    // karsilastirmada false:
    //     M = (ax > ay ? ax : ay)   ->   (NaN,3) icin M = 3 (SONLU!)
    // yani NaN asla M'ye girmez, NaN m'ye girer ve m/M NaN olur; sonuc
    // "uydurma bir sayi" olarak cikar.  OLCULDU: (NaN,3) p=2 -> 3.
    // Dahası, sonuc ASIMETRIK: (3,NaN) -> 0 iken (NaN,3) -> 3. Ayni girdi,
    // iki bilesen yer degistirilmis, iki farkli HIZ.  Bu tam da AJ1'in
    // §1'de "hata gorunmez" dedigi kotu sey.
    // DOGRULAMA: guard M UZERINDE degil, KAYNAK BILESENLER uzerinde olmali —
    // NaN ancak orada yakalanabilir.  (olcum: olcum/aj2/run_guard_pc.sh PC2)
    //
    // DELIBEREN OLARAK REFERANSTAN AYRILIR: resim RawAccel'in lp_distance'i
    // (tests/oracle/ref/math-vec2.hpp:34-37) Inf/NaN'yi OLDUGU GIBI
    // yayiyor — (Inf,3) p=2 icin O DA inf donuyor. Yani "port ozel
    // sizinti" DEGIL, iki tarafta da var. Portun farki: olculemez girdiyi
    // 0 sayiyor. Uretimde erisilemez (asagida), yani bu bir tutarlilik
    // tercihidir, bir hata onarimi degil.
    //
    // URETIMDE NASIL SONLU? — once bunu dogru yazelim, cunku onceki yorum
    // YANLIŞ GEREKCEYI veriyordu: "daemon.cpp:2323-2324 sonlu olmayan dx/dy'yi
    // daha ilk adimda sifirlar" (AJ1 §3 bunun 2360-2361 oldugunu duzeltti, ama
    // HALA YANLIŞ). OLCULDU: guard 2360-2361, flush_motion()'in
    // raw_passthrough KOLUNDA (2352'de acilir) ve o kol 2383'te return eder;
    // ivme yolu 2510'daki apply_motion_math'e gider. Yani guard IVME YOLUNDA
    // CALISMIYOR. modifier::modify()'de de girdi tarafinda isfinite korumasi
    // YOK: rawaccel.hpp:612-613 sona yakin, lp_distance ise 555'te cagriliyor
    // -> once gelir, sonra koruma.  (olcum: olcum/aj2/guard_konumu.cpp S1)
    //
    // Gercek gerekce TAVAN: dx/dy, kernel'in int32 ev.value'larinin
    // double'a toplamidir (daemon.cpp:2808/2810, linux/input.h:44 __s32) ve
    // read_batch 32 olayliktir (daemon.cpp:2573) -> |dx| <= 32 * INT32_MAX
    // = 6.87e10. |in| <= bunun * dpi_factor(<=1000) * domain_weight(<=1e6)
    // = 6.87e19. Referansin sqrt(x*x+y*y) tasma esigi ~1.34e154, yani
    // uretimde referans ASLA taslamaz ve 1e200 ayrimi 1.46e180 kat uzakta
    // kalir.  (olcum: ayni dosya S2/S3.)
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
    //  * M = ±Inf.  SUPERSEDED by the isfinite(M) guard above (R16): that
    //    guard returns 0 before this block, so an infinite component can no
    //    longer reach here.  The reachability that used to be listed here
    //    ((Inf,3) p=2 / (3,Inf) p=1 / (Inf,0) p=1 → L45, (Inf,3) p=-1 /
    //    (Inf,0) p=-1 → L46) is now dead, and the 708 bound above holds
    //    unconditionally again.  Kept as a record of what the guard changed.
    //
    //  * In PRODUCTION the components are finite — but NOT because of a guard.
    //    The previous text here cited daemon.cpp:2323-2324 as zeroing a
    //    non-finite dx/dy "before anything else"; AJ1 §3 flagged the line, and
    //    measuring the branch showed the CLAIM was wrong too: the real guard
    //    (2360-2361) sits in the raw_passthrough column that RETURNS at 2383,
    //    so the accel path never runs it, and modifier::modify() has no
    //    input-side guard either (612-613 runs after this call site).
    //    The genuine reason is the CEILING, documented at the guard above:
    //    int32 events summed into a double over a 32-event batch.  The
    //    subpixel remainder is still added in motion_math.hpp:37-38 AFTER
    //    modify() returns, so it cannot reach this function either.
    //    So the branches stay unreachable in production but live on the API
    //    surface — lp_distance is public and inline, and tests call it
    //    directly.  NOT dead code; do not delete.
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
