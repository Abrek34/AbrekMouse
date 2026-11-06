// stage_prof.cpp — PERF2 deterministic stage profiler (salt-okunur gorev).
// power-dual+4ema'nin 301.7 ns/event'ini asamalarina ayirir. Hicbir uretim
// dosyasini degistirmez; sadece include/daemon header'larini OKUR.
// Derleme: g++ -O3 -march=native -std=c++20 -I include olcum/aj4/stage_prof.cpp -o /tmp/opencode/stage_prof
// Determinizm: sabit girdiler (dx alternasyonu bench ile ayni), 7 kosu medyani,
// tum ornekler basilir; ikinci kost ayni medyani vermelidir.
#include "rawaccel.hpp"
#include "config.hpp"
#include "../daemon/motion_math.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rawaccel;

static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

// bench'teki power-dual+4ema kurulumunun aynisi; sadece halflife'lar parametrik.
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

struct StageResult { const char* name; double median; std::vector<double> samples; };

// Bir asamayi bench ile ayni donguyle zamanla (mod/sp/settings her kosuda taze).
static StageResult time_stage(const char* name, double in_hl, double sc_hl, double out_hl,
                              int iterations, int runs) {
    StageResult r{ name, 0, {} };
    for (int k = 0; k < runs; ++k) {
        profile prof;
        setup_dual(prof, in_hl, sc_hl, out_hl);
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
        r.samples.push_back(double(t1 - t0) / iterations);
    }
    std::sort(r.samples.begin(), r.samples.end());
    r.median = r.samples[r.samples.size() / 2];
    return r;
}

// Ham matematik islemlerinin tekil maliyeti (20M tekrar, 7 kosu medyani).
static double time_math(const char* name, int op, int iterations, int runs) {
    std::vector<double> s;
    volatile double sink = 0;
    for (int k = 0; k < runs; ++k) {
        double acc = 0.5 + k * 0.01;
        auto t0 = now_ns();
        for (int i = 0; i < iterations; ++i) {
            switch (op) {
                case 0: acc = std::exp2(-0.6931 * (0.5 + (i & 7) * 0.125)); break;
                case 1: acc = std::pow(0.3 + (i & 15) * 0.1, 0.8); break;
                case 2: acc = std::hypot(3.0 + (i & 7), 4.0 - (i & 3)); break;
                case 3: acc = std::min(acc, 1.0 + (i & 1)); break;
                case 4: acc = std::copysign(std::fabs(acc) + 0.001 * (i & 3), (i & 1) ? 1.0 : -1.0); break;
            }
        }
        auto t1 = now_ns();
        sink = acc;
        s.push_back(double(t1 - t0) / iterations);
    }
    (void)sink;
    std::sort(s.begin(), s.end());
    double med = s[s.size() / 2];
    std::printf("%-12s : %6.2f ns/call  (ornekler: %.2f %.2f %.2f)\n",
                name, med, s[0], s[s.size()/2], s.back());
    return med;
}

int main() {
    const int ITERS = 1000000, RUNS = 7;
    std::printf("== PERF2 asama profili (iters=%d, kosu=%d, medyan) ==\n", ITERS, RUNS);
    StageResult full = time_stage("FULL in+sc+out", 1.0, 1.0, 1.0, ITERS, RUNS);
    StageResult nosm = time_stage("NOSMOOTH       ", 0, 0, 0, ITERS, RUNS);
    StageResult inp  = time_stage("INPUT yalniz   ", 1.0, 0, 0, ITERS, RUNS);
    StageResult insc = time_stage("INPUT+SCALE    ", 1.0, 1.0, 0, ITERS, RUNS);
    for (auto* r : { &full, &nosm, &inp, &insc })
        std::printf("%s: medyan %7.2f ns  (ornekler: %.2f %.2f %.2f %.2f %.2f %.2f %.2f)\n",
                    r->name, r->median, r->samples[0], r->samples[1], r->samples[2],
                    r->samples[3], r->samples[4], r->samples[5], r->samples[6]);
    double c_in  = inp.median - nosm.median;   // 2x linear_ema
    double c_sc  = insc.median - inp.median;   // 2x simple_ema
    double c_out = full.median - insc.median;  // 2x linear_ema
    double c_sum = nosm.median + c_in + c_sc + c_out;
    std::printf("--- turetilmis maliyetler ---\n");
    std::printf("taban (smoothing yok) : %7.2f ns\n", nosm.median);
    std::printf("input  2x linear_ema  : %7.2f ns  (cagri basi %.2f)\n", c_in, c_in / 2);
    std::printf("scale  2x simple_ema  : %7.2f ns  (cagri basi %.2f)\n", c_sc, c_sc / 2);
    std::printf("output 2x linear_ema  : %7.2f ns  (cagri basi %.2f)\n", c_out, c_out / 2);
    std::printf("toplam FULL olcumu    : %7.2f ns\n", full.median);
    std::printf("kapanis kontrolu      : %7.2f ns  (sapma %+.2f ns, %+.2f%%)\n",
                c_sum, c_sum - full.median, 100 * (c_sum - full.median) / full.median);
    std::printf("== ham matematik maliyetleri ==\n");
    double c_exp2 = time_math("exp2", 0, 20000000, RUNS);
    double c_pow  = time_math("pow", 1, 20000000, RUNS);
    double c_hyp  = time_math("hypot", 2, 20000000, RUNS);
    double c_min  = time_math("min", 3, 20000000, RUNS);
    double c_cps  = time_math("copysign", 4, 20000000, RUNS);
    // Olay basina dusen cagri (koddan): linear smooth 4x exp2 (4 cagri/olay),
    // simple smooth 2x exp2 (2 cagri/olay), power apply 2x pow, hypot ~1.
    std::printf("--- olay basina tahmin (4 lin*4 exp2 + 2 smp*2 exp2 + 2 pow + 1 hypot) ---\n");
    std::printf("16x exp2 + 4x exp2 + 2x pow + 1x hypot = %.1f ns\n",
                16 * c_exp2 + 4 * c_exp2 + 2 * c_pow + 1 * c_hyp);
    return 0;
}
