#pragma once

#include <string>
#include <vector>
#include <array>
#include <deque>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <optional>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace rawaccel {

enum class hidpp_report_type : uint8_t {
    short_msg  = 0x10, // 7-byte short report
    long_msg   = 0x11, // 20-byte long report
    very_long  = 0x12, // 64-byte very long report (HID++ 2.0)
};

enum class hidpp_feature_index : uint16_t {
    root                = 0x0000,
    feature_set         = 0x0001,
    feature_info        = 0x0002,
    device_fw_version   = 0x0003,
    // Compatibility name retained for callers that used the old, incorrect
    // "device info" label.  HID++ 2.0 defines 0x0003 as Device Firmware
    // Version, not a generic device-information feature.
    device_info         = device_fw_version,
    device_unit_id      = 0x0004,
    device_name         = 0x0005,
    device_groups       = 0x0006,
    device_friendly_name = 0x0007,
    keep_alive          = 0x0008,
    battery_status      = 0x1000,
    wireless_status     = 0x0080,
    // Legacy names below are kept only for the feature-name table.  They are
    // not used as HID++ 2.0 aliases by the transport.
    device_activity     = 0x000A,
    keyboard_info       = 0x0012,
    mouse_info          = 0x0013,
    sensor_info         = 0x0014,
    gesture             = 0x0015,
    // HID++ 2.0 feature IDs used by Solaar's validated settings:
    // AdjustableDpi, ExtendedAdjustableDpi, ReportRate, and ExtendedReportRate.
    adjustable_dpi      = 0x2201,
    extended_adjustable_dpi = 0x2202, // X/Y DPI + capability-gated LOD
    report_rate         = 0x8060,     // 1..8 ms report periods
    extended_adjustable_report_rate = 0x8061, // 8 ms..125 us
    smart_shift         = 0x4C00,
    onboard_profiles    = 0x8100,
    // HID++ 2.0 battery features used by modern Logitech devices.
    // Unified Battery is a separate feature from BATTERY_STATUS.  The
    // distinction matters: the two features use different functions and
    // payload layouts even though older callers used the same alias.
    unified_battery     = 0x1004,
    battery_voltage     = 0x1001,
    unified_battery_v2  = unified_battery,
    // Illumination / LED feature IDs (Solaar SupportedFeature table) so the
    // notification handler can classify lighting events.
    led_control         = 0x1300,
    backlight           = 0x1981,
    backlight2          = 0x1982,
    backlight3          = 0x1983,
    illumination        = 0x1990,
    // Wireless device status — connection == data[1] == 1 (software reconfig
    // request) or data[2] == 1 (powered on); the handler treats any
    // WIRELESS_DEVICE_STATUS notification as a signed-on device.
    wireless_device_status = 0x1D4B,
    // ── Aşama 1: Easy-Switch / LED / Button / Superstrike ─────────────────
    change_host              = 0x1814, // multi-host switching (1=host, fn=write)
    reprog_controls_v4       = 0x1B04, // reprogrammable keys + divert
    superstrike_tuning       = 0x1B0C, // hall-effect trigger tuning
    persistent_remappable_action = 0x1C00, // persistent per-key remap
    brightness_control       = 0x8040, // simple brightness control
    rgb_effects              = 0x8071, // RGB zone effects
    // Aşama 2: DPI sliding — DPISliding is a sub-function of adjustable_dpi
};

struct hidpp_short_packet {
    uint8_t report_id    = 0x10;
    uint8_t device_index = 0xFF;
    uint8_t feature_index = 0;
    uint8_t function_id = 0;
    uint8_t software_id = 0;
    uint8_t params[3] = {0, 0, 0};

    std::array<uint8_t, 7> to_bytes() const;
    static std::optional<hidpp_short_packet> from_bytes(const uint8_t* data, size_t len);
};

struct hidpp_long_packet {
    uint8_t report_id    = 0x11;
    uint8_t device_index = 0xFF;
    uint8_t feature_index = 0;
    uint8_t function_id = 0;
    uint8_t software_id = 0;
    uint8_t params[16] = {};

