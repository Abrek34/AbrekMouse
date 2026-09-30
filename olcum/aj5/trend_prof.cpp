// trend_prof.cpp — PERF4 inline boru olcumu (salt-olcum araci).
// Yontem: uretim include/ agacinin KOPYASI (olcum/aj5/shadow) + -I ile
// derlenir; smooth() inline kalir (AJ1'in 716 ns'lik manuel-cagri tuzagina
// dusulmez). TREND_VARIANT=N ile trend bloklari tek tek kapatilir.
// Uretim dosyasina DOKUNULMAZ. Deterministik: sabit girdi, 7 kosu medyani.
// Derleme orn.: g++ -O3 -march=native -std=c++20 -DTREND_VARIANT=0 \
//   -I olcum/aj5/shadow/include olcum/aj5/trend_prof.cpp -o /tmp/opencode/trend_V0
#ifndef TREND_VARIANT
#define TREND_VARIANT 0
#endif
#include "rawaccel.hpp"
#include "config.hpp"
#include "../daemon/motion_math.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <vector>

using namespace rawaccel;

static inline unsigned long long now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}

static void setup_dual(profile& prof, double in_hl, double sc_hl, double out_hl) {
    prof.raw_passthrough = false;
    prof.accel_x.mode = accel_mode::power;
    prof.accel_y.mode = accel_mode::power;
    prof.accel_x.gain = true;
    prof.accel_y.gain = true;
    prof.accel_x.scale = 2.2;
    prof.accel_y.scale = 2.2;
    prof.accel_x.exponent_power = 0.8;
    prof.accel_y.exponent_power = 0.8;
    prof.speed_processor_args.whole = false;
    prof.speed_processor_args.input_speed_smooth_halflife = in_hl;
    prof.speed_processor_args.scale_smooth_halflife = sc_hl;
    prof.speed_processor_args.output_speed_smooth_halflife = out_hl;
    prof.output_dpi = NORMALIZED_DPI;
}

// mode 0 = NOSMOOTH tabani, mode 1 = INPUT-only (2x linear_ema cagrisi/olay)
static double time_mode(int mode, int iterations, int runs, std::vector<double>& out) {
    std::vector<double> s;
    for (int k = 0; k < runs; ++k) {
        profile prof;
        if (mode == 0) setup_dual(prof, 0, 0, 0);
        else setup_dual(prof, 1.0, 0, 0);
        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);
        modifier mod;
        speed_processor sp;
        sp.init(prof.speed_processor_args);
        double rem_x = 0, rem_y = 0, dx = 10.0, dy = 5.0;
        int ox = 0, oy = 0;
        auto t0 = now_ns();
        for (int i = 0; i < iterations; ++i) {
            apply_motion_math(mod, sp, settings, 1.0, 1, dx, dy, rem_x, rem_y, ox, oy);
            dx = -dx;
            dy = -dy;
        }
        auto t1 = now_ns();
        volatile long long sink = (long long)ox + oy + (long long)(rem_x * 1000);
        (void)sink;
#ifdef TREND_PROBE
        if (mode == 1 && k == runs - 1) linear_ema_smoother::trend_probe_print();
#endif
        s.push_back(double(t1 - t0) / iterations);
    }
    std::sort(s.begin(), s.end());
    out = s;
    return s[s.size() / 2];
}

int main() {
    const int ITERS = 1000000, RUNS = 7;
    std::vector<double> sb, si;
    double base = time_mode(0, ITERS, RUNS, sb);
    double inp = time_mode(1, ITERS, RUNS, si);
    double per_call = (inp - base) / 2.0;
    std::printf("VARIANT=%d base=%.2f ns input=%.2f ns fark=%.2f ns cagri-basi=%.2f ns\n",
                TREND_VARIANT, base, inp, inp - base, per_call);
    std::printf("  base-ornekler: %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",
                sb[0], sb[1], sb[2], sb[3], sb[4], sb[5], sb[6]);
    std::printf("  input-ornekler: %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",
                si[0], si[1], si[2], si[3], si[4], si[5], si[6]);
    return 0;
}
