// T13 — Differential oracle: LOCAL C++ port side.
//
// Compiles against the local project's own headers (include/) and prints the
// same <case-name>\t<speed>\t<gain> format as reference.cpp so the two can be
// diffed. If any row differs beyond the tolerance enforced by run_oracle.sh,
// the local port has drifted from the official reference for that input.

#include "oracle_cases.hpp"

#include "include/accel-union.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace rawaccel;

namespace {

accel_args build_args(const oracle::Case& k) {
    accel_args a;
    a.mode = (k.mode == "classic") ? accel_mode::classic
           : (k.mode == "jump") ? accel_mode::jump
           : (k.mode == "lookup") ? accel_mode::lookup
           : (k.mode == "natural") ? accel_mode::natural
           : (k.mode == "synchronous") ? accel_mode::synchronous
           : (k.mode == "power") ? accel_mode::power
           : accel_mode::noaccel;
    a.gain = k.gain;
    a.acceleration     = k.acceleration;
    a.scale            = k.scale;
    a.decay_rate       = k.decay_rate;
    a.gamma            = k.gamma;
    a.motivity         = k.motivity;
    a.exponent_classic = k.exponent_classic;
    a.exponent_power   = k.exponent_power;
    a.limit            = k.limit;
    a.sync_speed       = k.sync_speed;
    a.smooth           = k.smooth;
    a.input_offset     = k.input_offset;
    a.output_offset    = k.output_offset;
    a.cap              = { k.cap_x, k.cap_y };
    a.cap_mode_val     = static_cast<cap_mode>(k.cap_mode);
    a.length           = static_cast<int>(k.lut.size());
    a.data[0]          = 0; // force clear of default; filled below if present
    for (size_t i = 0; i < k.lut.size() && i < LUT_RAW_DATA_CAPACITY; ++i)
        a.data[i] = k.lut[i];
    return a;
}

} // namespace

int main() {
    std::vector<oracle::Case> all = oracle::cases();
    for (const auto& k : all) {
        accel_args  a = build_args(k);
        accel_union au;
        au.init(a);
        for (double spd : k.speeds) {
            double g = au.apply(spd, a);
            std::printf("%s\t%g\t%.9g\n", k.name.c_str(), spd, g);
        }
    }
    return 0;
}