    std::array<uint8_t, 20> to_bytes() const;
    static std::optional<hidpp_long_packet> from_bytes(const uint8_t* data, size_t len);
};

struct hidpp_very_long_packet {
    uint8_t report_id = 0x12;
    uint8_t device_index = 0xFF;
    uint8_t feature_index = 0;
    uint8_t function_id = 0;
    uint8_t software_id = 0;
    uint8_t params[60] = {};

    std::array<uint8_t, 64> to_bytes() const;
    static std::optional<hidpp_very_long_packet> from_bytes(const uint8_t* data, size_t len);
};

struct hidpp_notification {
    enum class kind : uint8_t {
        hidpp10,
        hidpp20,
        legacy_battery,
        legacy_illumination,
    };

    uint8_t report_id = 0;
    uint8_t device_index = 0xFF;
    uint8_t sub_id = 0;
    uint8_t address = 0;
    uint8_t feature_index = 0;
    kind type = kind::hidpp20;
    std::vector<uint8_t> payload;

    /// Parse a standard HID++ notification. Request replies and HID++ errors
    /// return nullopt, leaving callers free to route them to the request path.
    static std::optional<hidpp_notification>
    from_bytes(const uint8_t* data, size_t len);
};

struct hidpp_firmware_record {
    uint8_t level = 0;
    std::string name;
    uint8_t major = 0;
    uint8_t minor = 0;
    uint16_t build = 0;
    uint8_t transport_flags = 0;
    std::string unit_id;
    std::string model_id;
    std::string extras_hex;
};

struct hidpp_device_info {
    // 2 = HID++ 2.0 feature set discovered, 1 = HID++ 1.0 register protocol
    // (no feature index), 0 = Logitech hidraw node that implements neither
    // (e.g. the 046d:c542 Nano receiver).  protocol_version 0 devices are
    // listed for display only; HID++ hardware controls never apply.
    uint8_t protocol_version = 0;
    uint8_t target           = 0;
    uint16_t pid             = 0;
    std::string name;
    std::string friendly_name;
    std::string serial;
    uint32_t firmware_version = 0;
    std::string firmware_name;
    std::string unit_id;
    std::string model_id;
    uint8_t firmware_major = 0;
    uint8_t firmware_minor = 0;
    uint16_t firmware_build = 0;
    uint8_t transport_flags = 0;
    uint8_t device_kind = 0;
    // Transport-specific identifiers are packed consecutively in the
    // DEVICE_FW_VERSION record.  Keep both the raw model id and the decoded
    // IDs so callers never have to guess offsets from transport flags.
    std::string bluetooth_id;
    std::string bluetooth_le_id;
    std::string wireless_pid;
    std::string usb_id;
    std::vector<hidpp_firmware_record> firmware_records;
};

struct hidpp_feature_metadata {
    uint16_t feature_id = 0;
    uint8_t index = 0;
    uint8_t flags = 0;
    uint8_t version = 0;
};

struct hidpp_battery_info {
    uint8_t level = 0;          // 0-100, 255 = unknown
    bool charging = false;
    bool online   = true;
};

std::optional<hidpp_battery_info> hidpp_parse_unified_battery(
    const std::vector<uint8_t>& payload);
std::optional<hidpp_battery_info> hidpp_parse_battery_status(
    const std::vector<uint8_t>& payload);
std::optional<hidpp_battery_info> hidpp_parse_battery_charge(
    const std::vector<uint8_t>& payload);
std::optional<hidpp_battery_info> hidpp_parse_legacy_battery(
    const uint8_t* payload, size_t size);

struct hidpp_pairing_slot {
    uint8_t slot = 0;
    bool occupied = false;
    uint16_t pid = 0;
    std::string name;
    std::string serial;
    uint32_t connection_type = 0; // 1=USB, 2=BT, 3=Lightspeed
};

struct hidpp_dpi_info {
    uint16_t dpi_min = 0;
    uint16_t dpi_max = 0;
    uint16_t dpi_default = 0;
    uint16_t dpi_current = 0;
    uint16_t dpi_current_y = 0;
    std::vector<uint16_t> dpi_levels;
    bool extended = false;
    bool supports_y = false;
    bool supports_lift_off_distance = false;
    uint8_t lift_off_distance = 0; // 0=low, 1=medium, 2=high
};

