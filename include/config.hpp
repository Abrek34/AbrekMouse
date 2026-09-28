#pragma once
#include "rawaccel.hpp"
#include <string>
#include <vector>
#include <stdexcept>

namespace rawaccel {

static constexpr const char* DEFAULT_CONFIG_PATH = "/etc/rawaccel/settings.json";

// P120-FAZ2 (Aj8 BUG-3): upper bounds for the accel_args fields that drive
// output gain.  Sanitize (config load / IPC push), the CLI set-param domains
// and the CLI help text all share these so a value can never exceed what the
// GUI gauge itself allows (gui/ui_builder.inl: scale 0.01..100, power_exp
// 0.01..5, cap_x 0..500, cap_y 0..100, output_offset 0..100).  They are also
// exactly the boundary values the R15 round-trip test requires to be
// load-preserved, so sanitize must clamp at these maxima (never below).
static constexpr double SCALE_MAX          = 100.0;
static constexpr double EXP_POWER_MAX      = 5.0;
static constexpr double CAP_X_MAX          = 500.0;
static constexpr double CAP_Y_MAX          = 100.0;
static constexpr double OUTPUT_OFFSET_MAX  = 100.0;
// AJ4-K6 (config-presets, 6. gain-driving alan): `limit` was the ONLY member
// of this P120-FAZ2 block with no upper bound — sanitize accepted 1e6 while the
// gauge that displays it cannot leave [0,100].
//   measured: gui/ui_builder.inl:241 and :411 both build the limit spin as
//   make_spin(0, 100, 0.05, 1.5) — so a hand-edited `limit = 1e6` ran in the
//   daemon while the GUI rendered 100 next to it, and the next GUI save wrote
//   100 back (the P120-FAZ2 defect class this block exists to prevent).
//   The value 100 is NOT chosen freely — two independent constraints pin it:
//     * the R15 boundary round-trip test requires exactly 100.0 to load
//       byte-preserved (tests/test_accel.cpp:7869 assigns 100.0, :7914
//       EXPECT_NEAR(ax2.limit, 100.0, 1e-9)), so the ceiling may not be lower;
//     * the GUI gauge may not be higher (same lines), so it may not be larger.
//   Both land on 100.  Room to spare for real configs: built-in presets span
//   [1.2, 2.2] (include/presets.hpp) and the oracle grid spans [1.2, 1.8]
//   (tests/oracle/oracle_cases.hpp), so the ceiling touches neither.
//
// AJ4-K7 (same scan, olcum/config-presets/gauge_scan.py): the scan then showed
// that `limit` was NOT special — SIX more accel_args fields have a GUI gauge
// maximum that sanitize never enforced, and for all six the same two
// independent constraints agree on the number:
//   alan          GUI gauge (ui_builder.inl)   R15 boundary (test_accel.cpp)
//   acceleration  0..20   (:238)               20.0  (:7864)
//   decay_rate    0..10   (:243)               10.0  (:7871)
//   motivity      0.01..10(:248)               10.0  (:7873)
//   gamma         0.01..10(:249)               10.0  (:7874)
//   sync_speed    0.0001..100 (:246)           100.0 (:7875)
//   smooth        0..1    (:247)               1.0   (:7876)
//   (and `limit`  0..100  (:241)               100.0 (:7869))
// Measured headroom: every built-in preset and every struct default sits far
// below — acceleration 0.002..0.005, decay_rate 0.08..0.1, motivity 1.2..1.5,
// gamma 1, sync_speed 5, smooth 0.5 (include/presets.hpp, rawaccel-base.hpp:68-77).
// No test round-trips any of these through JSON above the boundary (grep: zero
// hits); the large values that exist (acceleration=999999, decay_rate=1000,
// smooth=16, sync_speed=500) are struct-direct into the algorithm and never
// pass through sanitize, so they are untouched.
static constexpr double LIMIT_MAX         = 100.0;
static constexpr double ACCEL_MAX         = 20.0;
static constexpr double DECAY_RATE_MAX    = 10.0;
static constexpr double MOTIVITY_MAX      = 10.0;
static constexpr double GAMMA_MAX         = 10.0;
static constexpr double SYNC_SPEED_MAX    = 100.0;
static constexpr double SMOOTH_MAX        = 1.0;
// NOT clamped (reported, not fixed — GUI-side, AJ4→AJ1):
//   input_offset — gui/ui_builder.inl:242 gauge is 0..100 but sanitize and the
//   CLI both permit [0, CAP_X_MAX] = [0,500] (O31-L2, tests/run_tests.sh:161
//   pins that CLI domain).  Here the GAUGE is the outlier — cap.x may be 500
//   and cap.x >= input_offset, so 500 is meaningful.  Clamping sanitize to 100
//   would break the BUG-7/O31-L2 contract; the fix is raising the gauge.

struct device_config {
    bool   disable         = false;
    int    dpi             = 800;
    int    polling_rate    = 1000;
};

struct device_profile {
    std::string  device_id;     // empty = apply to all mice
    std::string  name;
    device_config dev_cfg;
    profile      prof;
    // P-APP profile: non-empty → this profile only applies while an
    // application whose WM_CLASS (or cmdline basename) contains this
    // case-insensitive substring is focused.  Empty = applies always
    // (fallback).  When both device_id and match_app are set, BOTH must
    // match for the profile to be used; "All devices" with an app match
    // is the common configuration.
    std::string  match_app;
};

struct app_config {
    std::vector<device_profile> profiles;
    std::string                 active_profile = "default";
    bool                        use_raw_input  = true;
    std::string                 version;  // config schema version (e.g. "0.3.0")
};

/// Load config from a JSON file. Throws on parse error.
app_config load_config(const std::string& path);

/// Save config to a JSON file.
void save_config(const app_config& cfg, const std::string& path);

/// Convert a profile to/from JSON string (for IPC).
std::string profile_to_json(const device_profile& p);
device_profile profile_from_json(const std::string& json_str);

/// Serialize a full app_config to a JSON string (for the IPC config-push RPC:
/// GUI/CLI send the live config to the daemon so it persists it and reloads it).
std::string app_config_to_json(const app_config& cfg);

/// Parse a full app_config from a JSON string (IPI config-push RPC).
/// Same sanitization rules as load_config() (clamps DPI, rotation, etc.).
app_config app_config_from_json(const std::string& json_str);

/// Sanitize a device_profile in-place (clamp DPI, polling_rate, rotation, etc.).
void sanitize_device_profile(device_profile& dp);

/// R12-IMPLUT: pre-parse an imported profile JSON and report over-capacity or
/// odd-element LUT tables that profile_from_json() would silently clamp/floor.
/// Returns "" when the curve is acceptable, else a human-readable description.
/// Shared by the GUI import (CLI keeps its own copy).
std::string check_import_lut_size(const std::string& json_str);

/// Find or create config path (user home or /etc).
std::string find_config_path();

/// Migrate an older config to the current schema version.
/// Returns true if migration was performed, false if already current.
bool migrate_config(app_config& cfg);

/// Get the current config schema version.
const char* current_config_version();

} // namespace rawaccel
