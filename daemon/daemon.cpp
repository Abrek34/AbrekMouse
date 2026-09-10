#include "daemon.hpp"
#include "motion_math.hpp"
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <fcntl.h>
#include <grp.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <time.h>

namespace rawaccel {

// ── Timing helper ─────────────────────────────────────────────────────────────

// R13-perf: unified CLOCK_MONOTONIC_RAW for both ms and ns timestamps.
// Previously flush_motion() called steady_clock (vDSO) + CLOCK_MONOTONIC_RAW
// separately — 3 syscalls per event.  Now only 2 (start + end), both from the
// same clock source, eliminating drift between the two clocks.
static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 +
           static_cast<double>(ts.tv_nsec) / 1'000'000.0;
}

// ── sysfs device property helpers ────────────────────────────────────────────

/// P121/BUG-02: how long a device path whose I/O failed (EIO/ENODEV/EBADF
/// on read, or repeated failed open) stays on the re-open deny list.  A
/// broken node that remains listed in /dev/input would otherwise be
/// re-grabbed + uinput-create/destroyed every ~2 s forever.
/// P131: backoff shortened to 10 s — long enough to stop the churn loop,
/// short enough that a genuinely recovered device is re-grabbed quickly.
static constexpr double DENY_REOPEN_MS = 10000.0; // 10 s

/// P131/BUG-02: is this device currently on the re-open deny list?
/// Keyed by BOTH the /dev/input path and (when known) the stable device_id,
/// so a node that was renumbered by the kernel (eventN changes across
/// replugs) still backs off.  Loop thread only (no sync).
static bool reopen_denied(const std::string& path, const std::string& device_id,
                          const std::unordered_map<std::string, double>& path_deny,
                          const std::unordered_map<std::string, double>& dev_deny,
                          double nowt) {
    auto pi = path_deny.find(path);
    if (pi != path_deny.end() && nowt < pi->second) return true;
    if (!device_id.empty()) {
        auto di = dev_deny.find(device_id);
        if (di != dev_deny.end() && nowt < di->second) return true;
    }
    return false;
}

/// P131/BUG-02: put a device on the deny list under both keys.
static void deny_reopen(const std::string& path, const std::string& device_id,
                        std::unordered_map<std::string, double>& path_deny,
                        std::unordered_map<std::string, double>& dev_deny) {
    const double until = now_ms() + DENY_REOPEN_MS;
    path_deny[path] = until;
    if (!device_id.empty()) dev_deny[device_id] = until;
}

/// P131/BUG-02: drop deny entries whose path is no longer a real mouse node
/// (unplugged) or whose backoff window has expired.
static void prune_path_deny(std::unordered_map<std::string, double>& path_deny,
                            const std::vector<std::string>& mice, double nowt) {
    auto it = path_deny.begin();
    while (it != path_deny.end()) {
        bool present = false;
        for (auto& p : mice)
            if (p == it->first) { present = true; break; }
        if (!present || nowt >= it->second)
            it = path_deny.erase(it);
        else
            ++it;
    }
}

/// P131/BUG-02: device_id deny entries only expire by deadline (no path to
/// correlate against).
static void prune_dev_deny(std::unordered_map<std::string, double>& dev_deny,
                           double nowt) {
    auto it = dev_deny.begin();
    while (it != dev_deny.end()) {
        if (nowt >= it->second) it = dev_deny.erase(it);
        else ++it;
    }
}

/// Extract the event number from a /dev/input/eventN or /dev/input/by-id/... path.
/// Returns -1 on failure.
static int event_num_from_path(const std::string& path) {
    // Resolve symlinks to get the real /dev/input/eventN path
    char real[PATH_MAX] = {};
    const char* p = realpath(path.c_str(), real) ? real : path.c_str();

    // Find "event" and parse the number after it
    const char* ev = strstr(p, "event");
    if (!ev) return -1;
    ev += 5; // skip "event"
    errno = 0;
    char* end = nullptr;
    long n = strtol(ev, &end, 10);
    // BUG-7 fixed: use safe integer range check before cast, and validate endptr
    if (end == ev || errno != 0 || n < 0) return -1;
    if (n > INT_MAX || n < INT_MIN) return -1;
    return static_cast<int>(n);
}

/// Read a single integer from a sysfs file. Returns -1 on failure.
static int sysfs_read_int(const std::string& sysfs_path) {
    FILE* f = fopen(sysfs_path.c_str(), "r");
    if (!f) return -1;
    char buf[32] = {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return -1;
    errno = 0;
    char* end = nullptr;
    long val = std::strtol(buf, &end, 10);
    // BUG-7 fixed: validate endptr and range before cast to int
    if (end == buf || errno != 0 || val < INT_MIN || val > INT_MAX)
        return -1;
    return static_cast<int>(val);
}

/// Detect mouse polling rate (Hz) from sysfs.
/// Returns detected rate clamped to [POLL_RATE_MIN, POLL_RATE_MAX], or 0 if unknown.
static int detect_polling_rate(const std::string& event_path) {
    int n = event_num_from_path(event_path);
    if (n < 0) return 0;

    char buf[256];
    // 1. Direct polling_rate sysfs node (some HID drivers expose this)
    snprintf(buf, sizeof(buf), "/sys/class/input/event%d/device/polling_rate", n);
    int rate = sysfs_read_int(buf);
    if (rate > 0)
        return std::clamp(rate, (int)POLL_RATE_MIN, (int)POLL_RATE_MAX);

    // 2. USB bInterval (polling interval in ms for USB HID)
    //    bInterval is in frames (1 ms for full-speed USB, 0.125 ms for high-speed).
    //    /sys/class/input/eventN/device/device/bInterval is the USB endpoint interval.
    snprintf(buf, sizeof(buf), "/sys/class/input/event%d/device/device/bInterval", n);
    int binterval = sysfs_read_int(buf);
    if (binterval > 0) {
        // Determine the actual USB speed from sysfs to compute the correct
        // frame size: high-speed (480 Mbps) uses 125µs microframes, full-speed
        // (12 Mbps) uses 1ms frames.  The speed value is:
        //   1 = Low Speed (1.5 Mbps, not used for mice)
        //   2 = Full Speed (12 Mbps)
        //   3 = High Speed (480 Mbps)
        //   5 = SuperSpeed (5 Gbps)
        int rate_hz = 0;
        snprintf(buf, sizeof(buf), "/sys/class/input/event%d/device/device/speed", n);
        int usb_speed = sysfs_read_int(buf);
        if (usb_speed >= 3) {
            // High-speed or faster: bInterval is in 125µs units
            // For USB high-speed interrupt endpoints, bInterval is an
            // exponent: the period is 2^(bInterval-1) microframes.
            // Treating it as a linear divisor overestimates rates (e.g.
            // bInterval=4 is 1000 Hz, not 2000 Hz).
            if (binterval < 1 || binterval > 16) return 0;
            const double interval_us = 125.0 * (1u << (binterval - 1));
            rate_hz = static_cast<int>(1000000.0 / interval_us);
        } else if (usb_speed == 0) {
            // BUG-16 fix: `speed` sysfs could not be read (missing/unknown).
            // Previously this fell into the full-speed branch and reported an
            // up-to-8× too low rate for high-speed gaming mice.  Try the
            // high-speed interpretation first; a valid high-speed result in
            // [POLL_RATE_MIN, POLL_RATE_MAX] wins, otherwise fall back to
            // the full-speed interpretation.
            if (binterval >= 1 && binterval <= 16) {
                const double hs_interval_us = 125.0 * (1u << (binterval - 1));
                const int hs_rate = static_cast<int>(1000000.0 / hs_interval_us);
                if (hs_rate >= static_cast<int>(POLL_RATE_MIN) &&
                    hs_rate <= static_cast<int>(POLL_RATE_MAX))
                    return std::clamp(hs_rate, (int)POLL_RATE_MIN, (int)POLL_RATE_MAX);
            }
            rate_hz = 1000 / binterval;
        } else {
            // Full-speed: bInterval is in 1ms units
            rate_hz = 1000 / binterval;
        }
        if (rate_hz > 0)
            return std::clamp(rate_hz, (int)POLL_RATE_MIN, (int)POLL_RATE_MAX);
    }

    return 0; // unknown
}

/// Detect mouse DPI from sysfs resolution node.
/// Returns detected DPI clamped to [100, 32000], or 0 if unknown.
static int detect_dpi_sysfs(int event_n) {
    char buf[256];
    // Some drivers expose resolution in counts/mm under:
    // /sys/class/input/eventN/device/resolution
    snprintf(buf, sizeof(buf), "/sys/class/input/event%d/device/resolution", event_n);
    int res_counts_per_mm = sysfs_read_int(buf);
    if (res_counts_per_mm > 0) {
        // BUG-7-2 fixed: compute DPI in double to avoid float→int UB when
        // res_counts_per_mm * 25.4 overflows INT_MAX. Clamp before cast.
        double dpi_f = static_cast<double>(res_counts_per_mm) * 25.4;
        dpi_f = std::clamp(dpi_f, 100.0, 32000.0);
        if (dpi_f >= 100.0 && dpi_f <= 32000.0)
            return static_cast<int>(dpi_f);
        return 0;
    }
    return 0;
}

/// Best-effort battery level detection from sysfs.
/// Some gaming mice (e.g., certain Razer, Logitech, SteelSeries models) expose
/// battery percentage through their OWN power_supply subtree. Returns
/// percentage 0-100, or -1 if unknown.
static int detect_battery_level(const std::string& event_path) {
    int n = event_num_from_path(event_path);
    if (n < 0) return -1;

    // P121/BUG-04 scope: the ONLY legitimate source of mouse battery telemetry
    // is the device's own power_supply subtree.  The host's
    // /sys/class/power_supply/* (BAT0/BAT1/Cell0/Cell1) is the LAPTOP's
    // battery and must never be reported as the mouse's charge level.
    // P131: additionally, every node must declare type == "Battery" — USB
    // charging, Mains, or misc power nodes inside the device tree carry no
    // charge percentage, and matching on the directory name alone (e.g.
    // "hidpp_battery_0", "battery", "BAT1") is not a reliable filter.
    const std::string ps_dir = "/sys/class/input/event" + std::to_string(n) +
                               "/device/power_supply";
    DIR* dir = opendir(ps_dir.c_str());
    if (!dir) return -1;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue; // . and ..
        const std::string dname(ent->d_name);
        const std::string node = ps_dir + "/" + dname;

        // Only nodes whose type file reads "Battery" are considered.
        FILE* tf = fopen((node + "/type").c_str(), "r");
        bool is_battery = false;
        if (tf) {
            char tbuf[32] = {};
            is_battery = fgets(tbuf, sizeof(tbuf), tf) != nullptr &&
                         std::string(tbuf).find("Battery") != std::string::npos;
            fclose(tf);
        }
        if (!is_battery) continue;

        FILE* cf = fopen((node + "/capacity").c_str(), "r");
        if (!cf) continue;
        char cbuf[32] = {};
        int val = -1;
        if (fgets(cbuf, sizeof(cbuf), cf)) {
            size_t len = strlen(cbuf);
            while (len > 0 && (cbuf[len-1] == '\n' || cbuf[len-1] == '\r'))
                cbuf[--len] = '\0';
            char* end = nullptr;
            long parsed = std::strtol(cbuf, &end, 10);
            if (end != cbuf && parsed >= 0 && parsed <= 100) val = static_cast<int>(parsed);
        }
        fclose(cf);
        if (val >= 0) { closedir(dir); return val; }
    }
    closedir(dir);
    return -1; // unknown
}

