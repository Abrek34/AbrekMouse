// simd_parity.cpp — SIMD backend parity + Y-axis survival regression
//
// WHY THIS FILE EXISTS
// --------------------
// The AVX2 path of include/simd_math.hpp had a lane-selection bug: Y is stored
// in lane 1, but v2d_store()/v2d_get_y() read lane 2 (the zero padding lane).
// Every AVX2 build therefore wrote 0.0 into the Y component, so vertical mouse
// movement was silently dead in production — the daemon and CLI are built with
// -march=native (AVX2 on any x86-64 CPU since Haswell).
//
// It shipped because NOTHING ever compiled the AVX2 path in a test:
//   - tests/run_tests.sh     has no -march flag  -> SSE2 only
//   - tests/oracle/run_oracle.sh drops -march   -> SSE2 only
//   - .github/workflows/ci.yml sets RAWACCEL_PORTABLE=1 -> SSE2 only
// so the one path real users run was the one path never exercised.
//
// This test is compiled three times (AVX2 / SSE2 / scalar) by
// tests/run_simd_parity.sh. It asserts, per backend, that a Y-only input
// survives modifier::modify(), and the runner then diffs the three backends'
// numeric output so any future lane bug shows up as a backend mismatch.
//
// Output is plain deterministic text: one "case <label> <x> <y>" line each,
// so `diff` is a valid cross-backend comparison. DO NOT print addresses,
// timings, or anything backend-dependent outside the numeric fields.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "rawaccel.hpp"
#include "config.hpp"

using namespace rawaccel;

static const char* backend_name() {
#if RAWACCEL_HAVE_AVX2
    return "AVX2";
#elif RAWACCEL_HAVE_SSE2
    return "SSE2";
#else
    return "SCALAR";
#endif
}

static int failures = 0;

static void expect(const char* what, double got, double want, double eps) {
    bool ok = std::fabs(got - want) <= eps;
    if (!ok) {
        printf("FAIL: %s -> got %.12g, want %.12g\n", what, got, want);
        failures++;
    }
}

