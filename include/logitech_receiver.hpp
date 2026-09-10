#pragma once

#include <string>
#include <vector>

namespace rawaccel {

// This is intentionally a discovery-only layer.  It reads Linux sysfs and
// never opens a hidraw node or sends a vendor command, so probing hardware is
// safe even for unknown Logitech receivers.  HID++ transport will build on
// these stable, capability-gated records in a later phase.
enum class logitech_receiver_kind : unsigned char {
    unknown,
    nano,
    unifying,
    bolt,
    lightspeed,
    legacy_27mhz,
};

struct logitech_receiver_info {
    std::string sysfs_path;
    std::string product;
    unsigned int vendor_id = 0;
    unsigned int product_id = 0;
    logitech_receiver_kind kind = logitech_receiver_kind::unknown;
    int max_paired_devices = 0;
    bool hidpp_supported = false;
};

const char* logitech_receiver_kind_name(logitech_receiver_kind kind);

/// Enumerate Logitech USB receivers from sysfs. `sysfs_root` is injectable so
/// the parser can be tested without physical hardware.
std::vector<logitech_receiver_info>
discover_logitech_receivers(const std::string& sysfs_root = "/sys/bus/usb/devices");

} // namespace rawaccel