// ── Device discovery ──────────────────────────────────────────────────────────

/// Check if an evdev device is a physical mouse (REL_X + REL_Y, not virtual).
static bool is_physical_mouse(int fd) {
    libevdev* dev = nullptr;
    if (libevdev_new_from_fd(fd, &dev) < 0) return false;

    bool has_motion = libevdev_has_event_code(dev, EV_REL, REL_X) &&
                      libevdev_has_event_code(dev, EV_REL, REL_Y);

    bool is_virtual = false;
    const char* name = libevdev_get_name(dev);
    if (name) {
        std::string sname(name);
        if (sname.size() >= 10 &&
            sname.compare(sname.size() - 10, 10, "(RawAccel)") == 0)
            is_virtual = true;
        const char* phys = libevdev_get_phys(dev);
        if (phys && std::string(phys).find("uinput") != std::string::npos)
            is_virtual = true;
    }

    libevdev_free(dev);
    return has_motion && !is_virtual;
}

/// Resolve a /dev/input/eventN path to its stable /dev/input/by-id/... symlink.
/// Returns the by-id path if found, otherwise returns the original path.
/// Stable IDs match what the GUI stores in device_profile.device_id.
static std::string resolve_stable_id(const std::string& event_node) {
    const char* by_id = "/dev/input/by-id";
    DIR* dir = opendir(by_id);
    if (!dir) return event_node;

    char real_event[PATH_MAX] = {};
    if (!realpath(event_node.c_str(), real_event)) { closedir(dir); return event_node; }

    struct dirent* ent;
    std::string best;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string link = std::string(by_id) + "/" + ent->d_name;
        char target[PATH_MAX] = {};
        if (!realpath(link.c_str(), target)) continue;
        if (std::string(target) == std::string(real_event)) {
            if (best.empty())
                best = link;                      // first match
            else if (link.find("-event-mouse") != std::string::npos &&
                     best.find("-event-mouse") == std::string::npos)
                best = link;                      // prefer -event-mouse over other
        }
    }
    closedir(dir);
    return best.empty() ? event_node : best;
}

/// List all physical mouse event paths in /dev/input/, using stable by-id paths.
static std::vector<std::string> find_mice() {
    std::vector<std::string> result;
    glob_t g{};
    if (glob("/dev/input/event*", 0, nullptr, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                // D3: access error — permission issue or device already grabbed (informational)
                continue;
            }
            if (is_physical_mouse(fd))
                result.push_back(resolve_stable_id(g.gl_pathv[i]));
            close(fd);
        }
    }
    globfree(&g);
    return result;
}

// ── AccelDaemon ───────────────────────────────────────────────────────────────

AccelDaemon::AccelDaemon() = default;

AccelDaemon::~AccelDaemon() {
    // Stop IPC thread first — otherwise if start() failed and the user
    // forgot to call stop_ipc_server(), the std::thread destructor would
    // call std::terminate() (cannot join a still-running thread).
    stop_ipc_server();
    stop();
}

void AccelDaemon::log(const std::string& msg, bool verbose_only) {
    if (verbose_only && !verbose_) return;
    if (log_cb_) log_cb_(msg);
    else         std::cout << "[rawaccel] " << msg << "\n";
}

bool AccelDaemon::start(const std::string& config_path) {
    if (running_.load()) return true;

    config_path_ = config_path.empty() ? find_config_path() : config_path;

    try {
        config_ = load_config(config_path_);
    } catch (std::exception& e) {
        log("Config load failed: " + std::string(e.what()) + " — using defaults.");
        device_profile dp;
        dp.name = "default";
        dp.dev_cfg.dpi = 800;
        dp.dev_cfg.polling_rate = 1000;
        config_.profiles.push_back(dp);
    }

    // ── epoll setup ───────────────────────────────────────────────────────────
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        log("epoll_create1 failed: " + std::string(strerror(errno)));
        return false;
    }

    // ── inotify setup for hot-plug ────────────────────────────────────────────
    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ >= 0) {
        inotify_wd_ = inotify_add_watch(inotify_fd_, "/dev/input",
                                        IN_CREATE | IN_DELETE | IN_ATTRIB);
        if (inotify_wd_ >= 0) {
            epoll_event ev{};
            ev.events   = EPOLLIN;
            ev.data.fd  = inotify_fd_;
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, inotify_fd_, &ev) < 0) {
                log("epoll_ctl(inotify) failed: " + std::string(strerror(errno)) +
                    " — hot-plug disabled.");
                close(inotify_fd_); inotify_fd_ = -1; inotify_wd_ = -1;
            } else {
                log("Hot-plug: watching /dev/input via inotify.", true);
            }
        } else {
            log("inotify_add_watch failed — hot-plug disabled.");
            close(inotify_fd_);
            inotify_fd_ = -1;
        }
    } else {
        log("inotify_init1 failed — hot-plug disabled.");
    }

    if (!setup_devices()) {
        close(epoll_fd_); epoll_fd_ = -1;
        if (inotify_fd_ >= 0) { close(inotify_fd_); inotify_fd_ = -1; }
        return false;
    }

    running_.store(true);
    loop_thread_ = std::thread([this] { run_loop(); });
    // P171-BFIX: HID++ notification draining on its own thread — never on the
    // loop thread where it would delay mouse event processing.
    hidpp_thread_ = std::thread([this] { run_hidpp_worker(); });
    log("Daemon started.");
    return true;
}

void AccelDaemon::stop() {
    running_.store(false);
    if (loop_thread_.joinable()) loop_thread_.join();
    if (hidpp_thread_.joinable()) hidpp_thread_.join();
    teardown_devices();

    if (inotify_fd_ >= 0) { close(inotify_fd_); inotify_fd_ = -1; inotify_wd_ = -1; }
    if (epoll_fd_   >= 0) { close(epoll_fd_);   epoll_fd_   = -1; }

    log("Daemon stopped.");
}

bool AccelDaemon::reload() {
    reload_flag_.store(true);
    return true;
}

bool AccelDaemon::push_config(const std::string& json_str) {
    // Called from the IPC thread.  Parse + sanitize + persist here so an error
    // can be reported synchronously to the client; apply_new_config() then
    // live-applies the pre-validated config on the loop thread.
    try {
        // The IPC listener is created before start() by the executable so its
        // startup failure is non-fatal.  Do not let a client race that window
        // and make save_config() operate on an empty path.
        if (!running_.load(std::memory_order_acquire) || config_path_.empty())
            return false;
        app_config cfg = app_config_from_json(json_str);
        {
            // T8 — no-op guard: if the pushed config matches the currently
            // effective one, skip the disk write and the re-apply entirely.
            // config_ is guarded by devices_mutex_ (written in apply_new_config),
            // so read it under the same mutex.
            std::lock_guard<std::mutex> lk(devices_mutex_);
            if (app_config_to_json(cfg) == app_config_to_json(config_)) {
                log("Config push skipped (no-op guard: content unchanged).", true);
                return true;
            }
        }
        // Atomic write to the daemon's own config path — a root systemd daemon
        // can persist to /etc/rawaccel/settings.json even though the GUI/CLI
        // can only write the user's ~/.config copy.
        save_config(cfg, config_path_);
        {
            std::lock_guard<std::mutex> lk(push_cfg_mu_);
            push_cfg_        = std::move(cfg);
            push_cfg_pending_ = true;
        }
        log("Config pushed over IPC (" + config_path_ + ").", true);
        return true;
    } catch (std::exception& e) {
        log("Config push rejected: " + std::string(e.what()));
        return false;
    }
}

// ── Device setup ──────────────────────────────────────────────────────────────

