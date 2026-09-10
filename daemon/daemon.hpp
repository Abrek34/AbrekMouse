#pragma once
#include "../include/rawaccel.hpp"
#include "../include/config.hpp"
#include "../include/logitech_hidpp.hpp"
#include "lat_stats.hpp"              // latency histogram — no libevdev dependency
#include <libevdev/libevdev-uinput.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <set>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace rawaccel {

/// Represents one grabbed input device + its virtual output device.
struct mouse_device {
    struct telemetry_state {
        std::atomic<uint64_t> samples { 0 };
        std::atomic<double> speed_ips { 0.0 };
        std::atomic<double> out_ips { 0.0 };
        std::atomic<double> gain { 0.0 };
        std::atomic<double> dx { 0.0 };
        std::atomic<double> dy { 0.0 };
        std::atomic<double> wall_ms { 0.0 };
    };
    std::string      name;
    std::string      path;           // e.g. /dev/input/event3
    std::string      device_id;
    int              fd_in  = -1;    // grabbed evdev fd
    libevdev_uinput* uidev  = nullptr; // uinput virtual device handle
    int              dpi    = 800;
    int              poll_rate = 1000;
    int              detected_dpi          = 0;  // sysfs-detected DPI (0 = unknown)
    int              detected_polling_rate = 0;  // sysfs-detected polling rate in Hz (0 = unknown)
    int              detected_battery      = -1; // sysfs-detected battery % (0-100, -1 = unknown)

    // Per-device state
    modifier_settings  settings;
    speed_processor    sp;
    modifier           mod;

    // Timing
    double last_time_ms = 0;
    // R13-perf: pre-computed NORMALIZED_DPI / dpi — updated only on profile change,
    // avoids a floating-point division on every mouse event.
    // O6: direction matches reference input_dpi_normalization_factor (was dpi/NORMALIZED_DPI).
    double dpi_factor   = NORMALIZED_DPI / 800.0;

    // Subpixel accumulation: carry fractional remainder between frames
    double remainder_x   = 0.0;
    double remainder_y   = 0.0;
    // Set to true when a fatal I/O error occurs; run_loop removes the device
    bool   disconnected  = false;

    // BUG-18: SYN_DROPPED state must persist across process_device() calls.
    // The Linux input protocol says all events between a SYN_DROPPED and the
    // next SYN_REPORT are unreliable.  A previous bug had this as a function-
    // local flag — if SYN_DROPPED fell at the end of one read batch and the
    // matching SYN_REPORT arrived in the next epoll cycle, the flag was reset
    // to false in between and the daemon would forward unreliable events.
    bool   syn_dropped   = false;

    // ── Live telemetry (T30): last-motion sample per device ────────────────
    // Written by flush_motion() (loop thread) and read by status_json() (IPC
    // thread).  Every field is atomic: the generation counter provides a
    // consistent snapshot while the atomics themselves avoid the C++ data
    // race/UB that a seqlock over plain doubles would have.  The state is
    // held via unique_ptr so mouse_device remains movable.
    std::unique_ptr<telemetry_state> telemetry =
        std::make_unique<telemetry_state>();
    // ── Latency statistics — see lat_stats.hpp for the full implementation ──
    // Thread safety: flush_motion() (loop thread) writes; dump_latency_stats()
    // (main thread, on SIGUSR1) reads and resets.  lat_stats::mtx serialises access.
    lat_stats lat;
};

class AccelDaemon {
public:
    AccelDaemon();
    ~AccelDaemon();

    /// Load config and start processing. Returns false on error.
    bool start(const std::string& config_path = "");

    /// Stop gracefully (joins thread — do NOT call from a signal handler).
    void stop();

    /// Signal-safe stop: sets running_ = false without joining.
    /// Call from signal handlers; the main loop will complete shutdown.
    void request_stop() { running_.store(false); }

    /// Reload config (hot reload via SIGHUP).
    bool reload();

