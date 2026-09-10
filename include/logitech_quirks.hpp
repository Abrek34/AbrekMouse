// ── Per-model Logitech quirks + capability normalisation — P169 ───────────────
//
// Independent C++ encoding of the protocol behaviour that the Solaar project
// has validated on real hardware.  Solaar is GPLv2-or-later; this header is
// NOT a line-by-line translation of Solaar source.  Only the published
// semantics (feature IDs, validated model entries, default-DENY policy) are
// re-expressed here in RawAccel's own types, so the accelerated/daemon motion
// path and RawAccel's licence structure stay untouched.
//
// Keying follows Solaar `device.modelId`: the composited transport PID string
// (btid+btleid+wpid+usbid) reported by DEVICE_FW_VERSION function 0.  One
// entry covers the model on any transport.  `logitech_compose_model_id()`
// rebuilds that key from the decoded transport ids.
//
// RawAccel has no RGB write path today, so the NV-config / signature-effect
// rows are a *read-only policy*: they declare which persistent-light effects
// are known-good per model, exactly as Solaar gates them.  Any future code
// that enables such writes must consult this table first (default-DENY).
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

/// Per-model quirk record.  Default construction = fully policy-suppressed
/// (a model that is not listed must not be written without real-device
/// validation — the same conservative stance as Solaar).
struct logitech_quirks {
    std::vector<logitech_nvconfig_row> nvconfig;   // 0x8071 — default-DENY allowlist
    std::vector<logitech_headset_row> headset;     // 0x0622 — default-DENY allowlist
};

struct logitech_quirks_entry {
    const char* model_id;   // composited modelId, upper-case hex
    logitech_quirks quirks;
};

/// Solaar-validated rows only (lib/logitech_receiver/device_quirks.py).
/// Unknown models return nullptr via find_logitech_quirks(), i.e. are treated
/// as not-yet-validated and are therefore write-protected.
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
    // Short-key fallback: quirks table may use abbreviated keys (e.g. "32")
    // that are suffixes of the full composed model ID.
    for (const auto& entry : LOGITECH_QUIRKS) {
        size_t klen = std::char_traits<char>::length(entry.model_id);
        if (klen <= 4 && model_id.size() >= klen &&
            model_id.compare(model_id.size() - klen, klen, entry.model_id) == 0)
            return &entry.quirks;
    }
    return nullptr;
}

// ── Capability normalisation (feature-derived) ───────────────────────────────

/// Which battery facility the device advertises.  Priority ordering matches
/// Solaar: unified → status → voltage → HID++ 1.0 register fallback.
enum class logitech_battery_source : uint8_t {
    none,             // no battery feature advertised
    legacy10,         // HID++ 1.0 register 0x07 fallback
    battery_voltage,  // 0x1001 — percent is approximated from millivolts
    battery_status,   // 0x1000
    unified_battery,  // 0x1004
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
    return c;
}

/// Rebuild Solaar's composited modelId (upper-case hex, btid+btleid+wpid+usbid)
/// from the transport ids decoded by the transport.  Falls back to the raw
/// DEVICE_FW_VERSION model-id byte range ([7:13]) when no transport id is set.
inline std::string logitech_compose_model_id(const hidpp_device_info& info) {
    std::string out;
    for (const std::string* id : {&info.bluetooth_id, &info.bluetooth_le_id,
                                  &info.wireless_pid, &info.usb_id})
        if (!id->empty()) out += *id;
    if (!out.empty()) return out;
    return info.model_id;
}

} // namespace rawaccel