bool AccelDaemon::open_input_device(mouse_device& dev) {
    dev.fd_in = open(dev.path.c_str(), O_RDONLY | O_NONBLOCK);
    if (dev.fd_in < 0) {
        log("Cannot open " + dev.path + ": " + strerror(errno));
        return false;
    }

    char name[256] = {};
    // O4: check return value; fall back to path if the name cannot be read.
    // Guarantee null terminator even if the buffer is completely filled.
    if (ioctl(dev.fd_in, EVIOCGNAME(sizeof(name) - 1), name) < 0)
        snprintf(name, sizeof(name), "unknown(%s)", dev.path.c_str());
    name[sizeof(name) - 1] = '\0'; // overflow guard
    dev.name = std::string(name);

    // Build a stable composite device ID: "usb:VVVV:PPPP:serial"
    // Priority:
    //   1. vendor+product+serial → most stable, survives reboots and re-plugs
    //   2. vendor+product (no serial) → stable for single-device setups
    //   3. /dev/input/by-id/... symlink → stable if udev provides it
    //   4. /dev/input/eventN → last resort; changes on reboot with multiple USB devices

    // Read USB serial (EVIOCGUNIQ)
    char uniq[256] = {};
    ioctl(dev.fd_in, EVIOCGUNIQ(sizeof(uniq)), uniq); // ignore error — may be empty

    // Read vendor/product IDs (EVIOCGID)
    struct input_id iid = {};
    bool has_vid_pid = (ioctl(dev.fd_in, EVIOCGID, &iid) >= 0 &&
                        (iid.vendor != 0 || iid.product != 0));

    if (has_vid_pid) {
        // "usb:045e:082a:SERIAL" — unique per physical device even without serial
        char id_buf[512];
        snprintf(id_buf, sizeof(id_buf), "usb:%04x:%04x:%s",
                 iid.vendor, iid.product, uniq);
        dev.device_id = std::string(id_buf);
    } else if (uniq[0] != '\0') {
        // No vid/pid but has serial — use serial alone
        dev.device_id = std::string(uniq);
    } else {
        // Fallback: /dev/input/eventN — may change across reboots
        dev.device_id = dev.path;
    }

    if (ioctl(dev.fd_in, EVIOCGRAB, 1) < 0) {
        log("Skipping " + dev.name + " (" + dev.path + "): already grabbed.");
        close(dev.fd_in);
        dev.fd_in = -1;
        return false;
    }

    // Detect polling rate and DPI from sysfs (best-effort, non-blocking).
    // Results stored for status_json() and GUI auto-fill; never overwrite user config.
    dev.detected_polling_rate = detect_polling_rate(dev.path);
    int ev_n = event_num_from_path(dev.path);
    dev.detected_dpi = (ev_n >= 0) ? detect_dpi_sysfs(ev_n) : 0;
    // Best-effort battery level detection from sysfs (some gaming mice expose this)
    dev.detected_battery = detect_battery_level(dev.path);

    if (dev.detected_polling_rate > 0)
        log("Detected polling rate: " + std::to_string(dev.detected_polling_rate) +
            " Hz for " + dev.name, true);
    if (dev.detected_dpi > 0)
        log("Detected DPI: " + std::to_string(dev.detected_dpi) +
            " for " + dev.name, true);

    log("Opened mouse: " + dev.name + " [id=" + dev.device_id + "] (" + dev.path + ")", false);
    return true;
}

bool AccelDaemon::create_virtual_device(mouse_device& dev) {
    int tmp_fd = open(dev.path.c_str(), O_RDONLY | O_NONBLOCK);
    libevdev* src = nullptr;
    int src_fd = (tmp_fd >= 0) ? tmp_fd : dev.fd_in;

    if (libevdev_new_from_fd(src_fd, &src) < 0) {
        if (tmp_fd >= 0) close(tmp_fd);
        log("Cannot read capabilities of " + dev.path);
        return false;
    }

    std::string vname = dev.name + " (RawAccel)";
    libevdev_set_name(src, vname.c_str());
    libevdev_set_uniq(src, nullptr);

    libevdev_uinput* uidev = nullptr;
    int ret = libevdev_uinput_create_from_device(src, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
    libevdev_free(src);
    if (tmp_fd >= 0) close(tmp_fd);

    if (ret < 0) {
        log("Cannot create uinput device for " + dev.path + ": " + strerror(-ret));
        return false;
    }

    dev.uidev = uidev;
    log("Created virtual device: " + vname, true); // verbose: not needed in normal operation
    return true;
}

bool AccelDaemon::setup_devices() {
    auto mice = find_mice();
    if (mice.empty()) {
        // No mouse devices found yet.  Previously this aborted the daemon
        // (so systemd with Restart=on-failure would thrash through restarts
        // during boot while USB devices were still enumerating, or after a
        // permission-grant / abrek-conflict fix).  Instead postpone: keep the
        // event loop alive — the periodic empty-device re-scan (and inotify
        // hot-plug) will grab mice as soon as they appear or become accessible.
        log("No physical mice found yet — waiting for hot-plug. "
            "If this persists, check 'input' group membership or udev rules.");
        return true;
    }
    log("Found " + std::to_string(mice.size()) + " physical mouse device(s).");

    // P131/BUG-02: drop stale deny entries before scanning so a recovered
    // device is picked up and a still-broken one keeps backing off.
    const double setup_now = now_ms();
    prune_path_deny(path_deny_until_ms_, mice, setup_now);
    prune_dev_deny(dev_deny_until_ms_, setup_now);

    for (auto& path : mice) {
        if (opened_paths_.count(path)) continue; // already grabbed
        // P131/BUG-02: setup_devices() must respect the deny list too —
        // otherwise a dead-but-listed node denied by do_hotplug_scan() would
        // be re-grabbed on the next apply_new_config() fallback setup.
        auto dn = path_deny_until_ms_.find(path);
        if (dn != path_deny_until_ms_.end()) {
            if (setup_now < dn->second) continue;
            path_deny_until_ms_.erase(dn); // window expired — allow retry
        }

        mouse_device dev;
        dev.path = path;

        if (!open_input_device(dev)) {
            // P131/BUG-02: opening keeps failing on a dead-but-listed node;
            // back it off (by path at least — the id may be unknown).
            deny_reopen(path, dev.device_id, path_deny_until_ms_, dev_deny_until_ms_);
            continue;
        }
        // P131/BUG-02: the physical device may be the same (stable device_id)
        // one that recently hit an I/O error under a different eventN path.
        if (reopen_denied(path, dev.device_id, path_deny_until_ms_,
                          dev_deny_until_ms_, now_ms())) {
            log("Skipping recently-failed device: " + dev.name + " (" +
                dev.device_id + ")", true);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            deny_reopen(path, dev.device_id, path_deny_until_ms_, dev_deny_until_ms_);
            continue;
        }
        if (!create_virtual_device(dev)) {
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }

        const device_profile* prof = find_profile(dev.device_id);
        if (prof) apply_profile(dev, *prof);

        // Register in epoll
        epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = dev.fd_in;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, dev.fd_in, &ev) < 0) {
            log("epoll_ctl(add) failed for " + dev.path + ": " +
                strerror(errno) + " — skipping device.");
            if (dev.uidev) libevdev_uinput_destroy(dev.uidev);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }

        opened_paths_.insert(path);
        {
            std::lock_guard<std::mutex> lk(devices_mutex_);
            fd_to_dev_[dev.fd_in] = devices_.size(); // index before push
            devices_.push_back(std::move(dev));
        }
    }

    bool no_devices = false;
    {
        std::lock_guard<std::mutex> lk(devices_mutex_);
        no_devices = devices_.empty();
    }
    if (no_devices) {
        // Found devices but none could be grabbed (missing permissions or a
        // conflict, e.g. abrek holding the grab).  Don't abort — the periodic
        // empty-device re-scan retries automatically once the conflict clears.
        log("No mice could be grabbed (permission or conflict, e.g. abrek). "
            "Retrying automatically; to fix now:  sudo systemctl stop abrek");
    }
    return true;
}

void AccelDaemon::teardown_devices() {
    // Collect handles to destroy outside the lock to keep the critical section short
    std::vector<mouse_device> to_destroy;
    {
        std::lock_guard<std::mutex> lk(devices_mutex_);
        to_destroy = std::move(devices_);
        devices_.clear();
        opened_paths_.clear();
        fd_to_dev_.clear();
    }
    for (auto& dev : to_destroy) {
        if (epoll_fd_ >= 0 && dev.fd_in >= 0)
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, dev.fd_in, nullptr);
        if (dev.uidev) {
            libevdev_uinput_destroy(dev.uidev);
            dev.uidev = nullptr;
        }
        if (dev.fd_in >= 0) {
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            dev.fd_in = -1;
        }
    }
}

const device_profile* AccelDaemon::find_profile(const std::string& dev_id) const {
    // 1. Device-specific assignment takes priority
    for (auto& p : config_.profiles)
        if (!p.device_id.empty() && p.device_id == dev_id) return &p;
    // 2. "All devices" catch-all (P121/BUG-03): an empty device_id means the
    //    profile binds to every mouse without a device-specific match — the
    //    documented contract (config.hpp:18) and the GUI's "All devices"
    //    combo entry.  Previously these records were silently skipped unless
    //    they happened to be the active profile or profiles[0].
    for (auto& p : config_.profiles)
        if (p.device_id.empty()) return &p;
    // 3. Active profile
    for (auto& p : config_.profiles)
        if (p.name == config_.active_profile) return &p;
    // 4. First profile (last resort fallback)
    if (!config_.profiles.empty()) return &config_.profiles[0];
    return nullptr;
}

void AccelDaemon::apply_profile(mouse_device& dev, const device_profile& prof) {
    // Defense-in-depth clamp: values are already sanitized in config.cpp,
    // but guard here too in case of programmatic / future IPC paths.
    dev.dpi       = std::clamp(prof.dev_cfg.dpi,          1, 32000);
    dev.poll_rate = std::clamp(prof.dev_cfg.polling_rate,
                               (int)POLL_RATE_MIN, (int)POLL_RATE_MAX);
    // O6: reference uses input_dpi_normalization_factor = NORMALIZED_DPI / dpi
    // (true inches/s input speed); previously (dpi/NORMALIZED_DPI) was inverted.
    dev.dpi_factor = NORMALIZED_DPI / dev.dpi; // R13-perf: pre-compute
    dev.settings.prof = prof.prof;
    init_settings(dev.settings);
    dev.sp.init(prof.prof.speed_processor_args);
    // Reset subpixel remainders on profile change
    dev.remainder_x = 0.0;
    dev.remainder_y = 0.0;
}

// ── Shared config apply path (SIGHUP reload + IPC config push) ────────────────

void AccelDaemon::apply_new_config(const app_config& new_cfg) {
    // Live profile update: re-apply settings to all open devices WITHOUT
    // releasing the grab or destroying the virtual device.  This avoids
    // the ~150 ms mouse-loss window that teardown+setup caused (R5).
    // config_ is written under devices_mutex_ so that status_json() (IPC
    // thread) never reads a half-updated config_.
    bool any_live = false;
    {
        std::lock_guard<std::mutex> lk(devices_mutex_);
        config_ = new_cfg;
        for (auto& dev : devices_) {
            const device_profile* prof = find_profile(dev.device_id);
            if (prof) {
                apply_profile(dev, *prof);
                any_live = true;
                log("Live-updated profile for: " + dev.name, true);
            }
        }
    }

    if (!any_live) {
        // No grabbed devices yet — do a full setup so new devices are opened.
        teardown_devices();
        if (!setup_devices())
            log("Reload: no devices available after reload.");
    }
    log("Config reloaded.");
}

