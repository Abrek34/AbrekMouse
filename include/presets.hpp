#pragma once
#include "config.hpp"
#include <string>

namespace rawaccel {

/// Built-in preset names, in display order. `make_preset()` documents the
/// per-game tuning; the GUI preset dropdown and the CLI share this list so the
/// two surfaces never drift apart.
inline constexpr const char* PRESET_NAMES[] = {
    "gaming", "office", "precision", "disable",
    "cs2",    "valorant", "apex",     "fps",
};
inline constexpr int PRESET_COUNT = 8;

/// Create a profile from a built-in preset (single source of truth for preset
/// values — used by both `rawaccel-cli create-preset` and the GUI "New
/// Profile" preset dropdown). Flags an unknown preset by clearing `dp.name`.
inline device_profile make_preset(const std::string& preset_name, const std::string& profile_name) {
    device_profile dp;
    dp.name = profile_name;
    dp.dev_cfg.dpi = 800;
    dp.dev_cfg.polling_rate = 1000;
    dp.prof.raw_passthrough = false;

    if (preset_name == "gaming") {
        // Classic acceleration — popular for FPS games
        dp.prof.accel_x.mode = accel_mode::classic;
        dp.prof.accel_y.mode = accel_mode::classic;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.acceleration = 0.005;
        dp.prof.accel_y.acceleration = 0.005;
        dp.prof.accel_x.exponent_classic = 2.0;
        dp.prof.accel_y.exponent_classic = 2.0;
        dp.prof.accel_x.limit = 1.8;
        dp.prof.accel_y.limit = 1.8;
        dp.prof.accel_x.input_offset = 0;
        dp.prof.accel_y.input_offset = 0;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "office") {
        // Light natural acceleration for general use
        dp.prof.accel_x.mode = accel_mode::natural;
        dp.prof.accel_y.mode = accel_mode::natural;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.limit = 1.3;
        dp.prof.accel_y.limit = 1.3;
        dp.prof.accel_x.decay_rate = 0.08;
        dp.prof.accel_y.decay_rate = 0.08;
        dp.prof.accel_x.motivity = 1.2;
        dp.prof.accel_y.motivity = 1.2;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "precision") {
        // Low acceleration for precision work (CAD, design)
        dp.prof.accel_x.mode = accel_mode::classic;
        dp.prof.accel_y.mode = accel_mode::classic;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.acceleration = 0.002;
        dp.prof.accel_y.acceleration = 0.002;
        dp.prof.accel_x.exponent_classic = 1.5;
        dp.prof.accel_y.exponent_classic = 1.5;
        dp.prof.accel_x.limit = 1.2;
        dp.prof.accel_y.limit = 1.2;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "disable" || preset_name == "none" || preset_name == "off") {
        // Raw passthrough — no acceleration
        dp.prof.raw_passthrough = true;
        dp.prof.accel_x.mode = accel_mode::noaccel;
        dp.prof.accel_y.mode = accel_mode::noaccel;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "cs2") {
        // CS2 tactical shooter: pro eDPI band 560-1000, classic curve, early kick-in.
        // Low swap of slow movement = micro-adjust headshots stay 1:1, flicks ramp up.
        dp.prof.accel_x.mode = accel_mode::classic;
        dp.prof.accel_y.mode = accel_mode::classic;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.acceleration = 0.004;
        dp.prof.accel_y.acceleration = 0.004;
        dp.prof.accel_x.exponent_classic = 2.0;
        dp.prof.accel_y.exponent_classic = 2.0;
        dp.prof.accel_x.input_offset = 0;
        dp.prof.accel_y.input_offset = 0;
        dp.prof.accel_x.limit = 1.6;
        dp.prof.accel_y.limit = 1.6;
        dp.prof.accel_x.cap = { 18.0, 1.6 };
        dp.prof.accel_y.cap = { 18.0, 1.6 };
        dp.prof.accel_x.cap_mode_val = cap_mode::out;
        dp.prof.accel_y.cap_mode_val = cap_mode::out;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "valorant") {
        // Valorant (TenZ-inspired base): natural curve for smooth entry/exit,
        // modest gain, high cap so panic flicks stay controlled but fast.
        dp.prof.accel_x.mode = accel_mode::natural;
        dp.prof.accel_y.mode = accel_mode::natural;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.limit = 1.3;
        dp.prof.accel_y.limit = 1.3;
        dp.prof.accel_x.decay_rate = 0.08;
        dp.prof.accel_y.decay_rate = 0.08;
        dp.prof.accel_x.motivity = 1.2;
        dp.prof.accel_y.motivity = 1.2;
        dp.prof.accel_x.input_offset = 0.02;
        dp.prof.accel_y.input_offset = 0.02;
        dp.prof.accel_x.cap = { 30.0, 2.0 };
        dp.prof.accel_y.cap = { 30.0, 2.0 };
        dp.prof.accel_x.cap_mode_val = cap_mode::out;
        dp.prof.accel_y.cap_mode_val = cap_mode::out;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "apex") {
        // Apex Legends: tracking-heavy + verticality. Power mode ramps fast for
        // 180° flicks while light smoothing keeps track. output_offset floor ~0.9
        // keeps slow micro-aim close to 1:1 (avoid sub-1:1 muddy feel at 2-10 mm/s).
        dp.prof.accel_x.mode = accel_mode::power;
        dp.prof.accel_y.mode = accel_mode::power;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.scale = 2.2;
        dp.prof.accel_y.scale = 2.2;
        dp.prof.accel_x.exponent_power = 0.8;
        dp.prof.accel_y.exponent_power = 0.8;
        dp.prof.accel_x.input_offset = 0.02;
        dp.prof.accel_y.input_offset = 0.02;
        dp.prof.accel_x.output_offset = 0.9;
        dp.prof.accel_y.output_offset = 0.9;
        dp.prof.accel_x.cap = { 28.0, 2.2 };
        dp.prof.accel_y.cap = { 28.0, 2.2 };
        dp.prof.accel_x.cap_mode_val = cap_mode::out;
        dp.prof.accel_y.cap_mode_val = cap_mode::out;
        dp.prof.output_dpi = 1000;
    } else if (preset_name == "fps") {
        // Generic FPS: balanced classic curve, moderate acceleration and cap.
        // Safe starting point for most shooters / aim trainers.
        dp.prof.accel_x.mode = accel_mode::classic;
        dp.prof.accel_y.mode = accel_mode::classic;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_y.gain = true;
        dp.prof.accel_x.acceleration = 0.005;
        dp.prof.accel_y.acceleration = 0.005;
        dp.prof.accel_x.exponent_classic = 2.0;
        dp.prof.accel_y.exponent_classic = 2.0;
        dp.prof.accel_x.input_offset = 0.01;
        dp.prof.accel_y.input_offset = 0.01;
        dp.prof.accel_x.limit = 1.8;
        dp.prof.accel_y.limit = 1.8;
        dp.prof.accel_x.cap = { 20.0, 1.8 };
        dp.prof.accel_y.cap = { 20.0, 1.8 };
        dp.prof.accel_x.cap_mode_val = cap_mode::out;
        dp.prof.accel_y.cap_mode_val = cap_mode::out;
        dp.prof.output_dpi = 1000;
    } else {
        dp.name.clear(); // signal unknown preset
    }
    return dp;
}

} // namespace rawaccel