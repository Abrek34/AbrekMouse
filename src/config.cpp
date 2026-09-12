// Linux-only project; glibc keeps fstat()/stat() (and O_PATH semantics) behind
// the __USE_GNU/__USE_XOPEN2K8 feature gates.  M-BUG-14 uses fstat() to copy a
// config's existing permission bits, so expose the declarations explicitly.
#define _GNU_SOURCE 1
#include "config.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <climits>
#include <limits>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <array>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace rawaccel {

// ── Helpers ───────────────────────────────────────────────────────────────────

static accel_mode str_to_mode(const std::string& s) {
    if (s == "classic")     return accel_mode::classic;
    if (s == "power")       return accel_mode::power;
    if (s == "natural")     return accel_mode::natural;
    if (s == "jump")        return accel_mode::jump;
    if (s == "synchronous") return accel_mode::synchronous;
    if (s == "lookup")      return accel_mode::lookup;
    return accel_mode::noaccel;
}

static std::string mode_to_str(accel_mode m) {
    switch (m) {
    case accel_mode::classic:     return "classic";
    case accel_mode::power:       return "power";
    case accel_mode::natural:     return "natural";
    case accel_mode::jump:        return "jump";
    case accel_mode::synchronous: return "synchronous";
    case accel_mode::lookup:      return "lookup";
    default:                      return "noaccel";
    }
}

static cap_mode str_to_cap(const std::string& s) {
    if (s == "io") return cap_mode::io;
    if (s == "in") return cap_mode::in;
    return cap_mode::out;
}

static std::string cap_to_str(cap_mode c) {
    switch (c) {
    case cap_mode::io: return "io";
    case cap_mode::in: return "in";
    default:           return "out";
    }
}

// ── accel_args serialization ──────────────────────────────────────────────────

static json accel_args_to_json(const accel_args& a) {
    json j;
    j["mode"]             = mode_to_str(a.mode);
    j["gain"]             = a.gain;
    j["input_offset"]     = a.input_offset;
    j["output_offset"]    = a.output_offset;
    j["acceleration"]     = a.acceleration;
    j["decay_rate"]       = a.decay_rate;
    j["gamma"]            = a.gamma;
    j["motivity"]         = a.motivity;
    j["exponent_classic"] = a.exponent_classic;
    j["scale"]            = a.scale;
    j["exponent_power"]   = a.exponent_power;
    j["limit"]            = a.limit;
    j["sync_speed"]       = a.sync_speed;
    j["smooth"]           = a.smooth;
    j["cap"]              = { a.cap.x, a.cap.y };
    j["cap_mode"]         = cap_to_str(a.cap_mode_val);
    // LUT data — meaningful in lookup mode, but the LOADED user curve must
    // also survive a mode switch: `CLI set-param <p> mode natural` on a lookup
    // profile followed by the automatic save used to drop lut_data (it was
    // serialized only for lookup), silently erasing the curve forever — the
    // next `mode lookup` came back empty.  Serializing it whenever present is
    // safe here: this serializer only ever sees config-file args (profile_to_json
    // callers), never the live device args that the synchronous (GAIN) internal
    // LUT generator writes into (apply_profile mutates device.args, not the
    // config copy the save path serializes).
    if (a.length > 0) {
        json pts = json::array();
        for (int i = 0; i < a.length && i < (int)LUT_RAW_DATA_CAPACITY; i++)
            pts.push_back(a.data[i]);
        j["lut_data"] = pts;
        j["lut_length"] = a.length;
    }
    return j;
}

static accel_args accel_args_from_json(const json& j) {
    accel_args a;
    // B4 (P43): type-guard string fields so malformed JSON (wrong type) yields
    // a default instead of a nlohmann::json::type_error exception.
    if (j.contains("mode") && j["mode"].is_string()) {
        const std::string mode_str = j["mode"].get<std::string>();
        const accel_mode m = str_to_mode(mode_str);
        // str_to_mode() falls back to noaccel for unknown strings — reject
        // anything that isn't one of the seven valid mode names instead of
        // silently falling back to 1:1 passthrough (which would read as
        // "acceleration disabled" with no explanation).
        static const char* VALID_MODES[] = {
            "noaccel", "classic", "power", "natural", "jump",
            "synchronous", "lookup", nullptr };
        bool known = false;
        for (int i = 0; VALID_MODES[i] != nullptr; ++i)
            if (mode_str == VALID_MODES[i]) { known = true; break; }
        if (!known)
            throw std::runtime_error("unknown accel mode: '" + mode_str + "'");
        a.mode = m;
    }
    // O31-C3: accept numeric 0 / 1 for gain as well as booleans — hand-edited
    // config / older exporters emit 0/1, which a lenient `is_number()` read
    // used to accept; a strict is_boolean check since P120 silently dropped
    // such values back to the default.  Anything that is neither bool nor an
    // integral 0/1 keeps the default (never throws).
    if (j.contains("gain")) {
        const auto& g = j["gain"];
        if (g.is_boolean()) a.gain = g.get<bool>();
        else if (g.is_number_integer() && (g.get<long long>() == 0 || g.get<long long>() == 1))
            a.gain = (g.get<long long>() == 1);
    }
    // P120-FAZ2 (A5-02): the 12 numeric accel_args fields are strictly
    // validated.  Administrative decision: a wrong-typed (string/object/array)
    // or non-finite (NaN/Inf, incl. overflow like 1e999) value for ANY of them
    // REJECTS the whole config (load throws → CLI exits 1, file untouched)
    // instead of silently degrading to defaults — a hand-edited or
    // schema-mismatched config must not load half-correct acceleration math.
    // Absence still means "use default" (skip silently).
    auto require_number = [&](const char* key, double& out) {
        if (!j.contains(key)) return;
        const auto& v = j[key];
        if (!v.is_number())
            throw std::runtime_error(std::string("config field '") + key +
                                     "' must be a number, got " + v.type_name());
        double dv = v.get<double>();
        if (!std::isfinite(dv))
            throw std::runtime_error(std::string("config field '") + key +
                                     "' must be finite");
        out = dv;
    };
    require_number("input_offset",     a.input_offset);
    require_number("output_offset",    a.output_offset);
    require_number("acceleration",     a.acceleration);
    require_number("decay_rate",       a.decay_rate);
    require_number("gamma",            a.gamma);
    require_number("motivity",         a.motivity);
    require_number("exponent_classic", a.exponent_classic);
    require_number("scale",            a.scale);
    require_number("exponent_power",   a.exponent_power);
    require_number("limit",            a.limit);
    require_number("sync_speed",       a.sync_speed);
    require_number("smooth",           a.smooth);
    if (j.contains("cap")) {
        auto& cap = j["cap"];
        // O3: silently use default on missing element or wrong type instead of throwing.
        // Element type-guards (P99): a hand-edited `"cap": ["a","b"]` must degrade
        // to the default instead of throwing type_error and bricking the whole config.
        if (cap.is_array() && cap.size() >= 2) {
            if (cap[0].is_number()) a.cap.x = cap[0].get<double>();
            if (cap[1].is_number()) a.cap.y = cap[1].get<double>();
        }
    }
    if (j.contains("cap_mode") && j["cap_mode"].is_string()) {
        // BUG-NEW-85: whitelist cap_mode just like mode — an unknown/typo
        // string must not silently map to cap_mode::out (which changes the
        // io behavior of classic/power/synchronous and runs a different curve
        // branch).  Symmetric with the mode validation above.
        const std::string cap_str = j["cap_mode"].get<std::string>();
        if (cap_str != "in" && cap_str != "out" && cap_str != "io")
            throw std::runtime_error("unknown cap_mode: '" + cap_str + "'");
        a.cap_mode_val = str_to_cap(cap_str);
    }
    if (j.contains("lut_data") && j["lut_data"].is_array() &&
        j.contains("lut_length")) {
        // R6-1: no mode gate (was `&& a.mode == accel_mode::lookup`) so a
        // stored curve survives the lookup→other→lookup round-trip just like
        // the writer above.  The reader only ever consumes JSON-serialized
        // user state (never live device args), so loading a non-lookup profile
        // that carries a curve is safe — the engine simply ignores it until
        // the mode is lookup again.
        // BUG-5: nlohmann::json::get<int>() invokes UB when the JSON value
        // doesn't fit in `int` (libFuzzer + UBSan caught this with payloads
        // like `"lut_length": 1e26`).  Read as a double first, range-clamp,
        // then cast — the cast is now defined.
        double raw = j["lut_length"].is_number()
                     ? j["lut_length"].get<double>() : 0.0;
        if (!std::isfinite(raw) || raw < 0) raw = 0;
        if (raw > (double)LUT_RAW_DATA_CAPACITY) raw = LUT_RAW_DATA_CAPACITY;
        a.length = static_cast<int>(raw);
        auto& pts = j["lut_data"];
        // B5 (P43): do the min() in size_t so a pathological pts.size() cannot
        // overflow via the `int` narrowing cast (theoretical UB).
        size_t n = std::min(static_cast<size_t>(a.length), pts.size());
        for (size_t i = 0; i < n; i++) {
            // Same defence on the LUT entries themselves.
            double v = pts[i].is_number() ? pts[i].get<double>() : 0.0;
            if (!std::isfinite(v)) v = 0;
            // double → float overflow yields ±Inf which corrupts the LUT
            // binary search.  Clamp to the float-representable range first.
            constexpr double FLT_HI = static_cast<double>(std::numeric_limits<float>::max());
            if (v > FLT_HI)  v = FLT_HI;
            if (v < -FLT_HI) v = -FLT_HI;
            a.data[i] = static_cast<float>(v);
        }
        // O31-C2: a hand-edited lut_length may claim MORE pairs than lut_data
        // actually holds (e.g. lut_length 514 with 2 floats).  Trailing slots
        // stay at 0, so sort_lut_data lifts the dead (0,0) segment to the
        // front and lookup() snaps every speed to gain 0 — a dead cursor that
        // config.test_accel's round-trip would happily persist.  Pin the
        // length to the truly-parsed pair count (even; odd tails drop).
        a.length = static_cast<int>((n / 2) * 2);
    }
    return a;
}

