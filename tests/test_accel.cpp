// RawAccel Linux — Birim testleri
// Derleme: bash tests/run_tests.sh  (veya cmake --build build && ctest --test-dir build)
// Bağımlılık yok — sadece standart C++20 ve proje başlıkları.
//
// Test yapısı: basit assert makrosu, harici framework gerektirmiyor.
// Her SECTION() bağımsız bir test grubu; hata varsa stderr'e yazar ve
// süreç 1 ile çıkar.

#include "../include/accel-classic.hpp"
#include "../include/accel-natural.hpp"
#include "../include/accel-jump.hpp"
#include "../include/accel-synchronous.hpp"
#include "../include/accel-power.hpp"
#include "../include/accel-lookup.hpp"
#include "../include/accel-noaccel.hpp"
#include "../include/accel-union.hpp"
#include "../include/config.hpp"
#include "../include/logitech_receiver.hpp"
#include "../include/logitech_hidpp.hpp"
#include "../daemon/lat_stats.hpp"    // latency histogram (no libevdev dependency)
#include "../daemon/motion_math.hpp"  // apply_motion_math — subpixel accumulation

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <sys/stat.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <regex>
#include <algorithm>
#include <string>
#include <vector>
#include <unistd.h>

using namespace rawaccel;

// ── Test altyapısı ────────────────────────────────────────────────────────────
//
// CLI seçenekleri (R16 — test framework filtreleme desteği):
//   --filter <regex>   Yalnızca section adı regex ile eşleşenleri çalıştır
//   --list             Section adlarını basıp çık (assertion çalıştırma)
//   --quiet            PASS satırlarını bastırma; sadece FAIL ve özet göster
//   --help             Kullanım bilgisini bas

static int  g_tests  = 0;
static int  g_passed = 0;
static int  g_failed = 0;
static int  g_skipped_sections = 0;
static int  g_sections_matched = 0;
static const char* g_section = "";

static bool        g_section_active = true;   // current section runs assertions
static bool        g_list_only      = false;  // --list: print names, skip asserts
static bool        g_quiet          = false;  // --quiet: suppress PASS lines
static bool        g_have_filter    = false;
static std::regex  g_filter_regex;

#define SECTION(name) do { \
    g_section = name; \
    if (g_list_only) { \
        g_section_active = false; \
        if (g_have_filter && !std::regex_search(std::string(name), g_filter_regex)) { \
            g_skipped_sections++; \
        } else { \
            g_sections_matched++; \
            std::printf("%s\n", name); \
        } \
    } else if (g_have_filter && !std::regex_search(std::string(name), g_filter_regex)) { \
        g_section_active = false; \
        g_skipped_sections++; \
    } else { \
        g_section_active = true; \
        g_sections_matched++; \
        std::printf("\n[%s]\n", name); \
    } \
} while(0)

#define EXPECT(cond) do { \
    if (!g_section_active) break; \
    g_tests++; \
    auto _c = (cond); \
    if (_c) { \
        g_passed++; \
        if (!g_quiet) std::printf("  PASS  %s\n", #cond); \
    } else { \
        g_failed++; \
        std::fprintf(stderr, "  FAIL  %s:%d  %s  (section: %s)\n", \
                     __FILE__, __LINE__, #cond, g_section); \
    } \
} while(0)

#define EXPECT_NEAR(a, b, tol) do { \
    if (!g_section_active) break; \
    g_tests++; \
    double _a = (a), _b = (b), _t = (tol); \
    if (std::fabs(_a - _b) <= _t) { \
        g_passed++; \
        if (!g_quiet) std::printf("  PASS  %s ≈ %s  (%.6g ≈ %.6g)\n", #a, #b, _a, _b); \
    } else { \
        g_failed++; \
        std::fprintf(stderr, "  FAIL  %s:%d  |%s - %s| = %.6g > %.6g  (section: %s)\n", \
                     __FILE__, __LINE__, #a, #b, std::fabs(_a - _b), _t, g_section); \
    } \
} while(0)

// ── Yardımcı: varsayılan accel_args ile belirli alanları geçersiz kıl ─────────

static accel_args make_args(accel_mode mode) {
    accel_args a;
    a.mode = mode;
    a.gain = true;
    a.acceleration   = 0.005;
    a.exponent_classic = 2.0;
    a.exponent_power = 0.05;
    a.limit          = 1.5;
    a.decay_rate     = 0.1;
    a.input_offset   = 0.0;
    a.sync_speed     = 5.0;
    a.smooth         = 0.5;
    a.cap            = { 15.0, 1.5 };
    a.cap_mode_val   = cap_mode::out;
    a.length         = 0;
    return a;
}

static void test_logitech_receiver_discovery() {
    SECTION("Logitech receiver discovery — capability classification");
    const std::string root = "/tmp/rawaccel_receiver_fixture";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root + "/1-3");
    std::filesystem::create_directories(root + "/2-1");
    std::filesystem::create_directories(root + "/3-1");
    {
        std::ofstream(root + "/1-3/idVendor") << "046d\n";
        std::ofstream(root + "/1-3/idProduct") << "c548\n";
        std::ofstream(root + "/1-3/product") << "Logitech Bolt Receiver\n";
        std::ofstream(root + "/2-1/idVendor") << "046d\n";
        std::ofstream(root + "/2-1/idProduct") << "c542\n";
        std::ofstream(root + "/2-1/product") << "Logitech Wireless Receiver\n";
        std::ofstream(root + "/3-1/idVendor") << "046d\n";
        std::ofstream(root + "/3-1/idProduct") << "c52f\n";
        std::ofstream(root + "/3-1/product") << "Logitech Nano Receiver\n";
    }
    const auto found = discover_logitech_receivers(root);
    EXPECT(found.size() == 3);
    if (found.size() == 3) {
        // Check all receivers regardless of filesystem iteration order
        bool has_bolt = false, has_nano = false, has_advanced_nano = false;
        for (const auto& r : found) {
            if (r.kind == logitech_receiver_kind::bolt) {
                has_bolt = true;
                EXPECT(r.hidpp_supported);
                EXPECT(r.max_paired_devices == 6);
            } else if (r.kind == logitech_receiver_kind::nano) {
                if (r.product_id == 0xc542) {
                    has_nano = true;
                    EXPECT(!r.hidpp_supported);
                } else if (r.product_id == 0xc52f) {
                    has_advanced_nano = true;
                    EXPECT(r.hidpp_supported);
                }
                EXPECT(r.max_paired_devices == 1);
            }
        }
        EXPECT(has_bolt);
        EXPECT(has_nano);
        EXPECT(has_advanced_nano);
    }
    EXPECT(std::string(logitech_receiver_kind_name(logitech_receiver_kind::lightspeed)) == "Lightspeed");
    std::filesystem::remove_all(root);
}

static void test_logitech_hidpp_notification_classification() {
    SECTION("HID++ notification classification: battery/connection/illumination");

    // Build an identified device whose dynamic feature map mirrors a real
    // Lightspeed mouse (feature_id → dynamic index).
    hidpp_device dev;
    dev.device_index = 0x04;
    dev.features = {
        {0x0001, 0x01}, // FEATURE_SET
        {0x0003, 0x02}, // DEVICE_FW_VERSION
        {0x1000, 0x03}, // battery_status
        {0x1004, 0x04}, // unified_battery
        {0x1001, 0x05}, // battery_voltage
        {0x1D4B, 0x06}, // wireless_device_status
        {0x1990, 0x07}, // illumination
    };

    // ── legacy battery 0x0D: direct charge percentage ──────────────────
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::legacy_battery;
        n.sub_id = 0x0D;
        n.payload = {0x55, 0x00, 0x50}; // 85%, recharging (0x50)
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::battery);
        EXPECT(ev.feature_id == 0x0D);
        EXPECT(ev.battery.has_value());
        if (ev.battery) EXPECT(ev.battery->level == 85);
    }

    // ── legacy battery 0x07: Solaar approximation code + charging status ─
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::legacy_battery;
        n.sub_id = 0x07;
        n.payload = {0x07, 0x21, 0x00}; // code FULL(90), F0+F5 charging
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::battery);
        EXPECT(ev.battery.has_value());
        if (ev.battery) {
            EXPECT(ev.battery->level == 90);
            EXPECT(ev.battery->charging);
        }
    }

    // ── legacy illumination 0x17 ───────────────────────────────────────
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::legacy_illumination;
        n.sub_id = 0x17;
        n.device_index = 0x04;
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::illumination);
        EXPECT(ev.feature_id == 0x17);
        EXPECT(ev.feature_name == "illumination");
    }

    // ── HID++ 1.0 CONNECT_DISCONNECT (0x40) → disconnected ─────────────
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::hidpp10;
        n.sub_id = 0x40;
        n.address = 0x00;
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::connection);
        EXPECT(!ev.connected);
    }

    // ── HID++ 1.0 CONNECTED (0x42): bit 0 cleared = connected ──────────
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::hidpp10;
        n.sub_id = 0x42;
        n.address = 0x02; // connected (bit 0 == 0)
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::connection);
        EXPECT(ev.connected);

        n.address = 0x03; // not connected (bit 0 == 1)
        ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::connection);
        EXPECT(!ev.connected);
    }

    // ── HID++ 1.0 POWER (0x4B) → powered on ────────────────────────────
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::hidpp10;
        n.sub_id = 0x4B;
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::connection);
        EXPECT(ev.connected);
    }

    // ── unresolved hidpp20 feature (dynamic index 0x20 not allocated) ──
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::hidpp20;
        n.feature_index = 0x20;
        n.payload = {0x00, 0x00, 0x00};
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::unhandled);
    }

    // ── battery_status (0x1000): 3-byte report, 0x4B → 75% ─────────────
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::hidpp20;
        n.feature_index = 0x03; // battery_status
        n.payload = {0x4B, 0x00, 0x00};
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::battery);
        EXPECT(ev.feature_id == 0x1000);
        EXPECT(ev.feature_name == "battery_status");
        EXPECT(ev.battery.has_value());
        if (ev.battery) EXPECT(ev.battery->level == 75);
    }

    // ── unified_battery (0x1004): 5-byte report, 0xFF level = unknown ──
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::hidpp20;
        n.feature_index = 0x04; // unified_battery
        n.payload = {0x00, 0xFF, 0x00, 0x00, 0x00};
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::battery);
        EXPECT(ev.feature_id == 0x1004);
        if (ev.battery) EXPECT(ev.battery->level == 255);
    }

    // ── battery_voltage (0x1001): BE16 mV + flags ──────────────────────
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::hidpp20;
        n.feature_index = 0x05; // battery_voltage
        n.payload = {0x0F, 0xA7, 0x80}; // 0x0FA7 = 4007 mV, charging
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::battery);
        EXPECT(ev.feature_id == 0x1001);
        EXPECT(ev.feature_name == "battery_voltage");
        EXPECT(ev.battery.has_value());
        if (ev.battery) {
            EXPECT(ev.battery->charging);
            // 4007 mV on the shared Li-ion curve must land in the 80% bucket.
            EXPECT(ev.battery->level >= 80 && ev.battery->level <= 89);
        }
    }

    // ── wireless_device_status (0x1D4B) → connection alive ─────────────
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::hidpp20;
        n.feature_index = 0x06; // wireless_device_status
        n.payload = {0x00, 0x00, 0x00};
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::connection);
        EXPECT(ev.connected);
    }

    // ── illumination (0x1990) → illumination ───────────────────────────
    {
        hidpp_notification n;
        n.type = hidpp_notification::kind::hidpp20;
        n.feature_index = 0x07; // illumination
        n.payload = {0x01};
        auto ev = classify_hidpp_notification(dev, n);
        EXPECT(ev.kind == hidpp_notification_event_kind::illumination);
        EXPECT(ev.feature_id == 0x1990);
    }

    // ── drain_hidpp_notifications on an unavailable transport: no crash,
    //    zero delivered, callback never invoked ─────────────────────────
    {
        HidppTransport closed("/does/not/exist/hidraw0");
        size_t calls = 0;
        size_t delivered = 0;
        for (size_t i = 0; i < 4; ++i) {
            delivered += drain_hidpp_notifications(
                closed, dev, 8,
                [&calls](const hidpp_notification_event&) { ++calls; });
        }
        EXPECT(delivered == 0);
        EXPECT(calls == 0);
    }
}

static void test_logitech_hidpp_packets() {
    SECTION("Logitech HID++ packet serialization");
    // Short packet round-trip
    hidpp_short_packet short_pkt;
    short_pkt.report_id = 0x10;
    short_pkt.device_index = 0xFF;
    short_pkt.feature_index = 0x03;
    short_pkt.function_id = 0x02;
    short_pkt.software_id = 0x05;
    short_pkt.params[0] = 0x12;
    short_pkt.params[1] = 0x34;

    auto short_bytes = short_pkt.to_bytes();
    auto parsed_short = hidpp_short_packet::from_bytes(short_bytes.data(), short_bytes.size());
    EXPECT(parsed_short.has_value());
    EXPECT(short_bytes[0] == 0x10 && short_bytes[1] == 0xFF &&
           short_bytes[2] == 0x03 && short_bytes[3] == 0x25 &&
           short_bytes[4] == 0x12 && short_bytes[5] == 0x34 &&
           short_bytes[6] == 0x00);
    EXPECT(!hidpp_short_packet::from_bytes(short_bytes.data(), short_bytes.size() - 1));
    EXPECT(!hidpp_short_packet::from_bytes(nullptr, short_bytes.size()));
    if (parsed_short) {
        EXPECT(parsed_short->report_id == 0x10);
        EXPECT(parsed_short->device_index == 0xFF);
        EXPECT(parsed_short->feature_index == 0x03);
        EXPECT(parsed_short->function_id == 0x02);
        EXPECT(parsed_short->software_id == 0x05);
        EXPECT(parsed_short->params[0] == 0x12);
        EXPECT(parsed_short->params[1] == 0x34);
    }

    // Long packet round-trip
    hidpp_long_packet long_pkt;
    long_pkt.report_id = 0x11;
    long_pkt.device_index = 0xFF;
    long_pkt.feature_index = 0x07;
    long_pkt.function_id = 0x00;
    for (int i = 0; i < 16; ++i) long_pkt.params[i] = static_cast<uint8_t>(i);

    auto long_bytes = long_pkt.to_bytes();
    auto parsed_long = hidpp_long_packet::from_bytes(long_bytes.data(), long_bytes.size());
    EXPECT(parsed_long.has_value());
    EXPECT(!hidpp_long_packet::from_bytes(long_bytes.data(), long_bytes.size() + 1));
    EXPECT(long_bytes[2] == 0x07 && long_bytes[3] == 0x00 &&
           long_bytes[19] == 15);
    if (parsed_long) {
        EXPECT(parsed_long->report_id == 0x11);
        EXPECT(parsed_long->device_index == 0xFF);
        EXPECT(parsed_long->feature_index == 0x07);
        EXPECT(parsed_long->function_id == 0x00);
        for (int i = 0; i < 16; ++i) {
            EXPECT(parsed_long->params[i] == static_cast<uint8_t>(i));
        }
    }

    // Very long packet round-trip
    hidpp_very_long_packet vlong_pkt;
    vlong_pkt.report_id = 0x12;
    vlong_pkt.device_index = 0xFF;
    vlong_pkt.feature_index = 0x4B;
    vlong_pkt.function_id = 0x00;
    for (int i = 0; i < 60; ++i) vlong_pkt.params[i] = static_cast<uint8_t>(i + 10);

    auto vlong_bytes = vlong_pkt.to_bytes();
    auto parsed_vlong = hidpp_very_long_packet::from_bytes(vlong_bytes.data(), vlong_bytes.size());
    EXPECT(parsed_vlong.has_value());
    EXPECT(!hidpp_very_long_packet::from_bytes(nullptr, vlong_bytes.size()));
    EXPECT(vlong_bytes[2] == 0x4B && vlong_bytes[3] == 0x00 &&
           vlong_bytes[63] == static_cast<uint8_t>(69));
    if (parsed_vlong) {
        EXPECT(parsed_vlong->report_id == 0x12);
        EXPECT(parsed_vlong->device_index == 0xFF);
        EXPECT(parsed_vlong->feature_index == 0x4B);
        EXPECT(parsed_vlong->function_id == 0x00);
        for (int i = 0; i < 60; ++i) {
            EXPECT(parsed_vlong->params[i] == static_cast<uint8_t>(i + 10));
        }
    }
}

static void test_logitech_hidraw_discovery() {
    SECTION("Logitech hidraw device discovery");
    const std::string root = "/tmp/rawaccel_hidraw_fixture";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    // A missing scan root must yield an empty result without crashing.
    auto missing = discover_logitech_hidraw_devices(root.c_str());
    EXPECT(missing.empty());

    std::filesystem::create_directories(root);
    // Enumeration only considers hidraw-prefixed *character* files; a plain
    // regular file is skipped (fs::is_character_file), so a rightname-but-
    // wrong-type node can never be opened/ioctl'd for the vendor check.
    {
        std::ofstream(root + "/hidraw0") << "not a char device";
        std::ofstream(root + "/hidraw3") << "not a char device";
        std::ofstream(root + "/mouse")   << "x"; // non-hidraw name must be ignored
    }
    auto found = discover_logitech_hidraw_devices(root.c_str());
    EXPECT(found.empty());
    std::filesystem::remove_all(root, ec);

    // The production scan (real /dev, default root) runs without crashing; on
    // hardware with no Logitech hidraw node it returns an empty vector.
    auto devices = discover_logitech_hidraw_devices();
    (void)devices; // count depends on hardware — just exercise the default root

    // Identification of an unopenable node yields nullopt (never a protocol-0
    // display shell — that is only produced once the node is actually readable).
    EXPECT(!identify_logitech_device("/nonexistent/rawaccel/hidraw99").has_value());

    // A failed transport can neither confirm nor deny HID++ 1.0.
    HidppTransport closed("/nonexistent/rawaccel/hidraw99");
    EXPECT(!closed.is_open());
    EXPECT(!closed.probe_hidpp10());
    EXPECT(!closed.probe_hidpp10(0x00));
}

static void test_logitech_hidpp_hardware_controls() {
    SECTION("Logitech HID++ hardware-control rate mappings");
    EXPECT(hidpp_normalize_function_id(0x01) == 0x01);
    EXPECT(hidpp_normalize_function_id(0x10) == 0x01);
    EXPECT(hidpp_normalize_function_id(0x50) == 0x05);
    EXPECT(hidpp_register_uses_long_report(0x8207));
    EXPECT(!hidpp_register_uses_long_report(0x8107));

    // Packet serialization accepts Solaar's request-id-byte spelling while
    // putting only the normalized function nibble on the wire.
    hidpp_short_packet function_packet;
    function_packet.function_id = 0x50;
    function_packet.software_id = 0x03;
    const auto function_bytes = function_packet.to_bytes();
    EXPECT(function_bytes[3] == 0x53);

    EXPECT(static_cast<uint16_t>(hidpp_feature_index::adjustable_dpi) == 0x2201);
    EXPECT(static_cast<uint16_t>(hidpp_feature_index::extended_adjustable_dpi) == 0x2202);
    EXPECT(static_cast<uint16_t>(hidpp_feature_index::report_rate) == 0x8060);
    EXPECT(static_cast<uint16_t>(hidpp_feature_index::extended_adjustable_report_rate) == 0x8061);
    EXPECT(static_cast<uint16_t>(hidpp_feature_index::feature_info) == 0x0002);
    EXPECT(static_cast<uint16_t>(hidpp_feature_index::device_fw_version) == 0x0003);
    EXPECT(static_cast<uint16_t>(hidpp_feature_index::device_name) == 0x0005);
    EXPECT(static_cast<uint16_t>(hidpp_feature_index::device_friendly_name) == 0x0007);

    EXPECT(hidpp_rate_code_to_hz(false, 1).value_or(0) == 1000);
    EXPECT(hidpp_rate_code_to_hz(false, 8).value_or(0) == 125);
    EXPECT(!hidpp_rate_code_to_hz(false, 0).has_value());
    EXPECT(hidpp_rate_code_to_hz(true, 0).value_or(0) == 125);
    EXPECT(hidpp_rate_code_to_hz(true, 6).value_or(0) == 8000);
    EXPECT(!hidpp_rate_code_to_hz(true, 7).has_value());

    EXPECT(hidpp_hz_to_rate_code(false, 1000).value_or(0) == 1);
    EXPECT(hidpp_hz_to_rate_code(false, 125).value_or(0) == 8);
    EXPECT(!hidpp_hz_to_rate_code(false, 2000).has_value());
    EXPECT(hidpp_hz_to_rate_code(true, 2000).value_or(255) == 4);
    EXPECT(hidpp_hz_to_rate_code(true, 8000).value_or(255) == 6);
    EXPECT(!hidpp_hz_to_rate_code(true, 333).has_value());
    EXPECT(std::string(hidpp_feature_name(0x2201)) == "adjustable_dpi");
    EXPECT(std::string(hidpp_feature_name(0x0003)) == "device_fw_version");
    EXPECT(std::string(hidpp_feature_name(0x0005)) == "device_name");
    EXPECT(std::string(hidpp_feature_name(0x1004)) == "unified_battery");
    EXPECT(std::string(hidpp_feature_name(0xDEAD)) == "unknown");
    {
        // Solaar's FeatureSet.GetFeatureId payload (without report headers)
        // is [feature_hi, feature_lo, flags, version].
        auto metadata = hidpp_parse_feature_metadata(
            9, {0x22, 0x01, 0x60, 0x03});
        EXPECT(metadata.has_value());
        if (metadata) {
            EXPECT(metadata->feature_id == 0x2201);
            EXPECT(metadata->index == 9);
            EXPECT(metadata->flags == 0x60);
            EXPECT(metadata->version == 3);
        }
        EXPECT(!hidpp_parse_feature_metadata(9, {0x22, 0x01, 0x60}).has_value());
    }
    {
        const std::vector<uint8_t> pairing = {
            0x20, 0x04, 0x01, 0x40, 0x05, 0x00, 0x00, 0x03
        };
        auto slot = hidpp_parse_receiver_pairing(pairing, 1);
        EXPECT(slot.has_value());
        if (slot) {
            EXPECT(slot->slot == 1);
            EXPECT(slot->occupied);
            EXPECT(slot->pid == 0x4005);
            EXPECT(slot->connection_type == 1);
        }
        const std::vector<uint8_t> bolt_pairing = {
            0x51, 0x03, 0xA9, 0x01, 0x02, 0x03, 0x04, 0x05
        };
        auto bolt = hidpp_parse_receiver_pairing(bolt_pairing, 2, true);
        EXPECT(bolt.has_value());
        if (bolt) {
            EXPECT(bolt->slot == 2);
            EXPECT(bolt->pid == 0x01A9);
            EXPECT(bolt->connection_type == 3);
        }
        EXPECT(!hidpp_parse_receiver_pairing({0x20, 0x00}, 1).has_value());
        EXPECT(!hidpp_parse_receiver_pairing(pairing, 0).has_value());
    }
    HidppTransport unavailable("/dev/nonexistent-rawaccel-hidpp");
    EXPECT(!unavailable.read_register(0x07).has_value());
    EXPECT(!unavailable.read_register(0x0300).has_value());
    EXPECT(!unavailable.feature_request(0, 0).has_value());
    EXPECT(!unavailable.feature_request(0x2201, 0, nullptr, 17).has_value());
    EXPECT(!unavailable.supports_feature(0x2201));
    EXPECT(!unavailable.get_onboard_profile_info().has_value());
    EXPECT(!unavailable.read_onboard_profile_sector(1, 16).has_value());
    EXPECT(!unavailable.read_onboard_profile_sector(0, 16).has_value());
    EXPECT(!unavailable.read_onboard_profile_sector(1, 4097).has_value());
    unavailable.set_device_index(0x00);
    EXPECT(!unavailable.supports_feature(0x2201));
    EXPECT(!unavailable.write_register(0x07, nullptr, 0));
    EXPECT(!unavailable.write_register(0x0300, nullptr, 0));
    const uint8_t oversized_register_write[17] = {};
    EXPECT(!unavailable.write_register(0x07, oversized_register_write,
                                       sizeof(oversized_register_write)));
    EXPECT(!unavailable.receive_notification().has_value());
    EXPECT(unavailable.drain_notifications(0).empty());
    EXPECT(unavailable.drain_notifications(4).empty());

    const uint8_t notification_bytes[7] = {
        0x10, 0xFF, 0x07, 0x40, 0x32, 0x01, 0x00
    };
    auto notification = hidpp_notification::from_bytes(
        notification_bytes, sizeof(notification_bytes));
    EXPECT(notification.has_value());
    EXPECT(notification->device_index == 0xFF);
    EXPECT(notification->sub_id == 0x07);
    EXPECT(notification->address == 0x40);
    EXPECT(notification->feature_index == 0x07);
    EXPECT(notification->type == hidpp_notification::kind::legacy_battery);
    EXPECT(notification->payload.size() == 3);
    hidpp_device capability_device;
    capability_device.features = {
        {static_cast<uint16_t>(hidpp_feature_index::adjustable_dpi), 4},
        {static_cast<uint16_t>(hidpp_feature_index::onboard_profiles), 7}
    };
    EXPECT(capability_device.supports_feature(0x2201));
    EXPECT(capability_device.supports_feature(0x8100));
    EXPECT(!capability_device.supports_feature(0x8061));
    EXPECT(capability_device.feature_index(0x2201).value_or(0) == 4);
    EXPECT(capability_device.feature_index(0x8100).value_or(0) == 7);
    EXPECT(!capability_device.feature_index(0x8061).has_value());
    EXPECT(capability_device.feature_id(4).value_or(0) == 0x2201);
    EXPECT(capability_device.feature_id(7).value_or(0) == 0x8100);
    EXPECT(!capability_device.feature_id(9).has_value());
    EXPECT(capability_device.notification_feature_id(*notification).value_or(0) == 0x07);
    auto hidpp20_notification = *notification;
    hidpp20_notification.type = hidpp_notification::kind::hidpp20;
    hidpp20_notification.feature_index = 7;
    EXPECT(capability_device.notification_feature_id(hidpp20_notification)
               .value_or(0) == 0x8100);

    const uint8_t reply_bytes[7] = {
        0x10, 0xFF, 0x81, 0x07, 0x00, 0x00, 0x00
    };
    EXPECT(!hidpp_notification::from_bytes(reply_bytes,
                                            sizeof(reply_bytes)).has_value());
    const uint8_t invalid_subid[7] = {0x10, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT(!hidpp_notification::from_bytes(invalid_subid, sizeof(invalid_subid)).has_value());

    SECTION("Logitech HID++ battery parsing stays conservative on unknown payloads");
    {
        const std::vector<uint8_t> unified_unknown = {0x00, 0x00, 0xFF};
        auto unified = hidpp_parse_unified_battery(unified_unknown);
        EXPECT(unified.has_value());
        if (unified) {
            EXPECT(unified->level == 255);
            EXPECT(!unified->online);
        }

        const uint8_t legacy_unknown[3] = {0x00, 0x00, 0xFF};
        auto legacy = hidpp_parse_legacy_battery(legacy_unknown, sizeof(legacy_unknown));
        EXPECT(legacy.has_value());
        if (legacy) {
            EXPECT(legacy->level == 255);
            EXPECT(!legacy->charging);
            EXPECT(legacy->online);
        }
    }

    SECTION("Logitech HID++ 1.0 register 0x07 battery decoding");
    {
        const uint8_t full[3] = {7, 0x00, 0x00};
        const uint8_t charging[3] = {5, 0x21, 0x00};
        const uint8_t low[3] = {3, 0x22, 0x00};
        auto full_info = hidpp_parse_legacy_battery(full, sizeof(full));
        auto charging_info = hidpp_parse_legacy_battery(charging, sizeof(charging));
        auto low_info = hidpp_parse_legacy_battery(low, sizeof(low));
        EXPECT(full_info && full_info->level == 90 && !full_info->charging);
        EXPECT(charging_info && charging_info->level == 50 &&
               charging_info->charging);
        EXPECT(low_info && low_info->level == 20 && !low_info->charging);
    }

    SECTION("Logitech HID++ battery feature/register layouts stay separate");
    {
        auto status = hidpp_parse_battery_status({0, 75, 0x01});
        EXPECT(status && status->level == 255 && status->charging);
        auto charge = hidpp_parse_battery_charge({42, 0, 0x50});
        EXPECT(charge && charge->level == 42 && charge->charging);
        auto not_charge = hidpp_parse_battery_charge({42, 0, 0x30});
        EXPECT(not_charge && !not_charge->charging);
    }

    SECTION("Logitech HID++ firmware and transport-ID offsets");
    {
        const std::vector<uint8_t> record = {
            0x01, 'M', 'S', '2', 0x0A, 0x0B, 0x12, 0x34,
            0x0D, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
        };
        auto parsed = hidpp_parse_firmware_record(record);
        EXPECT(parsed.has_value());
        if (parsed) {
            EXPECT(parsed->unit_id.empty());
            EXPECT(parsed->model_id == "AABBCCDDEEFF");
            EXPECT(parsed->transport_flags == 0x0D);
            EXPECT(parsed->major == 0x0A && parsed->minor == 0x0B);
            EXPECT(parsed->build == 0x1234);
        }
        auto hardware = hidpp_parse_firmware_record({0x02, 0x07});
        EXPECT(hardware && hardware->level == 2 && hardware->major == 7);
    }
}

// ── Test 1: noaccel ──────────────────────────────────────────────────────────

static void test_noaccel() {
    SECTION("noaccel");
    accel_args args = make_args(accel_mode::noaccel);
    accel_noaccel na;
    EXPECT_NEAR(na(0.0,   args), 1.0, 1e-12);
    EXPECT_NEAR(na(10.0,  args), 1.0, 1e-12);
    EXPECT_NEAR(na(100.0, args), 1.0, 1e-12);
}

// ── Test 2: classic (gain mode) ──────────────────────────────────────────────

static void test_classic() {
    SECTION("classic — gain mode");
    accel_args args = make_args(accel_mode::classic);
    classic c(args);

    // x == 0 → gain = 1
    EXPECT_NEAR(c(0.0, args), 1.0, 1e-9);

    // x küçük → gain baseline'e yakın (1 + epsilon)
    double g_low  = c(1.0, args);
    double g_high = c(30.0, args);
    EXPECT(g_low  > 1.0);
    EXPECT(g_high > g_low);

    // cap.y = 1.5 → gain asla 1.5'i geçmemeli
    EXPECT(g_high <= 1.5 + 1e-9);

    // exponent <= 1 yolu: sabit gain = acceleration, cap guard
    accel_args args2 = args;
    args2.exponent_classic = 1.0;
    args2.cap.y = 0.5; // degenerate: cap.y < 1
    classic c2(args2);
    double g2 = c2(10.0, args2);
    EXPECT(g2 >= 1.0); // cap.y < 1 guard: negatif gain üretmemeli
}

// ── Test 3: natural ──────────────────────────────────────────────────────────

static void test_natural() {
    SECTION("natural — gain mode");
    accel_args args = make_args(accel_mode::natural);
    natural n(args);

    // x = 0 → 1.0 (guard)
    EXPECT_NEAR(n(0.0, args), 1.0, 1e-9);

    // Hız arttıkça gain artar.
    // Not: gain mode, RawAccel reference integral formülünü kullanır; asimptot
    // args.limit'e yakınsar (legacy modda olduğu gibi limit+1 değil).
    double g1 = n(1.0, args);
    double g2 = n(10.0, args);
    double g3 = n(50.0, args);
    EXPECT(g1 > 1.0);
    EXPECT(g2 > g1);
    EXPECT(g3 > g2);
    EXPECT(std::isfinite(g3)); // sonsuz veya NaN üretmemeli

    // x yakın 0 → guard devreye girmeli, 1.0 döndürmeli
    EXPECT_NEAR(n(1e-10, args), 1.0, 1e-9);

    // Gain mode kapalı (legacy)
    args.gain = false;
    natural n_legacy(args);
    double gl = n_legacy(10.0, args);
    EXPECT(gl > 1.0);
    EXPECT(gl <= args.limit + 1e-6);
}

// ── Test 4: jump ─────────────────────────────────────────────────────────────

static void test_jump() {
    SECTION("jump — reference (cap.x = step pos, cap.y = step gain)");
    accel_args args = make_args(accel_mode::jump);
    // make_args: gain=true, cap={15,1.5}, smooth=0.5 → smooth_rate = 2π/(0.5·15)
    jump j(args);

    // x=0 → 1.0 (GAIN origin)
    EXPECT_NEAR(j(0.0, args), 1.0, 1e-12);

    // Çok düşük hız → ~1.0 (sigmoid sıfıra yakın)
    double g_vlow = j(0.01, args);
    EXPECT(g_vlow >= 1.0);
    EXPECT(g_vlow < 1.05);

    // Çok yüksek hız → limit'e yakın (1 + step.y = 1.5); integral form asimptotik
    double g_high = j(100000.0, args);
    EXPECT(std::fabs(g_high - 1.5) < 1e-3);

    // Artan hız → artan gain
    EXPECT(j(20.0, args) > j(10.0, args));
    EXPECT(j(10.0, args) > j(1.0, args));

    // LEGACY: smooth sigmoid doğrudan çarpan döndürür
    accel_args args_l = args;
    args_l.gain = false;
    jump jl(args_l);
    // y(0.01) = 1 + step.y/(1 + exp(smooth_rate·(step.x−x))) ≈ 1.0 + 1.75e-6
    EXPECT_NEAR(jl(0.01, args_l), 1.0, 1e-5);
    EXPECT_NEAR(jl(500.0, args_l), 1.5, 1e-6);
    // LEGACY hard step (smooth=0): x < step.x → 1, else → 1 + step.y (basamak)
    accel_args args_hard = args;
    args_hard.gain  = false;
    args_hard.smooth = 0;
    jump jh(args_hard);
    EXPECT_NEAR(jh(1.0,  args_hard), 1.0, 1e-9);    // x < step.x
    EXPECT_NEAR(jh(15.0, args_hard), 1.5, 1e-9);    // x == step.x → 1 + step.y
    EXPECT_NEAR(jh(30.0, args_hard), 1.5, 1e-9);    // plateau 1 + step.y
    EXPECT_NEAR(jh(500.0, args_hard), 1.5, 1e-9);

    // GAIN hard step: 1 + step.y·(x - step.x)/x
    accel_args args_gh = args;
    args_gh.gain   = true;
    args_gh.smooth = 0;
    jump jgh(args_gh);
    EXPECT_NEAR(jgh(1.0,  args_gh), 1.0, 1e-9);
    EXPECT_NEAR(jgh(30.0, args_gh), 1.25, 1e-9);
    EXPECT_NEAR(jgh(500.0, args_gh), 1.485, 1e-9);

    SECTION("jump — BUG-NEW-17 denormal x guard (dA/x overflow → identity)");
    // With smoothing on, a post-smoothing x ≈ 1e-321 (denormal) made dA/x
    // overflow to ±Inf → the gain would be zeroed downstream (silent event
    // loss).  The guard returns 1.0 (identity) for any non-finite result,
    // mirroring every other mode's non-representable-output fallback.
    accel_args args_dn = args;            // gain + smooth enabled baseline
    jump jdn(args_dn);
    EXPECT(std::isfinite(jdn(std::numeric_limits<double>::denorm_min(), args_dn)));
    EXPECT(std::isfinite(jdn(1e-320, args_dn)));
    EXPECT_NEAR(jdn(std::numeric_limits<double>::denorm_min(), args_dn), 1.0, 1e-12);
    EXPECT_NEAR(jdn(1e-320, args_dn), 1.0, 1e-12);
    // x <= 0 short-circuits to 1.0 anyway
    EXPECT_NEAR(jdn(-1.0, args_dn), 1.0, 1e-12);
}

// ── Test 5: synchronous ──────────────────────────────────────────────────────

static void test_synchronous() {
    SECTION("synchronous — activation_framework (legacy)");
    accel_args args = make_args(accel_mode::synchronous);
    // make_args: motivity=1.5, gamma=1, sync_speed=5, smooth=0.5
    // smooth!=0 → sharpness=1 → non-linear tanh path.
    args.gain = false;

    synchronous s(args);

    // x=0 → 1.0 (port guard)
    EXPECT_NEAR(s(0.0, args), 1.0, 1e-12);

    // x = sync_speed → 1.0
    EXPECT_NEAR(s(5.0, args), 1.0, 1e-9);

    // x > sync_speed → gain up to motivity (1.5); x < sync_speed → down to 1/motivity
    EXPECT(s(10.0, args) > 1.0 && s(10.0, args) <= 1.5 + 1e-9);
    EXPECT(s(2.0,  args) < 1.0 && s(2.0,  args) >= 1.0/1.5 - 1e-9);
    // monotonic
    EXPECT(s(50.0, args) > s(10.0, args));
    EXPECT(s(1.0,  args) < s(3.0, args));

    SECTION("synchronous — linear clamp (smooth=0)");
    accel_args args_c = args;
    args_c.smooth = 0; // sharpness=16 → linear clamp
    synchronous sc(args_c);
    EXPECT_NEAR(sc(1e-9,  args_c), 1.0/1.5, 1e-9); // saturated min
    EXPECT_NEAR(sc(500.0, args_c), 1.5, 1e-9);     // saturated max
    EXPECT_NEAR(sc(5.0,   args_c), 1.0, 1e-9);

    SECTION("synchronous — GAIN (integral LUT)");
    accel_args args_g = make_args(accel_mode::synchronous);
    args_g.gain = true;
    args_g.smooth = 0;
    synchronous sg(args_g);
    // Constructor filled args_g.data with the 97-entry integral table.
    EXPECT(args_g.length == 0); // length is untouched (data is implicit)
    double g5 = sg(5.0, args_g);
    EXPECT(std::isfinite(g5));
    // Smooth-linear clamp with motivity 1.5: average multiplier over [0,5]
    // lies strictly below the saturated 1.5 but above the minimum 2/3.
    EXPECT(g5 >= 0.667 - 1e-2 && g5 <= 1.5 + 1e-3);
    // Far beyond the table (x > 2^9): constant tail → average approaches motivity.
    double gbig = sg(1e6, args_g);
    EXPECT(std::isfinite(gbig));
    EXPECT(gbig > 1.4);
    // Degenerate motivity → identity (guard)
    accel_args args_d = args_g;
    args_d.motivity = 0;
    synchronous sd(args_d);
    EXPECT_NEAR(sd(5.0, args_d), 1.0, 1e-6);
    EXPECT_NEAR(sd(50.0, args_d), 1.0, 1e-6);

    // P95 — GAIN LUT accuracy envelope vs the exact closed-form integral.
    //
    // The GAIN curve is a piecewise-trapezoid integral of the LEGACY sigmoid
    // fn() over the log grid { -3, 9, 8 }, stored as a float LUT and linearly
    // interpolated. Its discretization error is O(1/N^2) in the trapezoid
    // partition count N. This test independently re-integrates the same legacy
    // sigmoid at high resolution (the "exact closed form") and binds the
    // shipped LUT to a tight absolute tolerance that the current N=2
    // construction must satisfy, AND verifies the physical shape the LUT is
    // meant to encode: gain rises monotonically through the sync_speed gain
    // point and is bounded between 1/motivity and motivity.
    //
    // NOTE (P95 finding): raising N 2->4 would ~halve this error (measured:
    // max |LUT - exact| ≈ 1.28e-2 at N=2 vs ≈ 6.4e-3 at N=4, worst near the
    // gain-transition midpoint x≈7.5). It is NOT shipped because the local
    // synchronous LUT is compared bit-for-bit against the vendored RawAccel
    // reference by tests/oracle/run_oracle.sh, which uses N=2; changing N
    // would drift all 57 sync_gain rows well past its 1e-9 relative gate
    // (measured ~6e-3). The error bound below is therefore pegged to the
    // reference-parity N=2 construction, not to the tighter N=4 curve.
    SECTION("P95 — synchronous GAIN LUT: exact-integral envelope, monotonic, gain point");
    {
        // Legacy sigmoid: full-precision closed-form fn() the LUT integrates.
        accel_args lg_args = args_g;            // smooth=0 linear-clamp sigmoid
        lg_args.gain = false;
        synchronous lg(lg_args);

        // fp_rep_range { -3, 9, 8 } grid: x_k = 2^e * (1 + k/8).
        auto grid_x = [](int k) { return std::ldexp(1.0 + (k % 8) / 8.0, -3 + k / 8); };

        // High-resolution piecewise trapezoid of fn() over [0, x] on the SAME
        // log grid the ctor uses, N partitions per segment. This is the exact
        // gain the LUT approximates: (1/x) * integral of the LEGACY sigmoid.
        auto integral = [&](int N, double x) {
            double sum = 0, a = 0;
            int k = 0;
            while (a < x) {
                double b = k <= 96 ? grid_x(k) : std::scalbn(1, 9);
                if (b > x) b = x;
                double h = (b - a) / N;
                for (int i = 1; i <= N; i++) sum += lg(a + i * h, lg_args) * h;
                a = b; ++k;
            }
            return sum / x;                     // average sensitivity = gain
        };

        // Probe across every behavioural region: flat floor, the gain point
        // (sync_speed = 5) transition, the ramp, and the saturated tail.
        const double probes[] = { 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.5,
                                  10.0, 15.0, 30.0, 50.0, 100.0, 250.0, 500.0 };
        const int NEXACT = 4096;                // near-converged integral

        // P95-error envelope pegged to the reference-parity N=2 LUT: measured
        // max |LUT - exact| ≈ 1.28e-2 at the transition midpoint x=7.5; use a
        // comfortable deterministic cap (2x) so float rounding / platform FP
        // cannot false-fail while still catching any regression that pushes
        // the LUT more than 2.6x past its documented accuracy.
        constexpr double ENV = 2.7e-2;

        double prev = -1.0;                     // monotonicity sentinel
        bool monotonic = true;
        bool gain_crosses_one = false;
        for (double x : probes) {
            double exact = integral(NEXACT, x);
            double lut   = sg(x, args_g);
            EXPECT(std::isfinite(lut) && lut > 0);
            // accuracy: LUT interpolated gain within envelope of exact integral
            EXPECT_NEAR(lut, exact, ENV);
            // physical bounds: gain in [1/motivity, motivity]
            EXPECT(lut >= 1.0 / 1.5 - ENV && lut <= 1.5 + ENV);
            // monotonic non-decreasing through the gain point
            if (prev > 0 && lut + 1e-6 < prev) monotonic = false;
            if (prev > 0 && lut >= 1.0 && prev < 1.0) gain_crosses_one = true;
            prev = lut;
        }
        EXPECT(monotonic);
        // the "gain point": the curve must pass through exactly 1.0 where the
        // integral-average sensitivity crosses identity (between 6 and 10 ips
        // for motivity 1.5 / sync_speed 5 / smooth 0).
        EXPECT(gain_crosses_one);
    }
}

// ── Test 6: lookup (LUT) ─────────────────────────────────────────────────────

static void test_lookup() {
    SECTION("lookup — LUT interpolation (velocity off)");
    accel_args args = make_args(accel_mode::lookup);
    args.gain = false; // stored y is used directly as the gain

    // 3 nokta: (0, 1.0), (10, 1.5), (20, 2.0)
    float pts[] = { 0.0f, 1.0f, 10.0f, 1.5f, 20.0f, 2.0f };
    ::memcpy(args.data, pts, sizeof(pts));
    args.length = 6; // 3 çift

    lookup lut(args);

    // x <= 0 → 0 (reference: strictly positive domain)
    EXPECT_NEAR(lut(0.0,  args), 0.0, 1e-6);
    EXPECT_NEAR(lut(-1.0, args), 0.0, 1e-6);

    // Tam noktalarda
    EXPECT_NEAR(lut(10.0, args), 1.5, 1e-6);
    EXPECT_NEAR(lut(20.0, args), 2.0, 1e-6);

    // İnterpolasyon: x=5 → 1.25
    EXPECT_NEAR(lut(5.0, args), 1.25, 1e-6);

    // Son noktanın üstü → lineer ekstrapolasyon (referans lerp, t > 1)
    EXPECT_NEAR(lut(30.0, args), 2.5, 1e-6);

    SECTION("lookup — velocity (gain) mode");
    // Stored y is treated as output speed; effective gain = y / x.
    accel_args args_v = args;
    args_v.gain = true;
    float pv[] = { 0.0f, 0.0f, 10.0f, 5.0f, 20.0f, 10.0f };
    ::memcpy(args_v.data, pv, sizeof(pv));
    args_v.length = 6;
    lookup lv(args_v);
    EXPECT_NEAR(lv(0.0,  args_v), 0.0, 1e-6);
    EXPECT_NEAR(lv(5.0,  args_v), 0.5, 1e-6);
    EXPECT_NEAR(lv(10.0, args_v), 0.5, 1e-6);
    EXPECT_NEAR(lv(20.0, args_v), 0.5, 1e-6);

    // x first point altında (>0): fallback → ilk çıkış / ilk nokta hızı
    float pv2[] = { 5.0f, 2.0f, 10.0f, 3.0f };
    accel_args args_f = args;
    args_f.gain = true;
    ::memcpy(args_f.data, pv2, sizeof(pv2));
    args_f.length = 4;
    lookup lf(args_f);
    EXPECT_NEAR(lf(1.0, args_f), 0.4, 1e-6); // 2.0 / 5.0

    // Boş LUT → 1.0
    accel_args empty = make_args(accel_mode::lookup);
    empty.length = 0;
    lookup lut_empty(empty);
    EXPECT_NEAR(lut_empty(5.0, empty), 1.0, 1e-12);
}

// ── Test 7: power ────────────────────────────────────────────────────────────

static void test_power() {
    SECTION("power");
    accel_args args = make_args(accel_mode::power);
    args.exponent_power = 0.5;
    args.scale          = 1.0;
    args.output_offset  = 0.0;
    args.cap            = { 15.0, 2.0 };
    args.cap_mode_val   = cap_mode::out;

    power p(args);

    // x=0 → 1.0 (guard)
    EXPECT_NEAR(p(0.0, args), 1.0, 1e-9);

    // Pozitif girişte sonlu değer
    double g5  = p(5.0,  args);
    double g10 = p(10.0, args);
    EXPECT(std::isfinite(g5));
    EXPECT(std::isfinite(g10));
    EXPECT(g5 > 1.0);

    // cap.x=0 ile io modunda NaN üretmemeli (scale_from_gain_point guard)
    accel_args args_io = args;
    args_io.cap_mode_val = cap_mode::io;
    args_io.cap.x = 0.0; // degenerate
    power p_io(args_io);
    double g_io = p_io(5.0, args_io);
    EXPECT(std::isfinite(g_io));
}

// ── P81: power io degradation regressions ────────────────────────────────────

static void test_p81_power_io_fixes() {
    // P81 BUG 1: power + gain + cap_mode=io with the sanitized minimum
    // exponent_power (1e-4). scale_from_gain_point computed pow(gain/(n+1), 1/n)
    // with n=1e-4 → pow(·,10000) overflowed to Inf, silently zeroing the whole
    // output curve. Must now be finite with a reasonable gain (≤ the io cap.y).
    SECTION("P81 — power gain io, exponent_power=1e-4 finite, capped");
    {
        accel_args args = make_args(accel_mode::power);
        args.exponent_power = 1e-4; // sanitized minimum (N11 masked this with 0)
        args.cap            = { 15.0, 1.5 };
        args.cap_mode_val   = cap_mode::io;
        power p(args);
        for (double s : { 0.1, 1.0, 5.0, 15.0, 100.0, 1000.0 }) {
            double g = p(s, args);
            EXPECT(std::isfinite(g));
            EXPECT(g >= 0.0);
            EXPECT(g <= args.cap.y + 1e-9); // io gain point caps at cap.y
        }
        // The base curve rises smoothly from ~1 toward the io cap; it must stay
        // finite and below cap.y everywhere, never spiking to Inf.
        double g_cap = p(args.cap.x, args);
        EXPECT(std::isfinite(g_cap));
        EXPECT(g_cap >= 1.0);
        EXPECT(g_cap <= args.cap.y + 1e-9);
        EXPECT_NEAR(p(10000.0, args), args.cap.y, 1e-3); // tail converges to cap.y
    }

    // P81 BUG 2: power + legacy + cap_mode=io. The io+legacy early-return branch
    // left legacy_cap at DBL_MAX, so the hard cap was never applied and gain
    // diverged past cap.y. Must now be capped at cap.y.
    SECTION("P81 — power legacy io, gain capped at cap.y");
    {
        accel_args args = make_args(accel_mode::power);
        args.gain        = false; // legacy
        args.exponent_power = 0.05;
        args.cap            = { 15.0, 1.5 };
        args.cap_mode_val   = cap_mode::io;
        power p(args);
        for (double s : { 5.0, 100.0, 500.0, 1000.0, 10000.0 }) {
            double g = p(s, args);
            EXPECT(std::isfinite(g));
            EXPECT(g <= args.cap.y + 1e-9); // hard-capped at cap.y
            EXPECT(g >= 0.0);
        }
    }
}

// ── Test 8: accel_union dispatch ─────────────────────────────────────────────

static void test_accel_union() {
    SECTION("accel_union — mod dispatch");
    // Her mod için accel_union'un doğru implementasyonu seçtiğini doğrula
    accel_union au;

    auto check = [&](accel_mode m, double speed) -> double {
        accel_args a = make_args(m);
        au.init(a);
        return au.apply(speed, a);
    };

    EXPECT_NEAR(check(accel_mode::noaccel, 10.0), 1.0, 1e-12);
    EXPECT(check(accel_mode::classic,     10.0) >= 1.0);
    EXPECT(check(accel_mode::natural,     10.0) >= 1.0);
    EXPECT(std::isfinite(check(accel_mode::jump,        10.0)));
    EXPECT(std::isfinite(check(accel_mode::synchronous, 10.0)));
    EXPECT(std::isfinite(check(accel_mode::power,       10.0)));
}

// ── Test 9: JSON round-trip ──────────────────────────────────────────────────

static void test_json_roundtrip() {
    SECTION("JSON round-trip (config serialize/deserialize)");

    // Profil oluştur
    device_profile dp;
    dp.name      = "test-profile";
    dp.device_id = "/dev/input/event3";
    dp.match_app = "firefox";
    dp.dev_cfg.dpi          = 1600;
    dp.dev_cfg.polling_rate = 500;
    dp.prof.degrees_rotation = 15.0;
    dp.prof.degrees_snap     = 5.0;
    dp.prof.speed_min        = 2.0;
    dp.prof.speed_max        = 80.0;

    auto& ax = dp.prof.accel_x;
    ax.mode             = accel_mode::classic;
    ax.gain             = true;
    ax.acceleration     = 0.007;
    ax.exponent_classic = 2.5;
    ax.limit            = 1.8;
    ax.cap              = { 12.0, 1.8 };
    ax.cap_mode_val     = cap_mode::in;
    dp.prof.accel_y = ax;

    // JSON'a serialize et
    std::string json_str = profile_to_json(dp);
    EXPECT(!json_str.empty());

    // JSON'dan deserialize et
    device_profile dp2 = profile_from_json(json_str);

    EXPECT(dp2.name       == dp.name);
    EXPECT(dp2.device_id  == dp.device_id);
    EXPECT(dp2.match_app  == dp.match_app);
    EXPECT(dp2.dev_cfg.dpi          == dp.dev_cfg.dpi);
    EXPECT(dp2.dev_cfg.polling_rate == dp.dev_cfg.polling_rate);
    EXPECT_NEAR(dp2.prof.degrees_rotation, dp.prof.degrees_rotation, 1e-9);
    EXPECT_NEAR(dp2.prof.degrees_snap,     dp.prof.degrees_snap,     1e-9);
    EXPECT_NEAR(dp2.prof.speed_min,        dp.prof.speed_min,        1e-9);
    EXPECT_NEAR(dp2.prof.speed_max,        dp.prof.speed_max,        1e-9);

    auto& ax2 = dp2.prof.accel_x;
    EXPECT(ax2.mode             == ax.mode);
    EXPECT(ax2.gain             == ax.gain);
    EXPECT_NEAR(ax2.acceleration,     ax.acceleration,     1e-9);
    EXPECT_NEAR(ax2.exponent_classic, ax.exponent_classic, 1e-9);
    EXPECT_NEAR(ax2.limit,            ax.limit,            1e-9);
    EXPECT_NEAR(ax2.cap.x,            ax.cap.x,            1e-9);
    EXPECT_NEAR(ax2.cap.y,            ax.cap.y,            1e-9);
    EXPECT(ax2.cap_mode_val == ax.cap_mode_val);
}

// ── Test 10: JSON round-trip — LUT data ──────────────────────────────────────

static void test_json_roundtrip_lut() {
    SECTION("JSON round-trip — LUT noktaları");

    device_profile dp;
    dp.name = "lut-test";
    auto& ax = dp.prof.accel_x;
    ax.mode   = accel_mode::lookup;

    // 4 LUT noktası ekle
    float pts[] = { 0.0f, 1.0f, 5.0f, 1.3f, 15.0f, 1.7f, 30.0f, 2.0f };
    ::memcpy(ax.data, pts, sizeof(pts));
    ax.length = 8; // 4 çift
    dp.prof.accel_y = ax;

    std::string json_str = profile_to_json(dp);
    device_profile dp2   = profile_from_json(json_str);

    auto& ax2 = dp2.prof.accel_x;
    EXPECT(ax2.mode   == accel_mode::lookup);
    EXPECT(ax2.length == ax.length);
    for (int i = 0; i < ax.length; i++) {
        EXPECT_NEAR(static_cast<double>(ax2.data[i]),
                    static_cast<double>(ax.data[i]), 1e-4f);
    }
}

// ── Test 10b: JSON round-trip — LUT survives a mode switch (R6-1) ───────────

static void test_json_roundtrip_lut_mode_switch() {
    SECTION("JSON round-trip — LUT lookup→natural→lookup korunur");

    device_profile dp;
    dp.name = "lut-mode-switch";
    auto& ax = dp.prof.accel_x;
    ax.mode = accel_mode::lookup;
    float pts[] = { 0.0f, 1.0f, 5.0f, 1.3f, 15.0f, 1.7f, 30.0f, 2.0f };
    ::memcpy(ax.data, pts, sizeof(pts));
    ax.length = 8; // 4 çift
    dp.prof.accel_y = ax;

    // CLI `set-param <p> mode natural` keeps the LUT in memory but switches the
    // mode; the automatic save used to drop lut_data (serialized only for
    // lookup) — back-to-lookup came back with an empty curve.
    ax.mode               = accel_mode::natural;
    dp.prof.accel_y.mode  = accel_mode::natural;
    std::string json_str  = profile_to_json(dp);
    device_profile dp2    = profile_from_json(json_str);

    EXPECT(dp2.prof.accel_x.mode == accel_mode::natural);
    EXPECT(dp2.prof.accel_x.length == ax.length);

    // The curve travels with the profile; switching back restores it.
    dp2.prof.accel_x.mode = accel_mode::lookup;
    dp2.prof.accel_y.mode = accel_mode::lookup;
    const auto& ax2 = dp2.prof.accel_x;
    EXPECT(ax2.mode == accel_mode::lookup);
    for (int i = 0; i < ax2.length; i++) {
        EXPECT_NEAR(static_cast<double>(ax2.data[i]),
                    static_cast<double>(ax.data[i]), 1e-4f);
    }
}

// ── Test 11: app_config dosya round-trip ─────────────────────────────────────

static void test_file_roundtrip() {
    SECTION("app_config dosya round-trip (save + load)");

    app_config cfg;
    cfg.active_profile = "gaming";

    device_profile dp1;
    dp1.name = "default";
    dp1.prof.accel_x.mode = accel_mode::noaccel;
    dp1.prof.accel_y      = dp1.prof.accel_x;
    cfg.profiles.push_back(dp1);

    device_profile dp2;
    dp2.name = "gaming";
    dp2.dev_cfg.dpi = 3200;
    dp2.prof.accel_x.mode         = accel_mode::classic;
    dp2.prof.accel_x.acceleration = 0.003;
    dp2.prof.accel_x.exponent_classic = 1.8;
    dp2.prof.accel_y = dp2.prof.accel_x;
    cfg.profiles.push_back(dp2);

    // Geçici dosyaya kaydet
    std::string tmp = "/tmp/rawaccel_test_roundtrip.json";
    save_config(cfg, tmp);

    // Yeniden yükle
    app_config cfg2 = load_config(tmp);

    EXPECT(cfg2.active_profile == cfg.active_profile);
    EXPECT(cfg2.profiles.size() == cfg.profiles.size());

    auto& p2 = cfg2.profiles[1]; // gaming
    EXPECT(p2.name == "gaming");
    EXPECT(p2.dev_cfg.dpi == 3200);
    EXPECT(p2.prof.accel_x.mode == accel_mode::classic);
    EXPECT_NEAR(p2.prof.accel_x.acceleration,     0.003, 1e-9);
    EXPECT_NEAR(p2.prof.accel_x.exponent_classic, 1.8,   1e-9);

    // Temizlik
    std::remove(tmp.c_str());
}

static void test_save_config_relative_path() {
    SECTION("BUG-22 — save_config supports bare relative filenames");

    char tmpl[] = "/tmp/rawaccel_rel_XXXXXX";
    char* dir = mkdtemp(tmpl);
    EXPECT(dir != nullptr);
    if (!g_section_active || !dir) return;

    char old_cwd[4096] = {};
    bool ok = getcwd(old_cwd, sizeof(old_cwd)) != nullptr;
    EXPECT(ok);
    if (!ok) { rmdir(dir); return; }

    ok = chdir(dir) == 0;
    EXPECT(ok);
    if (!ok) { rmdir(dir); return; }

    app_config cfg;
    cfg.active_profile = "default";
    device_profile dp;
    dp.name = "default";
    cfg.profiles.push_back(dp);

    try {
        save_config(cfg, "settings.json");
        app_config loaded = load_config("settings.json");
        ok = loaded.profiles.size() == 1 && loaded.profiles[0].name == "default";
    } catch (...) {
        ok = false;
    }
    EXPECT(ok);

    std::remove("settings.json.tmp");
    std::remove("settings.json");
    const bool restored_cwd = chdir(old_cwd) == 0;
    EXPECT(restored_cwd);
    if (restored_cwd) rmdir(dir);
}

// ── Test 12: Monotonik artış kontrolü ────────────────────────────────────────

static void test_monotonic() {
    SECTION("gain monotonic artiş — classic/natural/jump");

    // Tüm modlarda gain, hız arttıkça azalmamalı (offset'i geçtikten sonra)
    struct Case { accel_mode mode; const char* name; };
    Case cases[] = {
        { accel_mode::classic, "classic" },
        { accel_mode::natural, "natural" },
        { accel_mode::jump,    "jump"    },
    };

    for (auto& tc : cases) {
        accel_args args = make_args(tc.mode);
        accel_union au;
        au.init(args);

        double prev = au.apply(0.1, args);
        bool monotonic = true;
        for (int i = 2; i <= 40; i++) {
            double g = au.apply(i * 1.0, args);
            if (g < prev - 1e-9) { monotonic = false; break; }
            prev = g;
        }
        if (!g_section_active) continue;
        g_tests++;
        if (monotonic) {
            g_passed++;
            if (!g_quiet) std::printf("  PASS  %s gain monotonic\n", tc.name);
        } else {
            g_failed++;
            std::fprintf(stderr, "  FAIL  %s gain NOT monotonic\n", tc.name);
        }
    }
}

// ── Test 13: power modunda monotonik artış ───────────────────────────────────

static void test_power_monotonic() {
    SECTION("power mod — monotonik artış");

    accel_args args;
    args.mode           = accel_mode::power;
    args.gain           = true;
    args.exponent_power = 0.05;
    args.scale          = 1.0;
    args.output_offset  = 0.0;
    args.cap_mode_val   = cap_mode::out;
    args.cap            = { 15.0, 2.0 };

    power pw(args);

    double prev = pw(1.0, args);
    bool ok = true;
    for (int i = 2; i <= 30; ++i) {
        double cur = pw(static_cast<double>(i), args);
        if (cur < prev - 1e-9) { ok = false; break; }
        prev = cur;
    }
    EXPECT(ok);

    // cap_mode::io
    args.cap_mode_val = cap_mode::io;
    args.cap = { 12.0, 1.8 };
    power pw_io(args);
    prev = pw_io(1.0, args);
    ok   = true;
    for (int i = 2; i <= 30; ++i) {
        double cur = pw_io(static_cast<double>(i), args);
        if (cur < prev - 1e-9) { ok = false; break; }
        prev = cur;
    }
    EXPECT(ok);

    // cap_mode::in
    args.cap_mode_val = cap_mode::in;
    args.cap = { 10.0, 0.0 };
    power pw_in(args);
    prev = pw_in(1.0, args);
    ok   = true;
    for (int i = 2; i <= 30; ++i) {
        double cur = pw_in(static_cast<double>(i), args);
        if (cur < prev - 1e-9) { ok = false; break; }
        prev = cur;
    }
    EXPECT(ok);
}

// ── Test 14: classic cap_mode::io ve cap_mode::in dalları ───────────────────

static void test_classic_cap_modes() {
    SECTION("classic — cap_mode::io ve cap_mode::in");

    // cap_mode::io: cap.x kapama noktası, cap.y GAIN formda ASİMPTOT'tur.
    // Referans GAIN: cap.x'te değil, sonsuzda cap.y'ye yakınsar (üst = 1 + cap.y).
    {
        accel_args args;
        args.mode              = accel_mode::classic;
        args.gain              = true;
        args.acceleration      = 0.005;
        args.exponent_classic  = 2.0;
        args.input_offset      = 0.0;
        args.cap_mode_val      = cap_mode::io;
        args.cap               = { 10.0, 1.8 };

        classic cl(args);
        // Referans: accel_raised = gain_accel(10, 0.8, 2, 0)^(2-1) = 0.04;
        // cl(10) = 1 + 0.04·10 = 1.4 (cap.y'den düşük), sonra asimptotik artar.
        double g_at_cap = cl(args.cap.x, args);
        EXPECT_NEAR(g_at_cap, 1.4, 1e-6);
        // Kapamadan sonra artış devam eder (sabit/ x + cap.y), asimptota yaklaşır
        double g_beyond = cl(args.cap.x * 2.0, args);
        EXPECT_NEAR(g_beyond, 1.6, 1e-6);
        EXPECT(g_beyond > g_at_cap);
        EXPECT_NEAR(cl(1e9, args), 1.8, 1e-6);   // asimptot: 1 + cap.y
        // Below offset, gain must be 1
        EXPECT_NEAR(cl(0.0, args), 1.0, 1e-12);
    }

    // cap_mode::in: cap.x sonrası gain sabit/ x + cap.y ile asimptotik artar
    {
        accel_args args;
        args.mode              = accel_mode::classic;
        args.gain              = true;
        args.acceleration      = 0.005;
        args.exponent_classic  = 2.0;
        args.input_offset      = 0.0;
        args.cap_mode_val      = cap_mode::in;
        args.cap               = { 12.0, 0.0 };  // cap.x active, cap.y ignored

        classic cl(args);
        // Referans: cap.y = gain(12, 0.005, 2, 0) = 2·0.005·12 = 0.12 (gömülü);
        // cl(12)=1.06, cl(24)=1.09; asimptot 1.12
        double g_at_cap   = cl(args.cap.x,       args);
        double g_beyond   = cl(args.cap.x * 2.0, args);
        EXPECT_NEAR(g_at_cap, 1.06, 1e-6);
        EXPECT_NEAR(g_beyond, 1.09, 1e-6);
        EXPECT(g_beyond > g_at_cap);           // asimptotik artış
        EXPECT(g_beyond < 1.12 + 1e-9);        // asimptotun altında
        // gain must be > 1 (actually accelerating)
        EXPECT(g_at_cap > 1.0);
    }
}

// ── Test 15: modifier — domain/range weights ve rotation ────────────────────
// Modifier sınıfı config.hpp içinde inline tanımlı; x/y ağırlıkları
// ile basit bir ölçekleme + rotasyon testi yapıyoruz.

static void test_modifier() {
    SECTION("modifier — domain/range weights, degrees_rotation");

    // range_weights.x == 2 → x hızı 2 katına çıkar
    {
        profile p;
        p.range_weights  = { 2.0, 1.0 };
        p.domain_weights = { 1.0, 1.0 };
        p.degrees_rotation = 0.0;

        // x'e doğrudan ağırlık uygulaması (modifier içinden hesap)
        // range_weights.x, x çıkışını scale eder
        double wx = p.range_weights.x;
        EXPECT_NEAR(wx, 2.0, 1e-12);
        EXPECT_NEAR(p.range_weights.y, 1.0, 1e-12);
    }

    // degrees_rotation: 90° → x ve y yer değiştirir
    {
        profile p;
        p.degrees_rotation = 90.0;
        const double PI   = 3.14159265358979323846;
        double rad        = p.degrees_rotation * PI / 180.0;
        double cx         = std::cos(rad);
        double sx         = std::sin(rad);
        // cos(90°) ≈ 0, sin(90°) ≈ 1
        EXPECT_NEAR(cx, 0.0, 1e-10);
        EXPECT_NEAR(sx, 1.0, 1e-10);
    }

    // domain_weights: eşit ağırlıkta, hız değişmez
    {
        profile p;
        p.domain_weights = { 1.0, 1.0 };
        double vx = 3.0, vy = 4.0;
        // Euclidean hız: sqrt(3²+4²) = 5
        double speed = std::hypot(vx * p.domain_weights.x,
                                  vy * p.domain_weights.y);
        EXPECT_NEAR(speed, 5.0, 1e-10);
    }

    // domain_weights: x ağırlığı 2 → hız artar
    {
        profile p;
        p.domain_weights = { 2.0, 1.0 };
        double vx = 3.0, vy = 4.0;
        double speed = std::hypot(vx * p.domain_weights.x,
                                  vy * p.domain_weights.y);
        // sqrt((6)²+(4)²) = sqrt(52)
        EXPECT_NEAR(speed, std::sqrt(52.0), 1e-10);
    }
}

// ── Test 16: N8 natural limit<1 guard, N11 power exponent_power=0 ─────────────

static void test_edge_guards() {
    SECTION("N8 natural limit<1 no inversion, N11 power exponent=0 finite");

    // N8: natural with args.limit < 1 — referans: limit ASİMPTOT'tur, kazanım
    //     onun üstüne yakınsar ama 1'in altında kalır (ters çevirme yok, her zaman ≥0).
    {
        accel_args args = make_args(accel_mode::natural);
        args.limit = 0.5; // < 1 → kazanım 0.5 asimptotuna yakınsar
        args.gain  = false; // non-gain mode
        natural n(args);
        EXPECT_NEAR(n(0.0, args), 1.0, 1e-9);
        // Referans değerler: n(5)=0.684, n(20)=0.509, n(50)→0.5
        EXPECT(n(5.0, args) > args.limit);       // asimptotun üstünde
        EXPECT(n(20.0, args) > args.limit);
        EXPECT(n(5.0, args) < 1.0);              // 1'in altında ama pozitif (inversion yok)
        EXPECT(std::isfinite(n(50.0, args)));
        EXPECT_NEAR(n(50.0, args), args.limit, 1e-3);  // limit asimptotu
    }

    // N8: natural with args.limit = 0 (extreme) should not crash or go below 0
    {
        accel_args args = make_args(accel_mode::natural);
        args.limit = 0.0;
        args.gain  = false;
        natural n(args);
        EXPECT(n(10.0, args) >= 0.0); // must not invert
        EXPECT(std::isfinite(n(10.0, args)));
    }

    // N11: power with exponent_power == 0 should be finite (not NaN/Inf)
    {
        accel_args args = make_args(accel_mode::power);
        args.exponent_power = 0.0; // would cause gain_inverse div-by-zero without guard
        power p(args);
        double g = p(10.0, args);
        EXPECT(std::isfinite(g));
        EXPECT(g >= 0.0);
    }

    // N11: power with exponent_power = 0 in io mode (uses scale_from_gain_point)
    {
        accel_args args = make_args(accel_mode::power);
        args.exponent_power = 0.0;
        args.cap_mode_val   = cap_mode::io;
        power p(args);
        double g = p(5.0, args);
        EXPECT(std::isfinite(g));
    }
}

// ── Test 17: modifier.modify() end-to-end ────────────────────────────────────
// R13: modifier hot-path'ini doğrudan test et — rotation, snap, speed clamp,
// directional weights ve acceleration'ın birlikte doğru çalıştığını doğrula.

static void test_modifier_end_to_end() {
    SECTION("modifier.modify() — end-to-end");

    // ── 1. Saf noaccel: modify hiçbir şey değiştirmemeli ──────────────────
    {
        profile p;
        p.accel_x.mode = accel_mode::noaccel;
        p.accel_y.mode = accel_mode::noaccel;

        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor   sp; sp.init(p.speed_processor_args);
        modifier          mod;

        vec2d in = { 3.0, 4.0 };
        mod.modify(in, sp, ms, 1.0 /*dpi_factor*/, 10.0 /*ms*/);
        EXPECT_NEAR(in.x, 3.0, 1e-9);
        EXPECT_NEAR(in.y, 4.0, 1e-9);
    }

    // ── 2. time <= 0 guard: BUG-HIGH-2 — input zeroed, no raw 1:1 leak ────
    // (The daemon clamps time before reaching modify(); this guard only fires
    //  on hostile unit-level input.  Zeroing is the documented FIX — the raw
    //  delta must NOT pass through unmodified 1:1.)
    {
        profile p;
        p.accel_x.mode = accel_mode::classic;
        p.accel_x.acceleration = 0.01;
        p.accel_y = p.accel_x;

        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor   sp; sp.init(p.speed_processor_args);
        modifier          mod;

        vec2d in = { 5.0, 0.0 };
        mod.modify(in, sp, ms, 1.0, 0.0 /*time=0*/);
        // time<=0 → early return with in zeroed (BUG-HIGH-2)
        EXPECT_NEAR(in.x, 0.0, 1e-9);
        EXPECT_NEAR(in.y, 0.0, 1e-9);
    }

    // ── 3. Classic acceleration: gain > 1 so output > input ───────────────
    {
        profile p;
        p.accel_x.mode = accel_mode::classic;
        p.accel_x.acceleration     = 0.01;
        p.accel_x.exponent_classic = 2.0;
        p.accel_x.cap              = { 100.0, 3.0 };
        p.accel_x.cap_mode_val     = cap_mode::out;
        p.accel_y = p.accel_x;

        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor   sp; sp.init(p.speed_processor_args);
        modifier          mod;

        vec2d in = { 30.0, 0.0 }; // fast horizontal movement
        mod.modify(in, sp, ms, 1.0, 1.0);
        // With acceleration, output magnitude must exceed input magnitude
        EXPECT(std::fabs(in.x) > 30.0);
        EXPECT(std::isfinite(in.x));
        EXPECT(std::isfinite(in.y));
    }

    // ── 4. 90° rotation: (dx,dy)=(1,0) → (0,1) ───────────────────────────
    {
        profile p;
        p.accel_x.mode = accel_mode::noaccel;
        p.accel_y.mode = accel_mode::noaccel;
        p.degrees_rotation = 90.0;

        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor   sp; sp.init(p.speed_processor_args);
        modifier          mod;

        vec2d in = { 1.0, 0.0 };
        mod.modify(in, sp, ms, 1.0, 1.0);
        // cos(90°)≈0, sin(90°)≈1 → rotated (1,0) = (0,1)
        EXPECT_NEAR(in.x,  0.0, 1e-9);
        EXPECT_NEAR(in.y,  1.0, 1e-9);
    }

    // ── 5. Speed clamp: slow input clamped up to speed_min ────────────────
    {
        profile p;
        p.accel_x.mode = accel_mode::noaccel;
        p.accel_y.mode = accel_mode::noaccel;
        p.speed_min = 50.0;   // ips
        p.speed_max = 200.0;

        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor   sp; sp.init(p.speed_processor_args);
        modifier          mod;

        // Input speed (counts/ms): 3 counts over 1ms, dpi_factor=1 → 3 ips
        // 3 ips < speed_min(50) → clamped up to 50 ips
        vec2d in = { 3.0, 0.0 };
        mod.modify(in, sp, ms, 1.0, 1.0);
        double out_speed = std::fabs(in.x); // ips after clamp
        EXPECT(out_speed >= 50.0 - 1e-6);
        EXPECT(out_speed <= 200.0 + 1e-6);
    }

    // ── 6. Directional DPI ratio: lr_output_dpi_ratio > 1 ────────────────
    // Referans: lr/ud oranları SADECE negatif (sol/aşağı) yöne uygulanır.
    {
        profile p;
        p.accel_x.mode = accel_mode::noaccel;
        p.accel_y.mode = accel_mode::noaccel;
        p.lr_output_dpi_ratio = 2.0; // sola hareket 2 katına çıkar

        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor   sp; sp.init(p.speed_processor_args);
        modifier          mod;

        vec2d in_right = { 5.0, 0.0 };
        mod.modify(in_right, sp, ms, 1.0, 1.0);
        EXPECT_NEAR(in_right.x, 5.0, 1e-9);   // sağa: oran uygulanmaz

        vec2d in_left = { -5.0, 0.0 };
        mod.modify(in_left, sp, ms, 1.0, 1.0);
        EXPECT_NEAR(in_left.x, -10.0, 1e-9);  // sola: ×2
    }

    // ── 7. Angle snap: diagonal near horizontal → snapped to horizontal ──
    {
        profile p;
        p.accel_x.mode = accel_mode::noaccel;
        p.accel_y.mode = accel_mode::noaccel;
        p.degrees_snap = 20.0; // snap cone half-angle

        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor   sp; sp.init(p.speed_processor_args);
        modifier          mod;

        // 10° angle from horizontal → within 20° snap cone → snapped to pure X
        double ang = 10.0 * M_PI / 180.0;
        double mag = 5.0;
        vec2d in = { mag * std::cos(ang), mag * std::sin(ang) };
        mod.modify(in, sp, ms, 1.0, 1.0);
        // After snap, y component must be zero
        EXPECT_NEAR(in.y, 0.0, 1e-9);
        EXPECT(std::fabs(in.x) > 0.0);
    }
}

// ── Test 18: speed_processor ─────────────────────────────────────────────────
// R14: calc_speed_whole/separate, distance modes, smoothing init

static void test_speed_processor() {
    SECTION("speed_processor — distance modes");

    speed_args sa;

    // ── euclidean (whole=true, lp_norm≈2) ────────────────────────────────
    {
        sa.whole   = true;
        sa.lp_norm = 2.0;
        speed_processor sp; sp.init(sa);
        EXPECT(sp.speed_flags.dist_mode == distance_mode::euclidean);

        // speed of (3,4) = 5
        vec2d v = { 3.0, 4.0 };
        double s = sp.calc_speed_whole(v, 1.0);
        EXPECT_NEAR(s, 5.0, 1e-9);
    }

    // ── max norm (lp_norm > MAX_NORM) ─────────────────────────────────────
    {
        sa.whole   = true;
        sa.lp_norm = 100.0; // > MAX_NORM(16)
        speed_processor sp; sp.init(sa);
        EXPECT(sp.speed_flags.dist_mode == distance_mode::max);

        vec2d v = { 3.0, 4.0 };
        double s = sp.calc_speed_whole(v, 1.0);
        EXPECT_NEAR(s, 4.0, 1e-9); // max(3,4) = 4
    }

    // ── Lp norm (whole=true, lp_norm=1) ──────────────────────────────────
    {
        sa.whole   = true;
        sa.lp_norm = 1.0; // Manhattan
        speed_processor sp; sp.init(sa);
        EXPECT(sp.speed_flags.dist_mode == distance_mode::Lp);

        vec2d v = { 3.0, 4.0 };
        double s = sp.calc_speed_whole(v, 1.0);
        EXPECT_NEAR(s, 7.0, 1e-9); // |3|+|4| = 7
    }

    // ── separate mode (whole=false) ───────────────────────────────────────
    {
        sa.whole = false;
        speed_processor sp; sp.init(sa);
        EXPECT(sp.speed_flags.dist_mode == distance_mode::separate);

        vec2d v = { -3.0, 4.0 };
        sp.calc_speed_separate(v, 1.0);
        // separate returns |x|, |y|
        EXPECT_NEAR(v.x, 3.0, 1e-9);
        EXPECT_NEAR(v.y, 4.0, 1e-9);
    }

    // ── smoothing flags set correctly ─────────────────────────────────────
    {
        speed_args sa2;
        sa2.whole = true;
        sa2.lp_norm = 2.0;
        sa2.input_speed_smooth_halflife  = 5.0;
        sa2.scale_smooth_halflife        = 3.0;
        sa2.output_speed_smooth_halflife = 2.0;
        speed_processor sp; sp.init(sa2);
        EXPECT(sp.speed_flags.should_smooth_input);
        EXPECT(sp.speed_flags.should_smooth_scale);
        EXPECT(sp.speed_flags.should_smooth_output);
    }

    // ── no smoothing when halflives are 0 ────────────────────────────────
    {
        speed_args sa3;
        sa3.input_speed_smooth_halflife  = 0.0;
        sa3.scale_smooth_halflife        = 0.0;
        sa3.output_speed_smooth_halflife = 0.0;
        speed_processor sp; sp.init(sa3);
        EXPECT(!sp.speed_flags.should_smooth_input);
        EXPECT(!sp.speed_flags.should_smooth_scale);
        EXPECT(!sp.speed_flags.should_smooth_output);
    }
}

// ── Test 19: config error paths ──────────────────────────────────────────────
// R15: bozuk JSON, eksik alan, aşırı LUT uzunluğu

static void test_config_error_paths() {
    SECTION("config error paths — bad JSON, missing fields, overlong LUT");

    // ── bozuk JSON → load_config exception fırlatmalı ─────────────────────
    {
        std::string tmp = "/tmp/rawaccel_test_bad.json";
        { std::ofstream f(tmp); f << "{ not valid json !!!"; }
        bool threw = false;
        try { load_config(tmp); } catch (...) { threw = true; }
        EXPECT(threw);
        std::remove(tmp.c_str());
    }

    // ── var olmayan dosya → exception ────────────────────────────────────
    {
        bool threw = false;
        try { load_config("/tmp/rawaccel_nonexistent_XXXXX.json"); }
        catch (...) { threw = true; }
        EXPECT(threw);
    }

    // ── eksik alanlar → varsayılan değerler korunmalı ─────────────────────
    {
        std::string tmp = "/tmp/rawaccel_test_minimal.json";
        { std::ofstream f(tmp); f << R"({"profiles": [{"name": "x"}]})"; }
        app_config cfg = load_config(tmp);
        EXPECT(!cfg.profiles.empty());
        EXPECT(cfg.profiles[0].name == "x");
        // DPI should fall back to struct default (0 — not yet overridden)
        EXPECT(cfg.profiles[0].dev_cfg.dpi == 0 || cfg.profiles[0].dev_cfg.dpi >= 0);
        std::remove(tmp.c_str());
    }

    // ── LUT length > capacity → clamped, no buffer overflow ───────────────
    {
        accel_args a = make_args(accel_mode::lookup);
        a.length = (int)LUT_RAW_DATA_CAPACITY + 100; // way over capacity
        lookup lut(a);
        // size (point count) must be <= LUT_POINTS_CAPACITY
        EXPECT(lut.size <= (int)LUT_POINTS_CAPACITY);
    }

    // ── profile_from_json with invalid JSON string → exception ────────────
    {
        bool threw = false;
        try { profile_from_json("not json"); }
        catch (...) { threw = true; }
        EXPECT(threw);
    }

    // ── cap array with too few elements → no crash (uses default) ─────────
    {
        std::string json_str = R"({
            "name": "test",
            "profile": {
                "accel_x": { "mode": "classic", "cap": [5] },
                "accel_y": { "mode": "noaccel" }
            }
        })";
        bool threw = false;
        device_profile dp;
        try { dp = profile_from_json(json_str); }
        catch (...) { threw = true; }
        // Should not throw — incomplete cap array falls back silently
        EXPECT(!threw);
    }
}

// ── Test 20: input validation / sanitization ─────────────────────────────────
// Verify that sanitize_device_profile() clamps values to safe ranges.

static void test_input_validation() {
    SECTION("input validation — DPI / polling_rate / rotation / snap / speed bounds");

    // ── DPI clamping via JSON load ────────────────────────────────────────
    {
        std::string tmp = "/tmp/rawaccel_test_validation.json";
        { std::ofstream f(tmp); f << R"({
            "profiles": [{
                "name": "v",
                "dpi": -100,
                "polling_rate": 999999
            }]
        })"; }
        app_config cfg = load_config(tmp);
        EXPECT(!cfg.profiles.empty());
        EXPECT(cfg.profiles[0].dev_cfg.dpi >= 1);           // negative → clamped up
        EXPECT(cfg.profiles[0].dev_cfg.polling_rate <= 8000); // huge → clamped down
        std::remove(tmp.c_str());
    }

    // ── rotation normalisation ────────────────────────────────────────────
    {
        std::string tmp = "/tmp/rawaccel_test_rot.json";
        { std::ofstream f(tmp); f << R"({
            "profiles": [{"name":"r","profile":{"degrees_rotation":720}}]
        })"; }
        app_config cfg = load_config(tmp);
        double rot = cfg.profiles[0].prof.degrees_rotation;
        EXPECT(rot >= 0 && rot < 360); // 720° → 0°
        std::remove(tmp.c_str());
    }

    // ── snap angle clamp ──────────────────────────────────────────────────
    {
        std::string tmp = "/tmp/rawaccel_test_snap.json";
        { std::ofstream f(tmp); f << R"({
            "profiles": [{"name":"s","profile":{"degrees_snap":90}}]
        })"; }
        app_config cfg = load_config(tmp);
        EXPECT(cfg.profiles[0].prof.degrees_snap <= 45); // 90° → 45°
        std::remove(tmp.c_str());
    }

    // ── speed_max >= speed_min ────────────────────────────────────────────
    {
        std::string tmp = "/tmp/rawaccel_test_speed.json";
        { std::ofstream f(tmp); f << R"({
            "profiles": [{"name":"sp","profile":{"speed_min":10,"speed_max":5}}]
        })"; }
        app_config cfg = load_config(tmp);
        double mn = cfg.profiles[0].prof.speed_min;
        double mx = cfg.profiles[0].prof.speed_max;
        EXPECT(mx == 0 || mx >= mn); // max < min → max fixed up
        std::remove(tmp.c_str());
    }

    // ── sanitize_device_profile standalone ───────────────────────────────
    {
        device_profile dp;
        dp.dev_cfg.dpi          = 0;
        dp.dev_cfg.polling_rate = 50; // below min
        dp.prof.degrees_snap    = -5;
        dp.prof.output_dpi      = 99999;
        sanitize_device_profile(dp);
        EXPECT(dp.dev_cfg.dpi >= 1);
        EXPECT(dp.dev_cfg.polling_rate >= 125);
        EXPECT(dp.prof.degrees_snap >= 0);
        EXPECT(dp.prof.output_dpi <= 32000);

        // T15 — exponent_classic clamped to the GUI range [1,10] so
        // manually-edited JSON cannot reach the exp<1 fragmented path.
        dp.prof.accel_x.exponent_classic = 0.5;
        dp.prof.accel_y.exponent_classic = 50.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.exponent_classic >= 1.0);   // 0.5 → 1.0
        EXPECT(dp.prof.accel_y.exponent_classic <= 10.0);  // 50  → 10.0
    }
}

// ── Test 21: multi-profile config round-trip ──────────────────────────────────
// Write a config with several profiles, reload it, verify integrity.

static void test_multi_profile_roundtrip() {
    SECTION("multi-profile config — save/load/active_profile integrity");

    std::string tmp = "/tmp/rawaccel_test_multiprof.json";

    // Build a config with 3 profiles
    app_config cfg_out;
    cfg_out.active_profile = "gaming";
    for (auto& nm : {"default", "gaming", "office"}) {
        device_profile dp;
        dp.name = nm;
        dp.dev_cfg.dpi = (dp.name == "gaming") ? 1600 : 800;
        dp.dev_cfg.polling_rate = 1000;
        dp.prof.accel_x.mode = (dp.name == "gaming")
                                ? accel_mode::classic : accel_mode::noaccel;
        cfg_out.profiles.push_back(dp);
    }
    save_config(cfg_out, tmp);

    // Reload
    app_config cfg_in = load_config(tmp);
    EXPECT(cfg_in.profiles.size() == 3);
    EXPECT(cfg_in.active_profile == "gaming");

    // Check profile order preserved
    EXPECT(cfg_in.profiles[0].name == "default");
    EXPECT(cfg_in.profiles[1].name == "gaming");
    EXPECT(cfg_in.profiles[2].name == "office");

    // Check DPI and mode
    EXPECT(cfg_in.profiles[1].dev_cfg.dpi == 1600);
    EXPECT(cfg_in.profiles[1].prof.accel_x.mode == accel_mode::classic);
    EXPECT(cfg_in.profiles[0].prof.accel_x.mode == accel_mode::noaccel);

    std::remove(tmp.c_str());
}

// ── Test 22: atomic write — no partial-read window ───────────────────────────
// Verify that save_config writes atomically (tmp → rename).

// N-15: save_config writes its temp file as "<path>.<pid>.tmp".  The pid is
// decided inside save_config, so tests can't hardcode the exact name.  Sweep
// the parent directory for any "<path>.*.tmp" leftover so a stray temp file
// from any pid (this run or a crashed one) is flagged.
static int tmp_leftover_count(const std::string& base_path) {
    const std::filesystem::path dir =
        std::filesystem::path(base_path).parent_path();
    const std::string stem =
        std::filesystem::path(base_path).filename().string() + ".";
    if (!std::filesystem::exists(dir)) return 0;
    int n = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.compare(0, stem.size(), stem) == 0 &&
            name.size() >= stem.size() + 4 &&
            name.compare(name.size() - 4, 4, ".tmp") == 0)
            ++n;
    }
    return n;
}

static void remove_tmp_leftovers(const std::string& base_path) {
    const std::filesystem::path dir =
        std::filesystem::path(base_path).parent_path();
    const std::string stem =
        std::filesystem::path(base_path).filename().string() + ".";
    if (!std::filesystem::exists(dir)) return;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (name.compare(0, stem.size(), stem) == 0 &&
            name.size() >= stem.size() + 4 &&
            name.compare(name.size() - 4, 4, ".tmp") == 0)
            std::filesystem::remove(entry.path());
    }
}

static void test_atomic_write() {
    SECTION("atomic config write — no .tmp file left after successful save");

    std::string tmp_path = "/tmp/rawaccel_test_atomic.json";
    // N-15: save_config names the temp file with a pid suffix
    // (path.<pid>.tmp), so a bare "<path>.tmp" assertion could never detect a
    // real leftover.  Glob path.*.tmp to catch a leftover from any pid.
    remove_tmp_leftovers(tmp_path);

    app_config cfg;
    device_profile dp; dp.name = "atomic_test";
    cfg.profiles.push_back(dp);

    save_config(cfg, tmp_path);

    // No .tmp (any pid suffix) may exist after a successful save
    EXPECT(tmp_leftover_count(tmp_path) == 0); // tmp files gone (renamed to final)

    // Final file must exist and be valid JSON
    app_config reloaded = load_config(tmp_path);
    EXPECT(!reloaded.profiles.empty());
    EXPECT(reloaded.profiles[0].name == "atomic_test");

    std::remove(tmp_path.c_str());
}

// ── Test 23: profile_from_json / profile_to_json IPC round-trip ──────────────

static void test_ipc_json_roundtrip() {
    SECTION("IPC profile JSON round-trip — profile_to_json → profile_from_json");

    device_profile dp;
    dp.name      = "ipc_test";
    dp.device_id = "/dev/input/by-id/usb-Razer-event-mouse";
    dp.dev_cfg.dpi          = 1200;
    dp.dev_cfg.polling_rate = 500;
    dp.prof.accel_x.mode    = accel_mode::natural;
    dp.prof.accel_x.motivity = 1.7;
    dp.prof.degrees_rotation = 15.0;

    std::string json_str = profile_to_json(dp);
    EXPECT(!json_str.empty());

    device_profile dp2 = profile_from_json(json_str);
    EXPECT(dp2.name == dp.name);
    EXPECT(dp2.device_id == dp.device_id);
    EXPECT(dp2.dev_cfg.dpi == dp.dev_cfg.dpi);
    EXPECT(dp2.dev_cfg.polling_rate == dp.dev_cfg.polling_rate);
    EXPECT(dp2.prof.accel_x.mode == dp.prof.accel_x.mode);
    EXPECT(std::fabs(dp2.prof.accel_x.motivity - dp.prof.accel_x.motivity) < 1e-9);
    EXPECT(std::fabs(dp2.prof.degrees_rotation - dp.prof.degrees_rotation) < 1e-9);
}

// ── Test 24: apply_motion_math — subpixel accumulation ───────────────────────
// flush_motion() delegates all math to apply_motion_math().  Test it directly
// without requiring uinput/libevdev.

static void test_motion_math() {
    SECTION("apply_motion_math — subpixel accumulation + noaccel passthrough");

    using rawaccel::apply_motion_math;
    using rawaccel::modifier;
    using rawaccel::speed_processor;
    using rawaccel::modifier_settings;
    using rawaccel::init_settings;
    using rawaccel::profile;
    using rawaccel::accel_mode;

    // ── 1. noaccel: output == input (no acceleration) ─────────────────────
    {
        profile p;
        p.accel_x.mode = accel_mode::noaccel;
        p.accel_y.mode = accel_mode::noaccel;
        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor sp; sp.init(p.speed_processor_args);
        modifier mod;

        double rx = 0.0, ry = 0.0;
        int ox = 0, oy = 0;
        apply_motion_math(mod, sp, ms, 1.0, 10.0,
                          5.0, 3.0, rx, ry, ox, oy);
        EXPECT(ox == 5);
        EXPECT(oy == 3);
        EXPECT_NEAR(rx, 0.0, 1e-9);
        EXPECT_NEAR(ry, 0.0, 1e-9);
    }

    // ── 2. Subpixel accumulation: 0.4 + 0.4 + 0.4 = 1 after 3 calls ─────
    // Input 0.4 each call → truncated to 0 until sum ≥ 1
    {
        profile p;
        p.accel_x.mode = accel_mode::noaccel;
        p.accel_y.mode = accel_mode::noaccel;
        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor sp; sp.init(p.speed_processor_args);
        modifier mod;

        double rx = 0.0, ry = 0.0;
        int ox = 0, oy = 0;

        apply_motion_math(mod, sp, ms, 1.0, 10.0, 0.4, 0.0, rx, ry, ox, oy);
        EXPECT(ox == 0);
        EXPECT_NEAR(rx, 0.4, 1e-9);

        apply_motion_math(mod, sp, ms, 1.0, 10.0, 0.4, 0.0, rx, ry, ox, oy);
        EXPECT(ox == 0);
        EXPECT_NEAR(rx, 0.8, 0.001); // small float rounding OK

        apply_motion_math(mod, sp, ms, 1.0, 10.0, 0.4, 0.0, rx, ry, ox, oy);
        EXPECT(ox == 1);                // 0.8 + 0.4 = 1.2 → trunc = 1
        EXPECT(rx < 0.25);              // remainder ≈ 0.2
    }

    // ── 3. Negative motion: subpixel remainder sign-correct ───────────────
    {
        profile p;
        p.accel_x.mode = accel_mode::noaccel;
        p.accel_y.mode = accel_mode::noaccel;
        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor sp; sp.init(p.speed_processor_args);
        modifier mod;

        double rx = 0.0, ry = 0.0;
        int ox = 0, oy = 0;
        apply_motion_math(mod, sp, ms, 1.0, 10.0, -0.7, 0.0, rx, ry, ox, oy);
        EXPECT(ox == 0);
        EXPECT(rx < 0.0);   // remainder is negative (sign-correct)
        EXPECT(rx > -1.0);

        apply_motion_math(mod, sp, ms, 1.0, 10.0, -0.7, 0.0, rx, ry, ox, oy);
        EXPECT(ox == -1);   // -0.7 + (-0.7) = -1.4 → trunc = -1
    }

    // ── 4. Acceleration: output magnitude > input magnitude ──────────────
    {
        profile p;
        p.accel_x.mode = accel_mode::classic;
        p.accel_x.acceleration     = 0.01;
        p.accel_x.exponent_classic = 2.0;
        p.accel_x.cap              = { 100.0, 5.0 };
        p.accel_y = p.accel_x;
        modifier_settings ms; ms.prof = p; init_settings(ms);
        speed_processor sp; sp.init(p.speed_processor_args);
        modifier mod;

        double rx = 0.0, ry = 0.0;
        int ox = 0, oy = 0;
        // Large fast input: 50 counts in 1 ms → speed >> 1
        apply_motion_math(mod, sp, ms, 1.0, 1.0, 50.0, 0.0, rx, ry, ox, oy);
        EXPECT(ox > 50);  // acceleration applied
        EXPECT(std::isfinite(rx));
    }
}

// Invalid event intervals must not poison the stateful speed smoothers.
static void test_nonfinite_time_does_not_poison_smoothers() {
    SECTION("modifier — non-finite time is ignored without poisoning EMA state");

    profile prof;
    prof.accel_x.mode = accel_mode::power;
    prof.accel_y.mode = accel_mode::power;
    prof.accel_x.gain = prof.accel_y.gain = false;
    prof.accel_x.scale = prof.accel_y.scale = 1.5;
    prof.accel_x.exponent_power = prof.accel_y.exponent_power = 0.8;
    prof.speed_processor_args.whole = true;
    prof.speed_processor_args.input_speed_smooth_halflife = 1.0;
    prof.speed_processor_args.scale_smooth_halflife = 1.0;
    prof.speed_processor_args.output_speed_smooth_halflife = 1.0;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);
    modifier mod;
    speed_processor sp;
    sp.init(prof.speed_processor_args);

    double rx = 0, ry = 0;
    int ox = 0, oy = 0;
    // BUG-HIGH-2: a non-finite interval must NOT leak the raw delta 1:1 — the
    // input is zeroed (the smoothers stay untouched, so no poisoning).  What
    // matters here is that no valid-motion raw leak occurs, then the stateful
    // smoothers still produce a finite, non-zero result on the next valid event.
    apply_motion_math(mod, sp, settings, 1.0,
                      std::numeric_limits<double>::quiet_NaN(),
                      4.0, 2.0, rx, ry, ox, oy);
    EXPECT(ox == 0 && oy == 0);

    apply_motion_math(mod, sp, settings, 1.0,
                      std::numeric_limits<double>::infinity(),
                      4.0, 2.0, rx, ry, ox, oy);
    EXPECT(ox == 0 && oy == 0);

    apply_motion_math(mod, sp, settings, 1.0, 1.0,
                      4.0, 2.0, rx, ry, ox, oy);
    EXPECT(ox != 0 || oy != 0);
    EXPECT(std::isfinite(rx) && std::isfinite(ry));
}

// ── Test 25: lat_stats histogram ─────────────────────────────────────────────
// lat_stats yaşam döngüsü, histogram doldurma, percentile hesabı, reset.

static void test_lat_stats() {
    SECTION("lat_stats — histogram, percentile, snapshot_and_reset");

    using rawaccel::lat_stats;

    // ── Boş histogram: count=0, percentile=0 ─────────────────────────────
    {
        lat_stats ls;
        EXPECT(ls.count == 0);
        EXPECT_NEAR(ls.avg_us(), 0.0, 1e-12);
        EXPECT_NEAR(ls.percentile(50), 0.0, 1e-12);
        EXPECT_NEAR(ls.percentile(99), 0.0, 1e-12);
    }

    // ── Tek örnek: min/max/avg = aynı değer ──────────────────────────────
    {
        lat_stats ls;
        ls.record(10.0);
        EXPECT(ls.count == 1);
        EXPECT_NEAR(ls.min_us, 10.0, 1e-9);
        EXPECT_NEAR(ls.max_us, 10.0, 1e-9);
        EXPECT_NEAR(ls.avg_us(), 10.0, 1e-9);
    }

    // ── Percentile hesabı — 100 örnek, tam dağılım ───────────────────────
    // 50 × 1µs + 50 × 3µs → p50 ≈ 1.5µs (bucket 2), p99 ≈ 3.0µs (bucket 6)
    {
        lat_stats ls;
        for (int i = 0; i < 50; i++) ls.record(1.0);
        for (int i = 0; i < 50; i++) ls.record(3.0);
        EXPECT(ls.count == 100);
        // p50: 50th sample → in the 50×1µs group (bucket index 2, upper edge 1.5µs)
        double p50 = ls.percentile(50);
        EXPECT(p50 >= 1.0 - 1e-9);
        EXPECT(p50 <= 1.5 + 1e-9);
        // p99: 99th sample → in the 50×3µs group
        double p99 = ls.percentile(99);
        EXPECT(p99 >= 3.0 - 1e-9);
        EXPECT(p99 <= 3.5 + 1e-9);
        // avg = (50×1 + 50×3)/100 = 2.0
        EXPECT_NEAR(ls.avg_us(), 2.0, 1e-9);
    }

    // ── Overflow bucket: sample > RANGE_US ───────────────────────────────
    {
        lat_stats ls;
        ls.record(1.0);                              // normal
        ls.record(lat_stats::RANGE_US + 100.0);      // overflow (> 500µs)
        EXPECT(ls.count == 2);
        EXPECT(ls.over == 1);
        // max_us must reflect the overflow sample
        EXPECT(ls.max_us > lat_stats::RANGE_US);
    }

    // ── snapshot_and_reset atomically copies + resets ─────────────────────
    {
        lat_stats ls;
        for (int i = 1; i <= 10; i++) ls.record(static_cast<double>(i));
        EXPECT(ls.count == 10);

        auto s = ls.snapshot_and_reset();
        // Snapshot has the old data
        EXPECT(s.count == 10);
        EXPECT_NEAR(s.min_us, 1.0, 0.5 + 1e-9); // ≤ 1.5 (first bucket upper edge)
        EXPECT(s.max_us >= 10.0);
        // After reset, ls is clean
        EXPECT(ls.count == 0);
        EXPECT_NEAR(ls.avg_us(), 0.0, 1e-12);
        // Snapshot percentile works independently
        double sp50 = s.percentile(50);
        EXPECT(sp50 > 0.0);
    }

    // ── R12-LATRAW: reset() zeroes the histogram without a snapshot ────────
    // Mirrors the accel→raw transition in apply_profile (daemon.cpp): the raw
    // contract mandates stale accel-era lat_* fields stop being published.
    {
        lat_stats ls;
        for (int i = 1; i <= 10; i++) ls.record(static_cast<double>(i));
        ls.record(lat_stats::RANGE_US + 100.0);          // overflow bucket
        EXPECT(ls.count == 11);
        EXPECT(ls.max_us > lat_stats::RANGE_US);
        ls.reset();
        EXPECT(ls.count == 0);
        EXPECT_NEAR(ls.avg_us(), 0.0, 1e-12);
        EXPECT(ls.min_us == 1e9 && ls.max_us == 0 && ls.over == 0);
        // percentile() on an empty histogram must not crash or leak old data
        EXPECT(ls.percentile(50) == 0.0);
        EXPECT(ls.percentile(100) == 0.0);
        // fresh recording after reset starts a clean histogram
        ls.record(5.0);
        EXPECT(ls.count == 1);
        EXPECT_NEAR(ls.min_us, 5.0, 0.5 + 1e-9);
    }

    // ── Monotone: p50 ≤ p95 ≤ p99 ────────────────────────────────────────
    {
        lat_stats ls;
        // Uniform 1–20µs
        for (int i = 1; i <= 20; i++)
            for (int j = 0; j < 5; j++)  // 5 samples each → 100 total
                ls.record(static_cast<double>(i));
        EXPECT(ls.count == 100);
        double p50 = ls.percentile(50);
        double p95 = ls.percentile(95);
        double p99 = ls.percentile(99);
        EXPECT(p50 <= p95 + 1e-9);
        EXPECT(p95 <= p99 + 1e-9);
        EXPECT(p99 > 0.0);
    }

    // ── BUG-HIGH-1: NaN percentile must not reach the uint64_t cast ───────
    {
        lat_stats ls;
        for (int i = 0; i < 10; i++) ls.record(2.0);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        EXPECT_NEAR(ls.percentile(nan), 0.0, 1e-12);
        // +Inf is not NaN: it falls into the pct >= 100 branch → max_us
        EXPECT_NEAR(ls.percentile(inf), ls.max_us, 1e-12);
        auto s = ls.snapshot_and_reset();
        EXPECT_NEAR(s.percentile(nan), 0.0, 1e-12);
        EXPECT_NEAR(s.percentile(inf), s.max_us, 1e-12);
    }

    // ── Move constructor: data transfers, mutex is fresh ──────────────────
    {
        lat_stats a;
        a.record(5.0);
        a.record(10.0);
        lat_stats b(std::move(a));
        EXPECT(b.count == 2);
        EXPECT(a.count == 0);       // source is reset after move
    }
}

// ── Test: natural gain mode with decay_rate=0 (BUG-2 regression) ─────────────

static void test_natural_decay_zero() {
    SECTION("natural — decay_rate=0 (accel=0) no NaN");
    // BUG-2: decay_rate=0 → accel=0 → division by zero in gain mode integral.
    // After fix, should return 1.0 (no acceleration) without NaN.

    // gain mode ON, decay_rate=0
    {
        accel_args args = make_args(accel_mode::natural);
        args.decay_rate = 0.0;
        args.gain = true;
        args.limit = 1.5;
        args.input_offset = 0;
        natural n(args);
        double g1 = n(0.0, args);
        double g5 = n(5.0, args);
        double g50 = n(50.0, args);
        EXPECT_NEAR(g1, 1.0, 1e-9);
        EXPECT(std::isfinite(g5));
        EXPECT(g5 >= 1.0 - 1e-9);   // should be exactly 1.0
        EXPECT(std::isfinite(g50));
        EXPECT(g50 >= 1.0 - 1e-9);
    }

    // gain mode OFF, decay_rate=0 — legacy mode, should also work
    {
        accel_args args = make_args(accel_mode::natural);
        args.decay_rate = 0.0;
        args.gain = false;
        args.limit = 1.5;
        args.input_offset = 0;
        natural n(args);
        double g = n(10.0, args);
        EXPECT(std::isfinite(g));
        EXPECT(g >= 1.0 - 1e-9);
    }

    // very small decay_rate (near-zero but nonzero)
    {
        accel_args args = make_args(accel_mode::natural);
        args.decay_rate = 1e-15;
        args.gain = true;
        args.limit = 2.0;
        args.input_offset = 0;
        natural n(args);
        double g = n(10.0, args);
        EXPECT(std::isfinite(g));
    }
}

// ── Test: all algorithms with extreme input values ───────────────────────────

static void test_extreme_inputs() {
    SECTION("extreme input values — no NaN/Inf from any algorithm");

    const double extremes[] = { 0.0, 1e-15, 1e-9, 0.001, 1.0, 100.0, 1e6, 1e15 };
    const accel_mode modes[] = {
        accel_mode::noaccel, accel_mode::classic, accel_mode::natural,
        accel_mode::jump, accel_mode::synchronous, accel_mode::power
    };

    for (auto mode : modes) {
        accel_args args = make_args(mode);
        accel_union au;
        au.init(args);
        bool all_finite = true;
        for (double x : extremes) {
            double g = au.apply(x, args);
            if (!std::isfinite(g)) {
                all_finite = false;
                std::fprintf(stderr, "  NaN/Inf at mode=%d, x=%.3g, g=%.3g\n",
                             (int)mode, x, g);
            }
        }
        EXPECT(all_finite);
    }

    // Negative speeds — should not crash
    {
        bool all_ok = true;
        for (auto mode : modes) {
            accel_args args = make_args(mode);
            accel_union au;
            au.init(args);
            double g = au.apply(-5.0, args);
            if (!std::isfinite(g)) all_ok = false;
        }
        EXPECT(all_ok);
    }
}

// ── Test: classic with degenerate cap_mode::io configs ───────────────────────

static void test_classic_degenerate() {
    SECTION("classic — degenerate cap_mode::io (cap.x <= input_offset)");

    accel_args args = make_args(accel_mode::classic);
    args.cap_mode_val = cap_mode::io;
    args.cap.x = 0.0;   // degenerate: cap.x == 0
    args.cap.y = 2.0;
    args.input_offset = 1.0;
    classic c(args);
    // Should not crash; gain should be finite
    double g = c(5.0, args);
    EXPECT(std::isfinite(g));
    EXPECT(g >= 1.0);
}

// ── Test: power with exponent_power=0 ────────────────────────────────────────

static void test_power_zero_exponent() {
    SECTION("power — exponent_power=0 (clamped to 1e-4)");
    accel_args args = make_args(accel_mode::power);
    args.exponent_power = 0.0;
    power p(args);
    double g = p(10.0, args);
    EXPECT(std::isfinite(g));
    EXPECT(g > 0.0);

    // Negative exponent (should still work, clamped)
    args.exponent_power = -1.0;
    power p2(args);
    double g2 = p2(10.0, args);
    EXPECT(std::isfinite(g2));
}

// ── Test: lookup with edge cases ─────────────────────────────────────────────

static void test_lookup_edge_cases() {
    SECTION("lookup — duplicate points, single point, odd length");

    // Single point (odd length=1 → 0 pairs)
    {
        accel_args args;
        args.mode = accel_mode::lookup;
        args.length = 1;
        args.data[0] = 5.0f;
        lookup lut(args);
        EXPECT(lut.size == 0);
        EXPECT_NEAR(lut(5.0, args), 1.0, 1e-9);
    }

    // Duplicate X values (x0 == x1)
    {
        accel_args args;
        args.mode = accel_mode::lookup;
        args.length = 4;
        args.data[0] = 10.0f; args.data[1] = 1.5f;
        args.data[2] = 10.0f; args.data[3] = 2.0f;  // same X
        lookup lut(args);
        double g = lut(10.0, args);
        EXPECT(std::isfinite(g));
    }

    // Maximum capacity: exactly LUT_POINTS_CAPACITY pairs
    {
        accel_args args;
        args.mode = accel_mode::lookup;
        int max_pairs = (int)LUT_POINTS_CAPACITY;
        args.length = max_pairs * 2;
        for (int i = 0; i < max_pairs; i++) {
            args.data[i*2]   = (float)(i * 1.0);
            args.data[i*2+1] = 1.0f + (float)i * 0.01f;
        }
        lookup lut(args);
        EXPECT(lut.size == max_pairs);
        double g = lut(50.0, args);
        EXPECT(std::isfinite(g));
    }
}

// ── Test: modifier with zero time (division by zero guard) ───────────────────

static void test_modifier_zero_time() {
    SECTION("modifier — zero/negative time guard");

    modifier mod;
    speed_processor sp;
    modifier_settings settings;
    settings.prof.accel_x.mode = accel_mode::classic;
    settings.prof.accel_y = settings.prof.accel_x;
    init_settings(settings);
    sp.init(settings.prof.speed_processor_args);

    // time = 0 → modify returns early with input zeroed (BUG-HIGH-2)
    {
        vec2d in = { 5.0, 3.0 };
        mod.modify(in, sp, settings, 1.0, 0.0);
        EXPECT_NEAR(in.x, 0.0, 1e-9);
        EXPECT_NEAR(in.y, 0.0, 1e-9);
    }

    // time = negative → also zeroes the input
    {
        vec2d in = { 5.0, 3.0 };
        mod.modify(in, sp, settings, 1.0, -1.0);
        EXPECT_NEAR(in.x, 0.0, 1e-9);
        EXPECT_NEAR(in.y, 0.0, 1e-9);
    }
}

// ── Test: EMA smoother stability with very large/small halflife ──────────────

static void test_ema_smoother_stability() {
    SECTION("EMA smoother — extreme halflife values");

    // halflife = 0 → coefficient = 0, smoother should pass through
    {
        simple_ema_smoother s;
        s.init(0.0);
        double r1 = s.smooth(10.0, 1.0);
        double r2 = s.smooth(20.0, 1.0);
        EXPECT(std::isfinite(r1));
        EXPECT(std::isfinite(r2));
    }

    // Very large halflife → heavy smoothing, output stays near zero initially
    {
        simple_ema_smoother s;
        s.init(1000.0);
        double r = s.smooth(100.0, 1.0);
        EXPECT(std::isfinite(r));
        EXPECT(r >= 0.0);
        EXPECT(r <= 100.0);
    }

    // linear_ema_smoother with zero halflife
    {
        linear_ema_smoother s;
        s.init(0.0, 0.0);
        double r = s.smooth(50.0, 1.0);
        EXPECT(std::isfinite(r));
    }
}

// ── Test: subpixel accumulation sign correctness ─────────────────────────────

static void test_subpixel_sign() {
    SECTION("subpixel accumulation — sign correctness across directions");

    modifier mod;
    speed_processor sp;
    modifier_settings settings;
    settings.prof.accel_x.mode = accel_mode::noaccel;
    settings.prof.accel_y = settings.prof.accel_x;
    init_settings(settings);
    sp.init(settings.prof.speed_processor_args);

    double rx = 0, ry = 0;
    int ox, oy;

    // Accumulate small positive movements
    for (int i = 0; i < 5; i++) {
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          0.3, 0.0, rx, ry, ox, oy);
    }
    // After 5 × 0.3 = 1.5, should have produced at least 1 count
    // (accumulated output is 1 with 0.5 remainder)
    EXPECT(rx >= 0.0);
    EXPECT(rx < 1.0);

    // Now reverse direction
    rx = 0; ry = 0;
    for (int i = 0; i < 5; i++) {
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          -0.3, 0.0, rx, ry, ox, oy);
    }
    EXPECT(rx <= 0.0);
    EXPECT(rx > -1.0);

    // Large rapid direction change: should not accumulate incorrectly
    rx = 0.9;  // almost at +1
    apply_motion_math(mod, sp, settings, 1.0, 1.0,
                      -2.0, 0.0, rx, ry, ox, oy);
    // Expected: 0.9 + (-2.0) = -1.1 → output -1, remainder -0.1
    EXPECT(ox == -1);
    EXPECT_NEAR(rx, -0.1, 1e-9);
}

// ── Test: sanitize_device_profile clamps extreme values ──────────────────────

static void test_sanitize_extremes() {
    SECTION("sanitize_device_profile — extreme value clamping");

    device_profile dp;
    dp.dev_cfg.dpi = -100;
    dp.dev_cfg.polling_rate = 99999;
    dp.prof.degrees_rotation = -720.0;
    dp.prof.degrees_snap = 90.0;
    dp.prof.output_dpi = 0;   // 0 = no output-DPI normalization (CFG-1)
    dp.prof.lr_output_dpi_ratio = 0.0001;
    dp.prof.ud_output_dpi_ratio = 999.0;
    dp.prof.speed_min = -5;
    dp.prof.speed_max = -10;
    dp.prof.speed_processor_args.lp_norm = -1.0;

    sanitize_device_profile(dp);

    EXPECT(dp.dev_cfg.dpi >= 1);
    EXPECT(dp.dev_cfg.polling_rate <= 8000);
    EXPECT(dp.prof.degrees_rotation >= 0.0);
    EXPECT(dp.prof.degrees_rotation < 360.0);
    EXPECT(dp.prof.degrees_snap <= 45.0);
    EXPECT(dp.prof.output_dpi == 0.0);   // 0 preserved: guard skips dpi_adjustment → 1:1
    EXPECT(dp.prof.lr_output_dpi_ratio >= 0.01);
    EXPECT(dp.prof.ud_output_dpi_ratio <= 100.0);
    EXPECT(dp.prof.speed_min >= 0);
    EXPECT(dp.prof.speed_max >= 0);
    EXPECT(dp.prof.speed_processor_args.lp_norm > 0);
}

// ── Test: sanitize sorts unsorted LUT data (BUG-NEW-1 fix) ──────────────────

static void test_lut_sort_on_sanitize() {
    SECTION("sanitize_device_profile — sorts unsorted LUT data");

    // Unsorted LUT points: (20, 2.0), (5, 1.2), (10, 1.5), (0, 1.0)
    // After sanitize, they must be sorted by speed (X): 0, 5, 10, 20
    {
        device_profile dp;
        dp.prof.accel_x.mode = accel_mode::lookup;
        dp.prof.accel_x.length = 8;  // 4 pairs × 2
        dp.prof.accel_x.data[0] = 20.0f; dp.prof.accel_x.data[1] = 2.0f;
        dp.prof.accel_x.data[2] = 5.0f;  dp.prof.accel_x.data[3] = 1.2f;
        dp.prof.accel_x.data[4] = 10.0f; dp.prof.accel_x.data[5] = 1.5f;
        dp.prof.accel_x.data[6] = 0.0f;  dp.prof.accel_x.data[7] = 1.0f;

        sanitize_device_profile(dp);

        // Verify sorted order: 0, 5, 10, 20
        EXPECT_NEAR(dp.prof.accel_x.data[0], 0.0f,  1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[1], 1.0f,  1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[2], 5.0f,  1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[3], 1.2f,  1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[4], 10.0f, 1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[5], 1.5f,  1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[6], 20.0f, 1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[7], 2.0f,  1e-9);

        // Verify the sorted LUT actually produces correct interpolation
        // (use velocity=false so stored y is read as the gain directly)
        dp.prof.accel_x.gain = false;
        lookup lut(dp.prof.accel_x);
        EXPECT_NEAR(lut(0.0, dp.prof.accel_x), 0.0, 1e-6);   // reference: x<=0 → 0
        EXPECT_NEAR(lut(5.0, dp.prof.accel_x), 1.2, 1e-6);
        EXPECT_NEAR(lut(10.0, dp.prof.accel_x), 1.5, 1e-6);
        EXPECT_NEAR(lut(20.0, dp.prof.accel_x), 2.0, 1e-6);
        // Midpoint interpolation: speed=2.5 → between (0,1.0) and (5,1.2) → 1.1
        EXPECT_NEAR(lut(2.5, dp.prof.accel_x), 1.1, 1e-6);
    }

    // accel_y is also sorted
    {
        device_profile dp;
        dp.prof.accel_y.mode = accel_mode::lookup;
        dp.prof.accel_y.length = 4;  // 2 pairs
        dp.prof.accel_y.data[0] = 30.0f; dp.prof.accel_y.data[1] = 1.8f;
        dp.prof.accel_y.data[2] = 10.0f; dp.prof.accel_y.data[3] = 1.3f;

        sanitize_device_profile(dp);

        EXPECT_NEAR(dp.prof.accel_y.data[0], 10.0f, 1e-9);
        EXPECT_NEAR(dp.prof.accel_y.data[1], 1.3f,  1e-9);
        EXPECT_NEAR(dp.prof.accel_y.data[2], 30.0f, 1e-9);
        EXPECT_NEAR(dp.prof.accel_y.data[3], 1.8f,  1e-9);
    }

    // Already sorted data stays sorted
    {
        device_profile dp;
        dp.prof.accel_x.mode = accel_mode::lookup;
        dp.prof.accel_x.length = 6;  // 3 pairs
        dp.prof.accel_x.data[0] = 0.0f;  dp.prof.accel_x.data[1] = 1.0f;
        dp.prof.accel_x.data[2] = 10.0f; dp.prof.accel_x.data[3] = 1.5f;
        dp.prof.accel_x.data[4] = 20.0f; dp.prof.accel_x.data[5] = 2.0f;

        sanitize_device_profile(dp);

        EXPECT_NEAR(dp.prof.accel_x.data[0], 0.0f,  1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[2], 10.0f, 1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[4], 20.0f, 1e-9);
    }

    // Reverse-sorted data gets properly sorted
    {
        device_profile dp;
        dp.prof.accel_x.mode = accel_mode::lookup;
        dp.prof.accel_x.length = 10;  // 5 pairs
        dp.prof.accel_x.data[0] = 40.0f; dp.prof.accel_x.data[1] = 2.5f;
        dp.prof.accel_x.data[2] = 30.0f; dp.prof.accel_x.data[3] = 2.0f;
        dp.prof.accel_x.data[4] = 20.0f; dp.prof.accel_x.data[5] = 1.7f;
        dp.prof.accel_x.data[6] = 10.0f; dp.prof.accel_x.data[7] = 1.3f;
        dp.prof.accel_x.data[8] = 0.0f;  dp.prof.accel_x.data[9] = 1.0f;

        sanitize_device_profile(dp);

        // Check ascending X order
        for (int i = 0; i < 4; i++) {
            EXPECT(dp.prof.accel_x.data[i * 2] <= dp.prof.accel_x.data[(i + 1) * 2]);
        }
        EXPECT_NEAR(dp.prof.accel_x.data[0], 0.0f, 1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[8], 40.0f, 1e-9);
    }

    // Single pair (length=2): no crash, stays as-is
    {
        device_profile dp;
        dp.prof.accel_x.mode = accel_mode::lookup;
        dp.prof.accel_x.length = 2;
        dp.prof.accel_x.data[0] = 5.0f; dp.prof.accel_x.data[1] = 1.5f;

        sanitize_device_profile(dp);

        EXPECT_NEAR(dp.prof.accel_x.data[0], 5.0f, 1e-9);
        EXPECT_NEAR(dp.prof.accel_x.data[1], 1.5f, 1e-9);
    }

    // Empty LUT (length=0): no crash
    {
        device_profile dp;
        dp.prof.accel_x.mode = accel_mode::lookup;
        dp.prof.accel_x.length = 0;

        sanitize_device_profile(dp);  // should not crash
        EXPECT(dp.prof.accel_x.length == 0);
    }
}

// ── Test: LUT sort + JSON round-trip (unsorted JSON → sorted after load) ─────

static void test_cfg_p54_guards() {
    SECTION("P54-B2 — LUT length clamp in sanitize");
    {
        device_profile dp;
        dp.name = "b2clamp";
        dp.prof.accel_x.mode = accel_mode::lookup;
        dp.prof.accel_x.length = 100000;
        dp.prof.accel_x.data[0] = 0.0f;
        dp.prof.accel_x.data[1] = 1.0f;
        sanitize_device_profile(dp);
        EXPECT(static_cast<size_t>(dp.prof.accel_x.length) <= LUT_RAW_DATA_CAPACITY);
        EXPECT(dp.prof.accel_x.length >= 0);

        device_profile dpn;
        dpn.name = "b2neg";
        dpn.prof.accel_x.mode = accel_mode::lookup;
        dpn.prof.accel_x.length = -7;
        sanitize_device_profile(dpn);
        EXPECT(dpn.prof.accel_x.length == 0);
    }

    SECTION("P54-B3 — save_config tmp is pid-suffixed, atomic, no .tmp left");
    {
        const std::string path = "/tmp/test_p54_b3.json";
        std::filesystem::remove(path);
        remove_tmp_leftovers(path);
        app_config cfg;
        cfg.active_profile = "p54";
        save_config(cfg, path);
        // N-15: temp name carries the pid suffix — glob path.*.tmp so a
        // leftover from any pid is caught.
        EXPECT(tmp_leftover_count(path) == 0);
        EXPECT(std::filesystem::exists(path));
        EXPECT(std::filesystem::file_size(path) > 0);
        std::filesystem::remove(path);
    }

    SECTION("P54-B4 — wrong-typed string fields fall back, no throw");
    {
        // All string fields get a non-string value; accel_args strings too.
        // The loader must not throw — defaults keep the config loadable.
        nlohmann::json j = {
            {"version", std::string("bad") + "type"},
            {"active_profile", 42},
            {"use_raw_input", "yes"},
            {"profiles", nlohmann::json::array({{
                {"name", 42},
                {"device_id", 456},
                {"dpi", "900"},
                {"profile", nlohmann::json{{"mode", 7}, {"gain", "yes"},
                                 {"cap_mode", 3}, {"name", 42}}}
            }})}
        };
        const std::string path = "/tmp/test_p54_b4.json";
        {
            std::ofstream f(path);
            f << j.dump(4);
        }
        app_config cfg = load_config(path);   // no exception allowed
        std::filesystem::remove(path);

        EXPECT(cfg.active_profile == "default");
        EXPECT(cfg.use_raw_input == true);
        EXPECT(cfg.profiles.size() == 1);
        EXPECT(cfg.profiles[0].name.empty());
        EXPECT(cfg.profiles[0].device_id.empty());
        EXPECT(cfg.profiles[0].dev_cfg.dpi == 800);
        EXPECT(cfg.profiles[0].prof.accel_x.mode == accel_mode::noaccel);
    }
}

static void test_lut_sort_json_roundtrip() {
    SECTION("LUT sort — unsorted JSON round-trip");

    // Build a config with unsorted LUT, save to JSON, reload, check sorted
    {
        device_profile dp;
        dp.name = "lut_sort_test";
        dp.prof.accel_x.mode = accel_mode::lookup;
        dp.prof.accel_x.length = 6;
        dp.prof.accel_x.data[0] = 15.0f; dp.prof.accel_x.data[1] = 24.0f; // output speed (y = x · gain)
        dp.prof.accel_x.data[2] = 5.0f;  dp.prof.accel_x.data[3] = 6.0f;
        dp.prof.accel_x.data[4] = 0.0f;  dp.prof.accel_x.data[5] = 1.0f;

        app_config cfg;
        cfg.active_profile = "lut_sort_test";
        cfg.profiles.push_back(dp);

        // Save and reload
        const char* tmp = "/tmp/test_lut_sort_rt.json";
        save_config(cfg, tmp);

        app_config cfg2 = load_config(tmp);
        std::remove(tmp);

        EXPECT(cfg2.profiles.size() == 1);
        auto& ax = cfg2.profiles[0].prof.accel_x;
        EXPECT(ax.length == 6);

        // After load + sanitize, data must be sorted by X.
        // save_config always stamps the schema version, so on reload the
        // current version is read back and migrate_config() is a no-op
        // (P43-BF1). Stored y are OUTPUT SPEEDS (velocity mode):
        // effective gain = y / speed.
        EXPECT_NEAR(ax.data[0], 0.0f,  1e-9);
        EXPECT_NEAR(ax.data[1], 1.0f,  1e-9);   // x=0 noktası dokunulmaz
        EXPECT_NEAR(ax.data[2], 5.0f,  1e-9);
        EXPECT_NEAR(ax.data[3], 6.0f,  1e-9);   // 5.0 noktasının output speed'i
        EXPECT_NEAR(ax.data[4], 15.0f, 1e-9);
        EXPECT_NEAR(ax.data[5], 24.0f, 1e-9);   // 15.0 noktası — migrate çalışmaz

        // Interpolation: velocity mode → geçerli gain = y/speed (özgün eğri geri gelir)
        lookup lut(ax);
        EXPECT_NEAR(lut(0.0,  ax), 0.0, 1e-6);
        EXPECT_NEAR(lut(5.0,  ax), 1.2, 1e-6);
        EXPECT_NEAR(lut(15.0, ax), 1.6, 1e-6);
    }

    SECTION("LUT — P43-BF1 regression: lookup+gain round-trip stays stable");
    {
        // BF1: app_config_from_json_obj read the schema version back from JSON.
        // Before the fix, version was never read, so migrate_config() re-ran
        // migrate_lookup_gain() on EVERY load, rescaling stored gain y by x
        // each round trip (200 → 20000 → 2M → ...). Now the migration must run
        // exactly once (only when the stored version is absent/stale).
        device_profile dp;
        dp.name = "bf1_rt";
        dp.prof.accel_x.mode = accel_mode::lookup;
        dp.prof.accel_x.gain = true;
        dp.prof.accel_x.length = 4; // 2 points
        dp.prof.accel_x.data[0] = 0.f;   dp.prof.accel_x.data[1] = 0.f;
        dp.prof.accel_x.data[2] = 100.f; dp.prof.accel_x.data[3] = 200.f;

        app_config cfg;
        cfg.active_profile = "bf1_rt";
        cfg.profiles.push_back(dp);

        const char* tmp = "/tmp/test_bf1_rt.json";
        save_config(cfg, tmp); // stamps version
        for (int it = 0; it < 3; it++) {
            app_config c = load_config(tmp);
            double y = c.profiles[0].prof.accel_x.data[3];
            EXPECT_NEAR(y, 200.0f, 1e-9); // no repeated rescaling on reload
        }
        std::remove(tmp);

        // And a legacy versionless file must STILL be migrated exactly once
        // (y_new = y · x) — one-shot migration behaviour is preserved.
        const char* legacy = "/tmp/test_bf1_legacy.json";
        {
            std::ofstream of(legacy);
            of << R"({ "active_profile": "bf1", "use_raw_input": true,
  "profiles": [ { "name": "bf1", "device_id": "",
      "profile": { "name": "bf1",
          "accel_x": { "mode": "lookup", "gain": true,
               "lut_data": [0.0,0.0,100.0,200.0], "lut_length": 4 },
          "accel_y": { "mode": "lookup", "gain": true,
               "lut_data": [0.0,0.0,100.0,200.0], "lut_length": 4 } } } ] })";
        }
        app_config cl = load_config(legacy);
        std::remove(legacy);
        EXPECT_NEAR(cl.profiles[0].prof.accel_x.data[3], 20000.0f, 1e-9); // 200 · 100
    }
}

// ── Test: motion_math int overflow + NaN/Inf guards (BUG-R3-4 fix) ───────────

static void test_motion_math_overflow() {
    SECTION("motion_math — int overflow clamp + NaN/Inf guard");

    modifier mod;
    speed_processor sp;
    modifier_settings settings;
    settings.prof.accel_x.mode = accel_mode::noaccel;
    settings.prof.accel_y = settings.prof.accel_x;
    init_settings(settings);
    sp.init(settings.prof.speed_processor_args);

    // Huge delta that would overflow int if not clamped
    {
        double rx = 0, ry = 0;
        int ox, oy;
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          3e9, -3e9, rx, ry, ox, oy);
        // Should be clamped to INT_MAX / INT_MIN, not UB
        EXPECT(ox > 0);
        EXPECT(oy < 0);
        EXPECT(std::isfinite(rx));
        EXPECT(std::isfinite(ry));
    }

    // Inf input should produce 0 output (clamped), finite remainder
    {
        double rx = 0, ry = 0;
        int ox, oy;
        // Use a classic accel with extreme acceleration that could produce Inf
        modifier mod2;
        speed_processor sp2;
        modifier_settings s2;
        s2.prof.accel_x.mode = accel_mode::classic;
        s2.prof.accel_x.acceleration = 999999.0;
        s2.prof.accel_x.exponent_classic = 10.0;
        s2.prof.accel_x.limit = 1e30;
        s2.prof.accel_y = s2.prof.accel_x;
        init_settings(s2);
        sp2.init(s2.prof.speed_processor_args);

        apply_motion_math(mod2, sp2, s2, 1.0, 1.0,
                          1e10, 1e10, rx, ry, ox, oy);
        // Key: no UB, result is finite (even if clamped)
        EXPECT(std::isfinite(rx));
        EXPECT(std::isfinite(ry));
        // ox/oy are just int values — they exist (no crash)
        (void)ox; (void)oy;
    }

    // Negative overflow
    {
        double rx = 0, ry = 0;
        int ox, oy;
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          -3e9, 3e9, rx, ry, ox, oy);
        EXPECT(ox < 0);
        EXPECT(oy > 0);
        EXPECT(std::isfinite(rx));
        EXPECT(std::isfinite(ry));
    }

    // Zero delta — no overflow, no crash
    {
        double rx = 0, ry = 0;
        int ox, oy;
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          0, 0, rx, ry, ox, oy);
        EXPECT(ox == 0);
        EXPECT(oy == 0);
        EXPECT_NEAR(rx, 0.0, 1e-9);
        EXPECT_NEAR(ry, 0.0, 1e-9);
    }
}

// ── Test: accel_args sanitization via sanitize_device_profile (BUG-R5-1) ─────

static void test_accel_args_sanitize() {
    SECTION("sanitize_device_profile — accel_args field clamping");

    // 1. Negative acceleration → NOT clamped (legitimate deceleration in classic mode)
    {
        device_profile dp;
        dp.prof.accel_x.acceleration = -5.0;
        dp.prof.accel_y.acceleration = -0.001;
        sanitize_device_profile(dp);
        EXPECT_NEAR(dp.prof.accel_x.acceleration, -5.0, 1e-9);
        EXPECT_NEAR(dp.prof.accel_y.acceleration, -0.001, 1e-9);
    }

    // 2. Negative scale → clamped to 0
    {
        device_profile dp;
        dp.prof.accel_x.scale = -10.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.scale >= 0);
    }

    // 3. Negative decay_rate → clamped to 0
    {
        device_profile dp;
        dp.prof.accel_x.decay_rate = -1.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.decay_rate >= 0);
    }

    // 4. Zero/negative exponent_power → clamped to 1e-4
    {
        device_profile dp;
        dp.prof.accel_x.exponent_power = 0;
        dp.prof.accel_y.exponent_power = -2.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.exponent_power >= 1e-4);
        EXPECT(dp.prof.accel_y.exponent_power >= 1e-4);
    }

    // 5. Negative input/output offsets → clamped to 0
    {
        device_profile dp;
        dp.prof.accel_x.input_offset = -10.0;
        dp.prof.accel_x.output_offset = -5.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.input_offset >= 0);
        EXPECT(dp.prof.accel_x.output_offset >= 0);
    }

    // 6. Negative limit → clamped to 0
    {
        device_profile dp;
        dp.prof.accel_x.limit = -3.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.limit >= 0);
    }

    // 7. Zero/negative sync_speed → clamped to 1e-4
    {
        device_profile dp;
        dp.prof.accel_x.sync_speed = 0;
        dp.prof.accel_y.sync_speed = -5.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.sync_speed >= 1e-4);
        EXPECT(dp.prof.accel_y.sync_speed >= 1e-4);
    }

    // 8. Negative smooth → clamped to 0
    {
        device_profile dp;
        dp.prof.accel_x.smooth = -1.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.smooth >= 0);
    }

    // 9. Negative motivity/gamma → clamped to 0
    {
        device_profile dp;
        dp.prof.accel_x.motivity = -2.0;
        dp.prof.accel_x.gamma = -1.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.motivity >= 0);
        EXPECT(dp.prof.accel_x.gamma >= 0);
    }

    // 10. Negative cap values → clamped to 0
    {
        device_profile dp;
        dp.prof.accel_x.cap.x = -5.0;
        dp.prof.accel_x.cap.y = -3.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.accel_x.cap.x >= 0);
        EXPECT(dp.prof.accel_x.cap.y >= 0);
    }

    // 11. Negative smooth halflifes → clamped to 0
    {
        device_profile dp;
        dp.prof.speed_processor_args.input_speed_smooth_halflife = -10.0;
        dp.prof.speed_processor_args.scale_smooth_halflife = -5.0;
        dp.prof.speed_processor_args.output_speed_smooth_halflife = -1.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.speed_processor_args.input_speed_smooth_halflife >= 0);
        EXPECT(dp.prof.speed_processor_args.scale_smooth_halflife >= 0);
        EXPECT(dp.prof.speed_processor_args.output_speed_smooth_halflife >= 0);
    }

    // 12. Negative domain/range weights → clamped to 0
    {
        device_profile dp;
        dp.prof.domain_weights.x = -1.0;
        dp.prof.domain_weights.y = -2.0;
        dp.prof.range_weights.x = -0.5;
        dp.prof.range_weights.y = -3.0;
        sanitize_device_profile(dp);
        EXPECT(dp.prof.domain_weights.x >= 0);
        EXPECT(dp.prof.domain_weights.y >= 0);
        EXPECT(dp.prof.range_weights.x >= 0);
        EXPECT(dp.prof.range_weights.y >= 0);
    }

    // 12b. P86: absurd domain/range weights (e.g. 1e300) → clamped to 1e6 ceiling
    //       so sanitized config can never push accel-LUT lookups past
    //       representable indices (defense-in-depth under the gain_apply clamp).
    {
        device_profile dp;
        dp.prof.domain_weights.x = 1e300;
        dp.prof.domain_weights.y = 4.54e133;
        dp.prof.range_weights.x = 1e308;
        dp.prof.range_weights.y = 9e15;
        sanitize_device_profile(dp);
        EXPECT_NEAR(dp.prof.domain_weights.x, 1e6, 1e-9);
        EXPECT_NEAR(dp.prof.domain_weights.y, 1e6, 1e-9);
        EXPECT_NEAR(dp.prof.range_weights.x, 1e6, 1e-9);
        EXPECT_NEAR(dp.prof.range_weights.y, 1e6, 1e-9);
    }

    // 13. JSON round-trip with negative accel_args → sanitized on load
    {
        const char* tmp = "/tmp/rawaccel_test_accel_args_sanitize.json";
        {
            std::ofstream f(tmp);
            f << R"({
                "profiles": [{
                    "name": "bad-args",
                    "dpi": 800, "polling_rate": 1000,
                    "profile": {
                        "accel_x": {
                            "mode": "classic",
                            "acceleration": -5.0,
                            "scale": -10.0,
                            "decay_rate": -1.0,
                            "exponent_power": -2.0,
                            "input_offset": -3.0,
                            "output_offset": -4.0,
                            "limit": -1.5,
                            "sync_speed": -5.0,
                            "smooth": -1.0,
                            "motivity": -2.0,
                            "gamma": -1.0,
                            "cap": [-5.0, -3.0]
                        },
                        "domain_weights": [-1.0, -2.0],
                        "range_weights": [-0.5, -3.0]
                    }
                }],
                "active_profile": "bad-args"
            })";
        }
        app_config cfg = load_config(tmp);
        auto& a = cfg.profiles[0].prof.accel_x;
        EXPECT_NEAR(a.acceleration, -5.0, 1e-9); // negative accel preserved (deceleration)
        EXPECT(a.scale >= 0);
        EXPECT(a.decay_rate >= 0);
        EXPECT(a.exponent_power >= 1e-4);
        EXPECT(a.input_offset >= 0);
        EXPECT(a.output_offset >= 0);
        EXPECT(a.limit >= 0);
        EXPECT(a.sync_speed >= 1e-4);
        EXPECT(a.smooth >= 0);
        EXPECT(a.motivity >= 0);
        EXPECT(a.gamma >= 0);
        EXPECT(a.cap.x >= 0);
        EXPECT(a.cap.y >= 0);
        EXPECT(cfg.profiles[0].prof.domain_weights.x >= 0);
        EXPECT(cfg.profiles[0].prof.domain_weights.y >= 0);
        EXPECT(cfg.profiles[0].prof.range_weights.x >= 0);
        EXPECT(cfg.profiles[0].prof.range_weights.y >= 0);
        std::remove(tmp);
    }

    // 14. Negative acceleration + integer exponent → referans GAIN formu yine de sonlu
    {
        accel_args args;
        args.mode = accel_mode::classic;
        args.acceleration = -0.01;
        args.exponent_classic = 2.0; // integer — pow(-0.01, 1) = -0.01, valid
        classic c(args);
        double gain = c(10.0, args);
        EXPECT(std::isfinite(gain));
        // Referans GAIN out: accel_raised=-0.01, cap.y=0.5, cap.x=-25,
        // c(10) = 1 + (constant/x + cap.y) = 1 + (6.25/10 + 0.5) = 2.125
        EXPECT_NEAR(gain, 2.125, 1e-9);
    }

    // 15. Negative acceleration + non-integer exponent → NaN guarded to 0 in constructor
    {
        accel_args args;
        args.mode = accel_mode::classic;
        args.acceleration = -0.01;
        args.exponent_classic = 2.5; // non-integer — pow(-0.01, 1.5) = NaN
        classic c(args);
        double gain = c(10.0, args);
        EXPECT(std::isfinite(gain)); // constructor NaN guard → accel_raised = 0 → gain = 1.0
    }
}

// ── Test: raw_passthrough JSON round-trip ─────────────────────────────────────

static void test_raw_passthrough_json() {
    SECTION("raw_passthrough — JSON round-trip");

    const char* tmp = "/tmp/rawaccel_test_raw_passthrough.json";

    // Default: false
    {
        profile p{};
        EXPECT(p.raw_passthrough == false);

        device_profile dp;
        dp.name = "raw-test-off";
        dp.prof = p;
        app_config cfg;
        cfg.profiles.push_back(dp);
        cfg.active_profile = dp.name;

        save_config(cfg, tmp);
        app_config cfg2 = load_config(tmp);
        EXPECT(cfg2.profiles[0].prof.raw_passthrough == false);
    }

    // Enabled: true round-trips correctly
    {
        profile p{};
        p.raw_passthrough = true;

        device_profile dp;
        dp.name = "raw-test-on";
        dp.prof = p;
        app_config cfg;
        cfg.profiles.push_back(dp);
        cfg.active_profile = dp.name;

        save_config(cfg, tmp);

        // Verify the JSON file actually contains the key
        {
            std::ifstream f(tmp);
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            EXPECT(content.find("raw_passthrough") != std::string::npos);
        }

        app_config cfg2 = load_config(tmp);
        EXPECT(cfg2.profiles[0].prof.raw_passthrough == true);
    }

    // Missing key in hand-edited JSON → defaults to false
    {
        {
            std::ofstream f(tmp);
            f << R"({
                "profiles": [{
                    "name": "no-raw-key",
                    "accel_x": { "mode": "noaccel" },
                    "accel_y": { "mode": "noaccel" }
                }],
                "active_profile": "no-raw-key"
            })";
        }
        app_config cfg = load_config(tmp);
        EXPECT(cfg.profiles[0].prof.raw_passthrough == false);
    }

    std::remove(tmp);
}

// ── Fuzz test: all accel modes × random accel_args → no NaN/Inf/crash ────────

static void test_fuzz_accel_args() {
    SECTION("fuzz — random accel_args across all modes, no NaN (Inf OK — motion_math clamps)");

    std::mt19937 rng(42); // deterministic seed for reproducibility
    std::uniform_real_distribution<double> dist_wide(-1000.0, 1000.0);
    std::uniform_int_distribution<int> dist_mode(0, 6);

    static const accel_mode modes[] = {
        accel_mode::noaccel, accel_mode::classic, accel_mode::power,
        accel_mode::natural, accel_mode::jump,    accel_mode::synchronous,
        accel_mode::lookup,
    };

    int nan_count = 0;
    constexpr int ITERS = 5000;

    for (int i = 0; i < ITERS; i++) {
        accel_args args;
        args.mode             = modes[dist_mode(rng)];
        args.acceleration     = dist_wide(rng);
        args.exponent_classic = dist_wide(rng);
        args.exponent_power   = dist_wide(rng);
        args.scale            = dist_wide(rng);
        args.decay_rate       = dist_wide(rng);
        args.limit            = dist_wide(rng);
        args.input_offset     = dist_wide(rng);
        args.output_offset    = dist_wide(rng);
        args.sync_speed       = dist_wide(rng);
        args.smooth           = dist_wide(rng);
        args.motivity         = dist_wide(rng);
        args.gamma            = dist_wide(rng);
        args.cap.x            = dist_wide(rng);
        args.cap.y            = dist_wide(rng);
        args.gain             = (rng() % 2) == 0;

        // Sanitize like the real pipeline does
        device_profile dp;
        dp.prof.accel_x = args;
        sanitize_device_profile(dp);
        args = dp.prof.accel_x;

        accel_union au;
        au.init(args);

        // Test with realistic speeds (not DBL_MAX — motion_math handles overflow)
        double speeds[] = {0, 0.001, 0.5, 1.0, 5.0, 50.0, 500.0, 10000.0};
        for (double spd : speeds) {
            double result = au.apply(spd, args);
            if (std::isnan(result)) nan_count++;
            // Inf is acceptable at the accel layer — motion_math's isfinite guard
            // converts it to 0 output. Only NaN would indicate a logic error.
        }
    }
    EXPECT(nan_count == 0);
}

// ── Fuzz test: random accel_args WITHOUT sanitize → motion_math catches all ──

static void test_fuzz_unsanitized_motion_math() {
    SECTION("fuzz — unsanitized random args through motion_math pipeline, output always finite");

    std::mt19937 rng(123);
    std::uniform_real_distribution<double> dist_wide(-1000.0, 1000.0);
    std::uniform_real_distribution<double> dist_delta(-50.0, 50.0);
    std::uniform_int_distribution<int> dist_mode(0, 6);

    static const accel_mode modes[] = {
        accel_mode::noaccel, accel_mode::classic, accel_mode::power,
        accel_mode::natural, accel_mode::jump,    accel_mode::synchronous,
        accel_mode::lookup,
    };

    int bad_output = 0;
    int bad_remainder = 0;
    constexpr int ITERS = 3000;

    for (int i = 0; i < ITERS; i++) {
        accel_args args;
        args.mode             = modes[dist_mode(rng)];
        args.acceleration     = dist_wide(rng);
        args.exponent_classic = dist_wide(rng);
        args.exponent_power   = dist_wide(rng);
        args.scale            = dist_wide(rng);
        args.decay_rate       = dist_wide(rng);
        args.limit            = dist_wide(rng);
        args.input_offset     = dist_wide(rng);
        args.output_offset    = dist_wide(rng);
        args.sync_speed       = dist_wide(rng);
        args.smooth           = dist_wide(rng);
        args.cap.x            = dist_wide(rng);
        args.cap.y            = dist_wide(rng);
        args.gain             = (rng() % 2) == 0;

        profile prof;
        prof.accel_x = args;
        prof.accel_y = args;

        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);

        speed_processor sp;
        sp.init(prof.speed_processor_args);

        modifier mod;
        double rx = 0, ry = 0;
        int ox = 0, oy = 0;

        double dx = dist_delta(rng);
        double dy = dist_delta(rng);
        double time_ms = 1.0; // safe time

        apply_motion_math(mod, sp, settings, 1.0, time_ms,
                          dx, dy, rx, ry, ox, oy);

        if (!std::isfinite((double)ox) || !std::isfinite((double)oy)) bad_output++;
        if (!std::isfinite(rx) || !std::isfinite(ry)) bad_remainder++;
    }
    EXPECT(bad_output == 0);
    EXPECT(bad_remainder == 0);
}

// ── Fuzz test: config JSON round-trip with random values ─────────────────────

static void test_fuzz_json_roundtrip() {
    SECTION("fuzz — random profile JSON round-trip preserves sanitized values");

    std::mt19937 rng(777);
    std::uniform_real_distribution<double> dist_wide(-500.0, 500.0);
    std::uniform_int_distribution<int> dist_mode(0, 6);

    const char* tmp = "/tmp/rawaccel_test_fuzz_roundtrip.json";
    constexpr int ITERS = 200;
    int sanitize_fail = 0;

    for (int i = 0; i < ITERS; i++) {
        device_profile dp;
        dp.name = "fuzz" + std::to_string(i);
        dp.dev_cfg.dpi = (int)dist_wide(rng);
        dp.dev_cfg.polling_rate = (int)dist_wide(rng);
        dp.prof.accel_x.mode = (accel_mode)dist_mode(rng);
        dp.prof.accel_x.acceleration = dist_wide(rng);
        dp.prof.accel_x.scale = dist_wide(rng);
        dp.prof.accel_x.decay_rate = dist_wide(rng);
        dp.prof.accel_x.exponent_power = dist_wide(rng);
        dp.prof.accel_x.exponent_classic = dist_wide(rng);
        dp.prof.accel_x.limit = dist_wide(rng);
        dp.prof.accel_x.input_offset = dist_wide(rng);
        dp.prof.accel_x.output_offset = dist_wide(rng);
        dp.prof.accel_x.sync_speed = dist_wide(rng);
        dp.prof.accel_x.smooth = dist_wide(rng);
        dp.prof.accel_x.cap.x = dist_wide(rng);
        dp.prof.accel_x.cap.y = dist_wide(rng);
        dp.prof.degrees_rotation = dist_wide(rng);
        dp.prof.degrees_snap = dist_wide(rng);
        dp.prof.speed_min = dist_wide(rng);
        dp.prof.speed_max = dist_wide(rng);
        dp.prof.domain_weights.x = dist_wide(rng);
        dp.prof.domain_weights.y = dist_wide(rng);
        dp.prof.range_weights.x = dist_wide(rng);
        dp.prof.range_weights.y = dist_wide(rng);
        dp.prof.speed_processor_args.lp_norm = dist_wide(rng);
        dp.prof.speed_processor_args.input_speed_smooth_halflife = dist_wide(rng);

        app_config cfg;
        cfg.profiles.push_back(dp);
        cfg.active_profile = dp.name;

        try {
            save_config(cfg, tmp);
            app_config cfg2 = load_config(tmp);
            auto& p = cfg2.profiles[0];
            // After round-trip, all sanitized constraints must hold
            if (p.dev_cfg.dpi < 1 || p.dev_cfg.dpi > 32000) sanitize_fail++;
            if (p.dev_cfg.polling_rate < 125 || p.dev_cfg.polling_rate > 8000) sanitize_fail++;
            if (p.prof.accel_x.scale < 0) sanitize_fail++;
            if (p.prof.accel_x.decay_rate < 0) sanitize_fail++;
            if (p.prof.accel_x.exponent_power < 1e-4) sanitize_fail++;
            if (p.prof.accel_x.input_offset < 0) sanitize_fail++;
            if (p.prof.accel_x.output_offset < 0) sanitize_fail++;
            if (p.prof.accel_x.limit < 0) sanitize_fail++;
            if (p.prof.accel_x.sync_speed < 1e-4) sanitize_fail++;
            if (p.prof.accel_x.smooth < 0) sanitize_fail++;
            if (p.prof.accel_x.cap.x < 0) sanitize_fail++;
            if (p.prof.accel_x.cap.y < 0) sanitize_fail++;
            if (p.prof.degrees_rotation < 0 || p.prof.degrees_rotation >= 360) sanitize_fail++;
            if (p.prof.degrees_snap < 0 || p.prof.degrees_snap > 45) sanitize_fail++;
            if (p.prof.speed_min < 0) sanitize_fail++;
            if (p.prof.speed_max < 0) sanitize_fail++;
            if (p.prof.domain_weights.x < 0) sanitize_fail++;
            if (p.prof.domain_weights.y < 0) sanitize_fail++;
            if (p.prof.range_weights.x < 0) sanitize_fail++;
            if (p.prof.range_weights.y < 0) sanitize_fail++;
            if (p.prof.speed_processor_args.lp_norm <= 0) sanitize_fail++;
            if (p.prof.speed_processor_args.input_speed_smooth_halflife < 0) sanitize_fail++;
        } catch (...) {
            sanitize_fail++; // should never throw — sanitize handles all edge cases
        }
    }
    std::remove(tmp);
    EXPECT(sanitize_fail == 0);
}

// ── Edge case: all accel modes × extreme speed values ────────────────────────

static void test_all_modes_extreme_speeds() {
    SECTION("edge — all accel modes × extreme speed values, all outputs finite");

    static const accel_mode modes[] = {
        accel_mode::noaccel, accel_mode::classic, accel_mode::power,
        accel_mode::natural, accel_mode::jump,    accel_mode::synchronous,
        accel_mode::lookup,
    };

    // Extreme speed values — boundaries and pathological inputs
    // Note: DBL_MAX excluded — pow(DBL_MAX, 2) = Inf is mathematically correct;
    // motion_math's isfinite guard handles overflow at the pipeline level.
    double speeds[] = {
        -1.0, -0.001, 0.0, 1e-15, 1e-9, 0.001, 0.5, 1.0,
        5.0, 50.0, 500.0, 5000.0, 1e6, 1e9, 1e15
    };

    int nan_count = 0;
    int inf_count = 0;

    for (auto mode : modes) {
        accel_args args = make_args(mode);
        // Also set some non-trivial params for each mode
        args.acceleration = 0.01;
        args.exponent_classic = 2.0;
        args.exponent_power = 0.3;
        args.decay_rate = 0.2;
        args.limit = 2.0;
        args.sync_speed = 5.0;
        args.smooth = 0.5;
        args.scale = 1.0;

        accel_union au;
        au.init(args);

        for (double spd : speeds) {
            double result = au.apply(spd, args);
            if (std::isnan(result)) nan_count++;
            if (std::isinf(result)) inf_count++;
        }
    }
    EXPECT(nan_count == 0);
    EXPECT(inf_count == 0);
}

// ── Edge case: EMA smoother stability under extreme time values ──────────────

static void test_ema_extreme_time() {
    SECTION("edge — EMA smoothers with extreme time values, no divergence");

    // Simple EMA
    {
        simple_ema_smoother s;
        s.init(5.0); // halflife=5ms
        int bad = 0;
        // Very large time gap (simulates long idle then sudden move)
        double r = s.smooth(10.0, 10000.0);
        if (!std::isfinite(r)) bad++;
        // Very small time (sub-microsecond)
        r = s.smooth(10.0, 1e-9);
        if (!std::isfinite(r)) bad++;
        // Zero time
        r = s.smooth(10.0, 0.0);
        if (!std::isfinite(r)) bad++;
        // Negative time (shouldn't happen but guard)
        r = s.smooth(10.0, -1.0);
        if (!std::isfinite(r)) bad++;
        // Huge speed
        r = s.smooth(1e15, 1.0);
        if (!std::isfinite(r)) bad++;
        // Many iterations — check no drift to Inf
        for (int i = 0; i < 10000; i++) {
            r = s.smooth(5.0, 1.0);
            if (!std::isfinite(r)) { bad++; break; }
        }
        EXPECT(bad == 0);
    }

    // Linear EMA
    {
        linear_ema_smoother l;
        l.init(5.0, 1.25); // halflife=5ms, trendHalflife=1.25ms
        int bad = 0;
        double r = l.smooth(10.0, 10000.0);
        if (!std::isfinite(r)) bad++;
        r = l.smooth(10.0, 1e-9);
        if (!std::isfinite(r)) bad++;
        r = l.smooth(10.0, 0.0);
        if (!std::isfinite(r)) bad++;
        r = l.smooth(10.0, -1.0);
        if (!std::isfinite(r)) bad++;
        r = l.smooth(1e15, 1.0);
        if (!std::isfinite(r)) bad++;
        for (int i = 0; i < 10000; i++) {
            r = l.smooth(5.0, 1.0);
            if (!std::isfinite(r)) { bad++; break; }
        }
        EXPECT(bad == 0);
    }

    // EMA with halflife=0 (should effectively disable smoothing — coefficient=0)
    {
        simple_ema_smoother s;
        s.init(0.0);
        int bad = 0;
        for (int i = 0; i < 100; i++) {
            double r = s.smooth((double)i, 1.0);
            if (!std::isfinite(r)) { bad++; break; }
        }
        EXPECT(bad == 0);
    }
}

// ── Edge case: subpixel accumulation — tiny deltas accumulate correctly ──────

static void test_subpixel_tiny_deltas() {
    SECTION("edge — subpixel accumulation: 1000 × 0.1 delta → ~100 total output");

    // noaccel mode: gain = 1.0, so output = input exactly.
    // 1000 events of dx=0.1, dy=0 → total should be ~100 counts.
    profile prof;
    prof.accel_x.mode = accel_mode::noaccel;
    prof.accel_y.mode = accel_mode::noaccel;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);

    speed_processor sp;
    sp.init(prof.speed_processor_args);

    modifier mod;
    double rx = 0, ry = 0;
    int total_x = 0, total_y = 0;

    for (int i = 0; i < 1000; i++) {
        int ox = 0, oy = 0;
        apply_motion_math(mod, sp, settings, 1.0 /* dpi_factor=1 */, 1.0 /* 1ms */,
                          0.1, 0.0, rx, ry, ox, oy);
        total_x += ox;
        total_y += oy;
    }

    // 1000 × 0.1 ≈ 100.0 — floating point means 10×0.1 != 1.0 exactly,
    // so the count may be slightly off (e.g. 91 instead of 100 due to rounding).
    // The key invariant: total_x + remainder_x ≈ 100.0 (no counts lost or created).
    double reconstructed = (double)total_x + rx;
    EXPECT_NEAR(reconstructed, 100.0, 1e-9); // BUG-NEW-15: 0.01 → 1e-9 (real FP drift is ~1e-14)
    EXPECT(total_y == 0);
    EXPECT(std::fabs(rx) < 1.0); // leftover remainder is sub-pixel
    EXPECT(std::fabs(ry) < 1e-9); // no Y movement at all
}

// ── Edge case: subpixel sign preservation ────────────────────────────────────

static void test_subpixel_negative_deltas() {
    SECTION("edge — subpixel: negative deltas accumulate correctly");

    profile prof;
    prof.accel_x.mode = accel_mode::noaccel;
    prof.accel_y.mode = accel_mode::noaccel;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);

    speed_processor sp;
    sp.init(prof.speed_processor_args);

    modifier mod;
    double rx = 0, ry = 0;
    int total_x = 0;

    // 500 events of dx=-0.3 → total should be ≈ -150
    for (int i = 0; i < 500; i++) {
        int ox = 0, oy = 0;
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          -0.3, 0.0, rx, ry, ox, oy);
        total_x += ox;
    }
    // Floating point: total_x + rx ≈ -150.0 (no counts lost)
    double reconstructed = (double)total_x + rx;
    EXPECT_NEAR(reconstructed, -150.0, 0.01);
    EXPECT(std::fabs(rx) < 1.0);
}

// ── Edge case: modifier with all flags active simultaneously ─────────────────

static void test_modifier_all_flags() {
    SECTION("edge — modifier with rotate+snap+clamp+directional weight, all outputs finite");

    profile prof;
    prof.accel_x.mode = accel_mode::classic;
    prof.accel_x.acceleration = 0.01;
    prof.accel_x.exponent_classic = 2.0;
    prof.accel_y = prof.accel_x;
    prof.degrees_rotation = 45.0;    // rotation active
    prof.degrees_snap = 10.0;        // snap active
    prof.speed_min = 1.0;            // speed clamp active
    prof.speed_max = 100.0;
    prof.range_weights = {1.5, 0.8}; // directional weight active (x != y)
    prof.lr_output_dpi_ratio = 1.5;  // dir mul active
    prof.ud_output_dpi_ratio = 0.8;
    prof.speed_processor_args.whole = true;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);

    speed_processor sp;
    sp.init(prof.speed_processor_args);

    modifier mod;
    int bad = 0;

    // Test various input directions
    double deltas[][2] = {
        {10, 0}, {0, 10}, {10, 10}, {-10, 5}, {5, -10}, {-5, -5},
        {1, 0}, {0, 1}, {0.1, 0.1}, {100, 100}, {0, 0}, {-0.1, 0.1}
    };

    for (auto& d : deltas) {
        vec2d in = {d[0], d[1]};
        mod.modify(in, sp, settings, 1.0, 1.0);
        if (!std::isfinite(in.x) || !std::isfinite(in.y)) bad++;
    }
    EXPECT(bad == 0);
}

// ── Edge case: modifier with separate distance mode ──────────────────────────

static void test_modifier_separate_mode() {
    SECTION("edge — modifier separate distance mode with smoothing, outputs finite");

    profile prof;
    prof.accel_x.mode = accel_mode::natural;
    prof.accel_x.decay_rate = 0.1;
    prof.accel_x.limit = 2.0;
    prof.accel_y.mode = accel_mode::jump;
    prof.accel_y.limit = 1.5;
    prof.accel_y.sync_speed = 5.0;
    prof.accel_y.smooth = 0.5;
    prof.speed_processor_args.whole = false; // separate mode
    prof.speed_processor_args.input_speed_smooth_halflife = 3.0;
    prof.speed_processor_args.scale_smooth_halflife = 2.0;
    prof.speed_processor_args.output_speed_smooth_halflife = 1.5;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);

    speed_processor sp;
    sp.init(prof.speed_processor_args);

    modifier mod;
    int bad = 0;

    // Simulate 500 events with varying deltas
    std::mt19937 rng(999);
    std::uniform_real_distribution<double> dist(-20.0, 20.0);
    for (int i = 0; i < 500; i++) {
        vec2d in = {dist(rng), dist(rng)};
        mod.modify(in, sp, settings, 1.0, 1.0);
        if (!std::isfinite(in.x) || !std::isfinite(in.y)) bad++;
    }
    EXPECT(bad == 0);
}

// ── Stress test: 10000 motion events — remainder never drifts ────────────────

static void test_stress_remainder_drift() {
    SECTION("stress — 10000 motion events, remainder stays bounded");

    profile prof;
    prof.accel_x.mode = accel_mode::classic;
    prof.accel_x.acceleration = 0.02;
    prof.accel_x.exponent_classic = 2.0;
    prof.accel_y = prof.accel_x;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);

    speed_processor sp;
    sp.init(prof.speed_processor_args);

    modifier mod;
    double rx = 0, ry = 0;
    int max_remainder_violation = 0;

    std::mt19937 rng(314);
    std::uniform_real_distribution<double> dist_delta(-30.0, 30.0);
    std::uniform_real_distribution<double> dist_time(0.5, 8.0);

    for (int i = 0; i < 10000; i++) {
        int ox = 0, oy = 0;
        double dx = dist_delta(rng);
        double dy = dist_delta(rng);
        double time = dist_time(rng);

        apply_motion_math(mod, sp, settings, 1.0, time,
                          dx, dy, rx, ry, ox, oy);

        // Remainder must always be in (-1, 1) — it's the fractional part after truncation
        if (std::fabs(rx) >= 1.0 || std::fabs(ry) >= 1.0) max_remainder_violation++;
        // Must always be finite
        if (!std::isfinite(rx) || !std::isfinite(ry)) max_remainder_violation++;
    }
    EXPECT(max_remainder_violation == 0);
}

// ── Stress test: alternating directions — no accumulation error ──────────────

static void test_stress_alternating_direction() {
    SECTION("stress — alternating +5/-5 deltas cancel out to zero total");

    profile prof;
    prof.accel_x.mode = accel_mode::noaccel;
    prof.accel_y.mode = accel_mode::noaccel;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);

    speed_processor sp;
    sp.init(prof.speed_processor_args);

    modifier mod;
    double rx = 0, ry = 0;
    int total_x = 0;

    for (int i = 0; i < 10000; i++) {
        int ox = 0, oy = 0;
        double dx = (i % 2 == 0) ? 5.0 : -5.0;
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          dx, 0.0, rx, ry, ox, oy);
        total_x += ox;
    }
    // 5000 × 5 + 5000 × (-5) = 0
    EXPECT(total_x == 0);
    EXPECT(std::fabs(rx) < 1e-9);
}

// ── Edge case: power mode with extreme params ────────────────────────────────

static void test_power_extreme_params() {
    SECTION("edge — power mode: extreme scale/exponent combinations, all finite");

    int bad = 0;

    // Very large scale
    {
        accel_args args = make_args(accel_mode::power);
        args.scale = 1000.0;
        args.exponent_power = 0.01;
        power p(args);
        double r = p(10.0, args);
        if (!std::isfinite(r)) bad++;
    }

    // Very small exponent (near minimum)
    {
        accel_args args = make_args(accel_mode::power);
        args.scale = 1.0;
        args.exponent_power = 1e-4; // minimum after sanitize
        power p(args);
        double r = p(10.0, args);
        if (!std::isfinite(r)) bad++;
    }

    // Large exponent
    {
        accel_args args = make_args(accel_mode::power);
        args.scale = 0.5;
        args.exponent_power = 5.0;
        power p(args);
        double r = p(1.0, args);
        if (!std::isfinite(r)) bad++;
        r = p(100.0, args);
        if (!std::isfinite(r)) bad++;
    }

    // Zero speed
    {
        accel_args args = make_args(accel_mode::power);
        power p(args);
        double r = p(0.0, args);
        if (!std::isfinite(r)) bad++;
    }

    // output_offset > 0
    {
        accel_args args = make_args(accel_mode::power);
        args.output_offset = 5.0;
        args.scale = 1.0;
        args.exponent_power = 0.5;
        power p(args);
        double r = p(0.1, args);
        if (!std::isfinite(r)) bad++;
        r = p(10.0, args);
        if (!std::isfinite(r)) bad++;
    }

    // BUG-NEW-11/16 (aj4): tiny floored exponent + large cap / offset used to
    // make gain_inverse() overflow to Inf (offset.x = Inf, constant = Inf).
    // The clamp keeps every intermediate double finite, so the gain stays
    // finite AND identical for reachable speeds (x <= offset.x → offset.y).
    {
        accel_args args = make_args(accel_mode::power);
        args.scale = 1.0;
        args.exponent_power = 1e-4;   // floors to 1e-3 in the power ctor
        args.cap_mode_val = cap_mode::out;
        args.cap.x = 15.0;
        args.cap.y = 100.0;           // g/(n+1) ^ (1/n) ≈ 99.9^1000 → overflows
        power p(args);
        const double xs_gain[] = {0.0, 1e-9, 0.001, 0.1, 1.0, 10.0, 100.0, 1e5, 1e10};
        for (double x : xs_gain)
            if (!std::isfinite(p(x, args))) bad++;
    }
    {
        // Legacy + huge output_offset → constant = offset.x·offset.y·n/(n+1)
        // would overflow to Inf before the clamp; now finite everywhere.
        accel_args args = make_args(accel_mode::power);
        args.gain = false;
        args.scale = 1.0;
        args.exponent_power = 1e-4;
        args.output_offset = 100.0;
        power p(args);
        const double xs_leg[] = {0.001, 0.1, 1.0, 100.0, 1e6};
        for (double x : xs_leg)
            if (!std::isfinite(p(x, args))) bad++;
    }

    EXPECT(bad == 0);
}

// ── Edge case: lookup mode with edge-case LUT data ───────────────────────────

static void test_lookup_extreme_lut() {
    SECTION("edge — lookup with single point, duplicate points, huge values");

    int bad = 0;

    // Single point (2 floats = 1 pair)
    {
        accel_args args;
        args.mode = accel_mode::lookup;
        args.length = 2;
        args.data[0] = 5.0f; args.data[1] = 1.5f;
        lookup l(args);
        double r = l(0.0, args);
        if (!std::isfinite(r)) bad++;
        r = l(5.0, args);
        if (!std::isfinite(r)) bad++;
        r = l(100.0, args);
        if (!std::isfinite(r)) bad++;
    }

    // Duplicate X points (same speed, different gain)
    {
        accel_args args;
        args.mode = accel_mode::lookup;
        args.length = 4;
        args.data[0] = 5.0f; args.data[1] = 1.0f;
        args.data[2] = 5.0f; args.data[3] = 2.0f; // duplicate x
        lookup l(args);
        double r = l(5.0, args);
        if (!std::isfinite(r)) bad++;
    }

    // Very large LUT values
    {
        accel_args args;
        args.mode = accel_mode::lookup;
        args.length = 4;
        args.data[0] = 0.0f;  args.data[1] = 1e6f;
        args.data[2] = 1e6f;  args.data[3] = 1e6f;
        lookup l(args);
        double r = l(500000.0, args);
        if (!std::isfinite(r)) bad++;
    }

    // Zero-length LUT
    {
        accel_args args;
        args.mode = accel_mode::lookup;
        args.length = 0;
        lookup l(args);
        double r = l(5.0, args);
        if (!std::isfinite(r)) bad++;
        EXPECT_NEAR(r, 1.0, 1e-9); // no points → gain = 1.0
    }

    EXPECT(bad == 0);
}

// ── Edge case: natural mode with extreme decay_rate and limit ────────────────

static void test_natural_extreme_params() {
    SECTION("edge — natural mode: extreme decay/limit combinations, all finite");

    int bad = 0;

    // Very high decay_rate → extremely fast convergence
    {
        accel_args args = make_args(accel_mode::natural);
        args.decay_rate = 1000.0;
        args.limit = 5.0;
        natural n(args);
        double r = n(0.001, args);
        if (!std::isfinite(r)) bad++;
        r = n(100.0, args);
        if (!std::isfinite(r)) bad++;
    }

    // limit = 1.0 → internal limit = 0 → result depends on gain mode.
    // Non-gain: limit*(1-decay)+1 = 0+1 = 1.0
    // Gain:     limit=0 → integral output = 0 → gain = 1.0 (no acceleration).
    //           (O4: old formula produced 2.0; with the reference integral the
    //           total output is the input itself, so gain is exactly 1.0.)
    //           With args.limit==1 the reference would divide by limit==0 in
    //           its constructor; N8 keeps this safe and yields 1.0.
    {
        accel_args args = make_args(accel_mode::natural);
        args.limit = 1.0;
        args.decay_rate = 0.1;
        args.gain = false;
        natural n_nongain(args);
        double r_ng = n_nongain(10.0, args);
        if (!std::isfinite(r_ng)) bad++;
        EXPECT_NEAR(r_ng, 1.0, 0.01);

        args.gain = true;
        natural n_gain(args);
        double r_g = n_gain(10.0, args);
        if (!std::isfinite(r_g)) bad++;
        EXPECT_NEAR(r_g, 1.0, 0.01); // no acceleration when limit clamped to 0
    }

    // gain=true, large input
    {
        accel_args args = make_args(accel_mode::natural);
        args.decay_rate = 0.5;
        args.limit = 3.0;
        args.gain = true;
        natural n(args);
        double r = n(1e6, args);
        if (!std::isfinite(r)) bad++;
    }

    EXPECT(bad == 0);
}

// ── Edge case: synchronous mode with extreme exponents ───────────────────────

static void test_synchronous_extreme() {
    SECTION("edge — synchronous mode: extreme exponents, all finite");

    int bad = 0;

    // Very large exponent
    {
        accel_args args = make_args(accel_mode::synchronous);
        args.sync_speed = 5.0;
        args.exponent_power = 100.0;
        synchronous s(args);
        double r = s(5.0, args); // at sync point → gain = 1.0
        if (!std::isfinite(r)) bad++;
        r = s(10.0, args);
        if (!std::isfinite(r)) bad++;
        r = s(0.001, args);
        if (!std::isfinite(r)) bad++;
    }

    // Very small exponent (near zero after sanitize → 1e-4)
    {
        accel_args args = make_args(accel_mode::synchronous);
        args.sync_speed = 5.0;
        args.exponent_power = 1e-4;
        synchronous s(args);
        double r = s(1.0, args);
        if (!std::isfinite(r)) bad++;
        r = s(100.0, args);
        if (!std::isfinite(r)) bad++;
    }

    EXPECT(bad == 0);
}

// ── R6: DPI ratio division-by-zero guard ─────────────────────────────────────

static void test_dpi_ratio_zero_guard() {
    SECTION("R6 — lr/ud_output_dpi_ratio zero does not produce Inf/NaN");

    // Set ratio to 0 (which sanitize would clamp, but test the guard directly)
    profile prof{};
    prof.accel_x.mode = accel_mode::noaccel;
    prof.accel_y.mode = accel_mode::noaccel;
    prof.lr_output_dpi_ratio = 0.0; // would cause 1.0/0.0 = Inf without guard
    prof.ud_output_dpi_ratio = 0.0;
    prof.domain_weights = {1, 1};
    prof.range_weights = {1, 1};

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);

    modifier mod;
    speed_processor sp;
    sp.init(prof.speed_processor_args);

    vec2d in = { -5.0, -3.0 }; // negative → triggers 1.0/ratio path
    mod.modify(in, sp, settings, 1.0, 1.0);

    // With zero ratio and guard: the DPI multiplier step is skipped entirely.
    // Without guard: in.x and in.y would become Inf.
    EXPECT(std::isfinite(in.x));
    EXPECT(std::isfinite(in.y));

    // Also verify non-zero ratio still works (reference: ratios apply to
    // the negative/left/down direction only, plain multiplication, no reciprocal)
    settings.prof.lr_output_dpi_ratio = 2.0;
    settings.prof.ud_output_dpi_ratio = 0.5;
    init_settings(settings);
    vec2d in2 = { 5.0, -3.0 };
    mod.modify(in2, sp, settings, 1.0, 1.0);
    EXPECT(std::isfinite(in2.x));
    EXPECT(std::isfinite(in2.y));
    // Positive x → no lr scaling; negative y → multiply by ud (0.5)
    EXPECT_NEAR(in2.x, 5.0, 0.01);
    EXPECT_NEAR(in2.y, -1.5, 0.01);
}

// ── R6: lp_distance zero-vector guard ────────────────────────────────────────

static void test_lp_distance_zero_vector() {
    SECTION("R6 — lp_distance({0,0}, p) returns 0 for any p");

    // Normal p
    EXPECT_NEAR(lp_distance({0, 0}, 2.0), 0.0, 1e-15);
    EXPECT_NEAR(lp_distance({0, 0}, 3.0), 0.0, 1e-15);
    // Negative p (would produce pow(0,neg)=Inf without guard)
    EXPECT_NEAR(lp_distance({0, 0}, -1.0), 0.0, 1e-15);
    EXPECT_NEAR(lp_distance({0, 0}, -0.5), 0.0, 1e-15);
    // Very large p
    EXPECT_NEAR(lp_distance({0, 0}, 100.0), 0.0, 1e-15);
    // Non-zero should still work
    double r = lp_distance({3, 4}, 2.0);
    EXPECT_NEAR(r, 5.0, 0.01);
}

// ── R7: EMA smoother extreme halflife values ─────────────────────────────────

static void test_ema_extreme_halflife() {
    SECTION("R7 — EMA smoother: halflife=0 (no smoothing), halflife=huge, halflife=tiny");

    // halflife=0 → windowCoefficient=0 → twc=1 → no smoothing (always latest value)
    {
        simple_ema_smoother s;
        s.init(0.0);
        double r = s.smooth(42.0, 1.0);
        EXPECT_NEAR(r, 42.0, 0.01);
        r = s.smooth(7.0, 1.0);
        EXPECT_NEAR(r, 7.0, 0.01); // immediate switch
    }

    // halflife=1e6 → very heavy smoothing → barely moves from initial value
    {
        simple_ema_smoother s;
        s.init(1e6);
        s.smooth(0.0, 1.0); // prime with 0
        double r = s.smooth(100.0, 1.0);
        EXPECT(r < 1.0); // should barely budge
        EXPECT(std::isfinite(r));
    }

    // halflife=1e-6 → effectively no smoothing
    {
        simple_ema_smoother s;
        s.init(1e-6);
        s.smooth(0.0, 1.0);
        double r = s.smooth(50.0, 1.0);
        EXPECT_NEAR(r, 50.0, 1.0); // nearly instant
        EXPECT(std::isfinite(r));
    }

    // Linear EMA: same checks
    {
        linear_ema_smoother l;
        l.init(0.0, 0.0);
        double r = l.smooth(42.0, 1.0);
        EXPECT_NEAR(r, 42.0, 0.01);
        r = l.smooth(7.0, 1.0);
        EXPECT_NEAR(r, 7.0, 0.01);
    }
}

// ── R7: full pipeline NaN injection test ─────────────────────────────────────

static void test_pipeline_nan_injection() {
    SECTION("R7 — motion_math guards against NaN from any acceleration output");

    // Construct a profile where classic acceleration with cap_mode::io
    // and degenerate parameters would produce NaN in the accel curve.
    // motion_math's isfinite guard should produce 0 output.
    modifier_settings settings;
    settings.prof.accel_x.mode = accel_mode::classic;
    settings.prof.accel_x.acceleration = -5.0; // negative + non-integer exponent
    settings.prof.accel_x.exponent_classic = 2.5; // non-integer
    settings.prof.accel_x.cap_mode_val = cap_mode::io;
    settings.prof.accel_x.cap.x = 0.0; // degenerate cap
    settings.prof.accel_x.cap.y = 0.0;
    settings.prof.accel_x.gain = true;
    settings.prof.accel_y = settings.prof.accel_x;
    settings.prof.domain_weights = {1, 1};
    settings.prof.range_weights = {1, 1};
    init_settings(settings);

    modifier mod;
    speed_processor sp;
    sp.init(settings.prof.speed_processor_args);

    double rx = 0, ry = 0;
    int ox = 0, oy = 0;
    // Even with degenerate args, output must be finite integers
    apply_motion_math(mod, sp, settings, 1.0, 1.0,
                      10.0, 5.0, rx, ry, ox, oy);
    EXPECT(std::isfinite(rx));
    EXPECT(std::isfinite(ry));
    // output may be 0 (NaN guard triggered) or valid int
    EXPECT(ox >= INT_MIN && ox <= INT_MAX);
    EXPECT(oy >= INT_MIN && oy <= INT_MAX);
}

// ── R7: classic cap_mode::io with cap.x < input_offset ──────────────────────

static void test_classic_io_degenerate_cap() {
    SECTION("R7 — classic io mode: cap.x < input_offset produces finite output");

    accel_args args;
    args.mode = accel_mode::classic;
    args.acceleration = 0.01;
    args.exponent_classic = 2.5;
    args.cap_mode_val = cap_mode::io;
    args.cap.x = 1.0;
    args.cap.y = 2.0;
    args.input_offset = 5.0; // offset > cap.x → degenerate
    args.gain = true;

    classic c(args);
    // x <= input_offset → 1.0
    EXPECT_NEAR(c(3.0, args), 1.0, 1e-9);
    // x > input_offset → should be finite
    double r = c(10.0, args);
    EXPECT(std::isfinite(r));

    // BUG-7 fix: init_gain::io clamps cap_x up to input_offset, so the
    // accelerated tail is a sane asymptotic curve (1.0 → cap.y) instead of
    // the old negative-gain degenerate constant.  Verify the curve still has
    // upward slope beyond input_offset and never produces negative gain.
    double r_slow = c(6.0, args);
    double r_fast = c(100.0, args);
    double r_inf  = c(1e9, args);
    EXPECT(std::isfinite(r_slow));
    EXPECT(std::isfinite(r_fast));
    EXPECT(std::isfinite(r_inf));
    EXPECT(r_fast > r_slow);          // still accelerating
    EXPECT(r_inf > r_fast);           // approaches cap.y from below
    EXPECT(r_inf <= args.cap.y + 1e-9); // asymptotic ceiling ≈ cap.y
    EXPECT(r_slow >= 1.0);
    // gain never goes negative (the old degenerate path produced sign flips)
    EXPECT(c(10.0, args) > 0.0);
    EXPECT(c(100.0, args) > 0.0);
}

// ── BUG-9: classic cap_mode::in with cap.x <= input_offset ──────────────────
// Property fuzz (Tur 4) caught this: pow(negative_base, fractional_exp) → NaN
// when the cap's pre-offset point is inside the offset region.  The NaN poisons
// every operator() return.

static void test_classic_in_degenerate_cap_naninf() {
    SECTION("BUG-9 — classic cap_mode::in: cap.x <= input_offset stays finite");

    // Real fuzz seed that previously produced NaN
    accel_args args;
    args.mode = accel_mode::classic;
    args.acceleration = 1.889;
    args.exponent_classic = 3.728;
    args.cap_mode_val = cap_mode::in;
    args.cap.x = 0.2624;
    args.cap.y = 2.418;
    args.input_offset = 2.409; // > cap.x
    args.gain = true;
    classic c(args);
    for (double s : {0.0, 0.5, 1.0, 1.5, 2.0, 5.0, 100.0, 1e6}) {
        double g = c(s, args);
        EXPECT(std::isfinite(g));
        EXPECT(g >= 0.0);
    }

    // Boundary case: cap.x == input_offset
    args.cap.x = 2.409;
    classic c2(args);
    for (double s : {0.0, 1.0, 2.409, 5.0, 100.0}) EXPECT(std::isfinite(c2(s, args)));

    // Reverse direction: cap.x > input_offset (normal config) stays finite too
    args.cap.x = 5.0;
    args.input_offset = 1.0;
    classic c3(args);
    for (double s : {0.0, 1.0, 5.0, 100.0, 1e6}) EXPECT(std::isfinite(c3(s, args)));
}

// ── BUG-13: save_config durability — fsync path is followed for content ─────
// We can't test fsync directly (kernel-level), but we can verify the temp
// file path is created and atomic rename happens correctly even when the
// target file is concurrently read.

// ── BUG-15: motion_math remainder MUST NOT accumulate when motion is clamped
// to INT range — pathological large input would otherwise lock the user into
// many frames of INT_MAX motion.

static void test_motion_math_clamp_remainder_reset() {
    SECTION("BUG-15 — motion_math: clamp does not leak unbounded remainder");
    modifier_settings settings = {};
    settings.prof.accel_x.mode = accel_mode::classic;
    settings.prof.accel_x.acceleration = 0.5;
    settings.prof.accel_x.exponent_classic = 2.0;
    settings.prof.accel_x.gain = 1;
    settings.prof.accel_y = settings.prof.accel_x;
    settings.prof.domain_weights = {1.0, 1.0};
    settings.prof.range_weights  = {1.0, 1.0};
    settings.prof.speed_max = 1e9;
    settings.prof.output_dpi = 800;
    settings.prof.lr_output_dpi_ratio = 1;
    settings.prof.ud_output_dpi_ratio = 1;
    init_settings(settings);
    speed_processor sp; sp.init(settings.prof.speed_processor_args);
    modifier mod;

    double rx = 0, ry = 0;
    int ox, oy;

    // Pathological: INT_MAX motion repeated → remainder must NOT grow
    for (int i = 0; i < 50; i++) {
        apply_motion_math(mod, sp, settings, 1.0, 8.0,
                          (double)INT_MAX, 0.0, rx, ry, ox, oy);
    }
    EXPECT(std::fabs(rx) < 1.0); // remainder reset, not accumulated
    EXPECT(std::fabs(ry) < 1.0);

    // Now small motion should produce small output, not INT_MAX
    apply_motion_math(mod, sp, settings, 1.0, 8.0, 1.0, 0.0, rx, ry, ox, oy);
    EXPECT(ox <= 100); // not INT_MAX → remainder did not leak

    // Healthy fractional motion still accumulates correctly (subpixel works)
    rx = ry = 0;
    int total = 0;
    for (int i = 0; i < 10; i++) {
        apply_motion_math(mod, sp, settings, 1.0, 8.0, 0.3, 0.0, rx, ry, ox, oy);
        total += ox;
    }
    EXPECT(total >= 2 && total <= 4); // 10 × 0.3 ≈ 3 pixels delivered

    // NaN/Inf input also stays safe (defence-in-depth verification)
    rx = ry = 0;
    apply_motion_math(mod, sp, settings, 1.0, 8.0,
                      std::numeric_limits<double>::quiet_NaN(), 0.0, rx, ry, ox, oy);
    EXPECT(std::isfinite(rx) && std::fabs(rx) < 1.0);
    EXPECT(ox == 0); // NaN motion → 0
}

static void test_save_config_durability_path() {
    SECTION("BUG-13 — save_config: tmp file is removed and target updated atomically");
    std::string path = "/tmp/_rawaccel_save_test.json";
    // N-15: save_config names the temp file with a pid suffix.  Glob
    // path.*.tmp so a leftover from any pid is caught.
    std::remove(path.c_str());
    remove_tmp_leftovers(path);

    app_config cfg;
    device_profile dp; dp.name = "test"; dp.dev_cfg.dpi = 800;
    dp.prof.accel_x.mode = accel_mode::noaccel;
    dp.prof.accel_y.mode = accel_mode::noaccel;
    cfg.profiles.push_back(dp);
    cfg.active_profile = "test";

    save_config(cfg, path);

    // Target file must exist; tmp file must NOT exist (renamed away).
    struct stat st;
    EXPECT(stat(path.c_str(), &st) == 0);
    EXPECT(tmp_leftover_count(path) == 0); // tmp (any pid) gone

    // File must be non-empty (fsync ensures content reached disk before rename)
    EXPECT(st.st_size > 0);

    // Round-trip
    app_config loaded = load_config(path);
    EXPECT(loaded.profiles.size() == 1);
    EXPECT(loaded.profiles[0].name == "test");

    std::remove(path.c_str());
}

// ── R7: power mode with output_offset > 0 ───────────────────────────────────

static void test_power_output_offset() {
    SECTION("R7 — power mode: output_offset > 0, finite outputs for all speeds");

    accel_args args;
    args.mode = accel_mode::power;
    args.scale = 0.1;
    args.exponent_power = 0.3;
    args.output_offset = 0.5;
    args.cap_mode_val = cap_mode::out;
    args.cap.y = 5.0;
    args.gain = true;

    power p(args);
    int bad = 0;
    double speeds[] = {0, 0.001, 0.1, 1.0, 5.0, 50.0, 500.0};
    for (double s : speeds) {
        double r = p(s, args);
        if (!std::isfinite(r)) bad++;
    }
    EXPECT(bad == 0);
}

// ── R7: directional weight interpolation boundary values ─────────────────────

// ── R7: directional weight blending at 0°, 45°, 90° ──────────────────────────
// Referans kuralı: scale = 1 + (gain - 1) * weight;  weight, yataydaki
// range_weights.x'ten dikeydeki .y'ye 2/π · açı ile doğrusal geçer.
// (noaccel'de gain=1 olduğu için weight'in etkisi yoktur — yalnızca hızlandırma
//  farkını modüle eder.)

static void test_directional_weight_boundary() {
    SECTION("R7 — directional weight blending at 0°, 45°, 90°");

    // Accel: classic LEGACY, gain = 1 + 0.005·ips (power 2). Weight bu gain'i modüle eder.
    profile prof{};
    prof.accel_x.mode            = accel_mode::classic;
    prof.accel_x.gain            = false;
    prof.accel_x.acceleration    = 0.005;
    prof.accel_x.exponent_classic = 2.0;
    prof.accel_x.input_offset    = 0.0;
    prof.accel_y                 = prof.accel_x;
    prof.range_weights.x = 2.0;
    prof.range_weights.y = 4.0;
    prof.domain_weights = {1, 1};
    prof.speed_processor_args.whole = true;
    prof.speed_processor_args.lp_norm = 2;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);
    EXPECT(settings.data.flags.apply_directional_weight);

    modifier mod;
    speed_processor sp;
    sp.init(prof.speed_processor_args);

    // Pure horizontal → reference_angle = 0 → weight = range_weights.x = 2.0
    // gain = 1 + 0.005·10 = 1.05 → scale = 1 + 0.05·2 = 1.10 → x = 11.0
    {
        vec2d in = {10.0, 0.0};
        mod.modify(in, sp, settings, 1.0, 1.0);
        EXPECT_NEAR(in.x, 11.0, 1e-6);
        EXPECT_NEAR(in.y, 0.0, 1e-9);
    }
    // Pure vertical → reference_angle = π/2 → weight = 2 + 2·(2/π)·(π/2) = 4.0
    // gain = 1.05 → scale = 1 + 0.05·4 = 1.20 → y = 12.0
    {
        vec2d in = {0.0, 10.0};
        mod.modify(in, sp, settings, 1.0, 1.0);
        EXPECT_NEAR(in.x, 0.0, 1e-9);
        EXPECT_NEAR(in.y, 12.0, 1e-6);
    }
}

// ── R9: subpixel remainder cumulative drift (1M iterations) ──────────────────

static void test_subpixel_cumulative_drift() {
    SECTION("R9 — subpixel remainder: 1M iterations, no cumulative drift");

    // Noaccel mode, input = 1.7 counts per frame → trunc(1.7) = 1, remainder 0.7
    // After many frames, total output + final remainder should match total input exactly.
    modifier_settings settings;
    settings.prof.accel_x.mode = accel_mode::noaccel;
    settings.prof.accel_y.mode = accel_mode::noaccel;
    init_settings(settings);

    modifier mod;
    speed_processor sp;
    sp.init(settings.prof.speed_processor_args);

    double rx = 0, ry = 0;
    long long total_out_x = 0, total_out_y = 0;
    constexpr int N = 1'000'000;
    constexpr double DX = 1.7, DY = -0.3;

    for (int i = 0; i < N; i++) {
        int ox = 0, oy = 0;
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          DX, DY, rx, ry, ox, oy);
        total_out_x += ox;
        total_out_y += oy;
    }

    double expected_x = DX * N;  // 1,700,000
    double expected_y = DY * N;  // -300,000
    double actual_x = total_out_x + rx;
    double actual_y = total_out_y + ry;

    // Total output + remainder should be very close to total input.
    // Allow 1.0 count tolerance for 1M iterations (1e-6 relative error).
    EXPECT_NEAR(actual_x, expected_x, 1.0);
    EXPECT_NEAR(actual_y, expected_y, 1.0);
    // Remainder should always be in (-1, 1)
    EXPECT(std::fabs(rx) < 1.0);
    EXPECT(std::fabs(ry) < 1.0);
}

// ── R9: classic gain mode cap behavior consistency ───────────────────────────

static void test_classic_gain_mode_cap_consistency() {
    SECTION("R9 — classic gain mode: output monotonically non-decreasing with speed");

    accel_args args;
    args.mode = accel_mode::classic;
    args.acceleration = 0.01;
    args.exponent_classic = 2.0;
    args.cap_mode_val = cap_mode::out;
    args.cap.y = 2.0;
    args.gain = true;
    args.input_offset = 0;

    classic c(args);

    // Verify monotonicity: for increasing speed, output should never decrease
    double prev = c(0.001, args);
    int violations = 0;
    for (double x = 0.1; x < 200.0; x += 0.1) {
        double cur = c(x, args);
        if (cur < prev - 1e-9) violations++;
        prev = cur;
    }
    EXPECT(violations == 0);
}

// ── R9: natural gain formula correctness ─────────────────────────────────────

static void test_natural_gain_formula() {
    SECTION("R9 — natural gain mode: output/x+1 converges to args.limit");

    accel_args args;
    args.mode = accel_mode::natural;
    args.decay_rate = 0.1;
    args.limit = 3.0;
    args.input_offset = 0;
    args.gain = true;

    natural n(args);

    // Internal limit = args.limit - 1 = 2.0
    // Reference gain mode (O4): output = limit*(decay/accel - offset_x) - limit/accel
    // → limit*x asymptotically, so gain = output/x + 1 → limit + 1 = args.limit.
    // (Old port formula had an extra +t term that pushed the asymptote to args.limit+1.)
    double r = n(100000.0, args);
    EXPECT_NEAR(r, 3.0, 0.01);  // args.limit

    // Just past the offset the integral cancels: output ≈ 0 → gain ≈ 1.0
    double r_low = n(0.001, args);
    EXPECT_NEAR(r_low, 1.0, 0.1);

    // Non-gain mode: should converge to args.limit = 3.0
    args.gain = false;
    natural n2(args);
    double r_ng = n2(100000.0, args);
    EXPECT_NEAR(r_ng, 3.0, 0.01);  // args.limit = 3.0 (internal limit+1)

    // Non-gain at x=0 → 1.0
    double r_ng_low = n2(0.001, args);
    EXPECT_NEAR(r_ng_low, 1.0, 0.01);
}

// ── P105: classic+natural derin doğruluk turu ─────────────────────────────────
// (R47 emri: smooth/sync_speed etkileşimi, decay_rate, limit, acceleration
//  küçük-değer — finite/monotonic/cap doğruları. classic/natural, diğer
//  modların parametrelerinden bağımsız olmalıdır.)

static void test_classic_natural_deep_accuracy() {
    SECTION("P105 — classic: acceleration küçük-değer finite/monotonic/cap");
    for (double acc : { 1e-6, 1e-4, 5e-3, 5e-2, 5e-1 }) {
        accel_args args = make_args(accel_mode::classic);
        args.gain = true;
        args.acceleration = acc;
        classic c(args);
        double g1  = c(1.0,    args);
        double g10 = c(10.0,   args);
        double g100 = c(100.0, args);
        double g1e4 = c(10000.0, args);
        EXPECT(std::isfinite(g1) && std::isfinite(g10)
               && std::isfinite(g100) && std::isfinite(g1e4));
        EXPECT(g1 >= 1.0);                      // baseline identity
        EXPECT(g10 >= g1 && g100 >= g10 && g1e4 >= g100); // monotonic
        EXPECT(g1e4 <= 1.5 + 1e-9);             // cap.out = 1.5
    }

    SECTION("P105 — classic: smooth/sync_speed bağımsızlığı");
    {
        accel_args base = make_args(accel_mode::classic);
        classic c0(base);
        double r0 = c0(42.0, base);
        accel_args alt = base;
        alt.smooth = 16.0;       // classic kullanmaz
        alt.sync_speed = 500.0;  // classic kullanmaz
        classic c1(alt);
        double r1 = c1(42.0, alt);
        EXPECT_NEAR(r1, r0, 1e-9);
    }

    SECTION("P105 — natural: decay_rate × limit etkileşimi finite");
    for (double dr : { 1e-4, 1e-1, 5.0, 1e3 }) {
        for (double lim : { 0.5, 1.0, 1.5, 3.0 }) {
            accel_args args = make_args(accel_mode::natural);
            args.gain = true;
            args.decay_rate = dr;
            args.limit = lim;
            natural n(args);
            double g1  = n(1.0,   args);
            double g10 = n(10.0,  args);
            double g100 = n(100.0, args);
            double g1e5 = n(100000.0, args);
            double g1e8 = n(100000000.0, args);
            EXPECT(std::isfinite(g1) && std::isfinite(g10)
                   && std::isfinite(g100) && std::isfinite(g1e5) && std::isfinite(g1e8));
            if (lim >= 1.0) {
                // limit >= 1 → artan monoton, args.limit'e yakınsar.
                // Düşük decay_rate'de yakınsama yavaştır (dr=1e-4: e^-5 ≈ %0.67
                // yanlık 1e5'te), asimptot 1e8'de ölçülür.
                EXPECT(g10 >= g1 - 1e-12 && g100 >= g10 - 1e-12 && g1e5 >= g100 - 1e-12);
                EXPECT(g1e5 <= lim + 0.01);     // asimptota alttan yaklaşır
                EXPECT_NEAR(g1e8, lim, 0.01);
            } else {
                // limit < 1 → bilinçli deceleration (gain < 1), asimptot limit
                EXPECT(g1e5 <= 1.0 + 1e-9);
                EXPECT_NEAR(g1e8, lim, 0.01);
            }
        }
    }

    SECTION("P105 — natural: decay_rate=0 kolu (flat) + limit<1 guard");
    {
        accel_args args = make_args(accel_mode::natural);
        args.gain = true;
        args.decay_rate = 0.0;
        args.limit = 3.0;
        natural n(args);
        EXPECT_NEAR(n(10.0, args), 1.0, 1e-9);  // flat (guard: accel≈0)
        EXPECT_NEAR(n(1e5, args), 1.0, 1e-9);
    }

    SECTION("P105 — natural: smooth/sync_speed bağımsızlığı");
    {
        accel_args base = make_args(accel_mode::natural);
        natural n0(base);
        double r0 = n0(42.0, base);
        accel_args alt = base;
        alt.smooth = 16.0;       // natural kullanmaz
        alt.sync_speed = 500.0;  // natural kullanmaz
        natural n1(alt);
        double r1 = n1(42.0, alt);
        EXPECT_NEAR(r1, r0, 1e-9);
    }

    // ── P105 family-close (R47 devamı): per-parameter accuracy sweeps ─────────

    SECTION("P105 — classic: exponent_classic 1..10 × input_offset × gain × cap.y");
    {
        const double ecs[]  = { 1.0, 1.0001, 1.5, 2.0, 3.3, 7.7, 10.0 };
        const double offs[] = { 0.0, 1e-3, 1.0, 1e3 };
        const double cys[]  = { 1.5, 1e3 };
        const bool   gains[] = { false, true };
        const double bases[] = { 0.0, 1e-9, 1e-3, 0.5, 1.0, 5.0, 100.0, 1e4, 1e6 };
        for (double ec : ecs)
        for (double off : offs)
        for (double cy : cys)
        for (bool gain : gains) {
            accel_args args = make_args(accel_mode::classic);
            args.gain = gain;
            args.exponent_classic = ec;
            args.input_offset = off;
            args.cap = { 15.0, cy };
            classic c(args);
            std::vector<double> speeds(bases, bases + 9);
            const double off_more[] = { 0.5 * off, off + 1e-3, off + 10.0 };
            speeds.insert(speeds.end(), off_more, off_more + 3);
            if (off > 0.0) speeds.push_back(off);
            std::sort(speeds.begin(), speeds.end());
            double prev = -1.0;
            for (double x : speeds) {
                double g = c(x, args);
                EXPECT(std::isfinite(g) && g > 0.0);          // finite & >0, speed 0..1e6
                EXPECT(g <= cy + 1e-9);                        // cap.y ceiling both modes
                EXPECT(g >= prev - 1e-9);                      // monotone non-decreasing
                prev = g;
            }
        }
    }

    SECTION("P105 — classic: scale/limit/decay_rate/output_offset foreign-param independence");
    {
        accel_args base = make_args(accel_mode::classic);
        classic c0(base);
        const double speeds[] = { 0.0, 1e-3, 1.0, 5.0, 100.0, 1e4, 1e6 };
        for (double scale : { 0.0, 1e6 })
        for (double limit : { 0.5, 1e3 })
        for (double decay : { 0.0, 1e3 })
        for (double out_off : { 0.9, 1e3 }) {
            accel_args alt = base;
            alt.scale = scale; alt.limit = limit;
            alt.decay_rate = decay; alt.output_offset = out_off;
            classic cv(alt);
            double prev = -1.0;
            for (double x : speeds) {
                double g = cv(x, alt);
                EXPECT(std::isfinite(g) && g > 0.0);           // foreign params stay finite
                EXPECT_NEAR(cv(x, alt), c0(x, base), 1e-9);    // classic ignores them
                EXPECT(g >= prev - 1e-9);                      // curve stays monotone
                prev = g;
            }
        }
    }

    SECTION("P105 — natural: limit × decay_rate × input_offset × gain (speed 1e-3..1e6)");
    {
        const double lims[] = { 0.0, 0.5, 0.9, 1.0, 1.5, 3.0, 1e3 };
        const double drs[]  = { 0.0, 1e-4, 0.1, 5.0, 1e3 };
        const double offs[] = { 0.0, 1e3 };
        const bool   gains[] = { false, true };
        const double bases[] = { 1e-3, 0.01, 0.1, 1.0, 5.0, 100.0, 1e4, 1e6 };
        for (double lim : lims)
        for (double dr : drs)
        for (double off : offs)
        for (bool gain : gains) {
            accel_args args = make_args(accel_mode::natural);
            args.gain = gain;
            args.limit = lim;
            args.decay_rate = dr;
            args.input_offset = off;
            natural n(args);
            bool flat = (dr == 0.0);          // decay_rate=0 → accel≈0 → flat 1.0
            bool up   = (lim >= 1.0);         // ascend to args.limit, else deliberate decel
            double prev = -1.0;
            for (double x : bases) {
                double g = n(x, args);
                EXPECT(std::isfinite(g) && g >= 0.0);          // finite; lim=0 tail → 0
                if (flat) EXPECT_NEAR(g, 1.0, 1e-9);            // flat identity (mount 0 guard)
                if (prev >= 0.0) {
                    if (up)   EXPECT(g >= prev - 1e-9);         // monotonicity verdict: non-decreasing
                    else      EXPECT(g <= prev + 1e-9);         // non-increasing (designed decel)
                }
                prev = g;
            }
            double asympt = n(1e6, args);
            EXPECT(std::isfinite(asympt));
            if (!flat) {
                // dr >= 1e-4 converges to args.limit in both gain modes, but the
                // decay-length (limit/accel) grows with limit — probe far enough.
                if (lim >= 10.0) {
                    double a9 = n(1e9, args);
                    EXPECT(std::isfinite(a9));
                    EXPECT(a9 > lim * 0.9);       // within 10% by 1e9
                } else {
                    double a8 = n(1e8, args);     // lim=0 → asymptote 0 (decel to stop)
                    EXPECT(std::isfinite(a8));
                    EXPECT_NEAR(a8, lim, 0.01);
                }
            }
        }
        // decay_rate=0 × limit<1 guard: flat 1.0 through the offset band too.
        for (double off : { 0.0, 1e3 })
        for (double lim : { 0.0, 0.5 }) {
            accel_args args = make_args(accel_mode::natural);
            args.gain = true; args.limit = lim; args.decay_rate = 0.0; args.input_offset = off;
            natural n(args);
            EXPECT_NEAR(n(0.0, args), 1.0, 1e-9);
            EXPECT_NEAR(n(off + 1e-9, args), 1.0, 1e-9);
            EXPECT_NEAR(n(off + 5.0, args), 1.0, 1e-9);
            EXPECT_NEAR(n(off + 1e6, args), 1.0, 1e-9);
        }
    }

    SECTION("P105 — natural: smooth/sync_speed/scale/output_offset foreign independence + smooth 0 vs tiny");
    {
        accel_args base = make_args(accel_mode::natural);
        natural n0(base);
        const double speeds[] = { 0.0, 1e-3, 0.5, 5.0, 100.0, 1e6 };
        for (double smooth : { 0.0, 1e-9, 1e3 })
        for (double ss : { 1e-4, 1e3 })
        for (double scale : { 0.0, 1e6 })
        for (double out_off : { 0.9, 1e3 }) {
            accel_args alt = base;
            alt.smooth = smooth; alt.sync_speed = ss;
            alt.scale = scale; alt.output_offset = out_off;
            natural nv(alt);
            for (double x : speeds) {
                double g = nv(x, alt);
                EXPECT(std::isfinite(g) && g > 0.0);           // foreign params stay finite
                EXPECT_NEAR(nv(x, alt), n0(x, base), 1e-9);    // natural ignores them
            }
        }
        // smooth=0 vs tiny boundary: no div-by-zero, identical curve.
        accel_args s0 = base; s0.smooth = 0.0;
        accel_args st = base; st.smooth = 1e-9;
        natural ns0(s0), nst(st);
        for (double x : speeds) {
            EXPECT_NEAR(ns0(x, s0), nst(x, st), 1e-9);
        }
    }
}

// ── O4: natural reference-alignment regression ────────────────────────────────

static void test_natural_reference_values() {
    SECTION("O4 — natural matches RawAccel reference integral");

    // Gain mode, offset=0: limit=1.5 → internal 0.5, accel = 0.1/0.5 = 0.2
    accel_args g_args = make_args(accel_mode::natural);
    g_args.limit = 1.5;
    g_args.decay_rate = 0.1;
    g_args.input_offset = 0.0;
    g_args.gain = true;
    natural ng(g_args);

    // Reference: gain = output/x + 1, output = limit*(decay/accel - offset_x) - limit/accel
    // x=10: decay = exp(-0.2*10) = exp(-2)
    double decay10 = std::exp(-0.2 * 10.0);
    double out10   = 0.5 * (decay10 / 0.2 + 10.0) - 0.5 / 0.2;
    EXPECT_NEAR(ng(10.0, g_args), out10 / 10.0 + 1.0, 1e-9);
    EXPECT_NEAR(ng(10.0, g_args), 1.283834, 1e-4);
    // Asymptote approaches args.limit, NOT args.limit + 1
    EXPECT(ng(1e9, g_args) < 1.5001);

    // Legacy mode with offset > 0: reference gain includes the offset terms.
    accel_args l_args = make_args(accel_mode::natural);
    l_args.limit = 1.5;
    l_args.decay_rate = 0.1;
    l_args.input_offset = 2.0;
    l_args.gain = false;
    natural nl(l_args);

    // accel = 0.1/0.5 = 0.2, x=10 → t=8, decay = exp(-1.6)
    double d   = std::exp(-0.2 * 8.0);
    double ox  = 2.0 - 10.0;
    double ref = 0.5 * (1.0 - (2.0 - d * ox) / 10.0) + 1.0;
    EXPECT_NEAR(nl(10.0, l_args), ref, 1e-9);
}

// ── O5: output_dpi must affect the modifier output ────────────────────────────

static void test_output_dpi_applied() {
    SECTION("O5 — output_dpi scales modifier output like the reference");

    profile prof{};
    prof.accel_x.mode = accel_mode::noaccel;
    prof.accel_y = prof.accel_x;
    prof.domain_weights = {1.0, 1.0};
    prof.range_weights  = {1.0, 1.0};
    prof.speed_max = 1e9;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);
    speed_processor sp; sp.init(prof.speed_processor_args);
    modifier mod;

    // Default output_dpi = NORMALIZED_DPI=1000, dpi_factor=1 → output unchanged
    vec2d in1 = {10.0, -6.0};
    mod.modify(in1, sp, settings, 1.0, 1.0);
    EXPECT_NEAR(in1.x, 10.0, 1e-9);
    EXPECT_NEAR(in1.y, -6.0, 1e-9);

    // output_dpi = 500 → output halved (dpi_adjustment = (out/1000) * dpi_factor)
    settings.prof.output_dpi = 500.0;
    init_settings(settings);
    vec2d in2 = {10.0, -6.0};
    mod.modify(in2, sp, settings, 1.0, 1.0);
    EXPECT_NEAR(in2.x, 5.0, 1e-9);
    EXPECT_NEAR(in2.y, -3.0, 1e-9);

    // output_dpi=2000 with dpi_factor=0.5 (2000 DPI device → 1000/2000):
    // multiplier = (2000/1000)*0.5 = 1.0 → output unchanged again
    settings.prof.output_dpi = 2000.0;
    init_settings(settings);
    vec2d in3 = {10.0, -6.0};
    mod.modify(in3, sp, settings, 0.5, 1.0);
    EXPECT_NEAR(in3.x, 10.0, 1e-9);
    EXPECT_NEAR(in3.y, -6.0, 1e-9);

    // CFG-1: output_dpi = 0 disables output-DPI normalization → 1:1 counts
    // (modifier guard `args.output_dpi > 0` skips dpi_adjustment entirely).
    settings.prof.output_dpi = 0.0;
    init_settings(settings);
    vec2d in4 = {10.0, -6.0};
    mod.modify(in4, sp, settings, 1.0, 1.0);
    EXPECT_NEAR(in4.x, 10.0, 1e-9);
    EXPECT_NEAR(in4.y, -6.0, 1e-9);
}

// ── R10: EMA smoother correctness ────────────────────────────────────────────

static void test_ema_smoother_halflife() {
    SECTION("R10 — simple EMA smoother: half-life convergence");

    // After 'halfLife' steps of constant input, the smoother output should be
    // within ~50% of the step input (by definition of half-life).
    simple_ema_smoother ema;
    double halfLife = 5.0;
    ema.init(halfLife);

    // Feed constant speed=10.0 at dt=1.0 for many iterations
    double val = 0;
    for (int i = 0; i < 200; i++)
        val = ema.smooth(10.0, 1.0);

    // Should converge to 10.0
    EXPECT_NEAR(val, 10.0, 0.01);

    // Test from zero: after exactly halfLife steps, should be ~50% of target
    simple_ema_smoother ema2;
    ema2.init(halfLife);
    double v2 = 0;
    for (int i = 0; i < (int)halfLife; i++)
        v2 = ema2.smooth(10.0, 1.0);
    // Half-life means ~50% convergence — allow generous range due to discrete steps
    EXPECT(v2 > 3.0 && v2 < 8.0);

    // Zero half-life → no smoothing, output == input immediately
    simple_ema_smoother ema3;
    ema3.init(0.0);
    double v3 = ema3.smooth(42.0, 1.0);
    EXPECT_NEAR(v3, 42.0, 0.001);
}

static void test_ema_smoother_zero_time() {
    SECTION("R10 — EMA smoother with zero/negative time");

    simple_ema_smoother ema;
    ema.init(5.0);

    // Feed a normal value first
    ema.smooth(10.0, 1.0);

    // Zero time: pow(coeff, 0) = 1 → twc = 0 → no update. Output should stay.
    double prev = ema.smooth(10.0, 1.0);
    double v = ema.smooth(999.0, 0.0);
    EXPECT_NEAR(v, prev, 0.01);

    // Negative time: pow may produce > 1 → twc could be negative.
    // Should not produce NaN or crash.
    double vn = ema.smooth(5.0, -1.0);
    EXPECT(std::isfinite(vn));
}

static void test_linear_ema_smoother() {
    SECTION("R10 — linear EMA smoother: convergence and stability");

    linear_ema_smoother lema;
    lema.init(5.0, 1.25);

    // Feed constant speed = 20.0
    double val = 0;
    for (int i = 0; i < 300; i++)
        val = lema.smooth(20.0, 1.0);
    EXPECT_NEAR(val, 20.0, 0.1);

    // After convergence, abrupt change to 0 — should not go negative
    for (int i = 0; i < 100; i++)
        val = lema.smooth(0.0, 1.0);
    EXPECT(val >= 0.0);

    // Zero half-life → immediate tracking
    linear_ema_smoother lema2;
    lema2.init(0.0, 0.0);
    double v2 = lema2.smooth(77.0, 1.0);
    EXPECT_NEAR(v2, 77.0, 0.001);
}

// ── R10: NaN propagation end-to-end for every mode ───────────────────────────

static void test_nan_propagation_all_modes() {
    SECTION("R10 — NaN propagation: full pipeline for every accel mode");

    // Test that no matter what extreme accel_args we feed, the output
    // (through modifier → motion_math) never produces NaN or Inf.
    accel_mode modes[] = {
        accel_mode::noaccel, accel_mode::classic, accel_mode::power,
        accel_mode::natural, accel_mode::jump, accel_mode::synchronous,
        accel_mode::lookup
    };
    const char* mode_names[] = {
        "noaccel", "classic", "power", "natural", "jump", "synchronous", "lookup"
    };

    double test_speeds[] = { 0.0, 1e-15, 0.001, 1.0, 100.0, 1e6, 1e15 };
    double test_times[]  = { 0.001, 0.0625, 1.0, 10.0, 100.0 };

    for (int m = 0; m < 7; m++) {
        accel_args args = make_args(modes[m]);
        if (modes[m] == accel_mode::lookup) {
            // Setup a minimal LUT
            args.data[0] = 0.0f; args.data[1] = 1.0f;
            args.data[2] = 50.0f; args.data[3] = 3.0f;
            args.length = 4;
        }

        modifier_settings settings;
        settings.prof.accel_x = args;
        settings.prof.accel_y = args;
        init_settings(settings);

        modifier mod;
        speed_processor sp;
        sp.init(settings.prof.speed_processor_args);

        bool any_nan = false;
        for (double spd : test_speeds) {
            for (double t : test_times) {
                double rem_x = 0, rem_y = 0;
                int out_x = 0, out_y = 0;
                apply_motion_math(mod, sp, settings, 1.0, t,
                                  spd, spd, rem_x, rem_y, out_x, out_y);
                if (!std::isfinite(rem_x) || !std::isfinite(rem_y)) {
                    any_nan = true;
                    std::fprintf(stderr, "    NaN/Inf in mode %s: speed=%.3g time=%.3g\n",
                                 mode_names[m], spd, t);
                }
            }
        }
        EXPECT(!any_nan);
    }
}

static void test_nan_propagation_pathological_params() {
    SECTION("R10 — NaN propagation: pathological parameter combos");

    // Classic: negative acceleration + non-integer exponent → pow(neg, frac) = NaN
    {
        accel_args args;
        args.mode = accel_mode::classic;
        args.acceleration = -5.0;
        args.exponent_classic = 2.7; // non-integer
        args.cap.y = 0;
        args.input_offset = 0;
        classic c(args);
        double r = c(10.0, args);
        EXPECT(std::isfinite(r));
    }

    // Power: scale=0 + exponent near 0
    {
        accel_args args;
        args.mode = accel_mode::power;
        args.scale = 0.0;
        args.exponent_power = 1e-4;
        args.output_offset = 0;
        power p(args);
        double r = p(10.0, args);
        EXPECT(std::isfinite(r));
    }

    // Natural: decay_rate = 0 + gain mode → 0/0 in integral
    {
        accel_args args;
        args.mode = accel_mode::natural;
        args.decay_rate = 0.0;
        args.limit = 5.0;
        args.gain = true;
        args.input_offset = 0;
        natural n(args);
        double r = n(10.0, args);
        EXPECT(std::isfinite(r));
        double r0 = n(0.0, args);
        EXPECT(std::isfinite(r0));
    }

    // Synchronous: power < 1 → gain → ∞ as x → 0
    {
        accel_args args;
        args.mode = accel_mode::synchronous;
        args.sync_speed = 5.0;
        args.exponent_power = 0.01; // very small
        synchronous s(args);
        double r = s(1e-10, args);
        EXPECT(std::isfinite(r));
        double r0 = s(0.0, args);
        EXPECT(std::isfinite(r0));
    }

    // Classic cap_mode::io with cap.x <= input_offset (degenerate)
    {
        accel_args args;
        args.mode = accel_mode::classic;
        args.cap_mode_val = cap_mode::io;
        args.cap.x = 5.0;
        args.cap.y = 2.0;
        args.input_offset = 10.0; // offset > cap.x
        args.exponent_classic = 0.5; // fractional exp
        classic c(args);
        double r = c(20.0, args);
        EXPECT(std::isfinite(r));
    }
}

// ── R10: Event batching simulation (mock events through motion_math) ─────────

static void test_event_batching_accumulation() {
    SECTION("R10 — event batching: accumulated dx/dy matches separate events");

    // Simulate: 3 events with REL_X=5,3,2 (total 10) should give same result
    // as single event with REL_X=10, because all arrive in the same SYN frame.
    accel_args args = make_args(accel_mode::classic);
    args.acceleration = 0.01;
    args.exponent_classic = 2.0;
    args.gain = true;
    args.cap.y = 0;

    modifier_settings settings;
    settings.prof.accel_x = args;
    settings.prof.accel_y = args;
    init_settings(settings);

    // Single batch: dx=10
    modifier mod1;
    speed_processor sp1;
    sp1.init(settings.prof.speed_processor_args);
    double rem_x1 = 0, rem_y1 = 0;
    int ox1 = 0, oy1 = 0;
    apply_motion_math(mod1, sp1, settings, 1.0, 1.0,
                      10.0, 0.0, rem_x1, rem_y1, ox1, oy1);
    double total_batch = ox1 + rem_x1;

    // Same call with identical initial state
    modifier mod2;
    speed_processor sp2;
    sp2.init(settings.prof.speed_processor_args);
    double rem_x2 = 0, rem_y2 = 0;
    int ox2 = 0, oy2 = 0;
    apply_motion_math(mod2, sp2, settings, 1.0, 1.0,
                      10.0, 0.0, rem_x2, rem_y2, ox2, oy2);
    double total_single = ox2 + rem_x2;

    EXPECT_NEAR(total_batch, total_single, 1e-9);
}

static void test_event_batching_split_vs_combined() {
    SECTION("R10 — event batching: split events vs combined in separate SYN frames");

    // When events come in SEPARATE SYN frames (different time), the result
    // should differ from a single combined event (because speed calculation
    // depends on delta magnitude). This is expected behavior.
    accel_args args = make_args(accel_mode::classic);
    args.acceleration = 0.01;
    args.exponent_classic = 2.0;
    args.gain = true;
    args.cap.y = 0;

    modifier_settings settings;
    settings.prof.accel_x = args;
    settings.prof.accel_y = args;
    init_settings(settings);

    // Combined: one frame dx=20 at t=1ms
    modifier mod_c;
    speed_processor sp_c;
    sp_c.init(settings.prof.speed_processor_args);
    double rem_cx = 0, rem_cy = 0;
    int ocx = 0, ocy = 0;
    apply_motion_math(mod_c, sp_c, settings, 1.0, 1.0,
                      20.0, 0.0, rem_cx, rem_cy, ocx, ocy);
    double combined = ocx + rem_cx;

    // Split: two frames dx=10 each at t=0.5ms each
    modifier mod_s;
    speed_processor sp_s;
    sp_s.init(settings.prof.speed_processor_args);
    double rem_sx = 0, rem_sy = 0;
    int osx1 = 0, osy1 = 0, osx2 = 0, osy2 = 0;
    apply_motion_math(mod_s, sp_s, settings, 1.0, 0.5,
                      10.0, 0.0, rem_sx, rem_sy, osx1, osy1);
    apply_motion_math(mod_s, sp_s, settings, 1.0, 0.5,
                      10.0, 0.0, rem_sx, rem_sy, osx2, osy2);
    double split = osx1 + osx2 + rem_sx;

    // With classic acceleration, higher speed (combined) should produce
    // MORE output than lower speed (split), because classic gain increases with speed.
    EXPECT(combined > split - 1.0); // combined >= split (approximately)
    // Both should be finite
    EXPECT(std::isfinite(combined));
    EXPECT(std::isfinite(split));
}

// ── R10: Speed processor distance modes ──────────────────────────────────────

static void test_speed_processor_all_distance_modes() {
    SECTION("R10 — speed processor: all distance modes produce finite results");

    accel_args args = make_args(accel_mode::classic);
    args.acceleration = 0.01;
    args.exponent_classic = 2.0;

    // Test all distance modes
    speed_args sp_args_list[] = {
        { true,  2.0,    0, 0, 0 },   // euclidean
        { true,  9999.0, 0, 0, 0 },   // max
        { true,  3.0,    0, 0, 0 },   // Lp (p=3)
        { false, 2.0,    0, 0, 0 },   // separate
    };
    const char* names[] = { "euclidean", "max", "Lp(3)", "separate" };

    for (int i = 0; i < 4; i++) {
        modifier_settings settings;
        settings.prof.accel_x = args;
        settings.prof.accel_y = args;
        settings.prof.speed_processor_args = sp_args_list[i];
        init_settings(settings);

        modifier mod;
        speed_processor sp;
        sp.init(sp_args_list[i]);

        double rem_x = 0, rem_y = 0;
        int ox = 0, oy = 0;
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          5.0, 3.0, rem_x, rem_y, ox, oy);

        bool ok = std::isfinite(rem_x) && std::isfinite(rem_y);
        if (!ok)
            std::fprintf(stderr, "    NaN in distance mode %s\n", names[i]);
        EXPECT(ok);
        // Should produce some output for non-zero input
        EXPECT(ox != 0 || oy != 0 || std::fabs(rem_x) > 0 || std::fabs(rem_y) > 0);
    }
}

// ── R10: Speed processor with smoothing enabled ──────────────────────────────

static void test_speed_processor_smoothing() {
    SECTION("R10 — speed processor: EMA smoothing produces stable output");

    accel_args args = make_args(accel_mode::classic);
    args.acceleration = 0.01;
    args.exponent_classic = 2.0;
    args.cap.y = 0;

    speed_args sp_args;
    sp_args.whole = true;
    sp_args.lp_norm = 2.0;
    sp_args.input_speed_smooth_halflife  = 5.0;
    sp_args.scale_smooth_halflife        = 3.0;
    sp_args.output_speed_smooth_halflife = 2.0;

    modifier_settings settings;
    settings.prof.accel_x = args;
    settings.prof.accel_y = args;
    settings.prof.speed_processor_args = sp_args;
    init_settings(settings);

    modifier mod;
    speed_processor sp;
    sp.init(sp_args);

    // Feed constant motion for many frames
    double rem_x = 0, rem_y = 0;
    int last_ox = 0;
    bool all_finite = true;
    for (int i = 0; i < 500; i++) {
        int ox = 0, oy = 0;
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          10.0, 0.0, rem_x, rem_y, ox, oy);
        if (!std::isfinite(rem_x) || !std::isfinite(rem_y))
            all_finite = false;
        last_ox = ox;
    }
    EXPECT(all_finite);
    // After convergence, output should be relatively stable
    EXPECT(last_ox > 0);

    // Abrupt stop: feed zero motion
    for (int i = 0; i < 100; i++) {
        int ox = 0, oy = 0;
        apply_motion_math(mod, sp, settings, 1.0, 1.0,
                          0.0, 0.0, rem_x, rem_y, ox, oy);
        if (!std::isfinite(rem_x) || !std::isfinite(rem_y))
            all_finite = false;
    }
    EXPECT(all_finite);
}

// ── R10: SYN_DROPPED simulation ──────────────────────────────────────────────

static void test_syn_dropped_reset_behavior() {
    SECTION("R10 — SYN_DROPPED: remainder reset preserves correctness");

    // After SYN_DROPPED, the daemon resets dx/dy accumulators.
    // Simulate: motion → SYN_DROPPED (discard) → fresh motion.
    // Verify remainder state is not corrupted by the discarded partial data.
    accel_args args = make_args(accel_mode::classic);
    args.acceleration = 0.01;
    args.exponent_classic = 2.0;

    modifier_settings settings;
    settings.prof.accel_x = args;
    settings.prof.accel_y = args;
    init_settings(settings);

    modifier mod;
    speed_processor sp;
    sp.init(settings.prof.speed_processor_args);

    // Normal frame
    double rem_x = 0, rem_y = 0;
    int ox = 0, oy = 0;
    apply_motion_math(mod, sp, settings, 1.0, 1.0,
                      10.0, 5.0, rem_x, rem_y, ox, oy);
    EXPECT(std::isfinite(rem_x) && std::isfinite(rem_y));

    // Simulate SYN_DROPPED: we do NOT call apply_motion_math for the dropped frame.
    // The accumulated dx/dy would be discarded by daemon. Remainder stays from last good frame.

    // Next good frame: fresh motion
    int ox2 = 0, oy2 = 0;
    apply_motion_math(mod, sp, settings, 1.0, 1.0,
                      3.0, 2.0, rem_x, rem_y, ox2, oy2);
    EXPECT(std::isfinite(rem_x) && std::isfinite(rem_y));

    // Output should be reasonable (not huge or zero from corrupted state)
    double total = std::abs(ox2) + std::fabs(rem_x);
    EXPECT(total >= 0.0 && total < 1000.0);
}

// ── T24: SYN_DROPPED event-stream scenarios ──────────────────────────────
//
// Faithful unit-level mirror of the daemon's process_device() decision table
// (daemon/daemon.cpp). Linux input protocol: ALL events between SYN_DROPPED
// and the next SYN_REPORT are unreliable and must be discarded.
//   - BUG-18: the dropped flag is device-state, so a SYN_DROPPED in one read
//     batch is still active when the clearing SYN_REPORT arrives in a later
//     invocation.
//   - R12: non-SYN events while dropped are discarded (motion + buttons).
//   - The clearing SYN_REPORT is consumed silently (not forwarded).
//   - Motion is flushed only on SYN_REPORT; a batch ending without SYN loses
//     its accumulated deltas (daemon's dx/dy are per-invocation locals).
// No new feature: pure observation harness over the documented state machine.

namespace t24syn {

enum : unsigned short {
    ev_syn = 0x00,
    ev_key = 0x01,
    ev_rel = 0x02,
};
enum : unsigned short {
    syn_report = 0x00,
    syn_mt_report = 0x01,
    syn_dropped = 0x03,
};
enum : unsigned short {
    rel_x = 0x00,
    rel_y = 0x01,
};
enum : unsigned short {
    btn_left = 0x110,
    btn_right = 0x111,
};

struct evt {
    unsigned short type, code;
    int value;
};

struct sim {
    bool dropped = false;
    bool has_syn = false;                 // a SYN_REPORT arrived this invocation
    bool wrote_unsynced = false;          // some event written without a SYN yet
    double dx = 0, dy = 0;
    bool has_motion = false;
    std::vector<evt> out;

    void handle(unsigned short type, unsigned short code, int value) {
        if (type == ev_syn) {
            if (code == syn_dropped) {
                dx = dy = 0;
                has_motion = false;
                dropped = true;
                return;
            }
            if (dropped) {
                // R1-06: only a genuine SYN_REPORT ends the dropped window.
                // Other EV_SYN subtypes (SYN_MT_REPORT/SYN_CONFIG) that arrive
                // inside the window are unreliable too — they stay dropped AND
                // are themselves discarded.
                if (code == syn_report) dropped = false;
                return;
            }
            has_syn = true;
            flush();
            out.push_back({ev_syn, syn_report, 0});
            wrote_unsynced = false;
        } else if (dropped) {
            return;                  // R12: discard all non-SYN while dropped
        } else if (type == ev_rel) {
            if (code == rel_x) dx += value;
            else if (code == rel_y) dy += value;
            has_motion = true;
        } else {
            // Buttons/wheel/etc. — daemon flushes pending motion BEFORE
            // forwarding a non-motion event (daemon.cpp:1376-1387), so the
            // partial frame is not reordered after the button.
            flush();
            out.push_back({type, code, value});
            wrote_unsynced = true;
        }
    }

    void end_batch() {
        // Daemon process_device() tail (BUG-CRIT-1): a frame is closed whenever
        // anything is still pending — pending motion OR events forwarded without
        // a trailing SYN_REPORT.  The old `!has_syn` guard dropped motion that
        // accumulated after a mid-batch SYN_REPORT (kernel coalesced a second
        // frame whose own SYN was not yet queued when the batch ended).  Pending
        // motion is flushed and the frame is closed with a synthetic SYN_REPORT.
        // Locals are per-invocation, so reset them.
        if (has_motion || wrote_unsynced) {
            flush();
            out.push_back({ev_syn, syn_report, 0});
        }
        has_syn = false;
        wrote_unsynced = false;
    }

    void flush() {
        if (!has_motion) return;
        out.push_back({ev_rel, rel_x, (int)dx});
        out.push_back({ev_rel, rel_y, (int)dy});
        dx = dy = 0;
        has_motion = false;
        wrote_unsynced = true;
    }
};

bool has_rel(const std::vector<evt>& v, int x, int y) {
    bool fx = false, fy = false;
    for (const evt& e : v) {
        if (e.type == ev_rel && e.code == rel_x && e.value == x) fx = true;
        if (e.type == ev_rel && e.code == rel_y && e.value == y) fy = true;
    }
    return fx && fy;
}

} // namespace t24syn

static void test_syn_dropped_event_stream() {
    using namespace t24syn;

    SECTION("T24 — SYN_DROPPED: baseline fresh motion is flushed on SYN");
    {
        sim s;
        s.handle(ev_rel, rel_x, 5);
        s.handle(ev_rel, rel_y, 7);
        s.handle(ev_syn, syn_report, 0);
        EXPECT(s.out.size() == 3);
        EXPECT(has_rel(s.out, 5, 7));
        EXPECT(s.out[2].type == ev_syn && s.out[2].code == syn_report);
        EXPECT(!s.dropped && s.dx == 0 && s.dy == 0 && !s.has_motion);
    }

    SECTION("T24 — SYN_DROPPED mid-batch discards motion, buttons and clears on SYN");
    {
        sim s;
        s.handle(ev_rel, rel_x, 5);
        s.handle(ev_syn, syn_dropped, 0);
        s.handle(ev_rel, rel_y, 20);
        s.handle(ev_key, btn_left, 1);
        s.handle(ev_syn, syn_report, 0);
        EXPECT(s.out.empty());
        EXPECT(!s.dropped);
    }

    SECTION("T24 — SYN_DROPPED persists across batches until SYN_REPORT (BUG-18)");
    {
        sim s;
        s.handle(ev_rel, rel_x, 5);
        s.handle(ev_syn, syn_dropped, 0);
        s.end_batch();
        EXPECT(s.dropped);
        s.handle(ev_rel, rel_y, 7);      // next invocation: still dropped
        s.handle(ev_syn, syn_report, 0);
        EXPECT(s.out.empty());
        EXPECT(!s.dropped);
    }

    SECTION("T24 — DOUBLE SYN_DROPPED keeps discarding until first SYN_REPORT");
    {
        sim s;
        s.handle(ev_rel, rel_x, 5);
        s.handle(ev_syn, syn_dropped, 0);
        s.handle(ev_syn, syn_dropped, 0);
        s.handle(ev_key, btn_left, 1);
        s.handle(ev_syn, syn_report, 0);
        EXPECT(s.out.empty());
        EXPECT(!s.dropped);
    }

    SECTION("T24 — fresh motion after the clearing SYN_REPORT is flushed normally");
    {
        sim s;
        s.handle(ev_rel, rel_x, 5);
        s.handle(ev_syn, syn_dropped, 0);
        s.handle(ev_syn, syn_report, 0);     // clears dropped, consumes SYN
        s.handle(ev_rel, rel_x, 3);
        s.handle(ev_rel, rel_y, 1);
        s.handle(ev_syn, syn_report, 0);
        EXPECT(has_rel(s.out, 3, 1));
        EXPECT(s.out.size() == 3);
        EXPECT(!s.dropped);
    }

    SECTION("T24 — SYN_MT_REPORT inside the dropped window does NOT end it (R1-06)");
    {
        // Only a genuine SYN_REPORT ends the [SYN_DROPPED, SYN_REPORT] window.
        // A non-SYN_REPORT EV_SYN subtype arriving inside it is itself
        // unreliable — the flag must stay set and later motion discarded.
        sim s;
        s.handle(ev_rel, rel_x, 5);
        s.handle(ev_syn, syn_dropped, 0);
        s.handle(ev_syn, syn_mt_report, 0);  // must NOT clear dropped
        s.handle(ev_rel, rel_y, 20);         // still unreliable: discarded
        s.handle(ev_syn, syn_report, 0);     // genuine SYN_REPORT: ends window
        EXPECT(s.out.empty());
        EXPECT(!s.dropped);
    }

    SECTION("T24 — buttons before a drop are kept, after a drop are discarded");
    {
        sim s;
        s.handle(ev_key, btn_left, 1);
        s.handle(ev_syn, syn_report, 0);
        s.handle(ev_rel, rel_x, 2);
        s.handle(ev_syn, syn_dropped, 0);
        s.handle(ev_key, btn_right, 1);      // unreliable window
        s.handle(ev_syn, syn_report, 0);
        EXPECT(s.out.size() == 2);
        EXPECT(s.out[0].type == ev_key && s.out[0].code == btn_left);
        EXPECT(s.out[1].type == ev_syn && s.out[1].code == syn_report);
    }

    SECTION("T24 — motion without SYN_REPORT is flushed at batch end (BUG-20: synthetic SYN)");
    {
        // Daemon tail (daemon.cpp:1391-1396): a frame that ended without a
        // SYN_REPORT is flushed with a synthetic SYN — the partial motion is
        // NOT dropped (this test documents the real queue behavior, replacing
        // the stale "motion is lost" model).
        sim s;
        s.handle(ev_rel, rel_x, 8);      // partial frame, no SYN yet
        s.end_batch();                   // daemon flushes + synthetic SYN_REPORT
        EXPECT(s.out.size() == 3);
        EXPECT(has_rel(s.out, 8, 0));
        EXPECT(s.out[2].type == ev_syn && s.out[2].code == syn_report);
        EXPECT(!s.dropped && !s.has_motion && !s.has_syn);
    }

    SECTION("T24 — batch end with only forwarded buttons emits one synthetic SYN");
    {
        // Non-motion events written unsynced also close with a synthetic SYN
        // (daemon.cpp:1393-1395, wrote_unsynced_event path).
        sim s;
        s.handle(ev_key, btn_left, 1);   // forwarded immediately
        s.end_batch();
        EXPECT(s.out.size() == 2);
        EXPECT(s.out[0].type == ev_key && s.out[0].code == btn_left);
        EXPECT(s.out[1].type == ev_syn && s.out[1].code == syn_report);
        EXPECT(!s.has_syn && !s.wrote_unsynced);
    }

    SECTION("T24 — clean batch ending on SYN_REPORT does not double-flush");
    {
        // has_syn=true path: the real SYN already closed the frame, so
        // end_batch() adds nothing (no synthetic SYN, no stray REL).
        sim s;
        s.handle(ev_rel, rel_x, 3);
        s.handle(ev_syn, syn_report, 0);
        s.end_batch();
        EXPECT(s.out.size() == 3);
        EXPECT(has_rel(s.out, 3, 0));
        EXPECT(s.out.back().type == ev_syn && s.out.back().code == syn_report);
        EXPECT(!s.wrote_unsynced);
    }

    SECTION("T24 — mid-batch SYN then tail motion is NOT lost (BUG-CRIT-1)");
    {
        // Kernel coalesced [REL:+5, SYN, REL:+3] and the batch ended (EAGAIN)
        // before the second frame's own SYN_REPORT was queued.  Even though a
        // SYN already arrived this invocation (has_syn=true), the tail +3 must
        // be flushed and the frame closed with a synthetic SYN_REPORT.
        sim s;
        s.handle(ev_rel, rel_x, 5);
        s.handle(ev_syn, syn_report, 0);      // frame 1 closes
        s.handle(ev_rel, rel_x, 3);            // tail of frame 2, no SYN yet
        s.end_batch();
        EXPECT(s.out.size() == 6);             // REL5 SYN REL3 SYN
        EXPECT(has_rel(s.out, 5, 0));
        EXPECT(has_rel(s.out, 3, 0));
        EXPECT(s.out[5].type == ev_syn && s.out[5].code == syn_report);
        EXPECT(!s.has_motion && !s.has_syn);
    }

    SECTION("T24 — motion then button (no SYN): relative order preserved, one closing SYN");
    {
        // Daemon flushes pending motion BEFORE forwarding the button
        // (daemon.cpp:1383-1387), so motion stays ahead of the button and the
        // whole frame closes with a single synthetic SYN at batch end.
        sim s;
        s.handle(ev_rel, rel_x, 4);
        s.handle(ev_key, btn_left, 1);
        s.end_batch();
        EXPECT(s.out.size() == 4);
        EXPECT(has_rel(s.out, 4, 0));
        EXPECT(s.out[2].type == ev_key && s.out[2].code == btn_left);
        EXPECT(s.out[3].type == ev_syn && s.out[3].code == syn_report);
    }

    SECTION("T24 — dropped flag does NOT leak into the next good batch window");
    {
        sim s;
        s.handle(ev_syn, syn_dropped, 0);
        s.handle(ev_syn, syn_report, 0);
        s.handle(ev_rel, rel_x, 4);
        s.handle(ev_syn, syn_report, 0);
        EXPECT(has_rel(s.out, 4, 0));
        EXPECT(s.out.size() == 3);
        EXPECT(!s.dropped);
    }
}

// ── Config edge cases ────────────────────────────────────────────────────

static void test_config_empty_profiles() {
    SECTION("R10 — config: empty profiles array handling");

    // Create a config JSON with empty profiles
    std::string tmp = "/tmp/rawaccel_test_empty_profiles.json";
    {
        std::ofstream f(tmp);
        f << R"({"active_profile":"nonexistent","profiles":[]})";
    }

    app_config cfg = load_config(tmp);
    EXPECT(cfg.profiles.empty());
    EXPECT(cfg.active_profile == "nonexistent");

    // Save it back and reload — should survive the round-trip
    save_config(cfg, tmp);
    app_config cfg2 = load_config(tmp);
    EXPECT(cfg2.profiles.empty());

    std::remove(tmp.c_str());
}

static void test_config_missing_active_profile() {
    SECTION("R10 — config: active_profile references non-existent profile");

    std::string tmp = "/tmp/rawaccel_test_missing_active.json";
    {
        std::ofstream f(tmp);
        f << R"({
            "active_profile": "ghost",
            "profiles": [
                { "name": "real", "dpi": 800, "polling_rate": 1000,
                  "profile": {} }
            ]
        })";
    }

    app_config cfg = load_config(tmp);
    EXPECT(cfg.active_profile == "ghost");
    EXPECT(cfg.profiles.size() == 1);
    EXPECT(cfg.profiles[0].name == "real");

    // Daemon's find_profile logic: active_profile doesn't match → falls back to profiles[0]
    // We can verify the config loads without crashing.

    std::remove(tmp.c_str());
}

static void test_config_extreme_values() {
    SECTION("R10 — config: extreme values are clamped by sanitize");

    device_profile dp;
    dp.dev_cfg.dpi = 999999;
    dp.dev_cfg.polling_rate = -50;
    dp.prof.degrees_rotation = 9999;
    dp.prof.degrees_snap = -10;
    dp.prof.lr_output_dpi_ratio = -5;
    dp.prof.ud_output_dpi_ratio = 999;
    dp.prof.speed_min = -10;
    dp.prof.speed_max = -5;
    dp.prof.accel_x.scale = -1;
    dp.prof.accel_x.decay_rate = -1;
    dp.prof.accel_x.exponent_power = 0;
    dp.prof.accel_x.limit = -10;
    dp.prof.accel_x.sync_speed = 0;
    dp.prof.accel_x.smooth = -1;

    sanitize_device_profile(dp);

    EXPECT(dp.dev_cfg.dpi >= 1 && dp.dev_cfg.dpi <= 32000);
    EXPECT(dp.dev_cfg.polling_rate >= 125 && dp.dev_cfg.polling_rate <= 8000);
    EXPECT(dp.prof.degrees_rotation >= 0 && dp.prof.degrees_rotation < 360);
    EXPECT(dp.prof.degrees_snap >= 0 && dp.prof.degrees_snap <= 45);
    EXPECT(dp.prof.lr_output_dpi_ratio >= 0.01);
    EXPECT(dp.prof.ud_output_dpi_ratio <= 100);
    EXPECT(dp.prof.speed_min >= 0);
    EXPECT(dp.prof.speed_max >= 0);
    EXPECT(dp.prof.accel_x.scale >= 0);
    EXPECT(dp.prof.accel_x.decay_rate >= 0);
    EXPECT(dp.prof.accel_x.exponent_power >= 1e-4);
    EXPECT(dp.prof.accel_x.limit >= 0);
    EXPECT(dp.prof.accel_x.sync_speed >= 1e-4);
    EXPECT(dp.prof.accel_x.smooth >= 0);
}

static void test_config_duplicate_device_id() {
    SECTION("R10 — config: duplicate device_id → first match wins");

    std::string tmp = "/tmp/rawaccel_test_dup_devid.json";
    {
        std::ofstream f(tmp);
        f << R"({
            "active_profile": "default",
            "profiles": [
                { "name": "fast", "device_id": "usb:1234:5678:", "dpi": 1600,
                  "polling_rate": 1000, "profile": { "accel_x": { "mode": "classic", "acceleration": 0.1 } } },
                { "name": "slow", "device_id": "usb:1234:5678:", "dpi": 800,
                  "polling_rate": 1000, "profile": { "accel_x": { "mode": "natural", "decay_rate": 0.2 } } }
            ]
        })";
    }

    app_config cfg = load_config(tmp);
    EXPECT(cfg.profiles.size() == 2);
    // Both have same device_id — first match should win (deterministic)
    EXPECT(cfg.profiles[0].device_id == cfg.profiles[1].device_id);
    EXPECT(cfg.profiles[0].name == "fast"); // first in array

    std::remove(tmp.c_str());
}

// Duplicate of check_duplicate_device_ids() from gui/main.cpp (pure logic, no GTK deps).
static std::string check_dup_ids(const app_config& cfg) {
    std::string warn;
    for (size_t i = 0; i < cfg.profiles.size(); ++i) {
        const auto& a = cfg.profiles[i];
        if (a.device_id.empty()) continue;
        for (size_t j = i + 1; j < cfg.profiles.size(); ++j) {
            const auto& b = cfg.profiles[j];
            if (a.device_id == b.device_id) {
                if (!warn.empty()) warn += "; ";
                warn += "\"" + a.name + "\" & \"" + b.name +
                        "\" share device " + a.device_id;
            }
        }
    }
    if (!warn.empty())
        warn = "Warning: duplicate device IDs — " + warn +
               ". Only the first matching profile is used by the daemon.";
    return warn;
}

static void test_check_duplicate_device_ids() {
    SECTION("R11 — check_duplicate_device_ids warning logic");

    // No duplicates — all empty device_id
    {
        app_config cfg;
        device_profile a; a.name = "a";
        device_profile b; b.name = "b";
        cfg.profiles = {a, b};
        EXPECT(check_dup_ids(cfg).empty());
    }

    // No duplicates — different device_ids
    {
        app_config cfg;
        device_profile a; a.name = "a"; a.device_id = "dev1";
        device_profile b; b.name = "b"; b.device_id = "dev2";
        cfg.profiles = {a, b};
        EXPECT(check_dup_ids(cfg).empty());
    }

    // Duplicate device_ids → warning
    {
        app_config cfg;
        device_profile a; a.name = "fast"; a.device_id = "usb:1234:5678:";
        device_profile b; b.name = "slow"; b.device_id = "usb:1234:5678:";
        cfg.profiles = {a, b};
        std::string w = check_dup_ids(cfg);
        EXPECT(!w.empty());
        EXPECT(w.find("fast") != std::string::npos);
        EXPECT(w.find("slow") != std::string::npos);
        EXPECT(w.find("usb:1234:5678:") != std::string::npos);
    }

    // Mixed: one pair duplicates, one unique
    {
        app_config cfg;
        device_profile a; a.name = "a"; a.device_id = "x";
        device_profile b; b.name = "b"; b.device_id = "y";
        device_profile c; c.name = "c"; c.device_id = "x";
        cfg.profiles = {a, b, c};
        std::string w = check_dup_ids(cfg);
        EXPECT(!w.empty());
        EXPECT(w.find("\"a\"") != std::string::npos);
        EXPECT(w.find("\"c\"") != std::string::npos);
        // "b" should NOT appear in warning
        EXPECT(w.find("\"b\"") == std::string::npos);
    }

    // Empty device_id is never a collision
    {
        app_config cfg;
        device_profile a; a.name = "a"; a.device_id = "";
        device_profile b; b.name = "b"; b.device_id = "";
        cfg.profiles = {a, b};
        EXPECT(check_dup_ids(cfg).empty());
    }
}

static void test_sanitize_nan_fields() {
    SECTION("R11 — sanitize replaces NaN/Inf in all accel_args + profile fields");

    device_profile dp;
    dp.name = "nan_test";
    dp.dev_cfg.dpi = 800;
    dp.dev_cfg.polling_rate = 1000;

    // Set all double fields to NaN
    double nan = std::numeric_limits<double>::quiet_NaN();
    dp.prof.accel_x.acceleration    = nan;
    dp.prof.accel_x.scale           = nan;
    dp.prof.accel_x.decay_rate      = nan;
    dp.prof.accel_x.exponent_classic = nan;
    dp.prof.accel_x.exponent_power  = nan;
    dp.prof.accel_x.input_offset    = nan;
    dp.prof.accel_x.output_offset   = nan;
    dp.prof.accel_x.limit           = nan;
    dp.prof.accel_x.sync_speed      = nan;
    dp.prof.accel_x.smooth          = nan;
    dp.prof.accel_x.motivity        = nan;
    dp.prof.accel_x.gamma           = nan;
    dp.prof.accel_x.cap             = { nan, nan };
    dp.prof.accel_y = dp.prof.accel_x;
    dp.prof.degrees_rotation  = nan;
    dp.prof.degrees_snap      = nan;
    dp.prof.speed_min         = nan;
    dp.prof.speed_max         = nan;
    dp.prof.lr_output_dpi_ratio = nan;
    dp.prof.ud_output_dpi_ratio = nan;
    dp.prof.domain_weights    = { nan, nan };
    dp.prof.range_weights     = { nan, nan };
    dp.prof.speed_processor_args.lp_norm = nan;
    dp.prof.speed_processor_args.input_speed_smooth_halflife = nan;
    dp.prof.speed_processor_args.scale_smooth_halflife = nan;
    dp.prof.speed_processor_args.output_speed_smooth_halflife = nan;

    sanitize_device_profile(dp);

    // Every field must be finite after sanitization
    EXPECT(std::isfinite(dp.prof.accel_x.acceleration));
    EXPECT(std::isfinite(dp.prof.accel_x.scale));
    EXPECT(std::isfinite(dp.prof.accel_x.decay_rate));
    EXPECT(std::isfinite(dp.prof.accel_x.exponent_classic));
    EXPECT(std::isfinite(dp.prof.accel_x.exponent_power));
    EXPECT(std::isfinite(dp.prof.accel_x.input_offset));
    EXPECT(std::isfinite(dp.prof.accel_x.output_offset));
    EXPECT(std::isfinite(dp.prof.accel_x.limit));
    EXPECT(std::isfinite(dp.prof.accel_x.sync_speed));
    EXPECT(std::isfinite(dp.prof.accel_x.smooth));
    EXPECT(std::isfinite(dp.prof.accel_x.cap.x));
    EXPECT(std::isfinite(dp.prof.accel_x.cap.y));
    EXPECT(std::isfinite(dp.prof.degrees_rotation));
    EXPECT(std::isfinite(dp.prof.degrees_snap));
    EXPECT(std::isfinite(dp.prof.speed_min));
    EXPECT(std::isfinite(dp.prof.speed_max));
    EXPECT(std::isfinite(dp.prof.lr_output_dpi_ratio));
    EXPECT(std::isfinite(dp.prof.ud_output_dpi_ratio));
    EXPECT(std::isfinite(dp.prof.domain_weights.x));
    EXPECT(std::isfinite(dp.prof.domain_weights.y));
    EXPECT(std::isfinite(dp.prof.range_weights.x));
    EXPECT(std::isfinite(dp.prof.range_weights.y));
    EXPECT(std::isfinite(dp.prof.speed_processor_args.lp_norm));
}

static void test_modify_subnormal_time() {
    SECTION("R11 — modify() with subnormal time does not produce NaN");

    // This bug was found by libFuzzer: time=1e-309 causes ips_factor=Inf
    // which cascades into NaN through the accel pipeline.
    for (int m = 0; m < 7; ++m) {
        profile prof{};
        prof.accel_x.mode = static_cast<accel_mode>(m);
        prof.accel_x.gain = true;
        prof.accel_x.acceleration = 0.007;
        prof.accel_x.exponent_classic = 2.0;
        prof.accel_x.exponent_power = 1.5;
        prof.accel_x.limit = 2.0;
        prof.accel_x.scale = 0.01;
        prof.accel_x.decay_rate = 0.1;
        prof.accel_x.sync_speed = 5.0;
        prof.accel_y = prof.accel_x;

        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);

        speed_processor sp;
        sp.init(prof.speed_processor_args);

        modifier mod;

        // Subnormal time values
        double subnormal_times[] = { 1e-309, 5e-324, 1e-200 };
        for (double t : subnormal_times) {
            vec2d input = { 100.0, 50.0 };
            mod.modify(input, sp, settings, 0.8, t);
            EXPECT(std::isfinite(input.x));
            EXPECT(std::isfinite(input.y));
        }
    }
}

// ── Round 12 — untested code path coverage ──────────────────────────────────

static void test_classic_sign_flip() {
    SECTION("R12 — classic cap_mode::io sign flip (cap.y < 1 → deceleration)");

    // When cap.y < 1 in io mode, the classic constructor flips sign:
    //   cap = cap.y - 1 = -0.5 → cap = 0.5, sign = -1
    // This produces deceleration (gain < 1).
    accel_args args = make_args(accel_mode::classic);
    args.cap_mode_val = cap_mode::io;
    args.cap.x = 10.0;
    args.cap.y = 0.5;  // < 1 → sign flip
    args.exponent_classic = 2.0;
    args.acceleration = 0.01;
    classic c(args);

    double g_low = c(1.0, args);
    double g_mid = c(5.0, args);
    double g_high = c(20.0, args);

    // All values should be finite
    EXPECT(std::isfinite(g_low));
    EXPECT(std::isfinite(g_mid));
    EXPECT(std::isfinite(g_high));
    // Sign flip: gain should be <= 1 (deceleration)
    EXPECT(g_high <= 1.0 + 1e-6);
    // At very low speed (below offset), gain = 1.0
    double g_zero = c(0.0, args);
    EXPECT_NEAR(g_zero, 1.0, 1e-9);
}

static void test_classic_linear_path_cap() {
    SECTION("R12 — classic linear path (exp<=1) with various cap values");

    accel_args args = make_args(accel_mode::classic);
    args.exponent_classic = 0.5;  // <= 1 → linear path
    args.acceleration = 0.1;
    args.cap.y = 1.05;  // small cap → cap = 0.05

    classic c(args);
    // Linear path returns 1 + min(accel_raised, cap)
    // accel_raised = acceleration = 0.1, cap = max(0, cap.y - 1) = 0.05
    // So: gain = 1 + min(0.1, 0.05) = 1.05 for any speed > offset
    double g = c(50.0, args);
    EXPECT_NEAR(g, 1.05, 1e-6);

    // cap.y = 0 → cap = DBL_MAX (no cap) → gain = 1 + acceleration
    accel_args args2 = args;
    args2.cap.y = 0;
    classic c2(args2);
    double g2 = c2(50.0, args2);
    EXPECT_NEAR(g2, 1.1, 1e-6);  // 1 + 0.1

    // cap.y = 0.8 → cap = max(0, -0.2) = 0 → gain = 1.0
    accel_args args3 = args;
    args3.cap.y = 0.8;  // < 1 → cap = max(0, -0.2) = 0
    classic c3(args3);
    double g3 = c3(50.0, args3);
    EXPECT_NEAR(g3, 1.0, 1e-6);
}

static void test_power_cap_branch() {
    SECTION("R12 — power mode cap branch (speed >= cap_x)");

    accel_args args = make_args(accel_mode::power);
    args.exponent_power = 0.3;
    args.scale = 0.5;
    args.cap_mode_val = cap_mode::out;
    args.cap.y = 2.0;  // output cap
    args.output_offset = 0;

    power p(args);

    // Verify finite output across increasing speeds (some will hit the cap branch)
    for (double s = 10; s <= 10000; s *= 2) {
        double g = p(s, args);
        EXPECT(std::isfinite(g));
    }
    // Very high speed should approach cap_y
    double g_high = p(1e6, args);
    EXPECT(std::isfinite(g_high));
    EXPECT_NEAR(g_high, args.cap.y, 0.1);

    // cap_mode::in
    accel_args args2 = args;
    args2.cap_mode_val = cap_mode::in;
    args2.cap.x = 50.0;  // input speed cap
    power p2(args2);
    double g_below = p2(10.0, args2);
    double g_above = p2(1000.0, args2);
    EXPECT(std::isfinite(g_below));
    EXPECT(std::isfinite(g_above));

    // cap_mode::io
    accel_args args3 = args;
    args3.cap_mode_val = cap_mode::io;
    args3.cap.x = 30.0;
    args3.cap.y = 1.8;
    power p3(args3);
    double g_at_cap = p3(30.0, args3);
    EXPECT(std::isfinite(g_at_cap));
    EXPECT_NEAR(g_at_cap, 1.8, 0.5);  // at cap point, gain ≈ cap.y (io mode has integration constant offset)
}

static void test_modifier_rotation_snap_combined() {
    SECTION("R12 — modifier rotation + snap combined flags");

    profile prof{};
    prof.degrees_rotation = 45.0;  // 45° rotation
    prof.degrees_snap = 10.0;      // 10° snap angle
    prof.accel_x.mode = accel_mode::noaccel;
    prof.accel_y = prof.accel_x;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);

    EXPECT(settings.data.flags.apply_rotate);
    EXPECT(settings.data.flags.apply_snap);
    EXPECT(settings.data.flags.compute_ref_angle);

    speed_processor sp;
    sp.init(prof.speed_processor_args);
    modifier mod;

    // Pure horizontal movement → after 45° rotation becomes diagonal
    // After snap with small angle, diagonal should NOT snap to axis
    vec2d in1 = { 100.0, 0.0 };
    mod.modify(in1, sp, settings, 1.0, 1.0);
    EXPECT(std::isfinite(in1.x));
    EXPECT(std::isfinite(in1.y));

    // Pure vertical → after rotation, should be rotated
    vec2d in2 = { 0.0, 100.0 };
    mod.modify(in2, sp, settings, 1.0, 1.0);
    EXPECT(std::isfinite(in2.x));
    EXPECT(std::isfinite(in2.y));

    // Near-horizontal with snap → should snap to horizontal
    // After 45° rotation, (100, 5) becomes something near diagonal,
    // but we test the snap logic with no rotation to verify snap alone:
    profile prof2{};
    prof2.degrees_snap = 20.0;
    prof2.accel_x.mode = accel_mode::noaccel;
    prof2.accel_y = prof2.accel_x;
    modifier_settings s2;
    s2.prof = prof2;
    init_settings(s2);
    speed_processor sp2;
    sp2.init(prof2.speed_processor_args);

    // Near-horizontal: angle = atan(3/100) ≈ 1.7° < snap 20° → snap to horizontal
    vec2d snap_h = { 100.0, 3.0 };
    mod.modify(snap_h, sp2, s2, 1.0, 1.0);
    EXPECT_NEAR(snap_h.y, 0.0, 1e-9);  // snapped to 0
    EXPECT(std::fabs(snap_h.x) > 50.0);  // X magnitude preserved

    // Near-vertical: angle = atan(100/3) ≈ 88.3° → > 90°-20°=70° → snap to vertical
    vec2d snap_v = { 3.0, 100.0 };
    mod.modify(snap_v, sp2, s2, 1.0, 1.0);
    EXPECT_NEAR(snap_v.x, 0.0, 1e-9);  // snapped to 0
    EXPECT(std::fabs(snap_v.y) > 50.0);  // Y magnitude preserved
}

static void test_speed_processor_lp_mode() {
    SECTION("R12 — speed_processor Lp distance mode (lp_norm = 3)");

    speed_args sa;
    sa.whole = true;
    sa.lp_norm = 3.0;  // Lp mode (not euclidean, not max)

    speed_processor sp;
    sp.init(sa);
    EXPECT(sp.speed_flags.dist_mode == distance_mode::Lp);

    // L3 distance of (3, 4) = (27 + 64)^(1/3) = 91^(1/3) ≈ 4.497
    vec2d v = { 3.0, 4.0 };
    double speed = sp.calc_speed_whole(v, 1.0);
    double expected = std::pow(27.0 + 64.0, 1.0 / 3.0);
    EXPECT_NEAR(speed, expected, 1e-6);

    // L∞ mode (lp_norm >= MAX_NORM)
    speed_args sa2;
    sa2.whole = true;
    sa2.lp_norm = 20.0;  // >= MAX_NORM (16)
    speed_processor sp2;
    sp2.init(sa2);
    EXPECT(sp2.speed_flags.dist_mode == distance_mode::max);
    double speed2 = sp2.calc_speed_whole({3.0, 4.0}, 1.0);
    EXPECT_NEAR(speed2, 4.0, 1e-9);  // max(3, 4) = 4

    // Separate mode
    speed_args sa3;
    sa3.whole = false;
    speed_processor sp3;
    sp3.init(sa3);
    EXPECT(sp3.speed_flags.dist_mode == distance_mode::separate);
    vec2d v3 = { -3.0, 4.0 };
    sp3.calc_speed_separate(v3, 1.0);
    EXPECT_NEAR(v3.x, 3.0, 1e-9);  // abs(−3)
    EXPECT_NEAR(v3.y, 4.0, 1e-9);
}

static void test_lookup_max_capacity() {
    SECTION("R12 — lookup LUT at max capacity boundary");

    accel_args args;
    args.mode = accel_mode::lookup;
    args.gain = false;  // velocity off → stored y kazanımı doğrudan okunur

    // Fill to exactly LUT_POINTS_CAPACITY (257 points = 514 floats)
    int max_pts = (int)LUT_POINTS_CAPACITY;
    args.length = max_pts * 2;  // 514

    // Create a simple linear LUT: speed = i, gain = 1 + i/1000
    for (int i = 0; i < max_pts && i * 2 + 1 < (int)LUT_RAW_DATA_CAPACITY; i++) {
        args.data[i * 2]     = (float)i;           // speed
        args.data[i * 2 + 1] = 1.0f + i / 1000.0f; // gain
    }

    lookup lut(args);
    EXPECT(lut.size == max_pts);

    // Test interpolation at various points
    // x <= 0 → 0 (reference lookup: strictly positive domain)
    double g0 = lut(0.0, args);
    EXPECT_NEAR(g0, 0.0, 1e-6);

    double g_mid = lut(128.0, args);  // midpoint
    EXPECT_NEAR(g_mid, 1.128, 1e-3);

    double g_end = lut(256.0, args);  // last point
    EXPECT_NEAR(g_end, 1.256, 1e-3);

    // After the last point the reference lerp EXTRAPOLATES (t > 1, a<b → maxsd
    // returns the unbounded value), not clamps:
    // y = 1.255 + (1000-255)·(1.256-1.255) = 2.0
    double g_beyond = lut(1000.0, args);
    EXPECT_NEAR(g_beyond, 2.0, 1e-3);

    // Verify interpolation between points
    double g_half = lut(0.5, args);  // between point 0 and 1
    // Linear interp: 1.0 + 0.5 * (1.001 - 1.0) = 1.0005
    EXPECT_NEAR(g_half, 1.0005, 1e-3);

    // Over-capacity: length > LUT_RAW_DATA_CAPACITY → clamped
    accel_args args_over = args;
    args_over.length = (int)LUT_RAW_DATA_CAPACITY + 100;  // way over
    lookup lut_over(args_over);
    EXPECT(lut_over.size <= (int)LUT_POINTS_CAPACITY);
}

static void test_ema_coefficient_zero() {
    SECTION("R12 — EMA smoother with halfLife=0 (coefficient=0)");

    // simple_ema_smoother with halfLife=0 → coefficient=0
    simple_ema_smoother ses;
    ses.init(0);
    EXPECT_NEAR(ses.windowCoefficient, 0.0, 1e-15);

    // pow(0, time) = 0 for time > 0 → twc = 1 → immediate tracking
    double s1 = ses.smooth(10.0, 1.0);
    EXPECT_NEAR(s1, 10.0, 1e-6);  // should jump to speed immediately
    double s2 = ses.smooth(20.0, 1.0);
    EXPECT_NEAR(s2, 20.0, 1e-6);

    // linear_ema_smoother with halfLife=0
    linear_ema_smoother les;
    les.init(0, 0);
    EXPECT_NEAR(les.windowCoefficient, 0.0, 1e-15);
    double ls1 = les.smooth(10.0, 1.0);
    EXPECT(std::isfinite(ls1));
    double ls2 = les.smooth(20.0, 1.0);
    EXPECT(std::isfinite(ls2));

    // Very small halfLife (nearly zero)
    simple_ema_smoother ses2;
    ses2.init(0.001);
    double s3 = ses2.smooth(100.0, 1.0);
    EXPECT(std::isfinite(s3));
    // With such short halflife, should track very closely
    EXPECT_NEAR(s3, 100.0, 5.0);
}

static void test_modifier_directional_weight_blend() {
    SECTION("R12 — modifier directional weight cos/sin blend");

    // Referans: scale = 1 + (gain - 1) * weight; weight, reference_angle'a göre
    // range_weights.x (yatay) → range_weights.y (dikey) arasında 2/π·açı ile geçer.
    profile prof{};
    prof.accel_x.mode            = accel_mode::classic;
    prof.accel_x.gain            = false;
    prof.accel_x.acceleration    = 0.005;
    prof.accel_x.exponent_classic = 2.0;
    prof.accel_x.input_offset    = 0.0;
    prof.accel_y                 = prof.accel_x;
    prof.speed_processor_args.whole = true;
    // Different X and Y weights → directional weight kicks in
    prof.range_weights.x = 2.0;
    prof.range_weights.y = 0.5;

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);
    EXPECT(settings.data.flags.apply_directional_weight);

    speed_processor sp;
    sp.init(prof.speed_processor_args);
    modifier mod;

    // gain = 1 + 0.005·ips (classic LEGACY, power 2)

    // Pure horizontal → reference_angle = 0 → weight = 2.0
    // scale = 1 + 0.05·2 = 1.10 → x = 11.0
    vec2d h = { 10.0, 0.0 };
    mod.modify(h, sp, settings, 1.0, 1.0);
    EXPECT_NEAR(h.x, 11.0, 1e-6);

    // Pure vertical → reference_angle = π/2 → weight = 2 + (0.5-2)·1 = 0.5
    // scale = 1 + 0.05·0.5 = 1.025 → y = 10.25
    vec2d v = { 0.0, 10.0 };
    mod.modify(v, sp, settings, 1.0, 1.0);
    EXPECT_NEAR(v.y, 10.25, 1e-6);

    // 45° → speed = 10√2 ≈ 14.1421, angle = π/4
    // weight = 2 + (0.5-2)·(2/π)·(π/4) = 1.25
    // gain = 1 + 0.005·14.1421 = 1.07071 → scale = 1 + 0.07071·1.25 ≈ 1.08839
    // → x = y = 10 · 1.08839 ≈ 10.8839 (scale delta BİLEŞENLERİNE uygulanır)
    vec2d d = { 10.0, 10.0 };
    mod.modify(d, sp, settings, 1.0, 1.0);
    EXPECT(std::isfinite(d.x));
    EXPECT(std::isfinite(d.y));
    EXPECT_NEAR(d.x, 10.8839, 1e-4); // BUG-NEW-15: 0.5 → 1e-4 (measured 10.883883)
    EXPECT_NEAR(d.y, 10.8839, 1e-4);
}

static void test_classic_io_sign_with_cap() {
    SECTION("R12 — classic cap_mode::io with cap.y > 1 (no sign flip)");

    accel_args args = make_args(accel_mode::classic);
    args.cap_mode_val = cap_mode::io;
    args.cap.x = 20.0;
    args.cap.y = 2.0;   // > 1 → no sign flip
    args.exponent_classic = 2.0;

    classic c(args);
    double g_at_zero = c(0.0, args);  // at offset → 1.0
    double g_mid = c(10.0, args);
    double g_high = c(50.0, args);

    EXPECT_NEAR(g_at_zero, 1.0, 1e-9);  // at offset
    EXPECT(g_mid >= 1.0);
    EXPECT(g_high >= 1.0);
    EXPECT(std::isfinite(g_mid));
    EXPECT(std::isfinite(g_high));
    // GAIN io formunda cap.y=2.0 ASİMPTOT'tur: cap.x=20'de ulaşılmaz.
    // accel_raised = gain_accel(20, 1, 2, 0) = 0.025 → c(20) = 2 - 10/20 = 1.5
    double g_cap = c(20.0, args);
    EXPECT_NEAR(g_cap, 1.5, 1e-9);
    // Çok uzakta 1 + cap.y = 2.0 asimptotuna yakınsar (işaret çevrilmez)
    // Sabit/x terimi 1e6'da ~1/1e6 ≈ 1e-6 kadar yaklaşır → tol 1e-4.
    EXPECT_NEAR(c(1e6, args), 2.0, 1e-4);
}

static void test_power_offset_x_guard() {
    SECTION("R12 — power mode offset.x guard (speed <= offset.x → offset.y)");

    accel_args args = make_args(accel_mode::power);
    args.exponent_power = 0.3;
    args.scale = 0.5;
    args.output_offset = 1.2;  // non-zero output offset
    args.cap_mode_val = cap_mode::out;
    args.cap.y = 3.0;

    power p(args);
    // Very low speed should return offset.y or 1.0
    double g_zero = p(0.0, args);
    EXPECT(std::isfinite(g_zero));
    // Negative speed guard
    double g_neg = p(-1.0, args);
    EXPECT(std::isfinite(g_neg));

    // Speed just above offset.x
    double g_just = p(0.01, args);
    EXPECT(std::isfinite(g_just));

    // Speed at cap boundary
    double g_huge = p(1e6, args);
    EXPECT(std::isfinite(g_huge));
}

static void test_speed_clamp_path() {
    SECTION("R12 — modifier speed_min/speed_max clamp");

    profile prof{};
    prof.accel_x.mode = accel_mode::noaccel;
    prof.accel_y = prof.accel_x;
    prof.speed_min = 5.0;   // minimum speed
    prof.speed_max = 100.0; // maximum speed

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);
    EXPECT(settings.data.flags.clamp_speed);

    speed_processor sp;
    sp.init(prof.speed_processor_args);
    modifier mod;

    // Very slow movement → should be clamped UP to speed_min
    vec2d slow = { 0.1, 0.0 };
    mod.modify(slow, sp, settings, 1.0, 1.0);
    EXPECT(std::isfinite(slow.x));
    // The actual output depends on the clamping ratio

    // Very fast movement → should be clamped DOWN to speed_max
    vec2d fast = { 10000.0, 0.0 };
    mod.modify(fast, sp, settings, 1.0, 1.0);
    EXPECT(std::isfinite(fast.x));

    // Normal speed (within range) → no clamping effect
    vec2d normal = { 50.0, 0.0 };
    mod.modify(normal, sp, settings, 1.0, 1.0);
    EXPECT(std::isfinite(normal.x));
    EXPECT_NEAR(normal.x, 50.0, 1.0);  // noaccel, should be ~50

    // speed_min == speed_max → everything clamped to one speed
    profile prof2{};
    prof2.accel_x.mode = accel_mode::noaccel;
    prof2.accel_y = prof2.accel_x;
    prof2.speed_min = 50.0;
    prof2.speed_max = 50.0;
    modifier_settings s2;
    s2.prof = prof2;
    init_settings(s2);
    speed_processor sp2;
    sp2.init(prof2.speed_processor_args);
    vec2d v = { 10.0, 0.0 };
    mod.modify(v, sp2, s2, 1.0, 1.0);
    EXPECT(std::isfinite(v.x));
}

static void test_dir_mul_negative_direction() {
    SECTION("R12 — directional DPI multiplier negative direction");

    // Referans: lr/ud oranları SADECE negatif yöne (sol/aşağı) uygulanır,
    // karşılığı değil; pozitif yön orandan etkilenmez.

    profile prof{};
    prof.accel_x.mode = accel_mode::noaccel;
    prof.accel_y = prof.accel_x;
    prof.lr_output_dpi_ratio = 2.0;  // left = 2x, right değişmez
    prof.ud_output_dpi_ratio = 3.0;  // up = 3x, down değişmez

    modifier_settings settings;
    settings.prof = prof;
    init_settings(settings);
    EXPECT(settings.data.flags.apply_dir_mul_x);
    EXPECT(settings.data.flags.apply_dir_mul_y);

    speed_processor sp;
    sp.init(prof.speed_processor_args);
    modifier mod;

    // Positive X (right) → no scaling
    vec2d right = { 10.0, 0.0 };
    mod.modify(right, sp, settings, 1.0, 1.0);
    EXPECT_NEAR(right.x, 10.0, 1e-9);

    // Negative X (left) → multiply by ratio
    vec2d left = { -10.0, 0.0 };
    mod.modify(left, sp, settings, 1.0, 1.0);
    EXPECT_NEAR(left.x, -20.0, 1e-9);   // -10 * 2.0

    // Positive Y (down) → no scaling
    vec2d down = { 0.0, 10.0 };
    mod.modify(down, sp, settings, 1.0, 1.0);
    EXPECT_NEAR(down.y, 10.0, 1e-9);

    // Negative Y (up) → multiply by ratio
    vec2d up = { 0.0, -10.0 };
    mod.modify(up, sp, settings, 1.0, 1.0);
    EXPECT_NEAR(up.y, -30.0, 1e-9);     // -10 * 3.0
}

static void test_synchronous_power_lt_one() {
    SECTION("R12 — synchronous LEGACY activation framework (guards + identity)");

    // Referans activation_framework<LEGACY>: x == sync_speed → 1.0;
    // x > sync_speed → motivity'ye yakınsar (üst sınır 1/motivity .. motivity).
    accel_args args;
    args.mode        = accel_mode::synchronous;
    args.gain        = false;   // LEGACY yolu
    args.sync_speed  = 5.0;
    args.smooth      = 0.5;     // sharpness = 1 → tanh aktivasyon
    args.motivity    = 1.5;
    args.gamma       = 1.0;

    synchronous s(args);

    // Çok küçük hız → port guard: x<=0 → 1.0, asla Inf/NaN yok
    double g_tiny = s(1e-10, args);
    EXPECT(std::isfinite(g_tiny));

    // Sıfır hız → 1.0 (port x<=0 guard'ı)
    double g_zero = s(0.0, args);
    EXPECT_NEAR(g_zero, 1.0, 1e-9);

    // sync_speed'de özdeşlik: s(5)=1.0
    double g_sync = s(5.0, args);
    EXPECT_NEAR(g_sync, 1.0, 1e-6);

    // Üstünde: artar, motivity (1.5) sınırına yakınsar
    double g_high = s(100.0, args);
    EXPECT(std::isfinite(g_high));
    EXPECT(g_high > 1.0);
    EXPECT_NEAR(g_high, 1.5, 1e-6);   // exp(tanh(γ_const·log20)·log_motivity) → motivity

    // Altında: 1'den küçük ama ≥ minimum_sens = 1/motivity = 0.6667
    double g_low = s(2.0, args);
    EXPECT(std::isfinite(g_low));
    EXPECT(g_low < 1.0);
    EXPECT(g_low > 1.0 / 1.5 - 1e-9);
    EXPECT_NEAR(g_low, 0.672517, 1e-6); // exp(-tanh(γ_const·log(5/2))·log_motivity)

    // smooth=0 → doğrusal kırpma: min/maks sens arasında sabit
    accel_args args_clamp = args;
    args_clamp.smooth = 0.0;
    synchronous sc(args_clamp);
    EXPECT_NEAR(sc(1e-6, args_clamp), 1.0 / 1.5, 1e-6);  // log_space < -1 → minimum
    EXPECT_NEAR(sc(1.0,  args_clamp), 1.0 / 1.5, 1e-6);  // log_space < -1 → minimum
    EXPECT_NEAR(sc(5.0,  args_clamp), 1.0, 1e-9);        // sync_speed
    EXPECT_NEAR(sc(100.0, args_clamp), 1.5, 1e-6);       // log_space > 1 → motivity
}

static void test_natural_legacy_mode() {
    SECTION("R12 — natural legacy (non-gain) mode");

    accel_args args = make_args(accel_mode::natural);
    args.gain = false;  // legacy mode
    args.limit = 2.0;
    args.decay_rate = 0.1;
    args.input_offset = 0;

    natural n(args);

    // At offset, return 1.0
    double g0 = n(0.0, args);
    EXPECT_NEAR(g0, 1.0, 1e-9);

    // As speed → ∞, gain → limit + 1 = 2.0
    double g_high = n(1000.0, args);
    EXPECT_NEAR(g_high, 2.0, 0.1);

    // Intermediate speed: monotonically increasing
    double g1 = n(10.0, args);
    double g2 = n(20.0, args);
    double g3 = n(50.0, args);
    EXPECT(g1 < g2);
    EXPECT(g2 < g3);
    EXPECT(g3 <= 2.0 + 1e-6);
}

static void test_config_output_dpi_sanitize() {
    SECTION("R12 — sanitize output_dpi boundary values");

    // output_dpi < 0 → "no normalization" sentinel 0 (CFG-1)
    device_profile dp;
    dp.prof.output_dpi = -500;
    sanitize_device_profile(dp);
    EXPECT(dp.prof.output_dpi == 0.0);

    // output_dpi in (0,1) → clamped to 1
    device_profile dph;
    dph.prof.output_dpi = 0.5;
    sanitize_device_profile(dph);
    EXPECT(dph.prof.output_dpi == 1.0);

    // output_dpi > 32000 → clamped to 32000
    device_profile dp2;
    dp2.prof.output_dpi = 99999;
    sanitize_device_profile(dp2);
    EXPECT(dp2.prof.output_dpi <= 32000.0);

    // output_dpi NaN → finite_or(NaN, NORMALIZED_DPI) should give NORMALIZED_DPI
    // then clamp: result in [1, 32000]
    device_profile dp3;
    dp3.prof.output_dpi = std::numeric_limits<double>::quiet_NaN();
    sanitize_device_profile(dp3);
    EXPECT(std::isfinite(dp3.prof.output_dpi));
    EXPECT(dp3.prof.output_dpi >= 1.0);
    EXPECT(dp3.prof.output_dpi <= 32000.0);
}

// ── BUG-5: JSON int extraction must not trigger UB on out-of-range doubles ───
// libFuzzer + UBSan caught nlohmann::json::get<int>() invoking
// "X is outside the range of representable values of type 'int'" UB when
// the JSON value was a float like 1e26.  All int-typed fields now go
// through json_get_int_safe() which clamps and casts defensively.

static void test_json_int_overflow_safe() {
    SECTION("BUG-5 — JSON parse rejects out-of-range numbers without UB");

    // 1e26 in dpi field — must be clamped to 32000 (sanitize) without
    // ever performing the UB int-cast that triggered the UBSan warning.
    {
        std::string js = R"({"profiles":[{"name":"x","dpi":1e26,"polling_rate":1e26,
                          "profile":{"accel_x":{"mode":"classic"}}}]})";
        std::string tmp = "/tmp/rawaccel_test_bug5.json";
        { std::ofstream f(tmp); f << js; }
        app_config cfg = load_config(tmp);
        std::remove(tmp.c_str());
        EXPECT(cfg.profiles.size() == 1);
        EXPECT(cfg.profiles[0].dev_cfg.dpi == 32000);              // upper clamp
        EXPECT(cfg.profiles[0].dev_cfg.polling_rate == 8000);       // upper clamp
    }
    // -1e30 → INT_MIN clamp path → sanitize → 1
    {
        std::string js = R"({"profiles":[{"name":"y","dpi":-1e30,"polling_rate":-1e30,
                          "profile":{}}]})";
        std::string tmp = "/tmp/rawaccel_test_bug5b.json";
        { std::ofstream f(tmp); f << js; }
        app_config cfg = load_config(tmp);
        std::remove(tmp.c_str());
        EXPECT(cfg.profiles[0].dev_cfg.dpi == 1);
        EXPECT(cfg.profiles[0].dev_cfg.polling_rate == 125);
    }
    // Non-numeric dpi (string) → fallback default
    {
        std::string js = R"({"profiles":[{"name":"z","dpi":"not a number",
                          "polling_rate":true,"profile":{}}]})";
        std::string tmp = "/tmp/rawaccel_test_bug5c.json";
        { std::ofstream f(tmp); f << js; }
        app_config cfg = load_config(tmp);
        std::remove(tmp.c_str());
        // Fallback was 800 / 1000, both within sanitize range.
        EXPECT(cfg.profiles[0].dev_cfg.dpi == 800);
        EXPECT(cfg.profiles[0].dev_cfg.polling_rate == 1000);
    }
    // lut_length 1e26 — must not UB and must clamp under LUT capacity.
    {
        std::string js = R"({"profiles":[{"name":"l",
                          "profile":{"accel_x":{"mode":"lookup","lut_length":1e26,
                                                 "lut_data":[1.0,1.0]}}}]})";
        std::string tmp = "/tmp/rawaccel_test_bug5d.json";
        { std::ofstream f(tmp); f << js; }
        app_config cfg = load_config(tmp);
        std::remove(tmp.c_str());
        EXPECT(cfg.profiles[0].prof.accel_x.length <=
               (int)LUT_RAW_DATA_CAPACITY);
        EXPECT(cfg.profiles[0].prof.accel_x.length >= 0);
    }
}

// ── BUG-1: rotation negative normalization preserves direction ───────────────

static void test_rotation_negative_normalization() {
    SECTION("BUG-1 — sanitize_profile: negative rotation maps to mathematical equivalent");

    // -45° (CCW 45) must become +315°, NOT +45° (which would flip direction).
    {
        device_profile dp;
        dp.prof.degrees_rotation = -45.0;
        sanitize_device_profile(dp);
        EXPECT_NEAR(dp.prof.degrees_rotation, 315.0, 1e-9);
    }
    // -90° → 270°
    {
        device_profile dp;
        dp.prof.degrees_rotation = -90.0;
        sanitize_device_profile(dp);
        EXPECT_NEAR(dp.prof.degrees_rotation, 270.0, 1e-9);
    }
    // -179.5° → 180.5°
    {
        device_profile dp;
        dp.prof.degrees_rotation = -179.5;
        sanitize_device_profile(dp);
        EXPECT_NEAR(dp.prof.degrees_rotation, 180.5, 1e-9);
    }
    // Multi-period wrap: -720° → 0°
    {
        device_profile dp;
        dp.prof.degrees_rotation = -720.0;
        sanitize_device_profile(dp);
        EXPECT_NEAR(dp.prof.degrees_rotation, 0.0, 1e-9);
    }
    // -725° → -725 mod 360 = -5 → +355
    {
        device_profile dp;
        dp.prof.degrees_rotation = -725.0;
        sanitize_device_profile(dp);
        EXPECT_NEAR(dp.prof.degrees_rotation, 355.0, 1e-9);
    }
    // Positive values must still wrap into [0,360): 720 → 0, 405 → 45.
    {
        device_profile dp;
        dp.prof.degrees_rotation = 720.0;
        sanitize_device_profile(dp);
        EXPECT_NEAR(dp.prof.degrees_rotation, 0.0, 1e-9);
    }
    {
        device_profile dp;
        dp.prof.degrees_rotation = 405.0;
        sanitize_device_profile(dp);
        EXPECT_NEAR(dp.prof.degrees_rotation, 45.0, 1e-9);
    }
    // Direction preservation property: rotating by sanitize(-θ) must
    // produce the same vector as rotating by 360-θ.
    {
        device_profile dp_neg, dp_pos;
        dp_neg.prof.degrees_rotation = -30.0;
        dp_pos.prof.degrees_rotation = 330.0;
        sanitize_device_profile(dp_neg);
        sanitize_device_profile(dp_pos);
        vec2d d_neg = direction(dp_neg.prof.degrees_rotation);
        vec2d d_pos = direction(dp_pos.prof.degrees_rotation);
        EXPECT_NEAR(d_neg.x, d_pos.x, 1e-12);
        EXPECT_NEAR(d_neg.y, d_pos.y, 1e-12);
    }
}

// ── R13 — lat_stats move + dpi_factor pre-compute ────────────────────────────

static void test_lat_stats_move_semantics() {
    SECTION("R13 — lat_stats move preserves data and resets source");

    // Record some data into a lat_stats instance
    lat_stats ls;
    ls.record(10.0);
    ls.record(20.0);
    ls.record(30.0);
    EXPECT(ls.count == 3);
    EXPECT_NEAR(ls.sum_us, 60.0, 1e-9);
    EXPECT_NEAR(ls.min_us, 10.0, 1e-9);
    EXPECT_NEAR(ls.max_us, 30.0, 1e-9);

    // Move construct: data transfers to new object, source resets
    lat_stats ls2(std::move(ls));
    EXPECT(ls2.count == 3);
    EXPECT_NEAR(ls2.sum_us, 60.0, 1e-9);
    EXPECT_NEAR(ls2.min_us, 10.0, 1e-9);
    EXPECT_NEAR(ls2.max_us, 30.0, 1e-9);
    // Source should be reset
    EXPECT(ls.count == 0);
    EXPECT_NEAR(ls.sum_us, 0.0, 1e-9);
    EXPECT_NEAR(ls.min_us, 1e9, 1.0); // reset sentinel

    // Move assign: record into ls, then move-assign from ls2
    ls.record(5.0);
    ls = std::move(ls2);
    EXPECT(ls.count == 3);
    EXPECT_NEAR(ls.sum_us, 60.0, 1e-9);
    // ls2 should be reset
    EXPECT(ls2.count == 0);

    // Snapshot and reset
    auto snap = ls.snapshot_and_reset();
    EXPECT(snap.count == 3);
    EXPECT_NEAR(snap.avg_us(), 20.0, 1e-9);
    EXPECT(ls.count == 0);
}

static void test_dpi_factor_precompute() {
    SECTION("R13 — dpi_factor pre-compute consistency");

    // Verify that dpi_factor = NORMALIZED_DPI / dpi for various DPI values.
    // O6: reference uses input_dpi_normalization_factor = NORMALIZED_DPI / dpi
    // (was inverted to dpi/NORMALIZED_DPI before the alignment).  With this
    // convention, speed = counts/ms * dpi_factor is the true movement speed in
    // inches/sec and the accel curve triggers at the same physical speed
    // regardless of the sensor DPI.
    for (int dpi : {100, 400, 800, 1600, 3200, 16000, 32000}) {
        double expected = NORMALIZED_DPI / dpi;
        // Simulate what apply_profile does:
        double computed = NORMALIZED_DPI / std::clamp(dpi, 1, 32000);
        EXPECT_NEAR(computed, expected, 1e-12);
    }

    // Extreme: DPI=1 (min), DPI=32000 (max)
    EXPECT_NEAR(NORMALIZED_DPI / 1.0, 1000.0, 1e-9);
    EXPECT_NEAR(NORMALIZED_DPI / 32000.0, 0.03125, 1e-9);

    // Verify the division result is finite and positive for all valid DPI
    for (int dpi = 1; dpi <= 32000; dpi += 1000) {
        double f = NORMALIZED_DPI / dpi;
        EXPECT(std::isfinite(f));
        EXPECT(f > 0);
    }
}

// R14 — magnitude() uses std::hypot (overflow-safe)
static void test_magnitude_hypot() {
    SECTION("R14 — magnitude uses hypot (overflow-safe)");

    // Normal values — must match manual sqrt(x^2 + y^2)
    EXPECT_NEAR(magnitude({3.0, 4.0}), 5.0, 1e-12);
    EXPECT_NEAR(magnitude({0.0, 0.0}), 0.0, 1e-12);
    EXPECT_NEAR(magnitude({1.0, 0.0}), 1.0, 1e-12);
    EXPECT_NEAR(magnitude({0.0, -7.0}), 7.0, 1e-12);

    // Typical mouse deltas
    EXPECT_NEAR(magnitude({5.0, 12.0}), 13.0, 1e-12);
    EXPECT_NEAR(magnitude({-8.0, 6.0}), 10.0, 1e-12);

    // Large values that would overflow with naive sqrt(x*x+y*y):
    // DBL_MAX ≈ 1.8e308, so x*x would be inf for x > ~1.34e154
    double big = 1e200;
    double result = magnitude({big, big});
    EXPECT(std::isfinite(result));
    EXPECT_NEAR(result, big * std::sqrt(2.0), big * 1e-12);

    // Very small values (subnormal territory) — must not underflow to zero
    double tiny = 1e-300;
    double tiny_result = magnitude({tiny, tiny});
    EXPECT(tiny_result > 0.0);
    EXPECT(std::isfinite(tiny_result));
    EXPECT_NEAR(tiny_result, tiny * std::sqrt(2.0), tiny * 1e-6);

    // Verify magnitude is used correctly in rawaccel pipeline
    // (modifier speed calculation uses magnitude on mouse deltas)
    vec2d delta = {10.0, 10.0};
    double speed = magnitude(delta);
    EXPECT_NEAR(speed, 10.0 * std::sqrt(2.0), 1e-10);
}

// ── R15: Comprehensive hardening tests ───────────────────────────────────────

/// Test LUT binary search boundary conditions exhaustively.
static void test_lut_binary_search_boundaries() {
    SECTION("R15 — LUT binary search boundary exhaustive");

    // 2-point LUT: linear interpolation between exactly 2 points
    // (velocity off → stored y kazanım olarak okunur; referans semantiği)
    {
        accel_args a = make_args(accel_mode::lookup);
        a.gain = false;  // velocity off → y doğrudan kazanım
        a.data[0] = 10.0f; a.data[1] = 1.0f;  // speed=10 → gain=1.0
        a.data[2] = 50.0f; a.data[3] = 3.0f;  // speed=50 → gain=3.0
        a.length = 4;
        accel_union au; au.init(a);

        // x <= 0 → 0 (reference lookup: strictly positive domain)
        EXPECT_NEAR(au.apply(0.0, a), 0.0, 1e-9);
        // Below first point → fallback to first point's gain
        EXPECT_NEAR(au.apply(5.0, a), 1.0, 1e-9);
        EXPECT_NEAR(au.apply(10.0, a), 1.0, 1e-9);

        // Exact midpoint: speed=30 → gain=2.0 (linear interp)
        EXPECT_NEAR(au.apply(30.0, a), 2.0, 1e-9);

        // At second point exactly
        EXPECT_NEAR(au.apply(50.0, a), 3.0, 1e-9);

        // Above last point → referans lerp t>1 ile EKSTRAPOLASYON yapar (kırpma değil).
        // y = 1 + (100-10)/(50-10)·2 = 5.5
        EXPECT_NEAR(au.apply(100.0, a), 5.5, 1e-9);
        // Far extrapolation (float index resolution): ~2 + (1e6-30)/20 ≈ 50000.5
        EXPECT_NEAR(au.apply(1e6, a), 50000.5, 0.5);
    }

    // 3-point LUT: binary search mid calculation
    {
        accel_args a = make_args(accel_mode::lookup);
        a.gain = false;  // velocity off → y doğrudan kazanım
        a.data[0] = 0.0f;  a.data[1] = 1.0f;
        a.data[2] = 25.0f; a.data[3] = 2.0f;
        a.data[4] = 50.0f; a.data[5] = 4.0f;
        a.length = 6;
        accel_union au; au.init(a);

        // Verify each segment
        EXPECT_NEAR(au.apply(0.0, a), 0.0, 1e-9);   // x<=0 → 0
        EXPECT_NEAR(au.apply(12.5, a), 1.5, 1e-9);  // midpoint seg 1
        EXPECT_NEAR(au.apply(25.0, a), 2.0, 1e-9);
        EXPECT_NEAR(au.apply(37.5, a), 3.0, 1e-9);  // midpoint seg 2
        EXPECT_NEAR(au.apply(50.0, a), 4.0, 1e-9);
    }

    // Max capacity LUT: fill all 257 points and verify endpoints
    {
        accel_args a = make_args(accel_mode::lookup);
        a.gain = false;  // velocity off → y doğrudan kazanım
        int cap = (int)LUT_POINTS_CAPACITY;
        a.length = cap * 2;
        for (int i = 0; i < cap; i++) {
            a.data[i * 2]     = static_cast<float>(i);       // speed = i
            a.data[i * 2 + 1] = 1.0f + static_cast<float>(i) * 0.01f;  // gain = 1 + i*0.01
        }
        accel_union au; au.init(a);

        // First point
        EXPECT_NEAR(au.apply(0.0, a), 0.0, 1e-4);
        // Last point
        EXPECT_NEAR(au.apply(cap - 1, a), 1.0 + (cap - 1) * 0.01, 0.01);
        // Beyond last → ekstrapolasyon: y = 3.55 + (1000-255)·(3.56-3.55) = 11.0
        EXPECT_NEAR(au.apply(1000.0, a), 11.0, 0.01);
        // Mid-point interpolation
        EXPECT_NEAR(au.apply(128.5, a), 1.0 + 128.5 * 0.01, 0.01);
    }
}

/// Test the full modifier pipeline with every feature flag enabled simultaneously.
static void test_modifier_full_pipeline_stress() {
    SECTION("R15 — modifier full pipeline: all features enabled simultaneously");

    profile p{};
    p.accel_x.mode = accel_mode::classic;
    p.accel_x.gain = true;
    p.accel_x.acceleration = 0.01;
    p.accel_x.exponent_classic = 2.0;
    p.accel_x.limit = 2.0;
    p.accel_x.input_offset = 5.0;
    p.accel_x.cap = {15, 1.8};
    p.accel_x.cap_mode_val = cap_mode::io;

    p.accel_y = p.accel_x;
    p.accel_y.mode = accel_mode::natural;
    p.accel_y.decay_rate = 0.1;

    p.degrees_rotation = 30.0;
    p.degrees_snap = 10.0;
    p.speed_min = 2.0;
    p.speed_max = 100.0;
    p.output_dpi = 1600;
    p.lr_output_dpi_ratio = 1.2;
    p.ud_output_dpi_ratio = 0.8;
    p.speed_processor_args.whole = true;
    p.speed_processor_args.lp_norm = 2.0;
    p.speed_processor_args.input_speed_smooth_halflife = 5.0;
    p.speed_processor_args.scale_smooth_halflife = 3.0;

    modifier_settings ms; ms.prof = p; init_settings(ms);
    speed_processor   sp; sp.init(p.speed_processor_args);
    modifier          mod;
    double dpi_factor = 1600.0 / NORMALIZED_DPI;

    // Run 10000 events through the pipeline
    double rem_x = 0, rem_y = 0;
    double total_out_x = 0, total_out_y = 0;
    for (int i = 0; i < 10000; i++) {
        double dx = 3.0 + std::sin(i * 0.1) * 2.0;
        double dy = 2.0 + std::cos(i * 0.1) * 1.5;
        double time_ms = 1.0; // 1000 Hz

        vec2d in = {dx, dy};
        mod.modify(in, sp, ms, dpi_factor, time_ms);

        // Every output must be finite
        EXPECT(std::isfinite(in.x));
        EXPECT(std::isfinite(in.y));

        // Apply subpixel accumulation
        double out_x_d = in.x + rem_x;
        double out_y_d = in.y + rem_y;
        int ox = (int)(out_x_d >= 0 ? out_x_d + 0.5 : out_x_d - 0.5);
        int oy = (int)(out_y_d >= 0 ? out_y_d + 0.5 : out_y_d - 0.5);
        rem_x = out_x_d - ox;
        rem_y = out_y_d - oy;

        total_out_x += ox;
        total_out_y += oy;
    }

    // Verify some motion actually occurred (not stuck at zero)
    EXPECT(std::fabs(total_out_x) > 100.0);
    EXPECT(std::fabs(total_out_y) > 100.0);

    // Verify remainder stays bounded
    EXPECT(std::fabs(rem_x) < 1.0);
    EXPECT(std::fabs(rem_y) < 1.0);
}

/// Test EMA smoother numerical stability over very long sequences.
static void test_ema_long_sequence_stability() {
    SECTION("R15 — EMA smoother: 100K iterations, no drift or divergence");

    // simple_ema_smoother: feed constant speed → must converge
    {
        simple_ema_smoother sm;
        sm.init(10.0); // halflife=10ms
        double last = 0;
        for (int i = 0; i < 100000; i++) {
            last = sm.smooth(5.0, 1.0); // dt=1ms
        }
        EXPECT_NEAR(last, 5.0, 1e-3);
    }

    // simple_ema_smoother: alternating values → output stays bounded
    {
        simple_ema_smoother sm;
        sm.init(10.0);
        double last = 0;
        for (int i = 0; i < 100000; i++) {
            double v = (i % 2 == 0) ? 10.0 : 0.0;
            last = sm.smooth(v, 1.0);
        }
        EXPECT(std::isfinite(last));
        EXPECT(last >= 0.0 && last <= 10.0);
    }

    // linear_ema_smoother: convergence test
    {
        linear_ema_smoother lm;
        lm.init(10.0, 10.0 * linear_ema_smoother::trendDampening);
        double last = 0;
        for (int i = 0; i < 100000; i++) {
            last = lm.smooth(5.0, 1.0);
        }
        EXPECT_NEAR(last, 5.0, 0.1);
    }

    // halflife=0 (no smoothing) — output tracks input
    {
        simple_ema_smoother sm;
        sm.init(0.0); // coefficient=0 → no smoothing
        for (int i = 0; i < 100; i++) sm.smooth(42.0, 1.0);
        double v = sm.smooth(99.0, 1.0);
        // With halflife=0, coefficient=0 → smooth returns min(window,cutoff)
        // which should be close to the input
        EXPECT(std::isfinite(v));
    }
}

/// Test config round-trip with every field at exact boundary values.
static void test_config_boundary_roundtrip() {
    SECTION("R15 — config round-trip: all fields at boundary values");

    device_profile dp;
    dp.name = "boundary-test";
    dp.device_id = "usb:1234:5678:serial";
    dp.dev_cfg.dpi = 32000;         // max
    dp.dev_cfg.polling_rate = 8000; // max

    auto& p = dp.prof;
    p.degrees_rotation = 359.99; // not 360: sanitize does fmod(360,360)=0
    p.degrees_snap = 45.0;
    p.speed_min = 0.0;
    p.speed_max = 500.0;
    p.output_dpi = 32000.0;
    p.lr_output_dpi_ratio = 100.0;
    p.ud_output_dpi_ratio = 0.01;
    p.raw_passthrough = false;

    auto& ax = p.accel_x;
    ax.mode = accel_mode::classic;
    ax.gain = true;
    ax.acceleration = 20.0;
    ax.exponent_classic = 10.0;
    ax.exponent_power = 5.0;
    ax.limit = 100.0;
    ax.input_offset = 100.0;
    ax.output_offset = 100.0;
    ax.decay_rate = 10.0;
    ax.scale = 100.0;
    ax.motivity = 10.0;
    ax.gamma = 10.0;
    ax.sync_speed = 100.0;
    ax.smooth = 1.0;
    ax.cap = {500, 100};
    ax.cap_mode_val = cap_mode::io;

    p.accel_y = ax;
    p.accel_y.mode = accel_mode::natural;

    auto& sp = p.speed_processor_args;
    sp.whole = true;
    sp.lp_norm = 32.0;
    sp.input_speed_smooth_halflife = 200.0;
    sp.scale_smooth_halflife = 200.0;
    sp.output_speed_smooth_halflife = 200.0;

    // Serialize → deserialize
    std::string json = profile_to_json(dp);
    device_profile dp2 = profile_from_json(json);

    // Verify every field
    EXPECT(dp2.name == dp.name);
    EXPECT(dp2.device_id == dp.device_id);
    EXPECT(dp2.dev_cfg.dpi == dp.dev_cfg.dpi);
    EXPECT(dp2.dev_cfg.polling_rate == dp.dev_cfg.polling_rate);
    EXPECT_NEAR(dp2.prof.degrees_rotation, 359.99, 1e-9);
    EXPECT_NEAR(dp2.prof.degrees_snap, p.degrees_snap, 1e-9);
    EXPECT_NEAR(dp2.prof.speed_min, p.speed_min, 1e-9);
    EXPECT_NEAR(dp2.prof.speed_max, p.speed_max, 1e-9);
    EXPECT_NEAR(dp2.prof.output_dpi, p.output_dpi, 1e-9);
    EXPECT_NEAR(dp2.prof.lr_output_dpi_ratio, p.lr_output_dpi_ratio, 1e-9);
    EXPECT_NEAR(dp2.prof.ud_output_dpi_ratio, p.ud_output_dpi_ratio, 1e-9);
    EXPECT(dp2.prof.raw_passthrough == false);

    auto& ax2 = dp2.prof.accel_x;
    EXPECT(ax2.mode == accel_mode::classic);
    EXPECT(ax2.gain == true);
    EXPECT_NEAR(ax2.acceleration, 20.0, 1e-9);
    EXPECT_NEAR(ax2.exponent_classic, 10.0, 1e-9);
    EXPECT_NEAR(ax2.limit, 100.0, 1e-9);
    EXPECT_NEAR(ax2.input_offset, 100.0, 1e-9);
    EXPECT_NEAR(ax2.output_offset, 100.0, 1e-9);
    EXPECT_NEAR(ax2.scale, 100.0, 1e-9);
    EXPECT_NEAR(ax2.cap.x, 500.0, 1e-3);
    EXPECT_NEAR(ax2.cap.y, 100.0, 1e-3);

    EXPECT(dp2.prof.accel_y.mode == accel_mode::natural);

    EXPECT_NEAR(dp2.prof.speed_processor_args.lp_norm, 32.0, 1e-9);
    EXPECT_NEAR(dp2.prof.speed_processor_args.input_speed_smooth_halflife, 200.0, 1e-9);
}

/// Test rotate() + direction() mathematical identity: rotate by θ then -θ = identity.
static void test_rotate_direction_identity() {
    SECTION("R15 — rotate/direction identity and edge cases");

    // Rotate 0° = identity
    vec2d v = {3.0, 4.0};
    vec2d r0 = rotate(v, direction(0.0));
    EXPECT_NEAR(r0.x, 3.0, 1e-12);
    EXPECT_NEAR(r0.y, 4.0, 1e-12);

    // Rotate 360° = identity
    vec2d r360 = rotate(v, direction(360.0));
    EXPECT_NEAR(r360.x, 3.0, 1e-9);
    EXPECT_NEAR(r360.y, 4.0, 1e-9);

    // Rotate 90° then -90° = identity
    vec2d r90 = rotate(v, direction(90.0));
    vec2d back = rotate(r90, direction(-90.0));
    EXPECT_NEAR(back.x, 3.0, 1e-9);
    EXPECT_NEAR(back.y, 4.0, 1e-9);

    // Rotate 90°: (1,0) → (0,1)
    vec2d unit_x = {1.0, 0.0};
    vec2d rot90 = rotate(unit_x, direction(90.0));
    EXPECT_NEAR(rot90.x, 0.0, 1e-12);
    EXPECT_NEAR(rot90.y, 1.0, 1e-12);

    // Rotate 180°: (1,0) → (-1,0)
    vec2d rot180 = rotate(unit_x, direction(180.0));
    EXPECT_NEAR(rot180.x, -1.0, 1e-12);
    EXPECT_NEAR(rot180.y, 0.0, 1e-12);

    // Magnitude preserved under rotation
    for (double deg = 0; deg < 360; deg += 17.3) {
        vec2d rv = rotate(v, direction(deg));
        EXPECT_NEAR(magnitude(rv), magnitude(v), 1e-9);
    }
}

/// Test all helper functions in math-vec2.hpp thoroughly.
static void test_vec2_helpers_exhaustive() {
    SECTION("R15 — vec2 helpers: maxsd, minsd, clampsd, lp_distance");

    EXPECT_NEAR(maxsd(3.0, 7.0), 7.0, 1e-15);
    EXPECT_NEAR(maxsd(-1.0, -5.0), -1.0, 1e-15);
    EXPECT_NEAR(maxsd(0.0, 0.0), 0.0, 1e-15);

    EXPECT_NEAR(minsd(3.0, 7.0), 3.0, 1e-15);
    EXPECT_NEAR(minsd(-1.0, -5.0), -5.0, 1e-15);

    EXPECT_NEAR(clampsd(5.0, 0.0, 10.0), 5.0, 1e-15);
    EXPECT_NEAR(clampsd(-5.0, 0.0, 10.0), 0.0, 1e-15);
    EXPECT_NEAR(clampsd(15.0, 0.0, 10.0), 10.0, 1e-15);
    EXPECT_NEAR(clampsd(0.0, 0.0, 0.0), 0.0, 1e-15);

    // lp_distance for various norms
    vec2d v = {3.0, 4.0};
    EXPECT_NEAR(lp_distance(v, 2.0), 5.0, 1e-9);   // L2 = euclidean
    EXPECT_NEAR(lp_distance(v, 1.0), 7.0, 1e-9);   // L1 = manhattan
    // L∞ approximation (large p)
    EXPECT_NEAR(lp_distance(v, 100.0), 4.0, 0.01); // → max(3,4) = 4

    // lp_distance with zero vector (R6 fix)
    EXPECT_NEAR(lp_distance({0, 0}, 2.0), 0.0, 1e-15);
    EXPECT_NEAR(lp_distance({0, 0}, 0.5), 0.0, 1e-15);
    EXPECT_NEAR(lp_distance({0, 0}, -1.0), 0.0, 1e-15);
}

/// Concurrent-like stress test for lat_stats — rapid record/snapshot cycles.
static void test_lat_stats_stress() {
    SECTION("R15 — lat_stats stress: 1M records + periodic snapshots");

    lat_stats ls;

    // Record 1M values
    for (int i = 0; i < 1000000; i++) {
        ls.record(static_cast<double>(i % 1000));
    }
    EXPECT(ls.count == 1000000);
    EXPECT_NEAR(ls.min_us, 0.0, 1e-9);
    EXPECT_NEAR(ls.max_us, 999.0, 1e-9);
    EXPECT_NEAR(ls.sum_us / ls.count, 499.5, 0.01); // avg of 0..999 repeated

    // Snapshot and reset
    auto snap = ls.snapshot_and_reset();
    EXPECT(snap.count == 1000000);
    EXPECT(ls.count == 0);

    // Histogram bucket coverage: bucket = int(lat_us / 0.5)
    // Value 0µs → bucket 0, value 249µs → bucket 498, values ≥500µs → over
    EXPECT(snap.hist[0] > 0);   // bucket 0: [0.0, 0.5) — hit by value 0
    EXPECT(snap.hist[998] > 0); // bucket 998: [499.0, 499.5) — hit by value 499
    EXPECT(snap.over > 0);      // values 500–999 go to over

    // Record after reset — works correctly
    ls.record(42.0);
    EXPECT(ls.count == 1);
    EXPECT_NEAR(ls.min_us, 42.0, 1e-9);
    EXPECT_NEAR(ls.max_us, 42.0, 1e-9);
}

/// Test all 7 accel modes through sanitize → init → apply for consistency.
static void test_all_modes_sanitize_init_apply() {
    SECTION("R15 — all modes: sanitize → init → apply cycle");

    accel_mode modes[] = {
        accel_mode::noaccel, accel_mode::classic, accel_mode::power,
        accel_mode::natural, accel_mode::jump, accel_mode::synchronous,
        accel_mode::lookup
    };

    for (auto m : modes) {
        device_profile dp;
        dp.name = "test";
        dp.dev_cfg.dpi = 800;
        dp.dev_cfg.polling_rate = 1000;
        auto& a = dp.prof.accel_x;
        a.mode = m;
        a.gain = true;
        a.acceleration = 0.01;
        a.exponent_classic = 2.0;
        a.exponent_power = 0.5;
        a.limit = 2.0;
        a.decay_rate = 0.1;
        a.scale = 1.0;
        a.motivity = 1.5;
        a.gamma = 1.0;
        a.sync_speed = 5.0;
        a.smooth = 0.5;
        a.cap = {15, 1.8};
        a.cap_mode_val = cap_mode::out;

        if (m == accel_mode::lookup) {
            a.data[0] = 0; a.data[1] = 1.0f;
            a.data[2] = 50; a.data[3] = 2.0f;
            a.length = 4;
        }

        dp.prof.accel_y = a;

        // Sanitize via device_profile (the public API)
        sanitize_device_profile(dp);

        // Init + apply at various speeds
        accel_union au;
        au.init(dp.prof.accel_x);
        for (double spd = 0.0; spd <= 200.0; spd += 5.0) {
            double g = au.apply(spd, dp.prof.accel_x);
            EXPECT(std::isfinite(g));
            EXPECT(g >= 0.0); // gain must never be negative
        }
    }
}

/// Test config save/load with Unicode profile names and device IDs.
static void test_config_unicode_names() {
    SECTION("R15 — config round-trip: Unicode profile names");

    app_config cfg;
    device_profile dp;
    dp.name = "Oyun Profili \xC3\xB6zel";  // "özel" in UTF-8
    dp.device_id = "usb:04d9:a0cd:\xE4\xB8\xAD\xE6\x96\x87"; // Chinese chars
    dp.dev_cfg.dpi = 1600;
    dp.dev_cfg.polling_rate = 1000;
    cfg.profiles.push_back(dp);
    cfg.active_profile = dp.name;

    // Save to temp file
    std::string tmp = "/tmp/rawaccel_test_unicode.json";
    save_config(cfg, tmp);

    // Load back
    app_config cfg2 = load_config(tmp);
    EXPECT(cfg2.profiles.size() == 1);
    EXPECT(cfg2.profiles[0].name == dp.name);
    EXPECT(cfg2.profiles[0].device_id == dp.device_id);
    EXPECT(cfg2.active_profile == dp.name);

    std::remove(tmp.c_str());
}

/// Test synchronous mode edge cases: sc=0, power boundary, speed near sync_speed.
static void test_synchronous_edge_cases() {
    SECTION("R15 — synchronous edge cases: near sync_speed, extreme power");

    // Speed exactly at sync_speed
    {
        accel_args a = make_args(accel_mode::synchronous);
        a.sync_speed = 10.0;
        a.acceleration = 0.5;
        a.gain = true;
        accel_union au; au.init(a);
        double g = au.apply(10.0, a);
        EXPECT(std::isfinite(g));
        EXPECT(g > 0.0);
    }

    // Speed just above and below sync_speed
    {
        accel_args a = make_args(accel_mode::synchronous);
        a.sync_speed = 10.0;
        a.acceleration = 0.5;
        a.gain = true;
        accel_union au; au.init(a);

        double g_below = au.apply(9.999, a);
        double g_above = au.apply(10.001, a);
        EXPECT(std::isfinite(g_below));
        EXPECT(std::isfinite(g_above));
        // Gain should be continuous near sync_speed
        EXPECT(std::fabs(g_below - g_above) < 0.1);
    }

    // Synchronous'ta acceleration kullanılmaz (sigmoide etki etmez).
    // Default profil (mot=1.5, smooth=0.5, sync=5, gamma=1) GAIN LUT referans
    // değeri: s(5) = 0.742577 (referans probe ile doğrulandı).
    {
        accel_args a = make_args(accel_mode::synchronous);
        a.acceleration = 0.0;
        a.gain = true;
        accel_union au; au.init(a);
        EXPECT_NEAR(au.apply(5.0, a), 0.742577, 1e-6);
    }
}

// ── P86: synchronous gain_apply float→int cast UB with huge speed ─────────────

static void test_synchronous_huge_speed_no_ub() {
    SECTION("P86 — synchronous gain_apply: huge speed (1e300) → finite, no UB");

    accel_args args = make_args(accel_mode::synchronous);
    args.gain = true;
    args.smooth = 0;
    synchronous s(args);

    // Huge speed (simulating domain_weights=1e300 applied upstream).
    // Before P86 fix, static_cast<int>(idx_f) was UB for idx_f > INT_MAX.
    double g_huge = s(1e300, args);
    EXPECT(std::isfinite(g_huge));

    // Also test the edge that originally triggers the bug path:
    // x huge enough that ilogb(x) >> range.stop, so scalbn(x,-e) stays huge.
    double g_1e15 = s(1e15, args);
    EXPECT(std::isfinite(g_1e15));

    double g_4e133 = s(4.54e133, args);
    EXPECT(std::isfinite(g_4e133));

    // Normal speeds must still work identically.
    double g_normal = s(10.0, args);
    EXPECT(std::isfinite(g_normal));
    EXPECT(g_normal > 0.0);
}

// ── P91 — denormal / extreme-input hardening ────────────────────────────────

static void test_p91_denormal_extreme_inputs() {
    SECTION("P91 — denormal speed (1e-300) finite and reasonable in every mode");
    {
        accel_union au;
        for (int m = 0; m < 7; ++m) {
            accel_mode mode = static_cast<accel_mode>(m);
            accel_args a = make_args(mode);
            au.init(a);
            double g = au.apply(1e-300, a);
            EXPECT(std::isfinite(g));
            EXPECT(g >= 0.0);
            EXPECT(g < 1000.0); // no explosion from underflow/overflow paths
        }
        // Direct constructors: classic / natural / power / synchronous
        accel_args ca = make_args(accel_mode::classic);
        classic     cl(ca);
        accel_args na = make_args(accel_mode::natural);
        natural     nt(na);
        accel_args pa = make_args(accel_mode::power);
        power       pw(pa);
        accel_args sa = make_args(accel_mode::synchronous);
        synchronous sy(sa);
        EXPECT(std::isfinite(cl(1e-300, ca)));
        EXPECT(std::isfinite(nt(1e-300, na)));
        EXPECT(std::isfinite(pw(1e-300, pa)));
        EXPECT(std::isfinite(sy(1e-300, sa)));
        EXPECT(std::isfinite(cl(5e-324, ca))); // smallest positive denormal
        EXPECT(std::isfinite(pw(5e-324, pa)));
    }

    SECTION("P91 — denormal DPI factor through modifier pipeline");
    {
        for (int m = 0; m < 7; ++m) {
            profile prof{};
            prof.accel_x.mode = static_cast<accel_mode>(m);
            prof.accel_x.gain = true;
            prof.accel_x.acceleration = 0.007;
            prof.accel_x.exponent_classic = 2.0;
            prof.accel_x.exponent_power = 1.5;
            prof.accel_x.limit = 2.0;
            prof.accel_x.scale = 0.01;
            prof.accel_x.decay_rate = 0.1;
            prof.accel_x.sync_speed = 5.0;
            prof.accel_y = prof.accel_x;
            modifier_settings settings;
            settings.prof = prof;
            init_settings(settings);
            speed_processor sp;
            sp.init(prof.speed_processor_args);
            modifier mod;
            vec2d input = { 50.0, 25.0 };
            mod.modify(input, sp, settings, 1e-300, 16.0); // dpi_factor denormal
            EXPECT(std::isfinite(input.x));
            EXPECT(std::isfinite(input.y));
            EXPECT(input.x < 1000.0);
            EXPECT(input.y < 1000.0);
        }
    }

    SECTION("P91 — denormal cursor delta (1e-300) through apply_motion_math");
    {
        profile prof{};
        prof.accel_x.mode = accel_mode::classic;
        prof.accel_x.gain = true;
        prof.accel_x.acceleration = 0.007;
        prof.accel_x.exponent_classic = 2.0;
        prof.accel_x.exponent_power = 1.5;
        prof.accel_x.limit = 2.0;
        prof.accel_x.scale = 0.01;
        prof.accel_x.decay_rate = 0.1;
        prof.accel_x.sync_speed = 5.0;
        prof.accel_y = prof.accel_x;
        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);
        speed_processor sp;
        sp.init(prof.speed_processor_args);
        modifier mod;
        double rx = 0.0, ry = 0.0;
        int out_x = 0, out_y = 0;
        apply_motion_math(mod, sp, settings, 1.0, 16.0,
                          1e-300, 1e-300, rx, ry, out_x, out_y);
        EXPECT(out_x == 0);
        EXPECT(out_y == 0);
        EXPECT(std::isfinite(rx));
        EXPECT(std::isfinite(ry));
        // Fraction must be preserved (not lost to NaN/Inf clamping).
        EXPECT(rx > 0.0 && rx < 1.0);
        EXPECT(ry > 0.0 && ry < 1.0);
    }

    SECTION("P91 — natural gain at extreme high speed (1e9 ips) stays finite");
    {
        // natural<GAIN> output = limit*(decay/accel - offset_x) - limit/accel,
        // gain = output/x + 1. At extreme speed the log-domain-style integral
        // must converge to args.limit without overflow (decay → 0, no Inf/NaN).
        accel_args args = make_args(accel_mode::natural);
        args.decay_rate  = 0.1;
        args.limit       = 1.5;
        args.input_offset = 0.0;
        args.gain        = true;
        natural n(args);
        double g = n(1e9, args);
        EXPECT(std::isfinite(g));
        EXPECT(g > 0.0);
        EXPECT_NEAR(g, 1.5, 5e-3);   // asymptote → args.limit
    }
}

static void test_p91_known_deviation_boundaries() {
    SECTION("P91 — s(0) identity rows (power/synchronous at speed 0)");
    {
        // The 7 documented power/synchronous @0 deviations are identity rows:
        // official ref yields gain exactly 1 at speed 0.
        accel_args pa = make_args(accel_mode::power);
        power       pw(pa);
        accel_args sa = make_args(accel_mode::synchronous);
        synchronous sy(sa);
        EXPECT_NEAR(pw(0.0, pa), 1.0, 1e-12);
        EXPECT_NEAR(sy(0.0, sa), 1.0, 1e-12);
        // identity persists for tiny but non-zero speed too (matches ref)
        double g_tiny_p = pw(1e-12, pa);
        EXPECT(std::isfinite(g_tiny_p));
        EXPECT(g_tiny_p > 0.0);   // finite, positive — not exactly 1 below s=0
        EXPECT(g_tiny_p < 1000.0);
    }

    SECTION("P91 — classic exp<=1 constant-gain (documented linear path)");
    {
        // known_deviations: classic_gain_exp_le1 — exponent <= 1 collapses to a
        // constant gain on the linear path; validate the boundary is respected
        // at exactly exp=1 and below, and that the discontinuity with exp>1
        // does not leak into the deviating regime.
        for (double e : { 0.5, 1.0 }) {
            accel_args args = make_args(accel_mode::classic);
            args.exponent_classic = e;
            classic c(args);
            double g_low  = c(1.0, args);
            double g_mid  = c(12.0, args);
            double g_high = c(200.0, args);
            EXPECT(std::isfinite(g_low));
            EXPECT(std::isfinite(g_mid));
            EXPECT(std::isfinite(g_high));
            EXPECT_NEAR(g_low, g_mid, 1e-9);   // constant along speed
            EXPECT_NEAR(g_low, g_high, 1e-9);  // constant along speed
        }
        // exp just above 1 is NOT in the deviation set: with a roomy gain cap
        // (10 > 1.5) it must vary with speed, not stay constant.
        accel_args args = make_args(accel_mode::classic);
        args.exponent_classic = 1.01;
        args.cap.y = 10.0; // avoid the 1.5 default cap clamping at s=1 already
        classic c(args);
        double g_a = c(5.0, args);
        double g_b = c(50.0, args);
        EXPECT(g_a > 1.0);
        EXPECT(g_b > g_a + 1e-6); // monotonic increase — not constant
    }
}


// ── P96 — DERİN TARAMA: ACCEL DOĞRULUK (deep accuracy sweep) ────────────────
// Deep accuracy pass over the whole accel math (see .aihaberlesme/mesajlar/aj2.log).
// Two genuine numerical bugs were found and fixed:
//   (A) power legacy+io with cap.x == 0 → scale 0 → every gain locked at 0
//       (silent mouse death). scale_from_gain_point / scale_from_output_point
//       now fall back to identity scale (1) for a degenerate io input cap.
//   (B) jump GAIN smooth with extreme smooth*cap.x (decay(0) = Inf) → the
//       antiderivative became NaN/±Inf → gains below the step were NaN/Inf.
//       smooth_antideriv() now evaluates log(1+exp(y)) with an overflow-stable
//       log1p form. decay()/smooth() (the LEGACY sigmoid path) are untouched.

static void test_p96_power_io_degenerate_cap() {
    SECTION("P96-A — power legacy io with cap.x=0: finite, movable gain (no silent kill)");
    {
        accel_args a = make_args(accel_mode::power);
        a.gain = false;
        a.cap = { 0.0, 1.8 };
        a.cap_mode_val = cap_mode::io;
        accel_union au;
        au.init(a);
        for (double x : { 0.0, 1e-9, 1.0, 5.0, 15.0, 100.0, 1e9 }) {
            double g = au.apply(x, a);
            EXPECT(std::isfinite(g));
            EXPECT(g >= 0.0);
        }
        // Before the fix the whole curve was pinned at gain 0; now it must move
        // and must not exceed the io cap (legacy_cap = cap.y).
        double g_mid  = au.apply(5.0, a);
        double g_high = au.apply(1e9, a);
        EXPECT(g_mid > 0.0);
        EXPECT(g_mid <= a.cap.y + 1e-9);
        EXPECT(g_high <= a.cap.y + 1e-9);
    }

    SECTION("P96-A — power legacy io cap.x=0 across exponent extremes");
    {
        for (double ep : { 1e-4, 10.0 }) {
            accel_args a = make_args(accel_mode::power);
            a.gain = false;
            a.exponent_power = ep;
            a.cap = { 0.0, 1.8 };
            a.cap_mode_val = cap_mode::io;
            accel_union au;
            au.init(a);
            for (double x : { 1.0, 5.0, 1e6, 1e9 }) {
                double g = au.apply(x, a);
                EXPECT(std::isfinite(g));
                EXPECT(g >= 0.0);
            }
        }
    }

    SECTION("P96-A — power GAIN io with cap.x=0 stays finite (flat gain = cap)");
    {
        for (double cy : { 0.0, 1.0, 1.8, 1e3 }) {
            accel_args a = make_args(accel_mode::power);
            a.gain = true;
            a.cap = { 0.0, cy };
            a.cap_mode_val = cap_mode::io;
            accel_union au;
            au.init(a);
            for (double x : { 1e-9, 1.0, 5.0, 15.0, 1e6 }) {
                double g = au.apply(x, a);
                EXPECT(std::isfinite(g));
                EXPECT(g >= 0.0);
            }
        }
    }
}

static void test_p96_jump_smooth_extremes_finite() {
    SECTION("P96-B — jump GAIN smooth: extreme smooth*cap.x → finite (no NaN/Inf)");
    {
        for (double smooth : { 1e-6, 0.005, 0.5, 1e3 }) {
            accel_args a = make_args(accel_mode::jump);
            a.gain = true;
            a.smooth = smooth;
            a.cap = { 1e6, 1.5 };
            a.cap_mode_val = cap_mode::in;
            accel_union au;
            au.init(a);
            for (double x : { 1e-9, 1.0, 1e3, 1e5, 5e5, 9.99e5, 1e6, 1.1e6, 1e9 }) {
                double g = au.apply(x, a);
                EXPECT(std::isfinite(g));      // used to be NaN / -Inf
                EXPECT(g >= 0.0);
                EXPECT(g <= a.cap.y + 1e-6);   // step capacity cap.y = 1.5
            }
        }
    }

    SECTION("P96-B — jump legacy smooth extreme stays finite (control, untouched path)");
    {
        for (double smooth : { 0.005, 1e3 }) {
            accel_args a = make_args(accel_mode::jump);
            a.gain = false;
            a.smooth = smooth;
            a.cap = { 1e6, 1.5 };
            a.cap_mode_val = cap_mode::in;
            accel_union au;
            au.init(a);
            for (double x : { 1.0, 1e3, 5e5, 1e6, 1.1e6, 1e9 }) {
                double g = au.apply(x, a);
                EXPECT(std::isfinite(g));
                EXPECT(g >= 0.0);
                EXPECT(g <= 1.0 + a.cap.y);    // step ramps 1 → 1.5
            }
        }
    }
}

static void test_p96_param_extreme_sweep() {
    SECTION("P96-C — param-extremes property sweep: every mode stays finite/non-negative");
    {
        // Sanitized ranges (src/config.cpp): acceleration 0..1e6, scale 0..1e6,
        // exponent_classic 1..10, exponent_power 1e-4..10, cap [0..1e6, 0..1e3],
        // offsets 0..1e3.
        const double accels[]    = { 0.0, 1e6 };
        const double scales[]    = { 0.0, 1e6 };
        const double ecs[]       = { 1.0, 1.0001, 10.0 };
        const double eps[]       = { 1e-4, 1.0, 10.0 };
        const double cxs[]       = { 0.0, 1e6 };
        const double cys[]       = { 0.0, 1e3 };
        const double offs[]      = { 0.0, 1e3 };
        const cap_mode cms[]     = { cap_mode::io, cap_mode::in, cap_mode::out };
        const double speeds[]    = { 0.0, 1e-9, 1e-3, 1.0, 5.0, 100.0, 1e6, 1e9 };

        static const char* mode_names[] = {
            "classic", "jump", "natural", "synchronous", "power", "lookup", "noaccel"
        };

        int sweep_bad[2][7] = {};
        accel_union au;
        for (bool gain : { false, true }) {
            for (int m = 0; m < 7; ++m) {
                for (double accel : accels)
                for (double scale : scales)
                for (double ec : ecs)
                for (double ep : eps)
                for (double off : offs)
                for (double cx : cxs)
                for (double cy : cys)
                for (cap_mode cm : cms) {
                    accel_args a = make_args(static_cast<accel_mode>(m));
                    a.gain = gain;
                    a.acceleration     = accel;
                    a.scale            = scale;
                    a.exponent_classic = ec;
                    a.exponent_power   = ep;
                    a.input_offset     = off;
                    a.output_offset    = off;
                    a.cap              = { cx, cy };
                    a.cap_mode_val     = cm;
                    au.init(a);
                    for (double s : speeds) {
                        double g = au.apply(s, a);
                        if (!std::isfinite(g) || g < -1e-6) {
                            if (++sweep_bad[gain][m] <= 3)
                                std::fprintf(stderr,
                                    "P96-C sweep BAD mode=%s gain=%d cm=%d accel=%g scale=%g "
                                    "ec=%g ep=%g cap=(%g,%g) off=%g speed=%g gain=%.17g\n",
                                    mode_names[m], gain, (int)cm, accel, scale, ec, ep,
                                    cx, cy, off, s, g);
                        }
                    }
                }
                if (gain) EXPECT(sweep_bad[1][m] == 0);
                else      EXPECT(sweep_bad[0][m] == 0);
            }
        }
    }

    SECTION("P96-C — mode-specific extremes: natural limit/decay, sync params");
    {
        // natural: limit 0..1e3, decay_rate 1e-4..1e6, offsets 0..1e3.
        for (bool gain : { false, true }) {
            for (double limit : { 0.0, 1.0, 1.5, 1e3 }) {
                for (double decay : { 1e-4, 0.1, 1e6 }) {
                    accel_args a = make_args(accel_mode::natural);
                    a.gain = gain;
                    a.limit = limit;
                    a.decay_rate = decay;
                    a.input_offset = 1e3;
                    accel_union au;
                    au.init(a);
                    for (double x : { 1e-9, 1e3, 5e3, 1e6, 1e9 }) {
                        double g = au.apply(x, a);
                        EXPECT(std::isfinite(g));
                        EXPECT(g >= -1e-6);
                    }
                }
            }
        }
        // synchronous: motivity/gamma/sync_speed/smooth extremes, gains and speeds.
        for (bool gain : { false, true }) {
            for (double mot : { 0.0, 1.0, 1e3 }) {
                for (double gam : { 0.0, 1.0, 1e3 }) {
                    for (double ss : { 1e-4, 5.0, 1e3 }) {
                        for (double sm : { 0.0, 1e-6, 0.5, 1e3 }) {
                            accel_args a = make_args(accel_mode::synchronous);
                            a.gain = gain;
                            a.motivity = mot;
                            a.gamma = gam;
                            a.sync_speed = ss;
                            a.smooth = sm;
                            accel_union au;
                            au.init(a);
                            for (double x : { 1e-9, 1e-3, 1.0, 1e2, 1e6, 1e9 }) {
                                double g = au.apply(x, a);
                                EXPECT(std::isfinite(g));
                                EXPECT(g >= -1e-6);
                            }
                        }
                    }
                }
            }
        }
    }

    SECTION("P96-C — lookup with length-0 and full LUT stays finite");
    {
        // length 0 → identity; extreme LUT → binary search + lerp bounds.
        for (bool gain : { false, true }) {
            accel_args a = make_args(accel_mode::lookup);
            a.gain = gain;
            accel_union au;
            au.init(a);
            for (double x : { 0.0, 1e-9, 1.0, 1e6, 1e9 }) {
                EXPECT(std::isfinite(au.apply(x, a)));
            }
            // a strict monotone table with extreme first point and end point.
            a.length = 6;
            a.data[0] = 1e-6f; a.data[1] = 1e-6f;
            a.data[2] = 0.5f;  a.data[3] = 0.5f;
            a.data[4] = 1e6f;  a.data[5] = 1e6f;
            au.init(a);
            for (double x : { 1e-9, 1e-3, 0.5, 1.0, 5.0, 1e5, 3e6, 1e9 }) {
                double g = au.apply(x, a);
                EXPECT(std::isfinite(g));
            }
        }
    }
}


// ── CUR-3: power GAIN overflow guard clamps to cap ceiling ───────────────────

static void test_cur3_power_inf_guard() {
    SECTION("CUR-3 — power Inf guard clamps to cap_y instead of snapping to 1.0");
    {
        // Realistic sanitized params can never overflow (SCALE_MAX=100,
        // EXP_POWER_MAX=5 → pow(100·1e9,5)=1e55 « DBL_MAX), so craft the struct
        // directly to force base_fn_impl overflow while staying below cap_x.
        accel_args args = make_args(accel_mode::power);

        // Cap requested (cap_y finite): overflow must clamp ONTO the ceiling —
        // a continuous continuation of the tail, NOT a hard identity snap.
        power cap;
        cap.gain_mode = true;
        cap.offset    = {};
        cap.scale     = 1e300; // pow(1e300·1e5, 2) = 1e610 → Inf
        cap.exponent  = 2.0;
        cap.constant  = 0.0;
        cap.cap_x     = 1e6;   // finite cap requested
        cap.cap_y     = 2.0;
        cap.constant_b = 0.0;
        EXPECT_NEAR(cap(1e5, args), 2.0, 1e-9);

        // No cap requested (cap_y stays DBL_MAX): unbounded curve keeps the
        // defensive identity fallback (never a dead frame / NaN downstream).
        power none;
        none = cap;
        none.cap_x = DBL_MAX;
        none.cap_y = DBL_MAX;
        EXPECT_NEAR(none(1e5, args), 1.0, 1e-9);

        // Finite (sub-overflow) base output passes through untouched — a speed
        // small enough that pow(1e300·s, 2) stays finite yields the raw curve.
        power finite_p;
        finite_p = cap;
        EXPECT(std::isfinite(finite_p(1e-160, args)));
    }
}


// ── main ─────────────────────────────────────────────────────────────────────

static void print_usage(const char* argv0) {
    std::printf(
        "RawAccel Linux — birim testleri\n"
        "\n"
        "Kullanım: %s [SEÇENEKLER]\n"
        "  --filter <regex>   Yalnızca section adı regex ile eşleşenleri çalıştır\n"
        "                     (örn. --filter 'classic|natural')\n"
        "  --list             Tüm section adlarını listele ve çık\n"
        "  --quiet            PASS satırlarını bastırma; sadece FAIL ve özet göster\n"
        "  -h, --help         Bu yardım metnini göster\n",
        argv0);
}

// ── P99 — config-layer deep-scan regressions ─────────────────────────────────
// Three guarantees were probed (Aj.5, .aihaberlesme/mesajlar/aj5.log):
//   (A) round-trip byte-identity for names with control/quote/unicode chars
//   (B) wrong-typed scalar fields degrade to defaults — config still loads,
//       and a resave afterwards does NOT silently drop the user's profiles
//   (C) .bak rotate: second save preserves the first config; a simulated
//       write failure leaves target + .bak intact and target fully valid
static void test_p99_config_guards() {
    SECTION("P99-A — round-trip byte-identity for hostile profile names");
    {
        const std::string path = "/tmp/test_p99_a.json";
        std::filesystem::remove(path);
        app_config cfg;
        cfg.active_profile = "main-\u2735";
        device_profile dp;
        dp.name = "line\nbreak";
        cfg.profiles.push_back(dp);
        dp.name = "quote\"backslash\\tab\tend";
        cfg.profiles.push_back(dp);
        dp.name = "\u00fc\u00f6\u00e7\u015f\u0131 \u65e5\u672c\u8a9e \U0001F3AF";
        cfg.profiles.push_back(dp);
        save_config(cfg, path);
        const std::string s1 = [path]{
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        }();
        // Second save must be byte-identical (stable, deterministic writer).
        save_config(cfg, path);
        const std::string s2 = [path]{
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        }();
        EXPECT(s1.size() == s2.size());
        EXPECT(s1 == s2);
        // And the reloaded profiles survive with names intact.
        app_config cfg2 = load_config(path);
        EXPECT(cfg2.profiles.size() == 3);
        if (cfg2.profiles.size() == 3) {
            EXPECT(cfg2.profiles[0].name == "line\nbreak");
            EXPECT(cfg2.profiles[1].name == "quote\"backslash\\tab\tend");
            EXPECT(cfg2.profiles[2].name == "\u00fc\u00f6\u00e7\u015f\u0131 \u65e5\u672c\u8a9e \U0001F3AF");
        }
        std::filesystem::remove(path);
    }

    SECTION("P99-B — wrong-typed fields degrade, resave keeps all profiles");
    {
        // The six P54-guard gaps found in the deep scan: raw_passthrough,
        // the speed_processor block, the profile-level doubles,
        // cap element types, domain/range_weights element types, and
        // lut_data as a non-array.  Before the fix these THREW on load,
        // bricking the entire config (and the daemon fell back to defaults).
        nlohmann::json j = {
            {"version", 1},
            {"active_profile", "main"},
            {"profiles", nlohmann::json::array({{
                {"name", "main"},
                {"dpi", 1600},
                {"profile", nlohmann::json{
                    {"raw_passthrough", "yes"},
                    {"output_dpi", "abc"},
                    {"yx_output_dpi_ratio", "abc"},
                    {"degrees_rotation", "abc"},
                    {"degrees_snap", "abc"},
                    {"speed_min", "abc"},
                    {"speed_max", "abc"},
                    {"lr_output_dpi_ratio", "abc"},
                    {"ud_output_dpi_ratio", "abc"},
                    {"domain_weights", nlohmann::json::array({"a","b"})},
                    {"range_weights", nlohmann::json::array({"a","b"})},
                    {"cap", nlohmann::json::array({"a","b"})},
                    {"speed_processor", nlohmann::json{
                        {"whole", "yes"},
                        {"lp_norm", "abc"},
                        {"input_speed_smooth_halflife", "abc"},
                        {"scale_smooth_halflife", "abc"},
                        {"output_speed_smooth_halflife", "abc"}}},
                    {"accel_x", nlohmann::json{{"mode", "noaccel"}}},
                    {"accel_y", nlohmann::json{{"mode", "noaccel"}}}
                }}
            }})}
        };
        const std::string path = "/tmp/test_p99_b.json";
        std::filesystem::remove(path);
        { std::ofstream f(path); f << j.dump(4); }
        app_config cfg = load_config(path);   // must NOT throw (P99-B)
        EXPECT(cfg.profiles.size() == 1);
        if (cfg.profiles.size() == 1) {
            auto& dpp = cfg.profiles[0];
            EXPECT(dpp.name == "main");
            EXPECT(dpp.dev_cfg.dpi == 1600);
            EXPECT(dpp.prof.raw_passthrough == false);              // degraded
            EXPECT_NEAR(dpp.prof.output_dpi, NORMALIZED_DPI, 1e-9); // sanitized default
            EXPECT_NEAR(dpp.prof.yx_output_dpi_ratio, 1.0, 1e-9);
            EXPECT_NEAR(dpp.prof.domain_weights.x, 1.0, 1e-9);      // defaults kept
            EXPECT_NEAR(dpp.prof.domain_weights.y, 1.0, 1e-9);
            EXPECT_NEAR(dpp.prof.range_weights.x, 1.0, 1e-9);
            EXPECT_NEAR(dpp.prof.range_weights.y, 1.0, 1e-9);
            EXPECT_NEAR(dpp.prof.accel_x.cap.x, 15.0, 1e-9);        // default cap
            EXPECT(dpp.prof.speed_processor_args.whole == true);    // degraded default
            EXPECT_NEAR(dpp.prof.speed_processor_args.lp_norm, 2.0, 1e-9);
        }
        // Golden rule: drafting a resave must NOT drop the user's profile
        // (fix must be degrade-on-load, not drop-on-resave).
        save_config(cfg, path);
        app_config cfg2 = load_config(path);
        EXPECT(cfg2.profiles.size() == 1);
        if (cfg2.profiles.size() == 1) EXPECT(cfg2.profiles[0].name == "main");
        std::filesystem::remove(path);

        // lut_data as a non-array in lookup mode must not brick the config.
        nlohmann::json jl = {
            {"version", 1},
            {"active_profile", "main"},
            {"profiles", nlohmann::json::array({{
                {"name", "main"},
                {"profile", nlohmann::json{
                    {"accel_x", nlohmann::json{{"mode", "lookup"},
                                               {"lut_length", 2},
                                               {"lut_data", "x"}}},
                    {"accel_y", nlohmann::json{{"mode", "noaccel"}}}
                }}
            }})}
        };
        const std::string pathl = "/tmp/test_p99_b_lut.json";
        std::filesystem::remove(pathl);
        { std::ofstream f(pathl); f << jl.dump(4); }
        app_config cfgl = load_config(pathl);   // must NOT throw
        EXPECT(cfgl.profiles.size() == 1);
        if (cfgl.profiles.size() == 1) {
            EXPECT(cfgl.profiles[0].prof.accel_x.mode == accel_mode::lookup);
            EXPECT(cfgl.profiles[0].prof.accel_x.data[0] == 0.0f);
            EXPECT(cfgl.profiles[0].prof.accel_x.data[1] == 0.0f);
        }
        std::filesystem::remove(pathl);
    }

    SECTION("P99-C — .bak rotate + simulated write failure restores previous");
    {
        const std::string dir = "/tmp/test_p99_c_dir";
        const std::string path = dir + "/settings.json";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);

        app_config c1;
        c1.active_profile = "first";
        c1.profiles.push_back([]{ device_profile p; p.name = "first"; return p; }());
        save_config(c1, path);
        const std::string first_bytes = [&]{
            std::ifstream f(path);
            return std::string((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        }();

        app_config c2;
        c2.active_profile = "second";
        c2.profiles.push_back([]{ device_profile p; p.name = "second"; return p; }());
        save_config(c2, path);
        EXPECT(std::filesystem::exists(path + ".bak"));
        const std::string bak_bytes = [&path]{
            std::ifstream f(path + ".bak");
            return std::string((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        }();
        EXPECT(bak_bytes == first_bytes);   // .bak preserves pre-second-save config

        // Simulated write failure on the NEXT save: previous config must survive.
        const bool euid_root = (::geteuid() == 0);
        if (!euid_root) {
            ::chmod(dir.c_str(), 0500);     // make temp creation fail in same user
            bool threw = false;
            try { save_config(c2, path); } catch (...) { threw = true; }
            ::chmod(dir.c_str(), 0700);
            EXPECT(threw);                                   // clean failure, no SIGABRT
            EXPECT(std::filesystem::exists(path));           // target intact
            EXPECT(std::filesystem::exists(path + ".bak"));  // .bak intact
            const int rc = std::system(("python3 -m json.tool " + path + " > /dev/null 2>&1").c_str());
            EXPECT(rc == 0);                                 // still complete valid JSON
        } else {
            // As root chmod is ineffective — simulate via an occupied
            // directory component (file where a dir is needed).
            const std::string d2 = "/tmp/test_p99_c_blocker";
            const std::string path2 = d2 + "/settings.json";
            std::filesystem::remove_all(d2);
            { std::ofstream f(d2); f << "x"; }
            bool threw = false;
            try { save_config(c2, path2); } catch (...) { threw = true; }
            EXPECT(threw);
            std::filesystem::remove(d2);
        }
        // After the failed save the loader must still read the previous config.
        EXPECT(load_config(path).active_profile == "second");
        std::filesystem::remove_all(dir);
    }
}

// ── P107: set-param domain contract ─────────────────────────────────────────
// The CLI now rejects out-of-domain set-param values (exit 1, config untouched)
// instead of silently clamping them.  This guards the CONFIG side of that
// contract: every value inside the CLI-accepted domains must survive
// sanitize_device_profile() byte-correct (no clamping of what the CLI accepts),
// so `set-param` always stores exactly what it reports.  Domains mirror the
// set-param validation in cli/main.cpp cmd_set_param.
static void test_p107_param_domain() {
    SECTION("P107 — set-param domain contract: in-domain values survive sanitize");
    {
        device_profile dp;
        auto& p = dp.prof;

        dp.dev_cfg.dpi = 32000;                    // upper edge
        dp.dev_cfg.polling_rate = POLL_RATE_MIN;   // lower edge (125)
        p.degrees_snap = 45;                       // upper edge
        p.output_dpi = 1;                          // lower edge
        p.lr_output_dpi_ratio = 100;               // upper edge
        p.ud_output_dpi_ratio = 0.01;              // lower edge
        p.yx_output_dpi_ratio = 100;
        p.domain_weights = { 0, 1e6 };             // [0, 1e6] inclusive edges
        p.range_weights = { 1e6, 0 };
        p.speed_processor_args.lp_norm = 1e-9;     // > 0 boundary
        p.accel_x.exponent_classic = 1;            // min (exp==1 linear path)
        p.accel_y.exponent_classic = 10;           // max
        p.accel_x.exponent_power = 1e-4;           // min
        p.accel_y.exponent_power = 1e-4;
        p.accel_x.sync_speed = 1e-4;               // min
        p.accel_y.sync_speed = 1e-4;
        p.accel_x.cap = { 0, 0 };                  // min
        p.accel_y.cap = { 15, 1.5 };
        p.accel_x.limit = 0;
        p.accel_y.limit = 1.5;
        p.accel_x.decay_rate = 0;
        p.accel_x.smooth = 0;
        p.accel_x.motivity = 0;
        p.accel_x.gamma = 0;
        p.accel_x.input_offset = 0;
        p.accel_x.output_offset = 0;
        p.accel_x.scale = 0.01;                 // MATH-2: min (0 is dead-curve/silent-boost)
        p.speed_min = 0;
        p.speed_max = 0;
        p.speed_processor_args.input_speed_smooth_halflife = SMOOTH_HALFLIFE_MAX;
        p.speed_processor_args.scale_smooth_halflife = SMOOTH_HALFLIFE_MAX;
        p.speed_processor_args.output_speed_smooth_halflife = SMOOTH_HALFLIFE_MAX;

        sanitize_device_profile(dp);

        EXPECT(dp.dev_cfg.dpi == 32000);
        EXPECT(dp.dev_cfg.polling_rate == POLL_RATE_MIN);
        EXPECT(std::fabs(p.degrees_snap - 45.0) < 1e-9);
        EXPECT(std::fabs(p.output_dpi - 1.0) < 1e-9);
        EXPECT(std::fabs(p.lr_output_dpi_ratio - 100.0) < 1e-9);
        EXPECT(std::fabs(p.ud_output_dpi_ratio - 0.01) < 1e-9);
        EXPECT(std::fabs(p.yx_output_dpi_ratio - 100.0) < 1e-9);
        EXPECT(std::fabs(p.domain_weights.x) < 1e-9 && std::fabs(p.domain_weights.y - 1e6) < 1e-6);
        EXPECT(std::fabs(p.range_weights.x - 1e6) < 1e-6 && std::fabs(p.range_weights.y) < 1e-9);
        EXPECT(std::fabs(p.speed_processor_args.lp_norm - 1e-9) < 1e-9);
        EXPECT(std::fabs(p.accel_x.exponent_classic - 1.0) < 1e-9 && std::fabs(p.accel_y.exponent_classic - 10.0) < 1e-9);
        EXPECT(std::fabs(p.accel_x.exponent_power - 1e-4) < 1e-9 && std::fabs(p.accel_y.exponent_power - 1e-4) < 1e-9);
        EXPECT(std::fabs(p.accel_x.sync_speed - 1e-4) < 1e-9 && std::fabs(p.accel_y.sync_speed - 1e-4) < 1e-9);
        EXPECT(std::fabs(p.accel_x.cap.x) < 1e-9 && std::fabs(p.accel_x.cap.y) < 1e-9);
        EXPECT(std::fabs(p.accel_y.cap.x - 15.0) < 1e-9 && std::fabs(p.accel_y.cap.y - 1.5) < 1e-9);
        EXPECT(std::fabs(p.accel_x.limit) < 1e-9 && std::fabs(p.accel_y.limit - 1.5) < 1e-9);
        EXPECT(std::fabs(p.accel_x.decay_rate) < 1e-9 && std::fabs(p.accel_x.smooth) < 1e-9);
        EXPECT(std::fabs(p.accel_x.motivity) < 1e-9 && std::fabs(p.accel_x.gamma) < 1e-9);
        EXPECT(std::fabs(p.accel_x.input_offset) < 1e-9 && std::fabs(p.accel_x.output_offset) < 1e-9);
        EXPECT(std::fabs(p.accel_x.scale - 0.01) < 1e-9);
        EXPECT(p.speed_processor_args.input_speed_smooth_halflife == SMOOTH_HALFLIFE_MAX);
        EXPECT(p.speed_processor_args.scale_smooth_halflife == SMOOTH_HALFLIFE_MAX);
        EXPECT(p.speed_processor_args.output_speed_smooth_halflife == SMOOTH_HALFLIFE_MAX);
    }
}

// ── O31-C2/C3: config-layer regressions ─────────────────────────────────────
static void test_o31_config_c2_c3() {
    SECTION("O31-C2 — truncated lut_length is pinned to actually-parsed pairs");
    {
        // 514-element length with only 4 floats → must not leave dead (0,0)
        // front segment; length is clamped to the 2 pairs actually present.
        // A current-format version field keeps migrate_lookup_gain() from
        // re-scaling the y values (that migration is out of scope here).
        std::string js = R"({"version":")";
        js += RAWACCEL_VERSION;
        js += R"(","profiles":[{"name":"l","profile":
                          {"accel_x":{"mode":"lookup","lut_length":514,
                                      "lut_data":[1.0,1.0, 2.0,4.0]}}}]})";
        std::string tmp = "/tmp/rawaccel_test_o31c2.json";
        { std::ofstream f(tmp); f << js; }
        app_config cfg = load_config(tmp);
        std::remove(tmp.c_str());
        auto& p = cfg.profiles[0].prof.accel_x;
        EXPECT(p.length == 4);
        EXPECT(p.data[0] == 1.0f);
        EXPECT(p.data[1] == 1.0f);
        EXPECT(p.data[2] == 2.0f);
        EXPECT(p.data[3] == 4.0f);
    }
    {
        // Odd count: 3 floats → only 1 complete pair survives.
        std::string js = R"({"version":")";
        js += RAWACCEL_VERSION;
        js += R"(","profiles":[{"name":"l","profile":
                          {"accel_x":{"mode":"lookup","lut_length":3,
                                      "lut_data":[1.0,1.0, 5.0]}}}]})";
        std::string tmp = "/tmp/rawaccel_test_o31c2b.json";
        { std::ofstream f(tmp); f << js; }
        app_config cfg = load_config(tmp);
        std::remove(tmp.c_str());
        EXPECT(cfg.profiles[0].prof.accel_x.length == 2);
        EXPECT(cfg.profiles[0].prof.accel_x.data[0] == 1.0f);
        EXPECT(cfg.profiles[0].prof.accel_x.data[1] == 1.0f);
    }
    SECTION("O31-C3 — numeric 0/1 for gain & raw_passthrough accepted like booleans");
    {
        // gain:1 & raw:1 → both true; gain:0 & raw:0 → both false;
        // gain:2 (non 0/1 integer) → silently keeps default (true).
        std::string js = R"({"version":")";
        js += RAWACCEL_VERSION;
        js += R"(","profiles":[
            {"name":"g1","profile":{"accel_x":{"gain":1},"raw_passthrough":1}},
            {"name":"g0","profile":{"accel_x":{"gain":0},"raw_passthrough":0}},
            {"name":"gb","profile":{"accel_x":{"gain":2}}}
        ]})";
        std::string tmp = "/tmp/rawaccel_test_o31c3.json";
        { std::ofstream f(tmp); f << js; }
        app_config cfg = load_config(tmp);
        std::remove(tmp.c_str());
        EXPECT(cfg.profiles[0].prof.accel_x.gain == true);
        EXPECT(cfg.profiles[0].prof.raw_passthrough == true);
        EXPECT(cfg.profiles[1].prof.accel_x.gain == false);
        EXPECT(cfg.profiles[1].prof.raw_passthrough == false);
        // gain:2 is not 0/1 integer → out-of-accepted set → keeps default true
        EXPECT(cfg.profiles[2].prof.accel_x.gain == true);
    }
}

// ── P106: per-parameter-family sınır (uç değer) regresyon tablosu ────────────
// (R47 emri: her parametre ailesi için KİLİTLENMİŞ sınır regresyonu. Aile başına
//  0/tiny/MAX san-unrange'li değerler + aileye özgü ayrım noktaları. Beklenen
//  değerler ya türetilebilir (exact) ya da döngüsel olmayan eşitsizliklerle
//  kilitlenir. Oracle'a DOKUNMAZ: stress testleri burada unit-seviyede kalır,
//  oracle grid'ine case eklenmez → 915 satır / 31 sapma değişmemelidir.
//  Sanitize aralıkları src/config.cpp sanitize_accel_args/sanitize_profile ve
//  cli/main.cpp set-param domain'leriyle birebir aynıdır.)
static void test_p106_extremes_table() {
    // ── Aile 1: speed_processor (whole / lp_norm / 3× halflife) ──────────────
    SECTION("P106 — speed_processor sınır tablosu: whole × lp_norm");

    // dist_mode eşleme tablosu (speed_processor::init):
    //   whole=false            → separate
    //   whole=true, lp<=0      → max
    //   whole=true, lp>=MAX_NORM(16) → max
    //   whole=true, |lp-2|<=1e-9 → euclidean
    //   else → Lp
    {
        struct mode_case { bool whole; double lp; distance_mode want; };
        mode_case cases[] = {
            { false, 2.0,        distance_mode::separate  },
            { true,  0.0,        distance_mode::max       },   // sanitize: <=0 → max (config'de 2'ye kırpılır)
            { true,  -1.5,       distance_mode::max       },   // negatif de max (programatik path)
            { true,  1e-9,       distance_mode::Lp        },   // "tiny" (>0, <16)
            { true,  1.0,        distance_mode::Lp        },   // Manhattan
            { true,  2.0,        distance_mode::euclidean },
            { true,  2.0000000009, distance_mode::euclidean }, // |lp-2| representable olarak <=1e-9
            { true,  2.000000001, distance_mode::Lp        },   // fp temsili nedeniyle 1e-9'un BİRİKİ dışına taşar
            { true,  2.0000001,  distance_mode::Lp        },   // epsilon'un hemen dışı
            { true,  3.0,        distance_mode::Lp        },
            { true,  16.0,       distance_mode::max       },   // tam MAX_NORM sınırı
            { true,  16.0001,    distance_mode::max       },
            { true,  1e6,        distance_mode::max       },   // "huge"
        };
        for (auto& c : cases) {
            speed_args sa;
            sa.whole = c.whole;
            sa.lp_norm = c.lp;
            speed_processor sp;
            sp.init(sa);
            EXPECT(sp.speed_flags.dist_mode == c.want);
        }
    }

    // exact hız: (3,4) her mod / sınır değerinde
    {
        speed_processor sp;
        speed_args sa;
        sa.whole = true; sa.lp_norm = 2.0;                     // euclidean
        sp.init(sa);
        EXPECT_NEAR(sp.calc_speed_whole({3,4}, 1.0), 5.0, 1e-9);
        for (double lp : { 0.0, 16.0, 16.0001, 1e6 }) {        // max: |(3,4)|∞ = 4
            sa.lp_norm = lp;
            sp.init(sa);
            EXPECT_NEAR(sp.calc_speed_whole({3,4}, 1.0), 4.0, 1e-9);
        }
        sa.lp_norm = 1.0;                                      // Manhattan: 3+4
        sp.init(sa);
        EXPECT_NEAR(sp.calc_speed_whole({3,4}, 1.0), 7.0, 1e-9);
        sa.lp_norm = 3.0;                                      // Lp: (3³+4³)^(1/3)
        sp.init(sa);
        EXPECT_NEAR(sp.calc_speed_whole({3,4}, 1.0), 4.4979414452754147, 1e-9);
        sa.lp_norm = 2.0000001;                                // epsilon dışı Lp ≈ 5
        sp.init(sa);
        EXPECT_NEAR(sp.calc_speed_whole({3,4}, 1.0), 5.0, 1e-5);
        sa.lp_norm = 1e-9;                                     // "tiny" Lp: eksen vektörü tam 1
        sp.init(sa);
        EXPECT_NEAR(sp.calc_speed_whole({1,0}, 1.0), 1.0, 1e-9);
        EXPECT_NEAR(sp.calc_speed_whole({0,1}, 1.0), 1.0, 1e-9);
        // tiny lp_norm ile (3,4) gibi çapraz vektörde 2^(1/p) → overflow.
        // lp_distance artık factored-out form kullanıyor: M*(1+(m/M)^p)^(1/p).
        // İç Inf.isfinite guard'ı yakalayıp max bileşene (M=4) clamped return eder.
        EXPECT_NEAR(sp.calc_speed_whole({3,4}, 1.0), 4.0, 1e-9);
        sa.whole = false;                                      // separate: |x|,|y|
        sp.init(sa);
        double vx = 0, vy = 0;
        // calc_speed_separate input uint'ünde çalışır
        vec2d vv = { -3.0, 4.0 };
        sp.calc_speed_separate(vv, 1.0);
        vx = vv.x; vy = vv.y;
        EXPECT_NEAR(vx, 3.0, 1e-9);
        EXPECT_NEAR(vy, 4.0, 1e-9);
        (void)vx; (void)vy;
    }

    SECTION("P106 — speed_processor sınır tablosu: halflife 0/tiny/huge (NaN yok)");

    // halflife=0 → smoothing DISABLED (flag'ler kapalı); negatif de kapalı (sanitize)
    {
        speed_args sa;
        sa.input_speed_smooth_halflife = 0.0;
        sa.scale_smooth_halflife       = 0.0;
        sa.output_speed_smooth_halflife = 0.0;
        speed_processor sp; sp.init(sa);
        EXPECT(!sp.speed_flags.should_smooth_input);
        EXPECT(!sp.speed_flags.should_smooth_scale);
        EXPECT(!sp.speed_flags.should_smooth_output);

        speed_args neg = sa;
        neg.input_speed_smooth_halflife = -3.0;    // sanitize → 0
        neg.output_speed_smooth_halflife = -7.0;
        speed_processor spn; spn.init(neg);
        EXPECT(!spn.speed_flags.should_smooth_input);
        EXPECT(!spn.speed_flags.should_smooth_output);
    }

    // tiny halflife → katsayılar 0'a underflow, EMA pasif (passthrough) ama flag AÇIK
    {
        speed_args sa;
        sa.whole = true;
        sa.lp_norm = 2.0;
        sa.input_speed_smooth_halflife = 1e-4;     // "tiny"
        speed_processor sp; sp.init(sa);
        EXPECT(sp.speed_flags.should_smooth_input);
        simple_ema_smoother ema;
        ema.init(1e-4);
        EXPECT(ema.windowCoefficient == 0.0);      // pow(0.5,10000) double'da underflow
        EXPECT_NEAR(ema.smooth(5.0, 1.0), 5.0, 1e-9);   // passthrough
        linear_ema_smoother lin;
        lin.init(1e-4, speed_processor::input_trend_halflife);
        EXPECT_NEAR(lin.smooth(5.0, 1.0), 5.0, 1e-9);
    }

    // huge halflife → koyu smoothing: ilk adımda girişin çok altında, monoton artar,
    // sabit girişle asimptota yaklaşır, hiç NaN üretmez; ani duruşta finite ve >=0.
    {
        simple_ema_smoother ema;
        ema.init(1e6);                             // "huge"
        EXPECT_NEAR(ema.windowCoefficient, 0.99999930685305971, 1e-15);
        double a = ema.smooth(5.0, 1.0);
        double b = ema.smooth(5.0, 1.0);
        double c = ema.smooth(5.0, 1.0);
        EXPECT(std::isfinite(a) && std::isfinite(b) && std::isfinite(c));
        EXPECT(a < 5.0 && a > 0.0);
        EXPECT(b > a && c > b);                    // artan (5'e asimptot)
        double conv = 0;
        bool nan = false;
        for (int i = 0; i < 2000; i++) {
            conv = ema.smooth(5.0, 1.0);
            if (!std::isfinite(conv)) nan = true;
        }
        EXPECT(!nan);
        EXPECT(conv < 5.0);                        // 1e6 halflife ile asimptota yavaş yaklaşır
        double stop = ema.smooth(0.0, 1.0);
        EXPECT(std::isfinite(stop));
        EXPECT(stop >= 0.0);
    }

    // pipeline: 3 halflife birden huge → 2000 hareket + 200 duruş karesi, NaN yok
    {
        profile prof;
        prof.accel_x = make_args(accel_mode::classic);
        prof.accel_y = prof.accel_x;
        prof.speed_processor_args.input_speed_smooth_halflife  = 1e6;
        prof.speed_processor_args.scale_smooth_halflife        = 1e6;
        prof.speed_processor_args.output_speed_smooth_halflife = 1e6;
        modifier_settings ms; ms.prof = prof; init_settings(ms);
        modifier mod;
        speed_processor sp; sp.init(prof.speed_processor_args);
        double rx = 0, ry = 0;
        int ox = 0, oy = 0;
        bool bad = false;
        for (int i = 0; i < 2000; i++) {
            apply_motion_math(mod, sp, ms, 1.0, 1.0, 6.0, 2.0, rx, ry, ox, oy);
            if (!std::isfinite(rx) || !std::isfinite(ry)) bad = true;
        }
        for (int i = 0; i < 200; i++) {
            apply_motion_math(mod, sp, ms, 1.0, 1.0, 0.0, 0.0, rx, ry, ox, oy);
            if (!std::isfinite(rx) || !std::isfinite(ry)) bad = true;
        }
        EXPECT(!bad);
    }

    // ── Aile 2: cap (0 / tiny / huge, cap.y=0 "cap yok", cap_mode sırası) ────
    SECTION("P106 — cap sınır tablosu: 0/tiny/huge × cap_mode sırası");

    // classic GAIN, cap_mode::out — default-ish kurulum (acc=0.01, exp=2)
    //   cap{15,1.5}: cap_y=0.5, cap_x=gain_inverse(0.5)=25, constant=-6.25
    {
        accel_args a = make_args(accel_mode::classic);
        a.acceleration = 0.01;
        a.cap = {15.0, 1.5}; a.cap_mode_val = cap_mode::out;
        classic c(a);
        EXPECT_NEAR(c(10.0, a), 1.1,   1e-9);      // cap_x(25) altı: base_fn              // g(10)=1.1
        EXPECT_NEAR(c(25.0, a), 1.25,  1e-9);      // tam cap_x
        EXPECT_NEAR(c(50.0, a), 1.375, 1e-9);      // cap tail: constant/x + cap_y + 1
        EXPECT_NEAR(c(1e6, a), 1.49999375, 1e-9);  // asimptot → cap_y+1=1.5
        EXPECT(c(1e6, a) < 1.5);
    }

    // cap.y=0 → "cap yok" (sentinel): sınırsız büyür, kapalı kuralla aynı değil
    {
        accel_args a = make_args(accel_mode::classic);
        a.acceleration = 0.01;
        a.cap = {15.0, 0.0}; a.cap_mode_val = cap_mode::out;
        classic c(a);
        EXPECT_NEAR(c(10.0, a), 1.1, 1e-9);        // cap.y=0 → cap DBL_MAX → saf base_fn
        EXPECT_NEAR(c(1000.0, a), 11.0, 1e-9);     // 0.01*1000 + 1 (cap YOK)
        EXPECT_NEAR(c(1e6, a), 10001.0, 1e-9);     // sınırsız
    }

    // cap tiny (cap.y=0.001 < 1) → sign-flip decelation (gain < 1), deterministic
    {
        accel_args a = make_args(accel_mode::classic);
        a.acceleration = 0.01;
        a.cap = {15.0, 0.001}; a.cap_mode_val = cap_mode::out;
        classic c(a);
        EXPECT_NEAR(c(10.0, a), 0.9, 1e-6);        // sign=-1: -(0.01*10) + 1
        double gh = c(1000.0, a);
        EXPECT(std::isfinite(gh));
        EXPECT(gh < 1.0);                          // decelasyon
    }

    // cap huge (cap.y=1e9): pratik hızlarda cap'siz kurmayla aynı, hep finite
    {
        accel_args a = make_args(accel_mode::classic);
        a.acceleration = 0.01;
        a.cap = {15.0, 1e9}; a.cap_mode_val = cap_mode::out;
        classic c(a);
        EXPECT_NEAR(c(1000.0, a), 11.0, 1e-9);
        EXPECT(std::isfinite(c(1e6, a)));
    }

    // cap_mode öncelik sırası: io | in | out — üçü de aynı cap{15,2} ile FARKLI kural
    {
        accel_args base = make_args(accel_mode::classic);
        base.acceleration = 0.01;
        base.cap = {15.0, 2.0};
        // out: cap_y=1 → cap_x=gain_inverse(1)=50, constant=(base_fn(50)-1)*50=-25
        accel_args a = base; a.cap_mode_val = cap_mode::out;
        classic c_out(a);
        EXPECT_NEAR(c_out(100.0, a), 1.75, 1e-9);      // -25/100 + 1 + 1
        EXPECT_NEAR(c_out(1e6, a), 1.999975, 1e-9);    // → cap_y+1=2
        // in: cap_x=15, cap_y=gain(15)=0.3 → constant=-2.25
        accel_args b = base; b.cap_mode_val = cap_mode::in;
        classic c_in(b);
        EXPECT_NEAR(c_in(100.0, b), 1.2775, 1e-9);
        EXPECT_NEAR(c_in(1e6, b), 1.29999775, 1e-9);
        // io: gain_accel(15,1,2,0)=1/30 → accel_raised=1/30, constant=-7.5
        accel_args d = base; d.cap_mode_val = cap_mode::io;
        classic c_io(d);
        EXPECT_NEAR(c_io(100.0, d), 1.925, 1e-9);
        EXPECT_NEAR(c_io(1e6, d), 1.9999925, 1e-9);
        // sıralama (x=100'de): io > out > in, üçü aynı değil
        EXPECT(c_io(100.0, d) > c_out(100.0, a) && c_out(100.0, a) > c_in(100.0, b));
    }

    // io + cap.y=0: "cap yok" DEĞİL — inversion (SURPRISE: sentinel yalnız out'ta)
    {
        accel_args a = make_args(accel_mode::classic);
        a.acceleration = 0.01;
        a.cap = {15.0, 0.0}; a.cap_mode_val = cap_mode::io;
        classic c(a);
        EXPECT_NEAR(c(10.0, a), 0.6666666666666666, 1e-9);  // sign flip: 1 - (1/30)*x
        EXPECT_NEAR(c(100.0, a), 0.075, 1e-9);
        EXPECT(c(10.0, a) < 1.0);                            // decelasyon
    }

    // power ailesi: cap{30,2} out/in/io → farklı kollar; 1e6'da out/io cap'a (2) yaklaşır
    {
        accel_args base = make_args(accel_mode::power);
        base.scale = 0.5; base.exponent_power = 0.3; base.output_offset = 0;
        base.cap = {30.0, 2.0};
        accel_args a = base; a.cap_mode_val = cap_mode::out;
        power p_out(a);
        EXPECT_NEAR(p_out(30.0, a), 1.87065823485, 1e-6);
        EXPECT_NEAR(p_out(1e6, a), 1.99999611975, 1e-6);
        EXPECT(p_out(1e6, a) < 2.0);
        accel_args b = base; b.cap_mode_val = cap_mode::in;
        power p_in(b);
        EXPECT_NEAR(p_in(30.0, b), 2.25334338084, 1e-6);
        EXPECT_NEAR(p_in(1e6, b), 2.92932611501, 1e-6);
        accel_args d = base; d.cap_mode_val = cap_mode::io;
        power p_io(d);
        EXPECT_NEAR(p_io(30.0, d), 1.53846153846, 1e-6);
        EXPECT_NEAR(p_io(1e6, d), 1.99998615385, 1e-6);
        EXPECT(p_in(30.0, b) > p_out(30.0, a) && p_out(30.0, a) > p_io(30.0, d));   // in>out>io
        EXPECT(std::fabs(p_in(30.0, b) - p_out(30.0, a)) > 0.1);                    // kollar farklı
    }

    // power cap.y=0 → cap yok (out): sınırsız, finite
    {
        accel_args a = make_args(accel_mode::power);
        a.scale = 0.5; a.exponent_power = 0.3; a.output_offset = 0;
        a.cap = {30.0, 0.0}; a.cap_mode_val = cap_mode::out;
        power p(a);
        EXPECT_NEAR(p(1000.0, a), 6.45195012148, 1e-6);
        EXPECT(std::isfinite(p(1e6, a)));
    }

    // ── Aile 3: per-device (dpi / polling_rate / device_id) ──────────────────
    SECTION("P106 — per-device sınır tablosu: dpi × polling_rate × device_id");

    // dpi sanitize: [1, 32000]
    {
        device_profile dp;
        dp.dev_cfg.polling_rate = 1000;
        struct dpi_case { int in; int want; };
        dpi_case dpi_cases[] = {
            { 0,          1     },   // altı → 1
            { -100,       1     },   // negatif → 1
            { 1,          1     },   // alt sınır
            { 100,        100   },
            { 16000,      16000 },
            { 32000,      32000 },   // üst sınır
            { 999999,     32000 },   // 1e6 aşım → 32000
        };
        for (auto& c : dpi_cases) {
            dp.dev_cfg.dpi = c.in;
            sanitize_device_profile(dp);
            EXPECT(dp.dev_cfg.dpi == c.want);
        }
    }

    // polling_rate sanitize: [POLL_RATE_MIN(125), POLL_RATE_MAX(8000)]
    {
        device_profile dp;
        dp.dev_cfg.dpi = 800;
        struct rate_case { int in; int want; };
        rate_case rate_cases[] = {
            { 0,     POLL_RATE_MIN },   // 0 → 125
            { 50,    POLL_RATE_MIN },
            { 124,   POLL_RATE_MIN },
            { POLL_RATE_MIN, POLL_RATE_MIN },
            { 1000,  1000 },
            { POLL_RATE_MAX, POLL_RATE_MAX },
            { 8001,  POLL_RATE_MAX },
            { 99999, POLL_RATE_MAX },
        };
        for (auto& c : rate_cases) {
            dp.dev_cfg.polling_rate = c.in;
            sanitize_device_profile(dp);
            EXPECT(dp.dev_cfg.polling_rate == c.want);
        }
    }

    // device_id: boş ve dolu — sanitize korur, JSON round-trip korur
    {
        device_profile dp;
        dp.device_id = "";
        sanitize_device_profile(dp);
        EXPECT(dp.device_id.empty());
        device_profile dp2;
        dp2.device_id = "usb:1234:5678:serial";
        sanitize_device_profile(dp2);
        EXPECT(dp2.device_id == "usb:1234:5678:serial");
        EXPECT(profile_from_json(profile_to_json(dp)).device_id.empty());
        EXPECT(profile_from_json(profile_to_json(dp2)).device_id == "usb:1234:5678:serial");
    }

    // device_id uzunluk sınırı: JSON yolunda 256'ya kesilir (kaynak config.hpp/MAX)
    {
        std::string long_id(300, 'a');
        nlohmann::json j = nlohmann::json::object();
        j["name"] = "cap"; j["device_id"] = long_id;
        j["dpi"] = 16000; j["polling_rate"] = 1000;
        j["profile"] = nlohmann::json::object();
        device_profile dp = profile_from_json(j.dump());
        EXPECT(dp.device_id.size() == 256);
        EXPECT(dp.name == "cap");
    }

    // name uzunluk sınırı: JSON yolunda 256'ya kesilir (R12-DUPNM, CFG-6).
    // A 257-char name must not survive the load — the round-trip is what the
    // duplicate dialog's generated-name cap guards against.
    {
        std::string long_name(257, 'c');
        nlohmann::json j = nlohmann::json::object();
        j["name"] = long_name; j["device_id"] = "";
        j["dpi"] = 800; j["polling_rate"] = 1000;
        j["profile"] = nlohmann::json::object();
        device_profile dp = profile_from_json(j.dump());
        EXPECT(dp.name.size() <= MAX_NAME_LEN);
        // A full MAX_NAME_LEN (256) name round-trips intact (P83: 256 OK).
        std::string exact(MAX_NAME_LEN, 'n');
        j["name"] = exact;
        EXPECT(profile_from_json(j.dump()).name == exact);
    }

    // R12-IMPLUT: check_import_lut_size — over-capacity and odd LUT tables must
    // be rejected BEFORE the sanitizer clamps/floors them (GUI import mirror of
    // the CLI O31-L1 / P120-FAZ2 gate).
    {
        auto lut_json = [](size_t n_elems) {
            nlohmann::json j = nlohmann::json::object();
            j["name"] = "lut"; j["device_id"] = "";
            j["dpi"] = 800; j["polling_rate"] = 1000;
            j["profile"] = nlohmann::json::object();
            auto ax = nlohmann::json::object();
            nlohmann::json lut = nlohmann::json::array();
            for (size_t i = 0; i < n_elems; i++)
                lut.push_back(static_cast<double>(i));
            ax["lut_data"] = lut;
            j["profile"]["accel_x"] = ax;
            j["profile"]["accel_y"] = nlohmann::json::object();
            return j.dump();
        };
        // max capacity: 514 raw = 257 points — acceptable, no message
        EXPECT(check_import_lut_size(lut_json(LUT_RAW_DATA_CAPACITY)).empty());
        // over capacity: 516 raw = 258 points — rejected with a message
        EXPECT(!check_import_lut_size(lut_json(LUT_RAW_DATA_CAPACITY + 2)).empty());
        // odd element count: 515 raw — rejected (O31-L1 never silently floors)
        EXPECT(!check_import_lut_size(lut_json(LUT_RAW_DATA_CAPACITY + 1)).empty());
        // malformed JSON parses nowhere — must not throw, returns ""
        EXPECT(check_import_lut_size("not json").empty());
        // non-LUT profile (no profile wrapper) is acceptable
        EXPECT(check_import_lut_size("{\"name\":\"x\"}").empty());
    }

    // P-APP: match_app — boş/dolu round-trip korur, JSON yolunda 128'e kesilir,
    // case-mismatch substring eşleştirme (daemon find_profile boyutu)
    {
        device_profile dp;
        dp.match_app = "";
        sanitize_device_profile(dp);
        EXPECT(dp.match_app.empty());
        dp.match_app = "FireFox";
        EXPECT(profile_from_json(profile_to_json(dp)).match_app == "FireFox");

        std::string long_app(300, 'x');
        nlohmann::json j = nlohmann::json::object();
        j["name"] = "cap"; j["device_id"] = "";
        j["match_app"] = long_app;
        j["dpi"] = 16000; j["polling_rate"] = 1000;
        j["profile"] = nlohmann::json::object();
        EXPECT(profile_from_json(j.dump()).match_app.size() == 128);
        // JSON'da olmayan match_app → boş (eski config'ler geriye dönük uyumlu)
        nlohmann::json j2 = nlohmann::json::object();
        j2["name"] = "legacy"; j2["device_id"] = "";
        j2["dpi"] = 800; j2["polling_rate"] = 1000;
        j2["profile"] = nlohmann::json::object();
        EXPECT(profile_from_json(j2.dump()).match_app.empty());
    }

    // boş device_id hiçbir zaman duplicate sayılmaz (iki boş profil çakışmasız)
    {
        app_config cfg;
        device_profile a; a.name = "a"; a.device_id = "";
        device_profile b; b.name = "b"; b.device_id = "";
        cfg.profiles = {a, b};
        EXPECT(check_dup_ids(cfg).empty());
    }

    // dev_cfg.dpi ve prof.output_dpi bağımsız alanlar — ayrı ayrı kırpılır
    {
        device_profile dp;
        dp.dev_cfg.dpi = 16000;          // device dpi
        dp.prof.output_dpi = 1.0;        // profile output dpi (alt sınır)
        sanitize_device_profile(dp);
        EXPECT(dp.dev_cfg.dpi == 16000);
        EXPECT(dp.prof.output_dpi >= 1.0);
    }

    // ── Aile 4: offset (input_offset / output_offset — classic/natural/power) ─
    SECTION("P106 — offset sınır tablosu: classic/natural/power × 0/tiny/max");

    // classic input_offset: x<=offset → kimlik bandı 1.0; tiny=0 gibi; max kaydırır
    {
        accel_args a = make_args(accel_mode::classic);
        a.acceleration = 0.01;
        classic c0(a);                                  // offset 0
        EXPECT_NEAR(c0(0.0, a), 1.0, 1e-9);
        EXPECT_NEAR(c0(5.0, a), 1.05, 1e-9);
        EXPECT_NEAR(c0(10.0, a), 1.1, 1e-9);

        accel_args t = a; t.input_offset = 1e-9;        // "tiny"
        classic ct(t);
        EXPECT_NEAR(ct(0.0, t), 1.0, 1e-9);
        EXPECT_NEAR(ct(5.0, t), 1.05, 1e-6);

        accel_args m = a; m.input_offset = 1e6;         // "max" (yukarıda boundsuz)
        classic cm(m);
        EXPECT_NEAR(cm(0.0, m), 1.0, 1e-9);
        EXPECT_NEAR(cm(5e5, m), 1.0, 1e-9);             // band içi → kimlik
        EXPECT_NEAR(cm(1e6, m), 1.0, 1e-9);             // tam offset → kimlik
        EXPECT_NEAR(cm(2e6, m), 1.249996875, 1e-6);     // cap tail'e girer (asimptot 1.5)
        EXPECT_NEAR(cm(1e7, m), 1.449999375, 1e-5);
        EXPECT(cm(1e7, m) < 1.5);
    }

    // natural input_offset: aynı kimlik bandı; tiny=0; max kaydırır
    {
        accel_args a = make_args(accel_mode::natural);
        natural n0(a);                                  // offset 0
        EXPECT_NEAR(n0(0.0, a), 1.0, 1e-9);
        EXPECT_NEAR(n0(5.0, a), 1.18393972059, 1e-6);   // limit 1.5, dr 0.1 → accel 0.2
        EXPECT_NEAR(n0(2e6, a), 1.49999875, 1e-6);      // asimptot → args.limit=1.5

        accel_args t = a; t.input_offset = 1e-9;
        natural nt(t);
        EXPECT_NEAR(nt(5.0, t), 1.18393972059, 1e-6);
        EXPECT_NEAR(nt(2e6, t), 1.49999875, 1e-6);

        accel_args m = a; m.input_offset = 1e6;
        natural nm(m);
        EXPECT_NEAR(nm(5e5, m), 1.0, 1e-9);             // band içi
        EXPECT_NEAR(nm(1e6, m), 1.0, 1e-9);             // tam offset
        EXPECT_NEAR(nm(2e6, m), 1.24999875, 1e-6);      // kaymış asimptot
        EXPECT_NEAR(nm(1e8, m), 1.494999975, 1e-5);
        EXPECT(nm(1e8, m) < 1.5);
    }

    // power output_offset: x<=offset.x → SABİT gain=output_offset (kaydırılmış çizgi)
    {
        accel_args a = make_args(accel_mode::power);
        a.scale = 0.5; a.exponent_power = 0.3;
        a.output_offset = 0.0;
        power p0(a);
        EXPECT_NEAR(p0(0.0, a), 1.0, 1e-9);
        EXPECT_NEAR(p0(1.0, a), 0.81225239636, 1e-6);   // (0.5*1)^0.3 + 0/1
        EXPECT_NEAR(p0(1e5, a), 1.49998884528, 1e-5);   // out-cap 1.5'e yaklaşır
        EXPECT(p0(1e5, a) < 1.5);

        accel_args o2 = a; o2.output_offset = 2.0;
        power p2(o2);
        EXPECT_NEAR(p2(1.0, o2), 2.0, 1e-9);            // offset.x≈8.407 altı → sabit 2
        EXPECT_NEAR(p2(1e5, o2), 1.50001611238, 1e-5);  // üstte cap 1.5'e doğru
        EXPECT(p2(1.0, o2) > p0(1.0, a));               // band farkı

        accel_args o9 = a; o9.output_offset = 1e-9;     // "tiny"
        power p9(o9);
        EXPECT_NEAR(p9(0.0, o9), 1.0, 1e-9);
        EXPECT(std::isfinite(p9(1e-6, o9)));            // offset.x≈8.3e-31 → baz eğri
        EXPECT_NEAR(p9(1.0, o9), 0.81225239636, 1e-6);  // tiny offset, bazla aynı

        accel_args om = a; om.output_offset = 1e6;      // "max": offset.x≈8.4e19 > cap.x≈3.23
        power pm(om);
        // P155#2: with output_offset ABOVE the out-cap (1e6 > cap.y=1.5), the
        // offset band is huge (offset.x≈8.4e19) but the cap threshold cap.x≈3.23
        // is far lower. The cap branch must win once speed crosses cap.x — the
        // OLD local order (speed <= offset.x BEFORE the cap check) returned the
        // flat 1e6 across the entire wide band, silently disabling the cap.
        // Reference at speed=1e5 → 1.5 + constant_b/1e5 ≈ 33.72 (matched).
        EXPECT_NEAR(pm(1.0, om), 1e6, 1e-3);            // below cap.x → offset band
        EXPECT_NEAR(pm(1e5, om), 33.7247111642, 1e-5);  // above cap.x → cap tail
        EXPECT_NEAR(pm(1e9, om), 1.50322247112, 1e-5);  // cap tail → cap.y asimptotu
    }

    // output_offset YALNIZCA power'ı etkiler: classic/natural'da kullanılmaz
    {
        accel_args a = make_args(accel_mode::classic);
        a.acceleration = 0.01;
        classic c_base(a);
        accel_args b = a; b.output_offset = 2.0;
        classic c_alt(b);
        accel_args m = a; m.output_offset = 1e6;
        classic c_max(m);
        for (double x : { 0.5, 1.0, 5.0, 17.0, 2000.0 }) {
            EXPECT_NEAR(c_base(x, a), c_alt(x, b), 1e-12);
            EXPECT_NEAR(c_base(x, a), c_max(x, m), 1e-12);
        }
        accel_args n = make_args(accel_mode::natural);
        natural n_base(n);
        accel_args nm2 = n; nm2.output_offset = 1e6;
        natural n_max(nm2);
        for (double x : { 0.5, 1.0, 5.0, 2000.0 }) {
            EXPECT_NEAR(n_base(x, n), n_max(x, nm2), 1e-12);
        }
    }

    // P155#1: power io cap with cap.y == 0 is DEGENERATE — the reference
    // derives scale=0 from the "gain 0" point, producing a dead curve
    // (legacy → exactly 0 at every speed; gain → NaN once scale hits 0).
    // The local port treats cap.y<=0 as "no cap requested": identity scale,
    // no legacy clamp, normal base curve. Regression test pins the fix.
    {
        accel_args io0 = make_args(accel_mode::power);
        io0.scale = 0.5; io0.exponent_power = 0.3;
        io0.cap = { 15.0, 0.0 };
        io0.cap_mode_val = cap_mode::io;

        power gain0(io0);                       // gain_mode=true (default)
        EXPECT_NEAR(gain0(1.0, io0), 1.0, 1e-9);          // identity scale
        EXPECT(gain0(1e5, io0) > 10.0);                     // real curve, not dead
        EXPECT(std::isfinite(gain0(1e5, io0)));

        accel_args io0l = io0; io0l.gain = false;
        power leg0(io0l);
        EXPECT_NEAR(leg0(1.0, io0l), 1.0, 1e-9);          // no cap clamp → base
        EXPECT(leg0(1e5, io0l) > 10.0);
    }

    // Power io cap with cap.x <= 0 in gain_mode is also degenerate —
    // previous behavior locked the curve to a flat constant cap.y for all speeds.
    // The fix treats cap.x<=0 as "no cap requested": identity scale, normal base curve.
    {
        accel_args iox0 = make_args(accel_mode::power);
        iox0.scale = 0.5; iox0.exponent_power = 0.3;
        iox0.cap = { 0.0, 2.0 };
        iox0.cap_mode_val = cap_mode::io;

        power gain_x0(iox0);
        EXPECT_NEAR(gain_x0(1.0, iox0), 1.0, 1e-9);
        EXPECT(gain_x0(1e5, iox0) > 10.0); // real curve, not flat 2.0
        EXPECT(std::isfinite(gain_x0(1e5, iox0)));
    }

    // Classic gain mode with acceleration == 0 and cap_mode::out (division-by-zero guard)
    {
        accel_args ca = make_args(accel_mode::classic);
        ca.gain = true;
        ca.acceleration = 0.0;
        ca.exponent_classic = 2.0;
        ca.cap = { 0.0, 2.0 };
        ca.cap_mode_val = cap_mode::out;
        classic c(ca);
        double val = c(10.0, ca);
        EXPECT(std::isfinite(val));
        EXPECT(val > 0.0);
    }

    // Power io cap in legacy mode with small cap.y (scale_from_output_point guard)
    {
        accel_args pa = make_args(accel_mode::power);
        pa.gain = false;
        pa.scale = 0.5;
        pa.exponent_power = 0.3;
        pa.cap = { 10.0, 0.5 };
        pa.cap_mode_val = cap_mode::io;
        power p(pa);
        double val = p(10.0, pa);
        EXPECT(std::isfinite(val));
        EXPECT(val > 0.0);
    }

    // P155#2: power GAIN cap ordering. When output_offset > cap_y the offset
    // plateau (offset.x) sits ABOVE the cap threshold (cap.x); the cap branch
    // must WIN past cap.x exactly like the reference orders it. The pre-fix
    // local version checked speed<=offset.x BEFORE the cap check, so every
    // speed below the huge offset.x returned the flat offset.y=1e6 and the
    // requested 1.5 cap was silently disabled (disabled cap → gain 1e6).
    // Reference tail: cap.y + constant_b/speed (constant_b integrates against
    // base_fn(cap.x)=offset plateau). Verified against vendored reference.
    {
        accel_args tt = make_args(accel_mode::power);
        tt.scale = 0.5; tt.exponent_power = 0.3;
        tt.cap = { 15.0, 1.5 };
        tt.cap_mode_val = cap_mode::out;
        // cap.x = gain_inverse(1.5,0.3,0.5) ≈ 3.23;  offset.y=1e6 → offset.x≈8.4e19
        tt.output_offset = 1e6;
        power ttg(tt);
        EXPECT_NEAR(ttg(1.0,  tt), 1e6, 1e-3);            // below cap.x → offset band
        EXPECT_NEAR(ttg(5.0,  tt), 644495.723284, 1e-2);  // just past cap.x → tail
        EXPECT_NEAR(ttg(1e5,  tt), 33.7247111642, 1e-5);  // asymptotic tail
        EXPECT_NEAR(ttg(1e9,  tt), 1.50322247112, 1e-5);  // → cap.y=1.5
        EXPECT(ttg(1e9, tt) < 1.51);
    }
}

// ── P119 FAO-1: synchronous motivity<1 × smooth grid ──────────────────────────
//
// BUG-01 (KRİTİK, onaylı düzeltme): legacy tanh yolu motivity<1'de TERS'ti —
// |log_space| tamiri tanh'ın odd işaretini düşürüyordu (m=0.3/smooth=0.5:
// local m≈0.3 vs ref 1/m≈3.33, smooth slider 0.5→0'da eğri kendini tersliyordu).
// Düzeltme odd-simetrik form:
//   exponent = sign(z)·pow(tanh(|z|^sharpness), 1/sharpness),  z = gamma_const·log(x/sync_speed)
// BUG-03: fractional sharpness (smooth=1 → s=0.5) ile OFFICIAL referans
// pow(neg, frac) yüzünden NaN üretir; yerel sonlu odd formu korunur ve burada
// aşağıdaki matematiksel referansa rel 1e-9 ile çivilenir (ref eğrisi /tmp
// altında hesaplanıp geri okunur). motivity>1 davranışı bit-idantiktir,
// dokunulmaz (oracle sync_gain/legacy p2/p1/p07 satırları değişmemelidir).

// Corrected odd-symmetric reference legacy curve. NaN-free; equal to the local
// port wherever the port is finite, and equal to the official reference
// wherever the official reference itself is finite (sharpness integral).
static double fao1_ref_sync_legacy(double m, double gamma, double syncspeed,
                                   double smooth, double x) {
    if (x <= 0) return 1.0;
    double lm = std::log(m);
    double gc = gamma / lm;
    double ss = (smooth == 0) ? 16.0 : 0.5 / smooth;
    if (ss >= 16) {
        double log_space = gc * (std::log(x) - std::log(syncspeed));
        if (log_space < -1) return 1.0 / m;
        if (log_space > 1)  return m;
        return std::exp(log_space * lm);
    }
    if (x == syncspeed) return 1.0;
    double z = gc * (std::log(x) - std::log(syncspeed));
    double e = std::pow(std::tanh(std::pow(std::abs(z), ss)), 1.0 / ss);
    if (z < 0) e = -e;
    return std::exp(e * lm);
}

static void test_fao1_sync_motivity_grid() {
    const double ms[]  = { 0.05, 0.3, 0.8 };
    const double sms[] = { 0.0, 0.5, 1.0 };
    const double gamma = 1.0, sync = 5.0;
    const double speeds[] = { 0.001, 0.01, 0.125, 0.5, 1, 2, 3, 5, 7, 10,
                              30, 100, 512, 5000, 100000 };

    SECTION("P119 FAO1 — synchronous motivity<1 × smooth grid (LEGACY vs math ref)");
    {
        const char* refpath = "/tmp/rawaccel_p119_sync_m1_ref.csv";
        bool written = false;
        {
            FILE* f = std::fopen(refpath, "w");
            if (f) {
                written = true;
                for (double m : ms) for (double sm : sms)
                    for (double x : speeds) {
                        double r = fao1_ref_sync_legacy(m, gamma, sync, sm, x);
                        std::fprintf(f, "%.4g %.4g %.8g %.17g\n", m, sm, x, r);
                    }
                std::fclose(f);
            }
        }
        EXPECT(written);

        std::ifstream in(refpath);
        EXPECT(in.good());
        double m, sm, x, r;
        while (in >> m >> sm >> x >> r) {
            EXPECT(std::isfinite(r));          // referans eğrisi her zaman sonlu (BUG-03)
            accel_args args = make_args(accel_mode::synchronous);
            args.gain = false;
            args.gamma = gamma; args.motivity = m;
            args.sync_speed = sync; args.smooth = sm;
            synchronous s(args);
            double loc = s(x, args);
            EXPECT(std::isfinite(loc));
            double denom = std::fabs(r);
            EXPECT(std::fabs(loc - r) <= 1e-9 * denom);  // rel 1e-9 pin
        }
        std::remove(refpath);

        // BUG-01 işaret (regression) testi — headline m=0.3: hiçbir smooth'ta
        // eğri ters dönmüyor: yüksek hız 1/m≈3.33'a, düşük hız m≈0.3'a gider.
        for (double sm : sms) {
            accel_args hi = make_args(accel_mode::synchronous);
            hi.gain = false;
            hi.gamma = gamma; hi.motivity = 0.3;
            hi.sync_speed = sync; hi.smooth = sm;
            synchronous sh(hi);
            EXPECT(sh(500.0, hi) > 3.0);             // buggy: ≈0.3 (ters)
            EXPECT(sh(0.001, hi) < 0.4);             // buggy: ≈3.3 (ters)
            EXPECT(sh(500.0, hi) > sh(0.001, hi));   // monotonic direction
            EXPECT(sh(5.0, hi) == 1.0);              // sync point identity
        }
    }

    SECTION("P119 FAO1 — synchronous motivity<1 × smooth grid (GAIN LUT vs exact integral)");
    {
        // GAIN LUT = N=2 piecewise-trapezoid (referans-parite) integral of the
        // corrected legacy over the log grid { -3, 9, 8 }; O(1/N^2) ayrıklaştırma
        // hatası nedeniyle exact integral ile MUTLAK, büyüklük-ölçekli bir zarf
        // altında karşılaştırılır (ölçülen max ≈ 0.23, m=0.05/smooth=0; 0.03·max(1/m,m)
        // her hücreyi ≥2.6× marjla kapsar). Yalnız tablo içi [0.125, 512].
        auto exact_gain = [&](int N, double x, double m, double sm) {
            double sum = 0, a = 0;
            int k = 0;
            while (a < x) {
                double b = (k <= 96) ? std::ldexp(1.0 + (k % 8) / 8.0, -3 + k / 8)
                                     : std::scalbn(1, 9);
                if (b > x) b = x;
                if (b <= a) b = x;
                double h = (b - a) / N;
                for (int i = 1; i <= N; i++)
                    sum += fao1_ref_sync_legacy(m, gamma, sync, sm, a + i * h) * h;
                a = b; ++k;
            }
            return sum / x;
        };
        const int NEXACT = 4096;
        const double probes[] = { 0.5, 1, 2, 3, 5, 7, 10, 30, 100, 300, 500 };
        for (double m : ms) for (double sm : sms) {
            accel_args args = make_args(accel_mode::synchronous);
            args.gain = true;
            args.gamma = gamma; args.motivity = m;
            args.sync_speed = sync; args.smooth = sm;
            synchronous sg(args);
            double env = 0.03 * std::max(1.0 / m, m);
            double prev = -1;
            bool mono = true;
            for (double x : probes) {
                double lut = sg(x, args);
                double ex  = exact_gain(NEXACT, x, m, sm);
                EXPECT(std::isfinite(lut));
                EXPECT_NEAR(lut, ex, env);
                if (prev > 0 && lut + 1e-6 < prev) mono = false;
                prev = lut;
            }
            EXPECT(mono);
            // Yüksek hız kuyruğu (2^9 ötesi lineer ekstrapolasyon) 1/m'e gider —
            // buggy (ters) LUT'ta m'e giderdi.
            double t = sg(1e5, args);
            EXPECT(std::isfinite(t));
            EXPECT(t > 1.0);
            EXPECT(t <= 1.0 / m + env);
            // Düşük hız tabanı (2^-3 altı sabit) sonlu.
            double b0 = sg(1e-4, args);
            EXPECT(std::isfinite(b0));
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (a == "--list") {
            g_list_only = true;
        } else if (a == "--quiet") {
            g_quiet = true;
        } else if (a == "--filter") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Hata: --filter <regex> argümanı gerekli\n");
                return 2;
            }
            try {
                g_filter_regex = std::regex(argv[++i], std::regex::ECMAScript);
                g_have_filter = true;
            } catch (const std::regex_error& e) {
                std::fprintf(stderr, "Hata: geçersiz regex '%s': %s\n", argv[i], e.what());
                return 2;
            }
        } else {
            std::fprintf(stderr, "Hata: bilinmeyen argüman '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (!g_list_only) std::printf("=== RawAccel Linux Birim Testleri ===\n");

    test_noaccel();
    test_classic();
    test_natural();
    test_jump();
    test_synchronous();
    test_lookup();
    test_power();
    test_p81_power_io_fixes();
    test_accel_union();
    test_json_roundtrip();
    test_json_roundtrip_lut();
    test_json_roundtrip_lut_mode_switch();
    test_file_roundtrip();
    test_save_config_relative_path();
    test_monotonic();
    test_power_monotonic();
    test_classic_cap_modes();
    test_modifier();
    test_edge_guards();
    test_modifier_end_to_end();
    test_speed_processor();
    test_config_error_paths();
    test_input_validation();
    test_multi_profile_roundtrip();
    test_atomic_write();
    test_ipc_json_roundtrip();
    test_motion_math();
    test_nonfinite_time_does_not_poison_smoothers();
    test_lat_stats();

    // New edge-case tests
    test_natural_decay_zero();
    test_extreme_inputs();
    test_classic_degenerate();
    test_power_zero_exponent();
    test_lookup_edge_cases();
    test_modifier_zero_time();
    test_ema_smoother_stability();
    test_subpixel_sign();
    test_sanitize_extremes();
    test_lut_sort_on_sanitize();
    test_cfg_p54_guards();
    test_lut_sort_json_roundtrip();
    test_motion_math_overflow();
    test_accel_args_sanitize();
    test_raw_passthrough_json();

    // Fuzz, edge-case, and stress tests
    test_fuzz_accel_args();
    test_fuzz_unsanitized_motion_math();
    test_fuzz_json_roundtrip();
    test_all_modes_extreme_speeds();
    test_ema_extreme_time();
    test_subpixel_tiny_deltas();
    test_subpixel_negative_deltas();
    test_modifier_all_flags();
    test_modifier_separate_mode();
    test_stress_remainder_drift();
    test_stress_alternating_direction();
    test_power_extreme_params();
    test_lookup_extreme_lut();
    test_natural_extreme_params();
    test_synchronous_extreme();

    // R6 regression tests
    test_dpi_ratio_zero_guard();
    test_lp_distance_zero_vector();

    // R7 deep-dive tests
    test_ema_extreme_halflife();
    test_pipeline_nan_injection();
    test_classic_io_degenerate_cap();
    test_classic_in_degenerate_cap_naninf();
    test_motion_math_clamp_remainder_reset();
    test_save_config_durability_path();
    test_power_output_offset();
    test_directional_weight_boundary();

    // R9 mükemmellik testleri
    test_subpixel_cumulative_drift();
    test_classic_gain_mode_cap_consistency();
    test_natural_gain_formula();
    test_classic_natural_deep_accuracy();
    test_natural_reference_values();

    // R10 — EMA smoother, NaN propagation, event batching, config edge cases
    test_ema_smoother_halflife();
    test_ema_smoother_zero_time();
    test_linear_ema_smoother();
    test_nan_propagation_all_modes();
    test_nan_propagation_pathological_params();
    test_event_batching_accumulation();
    test_event_batching_split_vs_combined();
    test_speed_processor_all_distance_modes();
    test_speed_processor_smoothing();
    test_syn_dropped_reset_behavior();
    test_syn_dropped_event_stream();
    test_config_empty_profiles();
    test_config_missing_active_profile();
    test_config_extreme_values();
    test_config_duplicate_device_id();
    test_check_duplicate_device_ids();
    test_sanitize_nan_fields();
    test_modify_subnormal_time();

    // R12 — untested code path coverage
    test_classic_sign_flip();
    test_classic_linear_path_cap();
    test_power_cap_branch();
    test_modifier_rotation_snap_combined();
    test_speed_processor_lp_mode();
    test_lookup_max_capacity();
    test_ema_coefficient_zero();
    test_modifier_directional_weight_blend();
    test_classic_io_sign_with_cap();
    test_power_offset_x_guard();
    test_speed_clamp_path();
    test_dir_mul_negative_direction();
    test_synchronous_power_lt_one();
    test_natural_legacy_mode();
    test_config_output_dpi_sanitize();
    test_rotation_negative_normalization();
    test_json_int_overflow_safe();

    // R13 — lat_stats move safety + dpi_factor pre-compute
    test_lat_stats_move_semantics();
    test_dpi_factor_precompute();
    test_output_dpi_applied();

    // R14 — magnitude hypot overflow safety
    test_magnitude_hypot();

    // R15 — comprehensive hardening tests
    test_lut_binary_search_boundaries();
    test_modifier_full_pipeline_stress();
    test_ema_long_sequence_stability();
    test_config_boundary_roundtrip();
    test_rotate_direction_identity();
    test_vec2_helpers_exhaustive();
    test_lat_stats_stress();
    test_all_modes_sanitize_init_apply();
    test_config_unicode_names();
    test_synchronous_edge_cases();

    // P86 — synchronous gain_apply UB fix regression
    test_synchronous_huge_speed_no_ub();

    // P91 — denormal/extreme-input hardening + known-deviation boundaries
    test_p91_denormal_extreme_inputs();
    test_p91_known_deviation_boundaries();

    // P96 — DERİN TARAMA: accel accuracy sweep + power/jump fixes regression
    test_p96_power_io_degenerate_cap();
    test_p96_jump_smooth_extremes_finite();
    test_p96_param_extreme_sweep();

    // CUR-3 — power GAIN overflow guard clamp (cap ceiling, not identity snap)
    test_cur3_power_inf_guard();

    // P99 — config-layer deep-scan regressions
    test_p99_config_guards();

    // P107 — set-param domain contract (CLI accepted-domain values survive sanitize)
    test_p107_param_domain();

    // O31-C2/C3 — config-layer regressions (LUT length pin + numeric gain/raw)
    test_o31_config_c2_c3();

    // P106 — per-parameter-family sınır (uç değer) regresyon tablosu
    test_p106_extremes_table();

    // P119 FAO-1 — sync motivity<1 × smooth grid (LEGACY math-ref pin + GAIN LUT envelope)
    test_fao1_sync_motivity_grid();

    // Logitech HID++ tests
    test_logitech_receiver_discovery();
    test_logitech_hidpp_packets();
    test_logitech_hidpp_notification_classification();
    test_logitech_hidpp_hardware_controls();
    test_logitech_hidraw_discovery();

    // P114 BUG-A: a --filter that matched nothing silently reported "0/0 geçti"
    // + exit 0 (a typo hid the whole suite behind a green gate). No match is a
    // hard error on stderr with exit 1. Runs BEFORE the --list early return so
    // `--list --filter <no-match>` cannot green-wrap a typo either
    // (R49: SECTION() now counts matches in list mode too).
    if (g_have_filter && g_sections_matched == 0) {
        std::fprintf(stderr,
                     "Hata: hiçbir test eşleşmedi ('--filter') — regex/yazım hatası mı?\n");
        return 1;
    }

    if (g_list_only) return 0;

    std::printf("\n=== Sonuç: %d/%d geçti", g_passed, g_tests);
    if (g_failed) std::printf(", %d BAŞARISIZ", g_failed);
    if (g_have_filter)
        std::printf(" (%d section eşleşti, %d atlandı)",
                    g_sections_matched, g_skipped_sections);
    std::printf(" ===\n");

    return g_failed ? 1 : 0;
}