// ── Hot-plug handler ──────────────────────────────────────────────────────────

void AccelDaemon::handle_hotplug() {
    // Drain inotify events
    char buf[4096] __attribute__((aligned(__alignof__(inotify_event))));
    bool changed = false;
    while (true) {
        ssize_t n = read(inotify_fd_, buf, sizeof(buf));
        if (n <= 0) break;
        ssize_t i = 0;
        while (i < n) {
            auto* ev = reinterpret_cast<inotify_event*>(buf + i);
            if (ev->len > 0) {
                std::string fname(ev->name);
                // Only care about event* nodes
                if (fname.rfind("event", 0) == 0) changed = true;
            }
            i += sizeof(inotify_event) + ev->len;
        }
    }

    if (changed)
        pending_hotplug_.store(true); // defer actual scan to run_loop (no usleep)
}

// ── Hot-plug device scan (called from run_loop after kernel settle delay) ─────

void AccelDaemon::do_hotplug_scan() {
    // Check for newly added mice
    auto mice = find_mice();

    // P121/BUG-02: prune the deny list — a path that is no longer listed in
    // /dev/input (real unplug) or whose backoff window expired is retryable.
    const double nowt = now_ms();
    prune_path_deny(path_deny_until_ms_, mice, nowt);
    prune_dev_deny(dev_deny_until_ms_, nowt);

    for (auto& path : mice) {
        if (opened_paths_.count(path)) continue;
        // P121/BUG-02: skip paths still in a backoff window (recent I/O error).
        auto dn = path_deny_until_ms_.find(path);
        if (dn != path_deny_until_ms_.end()) {
            if (nowt < dn->second) continue;
            path_deny_until_ms_.erase(dn); // window expired — allow retry
        }
        log("Hot-plug: new mouse detected at " + path);

        mouse_device dev;
        dev.path = path;
        if (!open_input_device(dev)) {
            // P121/BUG-02: opening keeps failing on a dead-but-listed node
            // (EIO etc.); back it off so we don't retry every ~2 s forever.
            deny_reopen(path, dev.device_id, path_deny_until_ms_, dev_deny_until_ms_);
            continue;
        }
        // P131/BUG-02: same physical device recently failed under another
        // eventN path — hold off instead of re-grabbing it immediately.
        if (reopen_denied(path, dev.device_id, path_deny_until_ms_,
                          dev_deny_until_ms_, now_ms())) {
            log("Hot-plug: skipping recently-failed device: " + dev.name +
                " (" + dev.device_id + ")", true);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            deny_reopen(path, dev.device_id, path_deny_until_ms_, dev_deny_until_ms_);
            continue;
        }
        if (!create_virtual_device(dev)) {
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }

        // Apply per-device profile assignment (same logic as setup_devices)
        const device_profile* prof = find_profile(dev.device_id);
        if (prof) apply_profile(dev, *prof);

        epoll_event eev{};
        eev.events  = EPOLLIN;
        eev.data.fd = dev.fd_in;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, dev.fd_in, &eev) < 0) {
            log("Hot-plug: epoll_ctl(add) failed for " + dev.path + ": " +
                strerror(errno) + " — skipping device.");
            if (dev.uidev) libevdev_uinput_destroy(dev.uidev);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }

        opened_paths_.insert(path);
        {
            std::lock_guard<std::mutex> lk(devices_mutex_);
            fd_to_dev_[dev.fd_in] = devices_.size();
            devices_.push_back(std::move(dev));
        }
    }

    // Remove devices whose path is no longer a physical mouse (disconnected)
    // Collect handles to destroy outside the lock
    std::vector<mouse_device> to_destroy;
    {
        std::lock_guard<std::mutex> lk(devices_mutex_);
        auto it = devices_.begin();
        while (it != devices_.end()) {
            bool still_physical = false;
            for (auto& p : mice)
                if (p == it->path) { still_physical = true; break; }

            if (!still_physical) {
                log("Hot-plug: mouse disconnected: " + it->name + " (" + it->path + ")");
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, it->fd_in, nullptr);
                opened_paths_.erase(it->path);
                to_destroy.push_back(std::move(*it));
                it = devices_.erase(it);
            } else {
                ++it;
            }
        }
        // Rebuild fd_to_dev_ index since vector indices shifted after erase
        fd_to_dev_.clear();
        for (size_t i = 0; i < devices_.size(); i++)
            fd_to_dev_[devices_[i].fd_in] = i;
    }
    for (auto& dev : to_destroy) {
        if (dev.uidev) libevdev_uinput_destroy(dev.uidev);
        if (dev.fd_in >= 0) {
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
        }
    }
}

// ── P171-BFIX: HID++ notification drain (background-thread hook) ───────────