// ── profile serialization ─────────────────────────────────────────────────────

static json profile_to_json_obj(const profile& p) {
    json j;
    j["name"]              = std::string(p.name);
    j["raw_passthrough"]   = p.raw_passthrough;
    j["domain_weights"]    = { p.domain_weights.x, p.domain_weights.y };
    j["range_weights"]     = { p.range_weights.x,  p.range_weights.y  };
    j["accel_x"]           = accel_args_to_json(p.accel_x);
    j["accel_y"]           = accel_args_to_json(p.accel_y);
    j["output_dpi"]        = p.output_dpi;
    j["yx_output_dpi_ratio"] = p.yx_output_dpi_ratio;
    j["degrees_rotation"]  = p.degrees_rotation;
    j["degrees_snap"]      = p.degrees_snap;
    j["speed_min"]         = p.speed_min;
    j["speed_max"]         = p.speed_max;
    j["lr_output_dpi_ratio"] = p.lr_output_dpi_ratio;
    j["ud_output_dpi_ratio"] = p.ud_output_dpi_ratio;

    // speed_processor_args
    auto& sp = p.speed_processor_args;
    j["speed_processor"] = {
        {"whole",                       sp.whole},
        {"lp_norm",                     sp.lp_norm},
        {"input_speed_smooth_halflife",  sp.input_speed_smooth_halflife},
        {"scale_smooth_halflife",        sp.scale_smooth_halflife},
        {"output_speed_smooth_halflife", sp.output_speed_smooth_halflife},
    };
    return j;
}

static profile profile_from_json_obj(const json& j) {
    profile p;

    if (j.contains("name") && j["name"].is_string()) {
        auto s = j["name"].get<std::string>();
        // CFG-6: the CLI/P83 contract allows up to MAX_NAME_LEN (256) chars
        // and the backing buffer is MAX_NAME_LEN+1, so the full name survives
        // the hot-path copy — no silent 256→255 truncation on reload.
        std::strncpy(p.name, s.c_str(), MAX_NAME_LEN);
        // strncpy doesn't write a null terminator when src ≥ N.  Default
        // construction zero-fills the array, but defend in depth in case
        // the caller passes a previously-populated profile.
        p.name[MAX_NAME_LEN] = '\0';
    }
    // P99: type-guard the remaining scalar fields so a single wrong-typed
    // value (hand-edit, future schema drift) degrades to the default instead
    // of throwing type_error and making the entire config unloadable (which
    // would also leave the daemon falling back to defaults).
    // O31-C3: accept numeric 0 / 1 for raw_passthrough as well as booleans
    // (symmetric with gain above) — never throws on a wrong type.
    if (j.contains("raw_passthrough")) {
        const auto& rp = j["raw_passthrough"];
        if (rp.is_boolean()) p.raw_passthrough = rp.get<bool>();
        else if (rp.is_number_integer() && (rp.get<long long>() == 0 || rp.get<long long>() == 1))
            p.raw_passthrough = (rp.get<long long>() == 1);
    }
    if (j.contains("domain_weights")) {
        auto& dw = j["domain_weights"];
        if (dw.is_array() && dw.size() >= 2) {
            if (dw[0].is_number()) p.domain_weights.x = dw[0].get<double>();
            if (dw[1].is_number()) p.domain_weights.y = dw[1].get<double>();
        }
    }
    if (j.contains("range_weights")) {
        auto& rw = j["range_weights"];
        if (rw.is_array() && rw.size() >= 2) {
            if (rw[0].is_number()) p.range_weights.x = rw[0].get<double>();
            if (rw[1].is_number()) p.range_weights.y = rw[1].get<double>();
        }
    }
    if (j.contains("accel_x") && j["accel_x"].is_object())
        p.accel_x = accel_args_from_json(j["accel_x"]);
    if (j.contains("accel_y") && j["accel_y"].is_object())
        p.accel_y = accel_args_from_json(j["accel_y"]);
    if (j.contains("output_dpi") && j["output_dpi"].is_number())
        p.output_dpi = j["output_dpi"].get<double>();
    if (j.contains("yx_output_dpi_ratio") && j["yx_output_dpi_ratio"].is_number())
        p.yx_output_dpi_ratio = j["yx_output_dpi_ratio"].get<double>();
    if (j.contains("degrees_rotation") && j["degrees_rotation"].is_number())
        p.degrees_rotation = j["degrees_rotation"].get<double>();
    if (j.contains("degrees_snap") && j["degrees_snap"].is_number())
        p.degrees_snap = j["degrees_snap"].get<double>();
    if (j.contains("speed_min") && j["speed_min"].is_number())
        p.speed_min = j["speed_min"].get<double>();
    if (j.contains("speed_max") && j["speed_max"].is_number())
        p.speed_max = j["speed_max"].get<double>();
    if (j.contains("lr_output_dpi_ratio") && j["lr_output_dpi_ratio"].is_number())
        p.lr_output_dpi_ratio = j["lr_output_dpi_ratio"].get<double>();
    if (j.contains("ud_output_dpi_ratio") && j["ud_output_dpi_ratio"].is_number())
        p.ud_output_dpi_ratio = j["ud_output_dpi_ratio"].get<double>();

    if (j.contains("speed_processor") && j["speed_processor"].is_object()) {
        auto& sp_j = j["speed_processor"];
        auto& sp   = p.speed_processor_args;
        if (sp_j.contains("whole") && sp_j["whole"].is_boolean())
            sp.whole = sp_j["whole"].get<bool>();
        if (sp_j.contains("lp_norm") && sp_j["lp_norm"].is_number())
            sp.lp_norm = sp_j["lp_norm"].get<double>();
        if (sp_j.contains("input_speed_smooth_halflife") && sp_j["input_speed_smooth_halflife"].is_number())
            sp.input_speed_smooth_halflife = sp_j["input_speed_smooth_halflife"].get<double>();
        if (sp_j.contains("scale_smooth_halflife") && sp_j["scale_smooth_halflife"].is_number())
            sp.scale_smooth_halflife = sp_j["scale_smooth_halflife"].get<double>();
        if (sp_j.contains("output_speed_smooth_halflife") && sp_j["output_speed_smooth_halflife"].is_number())
            sp.output_speed_smooth_halflife = sp_j["output_speed_smooth_halflife"].get<double>();
    }

    return p;
}

