// RawAccel Linux — libFuzzer harness for acceleration pipeline
//
// Exercises: modifier::modify(), apply_motion_math() with random inputs.
// Checks that no NaN escapes and no crash occurs.
//
// Build:
//   clang++ -std=c++20 -O2 -g -fsanitize=fuzzer,address,undefined \
//       -I include tests/fuzz_accel.cpp src/config.cpp             \
//       -o build-manual/fuzz_accel
//
// Run:
//   ./build-manual/fuzz_accel -max_len=256 -timeout=5

#include "../include/rawaccel.hpp"
#include "../include/config.hpp"
#include "../daemon/motion_math.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace rawaccel;

template <typename T>
static bool consume(const uint8_t*& data, size_t& size, T& out) {
    if (size < sizeof(T)) return false;
    std::memcpy(&out, data, sizeof(T));
    data += sizeof(T);
    size -= sizeof(T);
    return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Minimum: mode(1) + dx(8) + dy(8) + time(8) = 25 bytes
    if (size < 25) return 0;

    uint8_t mode_byte;
    double dx, dy, time_val;
    if (!consume(data, size, mode_byte)) return 0;
    if (!consume(data, size, dx)) return 0;
    if (!consume(data, size, dy)) return 0;
    if (!consume(data, size, time_val)) return 0;

    accel_mode mode = static_cast<accel_mode>(mode_byte % 7);

    // Read optional accel parameters (use defaults if not enough data)
    double acceleration = 0.007, exponent = 2.0, limit_val = 2.0;
    double cap_x = 10.0, cap_y = 2.0, rotation = 0.0;
    double speed_min = 0.0, speed_max = 100.0;
    consume(data, size, acceleration);
    consume(data, size, exponent);
    consume(data, size, limit_val);
    consume(data, size, cap_x);
    consume(data, size, cap_y);
    consume(data, size, rotation);
    consume(data, size, speed_min);
    consume(data, size, speed_max);

    // Build a device_profile and sanitize it (same path as real code)
    device_profile dp;
    dp.name = "fuzz";
    dp.dev_cfg.dpi = 800;
    dp.dev_cfg.polling_rate = 1000;
    dp.prof.accel_x.mode = mode;
    dp.prof.accel_x.gain = true;
    dp.prof.accel_x.acceleration = acceleration;
    dp.prof.accel_x.exponent_classic = exponent;
    dp.prof.accel_x.exponent_power = exponent;
    dp.prof.accel_x.limit = limit_val;
    dp.prof.accel_x.cap = { cap_x, cap_y };
    dp.prof.accel_x.scale = acceleration;
    dp.prof.accel_x.decay_rate = acceleration;
    dp.prof.accel_x.sync_speed = acceleration;
    dp.prof.accel_y = dp.prof.accel_x;
    dp.prof.degrees_rotation = rotation;
    dp.prof.speed_min = speed_min;
    dp.prof.speed_max = speed_max;

    // Sanitize (clamps all values to safe ranges)
    sanitize_device_profile(dp);

    // Init settings & speed_processor
    modifier_settings settings;
    settings.prof = dp.prof;
    init_settings(settings);

    speed_processor sp;
    sp.init(dp.prof.speed_processor_args);

    modifier mod;

    // T53-13: girdiyi pipeline'a değmeden törpüleme — eski kod dx/dy'yi
    // ±10000'e kelepçeleyip NaN→0, time<=0/Inf→1.0 yapınca daemon'un
    // 0.0625ms altı, subnormal/0/Inf (IPS_FACTOR_MAX), 1e300 (hypot),
    // NaN-yayılım koruma kolları bu harness'ta ERİŞİLEMEZ oluyordu.
    // Şimdi aynı bitler iki yola girer: HAM (kelepçesiz) ve hijyenik.
    const double raw_time = time_val;
    const double raw_dx = dx, raw_dy = dy;

    // ── Path 1: modifier::modify() ──────────────────────────────────────────
    {
        vec2d input = { raw_dx, raw_dy };
        double dpi_factor = NORMALIZED_DPI / static_cast<double>(dp.dev_cfg.dpi);
        milliseconds t = raw_time;
        mod.modify(input, sp, settings, dpi_factor, t);

        // T53-14: Inf de NaN kadar sessiz kaçış — !isfinite tuzağı.
        if (!std::isfinite(input.x) || !std::isfinite(input.y)) __builtin_trap();
    }

    // ── Path 2: apply_motion_math() ─────────────────────────────────────────
    {
        double rx = 0, ry = 0;
        int ox = 0, oy = 0;
        double dpi_factor = NORMALIZED_DPI / static_cast<double>(dp.dev_cfg.dpi);

        // Re-init speed_processor since we used it above
        speed_processor sp2;
        sp2.init(dp.prof.speed_processor_args);

        milliseconds t = raw_time;
        apply_motion_math(mod, sp2, settings, dpi_factor, t,
                          raw_dx, raw_dy, rx, ry, ox, oy);

        // T53-14: remainder NaN veya Inf ise bug (eski: yalnız isnan).
        if (!std::isfinite(rx) || !std::isfinite(ry)) __builtin_trap();
    }

    // ── Path 3: hijyenik yol — kelepçeli, deterministik (eski davranış) ─────
    {
        if (time_val <= 0 || !std::isfinite(time_val)) time_val = 1.0;
        if (time_val > 100.0) time_val = 100.0;
        milliseconds time_ms = time_val;
        if (!std::isfinite(dx)) dx = 0;
        if (!std::isfinite(dy)) dy = 0;
        if (dx > 10000) dx = 10000;
        if (dx < -10000) dx = -10000;
        if (dy > 10000) dy = 10000;
        if (dy < -10000) dy = -10000;

        vec2d input = { dx, dy };
        double dpi_factor = NORMALIZED_DPI / static_cast<double>(dp.dev_cfg.dpi);
        mod.modify(input, sp, settings, dpi_factor, time_ms);
        if (!std::isfinite(input.x) || !std::isfinite(input.y)) __builtin_trap();

        double rx = 0, ry = 0;
        int ox = 0, oy = 0;
        speed_processor sp3;
        sp3.init(dp.prof.speed_processor_args);
        apply_motion_math(mod, sp3, settings, dpi_factor, time_ms,
                          dx, dy, rx, ry, ox, oy);
        if (!std::isfinite(rx) || !std::isfinite(ry)) __builtin_trap();
    }

    return 0;
}