/// Dedicated worker thread: runs poll_hidpp_notifications() on its own cadence
/// so the 1–2 s hidraw open/poll/read cycles can NEVER delay the motion hot
/// path.  Previously this ran on the loop thread, which caused periodic mouse
/// stutter on Logitech Unifying receivers (the hidraw read shares the same USB
/// endpoint as the mouse HID reports).
void AccelDaemon::run_hidpp_worker() {
    while (running_.load()) {
        poll_hidpp_notifications();
        // Wake frequently so stop() joins promptly, while keeping the
        // hidraw I/O rate at poll_hidpp_notifications()' own 1–2 s cadence.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

/// BUG-24 (aj2): a HID++ battery reading (live notification or active query)
/// is merged into the evdev mouse_device whose stable id matches the hidpp
/// device's vendor:product.  When several mice share the id (typical for mice
/// behind one Unifying receiver) we deliberately label nothing rather than
/// risk painting the wrong device; the reading is still logged.
void AccelDaemon::apply_hidpp_battery(const hidpp_device& dev,
                                      const hidpp_battery_info& b) {
    if (b.level == 255) return; // unknown level — nothing meaningful to merge
    char needle[16] = {};
    char needle_dash[16] = {};
    std::snprintf(needle, sizeof(needle), "%04x:%04x", dev.vendor_id, dev.product_id);
    std::snprintf(needle_dash, sizeof(needle_dash), "%04x-%04x",
                  dev.vendor_id, dev.product_id);
    std::string haystack = std::string(needle) + "/" + needle_dash;

    std::lock_guard<std::mutex> lk(devices_mutex_);
    mouse_device* match = nullptr;
    int match_count = 0;
    for (auto& m : devices_) {
        if (m.device_id.find(needle) != std::string::npos ||
            m.device_id.find(needle_dash) != std::string::npos) {
            match = &m;
            match_count++;
        }
    }
    if (match_count == 1) {
        if (match->detected_battery != static_cast<int>(b.level)) {
            match->detected_battery = static_cast<int>(b.level);
            log("HID++ battery: " + match->name + " = " +
                    std::to_string(static_cast<int>(b.level)) + "%", true);
        }
    } else if (match_count == 0) {
        log("HID++ battery: " + std::to_string(static_cast<int>(b.level)) +
                "% (no evdev mouse matched vid:pid " + haystack + ")", true);
    } else {
        log("HID++ battery: " + std::to_string(static_cast<int>(b.level)) +
                "% (multiple mice share vid:pid " + haystack +
                " — not merged)", true);
    }
}

/// Between drain iterations, keep tabs on queued HID++ notifications without
/// ever blocking the event loop: each drain opens the hidraw node, consumes a
/// bounded number of notifications (0 ms poll), classifies them against the
/// device's cached feature map, and closes.  A replug is caught by the periodic
/// re-scan below, which clears the stale feature cache and re-identifies.
void AccelDaemon::poll_hidpp_notifications() {
    const double t = now_ms();

    // Periodic re-scan (every ~2 s): re-discover Logitech hidraw nodes and
    // re-identify replugged devices.  identify_logitech_device() opens a fresh
    // transport whose feature cache starts empty, so a replug transparently
    // recovers the device's dynamic feature-index space.
    if (t >= hidpp_rescan_ms_) {
        hidpp_rescan_ms_ = t + 2000.0;
        const auto paths = discover_logitech_hidraw_devices();

        std::set<std::string> seen(paths.begin(), paths.end());
        for (auto it = hidpp_devs_.begin(); it != hidpp_devs_.end();) {
            if (!seen.count(it->hidraw_path)) {
                log("HID++ device removed: " + it->hidraw_path, true);
                hidpp_transports_.erase(it->hidraw_path);
                it = hidpp_devs_.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& path : paths) {
            const bool known = std::any_of(
                hidpp_devs_.begin(), hidpp_devs_.end(),
                [&path](const hidpp_device& dev) { return dev.hidraw_path == path; });
            if (known) continue; // already identified; drain only
            if (auto dev = identify_logitech_device(path)) {
                log("HID++ device detected: " + path, true);
                hidpp_devs_.push_back(std::move(*dev));
            }
        }
    }

    // Drain requests are cheap (0 ms poll = attempt one non-blocking read each)
    // but still bounded: no device is drained more than every ~1 s, and at most
    // 8 notifications arrive per drain.
    if (t < hidpp_drain_ms_) return;
    hidpp_drain_ms_ = t + 1000.0;

    for (auto& dev : hidpp_devs_) {
        auto& transport_ptr = hidpp_transports_[dev.hidraw_path];
        if (!transport_ptr || !transport_ptr->is_open()) {
            transport_ptr = std::make_unique<HidppTransport>(dev.hidraw_path);
            if (!transport_ptr->is_open()) continue;
            transport_ptr->clear_feature_cache();
        }
        transport_ptr->set_device_index(dev.device_index);
        drain_hidpp_notifications(
            *transport_ptr, dev, 8,
            [this, &dev](const hidpp_notification_event& event) {
                switch (event.kind) {
                case hidpp_notification_event_kind::battery:
                    if (event.battery) {
                        // BUG-24 (aj2): surface live battery notifications on
                        // the mouse device, not just the log line.
                        dev.battery_level = event.battery->level;
                        apply_hidpp_battery(dev, *event.battery);
                        const std::string level = event.battery->level == 255
                            ? "unknown" : std::to_string(event.battery->level) + "%";
                        log("HID++ battery: dev " +
                                std::to_string(event.device_index) + " " + level +
                                (event.battery->charging ? " (charging)" : ""),
                            true);
                    }
                    break;
                case hidpp_notification_event_kind::connection:
                    log("HID++ link: dev " + std::to_string(event.device_index) +
                            (event.connected ? " connected" : " disconnected"),
                        true);
                    break;
                case hidpp_notification_event_kind::illumination:
                    log("HID++ illumination event: dev " +
                            std::to_string(event.device_index),
                        true);
                    break;
                case hidpp_notification_event_kind::generic:
                case hidpp_notification_event_kind::unhandled:
                    break;
                }
            });

        // BUG-24 (aj2): ACTIVE battery query — every 60 s per device.  Wireless
        // receivers/pairings that advertise neither a sysfs power-supply tree
        // nor (live) battery notifications would otherwise stay "unknown (-1)"
        // forever although the device answers a get_battery_status() request.
        if (now_ms() - static_cast<double>(dev.last_battery_ms) >= 60000.0) {
            dev.last_battery_ms = static_cast<uint64_t>(now_ms());
            if (auto b = transport_ptr->get_battery_status(dev.device_index)) {
                dev.battery_level = b->level;
                apply_hidpp_battery(dev, *b);
            }
        }
    }
}

// ── Main loop (epoll-based) ───────────────────────────────────────────────────

void AccelDaemon::run_loop() {
    constexpr int MAX_EVENTS = 32;
    epoll_event events[MAX_EVENTS];

    while (running_.load()) {
        // Handle config reload request (SIGHUP or IPC "reload")
        if (reload_flag_.exchange(false)) {
            log("Reloading config...");
            try {
                apply_new_config(load_config(config_path_));
            } catch (std::exception& e) {
                log("Config reload failed (keeping current): " + std::string(e.what()));
            }
        }

        // Handle a config pushed over IPC (GUI/CLI "set_config" — syncs the
        // user's edits into the daemon's own config file).  push_config() has
        // already validated and persisted it, so this cannot throw.
        {
            std::lock_guard<std::mutex> lk(push_cfg_mu_);
            if (push_cfg_pending_) {
                apply_new_config(push_cfg_);
                push_cfg_pending_ = false;
                log("Applied config pushed over IPC.", true);
            }
        }

        // Deferred hot-plug: wait for the kernel to finish creating the device node.
        // We retry once per epoll_wait cycle (every ~10ms); after 8 retries (~80ms)
        // we give up if the device still isn't ready. This keeps the event loop non-blocking.
        if (pending_hotplug_.load()) {
            hotplug_retry_++;
            // 8 × 10ms epoll_wait = ~80ms wait — USB hubs may take 50-100ms for
            // the kernel to make the device node available.
            if (hotplug_retry_ >= 8) {
                pending_hotplug_.store(false);
                hotplug_retry_ = 0;
                do_hotplug_scan();
            }
        }

        // Self-healing startup: when no devices are open (no mice at boot, or
        // permission/conflict that later cleared), force a scan every ~2 s even
        // without an inotify event — so the daemon converges on its own instead
        // of waiting for an unplug/replug.  Cheap: find_mice() on retired fds.
        if (devices_.empty()) {
            const double t = now_ms();
            if (t >= empty_rescan_ms_) {
                empty_rescan_ms_ = t + 2000.0;
                do_hotplug_scan();
            }
        }

        // P171-BFIX: HID++ housekeeping (re-scan + notification drain) now runs
        // on a dedicated hidpp_thread_ so the motion hot path below is never
        // delayed by hidraw open/poll/read cycles.

        // epoll_wait with 10ms timeout (allows flag checks above)
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 10);
        if (n < 0) {
            if (errno == EINTR) continue;
            log("epoll_wait error: " + std::string(strerror(errno)));
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            // inotify hot-plug event: drain events and set pending flag
            if (fd == inotify_fd_) {
                handle_hotplug();
                continue;
            }

            // O(1) fd -> device lookup
            auto it = fd_to_dev_.find(fd);
            if (it != fd_to_dev_.end() && it->second < devices_.size())
                process_device(devices_[it->second]);
        }

        // Clean up any devices that got a fatal I/O error during processing
        std::vector<mouse_device> disc_devs;
        {
            std::lock_guard<std::mutex> lk(devices_mutex_);
            auto dit = devices_.begin();
            while (dit != devices_.end()) {
                if (dit->disconnected) {
                    log("Removing disconnected device: " + dit->name);
                    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, dit->fd_in, nullptr);
                    fd_to_dev_.erase(dit->fd_in);
                    opened_paths_.erase(dit->path);
                    // P121/BUG-02: an I/O error often means the node is dead but
                    // still listed in /dev/input.  Deny immediate re-open so the
                    // ~2 s empty-rescan doesn't re-grab/uinput-churn it forever.
                    // P131: also deny by stable device_id so a kernel
                    // renumber (eventN → eventM) can't bypass the backoff.
                    deny_reopen(dit->path, dit->device_id,
                                path_deny_until_ms_, dev_deny_until_ms_);
                    disc_devs.push_back(std::move(*dit));
                    dit = devices_.erase(dit);
                } else {
                    ++dit;
                }
            }
            if (!disc_devs.empty()) {
                // Rebuild fd_to_dev_ index after removals
                fd_to_dev_.clear();
                for (size_t i = 0; i < devices_.size(); i++)
                    fd_to_dev_[devices_[i].fd_in] = i;
            }
        }
        for (auto& dev : disc_devs) {
            if (dev.uidev) libevdev_uinput_destroy(dev.uidev);
            if (dev.fd_in >= 0) {
                ioctl(dev.fd_in, EVIOCGRAB, 0);
                close(dev.fd_in);
            }
        }
    }
}

// ── Per-device event processing ───────────────────────────────────────────────

/// Nanosecond wall-clock timestamp using CLOCK_MONOTONIC_RAW.
/// More stable than steady_clock on Linux (no NTP slew adjustments).
static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

/// Write a single event to uinput; returns false if the write fails
/// (indicating the virtual device is dead — caller should mark dev as disconnected).
static inline bool uinput_write(libevdev_uinput* uidev, unsigned int type,
                                unsigned int code, int value) {
    return libevdev_uinput_write_event(uidev, type, code, value) == 0;
}

/// P93: batch REL_X + REL_Y into a SINGLE write() syscall instead of two
/// libevdev_uinput_write_event() calls (each = one write()).  The kernel uinput
/// driver injects every input_event struct found in the write buffer, so the
/// event stream is byte-identical — this only collapses the two syscalls into
/// one.  Zero-valued axes are skipped (behaves exactly like the old
/// `if (x != 0) write; if (y != 0) write` sequence).  Returns false on any
/// short/failed write so the caller can mark the device disconnected.
static inline bool uinput_write_rel(libevdev_uinput* uidev, int x, int y) {
    struct input_event evs[2];
    int n = 0;
    if (x != 0) { evs[n] = {}; evs[n].type = EV_REL; evs[n].code = REL_X; evs[n].value = x; n++; }
    if (y != 0) { evs[n] = {}; evs[n].type = EV_REL; evs[n].code = REL_Y; evs[n].value = y; n++; }
    if (n == 0) return true; // nothing to forward
    const int fd = libevdev_uinput_get_fd(uidev);
    if (fd < 0) return false;
    const ssize_t want = static_cast<ssize_t>(n) * sizeof(struct input_event);
    ssize_t written = 0;
    while (written < want) {
        const void* p = static_cast<const void*>(evs);
        ssize_t got = write(fd, static_cast<const char*>(p) + written,
                            want - written);
        if (got < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (got == 0) return false; // 0-length write: treat as failure
        written += got;
    }
    return true;
}

/// Apply acceleration to accumulated (dx,dy) and write REL events to uidev.
/// Updates dev timing and subpixel remainder. Does NOT write SYN.
/// Measures processing latency (time from call entry to last uinput write) in µs.
/// Returns false if a uinput write fails (caller should mark dev as disconnected).
static bool flush_motion(mouse_device& dev, libevdev_uinput* uidev,
                         double dx, double dy) {
    const uint64_t t_start = now_ns(); // latency measurement start

    // Raw passthrough: bypass the entire acceleration pipeline.
    // No rotation, no snap, no speed clamp, no weights, no subpixel accumulation.
    // dx/dy are already integer counts from the kernel — write them directly.
    if (dev.settings.prof.raw_passthrough) {
        // Defense-in-depth: clamp to INT range before casting (mirrors the
        // guard in motion_math.hpp). A pathological event batch with millions
        // of REL_X events in one SYN frame would otherwise be UB on cast.
        constexpr double INT_LO = static_cast<double>(INT_MIN);
        constexpr double INT_HI = static_cast<double>(INT_MAX);
        if (!std::isfinite(dx)) dx = 0;
        if (!std::isfinite(dy)) dy = 0;
        int ix = static_cast<int>(std::clamp(dx, INT_LO, INT_HI));
        int iy = static_cast<int>(std::clamp(dy, INT_LO, INT_HI));
        if (!uinput_write_rel(uidev, ix, iy)) return false;
        double lat_us = static_cast<double>(now_ns() - t_start) / 1000.0;
        dev.lat.record(lat_us);
        // Live telemetry: raw-passthrough path (no modifier). Fill counters and
        // deltas only — speeds are undefined without the speed pipeline.
        // NOTE (P121/BUG-05 doc): in practice this branch is currently
        // UNREACHABLE — raw REL_X/REL_Y are forwarded one-by-one inline in
        // process_device() and has_motion is never set, so flush_motion() is
        // never called in raw mode.  The block is kept as the telemetry
        // contract for a future batched raw path, and is harmless dead code.
        // seqlock protocol: bump counter to odd (write in progress)
        dev.telemetry->samples.fetch_add(1, std::memory_order_release);
        dev.telemetry->dx.store(static_cast<double>(ix), std::memory_order_relaxed);
        dev.telemetry->dy.store(static_cast<double>(iy), std::memory_order_relaxed);
        // P100: P93-perf'teki ile aynı optimizasyon — now_ms() yerine zaten
        // alınan t_start'tan türet (ns→ms). P121/BUG-06: telem_wall_ms
        // status dev JSON'unda yayımlanır (istemci bayatlık hesaplayabilsin),
        // bu yüzden ek clock_gettime okuması israf olur. Önceden 3 syscall,
        // artık 2 — değer anlamsal olarak aynı monotonic-RAW.
        dev.telemetry->wall_ms.store(static_cast<double>(t_start) / 1'000'000.0,
                                     std::memory_order_relaxed);
        // bump counter to even (write complete)
        dev.telemetry->samples.fetch_add(1, std::memory_order_release);
        return true;
    }

    // P93-perf: derive the interval timestamp from the latency-start read
    // (t_start) instead of calling now_ms() again.  This drops the per-event
    // clock_gettime read count from 3 to 2 (start + end) — one fewer vDSO call
    // (~13 ns on this kernel) with no semantic change: the interval is measured
    // start-to-start, and the sub-µs gap between the old now_ms() read and
    // function entry is negligible vs a >=0.0625ms poll window.  Both timers
    // stay on the same CLOCK_MONOTONIC_RAW source (AGENTS.md contract).
    double now = static_cast<double>(t_start) / 1'000'000.0; // ns -> ms, same clock source
    milliseconds time_ms = now - dev.last_time_ms;
    // D6: modify() returns early when time<=0 (no motion applied).
    // flush_motion follows the same strategy: don't send motion for zero/negative intervals.
    // On the first call last_time_ms==0, so time_ms will be very large and gets clamped to DEFAULT_TIME_MAX — correct.
    if (time_ms <= 0) time_ms = DEFAULT_TIME_MIN; // clamp to minimum window instead of zero
    if (time_ms > DEFAULT_TIME_MAX) time_ms = DEFAULT_TIME_MAX;
    dev.last_time_ms = now;

    int out_x = 0, out_y = 0;
    apply_motion_math(dev.mod, dev.sp, dev.settings, dev.dpi_factor, time_ms,
                      dx, dy, dev.remainder_x, dev.remainder_y, out_x, out_y);

    if (!uinput_write_rel(uidev, out_x, out_y)) return false;

    // Live telemetry (T30): last-motion sample. IPS = euclidean magnitude of
    // the RAW (pre-rotation) deltas × (dpi_factor / time_ms).  This equals the
    // modifier's internal speed ONLY for euclidean mode + domain_weights 1 +
    // snap 0 + no smoothing (BUG-25/aj2 — deliberate: telemetry reports the
    // physical input, not the curve-equivalent coordinate).  Atomic fields plus
    // the generation counter let the IPC reader take a seqlock-style snapshot
    // without a hot-path mutex.
    double ips_factor = dev.dpi_factor / time_ms;
    double in_ips     = magnitude({ dx, dy }) * ips_factor;
    double out_ips    = magnitude({ static_cast<double>(out_x), static_cast<double>(out_y) }) * ips_factor;
    // seqlock protocol: bump counter to odd (write in progress)
    dev.telemetry->samples.fetch_add(1, std::memory_order_release);
    dev.telemetry->speed_ips.store(in_ips, std::memory_order_relaxed);
    dev.telemetry->out_ips.store(out_ips, std::memory_order_relaxed);
    dev.telemetry->gain.store(in_ips > 0 ? out_ips / in_ips : 0, std::memory_order_relaxed);
    dev.telemetry->dx.store(dx, std::memory_order_relaxed);
    dev.telemetry->dy.store(dy, std::memory_order_relaxed);
    dev.telemetry->wall_ms.store(now, std::memory_order_relaxed);
    // bump counter to even (write complete)
    dev.telemetry->samples.fetch_add(1, std::memory_order_release);

    // Record processing latency (µs): time from flush_motion entry to last write.
    // This covers: modifier math + uinput write (does NOT include kernel→user round-trip).
    double lat_us = static_cast<double>(now_ns() - t_start) / 1000.0;
    dev.lat.record(lat_us);
    return true;
}

void AccelDaemon::process_device(mouse_device& dev) {
    auto* uidev = dev.uidev;
    if (!uidev) return;

    input_event ev;

    // Accumulate relative motion in this batch
    double dx = 0, dy = 0;
    bool has_motion  = false;
    bool has_syn     = false;
    bool wrote_unsynced_event = false;
    // BUG-18: syn_dropped is now a device-state field (mouse_device::syn_dropped)
    // so a SYN_DROPPED event in one read batch is correctly remembered until
    // the matching SYN_REPORT arrives in the next process_device() invocation.
    bool& syn_dropped = dev.syn_dropped;
    auto flush_pending_motion = [&]() -> bool {
        if (!has_motion) return true;
        if (!flush_motion(dev, uidev, dx, dy)) {
            dev.disconnected = true;
            return false;
        }
        wrote_unsynced_event = true;
        dx = dy = 0;
        has_motion = false;
        return true;
    };

    // Read all pending events
    while (true) {
        ssize_t n = read(dev.fd_in, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // normal: no more events
            if (errno == EINTR) continue;
            // Fatal errors: device physically disconnected or kernel error
            if (errno == EIO || errno == ENODEV || errno == EBADF) {
                log("Device error (" + std::string(strerror(errno)) +
                    "), marking for removal: " + dev.name);
                dev.disconnected = true;
            } else {
                // Unexpected errno (EINTR/EFAULT/EINVAL/etc.) — log so we
                // don't silently drop events.  Default policy: keep the
                // device, the next loop iteration will retry.
                log("read() unexpected errno=" + std::to_string(errno) +
                    " (" + strerror(errno) + ") on " + dev.name, true);
            }
            break;
        }
        if (n == 0) { dev.disconnected = true; break; } // EOF — treat as disconnect
        if (n < (ssize_t)sizeof(ev)) break; // short read, skip

        if (ev.type == EV_SYN) {
            if (ev.code == SYN_DROPPED) {
                // Kernel dropped events due to buffer overflow.
                // Per the Linux input protocol, ALL events between SYN_DROPPED
                // and the next SYN_REPORT are unreliable and must be discarded.
                // Set a flag so subsequent events in this batch are ignored.
                dx = dy = 0;
                has_motion = false;
                syn_dropped = true;
                continue;
            }
            // SYN_REPORT: if we were in syn_dropped state, this marks the
            // boundary — clear the flag and resume normal processing.
            // Do NOT flush any motion or forward this SYN (the dropped window
            // ends here; fresh data starts from the next event batch).
            if (syn_dropped) {
                syn_dropped = false;
                continue;
            }
            has_syn = true;
            if (!flush_pending_motion()) return;
            // Forward SYN
            if (!uinput_write(uidev, EV_SYN, SYN_REPORT, 0))
                { dev.disconnected = true; return; }
            wrote_unsynced_event = false;
        } else if (syn_dropped) {
            // R12: discard all non-SYN events while in SYN_DROPPED state.
            // Motion counts, button events, etc. are unreliable until the
            // next SYN_REPORT clears the dropped flag.
            continue;
        } else if (ev.type == EV_REL) {
            if (ev.code == REL_X || ev.code == REL_Y) {
                // Raw passthrough fast path: forward each REL_X/REL_Y event
                // INDIVIDUALLY (no batching) so the byte-stream written to
                // uinput is bit-identical to a "dumb" 1:1 forwarder like
                // abrek. libinput's adaptive accel treats batched vs split
                // events differently (single +15 vs 5×+3), and we noticed
                // this caused subtly different cursor feel even in raw mode.
                // Skip accumulation entirely in this mode.
                if (dev.settings.prof.raw_passthrough) {
                    if (!uinput_write(uidev, ev.type, ev.code, ev.value))
                        { dev.disconnected = true; return; }
                    wrote_unsynced_event = true;
                } else if (ev.code == REL_X) {
                    dx += ev.value; has_motion = true;
                } else {
                    dy += ev.value; has_motion = true;
                }
            } else {
                // Pass through other REL events (wheel, tilt, etc.)
                if (!flush_pending_motion()) return;
                if (!uinput_write(uidev, ev.type, ev.code, ev.value))
                    { dev.disconnected = true; return; }
                wrote_unsynced_event = true;
            }
        } else {
            // Forward all other events (buttons, misc, etc.)
            if (!flush_pending_motion()) return;
            if (!uinput_write(uidev, ev.type, ev.code, ev.value))
                { dev.disconnected = true; return; }
            wrote_unsynced_event = true;
        }
    }

    // If events were forwarded but no SYN arrived (rare edge case), flush with synthetic SYN
    if (!has_syn) {
        if (!flush_pending_motion()) return;
        if (wrote_unsynced_event && !uinput_write(uidev, EV_SYN, SYN_REPORT, 0))
            { dev.disconnected = true; return; }
    }
}

// ── Latency statistics dump ───────────────────────────────────────────────────

void AccelDaemon::dump_latency_stats() {
    // Called on SIGUSR1 from the main thread — prints to stdout (journald captures it).
    // Stats cover flush_motion processing time (modifier math + uinput write),
    // NOT the full kernel→userspace round-trip latency.
    // snapshot_and_reset() atomically reads and clears counters under the mutex.
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "=== RawAccel Processing Latency ===\n";
    std::lock_guard<std::mutex> lk(devices_mutex_);
    if (devices_.empty()) {
        std::cout << "  No devices currently grabbed.\n";
        std::cout << "===================================\n";
        std::cout.flush();
        return;
    }
    for (auto& dev : devices_) {
        std::cout << "  Device: " << dev.name;
        // BUG-21 (GPT-10): in raw_passthrough mode REL_X/REL_Y events bypass
        // flush_motion() entirely (forwarded one-by-one straight from
        // process_device), so dev.lat is never updated.  Make this explicit
        // in the dump so users don't see a misleading "No motion events".
        if (dev.settings.prof.raw_passthrough) {
            std::cout << "  [raw passthrough — no per-event measurement]\n";
            std::cout << "    (events forwarded 1:1 to uinput; the clock_gettime\n"
                         "     pair would add ~50 ns per event vs. 0 in raw mode.)\n";
            continue;
        }
        std::cout << "\n";
        auto s = dev.lat.snapshot_and_reset();
        if (s.count == 0) {
            std::cout << "    No motion events recorded yet.\n";
        } else {
            std::cout << std::fixed << std::setprecision(2)
                      << "    Samples  : " << s.count              << "\n"
                      << "    Min      : " << s.min_us             << " µs\n"
                      << "    Avg      : " << s.avg_us()           << " µs\n"
                      << "    p50      : " << s.percentile(50)     << " µs\n"
                      << "    p95      : " << s.percentile(95)     << " µs\n"
                      << "    p99      : " << s.percentile(99)     << " µs\n"
                      << "    Max      : " << s.max_us             << " µs\n";
            if (s.over > 0)
                std::cout << "    Overflow : " << s.over
                          << " samples > " << lat_stats::RANGE_US << " µs\n";
        }
        std::cout << "    (counters reset)\n";
    }
    std::cout << "===================================\n";
    std::cout.flush();
}

// ── IPC: Unix domain socket server ───────────────────────────────────────────

/// Helper: escape a string for JSON (only handles ASCII printable + common escapes).
/// Hard cap for the IPC config-push body.  Real config files are a few KB;
/// this bounds even pathological ones while staying far below document limits.
/// P121/BUG-07: 8 MB was far above anything legitimate; 1 MB keeps the
/// slow-loris surface small while never rejecting a real config.
static constexpr unsigned long long MAX_CONFIG_PUSH_BYTES =
    1ULL * 1024ULL * 1024ULL; // 1 MB

/// P121/BUG-01+07: total per-request deadline for the IPC worker (serial
/// accept loop).  A client that stays under each per-recv SO_RCVTIMEO (2 s)
/// by dribbling one byte at a time can no longer hold the worker longer
/// than this — bounds both a slow command line and a slowloris config body.
/// P131: raised to 10 s so a legitimately large status/config exchange over
/// a loaded socket never trips the guard, while a slow peer is still capped.
static constexpr uint64_t IPC_REQUEST_DEADLINE_NS =
    10ULL * 1000000000ULL; // 10 s

/// P131/BUG-07: separate deadline for the set_config BODY read.  The body is
/// up to 1 MB of JSON; a client that trickled the command line (allowed up to
/// the 10 s total above) must not also dribble the body for an unbounded
/// window — from body-read start it has 5 s.
static constexpr uint64_t CONFIG_BODY_DEADLINE_NS =
    5ULL * 1000000000ULL; // 5 s

static std::string json_str(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20)  out += ' '; // replace other controls with space
        else                out += static_cast<char>(c);
    }
    out += '"';
    return out;
}