// ── device_profile serialization ─────────────────────────────────────────────

static json device_profile_to_json(const device_profile& dp) {
    json j;
    j["name"]       = dp.name;
    j["device_id"]  = dp.device_id;
    if (!dp.match_app.empty())
        j["match_app"] = dp.match_app;
    j["dpi"]        = dp.dev_cfg.dpi;
    j["polling_rate"] = dp.dev_cfg.polling_rate;
    j["disable"]    = dp.dev_cfg.disable;
    j["profile"]    = profile_to_json_obj(dp.prof);
    return j;
}

/// Clamp device_config fields to safe ranges.
/// Called after JSON deserialization to prevent bad values reaching the daemon.
static void sanitize_device_config(device_config& dc) {
    // DPI: 1–32000 (modern sensors go up to 32 000)
    if (dc.dpi < 1)     dc.dpi = 1;
    if (dc.dpi > 32000) dc.dpi = 32000;
    // Polling rate: 125–8000 Hz
    if (dc.polling_rate < POLL_RATE_MIN) dc.polling_rate = POLL_RATE_MIN;
    if (dc.polling_rate > POLL_RATE_MAX) dc.polling_rate = POLL_RATE_MAX;
}

/// Sort LUT data in-place by speed (ascending) so binary search in lookup::operator()
/// produces correct results.  GUI already sorts via lut_set_points(), but JSON files
/// edited by hand or generated by external tools may have unsorted data.
static void sort_lut_data(accel_args& a) {
    const int n = a.length / 2;
    if (n <= 1) return;
    // Simple insertion sort — n is small (max 257) and avoids heap allocation.
    for (int i = 1; i < n; i++) {
        const size_t si = static_cast<size_t>(i);
        const float kx = a.data[si * 2];
        const float ky = a.data[si * 2 + 1];
        int j = i - 1;
        while (j >= 0 && a.data[static_cast<size_t>(j) * 2] > kx) {
            const size_t sj = static_cast<size_t>(j);
            a.data[(sj + 1) * 2]     = a.data[sj * 2];
            a.data[(sj + 1) * 2 + 1] = a.data[sj * 2 + 1];
            j--;
        }
        const int insert = j + 1;
        a.data[static_cast<size_t>(insert) * 2]     = kx;
        a.data[static_cast<size_t>(insert) * 2 + 1] = ky;
    }
}

/// Clamp accel_args fields to safe ranges.
/// Prevents NaN/Inf from pow(negative, non-integer) and similar edge cases
/// when values come from hand-edited JSON or external tools.
/// Replace non-finite (NaN / Inf) with a safe default.
static inline double finite_or(double v, double def) {
    return std::isfinite(v) ? v : def;
}

