// db_diff.cpp — PERF5 sayisal etki olcumu (salt-olcum araci).
// Ayni girdi dizisiyle deadband'li/deadband'siz CIKTI karsilastirmasi:
// mod.modify() ciktisi (double, trunc oncesi) olay olay kaydedilir.
// Derleme: -DTREND_VARIANT=0 -DTREND_DEADBAND=$db -I olcum/aj5/shadow/include
// Kullanim: ./db_X > /tmp/opencode/trace_X.txt  (her satir: i out_x out_y %.17g)
#ifndef TREND_VARIANT
#define TREND_VARIANT 0
#endif
#include "rawaccel.hpp"
#include "config.hpp"
#include <cstdio>

using namespace rawaccel;

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

int main(int argc, char** argv) {
    int scenario = argc > 1 ? std::atoi(argv[1]) : 0;
    const int N = 200000;
    profile prof;
    setup_dual(prof, 1.0, 1.0, 1.0); // FULL: input+scale+output trend'li
    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);
    modifier mod;
    speed_processor sp;
    sp.init(prof.speed_processor_args);
    for (int i = 0; i < N; ++i) {
        double dx, dy;
        if (scenario == 0) { // bench-sabiti: isaret degisir, buyukluk sabit
            dx = (i & 1) ? 10.0 : -10.0;
            dy = (i & 1) ? -5.0 : 5.0;
        } else { // degisken fare: hizlanma + durma + mikro hareket
            int ph = i % 20000;
            double v = ph < 5000 ? 2.0 + ph * 0.004
                     : ph < 8000 ? 22.0 - (ph - 5000) * 0.007
                     : ph < 10000 ? 0.0
                     : 0.5 + (ph % 7) * 0.13;
            dx = ((i & 1) ? v : -v) * 0.9;
            dy = ((i & 3) < 2 ? v : -v) * 0.4;
        }
        vec2d m = { dx, dy };
        mod.modify(m, sp, settings, 1.0, 1);
        std::printf("%d %.17g %.17g\n", i, m.x, m.y);
    }
    return 0;
}