struct hidpp_onboard_profile_info {
    uint8_t memory = 0;
    uint8_t active_profile = 0;
    uint8_t macro_profile = 0;
    uint8_t profile_count = 0;
    uint8_t out_of_bounds = 0;
    uint8_t button_count = 0;
    uint8_t sector_count = 0;
    uint16_t profile_size = 0;
    uint8_t shift = 0;
};

struct hidpp_onboard_profile_header {
    uint8_t profile = 0;
    uint16_t sector = 0;
    uint8_t enabled = 0;
};

enum class hidpp_lift_off_distance : uint8_t {
    low = 0,
    medium = 1,
    high = 2,
};

struct hidpp_device {
    std::string hidraw_path;
    uint8_t device_index = 0xFF;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    hidpp_device_info info;
    // Dynamic HID++ 2.0 feature IDs and their per-device indices.  Keeping
    // this snapshot with the identified device lets CLI/GUI callers expose
    // capabilities without probing hardware again.
    std::vector<std::pair<uint16_t, uint8_t>> features;
    std::vector<hidpp_feature_metadata> feature_metadata;
    bool connected = false;
    // BUG-24 (aj2): last known battery level (0..100, 255 = unknown) from
    // either a live notification or the periodic ACTIVE get_battery_status()
    // query (60 s cadence in the daemon's hidpp worker).  Daemon merges this
    // into the matching evdev mouse's detected_battery.  Worker-thread only.
    int      battery_level = -1;
    uint64_t last_battery_ms = 0; // CLOCK_MONOTONIC_RAW ms of last active query

    std::optional<uint8_t> feature_index(uint16_t feature_id) const;
    std::optional<uint16_t> feature_id(uint8_t dynamic_index) const;
    std::optional<uint16_t> notification_feature_id(
        const hidpp_notification& notification) const;
    bool supports_feature(uint16_t feature_id) const;
};

class HidppTransport {
public:
    explicit HidppTransport(const std::string& hidraw_path);
    ~HidppTransport();

    HidppTransport(const HidppTransport&) = delete;
    HidppTransport& operator=(const HidppTransport&) = delete;

    bool is_open() const { return fd_ >= 0; }

    uint16_t vendor_id() const { return vendor_id_; }
    uint16_t product_id() const { return product_id_; }
    uint8_t device_index() const { return device_index_.load(std::memory_order_relaxed); }

    // Send a request and wait for response with matching feature/function
    std::optional<hidpp_short_packet> send_short(
        uint8_t feature_index, uint8_t function_id,
        const uint8_t params[3] = nullptr,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
        uint8_t target_device_index = 0xFF);
    std::optional<hidpp_short_packet> send_short(
        hidpp_feature_index feature, uint8_t function_id,
        const uint8_t* params = nullptr,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
        uint8_t target_device_index = 0xFF);

    std::optional<hidpp_long_packet> send_long(
        uint8_t feature_index, uint8_t function_id,
        const uint8_t params[16] = nullptr,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
        uint8_t target_device_index = 0xFF);
    std::optional<hidpp_long_packet> send_long(
        hidpp_feature_index feature, uint8_t function_id,
        const uint8_t* params = nullptr,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
        uint8_t target_device_index = 0xFF);

    std::optional<hidpp_very_long_packet> send_very_long(
        uint8_t feature_index, uint8_t function_id,
        const uint8_t params[60] = nullptr,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
        uint8_t target_device_index = 0xFF);

    /// Read a HID++ 1.0 register using Solaar-compatible request IDs.
    /// The returned bytes are the register payload without report headers.
    std::optional<std::vector<uint8_t>> read_register(
        uint16_t register_id, const uint8_t* params = nullptr,
        size_t param_len = 0,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
        uint8_t target_device_index = 0xFF);

    /// Probe the HID++ 1.0 protocol with a short PING (read of register
    /// 0x0000).  A valid reply — success echo (0x81RR) OR register error
    /// (0x8FRR) — proves the endpoint speaks the legacy protocol even when
    /// HID++ 2.0 feature discovery came up empty.  Devices that implement no
    /// HID++ at all never answer, so this returns false.
    bool probe_hidpp10(uint8_t target_device_index = 0xFF);

