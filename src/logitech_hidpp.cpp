#include "logitech_hidpp.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <poll.h>
#include <filesystem>
#include <climits>

namespace fs = std::filesystem;

namespace rawaccel {

namespace {

// A single unresponsive/replay-looped receiver must never stall device
// identification for minutes (BUG-01).  Every unbounded enumeration loop in
// the identify path shares this budget; once it is exhausted the loop gives
// up on the remaining slots/chunks so later, unrelated, faster steps (name,
// battery, DPI, notifications) still run.
constexpr auto kIdentifyBudget = std::chrono::seconds(20);
// After this many consecutive per-slot/timeouts the loop aborts early, as
// Solaar does, instead of walking hundreds of indices at ~1 s each.
constexpr int kIdentifyTimeoutStreak = 3;

bool is_hidpp_error(const uint8_t* data, size_t len) {
    return data && len >= 4 &&
           (data[0] == 0x10 || data[0] == 0x11 || data[0] == 0x12) &&
           (data[2] == 0xFF || data[2] == 0x8F);
}

std::optional<uint32_t> rate_code_to_hz(bool extended, uint8_t code) {
    if (extended) {
        constexpr uint32_t rates[] = {125, 250, 500, 1000, 2000, 4000, 8000};
        return code < (sizeof(rates) / sizeof(rates[0]))
            ? std::optional<uint32_t>(rates[code]) : std::nullopt;
    }
    // REPORT_RATE stores the report period in milliseconds.
    constexpr uint32_t rates[] = {0, 1000, 500, 333, 250, 200, 166, 142, 125};
    return code > 0 && code < (sizeof(rates) / sizeof(rates[0]))
        ? std::optional<uint32_t>(rates[code]) : std::nullopt;
}

std::optional<uint8_t> hz_to_rate_code(bool extended, uint32_t hz) {
    if (extended) {
        constexpr uint32_t rates[] = {125, 250, 500, 1000, 2000, 4000, 8000};
        for (uint8_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
            if (rates[i] == hz) return i;
    } else {
        constexpr uint32_t rates[] = {0, 1000, 500, 333, 250, 200, 166, 142, 125};
        for (uint8_t i = 1; i < sizeof(rates) / sizeof(rates[0]); ++i)
            if (rates[i] == hz) return i;
    }
    return std::nullopt;
}

uint16_t read_be16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

/// Decode a raw DPI byte-pair list into concrete DPI levels.
///
/// Logitech's DPI list encoding uses a 0xE000..0xFFFF marker pair whose low
/// 13 bits are the step size; the following word is the last level produced
/// by stepping from the previous level (Solaar's produce_dpi_list rule).
/// Both the 0x2202 (extended, multi-sensor) and 0x2201 (legacy) paths push
/// these raw bytes, so a single decoder keeps them consistent.  A truncated
/// trailing marker (marker word without its `last` companion) is dropped
/// rather than pushed as a bogus 0xE000+ level.
std::vector<uint16_t> decode_dpi_levels(const std::vector<uint8_t>& list_bytes) {
    std::vector<uint16_t> levels;
    for (size_t i = 0; i + 1 < list_bytes.size(); i += 2) {
        const uint16_t value = read_be16(&list_bytes[i]);
        if (value == 0) break;
        // 0xE000..0xFFFF denotes a step/last pair in Logitech's DPI
        // list encoding (Solaar's produce_dpi_list uses the same rule).
        if ((value >> 13) == 0x07) {
            if (i + 3 < list_bytes.size()) {
                const uint16_t step = value & 0x1FFF;
                const uint16_t last = read_be16(&list_bytes[i + 2]);
                if (step != 0 && !levels.empty() &&
                    last > levels.back()) {
                    uint32_t next = static_cast<uint32_t>(levels.back()) + step;
                    for (; next <= last; next += step) {
                        if (next > 0xFFFF) break; // guard against uint32→uint16 truncation
                        levels.push_back(static_cast<uint16_t>(next));
                    }
                }
                i += 2;
            }
            // else: truncated marker pair at the tail — drop it (nothing
            // meaningful follows), matching the extended-path behaviour.
        } else {
            levels.push_back(value);
        }
    }
    return levels;
}

std::string bytes_to_hex(const uint8_t* data, size_t size) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i)
        out << std::setw(2) << static_cast<unsigned int>(data[i]);
    return out.str();
}

std::string bounded_string(const uint8_t* data, size_t size) {
    size_t length = 0;
    while (length < size && data[length] != 0) ++length;
    return std::string(reinterpret_cast<const char*>(data), length);
}

/// Decode a BATTERY_VOLTAGE payload into a battery percentage using the same
/// conservative Li-ion curve Solaar applies.  `voltage` is in millivolts and
/// `flags` carries the charge/status bits.  Shared by the feature-request path
/// (get_battery_status) and the live-notification handler so both decode a
/// given mV reading identically.
hidpp_battery_info battery_info_from_voltage(uint16_t voltage, bool charging) {
    static constexpr std::pair<uint16_t, uint8_t> curve[] = {
        {3500, 0}, {3579, 2}, {3646, 5}, {3671, 10},
        {3717, 20}, {3751, 30}, {3778, 40}, {3811, 50},
        {3859, 60}, {3922, 70}, {3989, 80}, {4067, 90},
        {4186, 100}
    };
    uint8_t level = 0;
    if (voltage >= curve[sizeof(curve) / sizeof(curve[0]) - 1].first) {
        level = 100;
    } else if (voltage > curve[0].first) {
        for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
            if (voltage <= curve[i].first) {
                const auto [lo_v, lo_p] = curve[i - 1];
                const auto [hi_v, hi_p] = curve[i];
                // M-BUG-13: the old all-integer expression truncated the
                // interpolation to the floor percentage (e.g. 50.7% -> 50%)
                // for mid-range voltages.  Convert before dividing so the
                // percentage rounds to the nearest whole value.
                const double fraction = static_cast<double>(voltage - lo_v) /
                                       static_cast<double>(hi_v - lo_v);
                const double pct = static_cast<double>(lo_p) +
                                   static_cast<double>(hi_p - lo_p) * fraction;
                level = static_cast<uint8_t>(std::lround(pct));
                break;
            }
        }
    }
    hidpp_battery_info info;
    info.level = level;
    info.charging = charging;
    info.online = true;
    return info;
}

std::optional<hidpp_battery_info> parse_unified_battery(
    const std::vector<uint8_t>& payload) {
    if (payload.size() < 3) return std::nullopt;
    const uint8_t discharge = payload[0];
    const uint8_t level = payload[1];
    const uint8_t status = payload[2];
    hidpp_battery_info info;

    if (discharge <= 100 && discharge != 0) {
        info.level = discharge;
    } else {
        // Unified Battery reports a coarse level when a direct percentage is not
        // available. Some devices leave the coarse bucket at zero when the level is
        // unknown or the payload is only partially populated, so preserve the
        // "unknown" sentinel instead of collapsing it to 0%.
        switch (level) {
        case 8: info.level = 90; break; // FULL
        case 4: info.level = 50; break; // GOOD
        case 2: info.level = 20; break; // LOW
        case 1: info.level = 5; break;  // CRITICAL
        default: info.level = 255; break;
        }
    }

    // If a direct percentage is absent but the coarse bucket is also empty, keep
    // the caller able to distinguish "unknown" from a real 0% battery reading.
    if (discharge == 0 && level == 0 && status == 0xFF) info.level = 255;

    info.charging = status == 0x01 || status == 0x02 || status == 0x04;
    info.online = status != 0x05 && status != 0x06 && status != 0xFF;
    return info;
}

std::optional<hidpp_battery_info> parse_battery_status_feature(
    const std::vector<uint8_t>& payload) {
    if (payload.size() < 3) return std::nullopt;
    hidpp_battery_info info;
    // BATTERY_STATUS is a different feature from UNIFIED_BATTERY.  Its
    // payload is discharge, next-discharge, status; the middle byte is not a
    // charging flag and must not be interpreted as one.
    info.level = payload[0] == 0 ? 255 : payload[0];
    const uint8_t status = payload[2];
    // B5: 0x02 (almost_full) is also charging, not "discharging".
    info.charging = status == 0x01 || status == 0x02 || status == 0x04;
    info.online = status != 0x05 && status != 0x06 && status != 0xFF;
    return info;
}

std::optional<hidpp_battery_info> parse_battery_charge(
    const std::vector<uint8_t>& payload) {
    if (payload.size() < 3) return std::nullopt;
    hidpp_battery_info info;
    // HID++ 1.0 register 0x0D (BATTERY_CHARGE): charge percentage is byte 0
    // and the high nibble of byte 2 carries 0x30/0x50/0x90 status values.
    info.level = payload[0] <= 100 ? payload[0] : 255;
    switch (payload[2] & 0xF0) {
    case 0x50: info.charging = true; break;       // recharging
    case 0x30: case 0x90: break;                  // discharging/full
    default: info.online = false; break;          // unknown/invalid state
    }
    return info;
}

std::optional<hidpp_battery_info> parse_legacy_battery_status(
    const uint8_t* payload, size_t size) {
    if (!payload || size < 3) return std::nullopt;
    hidpp_battery_info info;
    // HID++ 1.0 register 0x07 is not a percentage/status tuple.  Solaar
    // decodes byte 0 as an approximation code and byte 1 as the charging
    // flags.  Preserve unknown values as 255 rather than reporting a false
    // empty battery.
    switch (payload[0]) {
    case 7: info.level = 90; break; // FULL
    case 5: info.level = 50; break; // GOOD
    case 3: info.level = 20; break; // LOW
    case 1: info.level = 5; break;  // CRITICAL
    case 0: info.level = 255; break; // no level information
    default: info.level = 255; break;
    }

    const uint8_t charging = payload[1];
    info.charging = (charging & 0x21) == 0x21;
    // A successful register reply means the device is online.  0x07 has no
    // protocol-level offline sentinel; byte 2 is reserved/implementation
    // specific and must not be mistaken for the HID++ 2.0 status byte.
    info.online = true;
    return info;
}

std::optional<hidpp_firmware_record> parse_firmware_record(
    const std::vector<uint8_t>& payload) {
    if (payload.empty()) return std::nullopt;
    hidpp_firmware_record record;
    record.level = payload[0] & 0x0F;
    if (payload.size() >= 4)
        record.name = bounded_string(payload.data() + 1, 3);
    if (payload.size() >= 8) {
        record.major = payload[4];
        record.minor = payload[5];
        record.build = read_be16(payload.data() + 6);
    } else if (record.level == 2 && payload.size() >= 2) {
        // Solaar's hardware-level record is a compact [level, revision]
        // response rather than a normal name/version/build tuple.
        record.major = payload[1];
    }
    // Firmware record metadata after the version is transport flags followed
    // by model IDs.  The unit/model identity block returned by function 0 is
    // parsed separately below; do not treat the firmware name/version bytes
    // as a unit ID.
    if (payload.size() >= 9)
        record.transport_flags = payload[8];
    if (payload.size() >= 15)
        record.model_id = bytes_to_hex(payload.data() + 9, 6);
    const size_t extras_offset = payload.size() >= 8 ? 8 : 1;
    if (payload.size() > extras_offset)
        record.extras_hex = bytes_to_hex(
            payload.data() + extras_offset, payload.size() - extras_offset);
    return record;
}

uint8_t normalize_function_id(uint8_t function_id) {
    // Solaar commonly spells a function as the high nibble of the request-id
    // byte (0x10, 0x50), while this transport API historically also accepted
    // the bare four-bit function (0x1, 0x5).  Keep both forms equivalent at
    // the packet boundary; the software-id is allocated independently.
    return function_id <= 0x0F
        ? function_id
        : static_cast<uint8_t>((function_id >> 4) & 0x0F);
}

} // namespace