    /// Apply a full config pushed over IPC (the "set_config" RPC used by the
    /// GUI/CLI).  Parses + sanitizes the JSON, atomically persists it to the
    /// daemon's own config path (so it survives a daemon restart — this is what
    /// lets a root systemd daemon pick up edits made in the user's GUI, whose
    /// ~/.config copy differs from /etc/rawaccel/settings.json), then stores it
    /// for the event loop to live-apply.  Returns false on parse/persist error
    /// (the previous config is kept).
    bool push_config(const std::string& json_str);

    bool is_running() const { return running_.load(); }

    /// Set callback for logging.
    void set_log_cb(std::function<void(const std::string&)> cb) { log_cb_ = cb; }

    /// Enable/disable verbose debug output.
    void set_verbose(bool v) { verbose_ = v; }

    /// Print per-device latency statistics to stdout and reset counters.
    /// Call periodically (e.g. on SIGUSR1) to observe real-time performance.
    void dump_latency_stats();

    /// Atomically read-and-clear the IPC latency-dump flag.
    /// The IPC server (input-group accessible) sets this flag when an
    /// unprivileged client requests a stats dump; the main thread polls it
    /// alongside the SIGUSR1 path so latency reporting works without
    /// kill() permission on the (root-owned) daemon process.
    bool consume_latency_dump_request() {
        return latency_dump_flag_.exchange(false);
    }

    /// Build a JSON status string describing the daemon's current state.
    /// Thread-safe: acquires per-device lat_mtx for snapshot reads.
    std::string status_json() const;

    // ── Unix socket IPC ───────────────────────────────────────────────────────
    // start_ipc_server() opens a Unix domain socket at the given path and spawns
    // ipc_thread_ to accept connections.  stop_ipc_server() closes the socket and
    // joins the thread.  The path is typically $XDG_RUNTIME_DIR/rawaccel.sock or
    // /run/rawaccel.sock.  The protocol is line-based:
    //   client → "status\n"  → server → <json>\n
    //   client → "ping\n"    → server → "pong\n"
    //   client → "reload\n"  → server → {"ok":true,...}\n  (schedules config reload)
    //   client → "latency\n" → server → {"ok":true,...}\n  (schedules latency dump)
    //   client → "set_config <n>\n" followed by exactly <n> bytes of config JSON
    //            → server → {"ok":true,"config":path}\n   (persists + live-applies)
    //   client → <other>\n   → server → {"error":"unknown command"}\n
    // The socket is chmod 0660 root:input, so only members of the input group can
    // issue these commands (the same trust boundary as grabbing /dev/input anyway).
    bool start_ipc_server(const std::string& sock_path);
    void stop_ipc_server();
    static std::string ipc_sock_path(); ///< Derive socket path from PID-file convention.

private:
    void run_loop();
    /// P171-BFIX: HID++ notification drain on its own background thread so
    /// the 1–2 s hidraw open/poll/read cycles can never delay the motion
    /// hot path (mouse stutter regression on Logitech Unifying receivers).
    void run_hidpp_worker();
    bool setup_devices();
    void teardown_devices();
    bool open_input_device(mouse_device& dev);
    bool create_virtual_device(mouse_device& dev);
    void process_device(mouse_device& dev);
    void apply_profile(mouse_device& dev, const device_profile& prof);
    /// Shared apply path for both the SIGHUP reload and the IPC config push.
    /// Runs on the loop thread; live-updates open devices without dropping the
    /// grab, and falls back to a full setup when no devices are open yet.
    void apply_new_config(const app_config& new_cfg);
    /// Searches by device_id match first, then active_profile, then the first profile.
    const device_profile* find_profile(const std::string& dev_id) const;
    void handle_hotplug();
    void do_hotplug_scan();
    /// P168: non-blocking HID++ notification drain hook.  Runs on the loop
    /// thread at a bounded cadence, re-scans Logitech hidraw nodes, and
    /// forwards classified battery/link events to log().  Never touches the
    /// evdev/uinput motion hot path.
    void poll_hidpp_notifications();
    void log(const std::string& msg, bool verbose_only = false);

