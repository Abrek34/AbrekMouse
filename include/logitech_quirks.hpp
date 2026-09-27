// ── Per-model Logitech quirks + capability normalisation — P169 ───────────────
//
// Independent C++ encoding of the protocol behaviour that the Solaar project
// has validated on real hardware.  Solaar is GPLv2-or-later; this header is
// NOT a line-by-line translation of Solaar source.  Only the published
// semantics (feature IDs, validated model entries) are re-expressed here in
// RawAccel's own types, so the accelerated/daemon motion path and RawAccel's
// licence structure stay untouched.  The "default-DENY policy" Solaar applies
// to these rows is NOT re-expressed — see the MEASURED note below on why.
//
// Keying follows Solaar `device.modelId`: the composited transport PID string
// (btid+btleid+wpid+usbid) reported by DEVICE_FW_VERSION function 0.  One
// entry covers the model on any transport.  `logitech_compose_model_id()`
// rebuilds that key from the decoded transport ids.
//
// ⚠ MEASURED, NOT ASSUMED (O31-H6): RawAccel has **no write path** for
// 0x8071 / 0x0622.  The only HID++ writes in the tree are set_dpi,
// set_polling_rate, set_lift_off_distance, set_led_brightness,
// set_change_host and write_onboard_profile_sector — measured, and none of
// them consults this table.  The nvconfig/headset rows are therefore inert
// DATA: nothing reads them, and the "default-DENY allowlist" they were
// documented as is not enforced anywhere.  The protection they describe holds
// today only vacuously — there is no such write to protect, not a gate
// refusing one.
//
// So this is a *registry of device-validated rows*, not an active policy.
// That distinction is load-bearing for the next person: adding an RGB write
// path means adding the gate with it, in the same change.  Reading this table
// as a live allowlist is the specific mistake this paragraph exists to stop —
// the rows look like an enforced gate, and nothing is enforcing them.
//
// The capability normalisation helpers below are feature-derived (truth comes
// from the device's own FEATURE_SET discovery, never from assumptions), plus
// the same battery-priority ordering Solaar uses.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "logitech_hidpp.hpp"