std::optional<hidpp_battery_info> hidpp_parse_unified_battery(
    const std::vector<uint8_t>& payload) {
    return parse_unified_battery(payload);
}

std::optional<hidpp_battery_info> hidpp_parse_battery_status(
    const std::vector<uint8_t>& payload) {
    return parse_battery_status_feature(payload);
}

std::optional<hidpp_battery_info> hidpp_parse_battery_charge(
    const std::vector<uint8_t>& payload) {
    return parse_battery_charge(payload);
}

std::optional<hidpp_battery_info> hidpp_parse_legacy_battery(
    const uint8_t* payload, size_t size) {
    return parse_legacy_battery_status(payload, size);
}

std::optional<uint32_t> hidpp_rate_code_to_hz(bool extended, uint8_t code) {
    return rate_code_to_hz(extended, code);
}

std::optional<uint8_t> hidpp_hz_to_rate_code(bool extended, uint32_t hz) {
    return hz_to_rate_code(extended, hz);
}

uint8_t hidpp_normalize_function_id(uint8_t function_id) {
    return normalize_function_id(function_id);
}

bool hidpp_register_uses_long_report(uint16_t request_id) {
    return (request_id & 0xFF00u) == 0x8200u;
}

const char* hidpp_feature_name(uint16_t feature_id) {
    switch (static_cast<hidpp_feature_index>(feature_id)) {
    case hidpp_feature_index::root: return "root";
    case hidpp_feature_index::feature_set: return "feature_set";
    case hidpp_feature_index::feature_info: return "feature_info";
    case hidpp_feature_index::device_fw_version: return "device_fw_version";
    case hidpp_feature_index::device_unit_id: return "device_unit_id";
    case hidpp_feature_index::device_name: return "device_name";
    case hidpp_feature_index::device_groups: return "device_groups";
    case hidpp_feature_index::device_friendly_name: return "device_friendly_name";
    case hidpp_feature_index::keep_alive: return "keep_alive";
    case hidpp_feature_index::wireless_status: return "wireless_status";
    case hidpp_feature_index::device_activity: return "device_activity";
    case hidpp_feature_index::keyboard_info: return "keyboard_info";
    case hidpp_feature_index::mouse_info: return "mouse_info";
    case hidpp_feature_index::sensor_info: return "sensor_info";
    case hidpp_feature_index::gesture: return "gesture";
    case hidpp_feature_index::battery_status: return "battery_status";
    case hidpp_feature_index::unified_battery: return "unified_battery";
    case hidpp_feature_index::battery_voltage: return "battery_voltage";
    case hidpp_feature_index::adjustable_dpi: return "adjustable_dpi";
    case hidpp_feature_index::extended_adjustable_dpi: return "extended_adjustable_dpi";
    case hidpp_feature_index::report_rate: return "report_rate";
    case hidpp_feature_index::extended_adjustable_report_rate:
        return "extended_adjustable_report_rate";
    case hidpp_feature_index::smart_shift: return "smart_shift";
    case hidpp_feature_index::onboard_profiles: return "onboard_profiles";
    case hidpp_feature_index::led_control: return "led_control";
    case hidpp_feature_index::backlight: return "backlight";
    case hidpp_feature_index::backlight2: return "backlight2";
    case hidpp_feature_index::backlight3: return "backlight3";
    case hidpp_feature_index::illumination: return "illumination";
    case hidpp_feature_index::wireless_device_status: return "wireless_device_status";
    default: return "unknown";
    }
}

std::optional<hidpp_feature_metadata> hidpp_parse_feature_metadata(
    uint8_t index, const std::vector<uint8_t>& payload) {
    if (payload.size() < 4) return std::nullopt;
    const uint16_t feature_id = read_be16(payload.data());
    if (feature_id == 0) return std::nullopt;
    return hidpp_feature_metadata{
        feature_id, index, payload[2], payload[3]
    };
}

std::optional<hidpp_firmware_record> hidpp_parse_firmware_record(
    const std::vector<uint8_t>& payload) {
    return parse_firmware_record(payload);
}

std::optional<hidpp_pairing_slot> hidpp_parse_receiver_pairing(
    const std::vector<uint8_t>& payload, uint8_t slot, bool bolt) {
        if (slot == 0) return std::nullopt;
        hidpp_pairing_slot result;
        result.slot = slot;
        if (bolt) {
            // Bolt receiver-info pairing payload:
            // kind, WPID high, WPID low, serial[4...].
            if (payload.size() < 4) return std::nullopt;
            // B1: payload[0] is the echoed subregister byte — it is the
            // requested register/offset and is always non-zero, so the old
            // test reported EVERY Bolt slot as occupied.  The real occupied
            // state lives in the pairing data: kind nibble (payload[1]) and
            // the 16-bit WPID (payload[2..3]).  An empty slot has all zeros
            // there.
            result.occupied = payload[1] != 0 || payload[2] != 0 || payload[3] != 0;
            // Bolt uses a little-endian-looking WPID on the wire: Solaar
            // extracts byte 3 as the high byte and byte 2 as the low byte.
            result.pid = static_cast<uint16_t>(
                (static_cast<uint16_t>(payload[3]) << 8) | payload[2]);
            result.connection_type = 3; // Lightspeed/Bolt receiver link
            if (payload.size() >= 8)
                result.serial = bytes_to_hex(payload.data() + 4, 4);
            return result;
        }

        // Unifying/Nano receiver-info pairing payload.  These offsets are the
        // documented Solaar HID++ 1.0 register layout; the first byte (when
        // present) is the echoed sub-register and is retained in no field.
        if (payload.size() < 8) return std::nullopt;
        result.occupied = payload[3] != 0 || payload[4] != 0;
        result.pid = read_be16(payload.data() + 3);
        result.connection_type = 1; // USB receiver link
        return result;
}

// ── hidpp_short_packet ──────────────────────────────────────────────
std::array<uint8_t, 7> hidpp_short_packet::to_bytes() const {
    std::array<uint8_t, 7> buf;
    buf[0] = report_id;
    buf[1] = device_index;
    buf[2] = feature_index;
    buf[3] = static_cast<uint8_t>(
        (normalize_function_id(function_id) << 4) | (software_id & 0x0F));
    std::memcpy(buf.data() + 4, params, 3);
    return buf;
}

std::optional<hidpp_short_packet> hidpp_short_packet::from_bytes(const uint8_t* data, size_t len) {
    if (!data || len != 7) return std::nullopt;
    hidpp_short_packet pkt;
    pkt.report_id    = data[0];
    pkt.device_index = data[1];
    if (data[0] != 0x10) return std::nullopt;
    pkt.feature_index = data[2];
    pkt.function_id  = static_cast<uint8_t>(data[3] >> 4);
    pkt.software_id   = static_cast<uint8_t>(data[3] & 0x0F);
    std::memcpy(pkt.params, data + 4, 3);
    return pkt;
}

// ── hidpp_long_packet ───────────────────────────────────────────────
std::array<uint8_t, 20> hidpp_long_packet::to_bytes() const {
    std::array<uint8_t, 20> buf;
    buf[0] = report_id;
    buf[1] = device_index;
    buf[2] = feature_index;
    buf[3] = static_cast<uint8_t>(
        (normalize_function_id(function_id) << 4) | (software_id & 0x0F));
    std::memcpy(buf.data() + 4, params, 16);
    return buf;
}

std::optional<hidpp_long_packet> hidpp_long_packet::from_bytes(const uint8_t* data, size_t len) {
    if (!data || len != 20) return std::nullopt;
    hidpp_long_packet pkt;
    pkt.report_id    = data[0];
    pkt.device_index = data[1];
    if (data[0] != 0x11) return std::nullopt;
    pkt.feature_index = data[2];
    pkt.function_id  = static_cast<uint8_t>(data[3] >> 4);
    pkt.software_id   = static_cast<uint8_t>(data[3] & 0x0F);
    std::memcpy(pkt.params, data + 4, 16);
    return pkt;
}

// ── hidpp_very_long_packet ──────────────────────────────────────────
std::array<uint8_t, 64> hidpp_very_long_packet::to_bytes() const {
    std::array<uint8_t, 64> buf;
    buf[0] = report_id;
    buf[1] = device_index;
    buf[2] = feature_index;
    buf[3] = static_cast<uint8_t>(
        (normalize_function_id(function_id) << 4) | (software_id & 0x0F));
    std::memcpy(buf.data() + 4, params, 60);
    return buf;
}

std::optional<hidpp_very_long_packet> hidpp_very_long_packet::from_bytes(const uint8_t* data, size_t len) {
    if (!data || len != 64) return std::nullopt;
    hidpp_very_long_packet pkt;
    pkt.report_id = data[0];
    pkt.device_index = data[1];
    if (data[0] != 0x12) return std::nullopt;
    pkt.feature_index = data[2];
    pkt.function_id = static_cast<uint8_t>(data[3] >> 4);
    pkt.software_id = static_cast<uint8_t>(data[3] & 0x0F);
    std::memcpy(pkt.params, data + 4, 60);
    return pkt;
}

std::optional<hidpp_notification>
hidpp_notification::from_bytes(const uint8_t* data, size_t len) {
    if (!data || (len != 7 && len != 20 && len != 64)) return std::nullopt;
    if (data[0] != 0x10 && data[0] != 0x11 && data[0] != 0x12) return std::nullopt;
    // HID++ errors and register/feature replies have the high bit set in the
    // sub-id byte; they are consumed by the request matcher instead.
    const uint8_t sub_id = data[2];
    if ((sub_id & 0x80) != 0) return std::nullopt;
    const uint8_t address = data[3];
    // HID++ 2.0 notifications use software-id 0. HID++ 1.0 notifications
    // conventionally use sub-ids 0x40..0x7f. Keep the two legacy battery and
    // illumination forms recognized by Solaar as well.
    const bool hidpp10 = sub_id >= 0x40;
    const bool legacy_battery =
        (sub_id == 0x07 || sub_id == 0x0D) && len == 7 && data[6] == 0;
    const bool legacy_illumination = sub_id == 0x17 && len == 7;
    // B2: HID++ 1.0 notifications must not be misclassified as 2.0. 1.0
    // notifications carry sub-ids 0x40..0x7f, and 1.0 CONNECT_DISCONNECT
    // events can also have address & 0x0F == 0 — the old test alone would
    // silently drop them as "fake" 2.0.  Require a genuine 2.0 sub-id
    // (< 0x40, the 2.0 "feature index" range) in addition to address bits.
    const bool hidpp20 = sub_id < 0x40 && (address & 0x0F) == 0;
    if (!hidpp10 && !legacy_battery && !legacy_illumination && !hidpp20)
        return std::nullopt;
    // Reject packets with a zero sub-id before they reach feature maps.
    if (sub_id == 0) return std::nullopt;

    hidpp_notification notification;
    notification.report_id = data[0];
    notification.device_index = data[1];
    notification.sub_id = sub_id;
    notification.address = address;
    notification.feature_index = sub_id;
    notification.type = legacy_battery
        ? hidpp_notification::kind::legacy_battery
        : legacy_illumination
            ? hidpp_notification::kind::legacy_illumination
            : hidpp20
                ? hidpp_notification::kind::hidpp20
                : hidpp_notification::kind::hidpp10;
    notification.payload.assign(data + 4, data + len);
    return notification;
}

bool hidpp_device::supports_feature(uint16_t feature_id) const {
    return feature_index(feature_id).has_value();
}

std::optional<uint8_t> hidpp_device::feature_index(uint16_t feature_id) const {
    const auto it = std::find_if(features.begin(), features.end(),
                                 [feature_id](const auto& item) {
                                     return item.first == feature_id;
                                 });
    if (it == features.end()) return std::nullopt;
    return it->second;
}