/// P150: append `,"key":<v with prec decimals>` without ostringstream.
/// snprintf %.*f renders the same bytes as `o << fixed << setprecision(prec)`
/// for the finite magnitudes in status output (verified by probe over
/// 0/rounding-boundary/large/wall-clock values), at ~1/3 the cost: no
/// per-field locale/sentry/facet overhead. String escaping still goes
/// through json_str() above; ints use std::to_string (exact).
static inline void append_fixed(std::string& o, const char* key, double v,
                                int prec) {
    if (!std::isfinite(v)) v = 0.0;
    char nb[64];
    // Leading field separator emitted separately (push below) so the
    // decimal-separator repair loop can never corrupt a key or the comma.
    // Previously the format began with ',' which the loop below converted
    // to '.' — producing `"lat_samples":1234."lat_avg_us":...` invalid JSON.
    // (aj3 G-BUG-2 / bug_raporları BUG-06.)
    int n = snprintf(nb, sizeof nb, "\"%s\":%.*f", key, prec, v);
    if (n > 0) {
        // Guarantee JSON-standard '.' decimal separator regardless of LC_NUMERIC.
        // The only commas this buffer can now contain are decimal separators
        // inside the numeric value (keys are fixed constants with no ',').
        for (int i = 0; i < n && (size_t)i < sizeof nb; ++i) {
            if (nb[i] == ',') nb[i] = '.';
        }
        o.push_back(',');
        o.append(nb, static_cast<size_t>(std::min(n, (int)sizeof nb - 1)));
    }
}