static void sanitize_accel_args(accel_args& a) {
    // B2 (P43): LUT length must stay within the raw-data buffer.  The JSON
    // path clamps already, but programmatically-built profiles (GUI/CLI and
    // sanitize_device_profile()) can carry any int here, and sort_lut_data()
    // iterates length/2 points as a.data[i*2]/[i*2+1] — out-of-range would
    // be buffer UB.
    if (a.length < 0) a.length = 0;
    if (static_cast<size_t>(a.length) > LUT_RAW_DATA_CAPACITY) a.length = LUT_RAW_DATA_CAPACITY;

    // NaN / Inf guard: NaN silently passes comparison guards (NaN < 0 → false),
    // so we must replace non-finite values with safe defaults first.
    a.acceleration    = finite_or(a.acceleration, 0);
    a.scale           = finite_or(a.scale, 0);
    a.decay_rate      = finite_or(a.decay_rate, 0);
    a.exponent_classic = finite_or(a.exponent_classic, 2);
    // T15 — GUI parity: gtk_spin_button exposes exponent_classic over [1,10]
    // (gui/ui_builder.inl). Clamping here keeps manually-edited JSON (CLI)
    // from drifting below 1, where the reference curve amplifies into garbage
    // (exp=0.5, low speed → gain≈448). exp==1 still takes the documented
    // "linear path" in the classic port, so GUI-reachable values are unchanged.
    if (a.exponent_classic < 1.0) a.exponent_classic = 1.0;
    if (a.exponent_classic > 10.0) a.exponent_classic = 10.0;
    a.exponent_power  = finite_or(a.exponent_power, 1);
    a.input_offset    = finite_or(a.input_offset, 0);
    a.output_offset   = finite_or(a.output_offset, 0);
    a.limit           = finite_or(a.limit, 0);
    a.sync_speed      = finite_or(a.sync_speed, 1);
    a.smooth          = finite_or(a.smooth, 0);
    a.motivity        = finite_or(a.motivity, 0);
    a.gamma           = finite_or(a.gamma, 0);
    a.cap.x           = finite_or(a.cap.x, 0);
    a.cap.y           = finite_or(a.cap.y, 0);

    // acceleration: used as pow(acceleration, exp-1) in classic mode.
    //   Negative acceleration with integer exponent is a legitimate deceleration feature.
    //   Negative + non-integer exponent produces NaN, but the classic constructor and
    //   motion_math NaN guard handle this downstream — don't clamp here.
    // scale: used as pow(scale * x, exp) in power mode.
    //   Negative scale * positive x → negative base → NaN with non-integer exp.
    //   Zero → pow(0,x)=0 → gain≡0 (dead cursor) with no cap, or a silent
    //   1.5× constant boost with cap_mode=out (cap_x = gain_inverse(...,0) = 0).
    //   MATH-2: floor to the same scale floor as the GUI spin (0.01), matching
    //   the exponent_power floor pattern — a zero curve is never reachable.
    if (a.scale <= 0) a.scale = 0.01;
    // decay_rate: natural mode divides by limit to get internal accel coefficient.
    //   Negative → exp(+large) → diverging gain.  Clamp to >= 0.
    if (a.decay_rate < 0) a.decay_rate = 0;
    // exponent_power: power mode constructor clamps to 1e-4, but sanitize early.
    //   Zero causes division-by-zero in gain_inverse (1/n).
    if (a.exponent_power < 1e-4) a.exponent_power = 1e-4;
    // input_offset, output_offset: negative offsets are meaningless (speed is always >= 0).
    if (a.input_offset < 0) a.input_offset = 0;
    if (a.output_offset < 0) a.output_offset = 0;
    // limit: natural/jump subtract 1 → limit < 1 means deceleration.
    //   Constructors already clamp to max(0, limit-1), but values < 0 are nonsensical.
    if (a.limit < 0) a.limit = 0;
    // sync_speed: synchronous/jump mode midpoint.  Zero → division-by-zero.
    if (a.sync_speed < 1e-4) a.sync_speed = 1e-4;
    // smooth: jump mode steepness factor.  Negative just inverts the sigmoid — harmless
    //   but confusing; clamp to >= 0 for UI consistency.
    if (a.smooth < 0) a.smooth = 0;
    // motivity, gamma: not currently used by any algorithm but stored in config.
    //   Prevent negative values for forward-compatibility.
    if (a.motivity < 0) a.motivity = 0;
    if (a.gamma < 0) a.gamma = 0;
    // cap values: negative caps are meaningless.
    if (a.cap.x < 0) a.cap.x = 0;
    if (a.cap.y < 0) a.cap.y = 0;
    // BUG-7 fix: classic GAIN/io requires cap.x >= input_offset, otherwise
    // base_fn(cap_x) gets a non-positive base and the accelerated tail silently
    // collapses (accel-classic.hpp init_gain::io).  Clamp breakpoint up.
    if (a.cap.x < a.input_offset) a.cap.x = a.input_offset;

    // P120-FAZ2 (Aj8 BUG-3): cap the gain-driving fields at the GUI gauge
    // maxima (SCALE_MAX / EXP_POWER_MAX / CAP_X_MAX / CAP_Y_MAX /
    // OUTPUT_OFFSET_MAX in config.hpp).  Previously sanitize had NO upper
    // bound, so a hand-edited JSON or CLI value (scale=1e6, exponent_power=1e9)
    // could push pow(scale·x, n) gains astronomically beyond anything the GUI
    // gauge can produce.  The maxima are exactly the R15 boundary round-trip
    // values, so in-envelope configs are unchanged and boundary values survive
    // the load->save round-trip.
    if (a.scale           > SCALE_MAX)         a.scale          = SCALE_MAX;
    if (a.exponent_power  > EXP_POWER_MAX)     a.exponent_power = EXP_POWER_MAX;
    // Clamp input_offset first so the cap.x >= input_offset lower bound can
    // never be violated by the CAP_X_MAX upper bound below (BUG re-break):
    if (a.input_offset    > CAP_X_MAX)         a.input_offset   = CAP_X_MAX;
    if (a.cap.x           > CAP_X_MAX)         a.cap.x          = CAP_X_MAX;
    if (a.cap.y           > CAP_Y_MAX)         a.cap.y          = CAP_Y_MAX;
    if (a.output_offset   > OUTPUT_OFFSET_MAX) a.output_offset  = OUTPUT_OFFSET_MAX;
}

