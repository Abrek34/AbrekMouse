#pragma once
// T13 — shared parameter grid for the differential oracle.
//
// This file is intentionally dependency-free (plain values only) so that BOTH
// the vendored official RawAccel reference (tests/oracle/reference.cpp) and the
// local C++ port (tests/oracle/local.cpp) can map it onto their own accel_args
// structs. Field names were aligned 1:1 with RawAccelOfficial/rawaccel
// common/rawaccel-base.hpp (the local port only renames cap_mode → cap_mode_val).

#include <string>
#include <vector>
#include <utility>

namespace oracle {

struct Case {
    std::string name;
    std::string mode;              // classic|jump|lookup|natural|synchronous|power|noaccel
    bool        gain = true;

    double acceleration     = 0.005;
    double scale            = 1;
    double decay_rate       = 0.1;
    double gamma            = 1;
    double motivity         = 1.5;
    double exponent_classic = 2;
    double exponent_power   = 0.05;
    double limit            = 1.5;
    double sync_speed       = 5;
    double smooth           = 0.5;
    double input_offset     = 0;
    double output_offset    = 0;

    double cap_x            = 15;
    double cap_y            = 1.5;
    int    cap_mode         = 2;          // 0=io 1=in 2=out

    std::vector<float> lut;               // lookup: x,y,x,y,... pairs (as loaded)
    std::vector<double> speeds;
};

// Default speed sweep covering every behavioural region of every algorithm.
// P63 (esport grid genişletme): 2000/3000/4000 ips bands bridge the [1000,5000]
// gap flagged by P58 — tournament-grade flick/track speeds.
inline std::vector<double> default_speeds() {
    return { 0, 0.001, 0.005, 0.01, 0.1, 0.5, 1, 3, 5, 7.5, 10, 15, 30, 50,
             100, 250, 500, 1000, 2000, 3000, 4000, 5000, 10000, 100000 };
}

inline std::vector<Case> cases() {
    std::vector<Case> c;
    auto S = default_speeds();

    // ── Classic ────────────────────────────────────────────────────────────
    auto mkclass = [&](const std::string& n, bool gain, double accel, double exp,
                       double cx, double cy, int cm, double off = 0,
                       double spd = 5.0) {
        Case x;
        x.name = n; x.mode = "classic"; x.gain = gain;
        x.acceleration = accel; x.exponent_classic = exp;
        x.cap_x = cx; x.cap_y = cy; x.cap_mode = cm;
        x.input_offset = off; x.sync_speed = spd; x.speeds = S;
        return x;
    };
    c.push_back(mkclass("classic_gain_out_default",  true,  0.005, 2.0, 15,   1.5, 2));
    c.push_back(mkclass("classic_gain_io",           true,  0.005, 2.0, 150,  1.5, 0));
    c.push_back(mkclass("classic_gain_io_smallcapy", true,  0.005, 2.0, 150,  0.5, 0));
    c.push_back(mkclass("classic_gain_io_bigcapy",   true,  0.005, 2.0, 150,  2.0, 0));
    c.push_back(mkclass("classic_gain_in",           true,  0.005, 2.0, 150,  1.5, 1));
    c.push_back(mkclass("classic_gain_negaccel",     true, -0.01,  2.0, 0,    0,   2));
    c.push_back(mkclass("classic_gain_exp_le1",      true,  0.005, 0.5, 15,   1.5, 2));
    c.push_back(mkclass("classic_gain_offset",       true,  0.005, 2.0, 50,   1.5, 0, 10));
    c.push_back(mkclass("classic_legacy_out_default",false, 0.005, 2.0, 15,   1.5, 2));

    // ── Jump ───────────────────────────────────────────────────────────────
    auto mkjump = [&](const std::string& n, bool gain, double step_x, double step_y,
                      double smooth, double spd = 5.0) {
        Case x;
        x.name = n; x.mode = "jump"; x.gain = gain;
        x.acceleration = step_y; x.sync_speed = step_x; x.smooth = smooth;
        x.cap_x = step_x; x.cap_y = step_y;
        x.speeds = S;
        (void)spd;
        return x;
    };
    c.push_back(mkjump("jump_gain_smooth",     true,  15, 0.5, 0.5));
    c.push_back(mkjump("jump_legacy_smooth",   false, 15, 0.5, 0.5));
    c.push_back(mkjump("jump_legacy_tol1",     false, 15, 0.5, 1.0));
    c.push_back(mkjump("jump_legacy_hard",     false, 15, 1.0, 0.0));
    c.push_back(mkjump("jump_legacy_hard2",    false, 30, 1.0, 0.0));

    // ── Natural ─────────────────────────────────────────────────────────────
    auto mknat = [&](const std::string& n, bool gain, double limit, double decay,
                     double smooth) {
        Case x;
        x.name = n; x.mode = "natural"; x.gain = gain;
        x.limit = limit; x.decay_rate = decay; x.smooth = smooth;
        x.speeds = S;
        return x;
    };
    c.push_back(mknat("natural_gain_lim15", true, 1.5, 0.1, 0.5));
    c.push_back(mknat("natural_gain_lim05", true, 0.5, 0.1, 0.5));
    c.push_back(mknat("natural_legacy_lim15",false,1.5, 0.1, 0.5));
    c.push_back(mknat("natural_legacy_lim05",false,0.5, 0.1, 0.5));

    // ── Synchronous (activation_framework) ──────────────────────────────────
    auto mksync = [&](const std::string& n, bool gain, double power, double sync,
                      double smooth) {
        Case x;
        x.name = n; x.mode = "synchronous"; x.gain = gain;
        x.exponent_classic = power; x.sync_speed = sync; x.smooth = smooth;
        x.speeds = S;
        return x;
    };
    c.push_back(mksync("sync_gain_p2",    true, 2.0, 5.0, 0.5));
    c.push_back(mksync("sync_gain_p1",    true, 1.0, 5.0, 0.5));
    c.push_back(mksync("sync_gain_p07",   true, 0.7, 5.0, 0.5));
    c.push_back(mksync("sync_legacy_p2",  false,2.0, 5.0, 0.5));
    c.push_back(mksync("sync_legacy_p1",  false,1.0, 5.0, 0.5));
    // motivity/gamma sweep — P148: these default to 1.5/1.0 in *every* stacked
    // case, so the synchronous activation path (log_motivity, gamma_const) was
    // never oracle-drilled on motivity<1 (negative gamma_const → odd-symmetric
    // tanh branch) or gamma>log-space amplification. Presets ship motivity=1.2
    // (office/valorant), so this is reachable DA config.
    {
        Case x = mksync("sync_legacy_mot05_gam2", false, 2.0, 5.0, 0.5);
        x.motivity = 0.5; x.gamma = 2.0;   // motivity<1 → negative gamma_const
        c.push_back(x);
    }
    {
        Case x = mksync("sync_gain_mot2_gam05", true, 1.0, 5.0, 0.5);
        x.motivity = 2.0; x.gamma = 0.5;   // amplified gamma_const, gain path
        c.push_back(x);
    }

    // ── Lookup (velocity = gain) ────────────────────────────────────────────
    auto mklut = [&](const std::string& n, bool gain, const std::vector<float>& lut) {
        Case x;
        x.name = n; x.mode = "lookup"; x.gain = gain;
        x.lut = lut; x.speeds = S;
        return x;
    };
    const std::vector<float> lut7 = {
        0.f, 0.f, 1.f, 1.f, 2.f, 2.5f, 3.f, 4.f, 5.f, 10.f, 10.f, 30.f, 20.f, 100.f
    };
    c.push_back(mklut("lookup_velocity", true,  lut7));
    c.push_back(mklut("lookup_displacement", false, lut7));
    c.push_back(mklut("lookup_velocity_p1", true,  { 0.f, 1.f, 1.f, 1.f, 2.f, 1.f }));

    // ── Power ───────────────────────────────────────────────────────────────
    auto mkpow = [&](const std::string& n, bool gain, double scale, double exp) {
        Case x;
        x.name = n; x.mode = "power"; x.gain = gain;
        x.scale = scale; x.exponent_power = exp;
        x.speeds = S;
        return x;
    };
    c.push_back(mkpow("power_gain_p1",  true,  1.0, 1.0));
    c.push_back(mkpow("power_legacy_p1",false, 1.0, 1.0));
    // R3-NEW-2: exponent_power inside [1e-4, 1e-3).  The local port floors the
    // shared exponent at 1e-3 (BUG-02 — scale derivation and curve evaluation
    // use the SAME n), the vendored reference evaluates the raw exp.  Every
    // speed row deviates and is listed in known_deviations.txt; this case pins
    // the band so a future change to the floor can't go unnoticed.
    c.push_back(mkpow("power_tinyexp_floor", true,  1.0, 5e-4));

    // ── Regression sweep (P98): >100x gain-magnitude swing ──────────────────
    // Stress the relative-tolerance comparator across many orders of magnitude.
    // Custom speed lists EXCLUDE 0 on purpose: every power/synchronous row at
    // speed 0 is a documented "s(0) identity" deviation, and these cases exist
    // purely to hammer the inter-speed swing, so they must not add new rows.
    auto mkswing = [&](const std::string& n, bool gain, double exp,
                       double cx, double cy, int cm) {
        Case x;
        x.name = n; x.mode = "power"; x.gain = gain;
        x.scale = 1; x.exponent_power = exp;
        x.cap_x = cx; x.cap_y = cy; x.cap_mode = cm;
        x.speeds = { 1e-5, 1e-4, 1e-3, 0.01, 0.1, 0.3, 0.5, 0.7, 0.75, 0.8,
                     1, 3, 10, 100, 1000, 10000, 100000 };
        return x;
    };
    // exp=5, cap out {30,1.5}: cap.x = gain_inverse(1.5,5,1) ≈ 0.758, so rows
    // sweep 1e-25 → 1.5 (≈1.5e25 swing) and straddle the cap tail (0.75/0.8).
    c.push_back(mkswing("swing_power_gain_exp5_cap",    true,  5.0, 30, 1.5, 2));
    // exp=5, no cap (cap_y=0 → legacy_cap stays DBL_MAX): 1e-25 → 1e25 (1e50 swing).
    c.push_back(mkswing("swing_power_legacy_exp5_nocap",false, 5.0, 0,  0,   2));
    // exp=10, cap out {50,1.5}: cap.x ≈ 0.819, rows sweep 1e-50 → 1.5 (≈1.5e50 swing).
    c.push_back(mkswing("swing_power_gain_exp10_cap",   true, 10.0, 50, 1.5, 2));

    // ── P155 regression sweep ──────────────────────────────────────────────
    // Power io-cap cases added when the local port gained an explicit
    // cap.y<=0 / io branch. Speed 0 stays excluded (shared s(0) identity
    // deviation is covered globally); these rows exist to pin the io behavior.
    //  - io cap_y>0 (gain/legacy): must MATCH the reference exactly.
    //  - io cap_y=0: the reference derives scale=0 (gain: pow(0,...)/cap.x=0
    //    → dead curve / NaN; legacy: scale_from_output_point(...,C=0)=0), the
    //    local port treats cap.y<=0 as "no cap requested" → identity scale.
    //    These rows are the P155 documented deviation (see known_deviations.txt).
    //  - out cap {15,1.5} + output_offset=1e6 (offset.x≈8.4e19 >> cap.x≈3.23):
    //    P155#2 regression — the GAIN tail branch must win past cap.x exactly
    //    like the reference orders it (cap branch checked BEFORE the offset
    //    plateau). The old local order returned the flat 1e6 offset for every
    //    speed in the wide band, silently disabling the cap.
    auto mkp155 = [&](const std::string& n, bool gain, double cx, double cy, int cm,
                      double oo) {
        Case x;
        x.name = n; x.mode = "power"; x.gain = gain;
        x.scale = 0.5; x.exponent_power = 0.3;
        x.cap_x = cx; x.cap_y = cy; x.cap_mode = cm;
        x.output_offset = oo;
        x.speeds = { 1e-4, 1e-3, 0.01, 0.1, 1, 3, 5, 10, 100, 1000, 10000, 1e5 };
        return x;
    };
    c.push_back(mkp155("p155_io_cap2_gain",     true,  15, 2.0, 0, 0));
    c.push_back(mkp155("p155_io_cap2_legacy",   false, 15, 2.0, 0, 0));
    c.push_back(mkp155("p155_io_smallcapx_gain",true,  0.1, 2.0, 0, 0));
    c.push_back(mkp155("p155_io_cap0_gain",     true,  15, 0.0, 0, 0));
    c.push_back(mkp155("p155_io_cap0_legacy",   false, 15, 0.0, 0, 0));
    c.push_back(mkp155("p155_out_offtail_gain", true,  15, 1.5, 2, 1e6));
    c.push_back(mkp155("p155_out_offtail_legacy",false,15, 1.5, 2, 1e6));

    // ── Game profiles (R29 oyuncu turu — P60/P61 oracle kapsamı) ──────────
    // MIRRORS the shipped `rawaccel-cli create-preset` game sets (cs2, valorant,
    // apex, fps) AND the general sets (gaming, office, precision, disable) with
    // EXACT parameter values from include/presets.hpp (single source of truth
    // shared by CLI create-preset and the GUI preset dropdown), so the
    // differential oracle validates precisely what P60 ships. If a preset
    // changes or a new preset is added, mirror it here and in run_oracle.
    {
        Case x; // cs2
        x.name = "game_cs2_classic"; x.mode = "classic"; x.gain = true;
        x.acceleration = 0.004; x.exponent_classic = 2.0; x.limit = 1.6;
        x.input_offset = 0; x.cap_x = 18; x.cap_y = 1.6; x.cap_mode = 2;
        x.sync_speed = 5; x.speeds = S;
        c.push_back(x);
    }
    {
        Case x; // valorant
        x.name = "game_valorant_natural"; x.mode = "natural"; x.gain = true;
        x.limit = 1.3; x.decay_rate = 0.08; x.motivity = 1.2;
        x.input_offset = 0.02; x.cap_x = 30; x.cap_y = 2.0; x.cap_mode = 2;
        x.speeds = S;
        c.push_back(x);
    }
    {
        Case x; // apex (R37: output_offset 0.2 -> 1.0, sub-1:1 muddy fix;
        x.name = "game_apex_power"; x.mode = "power"; x.gain = true;
        x.scale = 2.2; x.exponent_power = 0.8;
        x.input_offset = 0.02; x.output_offset = 1.0;
        x.cap_x = 28; x.cap_y = 2.2; x.cap_mode = 2;
        x.speeds = S;
        c.push_back(x);
    }
    {
        Case x; // fps
        x.name = "game_fps_classic"; x.mode = "classic"; x.gain = true;
        x.acceleration = 0.005; x.exponent_classic = 2.0; x.limit = 1.8;
        x.input_offset = 0.01; x.cap_x = 20; x.cap_y = 1.8; x.cap_mode = 2;
        x.sync_speed = 5; x.speeds = S;
        c.push_back(x);
    }
    {
        Case x; // gaming (presets.hpp) — classic, cap {15,1.8} out (C-5: preset
        x.name = "game_gaming_classic"; x.mode = "classic"; x.gain = true;
        x.acceleration = 0.005; x.exponent_classic = 2.0; x.limit = 1.8;
        x.input_offset = 0; x.cap_x = 15; x.cap_y = 1.8; x.cap_mode = 2;
        x.sync_speed = 5; x.speeds = S;
        c.push_back(x);
    }
    {
        Case x; // office (presets.hpp) — natural, default cap {15,1.5} out
        x.name = "game_office_natural"; x.mode = "natural"; x.gain = true;
        x.limit = 1.3; x.decay_rate = 0.08; x.motivity = 1.2;
        x.input_offset = 0; x.cap_x = 15; x.cap_y = 1.5; x.cap_mode = 2;
        x.speeds = S;
        c.push_back(x);
    }
    {
        Case x; // precision (presets.hpp) — classic exp 1.5. cap.y mirrors the
        x.name = "game_precision_classic"; x.mode = "classic"; x.gain = true;
        x.acceleration = 0.002; x.exponent_classic = 1.5; x.limit = 1.2;
        x.input_offset = 0; x.cap_x = 24; x.cap_y = 1.2; x.cap_mode = 2;
        x.sync_speed = 5; x.speeds = S;
        c.push_back(x);
    }
    {
        Case x; // disable (presets.hpp) — raw passthrough → noaccel, gain 1 everywhere
        x.name = "game_disable_noaccel"; x.mode = "noaccel"; x.gain = true;
        x.speeds = S;
        c.push_back(x);
    }

    return c;
}

} // namespace oracle