std::string AccelDaemon::ipc_sock_path() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0') return std::string(xdg) + "/rawaccel.sock";
    return "/run/rawaccel.sock";
}

std::string AccelDaemon::status_json() const {
    // P150: std::string + reserve instead of ostringstream (~-33% JSON-build
    // time, byte-identical output). ostringstream pays locale/sentry overhead
    // per field; doubles go through append_fixed (snprintf), ints through
    // std::to_string, strings through json_str().
    std::string o;
    o.reserve(1024);
    o += "{";
    o += "\"running\":";
    o += (running_.load() ? "true" : "false");
    o += ",";

    // Collect device snapshots, config path, and active_profile under a single
    // devices_mutex_ lock.  This prevents a data race with run_loop()'s
    // reload path which writes config_ under the same mutex.
    // lat.mtx is acquired nested inside devices_mutex_; dump_latency_stats() uses
    // the same lock order (devices_mutex_ → lat.mtx) so no deadlock is possible.

struct DevSnap {
        std::string name, path, device_id;
        int dpi, poll_rate, detected_dpi, detected_polling_rate, detected_battery;
        lat_stats::snapshot lat_snap;   // P136: histogram copy (math runs lock-free below)
        bool     has_lat = false;       // lat_snap.count > 0
        uint64_t lat_count = 0;
        double lat_avg = 0, lat_p50 = 0, lat_p95 = 0, lat_p99 = 0, lat_max = 0;
        bool     telem_ok = false;   // counters matched under seqlock read
        double   telem_speed_ips = 0, telem_out_ips = 0, telem_gain = 0;
        double   telem_dx = 0, telem_dy = 0, telem_wall_ms = 0;
    };

    std::string cfg_path_snap;
    std::string active_prof_snap;
    std::vector<DevSnap> snaps;
    {
        std::lock_guard<std::mutex> lk(devices_mutex_);
        cfg_path_snap    = config_path_;
        active_prof_snap = config_.active_profile;
        for (const auto& dev : devices_) {
            DevSnap s;
            s.name = dev.name; s.path = dev.path; s.device_id = dev.device_id;
            s.dpi = dev.dpi; s.poll_rate = dev.poll_rate;
            s.detected_dpi = dev.detected_dpi;
            s.detected_polling_rate = dev.detected_polling_rate;
            s.detected_battery = dev.detected_battery;

            // P136 lock-narrowing: copy histogram counters under lat.mtx only
            // (fast memcpy, ~60ns). avg/percentile math runs on the copy AFTER
            // devices_mutex_ is released, so the motion thread's lat.record()
            // and reload/hotplug paths never wait on O(BUCKETS) scans.
            // copy() takes lat.mtx itself — do NOT hold llk here (would deadlock).
            s.lat_snap = dev.lat.copy();
            s.has_lat  = (s.lat_snap.count > 0);

            // Live telemetry (seqlock-style read): flush_motion() increments
            // telem_samples to an odd value before writing, writes the fields,
            // then increments to an even value.
            for (int attempts = 0; attempts < 8; attempts++) {
                const uint64_t s1 = dev.telemetry->samples.load(std::memory_order_acquire);
                if (s1 == 0) break; // no motion yet
                if ((s1 & 1) != 0) continue; // write in progress (odd counter) — spin
                const double t_speed = dev.telemetry->speed_ips.load(std::memory_order_relaxed);
                const double t_out   = dev.telemetry->out_ips.load(std::memory_order_relaxed);
                const double t_gain  = dev.telemetry->gain.load(std::memory_order_relaxed);
                const double t_dx    = dev.telemetry->dx.load(std::memory_order_relaxed);
                const double t_dy    = dev.telemetry->dy.load(std::memory_order_relaxed);
                const double t_wall  = dev.telemetry->wall_ms.load(std::memory_order_relaxed);
                const uint64_t s2 = dev.telemetry->samples.load(std::memory_order_acquire);
                if (s1 == s2) {
                    s.telem_ok = true;
                    s.telem_speed_ips = t_speed;
                    s.telem_out_ips   = t_out;
                    s.telem_gain      = t_gain;
                    s.telem_dx        = t_dx;
                    s.telem_dy        = t_dy;
                    s.telem_wall_ms   = t_wall;
                    break;
                }
            }
            snaps.push_back(s);
        }
    }

    // P136: percentile math on the histogram copies — NO locks held here
    // (neither devices_mutex_ nor lat.mtx). Same formulas as before
    // (snapshot::avg_us/percentile are identical to lat_stats::), only moved.
    for (auto& s : snaps) {
        if (!s.has_lat) continue;
        s.lat_count = s.lat_snap.count;
        s.lat_avg   = s.lat_snap.avg_us();
        s.lat_p50   = s.lat_snap.percentile(50);
        s.lat_p95   = s.lat_snap.percentile(95);
        s.lat_p99   = s.lat_snap.percentile(99);
        s.lat_max   = s.lat_snap.max_us;
    }

    o += "\"config\":";
    o += json_str(cfg_path_snap);
    o += ",\"active_profile\":";
    o += json_str(active_prof_snap);
    o += ",\"devices\":[";

    bool first = true;
    for (const auto& s : snaps) {
        if (!first) o += ",";
        first = false;
        o += "{\"name\":";
        o += json_str(s.name);
        o += ",\"path\":";
        o += json_str(s.path);
        o += ",\"device_id\":";
        o += json_str(s.device_id);
        o += ",\"dpi\":";
        o += std::to_string(s.dpi);
        o += ",\"poll_rate\":";
        o += std::to_string(s.poll_rate);
        o += ",\"detected_dpi\":";
        o += std::to_string(s.detected_dpi);
        o += ",\"detected_polling_rate\":";
        o += std::to_string(s.detected_polling_rate);
        o += ",\"detected_battery\":";
        o += std::to_string(s.detected_battery);
        if (s.lat_count > 0) {
            o += ",\"lat_samples\":";
            o += std::to_string(s.lat_count);
            append_fixed(o, "lat_avg_us", s.lat_avg, 2);
            append_fixed(o, "lat_p50_us", s.lat_p50, 2);
            append_fixed(o, "lat_p95_us", s.lat_p95, 2);
            append_fixed(o, "lat_p99_us", s.lat_p99, 2);
            append_fixed(o, "lat_max_us", s.lat_max, 2);
        }
        if (s.telem_ok) {
            append_fixed(o, "telem_in_ips", s.telem_speed_ips, 3);
            append_fixed(o, "telem_out_ips", s.telem_out_ips, 3);
            append_fixed(o, "telem_gain", s.telem_gain, 3);
            append_fixed(o, "telem_dx", s.telem_dx, 3);
            append_fixed(o, "telem_dy", s.telem_dy, 3);
            // P121/BUG-06: publish the sample timestamp (CLOCK_MONOTONIC_RAW,
            // ms since boot) so consumers can compute staleness.  A sample that
            // stopped being updated minutes ago is no longer "current speed".
            append_fixed(o, "telem_wall_ms", s.telem_wall_ms, 3);
        }
        o += "}";
    }
    o += "]}";
    return o;
}

