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
#include <pwd.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <functional>
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

/// Kernel ev.time is filled by the input core on CLOCK_REALTIME.  We never mix
/// it with CLOCK_MONOTONIC_RAW — only DELTAs of consecutive frame timestamps
/// are used for the SM-1 interval, so the clock base is irrelevant.
static inline uint64_t ev_now_us() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec / 1000);
}

/// uinput device fd from libevdev (defined early: used by create_virtual_device
/// for the RAC-1 O_NONBLOCK flag and by the write helpers).
static inline int uinput_fd(libevdev_uinput* uidev) {
    return libevdev_uinput_get_fd(uidev);
}

// ── P-APP: per-application profile matching ──────────────────────────────────
// A profile's match_app is matched case-insensitively as a SUBSTRING of the
// focused application class (WM_CLASS class, e.g. "firefox", "org.kde.krita").
// Substring semantics keep per-app configs tolerant of versioned WM_CLASSes
// (e.g. "Code" vs "code-server") while still letting "chromium" match anything
// Chromium-based that keeps the base class.
static bool ascii_icontains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t k = 0; k < needle.size(); ++k) {
            const char a = haystack[i + k], b = needle[k];
            if (std::tolower(static_cast<unsigned char>(a)) !=
                std::tolower(static_cast<unsigned char>(b))) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

/// Empty match_app matches everything (no app constraint).  Only meaningful
/// when the daemon has a focused-app report at all.
static bool profile_matches_app(const device_profile& p, const std::string& app) {
    if (p.match_app.empty()) return true;
    return ascii_icontains(app, p.match_app);
}

// ── sysfs device property helpers ────────────────────────────────────────────

/// P121/BUG-02: how long a device path whose I/O failed (EIO/ENODEV/EBADF
/// on read, or repeated failed open) stays on the re-open deny list.  A
/// broken node that remains listed in /dev/input would otherwise be
/// re-grabbed + uinput-create/destroyed every ~2 s forever.
/// P131: backoff shortened (was longer), R1-07: 10 s → 5 s — still stops the
/// ~2 s churn loop while a transient device hiccup recovers much sooner.
static constexpr double DENY_REOPEN_MS = 5000.0; // 5 s

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

/// Read the USB connection speed from sysfs `speed` and map it to the kernel
/// `enum usb_device_speed` semantics used by detect_polling_rate().
/// BUG-NEW-80: the sysfs `speed` file is a Mbps TEXT string ("1.5", "12",
/// "480", "5000", "10000"), NOT an enum integer.  sysfs_read_int() would turn
/// "12" into the int 12, which `>= 3` misread as HIGH speed (12 Mbps is
/// full-speed), silently applying 125µs bInterval math to a 1ms-frame device.
/// Returns:  3 = high-speed or faster (480+ Mbps, 125µs microframes)
///           2 = full-speed                 (12 Mbps, 1ms frames)
///           1 = low-speed                  (1.5 Mbps, 1ms frames)
///           <= 0 = unreadable / unknown (read(), parse or range failure)
static int sysfs_read_usb_speed(const std::string& sysfs_path) {
    FILE* f = fopen(sysfs_path.c_str(), "r");
    if (!f) return -1;
    char buf[64] = {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return -1;
    errno = 0;
    char* end = nullptr;
    const double mbps = std::strtod(buf, &end);
    if (end == buf || errno != 0 || !std::isfinite(mbps) || mbps <= 0)
        return -1;
    if (mbps >= 480.0) return 3; // High/Super/... Speed (125µs microframes)
    if (mbps >= 12.0)  return 2; // Full-speed (1ms frames)
    return 1;                    // Low-speed (1ms frames)
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
        // (12 Mbps) uses 1ms frames.  The `speed` node is a Mbps text string
        // ("1.5", "12", "480", "5000", ...) — decoded by sysfs_read_usb_speed()
        // into the kernel enum semantics below:
        //   1 = Low Speed (1.5 Mbps, not used for mice)
        //   2 = Full Speed (12 Mbps)
        //   3 = High Speed (480 Mbps, microframes)
        //   5 = SuperSpeed (5 Gbps, microframes)
        int rate_hz = 0;
        snprintf(buf, sizeof(buf), "/sys/class/input/event%d/device/device/speed", n);
        int usb_speed = sysfs_read_usb_speed(buf);
        if (usb_speed >= 3) {
            // High-speed or faster: bInterval is in 125µs units
            // For USB high-speed interrupt endpoints, bInterval is an
            // exponent: the period is 2^(bInterval-1) microframes.
            // Treating it as a linear divisor overestimates rates (e.g.
            // bInterval=4 is 1000 Hz, not 2000 Hz).
            if (binterval < 1 || binterval > 16) return 0;
            const double interval_us = 125.0 * (1u << (binterval - 1));
            rate_hz = static_cast<int>(1000000.0 / interval_us);
        } else if (usb_speed <= 0) {
            // BUG-16 fix: `speed` sysfs could not be read (missing/unknown).
            // Previously this fell into the full-speed branch and reported an
            // up-to-8× too low rate for high-speed gaming mice.  Try the
            // high-speed interpretation first; a valid high-speed result in
            // [POLL_RATE_MIN, POLL_RATE_MAX] wins, otherwise fall back to
            // the full-speed interpretation.  BUG-NEW-80: the old `== 0`
            // test was dead code — sysfs_read_int() returns -1, never 0.
            if (binterval >= 1 && binterval <= 16) {
                const double hs_interval_us = 125.0 * (1u << (binterval - 1));
                const int hs_rate = static_cast<int>(1000000.0 / hs_interval_us);
                if (hs_rate >= static_cast<int>(POLL_RATE_MIN) &&
                    hs_rate <= static_cast<int>(POLL_RATE_MAX))
                    return std::clamp(hs_rate, (int)POLL_RATE_MIN, (int)POLL_RATE_MAX);
            }
            rate_hz = static_cast<int>(std::llround(1000.0 / binterval));
        } else {
            // Full/low speed: bInterval is in 1ms units
            rate_hz = static_cast<int>(std::llround(1000.0 / binterval));
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
    int glob_result = glob("/dev/input/event*", 0, nullptr, &g);
    if (glob_result == 0) {
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
    if (glob_result == 0)
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
    // R1-04: multiple threads call log() — serialise.
    std::lock_guard<std::mutex> lk(log_mu_);
    if (log_cb_) log_cb_(msg);
    else         std::cout << "[rawaccel] " << msg << "\n";
}

bool AccelDaemon::start(const std::string& config_path) {
    if (running_.load()) return true;

    {   // D-1: main thread writes config_path_/config_ here while the IPC
        // server (started before start() by main.cpp) may already read them
        // under devices_mutex_.  Guard the writes so the status path never
        // observes a torn config_path_.
        std::lock_guard<std::mutex> lk(devices_mutex_);
        config_path_ = config_path.empty() ? find_config_path() : config_path;
        try {
            config_ = load_config(config_path_);
        } catch (std::exception& e) {
            log("Config load failed: " + std::string(e.what()) + " — using defaults.");
            config_.profiles.clear();
            device_profile dp;
            dp.name = "default";
            dp.dev_cfg.dpi = 800;
            dp.dev_cfg.polling_rate = 1000;
            config_.profiles.push_back(dp);
            config_.active_profile = "default";
        }
        config_hash_ = std::hash<std::string>{}(app_config_to_json(config_));
        // PAS-1: the top-level use_raw_input flag is now a real master switch.
        raw_input_enabled_.store(config_.use_raw_input, std::memory_order_relaxed);
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
    // R1-01: a thread body that escapes with an exception would hit
    // std::terminate() and kill the whole process.  Wrap every worker in an
    // outer try/catch that logs the failure and degrades to a graceful stop
    // (the inner loops already have targeted catches; this is the last line
    // of defense so a single bad code path can never crash the daemon).
    loop_thread_ = std::thread([this] {
        try {
            run_loop();
        } catch (const std::exception& e) {
            log(std::string("loop thread aborted: ") + e.what());
            request_stop();
        } catch (...) {
            log("loop thread aborted: unknown exception");
            request_stop();
        }
    });
    // P171-BFIX: HID++ notification draining on its own thread — never on the
    // loop thread where it would delay mouse event processing.
    hidpp_thread_ = std::thread([this] {
        try {
            run_hidpp_worker();
        } catch (const std::exception& e) {
            log(std::string("hidpp worker aborted: ") + e.what());
            request_stop();
        } catch (...) {
            log("hidpp worker aborted: unknown exception");
            request_stop();
        }
    });
    // R1-11: config persistence worker (drains save_q_ async).  Same
    // exception-containment pattern as the other workers.
    save_thread_ = std::thread([this] {
        try {
            save_worker();
        } catch (const std::exception& e) {
            log(std::string("save worker aborted: ") + e.what());
        } catch (...) {
            log("save worker aborted: unknown exception");
        }
    });
    log("Daemon started.");
    return true;
}

void AccelDaemon::stop() {
    running_.store(false);
    if (loop_thread_.joinable()) loop_thread_.join();
    if (hidpp_thread_.joinable()) hidpp_thread_.join();
    // Save worker drains whatever was still queued at stop time, so the last
    // pushed config is persisted even if shutdown raced an enqueue.
    if (save_thread_.joinable()) save_thread_.join();
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
    // Called from the IPC thread.  Parse + sanitize + de-dup here so a parse
    // error can be reported synchronously to the client; the actual disk write
    // (save_config) runs on save_thread_ (R1-11) and only a successful write
    // arms apply_new_config() on the loop thread.  Return semantics therefore
    // change: true now means "accepted and queued for save", not "saved"; a
    // save failure is logged by the worker and the config is not applied.
    try {
        // The IPC listener is created before start() by the executable so its
        // startup failure is non-fatal.  Do not let a client race that window
        // and make save_config() operate on an empty path.
        if (!running_.load(std::memory_order_acquire) || config_path_.empty())
            return false;
        // R10-PUSHG: a revert pushed while the PREVIOUS push is still armed
        // but not yet applied would slip through the guard below (it compares
        // against the APPLIED config).  Snapshot the pending config up front;
        // push_cfg_mu_ is released before devices_mutex_ is taken below to
        // keep the same lock order as the loop thread (push_cfg_mu_ →
        // devices_mutex_, never simultaneously here).
        bool has_pending = false;
        std::string pending_json;
        {
            std::lock_guard<std::mutex> lk(push_cfg_mu_);
            if (push_cfg_pending_) {
                pending_json = app_config_to_json(push_cfg_);
                has_pending = true;
            }
        }
        app_config cfg = app_config_from_json(json_str);
        {
            // PERF-2: no-op guard using hash of JSON (avoids double serialization).
            // config_hash_ is guarded by devices_mutex_ (updated in apply_new_config).
            std::lock_guard<std::mutex> lk(devices_mutex_);
            std::string new_json = app_config_to_json(cfg);
            if (has_pending && new_json == pending_json) {
                log("Config push skipped (matches pending un-applied config).", true);
                return true;
            }
            size_t new_hash = std::hash<std::string>{}(new_json);
            if (new_hash == config_hash_) {
                log("Config push skipped (no-op guard: hash unchanged).", true);
                return true;
            }
            // Hash differs — do full JSON comparison as fallback (hash collision defense).
            if (new_json == app_config_to_json(config_)) {
                config_hash_ = new_hash; // sync hash for future fast path
                log("Config push skipped (no-op guard: content unchanged).", true);
                return true;
            }
        }
            {
            // Enqueue for the save worker.  Off the IPC thread: a slow disk
            // must never stall a status/save IPC round-trip.
            // N-SAVEQ: each entry is a FULL config snapshot and the daemon only
            // ever applies the newest one, so any backlog ahead of this push is
            // already obsolete — coalesce (drop the backlog) instead of letting
            // a fast client grow the deque without bound at fsync speed.  An
            // in-flight save has already been popped, so it is never disturbed.
            std::lock_guard<std::mutex> lk(save_q_mu_);
            if (!save_q_.empty()) {
                save_q_.clear();
                log("Config push coalesced (N pending saves dropped).", true);
            }
            save_q_.emplace_back(std::move(cfg), config_path_);
        }
        log("Config push queued for save (" + config_path_ + ").", true);
        return true;
    } catch (std::exception& e) {
        log("Config push rejected: " + std::string(e.what()));
        return false;
    }
}

void AccelDaemon::save_worker() {
    // Sticky-flag + sleep-poll loop (the codebase avoids condition variables).
    // While running_ is false the queue is still drained so a config enqueued
    // concurrently with stop() is persisted, then the loop exits.
    for (;;) {
        std::optional<std::pair<app_config, std::string>> item;
        {
            std::lock_guard<std::mutex> lk(save_q_mu_);
            if (!save_q_.empty()) {
                item = std::move(save_q_.front());
                save_q_.pop_front();
            }
        }
        if (item) {
            try {
                // Atomic write to the daemon's own config path — a root systemd
                // daemon can persist to /etc/rawaccel/settings.json even though
                // the GUI/CLI can only write the user's ~/.config copy.
                save_config(item->first, item->second);
                {
                    // Arm the apply slot only after the file is on disk, so the
                    // live re-apply never precedes the persisted config.
                    std::lock_guard<std::mutex> lk(push_cfg_mu_);
                    push_cfg_        = std::move(item->first);
                    push_cfg_pending_ = true;
                }
                log("Config pushed over IPC (" + item->second + ").", true);
            } catch (const std::exception& e) {
                log("Config save failed: " + std::string(e.what()));
            } catch (...) {
                log("Config save failed: unknown exception");
            }
            continue;
        }
        if (!running_.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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

    std::string vname;
    {
        // R8-VNAME: uinput truncates the device name at 79 bytes.  A long
        // source name would silently clip the trailing "(RawAccel)"
        // self-identification marker — the daemon's name filter
        // (is_physical_mouse) would then no longer recognize its own vdev,
        // and a later hot-plug scan could grab the output device → double
        // acceleration.  Truncate the BASE name so the marker always survives.
        constexpr size_t kMaxUinputName = 79;          // UINPUT_MAX_NAME_SIZE - 1
        constexpr size_t kMarkerLen = 11;              // strlen(" (RawAccel)")
        constexpr const char* kMarker = " (RawAccel)";
        if (dev.name.size() + kMarkerLen > kMaxUinputName)
            vname = dev.name.substr(0, kMaxUinputName - kMarkerLen);
        else
            vname = dev.name;
        vname += kMarker;
    }
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
    // RAC-1: make the uinput sink non-blocking.  A slow consumer (compositor
    // stall, batch commit lag) then returns EAGAIN instead of blocking the
    // loop thread — the hot path can never be held hostage by the kernel
    // buffer.  uinput_write_retry() absorbs EAGAIN with bounded backoff.
    const int udev_fd = uinput_fd(uidev);
    if (udev_fd >= 0) {
        int flags = fcntl(udev_fd, F_GETFL);
        if (flags >= 0 && fcntl(udev_fd, F_SETFL, flags | O_NONBLOCK) < 0)
            log("create_virtual_device: fcntl(F_SETFL|O_NONBLOCK) failed: " +
                std::string(strerror(errno)) + " — sink stays blocking.", true);
    }
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

        // PAS-1: honour the top-level use_raw_input master switch (formerly
        // dormant — wired as the daemon's "intercept" gate).
        if (!raw_input_enabled_.load(std::memory_order_relaxed)) {
            log("Raw-input disabled in config — skipping device: " + dev.name, true);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }

        // HP-3: same physical device already open under another eventN node
        // (HID-composite / multi-interface mice expose several REL nodes with
        // one device_id).  Grab only the first — double-grab would duplicate
        // every physical report through two uinput devices.
        if (!dev.device_id.empty() &&
            opened_device_ids_.count(dev.device_id)) {
            log("Skipping duplicate device_id: " + dev.name + " (" +
                dev.device_id + ")", true);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }

        const device_profile* prof = find_profile(dev.device_id);

        // PAS-2: per-device disable flag — leave the device untouched so the
        // desktop handles it normally ("safe mode", no acceleration applied).
        if (prof && prof->dev_cfg.disable) {
            log("Skipping disabled device: " + dev.name +
                " [" + dev.device_id + "]", true);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }

        if (!create_virtual_device(dev)) {
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            // R7-UVIRT: uinput create failures (missing/unpermitted /dev/uinput)
            // are usually persistent — without a deny entry the setup scan
            // re-opens, re-grabs, re-releases the same node every cycle (log +
            // syscall churn while the failure persists).  Not setting missed_any
            // / rescan_needed_ here: the deny window gates the retry cadence and
            // the ~2 s empty/transient rescan stays available for transient cases.
            deny_reopen(dev.path, dev.device_id,
                        path_deny_until_ms_, dev_deny_until_ms_);
            continue;
        }

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
        if (!dev.device_id.empty()) opened_device_ids_.insert(dev.device_id);
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
        opened_device_ids_.clear();
        fd_to_dev_.clear();
    }
    for (auto& dev : to_destroy) {
        if (epoll_fd_ >= 0 && dev.fd_in >= 0 &&
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, dev.fd_in, nullptr) < 0)
            log("teardown: epoll_ctl(del) failed for " + dev.path + ": " +
                std::string(strerror(errno)), true);
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
    // P-APP: a profile with a non-empty match_app applies ONLY while the
    // focused application matches.  current_app_ is set from the GUI's
    // "set_active_app" IPC (WM_CLASS class, lowercased).  We check the app
    // constraint FIRST, per device preference order:
    //   1. device_id + app match       (highest priority)
    //   2. "all devices" + app match
    //   3. device_id (no app constraint, i.e. generic binding)
    //   4. "all devices" (generic fallback)
    //   5. active_profile / first profile (historic fallbacks)
    const bool have_app = !current_app_.empty();
    if (have_app) {
        for (auto& p : config_.profiles)
            if (!p.device_id.empty() && p.device_id == dev_id &&
                profile_matches_app(p, current_app_)) return &p;
        for (auto& p : config_.profiles)
            if (p.device_id.empty() &&
                profile_matches_app(p, current_app_)) return &p;
    }
    // 1. Device-specific assignment takes priority (C29-N1: apply the same
    //    app gate as the app-aware pass above — an app-scoped binding must
    //    NEVER leak into this generic pass, or it becomes a catch-all for
    //    every non-matching app / no-focus state).
    for (auto& p : config_.profiles)
        if (!p.device_id.empty() && p.device_id == dev_id &&
            profile_matches_app(p, current_app_)) return &p;
    // 2. "All devices" catch-all (P121/BUG-03): an empty device_id means the
    //    profile binds to every mouse without a device-specific match — the
    //    documented contract (config.hpp:18) and the GUI's "All devices"
    //    combo entry.  Previously these records were silently skipped unless
    //    they happened to be the active profile or profiles[0].
    //    (C29-N1: same app gate — empty match_app passes, app-bound does not.)
    for (auto& p : config_.profiles)
        if (p.device_id.empty() && profile_matches_app(p, current_app_)) return &p;
    // 3. Active profile
    for (auto& p : config_.profiles)
        if (p.name == config_.active_profile) return &p;
    // 4. First profile (last resort fallback)
    if (!config_.profiles.empty()) return &config_.profiles[0];
    return nullptr;
}

void AccelDaemon::apply_active_app() {
    // Consume the GUI's latest focus report (loop thread only).  IPC thread
    // wrote pending_app_ under active_app_mu_; we copy it into current_app_
    // and re-apply profiles only when the focused app actually changed.
    std::string next;
    {
        std::lock_guard<std::mutex> lk(active_app_mu_);
        if (!active_app_dirty_) return;
        active_app_dirty_ = false;
        next = pending_app_;
    }
    if (next == current_app_) return;

    if (next.empty())
        log("Focus: no application focused — app-specific profiles inactive.");
    else
        log("Focus: application \"" + next + "\" — re-applying profiles.");
    current_app_ = next;

    // Recompute the active profile for every open device, live (no grab drop).
    // LIVE-DISABLE: a profile that the app-switch just enabled/disabled is
    // applied/released in place — a device switched to a disabled profile is
    // released immediately instead of staying grabbed until replug.
    std::lock_guard<std::mutex> lk(devices_mutex_);
    for (auto it = devices_.begin(); it != devices_.end();) {
        const device_profile* prof = find_profile(it->device_id);
        if (prof && prof->dev_cfg.disable) {
            log("Active profile became disabled — releasing: " + it->name, true);
            release_device(*it);
            it = devices_.erase(it);
        } else {
            if (prof) apply_profile(*it, *prof);
            ++it;
        }
    }
    fd_to_dev_.clear();
    for (size_t i = 0; i < devices_.size(); i++)
        fd_to_dev_[devices_[i].fd_in] = i;
    // R10-REGRB: the focus switch may have re-enabled a profile for a device
    // that a previous switch live-released (it is no longer in devices_, so
    // the loop above could not apply it).  Kick the self-heal scan — it re-
    // opens devices that now match an enabled profile and skips still-disabled
    // ones (HP-1 gate), so a released device reappears within the ~2 s cadence
    // once its profile is active again.
    rescan_needed_.store(true);
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
    // SM-5: reconfigure (not init) — a running smoother keeps its EMA state so
    // a focus switch / hot reload changes the curve without tearing the
    // smoothed speed mid-motion.  Only a smoother being turned ON for the
    // first time is reset (avoids stale totals leaking in from halflife==0).
    dev.sp.reconfigure(prof.prof.speed_processor_args);
    // TEL-1: reset the telemetry generation counter on every profile apply.
    // Without it, a switch from accel → raw (raw path never increments the
    // counter) left a stale even counter that made old telem_* look "live" in
    // the GUI.  Zeroing samples makes status_json() report telem_ok=false
    // (cheap: two relaxed stores + a release; not on the per-event path, only
    // on profile applies).
    // R7-TELSR: seqlock-safe reset.  The hot path (flush_motion) bumps samples
    // ODD → writes payload → bumps EVEN, so the reader only pairs a snapshot
    // when both acquire-loads of the counter match.  The old reset zeroed the
    // payload and then store(0) directly: a reader that had already loaded the
    // PREVIOUS even counter could pair it with a partially-zeroed payload on
    // weak-memory hardware and pass s1==s2.  Mark the generation ODD (fetch_add
    //  release) BEFORE zeroing, then close with the 0 sentinel (release) — any
    // in-flight reader now observes ODD (mismatch/derailed) or 0 (telem_ok
    // =false), and never a stale-even pairing with torn zeros.
    dev.telemetry->samples.fetch_add(1, std::memory_order_release); // odd: in-progress
    dev.telemetry->speed_ips.store(0.0, std::memory_order_relaxed);
    dev.telemetry->out_ips.store(0.0, std::memory_order_relaxed);
    dev.telemetry->gain.store(0.0, std::memory_order_relaxed);
    // SYN-1: clear the remaining seqlock payload fields too.  An odd/even
    // counter mismatch would already make the reader drop the sample, but a
    // reader that loaded the OLD even counter could still pair it with stale
    // dx/dy/wall_ms values — writing them matters vs. leaving the old sample.
    dev.telemetry->dx.store(0.0, std::memory_order_relaxed);
    dev.telemetry->dy.store(0.0, std::memory_order_relaxed);
    dev.telemetry->wall_ms.store(0.0, std::memory_order_relaxed);
    dev.telemetry->samples.store(0, std::memory_order_release);
    // R12-LATRAW: the in-place telemetry reset above handles telem_* fields,
    // but the per-device latency histogram (lat_stats) is separate and only
    // flushed by flush_motion() — which never runs in raw passthrough.  On the
    // transition INTO raw 1:1 passthrough, clear it so the status JSON stops
    // publishing stale accel-era lat_* fields (AGENTS.md: raw passthrough must
    // NOT carry telemetry).  Narrow to raw so a live accel tuning reload never
    // wipes a histogram a user is actively reading.
    // R13-REALRATE: the measured polling rate has the exact same contract — it
    // is produced only by the flush_motion ring feed (below), which never runs
    // in raw mode.  Zero it AND drop the ring's sample history so the status
    // JSON stops publishing the last accel-era rate and a raw→accel switch
    // re-measures fresh from real frame intervals.
    if (prof.prof.raw_passthrough) {
        dev.lat.reset();
        dev.telemetry->real_polling_rate.store(0, std::memory_order_relaxed);
        dev.frame_ev_us_count = 0;
        dev.frame_ev_us_next = 0;
    }
    // R3-NEW-3: re-anchor the speed interval.  last_time_ms starts at 0 and —
    // critically — is NOT updated while the previous profile was in raw
    // passthrough (flush_motion() never runs there).  Without this refresh, the
    // first accelerated event after a raw→accel switch computed
    // time_ms = now − 0 → clamped to DEFAULT_TIME_MAX → speed ≈ 0 → gain ≈ 1:
    // a single suppressed frame.  Tying it to "now" on every profile apply makes
    // the first accelerated event measure a real interval (and also covers the
    // hot reload / device-connect cases the DEFAULT_TIME_MAX clamp was bare
    // mitigation for).
    dev.last_time_ms = now_ms();
    // The kernel-frame interval source (SM-1) must be re-anchored together with
    // the wall clock so a raw→accel switch doesn't measure a huge stale gap.
    if (dev.last_frame_ev_us == 0) {
        dev.last_frame_ev_us = ev_now_us();
    }
}

void AccelDaemon::release_device(mouse_device& dev) {
    // LIVE-DISABLE: called from apply_new_config()/apply_active_app() while
    // holding devices_mutex_.  Mirrors the disconnect-cleanup teardown
    // (daemon.cpp cleanup loop) for a device whose matched profile just became
    // disabled on a live reload — without this, the disable checkbox was only
    // honoured at setup/hotplug time and the device stayed grabbed+accelerated
    // until replug.
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, dev.fd_in, nullptr) < 0)
        log("disable-release: epoll_ctl(del) failed for " + dev.path + ": " +
            std::string(strerror(errno)), true);
    fd_to_dev_.erase(dev.fd_in);
    opened_paths_.erase(dev.path);
    if (!dev.device_id.empty())
        opened_device_ids_.erase(dev.device_id);
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
        config_hash_ = std::hash<std::string>{}(app_config_to_json(config_));
        for (auto it = devices_.begin(); it != devices_.end();) {
            const device_profile* prof = find_profile(it->device_id);
            if (prof && prof->dev_cfg.disable) {
                // LIVE-DISABLE: a reload/push that turned a device's profile off
                // must release the grab RIGHT NOW (release_device removes it from
                // epoll + the open sets and closes fd/uinput) — the old code
                // skipped applying but left the device grabbed+accelerated until
                // replug.
                log("Live reload disabled a grabbed device — releasing: " + it->name, true);
                release_device(*it);
                it = devices_.erase(it);
            } else if (prof) {
                apply_profile(*it, *prof);
                any_live = true;
                log("Live-updated profile for: " + it->name, true);
                ++it;
            } else {
                ++it;
            }
        }
        fd_to_dev_.clear();
        for (size_t i = 0; i < devices_.size(); i++)
            fd_to_dev_[devices_[i].fd_in] = i;
    }

    if (!any_live) {
        // No grabbed devices yet — do a full setup so new devices are opened.
        // F-4 (INFO) fast path: if the live grab set is unchanged (devices are
        // already open but no profile matched), a teardown+setup would close
        // and recreate the virtual uinput device — swallowing ~100-150 ms of
        // mouse input for every reload while all profiles are disabled/absent.
        // Re-applying the identity profile in place (no re-grab) is equivalent
        // to what teardown+setup yields for a profile-less reload; new/removed
        // devices are handled by the hot-plug scan, not here.
        bool have_open = false;
        {
            std::lock_guard<std::mutex> lk(devices_mutex_);
            have_open = !devices_.empty();
        }
        if (have_open) {
            std::lock_guard<std::mutex> lk(devices_mutex_);
            static const device_profile identity{};
            for (auto& dev : devices_) apply_profile(dev, identity);
            log("Reload: no profile matched — re-applied identity to open devices (fast path).");
        } else {
            teardown_devices();
            if (!setup_devices())
                log("Reload: no devices available after reload.");
        }
    }

    // N-BUG: the PAS-1 use_raw_input master switch was read only at start(), so
    // a reload/push that toggled it silently kept the OLD grab state — devices
    // stayed grabbed after the flag turned false (the daemon kept accelerating
    // a "raw" config), and re-enabling grabbed nothing until a restart.  The
    // grab set must follow the flag, so release everything on a turn-off and
    // re-scan on a turn-on (teardown/setup take devices_mutex_ themselves).
    const bool raw_now = config_.use_raw_input;
    if (raw_now != raw_input_enabled_.load(std::memory_order_relaxed)) {
        teardown_devices();
        raw_input_enabled_.store(raw_now, std::memory_order_relaxed);
        if (raw_now) {
            if (!setup_devices())
                log("Reload: no devices available after raw-input re-enable.");
        }
    }
    // R10-REGRB: this push may have re-enabled a profile whose device was
    // live-released earlier (it is no longer in devices_, so the loop above
    // cannot touch it).  Ask the self-heal scan to re-open now-matching
    // devices; do_hotplug_scan skips still-disabled profiles (HP-1), and the
    // flag clears itself once nothing is left to pick up.
    rescan_needed_.store(true);
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

    // R5-B: does any candidate mouse still need to be (re)opened?  Set on
    // transient failures (EIO, reopen-backoff, uinput/create error) so the
    // ~2 s self-heal rescan keeps running even while another device is open —
    // previously a second failed mouse was NEVER retried once devices_ was
    // non-empty.  Permanent gates (raw_input disabled, profile disabled,
    // duplicate device_id) intentionally do NOT set it — retrying them is a
    // no-op and would spam the log.
    bool missed_any = false;

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
            if (nowt < dn->second) {
                // R10-EIO: keep the self-heal cadence alive THROUGH the deny
                // window.  The window gates how often a transiently-failed
                // device is retried (P121/BUG-02), so skipping it must leave
                // missed_any set — otherwise rescan_needed_ clears after one
                // scan and the periodic rescan (and the retry) stops forever.
                missed_any = true;
                continue;
            }
            path_deny_until_ms_.erase(dn); // window expired — allow retry
        }
        log("Hot-plug: new mouse detected at " + path);

        mouse_device dev;
        dev.path = path;
        if (!open_input_device(dev)) {
            // P121/BUG-02: opening keeps failing on a dead-but-listed node
            // (EIO etc.); back it off so we don't retry every ~2 s forever.
            missed_any = true; // transient — keep the periodic rescan going
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
            missed_any = true; // backoff window may expire later — keep rescanning
            deny_reopen(path, dev.device_id, path_deny_until_ms_, dev_deny_until_ms_);
            continue;
        }

        // HP-3 (hot-plug): same physical device already open under another
        // eventN node (HID-composite / multi-interface mice expose several REL
        // nodes with one device_id).  setup_devices() guards this with
        // opened_device_ids_ but do_hotplug_scan only checked opened_paths_ —
        // a second node for a grabbed device was re-grabbed, duplicating every
        // physical report through a second uinput.
        if (!dev.device_id.empty() &&
            opened_device_ids_.count(dev.device_id)) {
            log("Hot-plug: duplicate device_id already grabbed — skipping: " +
                dev.name + " (" + dev.device_id + ")", true);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }

        // HP-1: honour the same PAS-1/PAS-2 gates as setup_devices. Without
        // this, the idle rescan re-captures mice that use_raw_input=false or a
        // dev_cfg.disable profile said to leave alone — "safe mode" silently
        // violated on every hot-plug cycle.
        if (!raw_input_enabled_.load(std::memory_order_relaxed)) {
            log("Hot-plug: raw-input disabled in config — skipping device: " +
                dev.name, true);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }
        const device_profile* prof = find_profile(dev.device_id);
        if (prof && prof->dev_cfg.disable) {
            log("Hot-plug: skipping disabled device: " + dev.name +
                " [" + dev.device_id + "]", true);
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            continue;
        }

        if (!create_virtual_device(dev)) {
            ioctl(dev.fd_in, EVIOCGRAB, 0);
            close(dev.fd_in);
            missed_any = true; // uinput/create failure is transient — retry soon
            // R7-UVIRT: deny under both keys so the rescan skips this node until
            // the window expires (usb:VVVV:...: + eventN paths) instead of
            // re-opening/re-grabbing/re-releasing it every ~2 s while the
            // failure persists.  remove-after plan in deny_reopen comment.
            deny_reopen(dev.path, dev.device_id,
                        path_deny_until_ms_, dev_deny_until_ms_);
            continue;
        }

        // Apply per-device profile assignment (same logic as setup_devices)
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
            missed_any = true; // registration failure is transient — retry soon
            continue;
        }

        opened_paths_.insert(path);
        if (!dev.device_id.empty()) opened_device_ids_.insert(dev.device_id);
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
                if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, it->fd_in, nullptr) < 0)
                    log("hot-plug: epoll_ctl(del) failed for " + it->path + ": " +
                        std::string(strerror(errno)), true);
                opened_paths_.erase(it->path);
                if (!it->device_id.empty())
                    opened_device_ids_.erase(it->device_id);
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

    // R5-B: publish whether a self-heal rescan is still pending so run_loop
    // keeps the ~2 s retry cadence alive until every candidate either opens or
    // lands in a permanent gate.
    rescan_needed_.store(missed_any);
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
/// device.  For directly connected mice, match by vendor:product.  For mice
/// behind a Unifying receiver, the HID++ device info contains the mouse's
/// serial number — match by that instead.  When several mice share the same
/// identifier we deliberately label nothing rather than risk painting the wrong
/// device; the reading is still logged.
void AccelDaemon::apply_hidpp_battery(const hidpp_device& dev,
                                      const hidpp_battery_info& b) {
    if (b.level == 255) return; // unknown level — nothing meaningful to merge

    std::string needle;
    // Prefer the HID++ device's serial number (mouse's serial) for matching,
    // as this uniquely identifies the mouse even behind a Unifying receiver.
    // Fall back to vendor:product for directly connected mice without serial.
    if (!dev.info.serial.empty()) {
        needle = dev.info.serial;
    } else {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04x:%04x", dev.vendor_id, dev.product_id);
        needle = buf;
    }

    std::lock_guard<std::mutex> lk(devices_mutex_);
    mouse_device* match = nullptr;
    int match_count = 0;
    for (auto& m : devices_) {
        if (m.device_id.find(needle) != std::string::npos) {
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
                "% (no evdev mouse matched id " + needle + ")", true);
    } else {
        log("HID++ battery: " + std::to_string(static_cast<int>(b.level)) +
                "% (multiple mice share id " + needle +
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
            // Use identify_logitech_devices (plural) to detect all device indices
            // on this hidraw node (receiver 0xFF, direct 0x00, paired 0x01-0x06).
            // identify_logitech_device (singular) only tries 0xFF/0x00 and would
            // miss mice behind a Unifying receiver.
            for (auto& dev : identify_logitech_devices(path)) {
                // protocol_version 0 = Logitech hidraw node with no HID++
                // protocol at all (e.g. the 046d:c542 Nano receiver): it is a
                // UI/CLI display entry only and has no battery or notification
                // stream to subscribe to.
                if (dev.info.protocol_version == 0) continue;
                log("HID++ device detected: " + path + " (idx=0x" +
                    std::to_string(static_cast<int>(dev.device_index)) + ")", true);
                hidpp_devs_.push_back(std::move(dev));
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

    // R8-HIDN: a Unifying/Nano receiver exposes ONE hidraw node shared by all
    // paired devices.  The old loop drained that SAME kernel fd once PER device;
    // the 0xFF "shell" entry (always first in hidpp_devs_) consumed every queued
    // notification, and the R6-4 filter then dropped the events that named other
    // device indexes — so paired mice receiver NO live battery/link notifications
    // (only the 60 s active query, which is why the symptom was "battery only
    // updates once a minute").  Drain ONCE per unique transport and route every
    // notification to the device its device_index names, classifying it against
    // that device's OWN feature map.
    std::vector<std::string> drained_paths;
    for (const auto& anchor : hidpp_devs_) {
        if (std::find(drained_paths.begin(), drained_paths.end(),
                      anchor.hidraw_path) != drained_paths.end())
            continue;
        drained_paths.push_back(anchor.hidraw_path);
        auto& transport_ptr = hidpp_transports_[anchor.hidraw_path];
        if (!transport_ptr || !transport_ptr->is_open()) continue;

        for (const auto& notification :
             transport_ptr->drain_notifications(8, std::chrono::milliseconds(0))) {
            // Route to the OWNING device: notifications are classified against
            // the target's own feature cache (hidpp_device::notification_feature_id).
            hidpp_device* target = nullptr;
            for (auto& d : hidpp_devs_) {
                if (d.hidraw_path != anchor.hidraw_path) continue;
                if (d.device_index == notification.device_index) { target = &d; break; }
            }
            if (!target) {
                // 0xFF broadcast, or a device index we have not identified yet:
                // attach to the first device on this transport (same attribution
                // the old code produced for whichever device it happened to drain).
                for (auto& d : hidpp_devs_) {
                    if (d.hidraw_path == anchor.hidraw_path) { target = &d; break; }
                }
            }
            if (!target) continue;

            const auto event = classify_hidpp_notification(*target, notification);
            if (event.kind == hidpp_notification_event_kind::unhandled) continue;
            switch (event.kind) {
            case hidpp_notification_event_kind::battery:
                if (event.battery) {
                    // BUG-24 (aj2): surface live battery notifications on
                    // the mouse device, not just the log line.
                    target->battery_level = event.battery->level;
                    apply_hidpp_battery(*target, *event.battery);
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
        }
    }
}

// ── Main loop (epoll-based) ───────────────────────────────────────────────────

void AccelDaemon::run_loop() {
    constexpr int MAX_EVENTS = 32;
    constexpr int MAX_CONSECUTIVE_EPOLL_ERRORS = 10;
    epoll_event events[MAX_EVENTS];
    int epoll_errors = 0;

    while (running_.load()) {
        // Handle a config pushed over IPC (GUI/CLI "set_config" — syncs the
        // user's edits into the daemon's own config file).  push_config() has
        // already validated and persisted it, so this cannot throw.
        // Process IPC push BEFORE SIGHUP reload: if both are pending
        // simultaneously, the IPC push is newer and should take precedence.
        {
            std::lock_guard<std::mutex> lk(push_cfg_mu_);
            if (push_cfg_pending_) {
                // R10-PUSHTC: mirror the SIGHUP reload path below — if the
                // apply throws (allocations in the profile/logging string
                // paths), the loop thread must not die via std::terminate.
                // Clear the flag either way so a stuck config is never
                // re-applied on every iteration (it stays on disk and takes
                // effect on the next daemon start).
                try {
                    apply_new_config(push_cfg_);
                    push_cfg_pending_ = false;
                    log("Applied config pushed over IPC.", true);
                } catch (std::exception& e) {
                    log("Config push apply failed (keeping current): " +
                        std::string(e.what()));
                    push_cfg_pending_ = false;
                }
            }
        }

        // P-APP: consume a focused-application report from the GUI (cheap
        // no-op when nothing changed — one mutex lock per loop iteration).
        apply_active_app();

        // Handle config reload request (SIGHUP or IPC "reload")
        if (reload_flag_.exchange(false)) {
            log("Reloading config...");
            try {
                apply_new_config(load_config(config_path_));
            } catch (std::exception& e) {
                log("Config reload failed (keeping current): " + std::string(e.what()));
            }
        }

        // Deferred hot-plug: wait for the kernel to finish creating the device node.
        // Wall-clock based: ~80ms after the hot-plug event, scan once.
        if (pending_hotplug_.load()) {
            const double t = now_ms();
            if (hotplug_start_ms_ == 0) {
                hotplug_start_ms_ = t;
            } else if (t - hotplug_start_ms_ >= 80.0) {
                pending_hotplug_.store(false);
                hotplug_start_ms_ = 0;
                do_hotplug_scan();
            }
        }

        // Self-healing startup: when no devices are open (no mice at boot, or
        // permission/conflict that later cleared), force a scan every ~2 s even
        // without an inotify event — so the daemon converges on its own instead
        // of waiting for an unplug/replug.  Cheap: find_mice() on retired fds.
        // R5-B: also keeps scanning while rescan_needed_ is set — a device that
        // failed with a transient error gets retried even when others are open
        // (previously a second failed mouse was never revisited).
        // L-BUG-2: snapshot the emptiness under the lock — devices_ is a
        // std::vector and reading .empty() unsynchronized while another thread
        // (hotplug) mutates it is a data race.
        bool devices_empty;
        {
            std::lock_guard<std::mutex> lk(devices_mutex_);
            devices_empty = devices_.empty();
        }
        if (devices_empty || rescan_needed_.load()) {
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
            // BUG-NEW-81: a persistent epoll error must NOT kill the loop.
            // The old `break` silently turned the daemon into a zombie
            // ("running", mouse unprocessed, no event loop) on any transient
            // EBADF/ENOMEM/EINVAL.  Count consecutive errors; only after a
            // sustained streak (the fd set is genuinely unusable) do we give
            // up and let the caller shut the daemon down in a visible way.
            log("epoll_wait error: " + std::string(strerror(errno)));
            if (++epoll_errors >= MAX_CONSECUTIVE_EPOLL_ERRORS) {
                log("epoll_wait failed " + std::to_string(epoll_errors) +
                    " times consecutively; shutting down.");
                request_stop();
                break;
            }
            continue;
        }
        epoll_errors = 0;

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            // inotify hot-plug event: drain events and set pending flag
            if (fd == inotify_fd_) {
                handle_hotplug();
                continue;
            }

            // O(1) fd -> device lookup.  R1-03: take the fd index under
            // devices_mutex_ so the map+vector read is never concurrent with
            // a hot-plug mutation from this thread's own scan or with the
            // hidpp battery thread's locked iteration.  The pointer stays
            // valid for the call: vector erase only happens in the cleanup
            // loop below, after this dispatch batch finishes.
            mouse_device* dev = nullptr;
            {
                std::lock_guard<std::mutex> lk(devices_mutex_);
                auto it = fd_to_dev_.find(fd);
                if (it != fd_to_dev_.end() && it->second < devices_.size())
                    dev = &devices_[it->second];
            }
            if (dev)
                process_device(*dev);
        }

        // Clean up any devices that got a fatal I/O error during processing
        std::vector<mouse_device> disc_devs;
        {
            std::lock_guard<std::mutex> lk(devices_mutex_);
            auto dit = devices_.begin();
            while (dit != devices_.end()) {
                if (dit->disconnected) {
                    log("Removing disconnected device: " + dit->name);
                    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, dit->fd_in, nullptr) < 0)
                        log("disconnect cleanup: epoll_ctl(del) failed for " +
                            dit->path + ": " + std::string(strerror(errno)), true);
                    fd_to_dev_.erase(dit->fd_in);
                    opened_paths_.erase(dit->path);
                    if (!dit->device_id.empty())
                        opened_device_ids_.erase(dit->device_id);
                    // P121/BUG-02: an I/O error often means the node is dead but
                    // still listed in /dev/input.  Deny immediate re-open so the
                    // ~2 s empty-rescan doesn't re-grab/uinput-churn it forever.
                    // P131: also deny by stable device_id so a kernel
                    // renumber (eventN → eventM) can't bypass the backoff.
                    deny_reopen(dit->path, dit->device_id,
                                path_deny_until_ms_, dev_deny_until_ms_);
                    // R10-EIO: a transient I/O error dropped this device from
                    // the grab set while another device is still open.  Kick
                    // the self-heal rescan so it is revisited once the 5 s
                    // deny window expires (do_hotplug_scan keeps the cadence
                    // alive through the window — see R10-EIO there); without
                    // this the device was never re-grabbed until replug.
                    rescan_needed_.store(true);
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

/// Retry a uinput write after EAGAIN.  RAC-1: the uinput fd is O_NONBLOCK (set
/// in create_virtual_device) so a slow consumer (compositor stall) can NEVER
/// block the hot path — when the kernel buffer is full a write returns EAGAIN
/// instead.  We retry with backoff because silently dropping the REL would lose
/// motion; a genuinely dead device keeps failing with a real error
/// (EPIPE/ENODEV/EBADF) that the caller turns into a disconnect.  The attempt
/// budget is bounded so a fully-stuck consumer costs at most ~120 ms of motor
/// stutter (dropping the tail) instead of a deadlocked loop thread.
/// always-respected no-op default keeps the single-event raw-path callers
/// unchanged (a lone dropped event is harmless); the batched flush passes the
/// daemon's log() so a dropped SYN frame is visible.
static bool uinput_write_retry(int fd, const struct input_event* ev, size_t nbytes,
                               const std::function<void(const std::string&)>& report = {}) {
    constexpr int   kMaxAttempts = 32;
    constexpr int   kMinDelayUs  = 50;
    constexpr int   kMaxDelayUs  = 4000;
    int             delay        = kMinDelayUs;
    size_t          done         = 0;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const uint8_t* p   = reinterpret_cast<const uint8_t*>(ev) + done;
        const size_t   len = nbytes - done;
        const ssize_t  got = write(fd, p, len);
        if (got < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct timespec ts;
                ts.tv_sec  = delay / 1'000'000;
                ts.tv_nsec = static_cast<long>(delay % 1'000'000) * 1000;
                nanosleep(&ts, nullptr);
                delay = (delay * 2 > kMaxDelayUs) ? kMaxDelayUs : delay * 2;
                continue;
            }
            return false;
        }
        if (got == 0) return false;
        done += static_cast<size_t>(got);
        if (done >= nbytes) return true;
    }
    // R5-A: attempt budget exhausted with `done < nbytes` — the tail (usually
    // the closing SYN_REPORT) was dropped.  The documented RAC-1 trade-off
    // keeps this behavior (drop the tail, never disconnect — a torn frame
    // merges with the next one; tearing down the device mid-stall is worse),
    // but the caller must not be told "all clear": a silent drop masked these
    // on every compositor/kernel stall.  Surface it through the report hook,
    // throttled to one message per ~2 s so a fully-stuck consumer can't flood
    // the log.
    if (done < nbytes) {
        static double last_log_ms = -1e9;
        const double   now_l      = now_ms();
        if (last_log_ms < 0 || now_l - last_log_ms >= 2000.0) {
            last_log_ms = now_l;
            if (report) report("uinput write stalled: dropped " +
                               std::to_string(nbytes - done) +
                               " tail byte(s) of a " +
                               std::to_string(nbytes) +
                               "-byte frame after " +
                               std::to_string(kMaxAttempts) + " attempts).");
        }
    }
    return true;
}

/// RAC-1 (raw path): single-event variant with the same bounded EAGAIN
/// absorption as the batched path.  The raw-passthrough and overflow paths
/// originally called libevdev_uinput_write_event() directly — on the
/// O_NONBLOCK uinput fd (set in create_virtual_device) a transient
/// compositor/kernel stall returns -EAGAIN, which used to tear the virtual
/// device down and flap the mouse until the next ~2 s scan.
static inline bool uinput_write_retry_ev(libevdev_uinput* uidev,
                                         unsigned int type, unsigned int code,
                                         int value) {
    const int fd = uinput_fd(uidev);
    if (fd < 0) return false;
    input_event ev{};
    ev.type  = type;
    ev.code  = code;
    ev.value = value;
    return uinput_write_retry(fd, &ev, sizeof(ev));
}

/// P93-BATCH (PERF): accumulate output events in a small stack buffer and submit
/// them all in ONE write() at the real SYN_REPORT.  A typical accel frame
/// (REL_X, REL_Y, SYN) used to cost two write() syscalls; with this accumulator
/// the whole frame — motion REL, any queued buttons and the closing SYN — goes
/// out in a single syscall.  Order is preserved ([motion…][buttons…][SYN]); a
/// full buffer only forces an early flush, so no event is ever dropped.
struct write_batch {
    input_event evs[16];
    size_t      n = 0;
    // R5-A: invoked when the bounded EAGAIN budget runs out and a whole frame
    // tail (usually the closing SYN) is dropped.  Set by process_device to the
    // daemon's log() so the silent-drop case becomes diagnosable.
    std::function<void(const std::string&)> drop_report;

    bool add(unsigned int type, unsigned int code, int value) {
        if (n >= 16) return false;
        input_event& e = evs[n++];
        e       = {};
        e.type  = type;
        e.code  = code;
        e.value = value;
        return true;
    }

    /// Add, flushing the current buffer first if it would overflow.  Returns
    /// false only on a hard uinput write error (caller disconnects the device).
    bool add_flush_if_full(libevdev_uinput* uidev, unsigned int type,
                           unsigned int code, int value) {
        if (n >= 16 && !flush(uidev)) return false;
        return add(type, code, value);
    }

    /// Add a REL pair (zero-valued axes are skipped, as before).
    bool add_rel(libevdev_uinput* uidev, int x, int y) {
        if (x != 0 && !add_flush_if_full(uidev, EV_REL, REL_X, x)) return false;
        if (y != 0 && !add_flush_if_full(uidev, EV_REL, REL_Y, y)) return false;
        return true;
    }

    /// Add the frame-closing SYN_REPORT.
    bool add_syn(libevdev_uinput* uidev) {
        return add_flush_if_full(uidev, EV_SYN, SYN_REPORT, 0);
    }

    /// Submit the whole buffer in ONE write() syscall and reset.  Returns false
    /// on a hard uinput error; EAGAIN is absorbed by the bounded retry (RAC-1).
    bool flush(libevdev_uinput* uidev) {
        if (n == 0) return true;
        const int fd = uinput_fd(uidev);
        if (fd < 0) return false;
        const bool ok = uinput_write_retry(fd, evs, n * sizeof(input_event),
                                           drop_report);
        n = 0;
        return ok;
    }
};

/// P93 (legacy ledger): the original "REL_X + REL_Y in one write" idea is
/// superseded by write_batch above, which also merges the closing SYN_REPORT
/// into the same syscall.
/// libevdev_uinput_write_event() calls (each = one write()).  The kernel uinput
/// driver injects every input_event struct found in the write buffer, so the
/// event stream is byte-identical — this only collapses the two syscalls into
/// one.  Zero-valued axes are skipped (behaves exactly like the old
/// `if (x != 0) write; if (y != 0) write` sequence).  Returns false on any
/// hard write error so the caller can mark the device disconnected (EAGAIN is
/// absorbed by the bounded retry, never a disconnect — RAC-1).
/// Apply acceleration to accumulated (dx,dy) and write REL events to uidev.
/// Updates dev timing and subpixel remainder. Does NOT write SYN.
/// Measures processing latency in µs as time from lat_anchor_ns (initially the
/// process_device() read-batch start) to this last write; the anchor is then
/// moved up to t_now so a SECOND flush in the same batch quantifies its own
/// work instead of re-measuring the whole batch (MED-4).
/// frame_ev_us = kernel ev.time (µs, CLOCK_REALTIME) of the SYN_REPORT that
/// closed this frame — the SM-1 interval is measured as
/// DELTA(last_frame_ev_us → frame_ev_us), the true USB poll period.  Wall
/// clock (last_time_ms) remains the fallback for the first frame and for the
/// raw path, and stays in sync so a later raw→accel switch re-anchors cleanly.
/// Returns false if a uinput write fails (caller should mark dev as disconnected).
static bool flush_motion(mouse_device& dev, libevdev_uinput* uidev,
                         double dx, double dy, uint64_t& lat_anchor_ns,
                         uint64_t frame_ev_us, write_batch& out) {
    const uint64_t t_now = now_ns(); // interval / telemetry timestamp

    // Raw passthrough: bypass the entire acceleration pipeline.
    // No rotation, no snap, no speed clamp, no weights, no subpixel accumulation.
    // dx/dy are already integer counts from the kernel — write them directly.
    if (dev.settings.prof.raw_passthrough) {
        // Defense-in-depth: clamp to INT range before casting (mirrors the
        // guard in motion_math.hpp). A pathological event batch with millions
        // of REL_X events in one SYN frame would otherwise be UB on cast.
        constexpr double INT_LO = static_cast<double>(INT_MIN);
        // BUG-CRIT-2: static_cast<double>(INT_MAX) rounds up to 2147483648.0 and
        // casting that back to int is UB.  Largest double whose truncation fits.
        constexpr double INT_HI = 2147483647.5;
        if (!std::isfinite(dx)) dx = 0;
        if (!std::isfinite(dy)) dy = 0;
        int ix = static_cast<int>(std::clamp(dx, INT_LO, INT_HI));
        int iy = static_cast<int>(std::clamp(dy, INT_LO, INT_HI));
        if (!out.add_rel(uidev, ix, iy)) return false;
        double lat_us = static_cast<double>(t_now - lat_anchor_ns) / 1000.0;
        lat_anchor_ns = t_now;
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
        dev.telemetry->wall_ms.store(static_cast<double>(t_now) / 1'000'000.0,
                                     std::memory_order_relaxed);
        // bump counter to even (write complete)
        dev.telemetry->samples.fetch_add(1, std::memory_order_release);
        return true;
    }

    // P93-perf: derive the interval timestamp from the latency-start read
    // (t_now) instead of calling now_ms() again.  The interval is measured
    // start-to-start on the same CLOCK_MONOTONIC_RAW source when the SM-1
    // kernel ev.time path is unavailable (first frame / pre-grab-anchor).
    double now = static_cast<double>(t_now) / 1'000'000.0; // ns -> ms, same clock source
    double time_ms;
    // SM-1: prefer the kernel frame-interval.  ev.time deltas are stamped by
    // the input core at each SYN_REPORT, so a coalesced read batch (loop
    // stall, scheduler preempt, wayland hiccup) can no longer shrink time_ms
    // toward zero and spike the gain — the poll period is what the device
    // actually reported, not what the process got around to.
    if (dev.last_frame_ev_us != 0 && frame_ev_us != 0 && frame_ev_us > dev.last_frame_ev_us) {
        // SM-1: true USB poll period from the kernel frame stamps.
        double gap_ms = static_cast<double>(frame_ev_us - dev.last_frame_ev_us) / 1000.0;
        // SM-4: after a genuine idle pause (no SYN of any kind for ≥ idle-gap
        // threshold) the first motion frame would measure the WHOLE gap —
        // clamped to DEFAULT_TIME_MAX → speed ≈ 0 → gain ≈ 1: the first flick
        // arrives one frame late ("post-idle kick").  Re-measure the fresh
        // motion against ONE nominal poll period so it is judged at real speed.
        // Devices that keep emitting empty SYN_REPORTs during idle are covered
        // by the empty-frame re-anchor (the SYN_REPORT handler advances
        // last_frame_ev_us even when has_motion == false), so they rarely reach
        // this threshold; silent devices rely on it.
        const double kIdleGapMs = DEFAULT_TIME_MAX; // ≥100 ms without frames = true idle
        if (gap_ms >= kIdleGapMs) {
            // SM-4/…-FIX: re-measure against the best-known polling rate.
            // Priority: real-time measured (from event timestamps) > sysfs-detected > profile nominal.
            // The real_polling_rate is computed from median kernel frame intervals (POLL-1 fix).
            const double det_hz = dev.telemetry->real_polling_rate.load(std::memory_order_relaxed) > 0
                ? static_cast<double>(dev.telemetry->real_polling_rate.load(std::memory_order_relaxed))
                : (dev.detected_polling_rate > 0
                    ? static_cast<double>(dev.detected_polling_rate)
                    : static_cast<double>(dev.poll_rate));
            time_ms = 1000.0 / std::max(det_hz, static_cast<double>(POLL_RATE_MIN));
        } else {
            time_ms = gap_ms;
        }
    } else {
        // Fallback / first frame: wall clock.  last_time_ms==0 on the first
        // call, so time_ms is huge → clamped to DEFAULT_TIME_MAX — correct.
        time_ms = now - dev.last_time_ms;
    }
    // D6: modify() returns early when time<=0 (no motion applied).
    // flush_motion follows the same strategy: don't send motion for zero/negative intervals.
    // SM-8: floor the ENTIRE (0, DEFAULT_TIME_MIN] band (not just <=0) — a
    // sub-minimum interval would otherwise slip through and inflate
    // speed = |d|·dpi_factor/time toward infinity for the clamp's own gain
    // spike.  DEFAULT_TIME_MIN is half a floor-frame at the max poll rate.
    if (time_ms < DEFAULT_TIME_MIN) time_ms = DEFAULT_TIME_MIN;
    if (time_ms > DEFAULT_TIME_MAX) time_ms = DEFAULT_TIME_MAX;
    dev.last_time_ms = now;
    // R11-POLL-1: feed the real polling-rate ring HERE, while last_frame_ev_us
    // still holds the PREVIOUS frame base (the SM-1 interval above is exactly
    // what we measure).  The old feed site lived in the SYN handler's
    // empty-frame path and sampled AFTER this function had advanced the anchor,
    // so on this motion path it always measured interval 0 and the ring only
    // ever filled from empty frames — a device that always moves silently
    // starved real_polling_rate and SM-4 / the status JSON fell back to
    // sysfs/nominal.
    if (dev.last_frame_ev_us != 0 && frame_ev_us > dev.last_frame_ev_us) {
        uint64_t interval_us = frame_ev_us - dev.last_frame_ev_us;
        // Only accept reasonable intervals (0.1ms - 10ms = 100Hz-10kHz)
        if (interval_us >= 100 && interval_us <= 10000) {
            dev.frame_ev_us_samples[dev.frame_ev_us_next] = interval_us;
            dev.frame_ev_us_next =
                (dev.frame_ev_us_next + 1) % mouse_device::POLL_RATE_SAMPLES;
            if (dev.frame_ev_us_count < mouse_device::POLL_RATE_SAMPLES)
                dev.frame_ev_us_count++;
            // Compute median polling rate from samples
            if (dev.frame_ev_us_count >= 4) {
                uint64_t sorted[mouse_device::POLL_RATE_SAMPLES];
                int n = dev.frame_ev_us_count;
                for (int k = 0; k < n; ++k)
                    sorted[k] = dev.frame_ev_us_samples[k];
                // Simple insertion sort for small array
                for (int k = 1; k < n; ++k) {
                    uint64_t key = sorted[k];
                    int j = k - 1;
                    while (j >= 0 && sorted[j] > key) {
                        sorted[j + 1] = sorted[j];
                        j--;
                    }
                    sorted[j + 1] = key;
                }
                uint64_t median_us = sorted[n / 2];
                if (median_us > 0) {
                    int rate_hz = static_cast<int>(1000000.0 / median_us + 0.5);
                    if (rate_hz >= 100 && rate_hz <= 10000)
                        dev.telemetry->real_polling_rate.store(
                            rate_hz, std::memory_order_relaxed);
                }
            }
        }
    }
    // Advance the kernel-frame anchor so consecutive frames measure their own
    // true intervals (the SYN that closed this frame is the next frame's base).
    if (frame_ev_us != 0) dev.last_frame_ev_us = frame_ev_us;

    int out_x = 0, out_y = 0;
    apply_motion_math(dev.mod, dev.sp, dev.settings, dev.dpi_factor, time_ms,
                      dx, dy, dev.remainder_x, dev.remainder_y, out_x, out_y);

    // P93-BATCH: defer the write — the closing SYN_REPORT is merged into the
    // same buffer and the whole frame goes out in ONE write() at the real
    // frame boundary (process_device SYN_REPORT handler).
    if (!out.add_rel(uidev, out_x, out_y)) return false;

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

    // Record processing latency (µs): time from the latency anchor (initially
    // the process_device() read-batch start — button/wheel work included — to
    // this last write).  A subsequent flush in the same batch is measured from
    // this write instead (MED-4), so each flush quantifies only its own work.
    double lat_us = static_cast<double>(t_now - lat_anchor_ns) / 1000.0;
    lat_anchor_ns = t_now;
    dev.lat.record(lat_us);
    return true;
}

void AccelDaemon::process_device(mouse_device& dev) {
    auto* uidev = dev.uidev;
    if (!uidev) return;
    // D-4/MED-4: latency is anchored at the start of this read batch so button
    // / wheel events processed before the motion SYN are counted too.
    // flush_motion() measures against it and slides it forward after each
    // flush so a second flush in a batch quantifies only its own work.
    uint64_t lat_anchor_ns = now_ns();
    // P93-BATCH: per-frame output accumulator — see struct write_batch above.
    // Everything a frame produces (motion REL, queued buttons, SYN) flushes in
    // ONE write() at the real SYN_REPORT instead of one syscall per event.
    write_batch out;
    out.drop_report = [this](const std::string& m) { log(m); };

    // R1-08: a pathological/foreign device that never signals EAGAIN could
    // spin this drain loop forever and hold the whole loop thread.  Cap each
    // read batch; level-triggered epoll re-reports the fd so the remainder
    // is picked up on the next dispatch — no events are lost.
    constexpr int kMaxDrainPerBatch = 4096;
    int drained = 0;

    // P93-PERF: batched event reads — a typical frame (REL_X, REL_Y, SYN = 3
    // events) now needs ONE read() syscall instead of three.  The per-event
    // processing body below iterates read_batch via the inner for loop.
    std::array<input_event, 32> read_batch;

    // Accumulate relative motion in this batch
    double dx = 0, dy = 0;
    bool has_motion  = false;
    bool wrote_unsynced_event = false;
    // SM-2: non-motion events (buttons, wheel, tilt) arriving between a motion
    // frame and its SYN must NOT trigger a premature flush_motion() — doing so
    // split one hardware frame across two output frames and halved/quartered
    // the measured time_ms (→2×–4× speed → spiked gain).  They are buffered and
    // written once as a group at the frame's real SYN_REPORT, preserving the
    // kernel's own frame grouping.
    std::array<input_event, 16> queued_events;
    size_t queued_count = 0;

    // BUG-18: syn_dropped is now a device-state field (mouse_device::syn_dropped)
    // so a SYN_DROPPED event in one read batch is correctly remembered until
    // the matching SYN_REPORT arrives in the next process_device() invocation.
    bool& syn_dropped = dev.syn_dropped;

    auto flush_pending_motion = [&](uint64_t frame_ev_us) -> bool {
        if (!has_motion) return true;
        if (!flush_motion(dev, uidev, dx, dy, lat_anchor_ns, frame_ev_us, out)) {
            dev.disconnected = true;
            return false;
        }
        dx = dy = 0;
        has_motion = false;
        return true;
    };

    auto flush_queued = [&]() -> bool {
        for (size_t i = 0; i < queued_count; ++i) {
            const input_event& e = queued_events[i];
            // P93-BATCH: accumulate into `out` — the frame SYN flushes the whole
            // buffer in one write().  On overflow flush in place so nothing is
            // dropped and event order is preserved.
            if (!out.add(e.type, e.code, e.value)) {
                if (!out.flush(uidev)) { dev.disconnected = true; return false; }
                if (!out.add(e.type, e.code, e.value)) { dev.disconnected = true; return false; }
            }
        }
        queued_count = 0;
        return true;
    };

    // LOW-1 / BUG-CRIT-1: merge a motion tail + its non-motion companions that
    // a previous batch ended with (no SYN).  They flush at THIS batch's real
    // SYN_REPORT, with the interval measured from the frame that opened them.
    if (dev.has_pending_motion) {
        dx = dev.pending_dx;
        dy = dev.pending_dy;
        has_motion = true;
        if (dev.pending_ev_count > 0) {
            for (size_t i = 0; i < dev.pending_ev_count && queued_count < queued_events.size(); ++i)
                queued_events[queued_count++] = dev.pending_events[i];
        }
        dev.has_pending_motion = false;
        dev.pending_dx = dev.pending_dy = 0.0;
        dev.pending_ev_count = 0;
    }

    // Read all pending events (batched — see read_batch above).
    while (drained++ < kMaxDrainPerBatch) {
        const ssize_t n = read(dev.fd_in, read_batch.data(),
                               static_cast<size_t>(read_batch.size()) * sizeof(input_event));
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
        const size_t read_count = static_cast<size_t>(n) / sizeof(input_event);
        if (read_count == 0) {
            // BUG-NEW-82: a short read is not a recoverable partial event —
            // the input-event stream is word-sized and any partial read leaves
            // the stream misaligned.  Log it (verbose) so silent data loss on
            // a torn read is at least diagnosable, then drop the tail.
            log("read() short read (" + std::to_string(n) + " of " +
                std::to_string(static_cast<size_t>(read_batch.size()) * sizeof(input_event)) +
                " bytes) on " + dev.name, true);
            break;
        }

for (size_t i = 0; i < read_count; ++i) {
            const input_event& ev = read_batch[i];
            if (ev.type == EV_SYN) {
            if (ev.code == SYN_DROPPED) {
                // Kernel dropped events due to buffer overflow.
                // Per the Linux input protocol, only events AFTER SYN_DROPPED
                // (until the next SYN_REPORT) are unreliable and must be
                // discarded.  Motion that ALREADY accumulated in this batch was
                // written into the kernel buffer BEFORE the overflow — it is
                // legal and must not be silently lost (RAC-4).  Park it into the
                // same LOW-1 tail mechanism a split batch uses, so the next
                // genuine SYN_REPORT flushes it with a real interval instead of
                // dropping it (loss becomes at most one poll frame of delay).
                if (has_motion || queued_count > 0) {
                    dev.pending_dx   += dx;
                    dev.pending_dy   += dy;
                    dev.has_pending_motion = true;
                    for (size_t i = 0; i < queued_count && dev.pending_ev_count < dev.pending_events.size(); ++i)
                        dev.pending_events[dev.pending_ev_count++] = queued_events[i];
                    queued_count = 0;
                }
                dx = dy = 0;
                has_motion = false;
                syn_dropped = true;
                continue;
            }
            // R1-06: the dropped window is [SYN_DROPPED, SYN_REPORT].  Only a
            // genuine SYN_REPORT marks its end.  A previous version cleared the
            // flag on ANY EV_SYN event — so a SYN_MT_REPORT or SYN_CONFIG that
            // happened to arrive inside the window would silently end the drop,
            // and the remaining unreliable motion events between it and the real
            // SYN_REPORT got forwarded as if they were fresh.  Other EV_SYN
            // subtypes inside the window stay in the dropped state (and are
            // themselves discarded).
            if (syn_dropped) {
                if (ev.code == SYN_REPORT) {
                    syn_dropped = false;
                    // RAC-4: re-anchor the SM-1 frame-interval base at the END
                    // of the dropped window.  Kernel timestamps of the window
                    // events are unreliable; counting from the pre-drop base
                    // would make the first post-drop frame measure a huge (or
                    // inverted) interval.  This SYN's ev.time is the true start
                    // of the fresh frame.
                    if (ev.time.tv_sec != 0 || ev.time.tv_usec != 0) {
                        const uint64_t t = static_cast<uint64_t>(ev.time.tv_sec) * 1'000'000ULL +
                                           static_cast<uint64_t>(ev.time.tv_usec);
                        if (t != dev.last_frame_ev_us)
                            dev.last_frame_ev_us = t;
                    }
                    dev.last_time_ms = now_ms();
                }
                // Do NOT flush any motion or forward this SYN (the dropped
                // window ends here; fresh data starts from the next event batch).
                continue;
            }
            if (ev.code != SYN_REPORT) {
                // RAC-5: only a genuine SYN_REPORT delimits an output frame.
                // Other SYN subtypes (SYN_MT_REPORT, SYN_CONFIG…) do NOT close
                // a frame, so flushing accumulated motion here would split it.
                // R8-SYNMT: in accel mode the matching slot data (ABS_MT_*) is
                // queued for the frame SYN, so writing a SYN_MT_REPORT inline
                // would emit it BEFORE its slot data — an order inversion that
                // breaks libinput's touch tracking.  Queue it with the frame
                // and let the SYN_REPORT flush preserve source order.  Raw
                // passthrough keeps the 1:1 inline contract.
                if (dev.settings.prof.raw_passthrough) {
                    if (!uinput_write_retry_ev(uidev, ev.type, ev.code, ev.value))
                        { dev.disconnected = true; return; }
                    wrote_unsynced_event = true;
                } else if (queued_count < queued_events.size()) {
                    queued_events[queued_count++] = ev;
                } else {
                    // Pathological >16-event burst: forwarding the subtype now
                    // (before its queued slot data) beats dropping it.
                    if (!flush_queued()) return;
                    if (!out.flush(uidev)) { dev.disconnected = true; return; }
                    if (!uinput_write_retry_ev(uidev, ev.type, ev.code, ev.value))
                        { dev.disconnected = true; return; }
                    wrote_unsynced_event = true;
                }
                continue;
            }
            // ── Genuine SYN_REPORT: real frame boundary ──
            // SM-1: capture the kernel frame timestamp.  Deltas of these (not
            // the wall clock) measure the true USB poll period, immune to
            // loop-thread stalls and clock-base quirks.
            uint64_t frame_ev_us = 0; // 0 → flush_motion falls back to wall clock
            if (ev.time.tv_sec != 0 || ev.time.tv_usec != 0) {
                frame_ev_us = static_cast<uint64_t>(ev.time.tv_sec) * 1'000'000ULL +
                              static_cast<uint64_t>(ev.time.tv_usec);
            }
            if (!flush_pending_motion(frame_ev_us)) return;
            if (!flush_queued()) return;
            // SM-4: a device that keeps emitting EMPTY SYN_REPORTs during idle
            // must still advance the SM-1 interval base here.  Previously only
            // the motion path (flush_motion) advanced last_frame_ev_us, so the
            // first motion frame after a pause measured the WHOLE idle gap
            // (→ DEFAULT_TIME_MAX → under-gained kick).  When flush_pending_motion
            // did run, flush_motion already advanced both anchors to this same
            // SYN's values — setting them again is idempotent.  Silent devices
            // (no frames at all during idle) are covered by the kIdleGapMs
            // re-measurement inside flush_motion instead.
            if (frame_ev_us != 0) {
                // R11-POLL-1: the real polling-rate ring is fed inside
                // flush_motion (motion path), where the PREVIOUS anchor is
                // still intact — sampling here, after flush_motion advanced
                // the anchor, always measured interval 0 for motion frames.
                // This empty-frame path keeps ONLY the SM-4 anchor re-advance:
                // idle devices that keep emitting empty SYN_REPORTs must not
                // measure the whole idle gap on their next motion frame.
                dev.last_frame_ev_us = frame_ev_us;
            }
            dev.last_time_ms = now_ms();
            // P93-BATCH: close this frame with ONE write() syscall — the motion
            // REL plus any queued non-motion events and the closing SYN_REPORT
            // are all in the same buffer (the kernel injects every input_event
            // found in a write).  Byte order: [motion…][buttons…][SYN].
            if (!out.add_syn(uidev)) { dev.disconnected = true; return; }
            if (!out.flush(uidev)) { dev.disconnected = true; return; }
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
                    if (!uinput_write_retry_ev(uidev, ev.type, ev.code, ev.value))
                        { dev.disconnected = true; return; }
                    wrote_unsynced_event = true;
                } else if (ev.code == REL_X) {
                    dx += ev.value; has_motion = true;
                } else {
                    dy += ev.value; has_motion = true;
                }
            } else {
                // Wheel / tilt / other relative axes — SM-2: buffer and write
                // at the frame SYN instead of flushing the motion early.
                // RAW-FIX: in raw_passthrough mode forward EVERY event
                // immediately (not just REL_X/REL_Y) so the output stream
                // order matches the source exactly.  Queuing a wheel/tilt event
                // until the SYN would reorder it behind a raw REL already
                // forwarded — violating the 1:1 bit-faithful contract.
                if (dev.settings.prof.raw_passthrough) {
                    if (!uinput_write_retry_ev(uidev, ev.type, ev.code, ev.value))
                        { dev.disconnected = true; return; }
                    wrote_unsynced_event = true;
                } else if (queued_count < queued_events.size()) {
                    queued_events[queued_count++] = ev;
                } else {
                    // A run of >16 pre-SYN non-motion events is pathological;
                    // write the backlog now rather than drop buttons (SM-2 only
                    // forbids flushing MOTION before the SYN — forwarding the
                    // non-motion group early changes no interval).
                    if (!flush_queued()) return;
                    if (!out.flush(uidev)) { dev.disconnected = true; return; }
                    if (!uinput_write_retry_ev(uidev, ev.type, ev.code, ev.value))
                        { dev.disconnected = true; return; }
                    wrote_unsynced_event = true;
                }
            }
        } else {
            // Buttons / misc — SM-2: buffer and write at the frame SYN instead
            // of flushing the motion early.
            // RAW-FIX: same 1:1 forwarding guarantee as the REL branch — buttons
            // must never wait for the frame SYN while earlier raw REL events are
            // already written (a press that lags its motion can mis-slot into
            // libinput's frame classification on some compositors).
            if (dev.settings.prof.raw_passthrough) {
                if (!uinput_write_retry_ev(uidev, ev.type, ev.code, ev.value))
                    { dev.disconnected = true; return; }
                wrote_unsynced_event = true;
            } else if (queued_count < queued_events.size()) {
                queued_events[queued_count++] = ev;
            } else {
                if (!flush_queued()) return;
                if (!out.flush(uidev)) { dev.disconnected = true; return; }
                if (!uinput_write_retry_ev(uidev, ev.type, ev.code, ev.value))
                    { dev.disconnected = true; return; }
                wrote_unsynced_event = true;
            }
        }
        } // for (read_batch events)
    } // while

    // End of batch.  LOW-1 / BUG-CRIT-1: the kernel coalesced a second frame
    // ([REL_X:+5, SYN, REL_X:+3]) and read() hit EAGAIN before that frame's
    // own SYN_REPORT was queued.  The old code flushed the +3 and closed it
    // with a SYNTHETIC SYN_REPORT — a frame boundary the kernel never
    // reported, which now (SM-1) would mangle the interval and produce a
    // double-SYN step.  Instead the unterminated frame (motion + any queued
    // non-motion) is DEFERRED to the device and merged into the next
    // process_device()'s frame, flushed at that REAL SYN.  The move is purely
    // internal; the deferred +3 is delayed by one poll frame at most.
    if (has_motion || queued_count > 0) {
        dev.pending_dx   += dx;
        dev.pending_dy   += dy;
        dev.has_pending_motion = true;
        dev.pending_ev_count = 0;
        for (size_t i = 0; i < queued_count && dev.pending_ev_count < dev.pending_events.size(); ++i)
            dev.pending_events[dev.pending_ev_count++] = queued_events[i];
    }
    // Close any frame that accumulated WRITTEN events (raw passthrough REL or
    // forwarded SYN subtypes) but never saw a SYN.  This is the only remaining
    // place a synthetic SYN_REPORT is emitted — for non-motion data only, where
    // no interval semantics are affected.  R7-RAWSYN: in raw passthrough the
    // written events ARE motion; the real source SYN_REPORT arrives in the next
    // batch and closes the frame there.  Emitting a synthetic SYN here would
    // split one kernel frame into [REL][SYN_synth][SYN_real] — a double-SYN /
    // extra empty frame that violates the T-B1 byte-identical 1:1 contract,
    // exactly the LOW-1 asymmetry the accel path was rebuilt to avoid.
    if (wrote_unsynced_event && !dev.settings.prof.raw_passthrough) {
        if (!uinput_write_retry_ev(uidev, EV_SYN, SYN_REPORT, 0))
            { dev.disconnected = true; return; }
        wrote_unsynced_event = false;
    }
}

// ── Latency statistics dump ───────────────────────────────────────────────────

void AccelDaemon::dump_latency_stats() {
    // Called on SIGUSR1 from the main thread — prints to stdout (journald captures it).
    // Stats cover flush_motion processing time (modifier math + uinput write),
    // NOT the full kernel→userspace round-trip latency.
    // TH-3: snapshot all device data under the lock, then release it before
    // doing any stdout I/O.  This prevents the loop thread from being starved
    // by a slow journald pipe (or a blocking stdout) while the lock is held.
    struct DevLatSnap {
        std::string name;
        bool raw_passthrough;
        lat_stats::snapshot snap;
        bool has_data;
    };
    std::vector<DevLatSnap> snaps;
    {
        std::lock_guard<std::mutex> lk(devices_mutex_);
        snaps.reserve(devices_.size());
        for (auto& dev : devices_) {
            DevLatSnap s;
            s.name = dev.name;
            s.raw_passthrough = dev.settings.prof.raw_passthrough;
            // BUG-21 (GPT-10): in raw_passthrough mode REL_X/REL_Y events bypass
            // flush_motion() entirely, so dev.lat is never updated.
            if (s.raw_passthrough) {
                s.has_data = false;
            } else {
                s.snap = dev.lat.snapshot_and_reset();
                s.has_data = (s.snap.count > 0);
            }
            snaps.push_back(std::move(s));
        }
    }
    // R1-04 RACE: the stdout report must be serialised against log() — the
    // loop/hidpp/ipc threads can be writing log lines (also std::cout) while
    // this SIGUSR1/IPC dump prints, interleaving and racing iostream's shared
    // buffers.  log_mu_ is taken ONLY AFTER devices_mutex_ was released so the
    // lock order matches the rest of the codebase (devices_mutex_ → log_mu_,
    // as in apply_new_config which logs under devices_mutex_) — never both at
    // once in the other order.
    const std::lock_guard<std::mutex> lk(log_mu_);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "=== RawAccel Processing Latency ===\n";
    if (snaps.empty()) {
        std::cout << "  No devices currently grabbed.\n";
        std::cout << "===================================\n";
        std::cout.flush();
        return;
    }
    for (const auto& s : snaps) {
        std::cout << "  Device: " << s.name;
        if (s.raw_passthrough) {
            std::cout << "  [raw passthrough — no per-event measurement]\n";
            std::cout << "    (events forwarded 1:1 to uinput; the clock_gettime\n"
                         "     pair would add ~50 ns per event vs. 0 in raw mode.)\n";
            continue;
        }
        std::cout << "\n";
        if (!s.has_data) {
            std::cout << "    No motion events recorded yet.\n";
        } else {
            std::cout << std::fixed << std::setprecision(2)
                      << "    Samples  : " << s.snap.count              << "\n"
                      << "    Min      : " << s.snap.min_us             << " µs\n"
                      << "    Avg      : " << s.snap.avg_us()           << " µs\n"
                      << "    p50      : " << s.snap.percentile(50)     << " µs\n"
                      << "    p95      : " << s.snap.percentile(95)     << " µs\n"
                      << "    p99      : " << s.snap.percentile(99)     << " µs\n"
                      << "    Max      : " << s.snap.max_us             << " µs\n";
            if (s.snap.over > 0)
                std::cout << "    Overflow : " << s.snap.over
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
        int dpi, poll_rate, detected_dpi, detected_polling_rate, real_polling_rate, detected_battery;
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
            s.real_polling_rate = dev.telemetry->real_polling_rate.load(std::memory_order_relaxed);
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
            // then increments to an even value.  8 spins was too few under a
            // very fast writer (~1 kHz+, BUG D-5) and silently dropped the
            // sample; 64 bounded spins keep the read window cheap without ever
            // blocking the writer.
            for (int attempts = 0; attempts < 64; attempts++) {
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
        o += ",\"real_polling_rate\":";
        o += std::to_string(s.real_polling_rate);
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
    // D26-N1: ipc_sock_path_ is claimed only on SUCCESS (right before the
    //        worker thread starts), NOT here.  If it were set up-front and
    //        any probe/bind/listen step failed, stop_ipc_server() on the
    //        way out would unconditional-unlink a pathname that may belong
    //        to the PREVIOUS* live daemon — killing this instance's IPC.

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

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
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
        // D-7: between the stale-probe unlink() and this bind() a second
        // daemon may have grabbed the socket.  Say so instead of a bare,
        // confusing "bind() failed".
        if (errno == EADDRINUSE)
            log("IPC: socket path was taken concurrently — another daemon "
                "started before us: " + sock_path);
        else
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
    // Claim the socket path for stop_ipc_server() only now that the socket
    // is bound, listening and being served (D26-N1).
    {
        std::lock_guard<std::mutex> lk(ipc_path_mu_);
        ipc_sock_path_ = sock_path;
    }
    ipc_sock_fd_.store(fd);

    ipc_running_.store(true);
    // R1-01: escape-proof worker — never let an exception hit terminate().
    ipc_thread_ = std::thread([this] {
        try {
            ipc_serve_loop();
        } catch (const std::exception& e) {
            log(std::string("ipc thread aborted: ") + e.what());
            stop_ipc_server(); // degrade: close the socket, stop serving
        } catch (...) {
            log("ipc thread aborted: unknown exception");
            stop_ipc_server();
        }
    });
    log("IPC socket: " + sock_path, true);
    return true;
}

void AccelDaemon::stop_ipc_server() {
    ipc_running_.store(false);
    int fd = ipc_sock_fd_.exchange(-1);
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
    }
    // ERR-1: never join the IPC thread from itself.  The thread-body catch
    // handler calls stop_ipc_server() on an abnormal exit; joining the current
    // thread throws std::system_error and the escaping exception would hit
    // std::terminate(), killing the daemon.  When called from the IPC thread
    // itself, skip the join (the thread unwinds right after this call) and let
    // the caller / normal stop() join it.
    if (ipc_thread_.joinable() && ipc_thread_.get_id() != std::this_thread::get_id())
        ipc_thread_.join();
    // The accept loop may still be returning from poll() when shutdown()
    // wakes it.  Do not close the descriptor until that thread has stopped:
    // closing it concurrently with poll/accept permits descriptor reuse and
    // can make the worker operate on an unrelated descriptor.
    if (fd >= 0) close(fd);
    // TH-1: ipc_sock_path_ can be cleared concurrently (the IPC thread's catch
    // handler and the main thread's normal shutdown may both reach here).  Copy
    // the path under the mutex, then unlink outside it.
    std::string path_to_unlink;
    {
        std::lock_guard<std::mutex> lk(ipc_path_mu_);
        path_to_unlink = ipc_sock_path_;
        ipc_sock_path_.clear();
    }
    if (!path_to_unlink.empty())
        unlink(path_to_unlink.c_str());
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

        // L-BUG-7: poll() re-arms immediately when the listening descriptor
        // reports an error state (POLLNVAL after the fd is closed under us in
        // shutdown, POLLERR/POLLHUP if the socket becomes unusable), so
        // accept4() would fail and the loop would hot-spin until
        // ipc_running_ clears.  Abandon the descriptor instead.
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            break;

        // R6: use the local 'fd' copy instead of ipc_sock_fd_ to avoid TOCTOU race
        // with stop_ipc_server() which may close ipc_sock_fd_ between poll() and accept4().
        int client = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) continue;
        // SEC-1: verify the connecting process belongs to the 'input' group
        // (or is root) via SO_PEERCRED.  The socket file permissions (0660
        // root:input) provide DAC-level gating, but a leaked FD or a race
        // between stale-probe and bind could let an unprivileged process slip
        // through — the kernel credential check closes that window.
        {
            struct ucred cred{};
            socklen_t clen = sizeof(cred);
            if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &clen) == 0) {
                bool allowed = (cred.uid == 0); // root always allowed
                if (!allowed) {
                    struct group* grp = getgrnam("input");
                    if (grp) {
                        // Check primary group
                        if (cred.gid == grp->gr_gid) allowed = true;
                        // Check supplementary groups via the peer's UID
                        if (!allowed) {
                            struct passwd* pw = getpwuid(cred.uid);
                            if (pw) {
                                int ngroups = 0;
                                getgrouplist(pw->pw_name, grp->gr_gid,
                                             nullptr, &ngroups);
                                if (ngroups > 0) {
                                    std::vector<gid_t> groups(ngroups);
                                    if (getgrouplist(pw->pw_name, grp->gr_gid,
                                                     groups.data(), &ngroups) == 0) {
                                        for (int i = 0; i < ngroups; i++) {
                                            if (groups[i] == grp->gr_gid) {
                                                allowed = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (!allowed) {
                    log("IPC: rejecting connection from unprivileged process"
                        " (uid=" + std::to_string(cred.uid) +
                        " gid=" + std::to_string(cred.gid) + ")", true);
                    close(client);
                    continue;
                }
            }
            // If getsockopt fails (kernel too old?), fall through — the DAC
            // permissions on the socket file are the fallback gate.
        }
        // ERR-3: never let a per-client exception (e.g. std::bad_alloc on a
        // huge payload) leak the descriptor or kill the whole IPC server.
        // Log, close, and keep serving the next client.
        try {
            handle_ipc_client(client);
        } catch (const std::exception& e) {
            log(std::string("ipc client error: ") + e.what());
        } catch (...) {
            log("ipc client error: unknown exception");
        }
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

    // D-8: never drop a client without a reason — on command-line timeout
    // reply with a JSON error (best-effort) instead of silently returning so
    // the client does not hang waiting for a response that never comes.
    auto reply_timeout = [&]() {
        const char* resp = "{\"error\":\"request timeout\"}\n";
        (void)send(client_fd, resp, strlen(resp), MSG_NOSIGNAL);
    };

    // Read the command line (up to a newline / 256 bytes).  Byte-wise recv is
    // fine here — IPC traffic is one short line per client.
    std::string line;
    char ch;
    while (line.size() < 256) {
        if (now_ns() >= deadline_ns) { reply_timeout(); return; } // slow command line
        ssize_t r = recv(client_fd, &ch, 1, 0);
        if (r < 0 && errno == EINTR) continue; // D-9: retry on signal
        // D-8 + RCVFIX: SO_RCVTIMEO expiry also drops a still-alive peer — it
        // could be reading a huge status response while we wait for its next
        // line.  Reply "request timeout" (best-effort) just like the total-
        // deadline path instead of returning silently, so the client never
        // blocks forever on a response that will not come.
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            reply_timeout();
            return;
        }
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
    } else if (line.rfind("set_active_app ", 0) == 0) {
        // P-APP: GUI reports the focused application's WM_CLASS (lowercased).
        // Empty payload clears the report ("none" / unescaped "{}").  We stash
        // the value under active_app_mu_; the loop thread consumes it and
        // re-applies app-scoped profiles without dropping any grab.
        std::string app = line.substr(std::strlen("set_active_app "));
        if (app == "none") app.clear();
        if (app.size() > 128) app.resize(128);
        std::lock_guard<std::mutex> lk(active_app_mu_);
        pending_app_ = app;
        active_app_dirty_ = true;
        response = "{\"ok\":true,\"message\":\"active app updated\"}\n";
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
                if (r < 0 && errno == EINTR) continue; // D-9: retry on signal
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
        if (w < 0 && errno == EINTR) continue; // D-9: retry on signal
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