std::optional<uint16_t> hidpp_device::feature_id(uint8_t dynamic_index) const {
    const auto it = std::find_if(features.begin(), features.end(),
                                 [dynamic_index](const auto& item) {
                                     return item.second == dynamic_index;
                                 });
    if (it == features.end()) return std::nullopt;
    return it->first;
}

std::optional<uint16_t> hidpp_device::notification_feature_id(
    const hidpp_notification& notification) const {
    if (notification.feature_index == 0) return std::nullopt;
    switch (notification.type) {
    case hidpp_notification::kind::hidpp20:
        return feature_id(notification.feature_index);
    case hidpp_notification::kind::legacy_battery:
        return notification.sub_id;
    case hidpp_notification::kind::legacy_illumination:
        return notification.sub_id;
    case hidpp_notification::kind::hidpp10:
        return static_cast<uint16_t>(notification.sub_id);
    }
    return std::nullopt;
}

// ── HidppTransport ──────────────────────────────────────────────────
HidppTransport::HidppTransport(const std::string& hidraw_path)
    : hidraw_path_(hidraw_path) {
    // ORTA-BUG-TRANSPORT-01: add O_CLOEXEC so the hidraw fd can never leak
    // into a future child process (the GUI lock file already uses O_CLOEXEC).
    fd_ = open(hidraw_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) return;

    // Get device info
    struct hidraw_devinfo info;
    if (ioctl(fd_, HIDIOCGRAWINFO, &info) >= 0) {
        vendor_id_ = info.vendor;
        product_id_ = info.product;
    } else {
        // L-BUG-23: a failed HIDIOCGRAWINFO previously left vendor/product
        // at 0 while the device was still probed as HID++ — receiver gating
        // (get_pairing_info) then silently disabled every receiver register
        // read with no way to tell why.  A hidraw node that cannot describe
        // itself is not a usable HID++ endpoint; treat it as not open so
        // callers report "unable to communicate" instead of guessing.
        close(fd_);
        fd_ = -1;
    }
}

HidppTransport::~HidppTransport() {
    if (fd_ >= 0) close(fd_);
}

void HidppTransport::set_device_index(uint8_t idx) {
    // L-BUG-40: consistent lock order (feature_mutex_ → request_mutex_) with
    // every reader path (resolve_feature_index, get_feature_metadata…).  The
    // device index is an atomic; the mutexes guard the feature caches, so
    // taking feature_mutex_ first can never deadlock against a reader (readers
    // never hold request_mutex_ while acquiring feature_mutex_).
    std::lock_guard feature_lock(feature_mutex_);
    feature_indices_.clear();
    feature_sets_.clear();
    feature_metadata_sets_.clear();
    std::lock_guard lock(request_mutex_);
    device_index_.store(idx, std::memory_order_relaxed);
}

void HidppTransport::clear_feature_cache() {
    // Same three maps set_device_index() clears, made available for the
    // replug / hotplug re-scan path (P168) without changing the target index.
    std::lock_guard feature_lock(feature_mutex_);
    feature_indices_.clear();
    feature_sets_.clear();
    feature_metadata_sets_.clear();
}

uint8_t HidppTransport::next_sw_id() {
    const uint8_t id = static_cast<uint8_t>(next_sw_id_ & 0x0F);
    next_sw_id_ = static_cast<uint8_t>((next_sw_id_ + 1) & 0x0F);
    return id;
}

bool HidppTransport::write_packet(const uint8_t* data, size_t len) {
    if (fd_ < 0 || !data) return false;
    const uint8_t* p = data;
    size_t left = len;
    while (left > 0) {
        ssize_t written = write(fd_, p, left);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        p += written;
        left -= static_cast<size_t>(written);
    }
    return true;
}

bool HidppTransport::read_packet(uint8_t* buf, size_t max_len, size_t& out_len,
                                  std::chrono::milliseconds timeout) {
    out_len = 0;
    if (fd_ < 0 || !buf || max_len == 0) return false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) return false;
        const auto poll_ms = std::min<int64_t>(remaining.count(), INT_MAX);
        struct pollfd pfd = { fd_, POLLIN, 0 };
        int ret;
        do {
            ret = poll(&pfd, 1, static_cast<int>(poll_ms));
        } while (ret < 0 && errno == EINTR);
        if (ret <= 0) return false;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
        const ssize_t n = read(fd_, buf, max_len);
        if (n > 0) {
            out_len = static_cast<size_t>(n);
            return true;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        return false;
    }
}

std::optional<std::vector<uint8_t>> HidppTransport::send_feature_request(
    uint8_t feature_index, uint8_t function_id, const uint8_t* params,
    size_t param_len, std::chrono::milliseconds timeout,
    uint8_t target_device_index) {
    if (param_len > 16) return std::nullopt;
    const uint8_t wire_function = normalize_function_id(function_id);

    std::lock_guard lock(request_mutex_);
    const uint8_t request_device = target_device_index != 0xFF
        ? target_device_index : device_index_.load(std::memory_order_relaxed);
    const uint8_t report_id = param_len > 3 ? 0x11 : 0x10;
    std::array<uint8_t, 20> request{};
    request[0] = report_id;
    request[1] = request_device;
    request[2] = feature_index;
    request[3] = static_cast<uint8_t>(
        (wire_function << 4) | (next_sw_id() & 0x0F));
    if (params && param_len != 0)
        std::memcpy(request.data() + 4, params, param_len);

    const size_t request_len = report_id == 0x11 ? 20 : 7;
    if (!write_packet(request.data(), request_len)) return std::nullopt;

    const uint8_t request_sw_id = request[3] & 0x0F;
    uint8_t buf[64];
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;
        size_t len = 0;
        if (!read_packet(buf, sizeof(buf), len, remaining)) continue;
        if (is_hidpp_error(buf, len)) return std::nullopt;
        if (len != 7 && len != 20 && len != 64) continue;
        if (buf[1] != request_device || buf[2] != feature_index ||
            (buf[3] >> 4) != wire_function ||
            (buf[3] & 0x0F) != request_sw_id) {
            // BUG-22 (aj4): a notification that arrives in the response wait
            // window is not the request's answer — stash it (bounded) instead
            // of discarding it, so drain_notifications() can still deliver it.
            if (auto notif = hidpp_notification::from_bytes(buf, len))
                if (pending_notifications_.size() < 16)
                    pending_notifications_.push_back(std::move(*notif));
            continue;
        }
        const size_t payload_len = len - 4;
        return std::vector<uint8_t>(buf + 4, buf + 4 + payload_len);
    }
    return std::nullopt;
}

std::optional<uint8_t> HidppTransport::resolve_feature_index(
    hidpp_feature_index feature, uint8_t target_device_index) {
    const auto feature_id = static_cast<uint16_t>(feature);
    if (feature_id == 0) return 0;
    const uint8_t target = target_device_index != 0xFF
        ? target_device_index : device_index_.load(std::memory_order_relaxed);
    const uint32_t cache_key = (static_cast<uint32_t>(target) << 16) | feature_id;
    {
        std::lock_guard lock(feature_mutex_);
        const auto it = feature_indices_.find(cache_key);
        if (it != feature_indices_.end()) return it->second;
    }

    const uint8_t params[3] = {
        static_cast<uint8_t>(feature_id >> 8),
        static_cast<uint8_t>(feature_id & 0xFF),
        0
    };
    auto response = send_short(0, 0, params, std::chrono::milliseconds(500),
                               target_device_index);
    if (!response || response->params[0] == 0) return std::nullopt;
    {
        std::lock_guard lock(feature_mutex_);
        feature_indices_[cache_key] = response->params[0];
    }
    return response->params[0];
}

std::optional<hidpp_short_packet> HidppTransport::send_short(
    hidpp_feature_index feature, uint8_t function_id, const uint8_t* params,
    std::chrono::milliseconds timeout, uint8_t target_device_index) {
    const auto index = resolve_feature_index(feature, target_device_index);
    if (!index) return std::nullopt;
    return send_short(*index, function_id, params, timeout, target_device_index);
}

std::optional<hidpp_long_packet> HidppTransport::send_long(
    hidpp_feature_index feature, uint8_t function_id, const uint8_t* params,
    std::chrono::milliseconds timeout, uint8_t target_device_index) {
    const auto index = resolve_feature_index(feature, target_device_index);
    if (!index) return std::nullopt;
    return send_long(*index, function_id, params, timeout, target_device_index);
}

std::optional<hidpp_short_packet> HidppTransport::send_short(
    uint8_t feature_index, uint8_t function_id,
    const uint8_t params[3], std::chrono::milliseconds timeout,
    uint8_t target_device_index) {

    std::lock_guard lock(request_mutex_);
    hidpp_short_packet req;
    req.report_id = 0x10;
    req.device_index = (target_device_index != 0xFF) ? target_device_index :
        device_index_.load(std::memory_order_relaxed);
    req.feature_index = feature_index;
    req.function_id = normalize_function_id(function_id);
    req.software_id = next_sw_id();
    if (params) {
        std::memcpy(req.params, params, sizeof(req.params));
    }

    auto req_bytes = req.to_bytes();
    if (!write_packet(req_bytes.data(), req_bytes.size())) return std::nullopt;

    uint8_t buf[64];
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;

        size_t len = 0;
        if (read_packet(buf, sizeof(buf), len, remaining)) {
            if (is_hidpp_error(buf, len)) return std::nullopt;
            if (auto rsp = hidpp_short_packet::from_bytes(buf, len)) {
                if (rsp->feature_index == req.feature_index &&
                    rsp->function_id == req.function_id &&
                    rsp->software_id == req.software_id &&
                    rsp->device_index == req.device_index) {
                    return rsp;
                }
            }
            // BUG-22 (aj4): a notification that arrives in the response wait
            // window is not this request's answer — stash it (bounded) instead
            // of discarding it, so drain_notifications() can still deliver it.
            if (auto notif = hidpp_notification::from_bytes(buf, len))
                if (pending_notifications_.size() < 16)
                    pending_notifications_.push_back(std::move(*notif));
        }
    }
    return std::nullopt;
}

std::optional<hidpp_long_packet> HidppTransport::send_long(
    uint8_t feature_index, uint8_t function_id,
    const uint8_t params[16], std::chrono::milliseconds timeout,
    uint8_t target_device_index) {

    std::lock_guard lock(request_mutex_);
    hidpp_long_packet req;
    req.report_id = 0x11;
    req.device_index = (target_device_index != 0xFF) ? target_device_index :
        device_index_.load(std::memory_order_relaxed);
    req.feature_index = feature_index;
    req.function_id = normalize_function_id(function_id);
    req.software_id = next_sw_id();
    if (params) {
        std::memcpy(req.params, params, sizeof(req.params));
    }

    auto req_bytes = req.to_bytes();
    if (!write_packet(req_bytes.data(), req_bytes.size())) return std::nullopt;

    uint8_t buf[64];
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;

        size_t len = 0;
        if (read_packet(buf, sizeof(buf), len, remaining)) {
            if (is_hidpp_error(buf, len)) return std::nullopt;
            if (auto rsp = hidpp_long_packet::from_bytes(buf, len)) {
                if (rsp->feature_index == req.feature_index &&
                    rsp->function_id == req.function_id &&
                    rsp->software_id == req.software_id &&
                    rsp->device_index == req.device_index) {
                    return rsp;
                }
            }
            // BUG-22 (aj4): stash notifications that arrive in the wait
            // window instead of dropping them (see send_short).
            if (auto notif = hidpp_notification::from_bytes(buf, len))
                if (pending_notifications_.size() < 16)
                    pending_notifications_.push_back(std::move(*notif));
        }
    }
    return std::nullopt;
}