bool AccelDaemon::start_ipc_server(const std::string& sock_path) {
    ipc_sock_path_ = sock_path;

    // Do not unlink a pathname that may belong to a live daemon.  A second
    // instance must fail rather than silently replacing the first daemon's
    // control socket.
    struct stat existing {};
    if (lstat(sock_path.c_str(), &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode)) {
            log("IPC: refusing to replace non-socket path: " + sock_path);
            return false;
        }
        int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (probe < 0) {
            log("IPC: socket probe failed: " + std::string(strerror(errno)));
            return false;
        }
        sockaddr_un probe_addr{};
        probe_addr.sun_family = AF_UNIX;
        if (sock_path.size() >= sizeof(probe_addr.sun_path)) {
            close(probe);
            log("IPC: socket path too long: " + sock_path);
            return false;
        }
        strncpy(probe_addr.sun_path, sock_path.c_str(),
                sizeof(probe_addr.sun_path) - 1);
        const bool live = connect(probe,
            reinterpret_cast<sockaddr*>(&probe_addr), sizeof(probe_addr)) == 0;
        const int probe_errno = errno;
        close(probe);
        if (live) {
            log("IPC: socket is already owned by a running daemon: " + sock_path);
            return false;
        }
        if (probe_errno != ECONNREFUSED && probe_errno != ENOENT) {
            log("IPC: cannot verify existing socket: " +
                std::string(strerror(probe_errno)));
            return false;
        }
        if (unlink(sock_path.c_str()) != 0 && errno != ENOENT) {
            log("IPC: cannot remove stale socket: " + std::string(strerror(errno)));
            return false;
        }
    } else if (errno != ENOENT) {
        log("IPC: cannot inspect socket path: " + std::string(strerror(errno)));
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        log("IPC: socket() failed: " + std::string(strerror(errno)));
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (sock_path.size() >= sizeof(addr.sun_path)) {
        log("IPC: socket path too long: " + sock_path);
        close(fd);
        return false;
    }
    strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log("IPC: bind() failed: " + std::string(strerror(errno)));
        close(fd);
        return false;
    }
    // Allow the 'input' group to connect (normal users added by the installer).
    // chown root:input + chmod 0660 → only root and input-group members can connect.
    // If chown fails (e.g. no input group), fall back to owner-only (0600).
    {
        struct group* grp = getgrnam("input");
        if (grp) {
            // Best-effort group ownership; if chown fails (e.g. unprivileged
            // start) we fall through to chmod which still tightens permissions.
            if (chown(sock_path.c_str(), 0, grp->gr_gid) != 0)
                log("IPC: chown(input group) failed: " + std::string(strerror(errno)) +
                    " — proceeding with default ownership.", true);
            chmod(sock_path.c_str(), 0660);
        } else {
            chmod(sock_path.c_str(), 0600); // fallback: owner only
        }
    }

    if (listen(fd, 8) < 0) {
        log("IPC: listen() failed: " + std::string(strerror(errno)));
        close(fd);
        return false;
    }
    ipc_sock_fd_.store(fd);

    ipc_running_.store(true);
    ipc_thread_ = std::thread([this] { ipc_serve_loop(); });
    log("IPC socket: " + sock_path, true);
    return true;
}

void AccelDaemon::stop_ipc_server() {
    ipc_running_.store(false);
    int fd = ipc_sock_fd_.exchange(-1);
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
    }
    if (ipc_thread_.joinable()) ipc_thread_.join();
    // The accept loop may still be returning from poll() when shutdown()
    // wakes it.  Do not close the descriptor until that thread has stopped:
    // closing it concurrently with poll/accept permits descriptor reuse and
    // can make the worker operate on an unrelated descriptor.
    if (fd >= 0) close(fd);
    if (!ipc_sock_path_.empty()) {
        unlink(ipc_sock_path_.c_str());
        ipc_sock_path_.clear();
    }
}

void AccelDaemon::ipc_serve_loop() {
    while (ipc_running_.load()) {
        // Guard: stop_ipc_server() may close the fd before we exit.
        int fd = ipc_sock_fd_.load();
        if (fd < 0) break;

        // Use poll() instead of select() to avoid FD_SETSIZE overflow when fd >= 1024.
        // select()'s FD_SET macro has undefined behaviour for fd >= FD_SETSIZE (typically 1024).
        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        int r = poll(&pfd, 1, 1000 /* ms */);
        if (r <= 0) continue; // timeout or error — check ipc_running_ again

        // R6: use the local 'fd' copy instead of ipc_sock_fd_ to avoid TOCTOU race
        // with stop_ipc_server() which may close ipc_sock_fd_ between poll() and accept4().
        int client = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) continue;
        handle_ipc_client(client);
        close(client);
    }
}

void AccelDaemon::handle_ipc_client(int client_fd) {
    // P121/BUG-01: SO_SNDTIMEO + SO_RCVTIMEO.  A client that sends a request
    // but never reads the (potentially large status) response would otherwise
    // let send() block for minutes once the socket buffer fills, stalling the
    // serial accept loop and locking out every other IPC caller.  With a 2 s
    // send timeout the worker drops the non-responsive peer and moves on.
    struct timeval tv { .tv_sec = 2, .tv_usec = 0 };
    struct timeval stv { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));

    // P121/BUG-07: total-request deadline.  The per-recv timeout alone lets a
    // low-and-slow client dribble (1 byte per <2 s) and hold the worker for
    // arbitrarily long; this caps the whole request (command line + body).
    const uint64_t deadline_ns = now_ns() + IPC_REQUEST_DEADLINE_NS;

    // Read the command line (up to a newline / 256 bytes).  Byte-wise recv is
    // fine here — IPC traffic is one short line per client.
    std::string line;
    char ch;
    while (line.size() < 256) {
        if (now_ns() >= deadline_ns) return; // slow command line — drop
        ssize_t r = recv(client_fd, &ch, 1, 0);
        if (r <= 0) return;
        if (ch == '\n') break;
        line.push_back(ch);
    }

    std::string response;
    if (line == "status") {
        response = status_json() + "\n";
    } else if (line == "ping") {
        response = "pong\n";
    } else if (line == "reload") {
        reload_flag_.store(true);
        response = "{\"ok\":true,\"message\":\"config reload scheduled\"}\n";
    } else if (line == "latency") {
        // Same effect as SIGUSR1, but accessible to any input-group user
        // even when the daemon runs as root via systemd (kill() returns
        // EPERM in that case).  Set a flag the main thread polls.
        latency_dump_flag_.store(true);
        response = "{\"ok\":true,\"message\":\"latency dump scheduled\"}\n";
        // If the client already half-closed its write side, our response would
        // be lost to EPIPE — but the dump still gets scheduled, which is all
        // the "latency" command promises.
    } else if (line.rfind("set_config ", 0) == 0) {
        // set_config <n>\n followed by exactly <n> bytes of config JSON.
        const char* sz = line.c_str() + strlen("set_config ");
        errno = 0;
        char* end = nullptr;
        unsigned long long body_len = strtoull(sz, &end, 10);
        if (errno != 0 || end == sz || body_len == 0 ||
            body_len > MAX_CONFIG_PUSH_BYTES || *end != '\0') {
            response = "{\"ok\":false,\"error\":\"invalid config payload size\"}\n";
        } else {
            std::string body;
            body.reserve((size_t)body_len);
            // P131/BUG-07: cap the body read at 5 s from body start, in
            // addition to the 10 s total-request deadline.
            const uint64_t body_deadline_ns = now_ns() + CONFIG_BODY_DEADLINE_NS;
            while (body.size() < (size_t)body_len) {
                if (now_ns() >= deadline_ns) break;      // total-request deadline
                if (now_ns() >= body_deadline_ns) break; // P131/BUG-07 body deadline
                char tmp[8192];
                size_t want = std::min<size_t>(sizeof(tmp),
                                               (size_t)body_len - body.size());
                ssize_t r = recv(client_fd, tmp, want, 0);
                if (r <= 0) break; // timeout or client disconnected
                body.append(tmp, (size_t)r);
            }
            if (body.size() != (size_t)body_len) {
                response = "{\"ok\":false,\"error\":\"incomplete config payload\"}\n";
            } else if (push_config(body)) {
                response = std::string("{\"ok\":true,\"config\":") +
                           json_str(config_path_) + "}\n";
            } else {
                response = "{\"ok\":false,\"error\":\"config rejected\"}\n";
            }
        }
    } else {
        response = "{\"error\":\"unknown command\"}\n";
    }

    // Write full response (handle partial writes).  P121/BUG-01: with
    // SO_SNDTIMEO set, w<=0 after a timeout means the peer stopped reading —
    // bail out and let the caller close the socket (the serial accept loop
    // must not stall on a stuck consumer).  P131: log it so a flapping
    // client is visible in the daemon's journal instead of silently dropped.
    const char* p = response.c_str();
    size_t left = response.size();
    while (left > 0) {
        ssize_t w = send(client_fd, p, left, MSG_NOSIGNAL);
        if (w <= 0) {
            // EPIPE/ECONNRESET: peer closed.  EAGAIN/EWOULDBLOCK: peer stopped
            // reading and the 2 s SO_SNDTIMEO elapsed.  Either way the client
            // is gone — log and drop.
            log("IPC: response send failed (" + std::string(strerror(errno)) +
                ") — dropping client.");
            return;
        }
        p += w; left -= (size_t)w;
    }
}

} // namespace rawaccel
