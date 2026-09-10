#include "logitech_receiver.hpp"

#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace rawaccel {
namespace {

struct receiver_spec {
    unsigned int product_id;
    logitech_receiver_kind kind;
    int slots;
    bool hidpp;
};

// Capability data is deliberately small and conservative.  Unknown Logitech
// USB products are shown but never treated as HID++-safe until their protocol
// support has been verified.
constexpr std::array<receiver_spec, 25> RECEIVERS = {{
    {0xc517, logitech_receiver_kind::legacy_27mhz, 4, true},
    {0xc518, logitech_receiver_kind::nano,          1, true},
    {0xc51a, logitech_receiver_kind::nano,          1, true},
    {0xc51b, logitech_receiver_kind::nano,          1, true},
    {0xc521, logitech_receiver_kind::nano,          1, true},
    {0xc525, logitech_receiver_kind::nano,          1, true},
    {0xc526, logitech_receiver_kind::nano,          1, true},
    {0xc52e, logitech_receiver_kind::nano,          1, true},
    {0xc52f, logitech_receiver_kind::nano,          1, true},
    {0xc52b, logitech_receiver_kind::unifying,      6, true},
    {0xc532, logitech_receiver_kind::unifying,      6, true},
    {0xc531, logitech_receiver_kind::nano,          1, true},
    {0xc534, logitech_receiver_kind::nano,          2, true},
    {0xc535, logitech_receiver_kind::nano,          1, true},
    {0xc537, logitech_receiver_kind::nano,          2, true},
    // This receiver is a Nano model but does not implement HID++; surface it
    // accurately rather than offering commands that could never work.
    {0xc542, logitech_receiver_kind::nano,          1, false},
    {0xc539, logitech_receiver_kind::lightspeed,    1, true},
    {0xc53a, logitech_receiver_kind::lightspeed,    1, true},
    {0xc53d, logitech_receiver_kind::lightspeed,    1, true},
    {0xc53f, logitech_receiver_kind::lightspeed,    1, true},
    {0xc541, logitech_receiver_kind::lightspeed,    1, true},
    {0xc545, logitech_receiver_kind::lightspeed,    1, true},
    {0xc547, logitech_receiver_kind::lightspeed,    1, true},
    {0xc54d, logitech_receiver_kind::lightspeed,    1, true},
    {0xc548, logitech_receiver_kind::bolt,          6, true},
}};

std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    std::string value;
    std::getline(in, value);
    return value;
}

bool read_hex(const fs::path& path, unsigned int& out) {
    const std::string text = read_text(path);
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    auto [end, ec] = std::from_chars(first, last, out, 16);
    return ec == std::errc{} && end == last;
}

const receiver_spec* receiver_for(unsigned int product_id) {
    for (const auto& spec : RECEIVERS)
        if (spec.product_id == product_id) return &spec;
    return nullptr;
}

} // namespace

const char* logitech_receiver_kind_name(logitech_receiver_kind kind) {
    switch (kind) {
    case logitech_receiver_kind::nano:          return "Nano";
    case logitech_receiver_kind::unifying:      return "Unifying";
    case logitech_receiver_kind::bolt:          return "Bolt";
    case logitech_receiver_kind::lightspeed:    return "Lightspeed";
    case logitech_receiver_kind::legacy_27mhz:  return "27MHz";
    default:                                    return "Unknown";
    }
}

std::vector<logitech_receiver_info>
discover_logitech_receivers(const std::string& sysfs_root) {
    std::vector<logitech_receiver_info> result;
    std::error_code ec;
    fs::directory_iterator it(sysfs_root, ec), end;
    while (!ec && it != end) {
        const fs::path path = it->path();
        ++it;
        unsigned int vendor = 0, product = 0;
        if (!read_hex(path / "idVendor", vendor) ||
            !read_hex(path / "idProduct", product) || vendor != 0x046d)
            continue;

        logitech_receiver_info info;
        info.sysfs_path = path.string();
        info.product = read_text(path / "product");
        info.vendor_id = vendor;
        info.product_id = product;
        if (const receiver_spec* spec = receiver_for(product)) {
            info.kind = spec->kind;
            info.max_paired_devices = spec->slots;
            info.hidpp_supported = spec->hidpp;
        }
        result.push_back(std::move(info));
    }
    return result;
}

} // namespace rawaccel