std::optional<hidpp_very_long_packet> HidppTransport::send_very_long(
    uint8_t feature_index, uint8_t function_id,
    const uint8_t params[60], std::chrono::milliseconds timeout,
    uint8_t target_device_index) {

    std::lock_guard lock(request_mutex_);
    hidpp_very_long_packet req;
    req.report_id = 0x12;
    req.device_index = (target_device_index != 0xFF) ? target_device_index :
        device_index_.load(std::memory_order_relaxed);
    req.feature_index = feature_index;
    req.function_id = normalize_function_id(function_id);
    req.software_id = next_sw_id();
    if (params) {
        std::memcpy(req.params, params, sizeof(req.params));
    }

    auto req_bytes = req.to_bytes();
    if (!write_packet(req_bytes.data(), req_bytes.size())) return std::nullopt;

    uint8_t buf[64];
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;

        size_t len = 0;
        if (read_packet(buf, sizeof(buf), len, remaining)) {
            if (is_hidpp_error(buf, len)) return std::nullopt;
            if (auto rsp = hidpp_very_long_packet::from_bytes(buf, len)) {
                if (rsp->feature_index == req.feature_index &&
                    rsp->function_id == req.function_id &&
                    rsp->software_id == req.software_id &&
                    rsp->device_index == req.device_index) {
                    return rsp;
                }
            }
            // BUG-22 (aj4): stash notifications that arrive in the wait
            // window instead of dropping them (see send_short).
            if (auto notif = hidpp_notification::from_bytes(buf, len))
                if (pending_notifications_.size() < 16)
                    pending_notifications_.push_back(std::move(*notif));
        }
    }
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> HidppTransport::read_register(
    uint16_t register_id, const uint8_t* params, size_t param_len,
    std::chrono::milliseconds timeout, uint8_t target_device_index) {
    // HID++ 1.0 encodes reads as 0x81RR, where RR is the register address.
    // This is deliberately separate from HID++ 2.0 feature requests: byte 3
    // contains the register/sub-register byte, not a function/software-id
    // nibble pair.
    if (register_id > 0x02FF || param_len > 3) return std::nullopt;
    std::lock_guard lock(request_mutex_);
    const uint8_t target = target_device_index != 0xFF
        ? target_device_index : device_index_.load(std::memory_order_relaxed);
    const uint16_t request_id = static_cast<uint16_t>(0x8100 | register_id);
    std::array<uint8_t, 7> request{};
    request[0] = 0x10;
    request[1] = target;
    request[2] = static_cast<uint8_t>(request_id >> 8);
    request[3] = static_cast<uint8_t>(request_id);
    if (params && param_len != 0)
        std::memcpy(request.data() + 4, params, param_len);
    if (!write_packet(request.data(), request.size())) return std::nullopt;

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    uint8_t buf[64];
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;
        size_t len = 0;
        if (!read_packet(buf, sizeof(buf), len, remaining)) continue;
        if (is_hidpp_error(buf, len)) return std::nullopt;
        if ((len != 7 && len != 20) || buf[1] != target ||
            buf[2] != request[2] || buf[3] != request[3])
            continue;
        // Long receiver-info replies echo the sub-register in the first
        // payload byte.  Match it as Solaar does so a stale slot response
        // cannot be returned for a different slot query.  Slots are read
        // from register 0x02B5, which encodes to request_id 0x83B5.
        if ((request_id & 0x00FF) == 0x00B5 && params && param_len != 0 &&
            (len < 5 || buf[4] != params[0]))
            continue;
        return std::vector<uint8_t>(buf + 4, buf + len);
    }
    return std::nullopt;
}

bool HidppTransport::probe_hidpp10(uint8_t target_device_index) {
    // HID++ 1.0 PING: read of register 0x0000.  A success echo (0x81RR) or a
    // register error (0x8FRR) can only be produced by a device that decodes
    // the HID++ register-access sub-ID range; a node that implements no HID++
    // at all never answers, which is how the 046d:c542 Nano receiver behaves.
    std::lock_guard lock(request_mutex_);
    const uint8_t target = target_device_index != 0xFF
        ? target_device_index : device_index_.load(std::memory_order_relaxed);
    std::array<uint8_t, 7> request{};
    request[0] = 0x10;
    request[1] = target;
    request[2] = 0x81;
    request[3] = 0x00;
    if (!write_packet(request.data(), request.size())) return false;

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    uint8_t buf[64];
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;
        size_t len = 0;
        if (!read_packet(buf, sizeof(buf), len, remaining)) continue;
        if ((len != 7 && len != 20) || buf[1] != target) continue;
        if (buf[2] == 0x81 && buf[3] == 0x00) return true; // success echo
        if (buf[2] == 0x8F && buf[3] == 0x00) return true; // register error
    }
    return false;
}

std::optional<std::vector<uint8_t>> HidppTransport::feature_request(
    uint16_t feature_id, uint8_t function_id, const uint8_t* params,
    size_t param_len, std::chrono::milliseconds timeout,
    uint8_t target_device_index) {
    if (feature_id == 0 || param_len > 16)
        return std::nullopt;
    const uint8_t target = target_device_index != 0xFF
        ? target_device_index : device_index_.load(std::memory_order_relaxed);
    // N-09: the capability gate previously enumerated features for the
    // transport's current device_index_, not for the resolved target — an
    // explicit alternate target (e.g. a device behind a receiver) could then
    // have a supported feature wrongly rejected locally.  resolve_feature_index
    // below is already target-correct and returns nullopt for unsupported
    // features, so it is the authoritative gate.
    const auto index = resolve_feature_index(
        static_cast<hidpp_feature_index>(feature_id), target);
    if (!index) return std::nullopt;
    return send_feature_request(*index, function_id, params, param_len,
                                timeout, target);
}

bool HidppTransport::write_register(
    uint16_t register_id, const uint8_t* value, size_t value_len,
    std::chrono::milliseconds timeout, uint8_t target_device_index) {
    // HID++ 1.0 encodes writes as 0x80RR. The 0x82xx request range is the
    // long-register form and must use a long report even for a short value;
    // selecting by value length alone breaks those devices.
    if (register_id > 0x02FF || value_len > 16) return false;
    std::lock_guard lock(request_mutex_);
    const uint8_t target = target_device_index != 0xFF
        ? target_device_index : device_index_.load(std::memory_order_relaxed);
    const uint16_t request_id = static_cast<uint16_t>(0x8000 | register_id);
    const uint8_t report_id =
        (value_len > 3 || hidpp_register_uses_long_report(request_id))
            ? 0x11 : 0x10;
    std::array<uint8_t, 20> request{};
    request[0] = report_id;
    request[1] = target;
    request[2] = static_cast<uint8_t>(request_id >> 8);
    request[3] = static_cast<uint8_t>(request_id);
    if (value && value_len != 0)
        std::memcpy(request.data() + 4, value, value_len);
    const size_t request_len = report_id == 0x11 ? request.size() : 7;
    if (!write_packet(request.data(), request_len)) return false;

    // HID++ 1.0 register writes (0x80RR) are fire-and-forget: no ACK is sent
    // by the device.  Waiting the full deadline below would time out on EVERY
    // write and report false even though the write succeeded.  Return success
    // immediately after a successful write_packet (the kernel accepted the
    // report).
    return true;
}

std::optional<hidpp_notification> HidppTransport::receive_notification(
    std::chrono::milliseconds timeout) {
    if (fd_ < 0) return std::nullopt;
    std::lock_guard lock(request_mutex_);
    const auto read_one = [&](std::chrono::milliseconds wait)
        -> std::optional<hidpp_notification> {
        uint8_t buf[64];
        const auto deadline = std::chrono::steady_clock::now() +
            (wait.count() <= 0 ? std::chrono::milliseconds(1) : wait);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) break;
            size_t len = 0;
            if (!read_packet(buf, sizeof(buf), len, remaining)) break;
            if (auto notification = hidpp_notification::from_bytes(buf, len))
                return notification;
        }
        return std::nullopt;
    };
    return read_one(timeout);
}

std::vector<hidpp_notification> HidppTransport::drain_notifications(
    size_t max_count, std::chrono::milliseconds timeout) {
    std::vector<hidpp_notification> notifications;
    if (max_count == 0 || fd_ < 0) return notifications;
    notifications.reserve(std::min<size_t>(max_count, 32));
    std::lock_guard lock(request_mutex_);
    // BUG-22 (aj4): flush any notifications stashed by send_feature_request()
    // from inside earlier request wait-windows before reading live packets.
    {
        size_t take = std::min(max_count - notifications.size(),
                               pending_notifications_.size());
        if (take > 0) {
            notifications.insert(notifications.end(),
                                 pending_notifications_.begin(),
                                 pending_notifications_.begin() +
                                     static_cast<ptrdiff_t>(take));
            pending_notifications_.erase(
                pending_notifications_.begin(),
                pending_notifications_.begin() +
                    static_cast<ptrdiff_t>(take));
        }
    }
    if (notifications.size() >= max_count) return notifications;
    const auto deadline = std::chrono::steady_clock::now() +
        (timeout.count() <= 0 ? std::chrono::milliseconds(1) : timeout);
    auto read_one = [&](std::chrono::milliseconds wait)
        -> std::optional<hidpp_notification> {
        uint8_t buf[64];
        const auto inner_deadline = std::chrono::steady_clock::now() + wait;
        while (std::chrono::steady_clock::now() < inner_deadline) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                inner_deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) break;
            size_t len = 0;
            if (!read_packet(buf, sizeof(buf), len, remaining)) break;
            if (auto notification = hidpp_notification::from_bytes(buf, len))
                return notification;
        }
        return std::nullopt;
    };
    while (notifications.size() < max_count &&
           std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;
        auto notification = read_one(remaining);
        if (!notification) break;
        notifications.push_back(std::move(*notification));
    }
    return notifications;
}

std::vector<hidpp_feature_metadata> HidppTransport::get_feature_metadata() {
    const uint8_t target = device_index_.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(feature_mutex_);
        const auto it = feature_metadata_sets_.find(target);
        if (it != feature_metadata_sets_.end()) return it->second;
    }
    std::vector<hidpp_feature_metadata> features;

    // ROOT.GetFeature(FEATURE_SET) returns the dynamic FEATURE_SET index,
    // followed by FEATURE_SET.GetCount and FEATURE_SET.GetFeature.
    const uint8_t feature_id[3] = {0x00, 0x01, 0x00};
    auto fs = send_short(0, 0x00, feature_id);
    if (!fs || fs->params[0] == 0) return features;

    {
        std::lock_guard lock(feature_mutex_);
        feature_indices_[(static_cast<uint32_t>(target) << 16) | 0x0001] =
            fs->params[0];
    }
    const uint8_t count_params[3] = {0, 0, 0};
    auto count_response = send_short(fs->params[0], 0x00, count_params,
                                     std::chrono::milliseconds(900), target);
    if (!count_response) return features;

    // Solaar's FEATURE_SET.GetCount response excludes ROOT.  ROOT therefore
    // occupies index 0 and the advertised count needs one added before
    // enumerating dynamic indices.  The fourth byte of GetFeatureId is the
    // feature version, not the dynamic index; the request index is the
    // authoritative metadata.
    const uint16_t count = static_cast<uint16_t>(count_response->params[0]) + 1;
    features.push_back({0x0000, 0, 0, 0});
    features.push_back({0x0001, fs->params[0], 0, 0});
    // A non-responding device would otherwise stall the daemon for minutes
    // (900 ms × up to 256 indices, ~4 min worst case).  Solaar aborts
    // enumeration after a short run of consecutive timeouts; match that so
    // a single unresponsive slot cannot freeze far slower sensors (name, DPI)
    // that follow.  Isolated misses are still skipped and the run continues.
    int consecutive_timeouts = 0;
    const auto deadline = std::chrono::steady_clock::now() + kIdentifyBudget;
    for (uint16_t i = 1; i < count; ++i) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        if (i == fs->params[0]) continue;
        // GetFeatureId has one parameter byte, so the request is short even
        // though devices commonly return its four-byte metadata in a long
        // report.  send_feature_request accepts either response report size.
        const uint8_t index_param = static_cast<uint8_t>(i);
        auto frsp = send_feature_request(
            fs->params[0], 0x01, &index_param, 1,
            std::chrono::milliseconds(900), target);
        const auto metadata = frsp
            ? hidpp_parse_feature_metadata(static_cast<uint8_t>(i), *frsp)
            : std::nullopt;
        if (!metadata) {
            if (++consecutive_timeouts >= kIdentifyTimeoutStreak) break;
            continue;
        }
        consecutive_timeouts = 0;
        const uint16_t fid = metadata->feature_id;
        features.push_back(*metadata);
        std::lock_guard lock(feature_mutex_);
        feature_indices_[(static_cast<uint32_t>(target) << 16) | fid] =
            static_cast<uint8_t>(i);
    }
    {
        std::lock_guard lock(feature_mutex_);
        feature_metadata_sets_[target] = features;
    }
    return features;
}

