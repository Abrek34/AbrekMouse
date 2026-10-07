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
// AJ4-K7 (same scan, olcum/arsiv/gauge_scan.py): the scan then showed
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
// T46-05: speed_min/speed_max had NO shared ceiling — the GUI spin said 500
// (later 100000 via L16-01), sanitize only enforced >= 0, and the CLI was
// min-only, so a JSON/CLI value > spin-max became silent truncation on the
// next GUI save.  100000 ips is far above any physical input domain (8000 Hz
// × 32000 DPI saturates the clamp long before it) and is exactly the value
// L16-01 already chose for the GUI spin — make it the single source of truth
// shared by GUI gauge, sanitize clamp and the CLI domain.
static constexpr double SPEED_MAX         = 100000.0;
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

/// Load config from a JSON file.  **Throws on far more than a parse error** —
/// "throws on parse error" (the wording until Aj.3 P103) covered 1 of the 8
/// measured throw categories, and the 7 it omitted are the ones that a
/// hand-edited or generated file actually triggers.  Callers MUST be inside a
/// try/catch for ALL of them:  a malformed number is the cheapest way to reach
/// one, and an unguarded call does not return — it aborts.  Measured: a config
/// carrying `"output_dpi": 1e400` aborts the process with SIGABRT, exit 134
/// (positive control for this note: the same call inside try/catch returns 3
/// and the caller logs and falls back).
///
///   1. `json::parse_error`          — syntactically malformed JSON.
///   2. `json::out_of_range`         — number overflow (`1e400`)  ← NOT a parse
///      error, despite arriving from inside `json::parse`; the parser rejects
///      the literal before the value ever reaches sanitization, so the
///      `finite_or()` NaN/Inf pass in `sanitize_profile` never sees it.
///   3. `runtime_error`              — file cannot be opened (`config.cpp:820`).
///   4. `runtime_error`              — root is not an object/null (`:714`).
///   5. `runtime_error`              — `profiles` is not an array (`:756`).
///   6. `runtime_error`              — `profiles[i]` is not an object (`:773`).
///   7. `runtime_error`              — an accel_args scalar has the wrong JSON
///      type, `"config field 'X' must be a number, got <type>"` (`:149`).
///   8. `runtime_error`              — …or is non-finite, `"must be finite"`
///      (`:153`).  Both from `require_number` (`:145-156`), applied to the 12
///      P120-FAZ2 scalars: absent key = "use default" and is silent, but a key
///      that is PRESENT and wrong type refuses the whole config — deliberate,
///      so a schema-mismatched file cannot load half-correct acceleration math.
///
/// Note the asymmetry, which is policy and not an oversight: the scalar
/// accel_args fields (7, 8) throw, while `cap` (`:169-178`), `mode` and the
/// scalar/string device fields degrade to defaults.  Sanitization never runs
/// on a config that threw, so "sanitize clamps this" is not a safety argument
/// for a rejected file.  All 7 production call sites are inside try/catch —
/// `daemon/daemon.cpp:471,2029`, `cli/main.cpp:770,2053,2111,3184`,
/// `gui/main.cpp:185` — and the GUI path (`:184-186`) additionally stashes the
/// file as `.corrupt-<unixtime>` before falling back, so a rejected config is
/// recoverable rather than merely survivable.
app_config load_config(const std::string& path);

/// Save config to a JSON file.  **Throws on every I/O failure** — the wording
/// here was one line with no contract at all (Aj.3 P106), which is the same gap
/// `load_config` had and worse: `load_config` at least said "throws on parse
/// error" (1 of its 8 measured categories), `save_config` said nothing while
/// throwing on all of its own.  Callers MUST be inside a try/catch.  An
/// unguarded call does not return — it aborts, and the config the caller
/// believed it had persisted is the OLD one on disk, because the write is
/// atomic and only the `rename()` publishes it.
///
/// `save_config` is `config.cpp:832-1013` (182 lines) and has 4 explicit throws
/// plus 1 implicit one:
///   1. `std::runtime_error` — cannot open/write the temp file (`:911`).
///   2. `std::runtime_error` — write error on the temp file (`:938`).
///   3. `std::runtime_error` — `fsync()` on the temp file failed (`:946`).
///   4. `std::runtime_error` — `rename()` temp → target failed (`:996`).
///   5. `std::filesystem::filesystem_error` — implicit, from
///      `fs::create_directories(parent_path)` (`:848`) when the parent cannot
///      be created.
///
/// The 4 explicit ones are the durability contract made visible: a throw means
/// the target file still holds the previous, valid config — the temp file is
/// pid-suffixed and `O_NOFOLLOW|O_EXCL`, so no half-written JSON is ever
/// published and no symlink can be clobbered.  That safety is why the throws
/// are not swallowed inside the function: a caller that catches learns the save
/// did NOT happen, and `cli/main.cpp`'s `safe_save` turns that into a clean exit
/// with a `.bak` rotation rather than a SIGABRT.
///
/// All 5 production call sites are inside try/catch — `cli/main.cpp:428` (in
/// `safe_save`, whose `try` is at `:427`), `cli/main.cpp:3172`, `:3235`,
/// `daemon/daemon.cpp:681` (`try` at `:677`, with both `catch (const
/// std::exception&)` and `catch (...)`), `gui/main.cpp:46` (in
/// `save_config_now`, `try` at `:45`).  Measured 5/5, the same coverage level
/// as `load_config`'s 7/7 — but note it is a *convention*, not a type
/// guarantee: nothing in the signature forces a 6th call site to catch, which
/// is the trap this note exists to close.
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
