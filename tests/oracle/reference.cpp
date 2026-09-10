// T13 — Differential oracle: OFFICIAL RawAccel reference side.
//
// Compiles against the vendored, verbatim RawAccelOfficial/rawaccel master
// headers (tests/oracle/ref/, MIT © 2020 a1xd). Outputs the reference gain
// for every (case, speed) in the shared grid as:
//     <case-name>\t<speed>\t<gain>
// tests/oracle/local.cpp prints the identical format for the local port, and
// run_oracle.sh diffs the two.

#include "oracle_cases.hpp"

#include "accel-union.hpp"   // vendored official reference union

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
    a.cap_mode         = static_cast<cap_mode>(k.cap_mode);
    a.length           = static_cast<int>(k.lut.size());
    a.data[0]          = 0; // force clear of default; filled below if present
    for (size_t i = 0; i < k.lut.size() && i < LUT_RAW_DATA_CAPACITY; ++i)
        a.data[i] = k.lut[i];
    return a;
}

} // namespace

namespace {

template <template <bool> class Accel>
double run(const accel_args& a, double spd) {
    if (a.gain)
        return Accel<GAIN>(a)(spd, a);
    return Accel<LEGACY>(a)(spd, a);
}

} // namespace

int main() {
    std::vector<oracle::Case> all = oracle::cases();
    for (const auto& k : all) {
        accel_args a = build_args(k);
        for (double spd : k.speeds) {
            double g = 0;
            switch (a.mode) {
            case accel_mode::classic:     g = run<classic>(a, spd);         break;
            case accel_mode::jump:        g = run<jump>(a, spd);            break;
            case accel_mode::natural:     g = run<natural>(a, spd);         break;
            case accel_mode::synchronous: g = run<activation_framework>(a, spd); break;
            case accel_mode::power:       g = run<power>(a, spd);           break;
            case accel_mode::lookup:      g = lookup(a)(spd, a);            break;
            default:                      g = accel_noaccel(a)(spd, a);     break;
            }
            std::printf("%s\t%g\t%.9g\n", k.name.c_str(), spd, g);
        }
    }
    return 0;
}