std::vector<std::pair<uint16_t, uint8_t>> HidppTransport::get_feature_set() {
    const uint8_t target = device_index_.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(feature_mutex_);
        const auto it = feature_sets_.find(target);
        if (it != feature_sets_.end()) return it->second;
    }
    const auto metadata = get_feature_metadata();
    std::vector<std::pair<uint16_t, uint8_t>> features;
    features.reserve(metadata.size());
    for (const auto& item : metadata)
        features.emplace_back(item.feature_id, item.index);
    {
        std::lock_guard lock(feature_mutex_);
        feature_sets_[target] = features;
    }
    return features;
}

bool HidppTransport::supports_feature(uint16_t feature_id) {
    if (feature_id == 0) return false;
    const auto features = get_feature_set();
    return std::find_if(features.begin(), features.end(),
                        [feature_id](const auto& item) {
                            return item.first == feature_id;
                        }) != features.end();
}

std::optional<hidpp_device_info> HidppTransport::get_device_info(uint8_t target_device_index) {
    const auto features = get_feature_set();
    const auto has_feature = [&](hidpp_feature_index feature) {
        const auto id = static_cast<uint16_t>(feature);
        return std::find_if(features.begin(), features.end(),
                            [id](const auto& item) { return item.first == id; })
               != features.end();
    };
    if (!has_feature(hidpp_feature_index::device_fw_version) &&
        !has_feature(hidpp_feature_index::device_name))
        return std::nullopt;

    hidpp_device_info info;
    info.protocol_version = 2;
    info.target = target_device_index != 0xFF
        ? target_device_index : device_index_.load(std::memory_order_relaxed);

    // HID++ 2.0 Device Name: function 0 returns the byte count and function
    // 0x10 reads chunks at an offset.  This is not the legacy 0x000B ID.
    if (has_feature(hidpp_feature_index::device_name)) {
        if (auto length_reply = feature_request(
                static_cast<uint16_t>(hidpp_feature_index::device_name), 0,
                nullptr, 0, std::chrono::milliseconds(500),
                target_device_index);
            length_reply && !length_reply->empty()) {
            const size_t length = std::min<size_t>((*length_reply)[0], 255);
            std::string name;
            name.reserve(length);
            for (size_t offset = 0; offset < length; offset += 16) {
                const uint8_t param = static_cast<uint8_t>(offset);
                auto chunk = feature_request(
                    static_cast<uint16_t>(hidpp_feature_index::device_name),
                    0x10, &param, 1, std::chrono::milliseconds(500),
                    target_device_index);
                if (!chunk || chunk->empty()) break;
                const size_t wanted = std::min<size_t>(16, length - offset);
                name.append(reinterpret_cast<const char*>(chunk->data()),
                            std::min(wanted, chunk->size()));
            }
            info.name = name;
        }
        // Function 0x20 returns the HID++ device kind.
        if (auto kind = feature_request(
                static_cast<uint16_t>(hidpp_feature_index::device_name), 0x20,
                nullptr, 0, std::chrono::milliseconds(500),
                target_device_index);
            kind && !kind->empty())
            info.device_kind = (*kind)[0];
    }

    // DEVICE_FRIENDLY_NAME uses the same chunk function as DEVICE_NAME, but
    // each fragment starts with an echoed offset byte.  That prefix is not
    // part of the UTF-8 name (Solaar deliberately skips it).
    if (has_feature(hidpp_feature_index::device_friendly_name)) {
        if (auto length_reply = feature_request(
                static_cast<uint16_t>(hidpp_feature_index::device_friendly_name),
                0, nullptr, 0, std::chrono::milliseconds(500),
                target_device_index);
            length_reply && !length_reply->empty()) {
            const size_t length = std::min<size_t>((*length_reply)[0], 255);
            std::string name;
            name.reserve(length);
            for (size_t offset = 0; offset < length; offset += 16) {
                const uint8_t param = static_cast<uint8_t>(offset);
                auto chunk = feature_request(
                    static_cast<uint16_t>(hidpp_feature_index::device_friendly_name),
                    0x10, &param, 1, std::chrono::milliseconds(500),
                    target_device_index);
                if (!chunk || chunk->size() < 2) break;
                const size_t wanted = std::min<size_t>(16, length - offset);
                const size_t available = std::min(wanted, chunk->size() - 1);
                name.append(reinterpret_cast<const char*>(chunk->data() + 1),
                            available);
            }
            if (name.size() == length) info.friendly_name = std::move(name);
        }
    }

    // Device Firmware Version: function 0 returns the number of firmware
    // records; function 0x10 returns one record.  Preserve every record for
    // read-only UI/CLI inspection instead of dropping bootloader/hardware
    // metadata after the first entry.
    if (has_feature(hidpp_feature_index::device_fw_version)) {
        if (auto count = feature_request(
                static_cast<uint16_t>(hidpp_feature_index::device_fw_version),
                0, nullptr, 0, std::chrono::milliseconds(700),
                target_device_index);
            count && !count->empty()) {
            // Function 0 carries both the record count and the device
            // identity block.  Solaar's offsets are count[1:5] for unit ID,
            // count[6] for transport flags and count[7:13] for model IDs.
            if (count->size() >= 13) {
                info.unit_id = bytes_to_hex(count->data() + 1, 4);
                info.transport_flags = (*count)[6];
                info.model_id = bytes_to_hex(count->data() + 7, 6);
                if (info.serial.empty()) info.serial = info.unit_id;
                size_t id_offset = 0;
                auto take_id = [&](uint8_t bit) {
                    if ((info.transport_flags & bit) == 0 ||
                        id_offset + 2 > info.model_id.size())
                        return std::string{};
                    const std::string id = info.model_id.substr(id_offset, 2);
                    id_offset += 2;
                    return id;
                };
                info.bluetooth_id = take_id(0x01);
                info.bluetooth_le_id = take_id(0x02);
                info.wireless_pid = take_id(0x04);
                info.usb_id = take_id(0x08);
            }
            // The advertised record count is untrusted (up to 255).  Real
            // devices expose at most a handful of firmware records and a
            // device that stops replying mid-loop would stall the daemon for
            // minutes (700 ms × 255 ≈ 3 min).  Cap the iteration and bail
            // after 3 consecutive timeouts, mirroring the feature-metadata
            // guard.
            const uint8_t record_count = std::min<uint8_t>((*count)[0], 8);
            int consecutive_timeouts = 0;
            for (uint8_t index = 0; index < record_count; ++index) {
                const uint8_t param = index;
                auto record = feature_request(
                    static_cast<uint16_t>(hidpp_feature_index::device_fw_version),
                    0x10, &param, 1, std::chrono::milliseconds(700),
                    target_device_index);
                if (!record) {
                    if (++consecutive_timeouts >= kIdentifyTimeoutStreak) break;
                    continue;
                }
                consecutive_timeouts = 0;
                if (auto parsed = hidpp_parse_firmware_record(*record)) {
                    info.firmware_records.push_back(*parsed);
                    if (info.firmware_name.empty() &&
                        (parsed->level == 0 || parsed->level == 1)) {
                        info.firmware_name = parsed->name;
                        info.firmware_major = parsed->major;
                        info.firmware_minor = parsed->minor;
                        info.firmware_build = parsed->build;
                        info.firmware_version =
                            (static_cast<uint32_t>(parsed->major) << 24) |
                            (static_cast<uint32_t>(parsed->minor) << 16) |
                            parsed->build;
                    }
                }
            }
        }
    }

    // Unit ID is four bytes on devices that expose the separate feature.
    if (has_feature(hidpp_feature_index::device_unit_id)) {
        if (auto unit = feature_request(
                static_cast<uint16_t>(hidpp_feature_index::device_unit_id), 0,
                nullptr, 0, std::chrono::milliseconds(500),
                target_device_index);
            unit && unit->size() >= 4)
            info.unit_id = bytes_to_hex(unit->data(), 4);
    }
    if (info.serial.empty()) info.serial = info.unit_id;
    return info;
}

std::optional<hidpp_battery_info> HidppTransport::get_battery_status(uint8_t target_device_index) {
    // HID++ 1.0 devices expose register 0x07.  Modern HID++ 2.0 devices
    // advertise one of the battery features below instead; prefer advertised
    // features so an unrelated register probe cannot be mistaken for a
    // successful battery read.
    const auto features = get_feature_set();
    const auto has_feature = [&](hidpp_feature_index feature) {
        const auto id = static_cast<uint16_t>(feature);
        return std::find_if(features.begin(), features.end(),
                            [id](const auto& item) { return item.first == id; })
               != features.end();
    };

    // BATTERY_STATUS function 0 and UNIFIED_BATTERY function 0x10 are
    // intentionally separate.  Their first three bytes look similar but the
    // middle byte means "next discharge level" only for BATTERY_STATUS.
    if (has_feature(hidpp_feature_index::battery_status))
        if (auto payload = feature_request(
                static_cast<uint16_t>(hidpp_feature_index::battery_status), 0x00,
                nullptr, 0, std::chrono::milliseconds(500),
                target_device_index)) {
            if (auto info = hidpp_parse_battery_status(*payload)) return info;
        }
    if (has_feature(hidpp_feature_index::unified_battery))
        if (auto payload = feature_request(
                static_cast<uint16_t>(hidpp_feature_index::unified_battery), 0x10,
                nullptr, 0, std::chrono::milliseconds(500),
                target_device_index)) {
            if (auto info = hidpp_parse_unified_battery(*payload)) return info;
        }
    if (has_feature(hidpp_feature_index::battery_voltage))
        if (auto rsp = send_short(hidpp_feature_index::battery_voltage, 0x00,
                                  nullptr, std::chrono::milliseconds(500),
                                  target_device_index)) {
            const uint16_t voltage = read_be16(rsp->params);
            // Same conservative Li-ion voltage curve used by Solaar.  This
            // remains a best-effort percentage because the feature reports
            // millivolts rather than a direct charge percentage.
            const bool charging = (rsp->params[2] & 0x80) != 0;
            return battery_info_from_voltage(voltage, charging);
        }

    // Legacy devices advertise either register.  Solaar probes
    // BATTERY_CHARGE first because it is the more common modern 1.0 layout,
    // then falls back to BATTERY_STATUS.  Keep the parser tied to the
    // register so the two payloads cannot be conflated.
    if (auto rsp = read_register(0x0D, nullptr, 0,
                                 std::chrono::milliseconds(500),
                                 target_device_index)) {
        return hidpp_parse_battery_charge(*rsp);
    }
    if (auto rsp = read_register(0x07, nullptr, 0,
                                 std::chrono::milliseconds(500),
                                 target_device_index)) {
        return hidpp_parse_legacy_battery(rsp->data(), rsp->size());
    }
    return std::nullopt;
}