    std::atomic<bool>   running_             { false };
    std::atomic<bool>   reload_flag_         { false };
    std::atomic<bool>   latency_dump_flag_   { false };
    std::thread         loop_thread_;
    // P171-BFIX: dedicated thread for HID++ notification draining.  Keeps the
    // hidraw open/poll/read cycles (1000–2000 ms cadence) fully off the loop
    // thread, which handles mouse motion events synchronously.
    std::thread         hidpp_thread_;
    app_config          config_;
    std::string         config_path_;
    std::vector<mouse_device> devices_;
    // Guards devices_ against concurrent access from ipc_thread_ (status_json)
    // and loop_thread_ (setup/hotplug/cleanup).  Always held briefly (< 1µs typical).
    mutable std::mutex        devices_mutex_;
    std::function<void(const std::string&)> log_cb_;
    bool                verbose_     = false;

    // epoll fd for event loop
    int epoll_fd_   = -1;
    // inotify fd + watch descriptor for /dev/input hot-plug
    int inotify_fd_ = -1;
    int inotify_wd_ = -1;
    // Track which paths are already opened (avoid re-grabbing on spurious events)
    std::set<std::string> opened_paths_;
    // P121/BUG-02: paths with failed I/O are denied re-open until this
    // (ms, CLOCK_MONOTONIC_RAW).  A broken-but-still-listed node would
    // otherwise churn grab/uinput-create/destroy every ~2 s forever.
    // Access only from the loop thread (setup + hotplug) — no sync needed.
    std::unordered_map<std::string, double> path_deny_until_ms_;
    // P131/BUG-02: same backoff keyed by the stable device_id, so a device
    // that errored under eventN is not re-grabbed instantly when the kernel
    // renumbers it to eventM.  Loop thread only.
    std::unordered_map<std::string, double> dev_deny_until_ms_;
    // O(1) fd -> device index lookup (avoids linear scan in hot path)
    std::unordered_map<int, size_t> fd_to_dev_;
    // Hot-plug retry counter — accessed only from run_loop() thread; no sync needed.
    // Using atomic here for explicitness and to silence potential sanitizer warnings.
    std::atomic<int> hotplug_retry_ { 0 };
    // Pending hot-plug flag: set by inotify, processed at safe point in loop
    std::atomic<bool> pending_hotplug_ { false };
    // Next time to run an empty-device re-scan (ms, CLOCK_MONOTONIC_RAW).
    // When devices_ is empty (no mice at boot, permission/conflict fixed later),
    // the loop re-scans every ~2 s instead of giving up — self-healing startup.
    double empty_rescan_ms_ = 0;

    // ── P168: HID++ notification drain state ────────────────────────────────
    // Only touched from the loop thread (no sync needed), same as
    // hotplug_retry_ / empty_rescan_ms_ above.
    // Paths of currently-identified Logitech hidraw devices with their
    // cached feature maps (post-replug cache is cleared + re-identified).
    std::vector<hidpp_device> hidpp_devs_;
    std::unordered_map<std::string, std::unique_ptr<HidppTransport>> hidpp_transports_;
    double hidpp_rescan_ms_ = 0;    // next hidraw re-scan time
    double hidpp_drain_ms_  = 0;    // next notification drain time

    // Config push slot: filled by the IPC thread (push_config), consumed by the
    // loop thread.  push_cfg_ is fully parsed+sanitized before being stored, so
    // the loop thread never throws on it.
    std::mutex   push_cfg_mu_;
    app_config   push_cfg_;
    bool         push_cfg_pending_ = false;

    // IPC server state
    std::thread         ipc_thread_;
    std::atomic<int>    ipc_sock_fd_  { -1 };  // listening socket (atomic: shared with ipc_thread_)
    std::string         ipc_sock_path_;
    std::atomic<bool>   ipc_running_  { false };

    void ipc_serve_loop(); // runs in ipc_thread_
    void handle_ipc_client(int client_fd);
};

} // namespace rawaccel