/// Clamp profile fields to safe ranges.
static void sanitize_profile(profile& p) {
    // NaN / Inf guard on all double fields — NaN silently passes < / > comparisons.
    p.degrees_rotation     = finite_or(p.degrees_rotation, 0);
    p.degrees_snap         = finite_or(p.degrees_snap, 0);
    p.speed_min            = finite_or(p.speed_min, 0);
    p.speed_max            = finite_or(p.speed_max, 0);
    p.lr_output_dpi_ratio  = finite_or(p.lr_output_dpi_ratio, 1);
    p.ud_output_dpi_ratio  = finite_or(p.ud_output_dpi_ratio, 1);
    p.yx_output_dpi_ratio  = finite_or(p.yx_output_dpi_ratio, 1);
    p.domain_weights.x     = finite_or(p.domain_weights.x, 1);
    p.domain_weights.y     = finite_or(p.domain_weights.y, 1);
    p.range_weights.x      = finite_or(p.range_weights.x, 1);
    p.range_weights.y      = finite_or(p.range_weights.y, 1);
    p.output_dpi           = finite_or(p.output_dpi, NORMALIZED_DPI);
    p.speed_processor_args.lp_norm = finite_or(p.speed_processor_args.lp_norm, 2);
    p.speed_processor_args.input_speed_smooth_halflife =
        finite_or(p.speed_processor_args.input_speed_smooth_halflife, 0);
    p.speed_processor_args.scale_smooth_halflife =
        finite_or(p.speed_processor_args.scale_smooth_halflife, 0);
    p.speed_processor_args.output_speed_smooth_halflife =
        finite_or(p.speed_processor_args.output_speed_smooth_halflife, 0);

    // Rotation: normalize into [0, 360) preserving direction.
    // Using fabs() would map -45° → 45° (wrong direction); the correct
    // mathematical equivalent of -45° is +315°.  fmod() preserves sign,
    // so we add 360 to push negative residues into the positive half.
    p.degrees_rotation = std::fmod(p.degrees_rotation, 360.0);
    if (p.degrees_rotation < 0)         p.degrees_rotation += 360.0;
    if (p.degrees_rotation == 0.0)      p.degrees_rotation  = 0.0;     // clear -0.0 sign
    if (p.degrees_rotation >= 360.0)    p.degrees_rotation  = 0.0;     // FP rounding edge
    // Snap: 0–45 degrees (meaningful range)
    if (p.degrees_snap < 0)  p.degrees_snap = 0;
    if (p.degrees_snap > 45) p.degrees_snap = 45;
    // Output DPI: 0 disables output-DPI normalization (the modifier's
    // `args.output_dpi > 0` guard then skips dpi_adjustment → 1:1 counts).
    // Previously 0 was clamped to 1, which yielded dpi_adjustment =
    // (1/1000)·dpi_factor ≈ 0.00125 at 800 dpi — a silent near-dead cursor
    // (CFG-1).  Negatives are nonsense → same "no normalization" sentinel.
    if (p.output_dpi < 0)               p.output_dpi = 0;
    if (p.output_dpi > 0 && p.output_dpi < 1) p.output_dpi = 1;
    if (p.output_dpi > 32000)           p.output_dpi = 32000;
    // DPI ratios: 0.01–100
    if (p.lr_output_dpi_ratio < 0.01) p.lr_output_dpi_ratio = 0.01;
    if (p.lr_output_dpi_ratio > 100)  p.lr_output_dpi_ratio = 100;
    if (p.ud_output_dpi_ratio < 0.01) p.ud_output_dpi_ratio = 0.01;
    if (p.ud_output_dpi_ratio > 100)  p.ud_output_dpi_ratio = 100;
    if (p.yx_output_dpi_ratio < 0.01) p.yx_output_dpi_ratio = 0.01;
    if (p.yx_output_dpi_ratio > 100)  p.yx_output_dpi_ratio = 100;
    // speed_min / speed_max: non-negative; max >= min if both nonzero
    if (p.speed_min < 0) p.speed_min = 0;
    if (p.speed_max < 0) p.speed_max = 0;
    if (p.speed_max > 0 && p.speed_max < p.speed_min)
        p.speed_max = p.speed_min;
    // lp_norm: must be > 0
    if (p.speed_processor_args.lp_norm <= 0) p.speed_processor_args.lp_norm = 2;
    // Smooth halflifes: negative has no meaning (> 0 check enables smoothing).
    // Upper bound (P155): pow(0.5, 1/hl) rounds to exactly 1.0 for hl ≥ ~1.4e16
    // (1/hl below half-ULP in double), which makes cutOffCoefficient = 1 and every
    // EMA increment twc/tcc = 0 — the smoother silently returns its initial 0
    // forever and the mouse stops responding.  SM-7: clamping to SMOOTH_HALFLIFE_MAX
    // (10 s) still removes the hazard while leaving a usable ceiling — anything
    // past ~10 s is indistinguishable from "the estimate never moves" (a
    // per-8ms EMA step of e^-0.0008 ≈ 0.9992) and pins the smoothed speed at its
    // initial 0, a dead-mouse symptom.  The CLI set-param domain uses the same
    // constant so every in-domain value survives sanitize byte-correct (P107).
    constexpr double kMaxSmoothHalflifeMs = SMOOTH_HALFLIFE_MAX;
    if (p.speed_processor_args.input_speed_smooth_halflife < 0)
        p.speed_processor_args.input_speed_smooth_halflife = 0;
    if (p.speed_processor_args.input_speed_smooth_halflife > kMaxSmoothHalflifeMs)
        p.speed_processor_args.input_speed_smooth_halflife = kMaxSmoothHalflifeMs;
    if (p.speed_processor_args.scale_smooth_halflife < 0)
        p.speed_processor_args.scale_smooth_halflife = 0;
    if (p.speed_processor_args.scale_smooth_halflife > kMaxSmoothHalflifeMs)
        p.speed_processor_args.scale_smooth_halflife = kMaxSmoothHalflifeMs;
    if (p.speed_processor_args.output_speed_smooth_halflife < 0)
        p.speed_processor_args.output_speed_smooth_halflife = 0;
    if (p.speed_processor_args.output_speed_smooth_halflife > kMaxSmoothHalflifeMs)
        p.speed_processor_args.output_speed_smooth_halflife = kMaxSmoothHalflifeMs;
    // Domain/range weights: negative values invert axes — confusing and unintended.
    // Clamp to a small positive minimum so acceleration math stays well-defined.
    // Upper bound (P86): absurd magnitudes (e.g. 1e300) could push accel-LUT
    // lookups past representable indices; clamp to a safe ceiling (defense-in-depth
    // on top of the synchronous gain_apply() clamp). 1e6 × speed stays far inside
    // double range and far beyond any real-world domain/range weight usage.
    if (p.domain_weights.x < 0) p.domain_weights.x = 0;
    if (p.domain_weights.y < 0) p.domain_weights.y = 0;
    if (p.range_weights.x < 0) p.range_weights.x = 0;
    if (p.range_weights.y < 0) p.range_weights.y = 0;
    if (p.domain_weights.x > 1e6) p.domain_weights.x = 1e6;
    if (p.domain_weights.y > 1e6) p.domain_weights.y = 1e6;
    if (p.range_weights.x > 1e6) p.range_weights.x = 1e6;
    if (p.range_weights.y > 1e6) p.range_weights.y = 1e6;
    // Acceleration arguments: clamp per-algorithm fields to safe ranges
    sanitize_accel_args(p.accel_x);
    sanitize_accel_args(p.accel_y);
    // LUT data: sort by speed so binary search in lookup::operator() works correctly
    sort_lut_data(p.accel_x);
    sort_lut_data(p.accel_y);
}

/// Safely extract an integer from a JSON node.  nlohmann's `get<int>()`
/// triggers UndefinedBehaviorSanitizer when the underlying number doesn't
/// fit in `int` (libFuzzer caught a 1e26 input).  Reading via double, then
/// clamping, then casting avoids that UB regardless of the input.
static int json_get_int_safe(const json& v, int fallback) {
    if (!v.is_number()) return fallback;
    double d = v.get<double>();
    if (!std::isfinite(d)) return fallback;
    if (d < (double)INT_MIN) return INT_MIN;
    // BUG-CRIT-2: (double)INT_MAX == 2147483648.0 — an input of exactly
    // 2147483648.0 passes the old `> (double)INT_MAX` test and makes the
    // static_cast<int> below UB.  Use >= so 2147483648.0* clamps to INT_MAX.
    if (d >= (double)INT_MAX) return INT_MAX;
    return static_cast<int>(d);
}

/// B4 (P43): read a string field with a type guard and a hard length cap so
/// malformed input (number/array/null) or absurd lengths cannot throw
/// type_error or balloon memory.
static std::string json_get_string_limited(const json& v,
                                           const std::string& fallback,
                                           size_t maxlen) {
    if (!v.is_string()) return fallback;
    auto s = v.get<std::string>();
    if (s.size() > maxlen) s.resize(maxlen);
    return s;
}

static device_profile device_profile_from_json(const json& j) {
    device_profile dp;
    // B4 (P43): type guard + length cap for the free-form string fields.
    // CFG-6: dp.name cap == top-level profile.name copy cap == MAX_NAME_LEN,
    // so a 256-char name round-trips identically through both (a single
    // truncation length for the same JSON, not "256 here vs 255 there").
    constexpr size_t MAX_DP_DEVICE_ID = 256;
    if (j.contains("name"))       dp.name      = json_get_string_limited(j["name"], "", MAX_NAME_LEN);
    if (j.contains("device_id"))  dp.device_id = json_get_string_limited(j["device_id"], "", MAX_DP_DEVICE_ID);
    // C29-N4: the UI displays empty device_id as "(all)" — accept the same
    // written form (and common aliases) so a hand-edited JSON/profile can't
    // silently become a dead literal ID that matches no device.
    if (dp.device_id == "(all)" || dp.device_id == "all" || dp.device_id == "*")
        dp.device_id.clear();
    if (j.contains("match_app"))  dp.match_app = json_get_string_limited(j["match_app"], "", 128);
    if (j.contains("dpi"))          dp.dev_cfg.dpi   = json_get_int_safe(j["dpi"], 800);
    if (j.contains("polling_rate")) dp.dev_cfg.polling_rate = json_get_int_safe(j["polling_rate"], 1000);
    if (j.contains("disable"))      dp.dev_cfg.disable = j["disable"].is_boolean()
                                                         ? j["disable"].get<bool>() : false;
    if (j.contains("profile") && j["profile"].is_object())
        dp.prof = profile_from_json_obj(j["profile"]);
    // Clamp to safe ranges after loading
    sanitize_device_config(dp.dev_cfg);
    sanitize_profile(dp.prof);
    return dp;
}