std::vector<hidpp_pairing_slot> HidppTransport::get_pairing_info(uint8_t target_device_index) {
    std::vector<hidpp_pairing_slot> slots;
    (void)target_device_index;
    // Pairing information is a HID++ 1.0 receiver register, not a function
    // of the HID++ 2.0 Device Firmware Version feature.  Restrict reads to
    // receiver product IDs whose layouts are documented by Solaar.
    const bool bolt = product_id_ == 0xc548;
    uint8_t count = 0;
    if (product_id_ == 0xc517) count = 4;
    else if (product_id_ == 0xc52b || product_id_ == 0xc532 ||
             product_id_ == 0xc548) count = 6;
    else if (product_id_ == 0xc534 || product_id_ == 0xc537) count = 2;
    else if (product_id_ == 0xc518 || product_id_ == 0xc51a ||
             product_id_ == 0xc51b || product_id_ == 0xc521 ||
             product_id_ == 0xc525 || product_id_ == 0xc526 ||
             product_id_ == 0xc52e || product_id_ == 0xc52f ||
             product_id_ == 0xc531 || product_id_ == 0xc535 ||
             product_id_ == 0xc539 ||
             product_id_ == 0xc53a || product_id_ == 0xc53d ||
             product_id_ == 0xc53f || product_id_ == 0xc541 ||
             product_id_ == 0xc545 || product_id_ == 0xc547 ||
             product_id_ == 0xc54d)
        count = 1;
    if (count == 0) return slots;

    // Receiver information reports the actual slot count on standard
    // Unifying/Nano receivers.  Prefer it over the conservative product
    // table, but never probe unknown products.
    if (!bolt) {
        const uint8_t info_subregister = 0x03;
        if (auto receiver_info = read_register(
                0x02B5, &info_subregister, 1,
                std::chrono::milliseconds(900), 0xFF);
            receiver_info && receiver_info->size() >= 7 &&
            (*receiver_info)[6] >= 1 && (*receiver_info)[6] <= 6)
            count = (*receiver_info)[6];
    }

    const auto pairing_deadline =
        std::chrono::steady_clock::now() + kIdentifyBudget;
    int pairing_timeouts = 0;
    for (uint8_t slot = 1; slot <= count; ++slot) {
        if (std::chrono::steady_clock::now() >= pairing_deadline) break;
        const uint8_t subregister = bolt
            ? static_cast<uint8_t>(0x50 + slot)
            : static_cast<uint8_t>(0x20 + slot - 1);
        auto payload = read_register(0x02B5, &subregister, 1,
                                     std::chrono::milliseconds(900), 0xFF);
        if (!payload) {
            if (++pairing_timeouts >= kIdentifyTimeoutStreak) break;
            continue;
        }
        pairing_timeouts = 0;
        if (auto parsed = hidpp_parse_receiver_pairing(*payload, slot, bolt)) {
            if (!bolt && parsed->occupied) {
                const uint8_t name_subregister =
                    static_cast<uint8_t>(0x40 + slot - 1);
                if (auto name_payload = read_register(
                        0x02B5, &name_subregister, 1,
                        std::chrono::milliseconds(900), 0xFF);
                    name_payload && name_payload->size() >= 2) {
                    const size_t length = std::min<size_t>(
                        (*name_payload)[1], name_payload->size() - 2);
                    parsed->name = bounded_string(
                        name_payload->data() + 2, length);
                }
                const uint8_t serial_subregister =
                    static_cast<uint8_t>(0x30 + slot - 1);
                if (auto serial_payload = read_register(
                        0x02B5, &serial_subregister, 1,
                        std::chrono::milliseconds(900), 0xFF);
                    serial_payload && serial_payload->size() >= 5)
                    parsed->serial = bytes_to_hex(
                        serial_payload->data() + 1, 4);
            }
            slots.push_back(std::move(*parsed));
        }
    }
    return slots;
}

std::optional<hidpp_dpi_info> HidppTransport::get_dpi_info(uint8_t target_device_index) {
    const auto request = [&](hidpp_feature_index feature, uint8_t function,
                             const uint8_t* params, size_t length)
        -> std::optional<std::vector<uint8_t>> {
        const auto index = resolve_feature_index(feature, target_device_index);
        if (!index) return std::nullopt;
        return send_feature_request(*index, function, params, length,
                                    std::chrono::milliseconds(700),
                                    target_device_index);
    };

    // EXTENDED_ADJUSTABLE_DPI (0x2202) is preferred: unlike the older feature
    // it carries X/Y DPI and, on supported mice, lift-off distance.  The
    // function IDs and payload offsets below match Solaar's
    // ExtendedAdjustableDpi implementation; LOD is never sent unless the
    // device advertises its LOD capability bit.
    const uint8_t get_caps[3] = {0, 0, 0};
    if (auto caps = request(hidpp_feature_index::extended_adjustable_dpi, 0x1,
                            get_caps, sizeof(get_caps));
        caps && caps->size() >= 3) {
        hidpp_dpi_info info;
        info.extended = true;
        info.supports_y = ((*caps)[2] & 0x01) != 0;
        info.supports_lift_off_distance = ((*caps)[2] & 0x02) != 0;

        if (auto current = request(hidpp_feature_index::extended_adjustable_dpi,
                                   0x5, get_caps, sizeof(get_caps));
            current && current->size() >= 5) {
            const uint16_t current_x = read_be16(&(*current)[1]);
            info.dpi_default = read_be16(&(*current)[3]);
            info.dpi_current = current_x != 0 ? current_x : info.dpi_default;
            if (info.supports_y && current->size() >= 9) {
                const uint16_t current_y = read_be16(&(*current)[5]);
                const uint16_t default_y = read_be16(&(*current)[7]);
                info.dpi_current_y = current_y != 0 ? current_y : default_y;
            }
            if (info.supports_lift_off_distance && current->size() >= 10)
                info.lift_off_distance = (*current)[9];
        } else {
            return std::nullopt;
        }

        // GetDpiList returns chunks.  Each chunk starts with three bytes of
        // feature metadata; the remainder contains big-endian DPI values.
        // A device that never sends the 0x0000 terminator must not burn the
        // full 256 × 700 ms budget (BUG-01): cap with the shared budget and
        // bail after a short timeout streak.
        std::vector<uint8_t> list_bytes;
        const auto dpi_deadline =
            std::chrono::steady_clock::now() + kIdentifyBudget;
        int dpi_timeouts = 0;
        for (uint16_t chunk = 0; chunk < 256; ++chunk) {
            if (std::chrono::steady_clock::now() >= dpi_deadline) break;
            const uint8_t list_params[3] = {0, 0, static_cast<uint8_t>(chunk)};
            auto reply = request(hidpp_feature_index::extended_adjustable_dpi,
                                 0x2, list_params, sizeof(list_params));
            if (!reply || reply->size() <= 3) {
                if (++dpi_timeouts >= kIdentifyTimeoutStreak) break;
                continue;
            }
            dpi_timeouts = 0;
            list_bytes.insert(list_bytes.end(), reply->begin() + 3, reply->end());
            if (list_bytes.size() >= 2 &&
                list_bytes[list_bytes.size() - 1] == 0 &&
                list_bytes[list_bytes.size() - 2] == 0)
                break;
        }
        // The scan helper terminates when the terminator is reached; a
        // 0xE000+ step-marker pair at the tail is also handled by
        // decode_dpi_levels (never pushed raw).
        info.dpi_levels = decode_dpi_levels(list_bytes);
        if (!info.dpi_levels.empty()) {
            info.dpi_min = info.dpi_levels.front();
            info.dpi_max = info.dpi_levels.back();
        }
        return info;
    }

    // ADJUSTABLE_DPI (0x2201) uses one sensor and a five-byte current/default
    // response.  Keep this fallback for the many older Logitech mice.
    if (auto current = request(hidpp_feature_index::adjustable_dpi, 0x2,
                               get_caps, sizeof(get_caps));
        current && current->size() >= 5) {
        hidpp_dpi_info info;
        const uint16_t current_dpi = read_be16(&(*current)[1]);
        info.dpi_default = read_be16(&(*current)[3]);
        info.dpi_current = current_dpi != 0 ? current_dpi : info.dpi_default;
        std::vector<uint8_t> list_bytes;
        const auto dpi_deadline =
            std::chrono::steady_clock::now() + kIdentifyBudget;
        int dpi_timeouts = 0;
        for (uint16_t chunk = 0; chunk < 256; ++chunk) {
            if (std::chrono::steady_clock::now() >= dpi_deadline) break;
            const uint8_t list_params[3] = {0, 0, static_cast<uint8_t>(chunk)};
            auto reply = request(hidpp_feature_index::adjustable_dpi, 0x1,
                                 list_params, sizeof(list_params));
            if (!reply || reply->size() <= 1) {
                if (++dpi_timeouts >= kIdentifyTimeoutStreak) break;
                continue;
            }
            dpi_timeouts = 0;
            list_bytes.insert(list_bytes.end(), reply->begin() + 1, reply->end());
            if (list_bytes.size() >= 2 &&
                list_bytes[list_bytes.size() - 1] == 0 &&
                list_bytes[list_bytes.size() - 2] == 0)
                break;
        }
        // Same encoding as the extended path: 0xE000+ step markers that the
        // legacy fallback previously pushed raw are now expanded correctly.
        info.dpi_levels = decode_dpi_levels(list_bytes);
        if (!info.dpi_levels.empty()) {
            info.dpi_min = info.dpi_levels.front();
            info.dpi_max = info.dpi_levels.back();
        }
        return info;
    }
    return std::nullopt;
}

std::optional<hidpp_onboard_profile_info>
HidppTransport::get_onboard_profile_info(uint8_t target_device_index) {
    const auto reply = feature_request(
        static_cast<uint16_t>(hidpp_feature_index::onboard_profiles), 0,
        nullptr, 0, std::chrono::milliseconds(700), target_device_index);
    if (!reply || reply->size() < 10) return std::nullopt;
    hidpp_onboard_profile_info info;
    info.memory = (*reply)[0];
    info.active_profile = (*reply)[1];
    info.macro_profile = (*reply)[2];
    info.profile_count = (*reply)[3];
    info.out_of_bounds = (*reply)[4];
    info.button_count = (*reply)[5];
    info.sector_count = (*reply)[6];
    info.profile_size = read_be16(&(*reply)[7]);
    info.shift = (*reply)[9];
    // Solaar only accepts the normal onboard-profile memory layout. Reject
    // malformed or unsupported descriptors before any sector access is added.
    if (info.memory != 0x01 || info.profile_count > 0x05 ||
        info.sector_count == 0 || info.profile_size == 0)
        return std::nullopt;
    return info;
}

