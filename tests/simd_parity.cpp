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

    simd::v2d h = simd::v2d_hypot(simd::v2d_set(3.0, 4.0), simd::v2d_set(0.0, 0.0));
    expect("v2d_hypot x", simd::v2d_get_x(h), 3.0, 1e-12);
    expect("v2d_hypot y", simd::v2d_get_y(h), 4.0, 1e-12);
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

int main() {
    printf("backend %s\n", backend_name());

    test_lane_roundtrip();

    // Y-only, X-only, and diagonal inputs. Classic gain on a raw (100,50)
    // delta is >1, so a surviving axis is unambiguously non-zero.
    run_pipeline("y_only", 0.0, 100.0);
    run_pipeline("x_only", 100.0, 0.0);
    run_pipeline("both", 100.0, 50.0);
    run_pipeline("both_rev", 50.0, 100.0);
    run_pipeline("small", 3.0, 7.0);

    if (failures == 0) {
        printf("result PASS\n");
        return 0;
    }
    printf("result FAIL (%d)\n", failures);
    return 1;
}