//─── Private helpers ───────────────────────────────────────────────────────────

/// Older-than comparison ("0.0.0"-style semver) used to decide whether a stored
/// version needs migration / downgrade protection.  Forward-declared for
/// app_config_to_json_obj() (defined further down).
static bool version_lt(const std::string& lhs, const std::string& rhs);

// ── Public API ────────────────────────────────────────────────────────────────

/// Shared app_config ↔ JSON object conversion.  Both the file loader and the
/// IPC config-push RPC go through these so the two never drift apart.
static app_config app_config_from_json_obj(const json& j) {
    app_config cfg;

    // P43-BF1 (critical): read the schema version back from JSON. Without this,
    // cfg.version stays empty on every load, so migrate_config() re-runs
    // migrate_lookup_gain() on each load->save round trip, re-scaling stored
    // lookup+gain points by x every time (200 -> 20000 -> 2M -> ...).
    if (j.contains("version") && j["version"].is_string())
        cfg.version = j["version"].get<std::string>();

    if (j.contains("active_profile"))
        cfg.active_profile = json_get_string_limited(j["active_profile"], "default", MAX_NAME_LEN);
    if (j.contains("use_raw_input") && j["use_raw_input"].is_boolean())
        cfg.use_raw_input = j["use_raw_input"].get<bool>();

    if (j.contains("profiles") && j["profiles"].is_array()) {
        // SEC-9: a hostile IPC client can push an arbitrarily large "profiles"
        // array (easy to craft, each entry ~1 KB) → unbounded memory growth in
        // a root daemon.  Cap the count at MAX_PROFILES; extra entries are
        // silently dropped (sanitize already tolerates malformed entries).
        for (auto& pj : j["profiles"]) {
            if (!pj.is_object()) continue;
            if (cfg.profiles.size() >= MAX_PROFILES) break;
            cfg.profiles.push_back(device_profile_from_json(pj));
        }
    }
    return cfg;
}

static json app_config_to_json_obj(const app_config& cfg) {
    json j;
    // CFG-5: never silently DOWNGRADE a config written by a NEWER binary.
    // save_config stamps the current schema version, so a config that arrived
    // as version 2.0 was rewritten as 1.1.0 on the first mutation and every
    // 2.0-era field was dropped — forward data loss.  Stamp RAWACCEL_VERSION
    // only when the stored version is absent or older; a newer version is
    // preserved verbatim (unknown keys are still dropped, but the version no
    // longer lies and the file does not regress).
    j["version"] = (cfg.version.empty() ||
                    version_lt(cfg.version, RAWACCEL_VERSION))
                   ? RAWACCEL_VERSION
                   : cfg.version;
    j["active_profile"] = cfg.active_profile;
    j["use_raw_input"]  = cfg.use_raw_input;
    j["profiles"]       = json::array();
    for (auto& dp : cfg.profiles)
        j["profiles"].push_back(device_profile_to_json(dp));
    return j;
}

std::string app_config_to_json(const app_config& cfg) {
    return app_config_to_json_obj(cfg).dump();
}

app_config app_config_from_json(const std::string& json_str) {
    // device_profile_from_json already sanitizes each profile on the way in.
    app_config cfg = app_config_from_json_obj(json::parse(json_str));
    migrate_config(cfg);
    return cfg;
}

app_config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open config file: " + path);
    app_config cfg = app_config_from_json_obj(json::parse(f));
    migrate_config(cfg);
    return cfg;
}