    /// Issue a capability-gated HID++ 2.0 feature request and return its
    /// payload without report headers. Unknown features are rejected locally.
    std::optional<std::vector<uint8_t>> feature_request(
        uint16_t feature_id, uint8_t function_id,
        const uint8_t* params = nullptr, size_t param_len = 0,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
        uint8_t target_device_index = 0xFF);

    /// Write a HID++ 1.0 register using a Solaar-compatible request ID.
    /// This is intentionally a low-level primitive; callers must capability
    /// and model-gate any user-visible write operation.
    bool write_register(
        uint16_t register_id, const uint8_t* value = nullptr,
        size_t value_len = 0,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
        uint8_t target_device_index = 0xFF);

    /// Receive one pending notification without sending a request.
    /// A zero timeout performs a short non-blocking poll.
    std::optional<hidpp_notification> receive_notification(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    /// Drain up to max_count pending notifications. The bound prevents a
    /// busy device from starving the caller's main/event loop.
    std::vector<hidpp_notification> drain_notifications(
        size_t max_count = 32,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    // High-level device queries
    std::optional<hidpp_device_info> get_device_info(uint8_t target_device_index = 0xFF);
    std::optional<hidpp_battery_info> get_battery_status(uint8_t target_device_index = 0xFF);
    std::vector<hidpp_pairing_slot> get_pairing_info(uint8_t target_device_index = 0xFF);
    std::optional<hidpp_dpi_info> get_dpi_info(uint8_t target_device_index = 0xFF);
    std::optional<hidpp_onboard_profile_info>
    get_onboard_profile_info(uint8_t target_device_index = 0xFF);
    std::vector<hidpp_onboard_profile_header>
    get_onboard_profile_headers(uint8_t target_device_index = 0xFF);
    std::optional<std::vector<uint8_t>> read_onboard_profile_sector(
        uint16_t sector, size_t size,
        uint8_t target_device_index = 0xFF);
    bool set_dpi(uint16_t dpi, uint8_t target_device_index = 0xFF);
    std::optional<uint32_t> get_polling_rate(uint8_t target_device_index = 0xFF);
    bool set_polling_rate(uint32_t hz, uint8_t target_device_index = 0xFF);
    bool set_report_rate(uint32_t hz, uint8_t target_device_index = 0xFF) {
        return set_polling_rate(hz, target_device_index);
    }
    std::optional<hidpp_lift_off_distance>
    get_lift_off_distance(uint8_t target_device_index = 0xFF);
    bool set_lift_off_distance(hidpp_lift_off_distance distance,
                               uint8_t target_device_index = 0xFF);

    // ── Aşama 1: multi-host (Easy-Switch), LED, onboard write, buttons ─────

    struct hidpp_host_info {
        uint8_t current_host = 0xFF;
        uint8_t host_count   = 0;
        uint8_t flags        = 0;
    };
    /// 0x1814 CHANGE_HOST fn=0x00 — current host index + number of hosts.
    std::optional<hidpp_host_info> get_change_host_info(uint8_t target_device_index = 0xFF);
    /// 0x1814 CHANGE_HOST fn=0x10 — switch to host idx (0-based).
    bool set_change_host(uint8_t host_index, uint8_t target_device_index = 0xFF);

    /// Highest-advertised LED feature brightness range (0..100).
    /// Prefers brightness_control (0x8040), then backlight2 (0x1982), then
    /// backlight (0x1981); returns {ok, supported, value}.
    std::optional<std::pair<bool, uint8_t>> get_led_brightness(uint8_t target_device_index = 0xFF);
    /// 0x8040 fn=0x10 / 0x1982 fn=0x10 / 0x1981 fn=0x10 — set brightness 0..100.
    bool set_led_brightness(uint8_t brightness, uint8_t target_device_index = 0xFF);
    /// Which LED feature this device advertises (0x1981/0x1982/0x8040 or 0 if none).
    uint16_t led_feature_id(uint8_t target_device_index = 0xFF);

    /// 0x1B04 REPROG_CONTROLS_V4 fn=0x00 — read the remappable control list
    /// (control index → HID usage).  Returns pairs (control_id, hid_usage).
    std::vector<std::pair<uint8_t, uint16_t>>
    get_reprog_controls(uint8_t target_device_index = 0xFF);

    /// Write an onboard-profile flash sector.  `data` must be sector_bytes long.
    /// 0x8100 fn=0x50 set_long_parameter benchmark, fn=0x48 write_sector.
    bool write_onboard_profile_sector(uint16_t sector,
                                      const std::vector<uint8_t>& data,
                                      uint8_t target_device_index = 0xFF);

    // Root feature: get feature set
    std::vector<std::pair<uint16_t, uint8_t>> get_feature_set();
    std::vector<hidpp_feature_metadata> get_feature_metadata();
    bool supports_feature(uint16_t feature_id);

    // Set target device index for subsequent queries
    void set_device_index(uint8_t idx);

    /// Drop the per-device feature-set / metadata caches.  Call after a
    /// device replug (or an HID++ notification that reports a disconnect) so
    /// the next feature request re-negotiates the dynamic index space instead
    /// of trusting a stale cache.
    void clear_feature_cache();

private:
    int fd_ = -1;
    // sw_id 0 (0x00) is reserved: the device uses it for every notification,
    // so a command issued with sw_id 0 could be mistaken for a notification
    // (raw-byte matcher) or a mid-request response.  Start at 1 so the first
    // command's response id never collides with a notification.
    uint8_t next_sw_id_ = 1;
    std::string hidraw_path_;
    uint16_t vendor_id_ = 0;
    uint16_t product_id_ = 0;
    std::atomic<uint8_t> device_index_{0xFF};
    mutable std::mutex request_mutex_;
    mutable std::mutex feature_mutex_;
    std::unordered_map<uint32_t, uint8_t> feature_indices_;
    std::unordered_map<uint8_t, std::vector<std::pair<uint16_t, uint8_t>>>
        feature_sets_;
    std::unordered_map<uint8_t, std::vector<hidpp_feature_metadata>>
        feature_metadata_sets_;

    uint8_t next_sw_id();
    std::optional<uint8_t> resolve_feature_index(hidpp_feature_index feature,
                                                 uint8_t target_device_index);
    bool write_packet(const uint8_t* data, size_t len);
    bool read_packet(uint8_t* buf, size_t max_len, size_t& out_len,
                     std::chrono::milliseconds timeout);
    std::optional<std::vector<uint8_t>> send_feature_request(
        uint8_t feature_index, uint8_t function_id,
        const uint8_t* params, size_t param_len,
        std::chrono::milliseconds timeout, uint8_t target_device_index);

    // BUG-22 (aj4): notifications that arrive inside a feature-request wait
    // window are parsed and stashed here instead of being discarded by
    // send_feature_request(); drain_notifications() flushes the stash first so
    // a burst of identify() requests can no longer silently swallow battery /
    // link notifications.  Bounded (16) — guarded by request_mutex_, which both
    // producers and consumers already hold.
    std::vector<hidpp_notification> pending_notifications_;
};

// ── High-level notification handler (SOLAAR FAZ-B / P168) ───────────────────

/// Normalized, transport-independent classification of a HID++ notification.
/// Kinds mirror the events users care about: live battery, link up/down, and
/// lighting until a dedicated configuration path exists in the GUI.
enum class hidpp_notification_event_kind : uint8_t {
    battery,       // charge level / charging state changed
    connection,    // device connected or disconnected
    illumination,  // lighting / backlight state changed
    generic,       // recognized feature notification with no dedicated kind
    unhandled,     // notification the handler intentionally ignores
};

struct hidpp_notification_event {
    hidpp_notification_event_kind kind = hidpp_notification_event_kind::generic;
    uint8_t device_index = 0xFF;
    uint16_t feature_id = 0;                // resolved HID++ 2.0 feature or 1.0 sub-id
    bool connected = false;                 // valid when kind == connection
    std::optional<hidpp_battery_info> battery; // valid when kind == battery
    std::string feature_name;               // hidpp_feature_name(feature_id)
    std::vector<uint8_t> payload;           // raw notification payload (debug)
};

/// Classify one parsed notification against a device's feature map.  Pure
/// logic — no transport I/O.  Returned event carries a normalized payload
/// plus enough context for a daemon hook to log or forward battery /
/// connection / illumination changes.
hidpp_notification_event classify_hidpp_notification(
    const hidpp_device& device, const hidpp_notification& notification);

/// Non-blocking drain hook: pull up to max_count pending notifications from
/// the transport, classify each against `device`, and forward every event to
/// `on_event` (called under a mutex — keep the handler cheap).  Never blocks
/// longer than the transport's internal poll; returns the number of events
/// delivered.
size_t drain_hidpp_notifications(
    HidppTransport& transport, const hidpp_device& device,
    size_t max_count,
    const std::function<void(const hidpp_notification_event&)>& on_event);

/// Convert HID++ report-rate codes to Hz and back.  These are pure helpers so
/// callers can validate a requested rate without touching hardware.
std::optional<uint32_t> hidpp_rate_code_to_hz(bool extended, uint8_t code);
std::optional<uint8_t> hidpp_hz_to_rate_code(bool extended, uint32_t hz);

/// Normalize a HID++ function identifier to the four-bit function field used
/// on the wire.  Callers may pass either Solaar's request-id byte form
/// (0x10/0x50) or the canonical function number (0x1/0x5).
uint8_t hidpp_normalize_function_id(uint8_t function_id);

/// HID++ 1.0 writes to the 0x82xx long-register request range in a long
/// report, even when the value itself fits in the short payload.
bool hidpp_register_uses_long_report(uint16_t request_id);

/// Return a stable human-readable name for a known HID++ feature ID.
/// Unknown/vendor-specific IDs are reported as "unknown".
const char* hidpp_feature_name(uint16_t feature_id);

/// Decode the payload returned by FEATURE_SET.GetFeatureId.  The payload is
/// [feature_hi, feature_lo, flags, version] after report headers.
std::optional<hidpp_feature_metadata> hidpp_parse_feature_metadata(
    uint8_t index, const std::vector<uint8_t>& payload);

/// Decode one DEVICE_FW_VERSION record.  The wire layout is
/// [level, name(3), major, minor, build(2), transport flags, model IDs...].
std::optional<hidpp_firmware_record> hidpp_parse_firmware_record(
    const std::vector<uint8_t>& payload);

/// Parse a read-only HID++ 1.0 receiver pairing-information response.
/// `bolt` selects the documented Bolt receiver layout; no writes are implied.
std::optional<hidpp_pairing_slot> hidpp_parse_receiver_pairing(
    const std::vector<uint8_t>& payload, uint8_t slot, bool bolt = false);

/// Discover all Logitech hidraw devices on the system.
std::vector<std::string> discover_logitech_hidraw_devices(const char* root = "/dev");

/// Try to open and identify a Logitech HID++ device.
///
/// Returns an identified HID++ 2.0 device (full feature set) or a HID++ 1.0
/// device (protocol_version 1, no feature index).  Logitech hidraw nodes that
/// implement neither protocol are still returned with protocol_version 0 and
/// connected=false so UI layers can explain the limitation instead of showing
/// an empty list; daemon callers must skip protocol_version 0 devices.
/// Only an unopenable node yields std::nullopt.
std::optional<hidpp_device> identify_logitech_device(const std::string& hidraw_path);

/// Discover every HID++ device reachable on a single hidraw node: the receiver
/// shell on 0xFF, a directly connected peripheral on 0x00, and any paired
/// wireless device the receiver answers for on indexes 1..6.  Probing only
/// 0xFF/0x00 previously surfaced just a single-interface receiver (which
/// carries no DPI/polling-rate features) and silently dropped every paired
/// mouse behind it.  Same fallbacks as identify_logitech_device (HID++ 1.0,
/// then protocol_version 0 display shell).  Empty only when the node cannot be
/// opened at all.
std::vector<hidpp_device> identify_logitech_devices(const std::string& hidraw_path);

} // namespace rawaccel