static void expect_true(const char* what, bool cond) {
    if (!cond) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

// Emit one backend-independent numeric line for the cross-backend diff.
//
// NaN is CANONICALISED, and only NaN. The sign of a NaN is not architecturally
// guaranteed — the same operation can propagate -nan on one backend and nan on
// another without either being wrong, so diffing the raw text would report a
// mismatch that is not a behaviour difference. ±inf and ±0 are left EXACTLY as
// they are: inf-vs-(-inf) and 0-vs-(-0) are real, observable differences and
// this file has already been caught by one of them (see test_extreme_primitives'
// abs(-0) case, which the scalar fallback got wrong).
static void emit_case(const char* label, double v) {
    char buf[64];
    if (std::isnan(v)) snprintf(buf, sizeof buf, "nan");
    else                snprintf(buf, sizeof buf, "%.17g", v);
    printf("case %s %s\n", label, buf);
}

// ── 1. Primitives: every lane accessor must round-trip X and Y ──────────────
// This is the check that pins the original bug directly.
static void test_lane_roundtrip() {
    const double X = 11.0, Y = 22.0;

    simd::v2d v = simd::v2d_set(X, Y);

    expect("v2d_get_x(v2d_set)", simd::v2d_get_x(v), X, 0.0);
    expect("v2d_get_y(v2d_set)", simd::v2d_get_y(v), Y, 0.0);

    double out[2] = { -1.0, -1.0 };
    simd::v2d_store(out, v);
    expect("v2d_store x", out[0], X, 0.0);
    expect("v2d_store y", out[1], Y, 0.0);

    double src[2] = { X, Y };
    simd::v2d lv = simd::v2d_load(src);
    expect("v2d_get_x(v2d_load)", simd::v2d_get_x(lv), X, 0.0);
    expect("v2d_get_y(v2d_load)", simd::v2d_get_y(lv), Y, 0.0);

    // hmin/hmax must ignore the zero padding lanes, otherwise hmin is
    // permanently 0.0 and hmax is wrong for all-negative inputs.
    expect("v2d_hmin", simd::v2d_hmin(v), X, 0.0);
    expect("v2d_hmax", simd::v2d_hmax(v), Y, 0.0);

    simd::v2d neg = simd::v2d_set(-5.0, -3.0);
    expect("v2d_hmin(neg)", simd::v2d_hmin(neg), -5.0, 0.0);
    expect("v2d_hmax(neg)", simd::v2d_hmax(neg), -3.0, 0.0);

    expect_true("v2d_all_finite", simd::v2d_all_finite(v));
    simd::v2d nan_v = simd::v2d_set(std::nan(""), 1.0);
    expect_true("!v2d_all_finite(NaN in Y)",
                !simd::v2d_all_finite(nan_v));

    // O31: v2d_hypot SİLİNDİ (include/simd_math.hpp) — bu üç çağrı yüzünden
    // buradan da kalktı. Nedeni: SIMD backend'leri sqrt(x*x+y*y) kullandığı
    // için 1e200'de `inf` veriyor, skaler `std::hypot` ile 1.41421e+200 döndüğü
    // halde bu kapı yalnız hypot(3,4) denediği için ayrışmaya KÖR kalıyordu.
    // Yerine test_extreme_primitives() geldi (aşağıda).
}

// ── 1b. Extreme inputs: the values the normal cases never reach ─────────────
//
// WHY THIS EXISTS
// ---------------
// The deleted v2d_hypot divergence (inf at 1e200 vs 1.41421e+200 scalar) was
// invisible to this gate because the gate only ever fed hypot(3, 4) — three and
// four are the two values a naive sqrt(x*x+y*y) gets RIGHT. A parity gate that
// only tries values which are known to agree is a gate that cannot fail.
//
// So: this section feeds the primitives values where the three backends are
// most likely to diverge, and emits every result as a `case` line so the
// runner's cross-backend diff covers them too.
//
// Two distinct mechanisms are at work and both are covered:
//   1. IEEE edge semantics — ±0, denormals, overflow-to-inf, NaN handling.
//      Denormals matter because AVX2/SSE2 inherit MXCSR FTZ/DAZ from the
//      environment: if anything ever sets those, every denormal silently
//      becomes 0 in the SIMD backends and stays denormal in the scalar one.
//   2. Lane discipline — v2d_hmin/v2d_hmax must reduce lanes 0,1 only. On ±0
//      and on equal lanes a wrong reduction still "looks" fine at 11.0/22.0.
static void test_extreme_primitives() {
    const double inf  = std::numeric_limits<double>::infinity();
    const double nan  = std::numeric_limits<double>::quiet_NaN();
    const double den  = std::numeric_limits<double>::denorm_min();  // 4.94e-324
    const double tiny = std::numeric_limits<double>::min();         // 2.23e-308
    const double dmax = std::numeric_limits<double>::max();
    const double NEG  = -0.0;

    // ── ±0 through min/max. MINPD/MAXPD return src2 when both operands are
    // zero, so a backend may legitimately differ from another here; the point
    // is that the three are pinned and a change shows up in the diff. The
    // VALUE is not asserted — it is not architecturally guaranteed.
    emit_case("min(+0,-0)",  simd::v2d_get_x(simd::v2d_min(simd::v2d_set1(0.0), simd::v2d_set1(NEG))));
    emit_case("min(-0,+0)",  simd::v2d_get_x(simd::v2d_min(simd::v2d_set1(NEG), simd::v2d_set1(0.0))));
    emit_case("max(+0,-0)",  simd::v2d_get_x(simd::v2d_max(simd::v2d_set1(0.0), simd::v2d_set1(NEG))));
    emit_case("max(-0,+0)",  simd::v2d_get_x(simd::v2d_max(simd::v2d_set1(NEG), simd::v2d_set1(0.0))));

    // ── v2d_abs MUST clear the sign bit, i.e. abs(-0.0) == +0.0. This is
    // asserted, not just diffed: the SIMD backends do it with
    // _mm_andnot_pd(0x8000…, a) which is unconditionally correct, so +0.0 is
    // the one value all three backends must produce. A `x < 0 ? -x : x`
    // implementation fails here, because (-0.0 < 0) is false and -0.0 is
    // therefore returned unchanged. MEASURED: the scalar fallback in
    // include/simd_math.hpp did exactly that and returned -0.
    expect("v2d_abs(-0.0)", simd::v2d_get_x(simd::v2d_abs(simd::v2d_set1(NEG))), 0.0, 0.0);
    emit_case("abs(-0)",  simd::v2d_get_x(simd::v2d_abs(simd::v2d_set1(NEG))));
    expect("v2d_abs(-DBL_MAX)", simd::v2d_get_x(simd::v2d_abs(simd::v2d_set1(-dmax))), dmax, 0.0);
    emit_case("abs(-max)", simd::v2d_get_x(simd::v2d_abs(simd::v2d_set1(-dmax))));

    // ── denormals must survive (no FTZ/DAZ). Any value other than the input
    // means the denormal was flushed to zero.
    emit_case("denorm_rt",  simd::v2d_get_x(simd::v2d_set(den, den)));
    emit_case("denorm+0",   simd::v2d_get_x(simd::v2d_add(simd::v2d_set1(den), simd::v2d_set1(0.0))));
    emit_case("denorm*1",   simd::v2d_get_x(simd::v2d_mul(simd::v2d_set1(den), simd::v2d_set1(1.0))));
    emit_case("denorm/1",   simd::v2d_get_x(simd::v2d_div(simd::v2d_set1(den), simd::v2d_set1(1.0))));
    emit_case("min(den,0)", simd::v2d_get_x(simd::v2d_min(simd::v2d_set1(den), simd::v2d_set1(0.0))));
    emit_case("sqrt(den)",  simd::v2d_get_x(simd::v2d_sqrt(simd::v2d_set1(den))));
    // Underflow to exactly zero is legitimate (tiny*tiny < DBL_MIN).
    emit_case("tiny*tiny", simd::v2d_get_x(simd::v2d_mul(simd::v2d_set1(tiny), simd::v2d_set1(tiny))));

    // ── overflow to +inf. This is the exact class the deleted v2d_hypot
    // diverged on, kept here so the next overflow-prone primitive is covered
    // by a case that CAN fail.
    emit_case("1e200*1e200", simd::v2d_get_x(simd::v2d_mul(simd::v2d_set1(1e200), simd::v2d_set1(1e200))));
    emit_case("max*max",     simd::v2d_get_x(simd::v2d_mul(simd::v2d_set1(dmax), simd::v2d_set1(dmax))));
    emit_case("1/0",         simd::v2d_get_x(simd::v2d_div(simd::v2d_set1(1.0), simd::v2d_set1(0.0))));
    emit_case("1e200+1e200", simd::v2d_get_x(simd::v2d_add(simd::v2d_set1(1e200), simd::v2d_set1(1e200))));

    // ── NaN operand order in min/max. Asserted only as "same as the other two
    // backends" via the diff; NaN selection is implementation-defined.
    emit_case("min(NaN,1)", simd::v2d_get_x(simd::v2d_min(simd::v2d_set1(nan), simd::v2d_set1(1.0))));
    emit_case("min(1,NaN)", simd::v2d_get_x(simd::v2d_min(simd::v2d_set1(1.0), simd::v2d_set1(nan))));
    emit_case("max(NaN,1)", simd::v2d_get_x(simd::v2d_max(simd::v2d_set1(nan), simd::v2d_set1(1.0))));
    // L04-03 (HIGH): the three max/min cases above are all DEGENERATE — MINPD
    // and MAXPD return the same value for ±0 and NaN-vs-number, so the gate
    // could not distinguish v2d_max from v2d_min (mutation stayed green).
    // These two use ordered, distinct operands and can only pass if max is
    // really max.  NOTE: emit_case() alone would NOT catch the mutation —
    // it only feeds the cross-backend diff, and a mutation hits all three
    // backends identically.  These use expect_true so the VALUE is pinned.
    emit_case("max(-1,1)",  simd::v2d_get_x(simd::v2d_max(simd::v2d_set1(-1.0), simd::v2d_set1(1.0))));
    emit_case("max(1,-1)",  simd::v2d_get_x(simd::v2d_max(simd::v2d_set1(1.0), simd::v2d_set1(-1.0))));
    emit_case("max(2,3)",   simd::v2d_get_x(simd::v2d_max(simd::v2d_set1(2.0), simd::v2d_set1(3.0))));
    expect_true("max(-1,1)==1", simd::v2d_get_x(simd::v2d_max(simd::v2d_set1(-1.0), simd::v2d_set1(1.0))) == 1.0);
    expect_true("max(2,3)==3",  simd::v2d_get_x(simd::v2d_max(simd::v2d_set1(2.0), simd::v2d_set1(3.0))) == 3.0);
    // Symmetric min pin (the mutation direction that used to be the only
    // detectable one — keep it explicit rather than accidental).
    expect_true("min(-1,1)==-1", simd::v2d_get_x(simd::v2d_min(simd::v2d_set1(-1.0), simd::v2d_set1(1.0))) == -1.0);
    expect_true("min(2,3)==2",   simd::v2d_get_x(simd::v2d_min(simd::v2d_set1(2.0), simd::v2d_set1(3.0))) == 2.0);

    // ── hmin/hmax lane discipline at the edges.
    emit_case("hmin(+0,-0)", simd::v2d_hmin(simd::v2d_set(0.0, NEG)));
    emit_case("hmax(+0,-0)", simd::v2d_hmax(simd::v2d_set(0.0, NEG)));
    emit_case("hmin(den,-den)", simd::v2d_hmin(simd::v2d_set(den, -den)));
    emit_case("hmax(den,-den)", simd::v2d_hmax(simd::v2d_set(den, -den)));

    // ── all_finite must classify the extremes correctly.
    expect_true("all_finite(1e200)",  simd::v2d_all_finite(simd::v2d_set1(1e200)));
    expect_true("!all_finite(inf)",   !simd::v2d_all_finite(simd::v2d_set1(inf)));
    expect_true("!all_finite(NaN)",   !simd::v2d_all_finite(simd::v2d_set1(nan)));
    expect_true("all_finite(denorm)", simd::v2d_all_finite(simd::v2d_set1(den)));
}

// ── 2. End-to-end: the acceleration pipeline must not eat the Y axis ───────
// modify_separate_simd() is only reached when speed_processor_args.whole is
// false, which routes through the SIMD block in include/rawaccel.hpp.
static modifier_settings make_settings(const profile& prof) {
    modifier_settings s;
    s.prof = prof;
    init_settings(s);
    return s;
}

static void run_pipeline(const char* label, double dx, double dy) {
    profile prof;
    prof.accel_x.mode = accel_mode::classic;
    prof.accel_y.mode = accel_mode::classic;
    prof.output_dpi     = NORMALIZED_DPI;
    prof.domain_weights = { 1, 1 };
    prof.range_weights  = { 1, 1 };
    prof.speed_processor_args.whole = false;  // -> distance_mode::separate

    modifier_settings settings = make_settings(prof);
    speed_processor sp;
    sp.init(prof.speed_processor_args);
    modifier mod;

    vec2d in{ dx, dy };
    mod.modify(in, sp, settings, 1.0, 16.0);

    // The regression: a non-zero Y input must never come out as exactly 0.
    if (dy != 0.0) {
        char buf[128];
        snprintf(buf, sizeof buf, "%s: Y axisi korunmadi", label);
        expect_true(buf, in.y != 0.0);
    }
    if (dx != 0.0) {
        char buf[128];
        snprintf(buf, sizeof buf, "%s: X axisi korunmadi", label);
        expect_true(buf, in.x != 0.0);
    }

    // Emitted for the cross-backend diff in run_simd_parity.sh.
    printf("case %s %.12g %.12g\n", label, in.x, in.y);
}

// ── 2b. Extreme inputs through the real pipeline ───────────────────────────
//
// The primitive checks above pin the arithmetic. These go through
// modifier::modify() so the uç-sınır contract the daemon actually relies on is
// checked end to end: modify() ends with an isfinite() guard
// (include/rawaccel.hpp) whose whole purpose is that no NaN/Inf reaches
// motion_math. If a backend ever produced a non-finite OUTPUT, that guard
// would be masking a backend bug instead of defending against bad input — and
// the guard makes both look identical here, which is why the primitive section
// above exists separately.
static void run_pipeline_extreme(const char* label, double dx, double dy) {
    profile prof;
    prof.accel_x.mode = accel_mode::classic;
    prof.accel_y.mode = accel_mode::classic;
    prof.output_dpi     = NORMALIZED_DPI;
    prof.domain_weights = { 1, 1 };
    prof.range_weights  = { 1, 1 };
    prof.speed_processor_args.whole = false;  // -> distance_mode::separate

    modifier_settings settings = make_settings(prof);
    speed_processor sp;
    sp.init(prof.speed_processor_args);
    modifier mod;

    vec2d in{ dx, dy };
    mod.modify(in, sp, settings, 1.0, 16.0);

    char buf[128];
    snprintf(buf, sizeof buf, "%s: X ciktisi sonlu degil (%g)", label, in.x);
    expect_true(buf, std::isfinite(in.x));
    snprintf(buf, sizeof buf, "%s: Y ciktisi sonlu degil (%g)", label, in.y);
    expect_true(buf, std::isfinite(in.y));

    emit_case(label, in.x);
    emit_case(label, in.y);
}

// ── 2c. Smoother branches of the SIMD path (L04-01) ────────────────────────
// scale/output smoothing make modify_separate_simd take the branches at
// rawaccel.hpp:428/437, which the base pipeline above never reaches.  A lane
// swap there (the historical Y-axis class) hits ALL THREE backends
// identically, so the cross-backend diff cannot see it — the value pins below
// are what catch it.  Input is y-only (0,300) with a strong acceleration, so:
//   * scale branch: out.y ≈ 300 * scale_y (large) vs swapped ≈ 300 * scale_x
//     (scale_x == 1 because x has no motion) — a ~40x gap.
//   * output branch: out.y ≈ v_in.y vs swapped = smooth(|v_in.x|) = 0.
static void run_pipeline_smoothing(const char* label, double dx, double dy,
                                   double scale_hl, double output_hl) {
    profile prof;
    prof.accel_x.mode = accel_mode::classic;
    prof.accel_y.mode = accel_mode::classic;
    prof.accel_x.acceleration = 2.0; // strong gain so the pins separate widely
    prof.accel_y.acceleration = 2.0;
    // High cap so the clean case reaches a large gain while a lane swap
    // collapses to ≈dy (scale branch, scale_x == 1) or 0 (output branch,
    // smooth(|0|) == 0).  Default cap.y = 1.5 would compress the separation.
    prof.accel_x.cap = { 15.0, 10.0 };
    prof.accel_y.cap = { 15.0, 10.0 };
    prof.output_dpi     = NORMALIZED_DPI;
    prof.domain_weights = { 1, 1 };
    prof.range_weights  = { 1, 1 };
    prof.speed_processor_args.whole = false;
    prof.speed_processor_args.scale_smooth_halflife        = scale_hl;
    prof.speed_processor_args.output_speed_smooth_halflife = output_hl;

    modifier_settings settings = make_settings(prof);
    speed_processor sp;
    sp.init(prof.speed_processor_args);
    modifier mod;

    // Several frames so the EMA has actually engaged by the last sample.
    // Every frame is fed the SAME fresh input: reusing the returned `in` (the
    // in-place modified value) would turn a lane swap into an oscillation
    // (X↔Y alternate) whose last sample lands back on the correct order,
    // masking the very bug this asserts (measured: mutation stayed green
    // with the reuse form).
    vec2d in{};
    for (int i = 0; i < 16; i++) {
        in = vec2d{ dx, dy };
        mod.modify(in, sp, settings, 1.0, 16.0);
    }

    if (dy != 0.0) {
        char buf[128];
        snprintf(buf, sizeof buf, "%s: Y axisi korunmadi", label);
        expect_true(buf, in.y != 0.0);
    }
    // L04-01 value pin: with the correct lane order the y-only input comes out
    // at least 3x the raw delta (gain ~10 at these settings); a lane swap
    // collapses it to ≈dy (scale branch) or ≈0 (output branch).
    if (dy > 0.0 && in.x == 0.0) {
        char buf[128];
        snprintf(buf, sizeof buf, "%s: lane sirasi bozuk (out.y=%g, beklenen > %g)",
                 label, in.y, dy * 3.0);
        expect_true(buf, in.y > dy * 3.0);
    }
    emit_case(label, in.x);
    emit_case(label, in.y);
}

// ── 2d. Non-unit per-axis output DPI ratio (L04-02) ────────────────────────
static void run_pipeline_dpi_ratio(const char* label, double lr, double yx,
                                   double dx, double dy) {
    profile prof;
    prof.accel_x.mode = accel_mode::classic;
    prof.accel_y.mode = accel_mode::classic;
    prof.output_dpi     = NORMALIZED_DPI;
    prof.domain_weights = { 1, 1 };
    prof.range_weights  = { 1, 1 };
    prof.lr_output_dpi_ratio = lr;
    prof.yx_output_dpi_ratio = yx;
    prof.speed_processor_args.whole = false;

    modifier_settings settings = make_settings(prof);
    speed_processor sp;
    sp.init(prof.speed_processor_args);
    modifier mod;

    vec2d in{ dx, dy };
    mod.modify(in, sp, settings, 1.0, 16.0);

    // With yx = 2 and equal raw deltas the per-axis gains are equal (same
    // accel args), so out.y must be exactly twice out.x.  The mutation
    // "v2d_set(dpi_adj, dpi_adj)" collapses them to equal.
    if (dx == dy && dy != 0.0 && yx > 1.0) {
        char buf[160];
        snprintf(buf, sizeof buf,
                 "%s: YX dpi ratio uygulanmadi (out.x=%g out.y=%g, beklenen y=%.3g)",
                 label, in.x, in.y, in.x * yx);
        expect_true(buf, std::fabs(in.y - in.x * yx) < 1e-6 * std::fabs(in.x * yx));
    }
    emit_case(label, in.x);
    emit_case(label, in.y);
}

int main() {
    printf("backend %s\n", backend_name());

    test_lane_roundtrip();
    test_extreme_primitives();

    // Y-only, X-only, and diagonal inputs. Classic gain on a raw (100,50)
    // delta is >1, so a surviving axis is unambiguously non-zero.
    run_pipeline("y_only", 0.0, 100.0);
    run_pipeline("x_only", 100.0, 0.0);
    run_pipeline("both", 100.0, 50.0);
    run_pipeline("both_rev", 50.0, 100.0);
    run_pipeline("small", 3.0, 7.0);

    // Extreme inputs: the guard is supposed to neutralise all of them, and the
    // three backends must agree on HOW it neutralises them.
    const double inf  = std::numeric_limits<double>::infinity();
    const double nan  = std::numeric_limits<double>::quiet_NaN();
    const double den  = std::numeric_limits<double>::denorm_min();
    const double dmax = std::numeric_limits<double>::max();
    run_pipeline_extreme("x_zero",      0.0,   100.0);
    run_pipeline_extreme("neg_zero",   -0.0,   100.0);
    run_pipeline_extreme("denorm",      den,   -den);
    run_pipeline_extreme("huge",        1e200,  1e200);
    run_pipeline_extreme("huge_mixed",  1e200, -1e200);
    run_pipeline_extreme("dmax",        dmax,   dmax);
    run_pipeline_extreme("inf_in",      inf,    1.0);
    run_pipeline_extreme("nan_in",      nan,    1.0);
    run_pipeline_extreme("both_inf",    inf,    inf);

    // L04-01 (HIGH): the gate never exercised the SCALE/OUTPUT smoother
    // branches of modify_separate_simd — both smoothing halflifes were 0, so
    // the X/Y lane swap at rawaccel.hpp:437 (`v2d_set(ox, oy)`) could not be
    // observed (mutation stayed green).  Y-only inputs + value pins inside
    // run_pipeline_smoothing() close that: the swapped result collapses.
    run_pipeline_smoothing("scale_smooth_y_only",  0.0, 300.0, 20.0, 0.0);
    run_pipeline_smoothing("output_smooth_y_only", 0.0, 300.0, 0.0, 20.0);
    run_pipeline_smoothing("both_smooth_y_only",   0.0, 300.0, 20.0, 20.0);

    // L04-02 (HIGH): yx_output_dpi_ratio is a real user setting; the old gate
    // left it at 1, so dropping the Y multiplier (rawaccel.hpp:461) stayed
    // green.  Equal raw deltas make the per-axis gains equal (same args), so
    // the ratio pins the comparison: out.y must be exactly 2x out.x.
    run_pipeline_dpi_ratio("yx_ratio_2", 1.0, 2.0, 100.0, 100.0);

    if (failures == 0) {
        printf("result PASS\n");
        return 0;
    }
    printf("result FAIL (%d)\n", failures);
    return 1;
}