void save_config(const app_config& cfg, const std::string& arg_path) {
    // O31-C5: an atomic tmp+rename overwrite of a SYMLINKED config path would
    // replace the symlink itself with a regular file, silently detaching the
    // user's `~/.config/rawaccel/settings.json -> /etc/rawaccel/settings.json`
    // link.  Resolve any symlink to its real target up front (the rename and
    // the .bak hard-link then operate on the target inode, and the symlink
    // keeps working).  A nonexistent or dangling link just keeps arg_path —
    // canoncal() cannot resolve it, so the first save creates a real file.
    std::string path = arg_path;
    {
        std::error_code ec_canon;
        fs::path canon = fs::canonical(arg_path, ec_canon);
        if (!ec_canon) path = canon.string();
    }
    fs::path parent_path = fs::path(path).parent_path();
    if (!parent_path.empty())
        fs::create_directories(parent_path);

    json j = app_config_to_json_obj(cfg);

    // Write to a temp file first, then rename — atomic on Linux (same filesystem).
    // This prevents the daemon from reading a half-written/truncated JSON.
    //
    // BUG-13: ofstream::flush() only pushes the userspace buffer to the kernel
    // page cache.  rename() is atomic with respect to other readers on the same
    // FS, but on a power loss the new directory entry may point at a file
    // whose pages were never committed → user finds an empty file after reboot.
    // Cure: fsync() the file before rename, then fsync() the parent directory
    // so the rename itself is durable.
    // B3 (P43): deterministic `path + ".tmp"` allowed two races — (a) an
    // attacker who can write the config directory could plant a symlink at
    // that name and have the daemon (usually root) O_TRUNC/write through it;
    // (b) two concurrent savers clobbered each other's temp file.  Use a
    // process-scoped name plus O_NOFOLLOW|O_EXCL so we never follow a link
    // and never touch a file we did not just create.
    std::string tmp_path = path + "." + std::to_string(::getpid()) + ".tmp";
    {
        std::string content = j.dump(4) + "\n";
        // M-BUG-14: a blanket 0644 on the temp file silently widened an
        // existing 0600 config to world-readable after the rename.  Preserve
        // the current file's permission bits when present; fall back to 0600
        // (settings files contain no secrets, but least-privilege is the
        // safer default) for first-time saves.
        //
        // Note: uses open(O_PATH)+fstat rather than the POSIX stat() call —
        // unistd.h's forward-declared `struct stat` tag makes the plain
        // `::stat(...)` expression unwieldy, and O_PATH avoids needing read
        // permission on the target.  O_NOFOLLOW keeps the B3 symlink rule:
        // we only ever copy the mode of a path we could otherwise write.
        struct stat pst = {};
        mode_t mode = 0600;
        int pfd = ::open(path.c_str(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
        if (pfd >= 0) {
            if (::fstat(pfd, &pst) == 0) mode = pst.st_mode & 0777;
            ::close(pfd);
        }
        int fd = ::open(tmp_path.c_str(),
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
        if (fd < 0 && errno == EEXIST) {
            // Stale temp from an earlier aborted save in this process — it is
            // ours (pid-suffixed) and guaranteed non-symlink only if unlinked
            // right here; retry once before giving up.
            ::unlink(tmp_path.c_str());
            fd = ::open(tmp_path.c_str(),
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
        }
        if (fd < 0)
            throw std::runtime_error("Cannot write temp config: " + tmp_path +
                                     " (" + std::strerror(errno) + ")");
        const char* p = content.c_str();
        size_t left = content.size();
        while (left > 0) {
            ssize_t w = ::write(fd, p, left);
            if (w < 0) {
                if (errno == EINTR) continue;
                int err = errno;
                ::close(fd); ::unlink(tmp_path.c_str());
                throw std::runtime_error("Write error on temp config: " +
                                         tmp_path + " (" + std::strerror(err) + ")");
            }
            p += w; left -= (size_t)w;
        }
        if (::fsync(fd) != 0) {
            int err = errno;
            ::close(fd); ::unlink(tmp_path.c_str());
            throw std::runtime_error("fsync(temp config) failed: " +
                                     tmp_path + " (" + std::strerror(err) + ")");
        }
        ::close(fd);
    }

    // Preserve one generation of config history without ever removing the
    // live pathname.  The old implementation renamed `path` to `.bak` first,
    // then renamed the new temporary file into `path`.  That creates a small
    // but real ENOENT window: a daemon reload or a GUI/CLI read in that window
    // sees a missing configuration and can fall back to defaults.
    //
    // A hard link is an instantaneous snapshot of the old inode on the same
    // filesystem.  Renaming that private link over `.bak` also replaces a
    // pre-existing (including symlink) backup entry without following it.
    // The live `path` remains readable until the final atomic replacement.
    {
        std::error_code ec_bak;
        if (fs::exists(path, ec_bak) && !ec_bak) {
            std::string bak_path = path + ".bak";
            std::string bak_tmp_path = path + "." + std::to_string(::getpid()) + ".bak.tmp";
            fs::create_hard_link(path, bak_tmp_path, ec_bak);
            if (ec_bak && ec_bak == std::errc::file_exists) {
                // A stale private backup from an interrupted earlier save in
                // this process is safe to replace; it is never the live path.
                std::error_code ec_remove;
                fs::remove(bak_tmp_path, ec_remove);
                ec_bak.clear();
                fs::create_hard_link(path, bak_tmp_path, ec_bak);
            }
            if (!ec_bak) {
                fs::rename(bak_tmp_path, bak_path, ec_bak);
                if (ec_bak) {
                    std::error_code ec_remove;
                    fs::remove(bak_tmp_path, ec_remove);
                }
            }
            // Backup creation is best-effort: failure must not prevent a
            // successfully fsynced new config from replacing the old one.
        }
    }

    std::error_code ec;
    fs::rename(tmp_path, path, ec);
    if (ec) {
        // The temporary file is created beside the target, so a cross-device
        // rename is not a valid recovery case.  Never overwrite the live file
        // with copy_file(): readers could observe a truncated or partial JSON.
        std::error_code ec_rm;
        fs::remove(tmp_path, ec_rm);
        throw std::runtime_error("Cannot replace config file: " + path +
                                 " (" + ec.message() + ")");
    }

    // BUG-13 (cont.): fsync the parent directory so the rename is durable.
    // Without this, a power loss between rename() and a kernel writeback
    // could leave the directory entry pointing nowhere.  Best-effort —
    // some filesystems (e.g. NFS) may not honour directory fsync.
    {
        std::string parent = fs::path(path).parent_path().string();
        if (parent.empty()) parent = ".";
        int dfd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dfd >= 0) {
            (void)::fsync(dfd);
            ::close(dfd);
        }
    }
}

std::string profile_to_json(const device_profile& p) {
    // Compact (single-line) dump: the CLI export format writes one JSON object
    // per line so that the line-stream import fallback can split on newlines.
    // Pretty-printed (.dump(4)) multi-line output broke round-trip for
    // multi-profile configs (R3-NEW-1 follow-up, verified by the agent audit).
    return device_profile_to_json(p).dump();
}

device_profile profile_from_json(const std::string& json_str) {
    // device_profile_from_json already calls sanitize_* on load
    return device_profile_from_json(json::parse(json_str));
}

/// R12-IMPLUT: pre-sanitize LUT size validation for importers (GUI; CLI has its
/// own copy at cli/main.cpp).  The parsing path below *clamps* an over-capacity
/// LUT and *floors* an odd element count during load, so a naive import silently
/// accepts a curve that differs from the file on disk — the CLI rejects these
/// up front (O31-L1 size check, P120-FAZ2) and the GUI must too.  Returns "" on
/// OK, else a human-readable problem description (not a tr() key — callers wrap
/// it with their own context).
///
/// The raw JSON is re-parsed here because every structured parser that can
/// report lengths is also a sanitizer; we deliberately DON'T reuse
/// profile_from_json for the check.  That would leave the GUI's LUT-max guard
/// dead code (widgets_sync.inl truncation warning) — identical mismatch as the
/// CLI import proved worth rejecting.
std::string check_import_lut_size(const std::string& json_str) {
    try {
        const json j = json::parse(json_str);
        auto check = [](const json& root, const char* axis_key,
                        const char* axis_name) -> std::string {
            if (!root.contains("profile") || !root["profile"].is_object())
                return std::string();
            const json& args = root["profile"];
            if (!args.contains(axis_key) || !args[axis_key].is_object())
                return std::string();
            const json& ax = args[axis_key];
            if (!ax.contains("lut_data") || !ax["lut_data"].is_array())
                return std::string();
            const size_t n = ax["lut_data"].size();
            if (n % 2 != 0 || n / 2 > LUT_POINTS_CAPACITY) {
                std::string msg = "LUT (" + std::string(axis_name) + " axis) has " +
                                  std::to_string(n) + " raw elements (" +
                                  std::to_string(n / 2) + " points";
                if (n % 2 != 0) msg += ", with an odd element count";
                msg += "); maximum is " + std::to_string(LUT_POINTS_CAPACITY) +
                       " points (" + std::to_string(LUT_RAW_DATA_CAPACITY) +
                       " raw elements).";
                return msg;
            }
            return std::string();
        };
        std::string x = check(j, "accel_x", "X");
        if (!x.empty()) return x;
        return check(j, "accel_y", "Y");
    } catch (const std::exception&) {
        // Malformed JSON is reported properly by profile_from_json on the
        // actual import; the size pre-check is best-effort only.
        return std::string();
    }
}

/// Sanitize a device_profile in-place (useful for values set programmatically).
void sanitize_device_profile(device_profile& dp) {
    sanitize_device_config(dp.dev_cfg);
    sanitize_profile(dp.prof);
}

std::string find_config_path() {
    // CFG-7: if XDG_CONFIG_HOME is set (non-empty), it takes precedence over
    // a fixed $HOME/.config — that is the whole point of the variable and it
    // is what various CLI tools honor.
    auto xdg_override = []() -> std::string {
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && xdg[0] != '\0')
            return std::string(xdg) + "/rawaccel/settings.json";
        return {};
    };
    std::string xdg = xdg_override();
    // D7: when running under sudo, HOME may be /root — prefer SUDO_USER's home directory.
    const char* sudo_user = std::getenv("SUDO_USER");
    if (sudo_user && sudo_user[0] != '\0') {
        struct passwd  pwd_buf;
        struct passwd* result = nullptr;
        // CFG-7: getpwnam_r can return ERANGE when the caller buffer is too
        // small for a long passwd line (up to 8 KiB / 64+ KiB with NSS+LDAP).
        // Retry with a growing buffer instead of silently falling back to the
        // wrong (possibly /root) home or the hardcoded /etc path.
        size_t buf_size = 16384;
        int    ret;
        std::vector<char> buf;
        for (int attempt = 0; attempt < 4; ++attempt) {
            buf.assign(buf_size, '\0');
            ret = getpwnam_r(sudo_user, &pwd_buf, buf.data(), buf.size(), &result);
            if (ret == 0 && result && result->pw_dir && result->pw_dir[0] != '\0') {
                if (!xdg.empty()) return xdg;                  // CFG-7
                return std::string(result->pw_dir) + "/.config/rawaccel/settings.json";
            }
            if (ret != ERANGE) break;
            buf_size *= 2; // up to 256 KiB worst-case on the last attempt
        }
    }
    // Normal user or root (e.g. systemd service)
    if (!xdg.empty()) return xdg;                               // CFG-7
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/.config/rawaccel/settings.json";
    }
    return DEFAULT_CONFIG_PATH;
}

const char* current_config_version() {
    return RAWACCEL_VERSION;
}

// 0.4.0 breaking change: in lookup + gain mode the stored y is now an OUTPUT
// SPEED (effective gain = y / speed), matching reference RawAccel. Configs
// written by <=0.3.x stored y as a direct gain. Reproduce the old curve by
// scaling each point: y_new = y_old * x.
static void migrate_lookup_gain(app_config& cfg) {
    for (auto& dp : cfg.profiles) {
        auto fix = [](accel_args& a) {
            if (a.mode != accel_mode::lookup || !a.gain) return;
            int n = a.length / 2;
            for (int i = 0; i < n; i++) {
                double x = a.data[i * 2];
                double y = a.data[i * 2 + 1];
                if (!(x > 0)) continue; // first point at speed 0 stays (velocity division is guarded)
                // BUG-MED-1: y*x can overflow to Inf for extreme speed/gain
                // values, poisoning the LUT with non-finite entries that cause
                // NaN propagation in the acceleration pipeline.  Drop the
                // point (leave the old value) if the product is non-finite.
                double product = y * x;
                if (std::isfinite(product))
                    a.data[i * 2 + 1] = static_cast<float>(product);
            }
            // ORTA-BUG-MOTION-04: the odd-length trailing element needs no
            // migration — it is unpaired (no x to multiply), so leave it as-is.
            // (Previously a dead `y = data[last]; data[last] = y;` no-op.)
        };
        fix(dp.prof.accel_x);
        fix(dp.prof.accel_y);
    }
}

/// Simple semantic-version "older than" comparison for "0.0.0"-style strings.
/// Returns true when lhs < rhs (missing/unknown components count as 0).
static bool version_lt(const std::string& lhs, const std::string& rhs) {
    auto parse = [](const std::string& v, std::array<int,3>& out) {
        size_t start = 0, pos = 0;
        int part = 0;
        while (part < 3 && pos != std::string::npos) {
            pos = v.find('.', start);
            const std::string tok = (pos == std::string::npos) ? v.substr(start) : v.substr(start, pos - start);
            if (tok.empty()) return false;
            // BUG-LOW-2: strtoul("-1") does NOT set errno and returns
            // ULONG_MAX (two's-complement wrap), which then clamps to INT_MAX
            // — a negative version component became a huge positive one, so a
            // corrupt stored version like "0.6.-1" compared "newer than"
            // everything and migration was silently skipped.  A leading '-'
            // is never valid: flag it and let the history treat the version
            // as corrupt (lhs → older than everything, rhs → never less).
            if (tok[0] == '-') return false;
            char* end = nullptr;
            errno = 0;
            const unsigned long n = std::strtoul(tok.c_str(), &end, 10);
            // BUG-NEW-70: strtoul overflow (ERANGE) sets errno and returns
            // ULONG_MAX WITHOUT touching end — a value like "99999999999"
            // falls through as ULONG_MAX and `static_cast<int>` is UB.
            // Clamp to the int range instead of relying on the cast.
            if (errno == ERANGE || end == tok.c_str())
                return false;
            if (end && *end != '\0') return false;
            out[part++] = static_cast<int>(std::min<unsigned long>(n, INT_MAX));
            if (pos != std::string::npos) start = pos + 1;
        }
        return part > 0;
    };
    std::array<int,3> a = {0,0,0}, b = {0,0,0};
    // YÜKSEK-BUG-MOTION-02: a corrupt/unparseable STORED version (e.g. "abc")
    // must be treated as "older than any real version" so migration still runs.
    // Previously parse() failure → false here silently skipped ALL migrations,
    // leaving pre-0.4 lookup+gain data permanently unmigrated.
    if (!parse(lhs, a)) return true; // corrupt lhs → older than everything
    if (!parse(rhs, b)) return false; // corrupt rhs never compares less
    if (a[0] != b[0]) return a[0] < b[0];
    if (a[1] != b[1]) return a[1] < b[1];
    if (a[2] != b[2]) return a[2] < b[2];
    // FINDING-34-3: first 3 components equal — a version with more
    // components is greater (e.g. "0.6.4" < "0.6.4.1").
    auto count_dots = [](const std::string& s) {
        int n = 0;
        for (char c : s) n += (c == '.');
        return n;
    };
    return count_dots(lhs) < count_dots(rhs);
}

bool migrate_config(app_config& cfg) {
    // A blank or unknown stored version is treated as "older than current":
    // compare semver-style so stale/downgraded configs are always migrated,
    // no matter what the exact version string was.
    if (cfg.version == RAWACCEL_VERSION) {
        return false;
    }

    bool migrated = false;

    // Migration from pre-0.2.0 (no version field)
    if (cfg.version.empty()) {
        // Older configs didn't have the version field
        // They also might be missing some newer fields that get default-initialized
        // The struct default values should handle most of this, but we can add
        // explicit defaults here if needed
        migrated = true;
    }

    // Migration from 0.2.x to 0.3.0
    if (version_lt(cfg.version, "0.3.0")) {
        // No breaking changes in 0.3.0, just new fields (version)
        // The struct default values handle new fields
        migrated = true;
    }

    // Migration from 0.3.x to 0.4.0 — lookup+gain data semantics changed.
    if (cfg.version.empty() || version_lt(cfg.version, "0.4.0")) {
        migrate_lookup_gain(cfg);
        migrated = true;
    }

    // Update to current version
    if (migrated) {
        cfg.version = RAWACCEL_VERSION;
    }

    return migrated;
}

} // namespace rawaccel