std::vector<hidpp_onboard_profile_header>
HidppTransport::get_onboard_profile_headers(uint8_t target_device_index) {
    std::vector<hidpp_onboard_profile_header> headers;
    const auto info = get_onboard_profile_info(target_device_index);
    if (!info) return headers;
    auto read_headers = [&](uint8_t storage) {
        std::vector<hidpp_onboard_profile_header> result;
        for (uint8_t i = 0; i < info->profile_count; ++i) {
            // Profile headers are read with ONBOARD_PROFILES fn 0x50, whose
            // params are [memory:0x00 RAM / 0x01 ROM, 0, 0, index*4] (Solaar
            // hidpp20.OnboardProfiles.get_profile_headers).  Solaar also uses
            // fn 0x50 for raw sector reads, so this is not the register
            // style 0x05 — using 0x05 here was silently rejected by devices.
            const uint8_t params[4] = {storage, 0, 0,
                                       static_cast<uint8_t>(i * 4)};
            const auto reply = feature_request(
                static_cast<uint16_t>(hidpp_feature_index::onboard_profiles),
                0x50, params, sizeof(params), std::chrono::milliseconds(700),
                target_device_index);
            if (!reply || reply->size() < 3) return result;
            if ((*reply)[0] == 0xFF && (*reply)[1] == 0xFF) break;
            const uint16_t sector = read_be16(reply->data());
            if (sector == 0 || sector >= info->sector_count) return result;
            result.push_back({
                static_cast<uint8_t>(i + 1), sector, (*reply)[2]
            });
        }
        return result;
    };

    headers = read_headers(0x00);
    if (headers.empty()) {
        // Solaar falls back to ROM headers when RAM is blank (all-zero or
        // erased 0xFF marker) on devices that keep profiles in flash.
        headers = read_headers(0x01);
    }
    return headers;
}

std::optional<std::vector<uint8_t>>
HidppTransport::read_onboard_profile_sector(
    uint16_t sector, size_t size, uint8_t target_device_index) {
    const auto info = get_onboard_profile_info(target_device_index);
    if (!info || sector == 0 || sector >= info->sector_count ||
        size == 0 || size > info->profile_size || size > 4096)
        return std::nullopt;

    std::vector<uint8_t> result;
    result.reserve(size);
    for (size_t offset = 0; offset < size; offset += 16) {
        // Sectors are read with ONBOARD_PROFILES fn 0x50 (Solaar's
        // read_sector), params [sector_hi, sector_lo, offset_hi, offset_lo].
        const uint8_t params[4] = {
            static_cast<uint8_t>(sector >> 8),
            static_cast<uint8_t>(sector),
            static_cast<uint8_t>(offset >> 8),
            static_cast<uint8_t>(offset)
        };
        const auto reply = feature_request(
            static_cast<uint16_t>(hidpp_feature_index::onboard_profiles),
            0x50, params, sizeof(params), std::chrono::milliseconds(700),
            target_device_index);
        if (!reply || reply->empty()) return std::nullopt;
        const size_t wanted = std::min<size_t>(16, size - offset);
        if (reply->size() < wanted) return std::nullopt;
        result.insert(result.end(), reply->begin(), reply->begin() + wanted);
    }
    return result;
}

bool HidppTransport::set_dpi(uint16_t dpi, uint8_t target_device_index) {
    const auto info = get_dpi_info(target_device_index);
    if (!info || dpi == 0) return false;
    // Solaar only exposes this setting after GetDpiList succeeds.  Do the
    // same here instead of sending an arbitrary value to model firmware.
    if (info->dpi_levels.empty() ||
        std::find(info->dpi_levels.begin(), info->dpi_levels.end(), dpi) ==
            info->dpi_levels.end())
        return false;
    // P-BUG-1: do not refuse the whole DPI change just because the advertised
    // LOD byte is out of the valid range (low=0, medium=1, high=2).  Clamp it
    // to the highest valid value instead, matching set_lift_off_distance().
    const auto index = resolve_feature_index(
        info->extended ? hidpp_feature_index::extended_adjustable_dpi
                       : hidpp_feature_index::adjustable_dpi,
        target_device_index);
    if (!index) return false;

    if (info->extended) {
        const uint16_t y = info->supports_y && info->dpi_current_y
            ? info->dpi_current_y : dpi;
        const uint8_t params[6] = {
            0, static_cast<uint8_t>(dpi >> 8), static_cast<uint8_t>(dpi),
            static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y),
            static_cast<uint8_t>(info->supports_lift_off_distance
                ? std::min<uint16_t>(info->lift_off_distance, 2) : 0)
        };
        return send_feature_request(*index, 0x6, params, sizeof(params),
                                    std::chrono::milliseconds(700),
                                    target_device_index).has_value();
    }
    const uint8_t params[3] = {
        0, static_cast<uint8_t>(dpi >> 8), static_cast<uint8_t>(dpi)
    };
    return send_feature_request(*index, 0x3, params, sizeof(params),
                                std::chrono::milliseconds(700),
                                target_device_index).has_value();
}

std::optional<uint32_t> HidppTransport::get_polling_rate(uint8_t target_device_index) {
    // BUG-23 (aj2): query EXTENDED_ADJUSTABLE_REPORT_RATE FIRST.  A device that
    // advertises both 0x8060 and 0x8061 may have been configured to >1000 Hz
    // through the extended feature; the legacy REPORT_RATE read can only return
    // code 1 (1000 Hz), silently under-reporting.  Solaar probes extended first.
    if (auto index = resolve_feature_index(
            hidpp_feature_index::extended_adjustable_report_rate,
            target_device_index)) {
        if (auto reply = send_feature_request(*index, 0x2, nullptr, 0,
                                              std::chrono::milliseconds(500),
                                              target_device_index);
            reply && !reply->empty())
            return rate_code_to_hz(true, (*reply)[0]);
    }
    if (auto index = resolve_feature_index(hidpp_feature_index::report_rate,
                                            target_device_index)) {
        if (auto reply = send_feature_request(*index, 0x1, nullptr, 0,
                                              std::chrono::milliseconds(500),
                                              target_device_index);
            reply && !reply->empty())
            if (auto hz = rate_code_to_hz(false, (*reply)[0])) return hz;
    }
    return std::nullopt;
}

bool HidppTransport::set_polling_rate(uint32_t hz, uint8_t target_device_index) {
    if (auto code = hz_to_rate_code(false, hz)) {
        if (auto index = resolve_feature_index(hidpp_feature_index::report_rate,
                                               target_device_index)) {
            // REPORT_RATE.GetSupportedRates returns one bit per 1..8 ms
            // setting.  Refuse values not advertised by the device.
            if (auto supported = send_feature_request(
                    *index, 0x0, nullptr, 0, std::chrono::milliseconds(500),
                    target_device_index);
                supported && !supported->empty() &&
                (((*supported)[0] >> (*code - 1)) & 0x01) != 0) {
            const uint8_t params[3] = {*code, 0, 0};
            if (send_feature_request(*index, 0x2, params, sizeof(params),
                                     std::chrono::milliseconds(500),
                                     target_device_index))
                return true;
            }
        }
    }
    if (auto code = hz_to_rate_code(true, hz)) {
        if (auto index = resolve_feature_index(
                hidpp_feature_index::extended_adjustable_report_rate,
                target_device_index)) {
            // EXTENDED_ADJUSTABLE_REPORT_RATE.GetSupportedRates returns a
            // big-endian bit mask for codes 0..6.
            if (auto supported = send_feature_request(
                    *index, 0x1, nullptr, 0, std::chrono::milliseconds(500),
                    target_device_index);
                supported && supported->size() >= 2 &&
                ((static_cast<uint16_t>((*supported)[0]) << 8 |
                  (*supported)[1]) & (static_cast<uint16_t>(1) << *code)) != 0) {
            const uint8_t params[3] = {*code, 0, 0};
            return send_feature_request(*index, 0x3, params, sizeof(params),
                                        std::chrono::milliseconds(500),
                                        target_device_index).has_value();
            }
        }
    }
    return false;
}

std::optional<hidpp_lift_off_distance>
HidppTransport::get_lift_off_distance(uint8_t target_device_index) {
    const auto info = get_dpi_info(target_device_index);
    if (!info || !info->supports_lift_off_distance ||
        info->lift_off_distance > static_cast<uint8_t>(hidpp_lift_off_distance::high))
        return std::nullopt;
    return static_cast<hidpp_lift_off_distance>(info->lift_off_distance);
}

bool HidppTransport::set_lift_off_distance(hidpp_lift_off_distance distance,
                                           uint8_t target_device_index) {
    const auto info = get_dpi_info(target_device_index);
    if (!info || !info->extended || !info->supports_lift_off_distance ||
        distance > hidpp_lift_off_distance::high)
        return false;
    const auto index = resolve_feature_index(hidpp_feature_index::extended_adjustable_dpi,
                                             target_device_index);
    if (!index) return false;
    const uint16_t y = info->supports_y && info->dpi_current_y
        ? info->dpi_current_y : info->dpi_current;
    const uint8_t params[6] = {
        0, static_cast<uint8_t>(info->dpi_current >> 8),
        static_cast<uint8_t>(info->dpi_current),
        static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y),
        static_cast<uint8_t>(distance)
    };
    return send_feature_request(*index, 0x6, params, sizeof(params),
                                std::chrono::milliseconds(700),
                                target_device_index).has_value();
}

// ── Aşama 1: multi-host (Easy-Switch), LED, onboard write, buttons ─────────

std::optional<HidppTransport::hidpp_host_info>
HidppTransport::get_change_host_info(uint8_t target_device_index) {
    auto index = resolve_feature_index(hidpp_feature_index::change_host,
                                       target_device_index);
    if (!index) return std::nullopt;
    if (auto reply = send_feature_request(*index, 0x00, nullptr, 0,
                                          std::chrono::milliseconds(500),
                                          target_device_index);
        reply && reply->size() >= 2) {
        hidpp_host_info info;
        info.current_host = (*reply)[0];
        info.host_count   = (*reply)[1];
        info.flags        = reply->size() > 2 ? (*reply)[2] : 0;
        return info;
    }
    return std::nullopt;
}

bool HidppTransport::set_change_host(uint8_t host_index,
                                     uint8_t target_device_index) {
    auto index = resolve_feature_index(hidpp_feature_index::change_host,
                                       target_device_index);
    if (!index) return false;
    // 0x10 set_current_host.  Solaar reserves one byte for the host parameter.
    const uint8_t params[1] = { host_index };
    return send_feature_request(*index, 0x10, params, sizeof(params),
                                std::chrono::milliseconds(700),
                                target_device_index).has_value();
}

uint16_t HidppTransport::led_feature_id(uint8_t target_device_index) {
    // Priority mirrors Solaar: brightness_control (0x8040) is the simple
    // modern path, then backlight2 (0x1982), then legacy backlight (0x1981).
    static const hidpp_feature_index prio[] = {
        hidpp_feature_index::brightness_control,
        hidpp_feature_index::backlight2,
        hidpp_feature_index::backlight,
    };
    for (auto f : prio) {
        if (resolve_feature_index(f, target_device_index))
            return static_cast<uint16_t>(f);
    }
    return 0;
}

std::optional<std::pair<bool, uint8_t>>
HidppTransport::get_led_brightness(uint8_t target_device_index) {
    const uint16_t fid = led_feature_id(target_device_index);
    if (fid == 0) return std::nullopt;
    // 0x30 get_led_info (brightness_control) / 0x00 get_config / 0x00 get_info.
    // Solaar: every LED feature exposes the current level via fn 0x00.
    const auto index = resolve_feature_index(
        static_cast<hidpp_feature_index>(fid), target_device_index);
    if (!index) return std::nullopt;
    if (auto reply = send_feature_request(*index, 0x00, nullptr, 0,
                                          std::chrono::milliseconds(500),
                                          target_device_index);
        reply && !reply->empty()) {
        // Byte layout varies: 0x8040 → [0]=1 supported,[1]=current,[2]=…;
        // 0x1982 → [0]=1, [1]=current state bit and [3]=brightness.
        const bool supported = ((*reply)[0] & 0x01) != 0;
        uint8_t value = 0;
        if (fid == static_cast<uint16_t>(hidpp_feature_index::brightness_control))
            value = reply->size() > 1 ? (*reply)[1] : 0;
        else if (fid == static_cast<uint16_t>(hidpp_feature_index::backlight2))
            value = reply->size() > 3 ? (*reply)[3] : 0;
        else
            value = reply->size() > 1 ? (*reply)[1] : 0;
        return std::make_pair(supported, value);
    }
    return std::nullopt;
}