namespace rawaccel {

// Forward declaration — the definition below is needed by the
// find_logitech_quirks(const hidpp_device_info&) overload (NEW-3 fix).
inline std::string logitech_compose_model_id(const hidpp_device_info& info);

// ── Per-model quirks table (mirrors Solaar device_quirks.py) ─────────────────

/// One 0x8071 RGBEffects NvConfig (persistent boot/shutdown effect) cap.
/// `fields` = colour/speed fields the firmware is KNOWN to honour; an empty
/// list means "On/Off toggle only".
struct logitech_nvconfig_row {
    uint16_t cap_id;
    std::vector<std::string> fields;
};

/// One 0x0622 HeadsetRGB signature-effect slot.  Same field semantics.
struct logitech_headset_row {
    uint16_t effect_id;
    std::vector<std::string> fields;
};

/// Per-model quirk record: the device-validated rows for one model.
/// Default construction = *no validated rows*, which is the correct answer
/// for an unknown model (Solaar treats unlisted models the same conservative
/// way).  Note that today no write path reads these vectors, so "empty" is not
/// yet load-bearing — see the MEASURED note at the top of this header.
struct logitech_quirks {
    std::vector<logitech_nvconfig_row> nvconfig;   // 0x8071 — validated rows
    std::vector<logitech_headset_row> headset;     // 0x0622 — validated rows
};

struct logitech_quirks_entry {
    const char* model_id;   // composited modelId, upper-case hex
    logitech_quirks quirks;
};

/// Solaar-validated rows only (lib/logitech_receiver/device_quirks.py).
/// Unknown models return nullptr via find_logitech_quirks() — that means "no
/// validated rows on record", full stop.  It is NOT a write-protection verdict
/// today: nothing writes these features, so the nullptr is never consulted to
/// refuse anything.  A future RGB write path must branch on it explicitly.
inline const std::vector<logitech_quirks_entry> LOGITECH_QUIRKS = [] {
    std::vector<logitech_quirks_entry> t;
    // G502 X PLUS — 0x8071 cap 0x0001 (startup): the colour bytes are inert,
    // only the enabled flag is honoured → toggle only (empty field set).
    t.push_back({"4099C0950000",
                 {{{0x0001, {}}}, {}}});
    // G515 LIGHTSPEED TKL — startup (0x0001) and shutdown (0x0040) both honour
    // both colour fields.
    t.push_back({"B38940B4C355",
                 {{{0x0001, {"color1", "color2"}},
                   {0x0040, {"color1", "color2"}}},
                  {}}});
    // G522 LIGHTSPEED (Centurion model byte 0x32) — 0x0622 startup honours the
    // primary colour only, shutdown honours both; the passive slot's behaviour
    // is not understood, so it is suppressed entirely.
    //
    // ⚠ O31-H2 — THIS ROW IS CURRENTLY UNREACHABLE, AND THAT IS KNOWN, NOT
    // OVERLOOKED.  "32" is a single model *byte* taken from the middle of the
    // 12-hex-char model id, not a model id.  logitech_compose_model_id() can
    // only ever return "" or 12 chars (measured over all 16 transport-flag
    // combinations x both response-size cases), so this key can never be
    // matched by find_logitech_quirks().  The short-key loop that was supposed
    // to make it reachable has been removed as dead code (see above).
    //
    // Status is deliberately PARKED, matching the recorded project decision
    // (FIX_LOG.md O31-H2 / "Bug Hata Raporları.md" O31-H2: "gerçek 12-char id
    // doğrulanmadan yazılmaz").  The real 12-char G522 model id cannot be
    // obtained without the physical device, and a guessed id would be worse
    // than an honest dead row.  The two ways out, both needing a decision:
    //   (a) read the real 12-char id off a G522 and replace "32" with it, or
    //   (b) drop this row and leave the G522 with no validated rows at all,
    //       conservative policy the table already applies to unknown models.
    // Either way this comment goes with the change.  test_logitech_quirks_
    // model_id_shape() pins the invariant that keeps this row dead, so a future
    // change to the id parsing surfaces here instead of silently mattering.
    t.push_back({"32",
                 {{},
                  {{0, {"color1"}},
                   {1, {"color1", "color2"}}}}});
    return t;
}();

inline const logitech_quirks* find_logitech_quirks(const std::string& model_id) {
    for (const auto& entry : LOGITECH_QUIRKS)
        if (model_id == entry.model_id)
            return &entry.quirks;
    // O31-H2: a "short-key fallback" loop used to live here.  It was provably
    // dead and has been removed — its match condition was
    //   klen <= 4 && model_id.size() == klen && model_id == entry.model_id
    // of which the last clause is already tested by the loop above for EVERY
    // entry, so the loop could never produce a hit the first loop had missed
    // (measured: 0 new matches over the whole probe set).  Its comment claimed
    // it let an abbreviated key match "the FULL composed model ID", but
    // model_id.size() == klen (12 == 2) makes that impossible — the code said
    // the opposite of what the comment described.  Reachable model_id values
    // are only "" or 12 hex chars (see logitech_compose_model_id), so no short
    // key can match by any route.  If a real short key is ever wanted it needs
    // a verified full 12-char ID from hardware, not a matching heuristic.
    return nullptr;
}

/// Overload: look the device up by its composed modelId.
///
/// O31-H2: this used to add a second probe of the raw `info.model_id`.  That
/// probe was provably dead as well and is gone: compose() returns
/// `info.model_id` verbatim whenever it is non-empty (so the re-probe would
/// be looking at the very string that had just missed), and when it IS empty
/// every transport id is empty too, because all five fields are assigned
/// inside the same `if (count->size() >= 13)` block in logitech_hidpp.cpp —
/// so the re-probe would look up "" and no key is the empty string.
inline const logitech_quirks* find_logitech_quirks(
        const hidpp_device_info& info) {
    return find_logitech_quirks(logitech_compose_model_id(info));
}

// ── Capability normalisation (feature-derived) ───────────────────────────────

/// Which battery facility the device advertises.  Priority ordering matches
/// Solaar: unified → status → voltage → HID++ 1.0 register fallback.
enum class logitech_battery_source : uint8_t {
    none,                 // no battery feature advertised
    legacy10,             // HID++ 1.0 register 0x07 fallback
    battery_voltage,      // 0x1001 — percent is approximated from millivolts
    battery_status,       // 0x1000
    unified_battery,      // 0x1004
    centurion_battery,    // 0x0104 — PRO X 2 LIGHTSPEED, G515 LS TKL, etc.
};

inline bool logitech_has_feature(
    const std::vector<std::pair<uint16_t, uint8_t>>& features, uint16_t id) {
    for (const auto& f : features)
        if (f.first == id) return true;
    return false;
}

inline logitech_battery_source preferred_battery_source(
    const std::vector<std::pair<uint16_t, uint8_t>>& features,
    bool hidpp10 = false) {
    if (logitech_has_feature(features,
            static_cast<uint16_t>(hidpp_feature_index::unified_battery)))
        return logitech_battery_source::unified_battery;
    if (logitech_has_feature(features,
            static_cast<uint16_t>(hidpp_feature_index::centurion_battery_soc)))
        return logitech_battery_source::centurion_battery;
    if (logitech_has_feature(features,
            static_cast<uint16_t>(hidpp_feature_index::battery_status)))
        return logitech_battery_source::battery_status;
    if (logitech_has_feature(features,
            static_cast<uint16_t>(hidpp_feature_index::battery_voltage)))
        return logitech_battery_source::battery_voltage;
    if (hidpp10) return logitech_battery_source::legacy10;
    return logitech_battery_source::none;
}

/// Hardware controls a device advertises via FEATURE_SET.  The GUI disables
/// fields whose feature is missing and annotates the reason; every value here
/// comes from the device's own capability discovery, never from a model guess.
struct logitech_controls {
    bool dpi                 = false; // 0x2201 or 0x2202 advertised
    bool dpi_xy              = false; // 0x2202 → per-axis DPI
    bool lod                 = false; // lift-off distance requires 0x2202
    bool report_rate         = false; // 0x8060 (legacy 1..8 ms periods)
    bool report_rate_extended = false; // 0x8061 (extended, down to 125 us)
    bool onboard_profiles    = false; // 0x8100 (read-only query supported)
    // ── Aşama 1 ─────────────────────────────────────────────────────
    bool change_host         = false; // 0x1814 Easy-Switch
    bool led_brightness      = false; // 0x8040 / 0x1982 / 0x1981
    bool reprog_controls     = false; // 0x1B04 button list / diversion
    bool super_strike        = false; // 0x1B0C hall-effect trigger
};

inline logitech_controls logitech_controls_for(
    const std::vector<std::pair<uint16_t, uint8_t>>& features) {
    logitech_controls c;
    c.dpi = logitech_has_feature(features,
                static_cast<uint16_t>(hidpp_feature_index::adjustable_dpi)) ||
            logitech_has_feature(features,
                static_cast<uint16_t>(hidpp_feature_index::extended_adjustable_dpi));
    c.dpi_xy = logitech_has_feature(features,
        static_cast<uint16_t>(hidpp_feature_index::extended_adjustable_dpi));
    c.lod = c.dpi_xy;
    c.report_rate = logitech_has_feature(features,
        static_cast<uint16_t>(hidpp_feature_index::report_rate));
    c.report_rate_extended = logitech_has_feature(features,
        static_cast<uint16_t>(hidpp_feature_index::extended_adjustable_report_rate));
    c.onboard_profiles = logitech_has_feature(features,
        static_cast<uint16_t>(hidpp_feature_index::onboard_profiles));
    c.change_host = logitech_has_feature(features,
        static_cast<uint16_t>(hidpp_feature_index::change_host));
    c.led_brightness = logitech_has_feature(features,
            static_cast<uint16_t>(hidpp_feature_index::brightness_control)) ||
        logitech_has_feature(features,
            static_cast<uint16_t>(hidpp_feature_index::backlight2)) ||
        logitech_has_feature(features,
            static_cast<uint16_t>(hidpp_feature_index::backlight));
    c.reprog_controls = logitech_has_feature(features,
        static_cast<uint16_t>(hidpp_feature_index::reprog_controls_v4));
    c.super_strike = logitech_has_feature(features,
        static_cast<uint16_t>(hidpp_feature_index::superstrike_tuning));
    return c;
}

/// Rebuild Solaar's composited modelId (upper-case hex, btid+btleid+wpid+usbid)
/// used to key the quirks table.  The raw DEVICE_FW_VERSION model-id byte
/// range ([7:13], 6 bytes → 12 hex chars) already IS Solaar's `modelId` — the
/// firmware zero-pads the byte-pairs of transports the device does not sit on
/// (e.g. "B38940B4C355"), so every quirks key is 12 chars.  Re-assembling only
/// the flagged transport ids drops that padding and yields ≤8 chars, which can
/// never match the 12-char keys (NEW-3) — wireless devices would silently lose
/// all model quirks.  Prefer the raw 12-char range; keep the decoded per-
/// transport ids for callers that need the individual pairs.
inline std::string logitech_compose_model_id(const hidpp_device_info& info) {
    if (!info.model_id.empty())
        return info.model_id;
    std::string out;
    for (const std::string* id : {&info.bluetooth_id, &info.bluetooth_le_id,
                                  &info.wireless_pid, &info.usb_id})
        if (!id->empty()) out += *id;
    return out;
}

} // namespace rawaccel