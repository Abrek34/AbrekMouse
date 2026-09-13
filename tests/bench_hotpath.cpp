#include "rawaccel.hpp"
#include "config.hpp"
#include "../daemon/motion_math.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace rawaccel;

struct BenchResult {
    const char* name;
    double ns_per_event;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t syscalls;
};

void run_benchmark(const char* name, modifier& mod, speed_processor& sp,
                   modifier_settings& settings, double dpi_factor,
                   milliseconds time_ms, int iterations,
                   std::vector<BenchResult>& results) {
    double remainder_x = 0, remainder_y = 0;
    int out_x = 0, out_y = 0;
    double dx = 10.0, dy = 5.0;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        apply_motion_math(mod, sp, settings, dpi_factor, time_ms, dx, dy, remainder_x, remainder_y, out_x, out_y);
        dx = -dx;
        dy = -dy;
    }
    auto end = std::chrono::high_resolution_clock::now();

    double total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    double ns_per_event = total_ns / iterations;

    results.push_back({name, ns_per_event, 0, 0, 0});
    std::cout << name << ": " << ns_per_event << " ns/event (" << iterations << " iterations)\n";
}

int main(int argc, char** argv) {
    int iterations = (argc > 1) ? std::atoi(argv[1]) : 100000;
    std::vector<BenchResult> results;

    double dpi_factor = 1.0;
    milliseconds time_ms = 1;

    // 1. noaccel / raw passthrough
    {
        profile prof;
        prof.raw_passthrough = true;
        prof.accel_x.mode = accel_mode::noaccel;
        prof.accel_y.mode = accel_mode::noaccel;
        prof.output_dpi = NORMALIZED_DPI;

        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);

        modifier mod;
        speed_processor sp;
        sp.init(prof.speed_processor_args);

        run_benchmark("noaccel", mod, sp, settings, dpi_factor, time_ms, iterations, results);
    }

    // 2. power-whole (combined axis, no smoothing)
    {
        profile prof;
        prof.raw_passthrough = false;
        prof.accel_x.mode = accel_mode::power;
        prof.accel_y.mode = accel_mode::power;
        prof.accel_x.gain = true;
        prof.accel_y.gain = true;
        prof.accel_x.scale = 2.2;
        prof.accel_y.scale = 2.2;
        prof.accel_x.exponent_power = 0.8;
        prof.accel_y.exponent_power = 0.8;
        prof.speed_processor_args.whole = true;
        prof.output_dpi = NORMALIZED_DPI;

        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);

        modifier mod;
        speed_processor sp;
        sp.init(prof.speed_processor_args);

        run_benchmark("power-whole", mod, sp, settings, dpi_factor, time_ms, iterations, results);
    }

    // 3. classic (combined axis, no smoothing)
    {
        profile prof;
        prof.raw_passthrough = false;
        prof.accel_x.mode = accel_mode::classic;
        prof.accel_y.mode = accel_mode::classic;
        prof.accel_x.gain = true;
        prof.accel_y.gain = true;
        prof.accel_x.acceleration = 0.005;
        prof.accel_y.acceleration = 0.005;
        prof.accel_x.exponent_classic = 2.0;
        prof.accel_y.exponent_classic = 2.0;
        prof.accel_x.limit = 1.8;
        prof.accel_y.limit = 1.8;
        prof.speed_processor_args.whole = true;
        prof.output_dpi = NORMALIZED_DPI;

        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);

        modifier mod;
        speed_processor sp;
        sp.init(prof.speed_processor_args);

        run_benchmark("classic", mod, sp, settings, dpi_factor, time_ms, iterations, results);
    }

    // 4. power + rotation 45° + snap 15° + speed clamp
    {
        profile prof;
        prof.raw_passthrough = false;
        prof.accel_x.mode = accel_mode::power;
        prof.accel_y.mode = accel_mode::power;
        prof.accel_x.gain = true;
        prof.accel_y.gain = true;
        prof.accel_x.scale = 2.2;
        prof.accel_y.scale = 2.2;
        prof.accel_x.exponent_power = 0.8;
        prof.accel_y.exponent_power = 0.8;
        prof.speed_processor_args.whole = true;
        prof.degrees_rotation = 45.0;
        prof.degrees_snap = 15.0;
        prof.speed_min = 10.0;
        prof.speed_max = 4000.0;
        prof.output_dpi = NORMALIZED_DPI;

        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);

        modifier mod;
        speed_processor sp;
        sp.init(prof.speed_processor_args);

        run_benchmark("power+rot45+snap15+clamp", mod, sp, settings, dpi_factor, time_ms, iterations, results);
    }

    // 5. power dual-axis + 4 EMA smoothers (input, scale, output x2)
    {
        profile prof;
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
        prof.speed_processor_args.input_speed_smooth_halflife = 1.0;
        prof.speed_processor_args.scale_smooth_halflife = 1.0;
        prof.speed_processor_args.output_speed_smooth_halflife = 1.0;
        prof.output_dpi = NORMALIZED_DPI;

        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);

        modifier mod;
        speed_processor sp;
        sp.init(prof.speed_processor_args);

        run_benchmark("power-dual+4ema", mod, sp, settings, dpi_factor, time_ms, iterations, results);
    }

    // 6. full apply_motion_math with subpixel (classic, whole, no smoothing)
    {
        profile prof;
        prof.raw_passthrough = false;
        prof.accel_x.mode = accel_mode::classic;
        prof.accel_y.mode = accel_mode::classic;
        prof.accel_x.gain = true;
        prof.accel_y.gain = true;
        prof.accel_x.acceleration = 0.005;
        prof.accel_y.acceleration = 0.005;
        prof.accel_x.exponent_classic = 2.0;
        prof.accel_y.exponent_classic = 2.0;
        prof.accel_x.limit = 1.8;
        prof.accel_y.limit = 1.8;
        prof.speed_processor_args.whole = true;
        prof.output_dpi = NORMALIZED_DPI;

        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);

        modifier mod;
        speed_processor sp;
        sp.init(prof.speed_processor_args);

        double remainder_x = 0, remainder_y = 0;
        int out_x = 0, out_y = 0;
        double dx = 10.0, dy = 5.0;

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            apply_motion_math(mod, sp, settings, dpi_factor, time_ms, dx, dy, remainder_x, remainder_y, out_x, out_y);
            dx = -dx;
            dy = -dy;
        }
        auto end = std::chrono::high_resolution_clock::now();
        // Consume the outputs so GCC cannot dead-code-eliminate loop-carried
        // work (remainder/sp are otherwise unused after the loop, which let the
        // compiler collapse this benchmark into a partial pass-through — a DCE
        // artifact that understated the true per-event cost).
        volatile int sink = out_x + out_y;
        (void)sink;

        double total_ns = std::chrono::duration<double, std::nano>(end - start).count();
        double ns_per_event = total_ns / iterations;

        results.push_back({"apply_motion_math(full)", ns_per_event, 0, 0, 0});
        std::cout << "apply_motion_math(full): " << ns_per_event << " ns/event (" << iterations << " iterations)\n";
    }

    // Summary
    std::cout << "\n=== SUMMARY ===\n";
    for (const auto& r : results) {
        std::cout << r.name << ": " << r.ns_per_event << " ns/event\n";
    }

    return 0;
}