bool HidppTransport::set_led_brightness(uint8_t brightness,
                                        uint8_t target_device_index) {
    const uint16_t fid = led_feature_id(target_device_index);
    if (fid == 0 || brightness > 100) return false;
    const auto index = resolve_feature_index(
        static_cast<hidpp_feature_index>(fid), target_device_index);
    if (!index) return false;
    // 0x8040: fn 0x10 set_led_power.  0x1982: fn 0x10 set_brightness.
    // 0x1981: fn 0x10 set_brightness.  Payload = single brightness byte.
    const uint8_t params[1] = { brightness };
    return send_feature_request(*index, 0x10, params, sizeof(params),
                                std::chrono::milliseconds(700),
                                target_device_index).has_value();
}

std::vector<std::pair<uint8_t, uint16_t>>
HidppTransport::get_reprog_controls(uint8_t target_device_index) {
    std::vector<std::pair<uint8_t, uint16_t>> out;
    auto index = resolve_feature_index(hidpp_feature_index::reprog_controls_v4,
                                       target_device_index);
    if (!index) return out;
    // 0x1B04 fn=0x00 get_capabilities returns controlCount in byte 1.
    auto caps = send_feature_request(*index, 0x00, nullptr, 0,
                                     std::chrono::milliseconds(500),
                                     target_device_index);
    if (!caps || caps->size() < 2) return out;
    const uint8_t count = std::min<uint8_t>((*caps)[1], 32);
    for (uint8_t ctrl = 0; ctrl < count; ++ctrl) {
        // fn=0x10 get_control_info → control_id in byte 0, HID usage in 1..2.
        auto info = send_feature_request(*index, 0x10, &ctrl, 1,
                                         std::chrono::milliseconds(500),
                                         target_device_index);
        if (info && info->size() >= 3) {
            const uint16_t usage = (static_cast<uint16_t>((*info)[1]) << 8) |
                                   (*info)[2];
            out.emplace_back((*info)[0], usage);
        }
    }
    return out;
}

bool HidppTransport::write_onboard_profile_sector(
    uint16_t sector, const std::vector<uint8_t>& data,
    uint8_t target_device_index) {
    // 0x8100 OnboardProfiles — write a single flash sector.
    // fn=0x48 write_sector: params = sector (u16 BE) + up to 16 bytes.
    auto index = resolve_feature_index(hidpp_feature_index::onboard_profiles,
                                       target_device_index);
    if (!index) return false;
    const size_t chunk = 16;
    for (size_t off = 0; off < data.size(); off += chunk) {
        std::vector<uint8_t> params(2 + chunk, 0);
        params[0] = static_cast<uint8_t>(sector >> 8);
        params[1] = static_cast<uint8_t>(sector);
        const size_t n = std::min(chunk, data.size() - off);
        std::copy(data.begin() + off, data.begin() + off + n, params.begin() + 2);
        if (!send_feature_request(*index, 0x48, params.data(),
                                  static_cast<uint8_t>(params.size()),
                                  std::chrono::milliseconds(500),
                                  target_device_index))
            return false;
    }
    return true;
}

// ── Discovery & identification ──────────────────────────────────────
std::vector<std::string> discover_logitech_hidraw_devices(const char* root) {
    std::vector<std::string> result;
    std::error_code ec;
    fs::directory_iterator it(root, ec), end;
    while (!ec && it != end) {
        const fs::path p = it->path();
        ++it;
        std::error_code fec;
        if (!fs::is_character_file(p, fec)) continue;
        std::string name = p.filename().string();
        if (name.rfind("hidraw", 0) != 0) continue;

        std::string path = p.string();
        // ORTA-BUG-TRANSPORT-02: add O_CLOEXEC to the discovery ioctl fd too.
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) >= 0) {
            if (info.vendor == 0x046d) { // Logitech vendor ID
                result.push_back(path);
            }
        }
        close(fd);
    }
    return result;
}

static std::string hidraw_export_name(const std::string& path) {
    char buf[256] = {};
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::string();
    if (ioctl(fd, HIDIOCGRAWNAME(sizeof(buf) - 1), buf) < 0)
        buf[0] = '\0';
    close(fd);
    return std::string(buf);
}

std::optional<hidpp_device> identify_logitech_device(const std::string& hidraw_path) {
    HidppTransport transport(hidraw_path);
    if (!transport.is_open()) return std::nullopt;

    // Receivers normally use 0xFF, while directly connected peripherals
    // commonly use 0x00. Probe both without assuming the endpoint topology.
    auto features = transport.get_feature_set();
    uint8_t target = transport.device_index();
    if (features.empty() && target != 0x00) {
        transport.set_device_index(0x00);
        features = transport.get_feature_set();
        target = 0x00;
    }

    hidpp_device device;
    device.hidraw_path = hidraw_path;
    device.vendor_id = transport.vendor_id();
    device.product_id = transport.product_id();
    device.device_index = target;

    if (!features.empty()) {
        auto info = transport.get_device_info(target);
        if (!info) return std::nullopt;
        device.info = *info;
        device.features = std::move(features);
        device.feature_metadata = transport.get_feature_metadata();
        device.connected = true;
        return device;
    }

    // No HID++ 2.0 feature index.  Try the legacy HID++ 1.0 protocol before
    // giving up: older wired mice (G700/G7-era, M-series) answer register
    // reads but never expose a feature set.  The receiver always answers on
    // 0xFF; a direct 1.0 peripheral answers on 0x00.
    uint8_t v10_target = 0xFF;
    bool v10 = transport.probe_hidpp10(0xFF);
    if (!v10 && transport.probe_hidpp10(0x00)) {
        v10 = true;
        v10_target = 0x00;
    }
    if (v10) {
        device.device_index = v10_target;
        device.info.protocol_version = 1;
        device.info.target = v10_target;
        // Kernel-exposed HID name; 1.0 register 0x0005 is not a reliable name
        // source across devices.
        device.info.name = hidraw_export_name(hidraw_path);
        device.connected = true;
        return device;
    }

    // A Logitech hidraw node with neither protocol (e.g. the 046d:c542 Nano
    // receiver, which implements no HID++ at all).  Surface it with
    // protocol_version 0 so the GUI/CLI can explain the limitation instead of
    // showing an empty panel.  Daemon callers filter these out.
    device.info.protocol_version = 0;
    device.info.target = target;
    device.info.name = hidraw_export_name(hidraw_path);
    device.connected = false;
    return device;
}

// ── High-level notification classification (SOLAAR FAZ-B / P168) ────────────

hidpp_notification_event classify_hidpp_notification(
    const hidpp_device& device, const hidpp_notification& notification) {
    hidpp_notification_event event;
    event.device_index = notification.device_index;
    event.payload = notification.payload;

    switch (notification.type) {
    case hidpp_notification::kind::legacy_battery:
        // HID++ 1.0 register 0x0D (BATTERY_CHARGE) carries a direct charge
        // percentage; register 0x07 (BATTERY_STATUS) carries Solaar's
        // approximation code + charging flags.  Match the GUI panel's split.
        event.feature_id = notification.sub_id;
        event.feature_name =
            notification.sub_id == 0x0D ? "battery_charge" : "battery_status";
        event.kind = hidpp_notification_event_kind::battery;
        if (notification.sub_id == 0x0D)
            event.battery = hidpp_parse_battery_charge(notification.payload);
        else
            event.battery = hidpp_parse_legacy_battery(
                notification.payload.data(), notification.payload.size());
        break;

    case hidpp_notification::kind::legacy_illumination:
        // HID++ 1.0 register 0x17 (ILLUMINATION): lighting changed.
        event.feature_id = notification.sub_id;
        event.feature_name = "illumination";
        event.kind = hidpp_notification_event_kind::illumination;
        break;

    case hidpp_notification::kind::hidpp10: {
        // Sub-IDs 0x40–0x4B are HID++ 1.0 / DJ link notifications (Solaar
        // Notification enum): CONNECT_DISCONNECT, DJ_PAIRING, CONNECTED,
        // POWER.  Everything else is dropped as uninteresting.
        event.feature_id = static_cast<uint16_t>(notification.sub_id);
        switch (notification.sub_id) {
        case 0x40: // CONNECT_DISCONNECT (unpairing)
            event.feature_name = "connect_disconnect";
            event.kind = hidpp_notification_event_kind::connection;
            event.connected = false;
            break;
        case 0x41: // DJ_PAIRING (link established unless the 0x40 flag is set)
            event.feature_name = "dj_pairing";
            event.kind = hidpp_notification_event_kind::connection;
            event.connected =
                (notification.payload.empty() ||
                 (notification.payload[0] & 0x40) == 0);
            break;
        case 0x42: // CONNECTED (connected = address bit 0 clear)
            event.feature_name = "connected";
            event.kind = hidpp_notification_event_kind::connection;
            event.connected = (notification.address & 0x01) == 0;
            break;
        case 0x4B: // POWER (powered on)
            event.feature_name = "power";
            event.kind = hidpp_notification_event_kind::connection;
            event.connected = true;
            break;
        default:
            event.kind = hidpp_notification_event_kind::unhandled;
            break;
        }
        break;
    }

    case hidpp_notification::kind::hidpp20: {
        const auto feature_id = device.notification_feature_id(notification);
        if (!feature_id) {
            // Dynamic index is not allocated on this device (stale feature
            // cache after a replug, or a notification from an unknown device).
            // Do not guess: report unhandled so the replug re-scan path can
            // clear the cache and re-identify.
            event.kind = hidpp_notification_event_kind::unhandled;
            break;
        }
        event.feature_id = *feature_id;
        event.feature_name = hidpp_feature_name(*feature_id);
        switch (static_cast<hidpp_feature_index>(*feature_id)) {
        case hidpp_feature_index::battery_status:
            event.kind = hidpp_notification_event_kind::battery;
            event.battery = hidpp_parse_battery_status(notification.payload);
            break;
        case hidpp_feature_index::unified_battery:
            event.kind = hidpp_notification_event_kind::battery;
            event.battery = hidpp_parse_unified_battery(notification.payload);
            break;
        case hidpp_feature_index::battery_voltage:
            event.kind = hidpp_notification_event_kind::battery;
            if (notification.payload.size() >= 3) {
                const uint16_t voltage = read_be16(notification.payload.data());
                const bool charging = (notification.payload[2] & 0x80) != 0;
                event.battery = battery_info_from_voltage(voltage, charging);
            }
            break;
        case hidpp_feature_index::wireless_device_status:
            // The device is alive — it just reported being present.  The
            // 0x1D4B feature carries software-reconfig and powered-on flags;
            // any live notification means the link is up.
            event.kind = hidpp_notification_event_kind::connection;
            event.connected = true;
            break;
        case hidpp_feature_index::led_control:
        case hidpp_feature_index::backlight:
        case hidpp_feature_index::backlight2:
        case hidpp_feature_index::backlight3:
        case hidpp_feature_index::illumination:
            event.kind = hidpp_notification_event_kind::illumination;
            break;
        default:
            event.kind = hidpp_notification_event_kind::generic;
            break;
        }
        break;
    }

    default:
        event.kind = hidpp_notification_event_kind::unhandled;
        break;
    }

    return event;
}

size_t drain_hidpp_notifications(
    HidppTransport& transport, const hidpp_device& device, size_t max_count,
    const std::function<void(const hidpp_notification_event&)>& on_event) {
    const auto notifications = transport.drain_notifications(
        max_count, std::chrono::milliseconds(0));
    size_t delivered = 0;
    for (const auto& notification : notifications) {
        const auto event = classify_hidpp_notification(device, notification);
        if (event.kind == hidpp_notification_event_kind::unhandled) continue;
        if (on_event) on_event(event);
        ++delivered;
    }
    return delivered;
}

} // namespace rawaccel