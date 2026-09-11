#include "../include/config.hpp"
#include "../include/presets.hpp"
#include "../include/rawaccel.hpp"
#include "../include/logitech_receiver.hpp"
#include "../include/logitech_hidpp.hpp"
#include "../include/nlohmann/json.hpp"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <iomanip>
#include <cmath>
#include <cctype>
#include <csignal>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

// Version number comes from RAWACCEL_VERSION in rawaccel-base.hpp (single source of truth).
static constexpr const char* VERSION = rawaccel::RAWACCEL_VERSION;

using namespace rawaccel;

// ── Daemon communication ──────────────────────────────────────────────────────

/// True when the process at /proc/<pid> is genuinely the rawaccel daemon
/// (R5-S-1). /proc/<pid>/comm can be spoofed with prctl(PR_SET_NAME); the
/// kernel-maintained /proc/<pid>/exe link cannot.  The exe check is best-effort
/// (falls back to comm alone when /proc is restricted — EACCES against a root
/// daemon) so a permission restriction never breaks daemon detection.
static bool pid_is_rawaccel_daemon(pid_t pid) {
    if (pid <= 0) return false;
    char proc_root[64];
    std::snprintf(proc_root, sizeof(proc_root), "/proc/%d", (int)pid);

    // 1) Kernel-maintained binary link — strongest identity check.
    {
        char link[96];
        std::snprintf(link, sizeof(link), "%s/exe", proc_root);
        char exe[4096];
        ssize_t n = readlink(link, exe, sizeof(exe) - 1);
        if (n > 0) {
            exe[n] = '\0';
            const char* base = std::strrchr(exe, '/');
            base = base ? base + 1 : exe;
            if (std::strcmp(base, "rawaccel-daemon") != 0) return false;
        } else if (errno == ENOENT || errno == ESRCH) {
            return false; // process gone
        }
        // else EACCES/EPERM (restricted /proc): fall through to the comm check.
    }

    // 2) /proc/<pid>/comm must also name the daemon.
    char comm_path[96];
    std::snprintf(comm_path, sizeof(comm_path), "%s/comm", proc_root);
    FILE* cf = std::fopen(comm_path, "r");
    if (!cf) return false;
    char comm[64] = {};
    (void)!std::fgets(comm, sizeof(comm), cf);
    std::fclose(cf);
    size_t len = std::strlen(comm);
    if (len > 0 && comm[len-1] == '\n') comm[len-1] = '\0';
    return std::strcmp(comm, "rawaccel-daemon") == 0;
}

/// Result codes from a daemon-signal attempt — lets the caller distinguish
/// "daemon not running" from "running but I lack permission" (the daemon
/// runs as root via systemd, and `kill()` from an unprivileged user returns
/// EPERM).  This was previously squashed into a single bool, producing the
/// misleading "is it running?" message even when the daemon was up.
enum class signal_result { sent, not_running, permission_denied, other };

static signal_result send_signal_to_daemon(int sig) {
    // N6: daemon prefers $XDG_RUNTIME_DIR/rawaccel.pid — check it first.
    std::vector<std::string> paths;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0') paths.push_back(std::string(xdg) + "/rawaccel.pid");
    paths.push_back("/run/rawaccel.pid");
    paths.push_back("/tmp/rawaccel.pid");

    for (auto& path : paths) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        pid_t pid = 0;
        f >> pid;
        // R5-S-1: only signal a PID that is actually the rawaccel daemon —
        // prevents signalling a recycled / spoofed process.
        if (!pid_is_rawaccel_daemon(pid)) continue;
        if (kill(pid, sig) == 0) return signal_result::sent;
        if (errno == EPERM)      return signal_result::permission_denied;
        return signal_result::other;
    }
    return signal_result::not_running;
}

// ── Daemon IPC (Unix socket) ──────────────────────────────────────────────────

/// Candidate IPC socket paths, in the order the daemon tries them.
/// The daemon writes the *first* one it can bind: $XDG_RUNTIME_DIR (only set
/// for user services), then /run (system service — what systemd uses).
/// CLI is invoked from a user shell, where XDG_RUNTIME_DIR *is* set, so a
/// naive `daemon_sock_path()` would point at /run/user/1000/rawaccel.sock
/// while the system daemon listens on /run/rawaccel.sock.  Walk the whole
/// list so both deployments work.
static std::vector<std::string> daemon_sock_candidates() {
    std::vector<std::string> v;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0') v.push_back(std::string(xdg) + "/rawaccel.sock");
    v.push_back("/run/rawaccel.sock");
    return v;
}

/// Send a raw request over the daemon socket and return the response.
/// Empty string on failure.  Used so unprivileged users (in the input group)
/// can ask the root-owned daemon to reload without needing kill() permission.
static std::string daemon_ipc_send(const std::string& request) {
    for (const auto& sock : daemon_sock_candidates()) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) continue;

        struct timeval tv { .tv_sec = 2, .tv_usec = 0 }; // 2000 ms (matches daemon server timeout)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (sock.size() >= sizeof(addr.sun_path)) { close(fd); continue; }
        std::strncpy(addr.sun_path, sock.c_str(), sizeof(addr.sun_path) - 1);
        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close(fd); continue; // try the next candidate
        }

        size_t total_sent = 0;
        bool send_ok = true;
        while (total_sent < request.size()) {
            ssize_t n = send(fd, request.c_str() + total_sent, request.size() - total_sent, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) continue;
                send_ok = false;
                break;
            }
            if (n == 0) { send_ok = false; break; }
            total_sent += static_cast<size_t>(n);
        }
        if (!send_ok) {
            close(fd);
            continue; // try the next candidate
        }

        std::string resp;
        char buf[4096];
        while (true) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break; // EOF (daemon closed) or 2 s RCVTIMEO
            resp.append(buf, (size_t)n);
            // C-4: no silent 64 KB truncation — the daemon closes the socket
            // after the full response, so keep reading.  Runaway guard only
            // (a stuck/broken daemon should not balloon memory forever).
            if (resp.size() > 16 * 1024 * 1024) { resp.clear(); break; }
        }
        close(fd);
        return resp;
    }
    return {};
}

/// Send a one-line command and return the response (eventless variant).
static std::string daemon_ipc_query(const std::string& cmd) {
    return daemon_ipc_send(cmd + "\n");
}

/// Ask the daemon to reload its config.  Tries the IPC socket first (works
/// for any user in the input group regardless of who the daemon runs as),
/// then falls back to SIGHUP for older daemons that don't speak IPC.
/// P130-R49: now returns the full signal_result so callers know *why* the
/// reload failed (not_running vs permission_denied) instead of having to
/// re-issue the SIGHUP a second time just to find out.
static signal_result daemon_reload_via_any_path() {
    std::string resp = daemon_ipc_query("reload");
    if (resp.find("\"ok\":true") != std::string::npos) return signal_result::sent;
    return send_signal_to_daemon(SIGHUP);
}

/// Push the caller's full config to the running daemon (IPC "set_config" RPC).
/// The daemon persists it to its own config path (a root systemd daemon writes
/// /etc/rawaccel/settings.json even though this CLI's working copy lives in the
/// user's home) and live-applies it.  Falls back to a SIGHUP reload for daemons
/// that predate the RPC.
/// @return  true if the daemon acknowledged the pushed config OR (fallback) a
///          reload signal was accepted.
static bool daemon_apply_config(const app_config& cfg) {
    std::string json = app_config_to_json(cfg);
    std::string req = "set_config " + std::to_string(json.size()) + "\n" + json;
    std::string resp = daemon_ipc_send(req);
    // P156/HIGH: the daemon rejected the push (schema/validation failure or
    // persistence error) — it answered, understood our message, and said NO.
    // We must NOT fall through to a reload: reload re-reads the daemon's own
    // (unchanged) file, so the caller's config would silently never apply, yet
    // the fallback's reload would report "ok:true" and the CLI would print
    // "Daemon reloaded." with rc=0 — a lie.
    if (resp.find("\"ok\":false") != std::string::npos) return false;
    if (resp.find("\"ok\":true") != std::string::npos) return true;
    // Only daemons with no idea what set_config is (empty/error response) get
    // the legacy SIGHUP reload path.
    return daemon_reload_via_any_path() == signal_result::sent;
}

// ── Global CLI flags ──────────────────────────────────────────────────────────

/// When true, mutating commands save the config locally but do NOT push it to
/// the running daemon — the user can review/apply later with `rawaccel-cli
/// reload`.  P82-CRIT-1: opt-in guard so a one-shot `-c /tmp/t.json create x`
/// (whose working copy lives outside the daemon's own config path) does not
/// silently clobber the live daemon config unless the user explicitly wants it.
static bool g_no_daemon = false;

/// When true, `list` renders the config as JSON (machine-readable) instead of
/// the human-readable profile dump.  Parsed as a global option so it works in
/// either position: `rawaccel-cli list --json` or `rawaccel-cli --json list`.
static bool g_json = false;

/// Apply a config change to the running daemon, honoring the global --no-daemon
/// flag.  When the flag is set the change is written to the local config file
/// only; the user can later push it with `rawaccel-cli reload` (or rerun
/// without --no-daemon).  P82-CRIT-1.
/// @return  0 on success (config saved + daemon push ok or skipped via -n).
/// @return  1 if the daemon push failed — the config WAS saved locally, but the
///          running daemon may not have picked it up (P115-A5-05: this used to
///          be silent with rc=0).
static int daemon_apply_if_enabled(const app_config& cfg) {
    if (g_no_daemon) {
        std::cout << "Config updated locally (not applied to daemon — "
                     "use 'rawaccel-cli reload' or remove --no-daemon)\n";
        return 0;
    }
    if (daemon_apply_config(cfg)) {
        std::cout << "Daemon reloaded.\n";
        return 0;
    }
    std::cerr << "Warning: config saved locally but the daemon did not reload it.\n"
              << "  Re-run without the change, or apply with: rawaccel-cli reload\n";
    return 1;
}

/// Print a uniform "couldn't reach daemon" diagnostic.  Suggests the right
/// remediation based on whether kill() failed with EPERM (sudo / systemctl)
/// or because the PID file simply isn't there.
static void print_signal_failure(signal_result r, const char* action, const char* kill_signal) {
    switch (r) {
    case signal_result::permission_denied:
        std::cerr << "Permission denied while trying to " << action << " the daemon.\n"
                  << "The daemon is running as root; signal it with:\n";
        if (std::strcmp(action, "reload") == 0 || std::strcmp(action, "stop") == 0)
            std::cerr << "  sudo systemctl " << action << " rawaccel    (preferred)\n";
        std::cerr << "  sudo kill -" << kill_signal << " $(cat /run/rawaccel.pid)\n";
        break;
    case signal_result::not_running:
        std::cerr << "Daemon is not running.  Start it with: sudo systemctl start rawaccel\n";
        break;
    default:
        std::cerr << "Could not " << action << " the daemon (errno=" << errno << ").\n";
        break;
    }
}

static int finite_double_to_int(double v) {
    if (v < static_cast<double>(INT_MIN)) return INT_MIN;
    if (v > static_cast<double>(INT_MAX)) return INT_MAX;
    return static_cast<int>(v);
}

/// save_config() throws std::runtime_error on any I/O failure (temp write,
/// fsync, rename, fs::create_directories).  An uncaught exception reaches
/// main() and hits std::terminate() — the CLI dies with SIGABRT (exit 134,
/// core dump) instead of a clean diagnostic.  Wrap every mutation call site.
/// @return  false if the save failed (message already printed to stderr).
static bool safe_save(const app_config& cfg, const std::string& path) {
    try {
        save_config(cfg, path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save config: " << e.what() << "\n";
        return false;
    }
}

// ── Profile display ────────────────────────────────────────────────────────────

/// Space-joined key list for error messages (P107).
static std::string join_keys(const std::vector<std::string>& keys) {
    std::string s;
    for (size_t i = 0; i < keys.size(); i++)
        s += (i ? " " : "") + keys[i];
    return s;
}

static void print_accel_args(const accel_args& a, const std::string& prefix = "  ") {
    auto mode_str = [](accel_mode m) -> std::string {
        switch (m) {
        case accel_mode::classic:     return "classic";
        case accel_mode::power:       return "power";
        case accel_mode::natural:     return "natural";
        case accel_mode::jump:        return "jump";
        case accel_mode::synchronous: return "synchronous";
        case accel_mode::lookup:      return "lookup";
        default:                      return "noaccel";
        }
    };

    std::cout << prefix << "mode:             " << mode_str(a.mode) << "\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << prefix << "gain:             " << (a.gain ? "true" : "false") << "\n";
    std::cout << prefix << "acceleration:     " << a.acceleration     << "\n";
    std::cout << prefix << "input_offset:     " << a.input_offset     << "\n";
    std::cout << prefix << "output_offset:    " << a.output_offset    << "\n";
    std::cout << prefix << "scale:            " << a.scale            << "\n";
    std::cout << prefix << "exponent_classic: " << a.exponent_classic << "\n";
    std::cout << prefix << "exponent_power:   " << a.exponent_power   << "\n";
    std::cout << prefix << "limit:            " << a.limit            << "\n";
    std::cout << prefix << "motivity:         " << a.motivity         << "\n";
    std::cout << prefix << "gamma:            " << a.gamma            << "\n";
    std::cout << prefix << "decay_rate:       " << a.decay_rate       << "\n";
    std::cout << prefix << "sync_speed:       " << a.sync_speed       << "\n";
    std::cout << prefix << "smooth:           " << a.smooth           << "\n";
    std::cout << prefix << "cap:              [" << a.cap.x << ", " << a.cap.y << "]\n";
    auto cap_mode_str = [](cap_mode c) -> std::string {
        switch (c) {
        case cap_mode::in:  return "in";
        case cap_mode::io:  return "io";
        default:            return "out";
        }
    };
    std::cout << prefix << "cap_mode:         " << cap_mode_str(a.cap_mode_val) << "\n";
}

static void print_profile(const device_profile& dp) {
    auto& p = dp.prof;
    std::cout << "Profile: " << dp.name << "\n";
    // P115-A5-09: a disabled profile (per-device disable flag) used to be
    // invisible in every listing — status claimed the profile was active while
    // its acceleration was actually bypassed.
    if (dp.dev_cfg.disable)
        std::cout << "  disabled:     true  (stored flag — dormant: daemon hot path ignores it; "
                     "real 1:1 bypass = set-param <profile> raw true)\n";
    std::cout << "  device_id:    " << (dp.device_id.empty() ? "(all)" : dp.device_id) << "\n";
    if (p.raw_passthrough) {
        std::cout << "  raw:          true  (all processing bypassed)\n";
        std::cout << "  dpi:          " << dp.dev_cfg.dpi         << "\n";
        std::cout << "  polling_rate: " << dp.dev_cfg.polling_rate << "\n";
        return;
    }
    std::cout << "  dpi:          " << dp.dev_cfg.dpi         << "\n";
    std::cout << "  polling_rate: " << dp.dev_cfg.polling_rate << "\n";
    std::cout << "  rotation:     " << p.degrees_rotation      << "°\n";
    std::cout << "  snap:         " << p.degrees_snap          << "°\n";
    // Use epsilon comparison: sub-ULP residue from JSON round-trip can leave
    // speed_min as e.g. 1e-17 even when the user typed "0", which would print
    // without the "(disabled)" hint and confuse status output.
    std::cout << "  speed_min:    " << p.speed_min << (std::fabs(p.speed_min) < 1e-9 ? "  (disabled)" : "") << "\n";
    std::cout << "  speed_max:    " << p.speed_max << (std::fabs(p.speed_max) < 1e-9 ? "  (disabled)" : "") << "\n";
    std::cout << "  output_dpi:   " << p.output_dpi << (std::fabs(p.output_dpi - NORMALIZED_DPI) < 1e-9 ? "  (default 1000)" : "") << "\n";
    std::cout << "  lr_ratio:     " << p.lr_output_dpi_ratio << (std::fabs(p.lr_output_dpi_ratio - 1.0) < 1e-9 ? "  (off)" : "") << "\n";
    std::cout << "  ud_ratio:     " << p.ud_output_dpi_ratio << (std::fabs(p.ud_output_dpi_ratio - 1.0) < 1e-9 ? "  (off)" : "") << "\n";
    std::cout << "  yx_ratio:     " << p.yx_output_dpi_ratio << (std::fabs(p.yx_output_dpi_ratio - 1.0) < 1e-9 ? "  (off)" : "") << "\n";
    {
        auto& sp = p.speed_processor_args;
        std::string dist = sp.whole ? (sp.lp_norm >= 16 || sp.lp_norm <= 0 ? "max" :
                           (std::fabs(sp.lp_norm - 2.0) > 1e-9 ? "lp" : "euclidean")) : "separate";
        std::cout << "  distance_mode:  " << dist << "\n";
        if (dist == "lp")
            std::cout << "  lp_norm:        " << sp.lp_norm << "\n";
        if (sp.input_speed_smooth_halflife > 0)
            std::cout << "  input_smooth_halflife:  " << sp.input_speed_smooth_halflife << "\n";
        if (sp.scale_smooth_halflife > 0)
            std::cout << "  scale_smooth_halflife:  " << sp.scale_smooth_halflife << "\n";
        if (sp.output_speed_smooth_halflife > 0)
            std::cout << "  output_smooth_halflife: " << sp.output_speed_smooth_halflife << "\n";
    }
    std::cout << "  Acceleration (X axis):\n";
    print_accel_args(p.accel_x, "    ");
    if (p.accel_x != p.accel_y) {
        std::cout << "  Acceleration (Y axis):\n";
        print_accel_args(p.accel_y, "    ");
    } else {
        std::cout << "  Acceleration (Y axis): same as X\n";
    }
}

// ── Commands ──────────────────────────────────────────────────────────────────

static int cmd_list(const app_config& cfg) {
    std::cout << "Active profile: " << cfg.active_profile << "\n\n";
    for (auto& dp : cfg.profiles) {
        print_profile(dp);
        std::cout << "\n";
    }
    return 0;
}

/// `rawaccel-cli list --json` — machine-readable rendering of the whole config.
/// Round-trips the exact field names/values the daemon and GUI agree on, so
/// scripts can cross-check preset loads against include/presets.hpp directly.
static int cmd_list_json(const app_config& cfg) {
    std::cout << app_config_to_json(cfg) << "\n";
    return 0;
}

static int cmd_show(const app_config& cfg, const std::string& name) {
    for (auto& dp : cfg.profiles) {
        if (dp.name == name) {
            print_profile(dp);
            return 0;
        }
    }
    std::cerr << "Profile not found: " << name << "\n";
    return 1;
}

static int cmd_set(app_config& cfg, const std::string& config_path, const std::string& name) {
    for (auto& dp : cfg.profiles) {
        if (dp.name == name) {
            cfg.active_profile = name;
            if (!safe_save(cfg, config_path)) return 1;
            std::cout << "Active profile set to: " << name << "\n";
            // Push the new config to the daemon (IPC set_config) so it takes
            // effect even when the daemon reads a different file (systemd /etc).
            return daemon_apply_if_enabled(cfg);
        }
    }
    std::cerr << "Profile not found: " << name << "\n";
    return 1;
}

static int cmd_create(app_config& cfg, const std::string& config_path, const std::string& name) {
    // BUG-12: empty profile name has no useful semantics — every later
    // command (`delete ""`, `show ""`, `set-param "" ...`) would target the
    // first nameless profile creating ambiguity.  Reject up front.
    if (name.empty()) {
        std::cerr << "Profile name must not be empty.\n";
        return 1;
    }
    // P82-MED-1: a >256-char name persists full now but the load-side cap
    // (MAX_DP_NAME=256 in config.cpp) silently truncates it to 256 on any
    // reload+resave.  Reject up front so the stored name always matches what
    // the user supplied.  256 chars is allowed (round-trips intact).
    if (name.size() > MAX_NAME_LEN) {
        std::cerr << "Profile name too long: " << name.size()
                  << " chars (max " << MAX_NAME_LEN << ").\n";
        return 1;
    }
    // Check duplicate
    for (auto& dp : cfg.profiles) {
        if (dp.name == name) {
            std::cerr << "Profile already exists: " << name << "\n";
            return 1;
        }
    }
    device_profile dp;
    dp.name = name;
    dp.dev_cfg.dpi = 800;
    dp.dev_cfg.polling_rate = 1000;
    dp.prof.accel_x.mode = accel_mode::noaccel;
    dp.prof.accel_y.mode = accel_mode::noaccel;
    cfg.profiles.push_back(dp);
    if (!safe_save(cfg, config_path)) return 1;
    std::cout << "Created profile: " << name << "\n";
    return daemon_apply_if_enabled(cfg);
}

static int cmd_delete(app_config& cfg, const std::string& config_path, const std::string& name) {
    auto it = std::remove_if(cfg.profiles.begin(), cfg.profiles.end(),
                             [&](const device_profile& dp) { return dp.name == name; });
    if (it == cfg.profiles.end()) {
        std::cerr << "Profile not found: " << name << "\n";
        return 1;
    }
    cfg.profiles.erase(it, cfg.profiles.end());
    // If we just deleted the active profile, fall back to the first remaining one.
    bool active_changed = false; // P156: only report a change when the active profile actually moved
    if (cfg.active_profile == name && !cfg.profiles.empty()) {
        cfg.active_profile = cfg.profiles[0].name;
        active_changed = true;
    }
    // P115-A5-08: deleting the LAST profile used to leave a dangling
    // active_profile persisted as {"active_profile":"p","profiles":[]}.
    // Clear it so the JSON never advertises a profile that no longer exists.
    if (cfg.profiles.empty()) {
        cfg.active_profile.clear();
        active_changed = true;
        // C-7: report the empty state clearly instead of "Active profile is
        // now: " (trailing blank) — the next run recreates the default profile.
        std::cout << "No profiles remain — a fresh 'default' profile is "
                     "recreated on the next run.\n";
    }
    if (!safe_save(cfg, config_path)) return 1;
    std::cout << "Deleted profile: " << name << "\n";
    if (active_changed && !cfg.active_profile.empty())
        std::cout << "Active profile is now: " << cfg.active_profile << "\n";
    return daemon_apply_if_enabled(cfg);
}

static int cmd_duplicate(app_config& cfg, const std::string& config_path,
                         const std::string& src_name, const std::string& dst_name) {
    // Reject empty new name
    if (dst_name.empty()) {
        std::cerr << "Profile name must not be empty.\n";
        return 1;
    }
    // P82-MED-1: symmetric cap with load-side (MAX_NAME_LEN).
    if (dst_name.size() > MAX_NAME_LEN) {
        std::cerr << "Profile name too long: " << dst_name.size()
                  << " chars (max " << MAX_NAME_LEN << ").\n";
        return 1;
    }
    // Reject if destination already exists
    for (auto& dp : cfg.profiles) {
        if (dp.name == dst_name) {
            std::cerr << "Profile already exists: " << dst_name << "\n";
            return 1;
        }
    }
    // Find source profile
    device_profile* src = nullptr;
    for (auto& dp : cfg.profiles) {
        if (dp.name == src_name) {
            src = &dp;
            break;
        }
    }
    if (!src) {
        std::cerr << "Source profile not found: " << src_name << "\n";
        return 1;
    }
    // Deep copy
    device_profile dst = *src;
    dst.name = dst_name;
    // Clear device_id on duplicate — user must explicitly assign the new profile
    // to a device if desired.  This avoids accidental device_id collision.
    dst.device_id.clear();
    cfg.profiles.push_back(std::move(dst));
    if (!safe_save(cfg, config_path)) return 1;
    std::cout << "Duplicated profile: '" << src_name << "' → '" << dst_name << "'\n";
    return daemon_apply_if_enabled(cfg);
}

static int cmd_create_preset(app_config& cfg, const std::string& config_path,
                             const std::string& preset_name, const std::string& profile_name) {
    if (profile_name.empty()) {
        std::cerr << "Profile name must not be empty.\n";
        return 1;
    }
    // P82-MED-1: symmetric cap with load-side (MAX_NAME_LEN).
    if (profile_name.size() > MAX_NAME_LEN) {
        std::cerr << "Profile name too long: " << profile_name.size()
                  << " chars (max " << MAX_NAME_LEN << ").\n";
        return 1;
    }
    // Check duplicate
    for (auto& dp : cfg.profiles) {
        if (dp.name == profile_name) {
            std::cerr << "Profile already exists: " << profile_name << "\n";
            return 1;
        }
    }
    device_profile dp = make_preset(preset_name, profile_name);
    if (dp.name.empty()) {
        std::cerr << "Unknown preset: '" << preset_name
                  << "'.  Available: gaming, office, precision, disable, cs2, valorant, apex, fps\n";
        return 1;
    }
    cfg.profiles.push_back(dp);
    if (!safe_save(cfg, config_path)) return 1;
    std::cout << "Created profile '" << profile_name << "' from preset '" << preset_name << "'\n";
    return daemon_apply_if_enabled(cfg);
}

/// Validate config file and report issues without modifying it.
static int cmd_validate(const std::string& config_path) {
    // P120-FAZ2 (A5-03): a missing config is an explicit error (rc=1) with a
    // clear message — never a silent false-PASS, and validation
    // must not CREATE the file (main() dispatches validate before any default
    // creation).  An existing-but-broken config reports errors to stderr and
    // the file is never written to.
    if (::access(config_path.c_str(), F_OK) != 0) {
        // M-BUG-16: the "config yok" token leaked Turkish into an otherwise
        // English CLI.  Validation is distributed to non-Turkish users too.
        std::cerr << "ERROR: config file not found: " << config_path << "\n";
        return 1;
    }
    std::cout << "Validating config: " << config_path << "\n";
    try {
        app_config cfg = load_config(config_path);
        bool has_errors = false;
        bool has_warnings = false;

        // Check 1: at least one profile
        if (cfg.profiles.empty()) {
            std::cerr << "ERROR: No profiles defined.\n";
            has_errors = true;
        }

        // Check 2: active profile exists
        bool active_found = false;
        for (auto& dp : cfg.profiles) {
            if (dp.name == cfg.active_profile) {
                active_found = true;
                break;
            }
        }
        if (!active_found && !cfg.profiles.empty()) {
            std::cerr << "ERROR: Active profile '" << cfg.active_profile
                      << "' not found in profiles list.\n";
            has_errors = true;
        } else if (!active_found && cfg.profiles.empty()) {
            // Already reported above
        }

        // Check 3: duplicate profile names
        std::vector<std::string> names;
        for (auto& dp : cfg.profiles) {
            if (std::find(names.begin(), names.end(), dp.name) != names.end()) {
                std::cerr << "ERROR: Duplicate profile name: '" << dp.name << "'\n";
                has_errors = true;
            }
            names.push_back(dp.name);
        }

        // Check 4: duplicate device_ids (non-empty)
        std::vector<std::string> device_ids;
        for (auto& dp : cfg.profiles) {
            if (!dp.device_id.empty()) {
                if (std::find(device_ids.begin(), device_ids.end(), dp.device_id) != device_ids.end()) {
                    std::cerr << "WARNING: Duplicate device_id: '" << dp.device_id
                              << "' (first match wins in daemon)\n";
                    has_warnings = true;
                }
                device_ids.push_back(dp.device_id);
            }
        }

        // Check 5: validate each profile
        for (auto& dp : cfg.profiles) {
            if (dp.name.empty()) {
                std::cerr << "ERROR: Profile with empty name found.\n";
                has_errors = true;
            }
            // C-5: mirror the load-side MAX_NAME_LEN cap so validate() reports
            // an over-long name as an error instead of silently accepting it
            // only to be truncated at the next load.
            if (dp.name.size() > MAX_NAME_LEN) {
                std::cerr << "ERROR: Profile name exceeds " << MAX_NAME_LEN
                          << " chars in profile '" << dp.name << "'\n";
                has_errors = true;
            }
            // Sanitize and check for clamping (would have happened on load)
            auto& p = dp.prof;
            if (p.speed_min > p.speed_max && p.speed_max > 0) {
                std::cerr << "WARNING: speed_min > speed_max in profile '" << dp.name << "'\n";
                has_warnings = true;
            }
            if (p.output_dpi < 1 || p.output_dpi > 32000) {
                std::cerr << "WARNING: output_dpi out of range [1, 32000] in profile '" << dp.name << "'\n";
                has_warnings = true;
            }
            if (dp.dev_cfg.dpi < 1 || dp.dev_cfg.dpi > 32000) {
                std::cerr << "WARNING: dpi out of range [1, 32000] in profile '" << dp.name << "'\n";
                has_warnings = true;
            }
            if (dp.dev_cfg.polling_rate < POLL_RATE_MIN || dp.dev_cfg.polling_rate > POLL_RATE_MAX) {
                std::cerr << "WARNING: polling_rate out of range ["
                          << POLL_RATE_MIN << ", " << POLL_RATE_MAX << "] in profile '" << dp.name << "'\n";
                has_warnings = true;
            }
            // Check accel_x/accel_y for LUT length consistency
            if (p.accel_x.mode == accel_mode::lookup && p.accel_x.length % 2 != 0) {
                std::cerr << "WARNING: LUT data length is odd in profile '" << dp.name
                          << "' (X axis) — should be even (speed, gain pairs)\n";
                has_warnings = true;
            }
            if (p.accel_y.mode == accel_mode::lookup && p.accel_y.length % 2 != 0) {
                std::cerr << "WARNING: LUT data length is odd in profile '" << dp.name
                          << "' (Y axis) — should be even (speed, gain pairs)\n";
                has_warnings = true;
            }
        }

        if (has_errors) {
            std::cout << "\nValidation FAILED.\n";
            return 1;
        } else if (has_warnings) {
            std::cout << "\nValidation passed with warnings.\n";
            return 0;
        } else {
            std::cout << "\nValidation passed. All checks OK.\n";
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to load/parse config: " << e.what() << "\n";
        return 1;
    }
}

/// Canonical string for a parameter key's post-sanitize stored value.
/// sanitize_device_profile() clamps DPI/polling/rotation/speed ordering etc.,
/// so reporting the raw entered string back can mislead (e.g. `dpi 999999`
/// would print 999999 even though 32000 was stored).  Read the field back
/// instead so the output always shows what actually landed in the config.
static std::string stored_value_str(const device_profile& dp, const std::string& key) {
    auto fmt = [](double x) {
        std::ostringstream o;
        o << std::fixed << std::setprecision(6) << x;
        return o.str();
    };
    auto mode_name = [](accel_mode m) -> const char* {
        switch (m) {
        case accel_mode::classic:     return "classic";
        case accel_mode::power:       return "power";
        case accel_mode::natural:     return "natural";
        case accel_mode::jump:        return "jump";
        case accel_mode::synchronous: return "synchronous";
        case accel_mode::lookup:      return "lookup";
        default:                      return "noaccel";
        }
    };
    const auto& a = dp.prof.accel_x;
    const auto& sp = dp.prof.speed_processor_args;

    if (key == "mode")            return mode_name(a.mode);
    if (key == "device_id")       return dp.device_id.empty() ? "(all)" : dp.device_id;
    if (key == "gain")            return a.gain ? "true" : "false";
    if (key == "raw")             return dp.prof.raw_passthrough ? "true" : "false";
    if (key == "dpi")             return std::to_string(dp.dev_cfg.dpi);
    if (key == "polling_rate")    return std::to_string(dp.dev_cfg.polling_rate);
    if (key == "cap_mode") {
        switch (a.cap_mode_val) {
        case cap_mode::in: return "in";
        case cap_mode::io: return "io";
        default:           return "out";
        }
    }
    if (key == "distance_mode") {
        // C-3: match print_profile() — lp_norm<=0 (hand-edited JSON) and
        // lp_norm>=16 both behave as "max", so report the same string.
        if (!sp.whole)                   return "separate";
        if (sp.lp_norm >= 16 || sp.lp_norm <= 0) return "max";
        if (std::fabs(sp.lp_norm - 2.0) < 1e-9) return "euclidean";
        return "lp";
    }
    if (key == "acceleration")        return fmt(a.acceleration);
    if (key == "exponent_classic")    return fmt(a.exponent_classic);
    if (key == "exponent_power")      return fmt(a.exponent_power);
    if (key == "limit")               return fmt(a.limit);
    if (key == "decay_rate")          return fmt(a.decay_rate);
    if (key == "input_offset")        return fmt(a.input_offset);
    if (key == "output_offset")       return fmt(a.output_offset);
    if (key == "scale")               return fmt(a.scale);
    if (key == "sync_speed")          return fmt(a.sync_speed);
    if (key == "smooth")              return fmt(a.smooth);
    if (key == "motivity")            return fmt(a.motivity);
    if (key == "gamma")               return fmt(a.gamma);
    if (key == "cap_x")               return fmt(a.cap.x);
    if (key == "cap_y")               return fmt(a.cap.y);
    if (key == "rotation")            return fmt(dp.prof.degrees_rotation);
    if (key == "snap")                return fmt(dp.prof.degrees_snap);
    if (key == "speed_min")           return fmt(dp.prof.speed_min);
    if (key == "speed_max")           return fmt(dp.prof.speed_max);
    if (key == "output_dpi")          return fmt(dp.prof.output_dpi);
    if (key == "lr_ratio")            return fmt(dp.prof.lr_output_dpi_ratio);
    if (key == "ud_ratio")            return fmt(dp.prof.ud_output_dpi_ratio);
    if (key == "yx_ratio")            return fmt(dp.prof.yx_output_dpi_ratio);
    if (key == "lp_norm")             return fmt(sp.lp_norm);
    if (key == "input_smooth_halflife")  return fmt(sp.input_speed_smooth_halflife);
    if (key == "scale_smooth_halflife")  return fmt(sp.scale_smooth_halflife);
    if (key == "output_smooth_halflife") return fmt(sp.output_speed_smooth_halflife);
    if (key == "domain_weights")  return fmt(dp.prof.domain_weights.x);
    if (key == "domain_weight_x") return fmt(dp.prof.domain_weights.x);
    if (key == "domain_weight_y") return fmt(dp.prof.domain_weights.y);
    if (key == "range_weights")   return fmt(dp.prof.range_weights.x);
    if (key == "range_weight_x")  return fmt(dp.prof.range_weights.x);
    if (key == "range_weight_y")  return fmt(dp.prof.range_weights.y);
    return key; // fallback — unknown/string keys shouldn't reach here
}

/// Quick parameter setter: rawaccel set-param <profile> <key> <value>
static int cmd_set_param(app_config& cfg, const std::string& config_path,
                         const std::string& profile_name,
                         const std::string& key, const std::string& val)
{
    device_profile* dp = nullptr;
    for (auto& p : cfg.profiles) {
        if (p.name == profile_name) { dp = &p; break; }
    }
    if (!dp) { std::cerr << "Profile not found: " << profile_name << "\n"; return 1; }

    auto& a = dp->prof.accel_x;
    auto& ay = dp->prof.accel_y;
    // BUG-19: previously an unknown mode silently fell through to noaccel,
    // so a typo like "classicc" would silently disable accel without
    // feedback.  Returns false for unknown input so the caller can error.
    auto set_mode = [](accel_args& a, const std::string& m) -> bool {
        if      (m == "classic")     a.mode = accel_mode::classic;
        else if (m == "power")       a.mode = accel_mode::power;
        else if (m == "natural")     a.mode = accel_mode::natural;
        else if (m == "jump")        a.mode = accel_mode::jump;
        else if (m == "synchronous") a.mode = accel_mode::synchronous;
        else if (m == "lookup")      a.mode = accel_mode::lookup;
        else if (m == "noaccel" ||
                 m == "off" ||
                 m == "none")        a.mode = accel_mode::noaccel;
        else return false;
        return true;
    };
    // BUG-19: strict bool — only accept canonical truthy/falsey strings.
    auto parse_strict_bool = [](const std::string& s, bool& out) -> bool {
        if (s == "1" || s == "true"  || s == "yes" || s == "on")  { out = true;  return true; }
        if (s == "0" || s == "false" || s == "no"  || s == "off") { out = false; return true; }
        return false;
    };

    // P107: validate the key against the FULL set up front so an unknown key
    // reports "Unknown key" instead of a misleading numerical parse failure
    // ("Invalid numeric value: true" for the nonexistent 'disable' key, etc.).
    static const std::vector<std::string> all_keys = {
        "mode", "gain", "cap_mode", "cap_x", "cap_y", "acceleration",
        "exponent_classic", "exponent_power", "limit", "decay_rate",
        "input_offset", "output_offset", "scale", "sync_speed", "smooth",
        "motivity", "gamma", "raw", "device_id", "rotation", "snap", "dpi",
        "polling_rate", "speed_min", "speed_max", "output_dpi", "lr_ratio",
        "ud_ratio", "yx_ratio", "distance_mode", "lp_norm",
        "input_smooth_halflife", "scale_smooth_halflife",
        "output_smooth_halflife", "domain_weights", "domain_weight_x",
        "domain_weight_y", "range_weights", "range_weight_x", "range_weight_y"
    };
    if (std::find(all_keys.begin(), all_keys.end(), key) == all_keys.end()) {
        std::cerr << "Unknown key: " << key << "\n"
                  << "Valid keys: " << join_keys(all_keys) << "\n";
        return 1;
    }
    // Parse numeric value only for numeric params (not for string/bool keys)
    static const std::vector<std::string> non_numeric_keys = {
        "mode", "gain", "cap_mode", "distance_mode", "raw", "device_id"
    };
    double v = 0;
    bool need_numeric = true;
    for (auto& nk : non_numeric_keys) if (nk == key) { need_numeric = false; break; }
    if (need_numeric) {
        // BUG-11: std::stod("1.5junk") happily returns 1.5 and silently drops
        // the trailing garbage — a typo would slip through unnoticed.  Use the
        // optional `pos` out-param and require it to consume the entire string
        // (allowing only trailing whitespace).
        try {
            size_t pos = 0;
            v = std::stod(val, &pos);
            // Skip trailing whitespace
            while (pos < val.size() &&
                   std::isspace(static_cast<unsigned char>(val[pos]))) ++pos;
            if (pos != val.size()) {
                std::cerr << "Invalid numeric value: " << val
                          << " (trailing garbage)\n";
                return 1;
            }
        } catch (...) { std::cerr << "Invalid numeric value: " << val << "\n"; return 1; }
        // BUG-10: NaN/Inf passed straight through to JSON storage as "NaN"/
        // "Infinity" — non-portable per the JSON spec.  Daemon would sanitize
        // on load but the file itself is malformed.  Reject before persisting.
        if (!std::isfinite(v)) {
            std::cerr << "Invalid numeric value: " << val
                      << " (NaN/Inf not allowed)\n";
            return 1;
        }
    }

    // P107: reject out-of-domain values instead of silently clamping them.
    // sanitize_device_profile() still clamps hand-edited JSON at load time
    // (a documented degrade path), but the CLI must not persist a different
    // value than the user asked for while exiting 0 — a script checking $?
    // would see "success" for e.g. `snap 90` that actually stored 45.
    // Domains mirror the sanitize ranges in src/config.cpp exactly.
    // Intentionally unconstrained (accept any finite value): acceleration
    // (negative is a legit classic-decel feature) and rotation (any angle is
    // normalized into [0,360) — documented aliasing).  Non-numeric keys
    // (mode/gain/cap_mode/raw/distance_mode/device_id) never reach here.
    auto range_ok = [&](const char* k, double lo, double hi) -> bool {
        if (v >= lo && v <= hi) return true;
        std::cerr << "Invalid value for '" << k << "': " << val
                  << "  (valid range: " << lo << ".." << hi << ")\n";
        return false;
    };
    auto min_ok = [&](const char* k, double lo) -> bool {
        if (v >= lo) return true;
        std::cerr << "Invalid value for '" << k << "': " << val
                  << "  (must be >= " << lo << ")\n";
        return false;
    };
    auto int_ok = [&](const char* k, double lo, double hi) -> bool {
        if (v != std::floor(v)) {
            std::cerr << "Invalid value for '" << k << "': " << val
                      << "  (must be an integer)\n";
            return false;
        }
        return range_ok(k, lo, hi);
    };

    if (key == "dpi") {
        if (!int_ok("dpi", 1, 32000)) return 1;
    } else if (key == "output_dpi") {
        // P156: output_dpi is a double field in the config schema — an integer
        // coercion rejected legitimate fractional values (e.g. DPI != 1).  The
        // sanitizer clamps the range anyway; only the range is meaningful here.
        if (!range_ok("output_dpi", 1, 32000)) return 1;
    } else if (key == "polling_rate") {
        if (!int_ok("polling_rate", POLL_RATE_MIN, POLL_RATE_MAX)) return 1;
    } else if (key == "snap") {
        if (!range_ok("snap", 0, 45)) return 1;
    } else if (key == "lr_ratio" || key == "ud_ratio" || key == "yx_ratio") {
        if (!range_ok(key.c_str(), 0.01, 100)) return 1;
    } else if (key == "exponent_classic") {
        if (!range_ok("exponent_classic", 1, 10)) return 1;
    } else if (key == "exponent_power") {
        // P120-FAZ2 (Aj8 BUG-3): clipped at the GUI gauge max (EXP_POWER_MAX).
        if (!range_ok(key.c_str(), 1e-4, EXP_POWER_MAX)) return 1;
    } else if (key == "sync_speed") {
        if (!min_ok(key.c_str(), 1e-4)) return 1;
    } else if (key == "lp_norm") {
        if (v <= 0) {
            std::cerr << "Invalid value for 'lp_norm': " << val
                      << "  (must be > 0)\n";
            return 1;
        }
    } else if (key == "cap_x") {
        // P120-FAZ2: GUI gauge max CAP_X_MAX.
        if (!range_ok(key.c_str(), 0, CAP_X_MAX)) return 1;
    } else if (key == "cap_y") {
        // P120-FAZ2: GUI gauge max CAP_Y_MAX.
        if (!range_ok(key.c_str(), 0, CAP_Y_MAX)) return 1;
    } else if (key == "output_offset") {
        // P120-FAZ2: GUI gauge max OUTPUT_OFFSET_MAX.
        if (!range_ok(key.c_str(), 0, OUTPUT_OFFSET_MAX)) return 1;
    } else if (key == "scale") {
        // P120-FAZ2: GUI gauge max SCALE_MAX.
        if (!range_ok(key.c_str(), 0, SCALE_MAX)) return 1;
    } else if (key == "limit" || key == "decay_rate" || key == "motivity" ||
               key == "gamma" || key == "input_offset" || key == "smooth" ||
               key == "speed_min" || key == "speed_max" ||
               key == "input_smooth_halflife" || key == "scale_smooth_halflife" ||
               key == "output_smooth_halflife") {
        if (!min_ok(key.c_str(), 0)) return 1;
    } else if (key == "domain_weights" || key == "domain_weight_x" ||
               key == "domain_weight_y" || key == "range_weights" ||
               key == "range_weight_x" || key == "range_weight_y") {
        if (!range_ok(key.c_str(), 0, 1e6)) return 1;
    }

    if      (key == "mode")             {
        if (!set_mode(a, val) || !set_mode(ay, val)) {
            std::cerr << "Invalid mode: '" << val << "'.  Valid: classic, power, "
                         "natural, jump, synchronous, lookup, noaccel\n";
            return 1;
        }
        // C-8: with mode=lookup but no LUT data the daemon gets an empty
        // curve; warn loudly instead of letting the user believe it took.
        if (val == "lookup" && a.length == 0) {
            std::cerr << "WARNING: mode=lookup has no LUT data yet — set it "
                         "with `rawaccel-cli set-param lut-data` before it "
                         "does anything useful.\n";
        }
    }
    else if (key == "gain")             {
        bool b;
        if (!parse_strict_bool(val, b)) {
            std::cerr << "Invalid bool for 'gain': '" << val
                      << "'.  Valid: true/false/1/0/yes/no/on/off\n";
            return 1;
        }
        a.gain = ay.gain = b;
    }
    else if (key == "acceleration")     { a.acceleration = ay.acceleration = v; }
    else if (key == "exponent_classic") { a.exponent_classic = ay.exponent_classic = v; }
    else if (key == "exponent_power")   { a.exponent_power = ay.exponent_power = v; }
    else if (key == "limit")            { a.limit = ay.limit = v; }
    else if (key == "decay_rate")       { a.decay_rate = ay.decay_rate = v; }
    else if (key == "input_offset")     { a.input_offset = ay.input_offset = v; }
    else if (key == "output_offset")    { a.output_offset = ay.output_offset = v; }
    else if (key == "scale")            { a.scale = ay.scale = v; }
    else if (key == "sync_speed")       { a.sync_speed = ay.sync_speed = v; }
    else if (key == "smooth")           { a.smooth = ay.smooth = v; }
    else if (key == "motivity")         { a.motivity = ay.motivity = v; }
    else if (key == "gamma")            { a.gamma = ay.gamma = v; }
    else if (key == "cap_y")            { a.cap.y = ay.cap.y = v; }
    else if (key == "cap_x")            { a.cap.x = ay.cap.x = v; }
    else if (key == "cap_mode")         {
        cap_mode m;
        if      (val == "in")  m = cap_mode::in;
        else if (val == "out") m = cap_mode::out;
        else if (val == "io" || val == "in_out" || val == "both") m = cap_mode::io;
        else {
            std::cerr << "Invalid cap_mode: '" << val
                      << "'.  Valid: in, out, io\n";
            return 1;
        }
        a.cap_mode_val = ay.cap_mode_val = m;
    }
    else if (key == "raw")              {
        bool b;
        if (!parse_strict_bool(val, b)) {
            std::cerr << "Invalid bool for 'raw': '" << val << "'\n";
            return 1;
        }
        dp->prof.raw_passthrough = b;
    }
    else if (key == "device_id")        {
        // Per-device profile assignment. Empty string clears the assignment
        // (profile then applies to all unmatched mice).  Non-empty values are
        // matched against the daemon's composite ID "usb:VVVV:PPPP:serial" or a
        // /dev/input/{by-id,eventN} node path — copy verbatim.
        // P115-A5-01: the load-side cap is MAX_DP_DEVICE_ID (256) in
        // src/config.cpp.  A longer value used to be accepted and then silently
        // truncated on the next reload+resave — same trap P82-MED-1 fixed for
        // names.  Reject up front so the stored ID always equals what the user
        // supplied.
        if (val.size() > 256) {
            std::cerr << "device_id too long: " << val.size()
                      << " chars (max 256).\n";
            return 1;
        }
        dp->device_id = val;
    }
    else if (key == "rotation")         { dp->prof.degrees_rotation = v; }
    else if (key == "snap")             { dp->prof.degrees_snap = v; }
    else if (key == "dpi")              { dp->dev_cfg.dpi = finite_double_to_int(v); }
    else if (key == "polling_rate")     { dp->dev_cfg.polling_rate = finite_double_to_int(v); }
    else if (key == "speed_min")        { dp->prof.speed_min = v; }
    else if (key == "speed_max")        { dp->prof.speed_max = v; }
    else if (key == "output_dpi")        { dp->prof.output_dpi = v; }
    else if (key == "lr_ratio")         { dp->prof.lr_output_dpi_ratio = v; }
    else if (key == "ud_ratio")         { dp->prof.ud_output_dpi_ratio = v; }
    else if (key == "yx_ratio")         { dp->prof.yx_output_dpi_ratio = v; }
    else if (key == "distance_mode")    {
        if      (val == "separate" || val == "manhattan") {
            dp->prof.speed_processor_args.whole = false;
        }
        else if (val == "max" || val == "chebyshev") {
            dp->prof.speed_processor_args.whole = true;
            dp->prof.speed_processor_args.lp_norm = 9999;
        }
        else if (val == "lp") {
            dp->prof.speed_processor_args.whole = true;
            // P115-A5-06: switching to lp-mode must not leave a stale max-mode
            // sentinel (lp_norm=9999) behind — that kept the LAST lp_norm value
            // and made stored_value_str() echo "max" after the user asked for
            // "lp".  An explicit lp_norm set BEFORE this call is preserved
            // (semantics: "lp, at my norm"); the sentinel is the only victim.
            if (dp->prof.speed_processor_args.lp_norm >= 16)
                dp->prof.speed_processor_args.lp_norm = 2.0;
        }
        else if (val == "euclidean") {
            dp->prof.speed_processor_args.whole = true;
            dp->prof.speed_processor_args.lp_norm = 2;
        }
        else {
            std::cerr << "Invalid distance_mode: '" << val
                      << "'.  Valid: euclidean, max, lp, separate\n";
            return 1;
        }
    }
    else if (key == "lp_norm")          { dp->prof.speed_processor_args.lp_norm = v; }
    else if (key == "input_smooth_halflife")  { dp->prof.speed_processor_args.input_speed_smooth_halflife = v; }
    else if (key == "scale_smooth_halflife")  { dp->prof.speed_processor_args.scale_smooth_halflife = v; }
    else if (key == "output_smooth_halflife") { dp->prof.speed_processor_args.output_speed_smooth_halflife = v; }
    else if (key == "domain_weights")  { dp->prof.domain_weights.x = dp->prof.domain_weights.y = v; }
    else if (key == "domain_weight_x") { dp->prof.domain_weights.x = v; }
    else if (key == "domain_weight_y") { dp->prof.domain_weights.y = v; }
    else if (key == "range_weights")   { dp->prof.range_weights.x = dp->prof.range_weights.y = v; }
    else if (key == "range_weight_x")  { dp->prof.range_weights.x = v; }
    else if (key == "range_weight_y")  { dp->prof.range_weights.y = v; }
    else                                {
        std::cerr << "Unknown key: " << key << "\n"
                  << "Valid keys: mode, gain, cap_mode, cap_x, cap_y, acceleration, "
                     "exponent_classic, exponent_power, limit, decay_rate, motivity, "
                     "gamma, input_offset, output_offset, scale, sync_speed, smooth, "
                     "raw, device_id, rotation, snap, dpi, polling_rate, speed_min, "
                     "speed_max, output_dpi, lr_ratio, ud_ratio, yx_ratio, "
                     "distance_mode, lp_norm, input_smooth_halflife, "
                     "scale_smooth_halflife, output_smooth_halflife, domain_weights, "
                     "domain_weight_x, domain_weight_y, range_weights, range_weight_x, "
                     "range_weight_y\n";
        return 1;
    }

    // Sanitize after setting — clamps DPI, polling rate, rotation, etc. to safe ranges
    sanitize_device_profile(*dp);
    if (!safe_save(cfg, config_path)) return 1;
    // Print the post-sanitize value actually stored (sanitize may have clamped
    // e.g. `dpi 999999` → 32000, or resolved speed_min/speed_max ordering).
    std::cout << "Set " << key << " = " << stored_value_str(*dp, key)
              << " in profile '" << profile_name << "'\n";
    return daemon_apply_if_enabled(cfg);
}

static int cmd_export(const app_config& cfg, const std::string& name) {
    if (name.empty()) {
        for (auto& dp : cfg.profiles)
            std::cout << profile_to_json(dp) << "\n";
        return 0;
    }
    for (auto& dp : cfg.profiles) {
        if (dp.name == name) {
            std::cout << profile_to_json(dp) << "\n";
            return 0;
        }
    }
    std::cerr << "Profile not found: " << name << "\n";
    return 1;
}

static int cmd_import(app_config& cfg, const std::string& config_path, const std::string& json_file) {
    std::ifstream f(json_file, std::ios::in | std::ios::binary);
    if (!f.is_open()) { std::cerr << "Cannot open: " << json_file << "\n"; return 1; }
    // L-BUG-8 + C-10: bound the file by reading chunked, not with stat() —
    // stat can fail (FUSE, pipes, exotic filesystems) and would silently
    // bypass the size limit, letting a malformed multi-GB file be slurped.
    std::string content;
    char chunk[1 << 16];
    constexpr size_t kImportMax = 1024 * 1024;
    while (f && content.size() <= kImportMax) {
        f.read(chunk, sizeof(chunk));
        content.append(chunk, (size_t)f.gcount());
    }
    if (content.size() > kImportMax) {
        std::cerr << "Refusing to import: " << json_file
                  << " is larger than 1MB\n";
        return 1;
    }

    // R3-NEW-1: export with an empty <name> prints one profile object per line,
    // so `export > all.json && import all.json` fed a multi-object stream to a
    // single-object parser and failed.  Import now accepts any of:
    //   1. a single profile object,
    //   2. a {"profiles":[...]} wrapper,
    //   3. a newline-delimited stream of profile objects (the export format),
    //   4. a bare JSON array of profile objects.
    std::vector<std::string> frags;
    try {
        auto all = nlohmann::json::parse(content);
        if (all.is_object() && all.contains("profiles") && all["profiles"].is_array()) {
            for (auto& jp : all["profiles"]) frags.push_back(jp.dump());
        } else if (all.is_object()) {
            frags.push_back(content); // single profile object
        } else if (all.is_array() && !all.empty() && all[0].is_object()) {
            for (auto& jp : all) frags.push_back(jp.dump());
        } else {
            std::cerr << "Invalid profile JSON: expected a profile object, a "
                         "{\"profiles\":[...]} array, or a line stream.\n";
            return 1;
        }
    } catch (const std::exception&) {
        // Not one JSON document → treat as the export line-stream format.
        std::istringstream is(content);
        std::string line;
        while (std::getline(is, line)) {
            size_t a = line.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) continue;
            size_t b = line.find_last_not_of(" \t\r\n");
            std::string t = line.substr(a, b - a + 1);
            if (t.empty()) continue;
            try {
                const auto jl = nlohmann::json::parse(t);
                if (!jl.is_object()) throw std::runtime_error("not a JSON object");
            }
            catch (...) {
                std::cerr << "Invalid profile JSON line in stream: " << t << "\n";
                return 1;
            }
            frags.push_back(t);
        }
    }
    if (frags.empty()) {
        std::cerr << "No profiles found in: " << json_file << "\n";
        return 1;
    }

    std::vector<device_profile> batch;
    for (auto& fr : frags) {
        device_profile dp;
        try {
            dp = profile_from_json(fr);
        } catch (std::exception& e) {
            std::cerr << "Invalid profile JSON: " << e.what() << "\n";
            return 1;
        }

        // BUG-15-fix-followup: the LUT truncate warning was previously placed
        // AFTER profile_from_json() which calls sanitize_profile() →
        // sort_lut_data() — by then a.length is already clamped to
        // LUT_POINTS_CAPACITY*2, so the warning was dead code.  Re-parse the raw
        // JSON to count the original lut_data array size and warn at import time.
        try {
            auto raw = nlohmann::json::parse(fr);
            // P120-FAZ2 (A5-04): an imported LUT larger than the engine capacity
            // (> 514 raw elements, i.e. > 257 speed/gain points) is REJECTED with
            // rc=1 + an explicit error.  Previously import warned and silently
            // truncated the curve — that could swap a legit 257-point table for an
            // unrelated prefix.  No silent truncation.
            auto check_lut_raw = [&](const char* axis_key, const char* axis) -> bool {
                // P115-A5-04: the raw LUT sits under "profile" (device_profile
                // JSON), not at the top level — the previous check looked at
                // raw["accel_x"] and never matched, silently dropping the
                // truncation warning.
                if (!raw.contains("profile")) return true;
                auto& ax = raw["profile"];
                if (!ax.contains(axis_key)) return true;
                auto& a = ax[axis_key];
                if (!a.contains("lut_data") || !a["lut_data"].is_array()) return true;
                size_t n = a["lut_data"].size();
                if (n / 2 > LUT_POINTS_CAPACITY) {
                    std::cerr << "ERROR: LUT (" << axis << " axis) in imported "
                              << "profile has " << (n/2) << " points; maximum is "
                              << LUT_POINTS_CAPACITY << " (" << LUT_RAW_DATA_CAPACITY
                              << " raw elements). Import rejected — fix the file.\n";
                    return false;
                }
                return true;
            };
            if (!check_lut_raw("accel_x", "X")) return 1;
            if (!check_lut_raw("accel_y", "Y")) return 1;
        } catch (const std::exception& e) {
            std::cerr << "Warning: could not inspect raw LUT size: " << e.what() << "\n";
        }

        // BUG-20: previously cmd_import did NOT validate the profile name.  An
        // empty name or a duplicate of an existing profile would be silently
        // appended, leaving the user with multiple ambiguous profiles that
        // commands like delete/show/set-param target by first match.
        if (dp.name.empty()) {
            std::cerr << "Imported profile has no 'name' — refusing to import "
                         "(would leave the config ambiguous).\n";
            return 1;
        }
        // C-2: enforce the same MAX_NAME_LEN cap that every other
        // profile-create/rename path enforces (P82-MED-1 symmetric cap).
        if (dp.name.size() > MAX_NAME_LEN) {
            std::cerr << "Imported profile name is " << dp.name.size()
                      << " chars (max " << MAX_NAME_LEN << ").\n";
            return 1;
        }
        for (auto& existing : cfg.profiles) {
            if (existing.name == dp.name) {
                std::cerr << "Profile '" << dp.name << "' already exists. "
                             "Delete it first or rename the JSON before importing.\n";
                return 1;
            }
        }
        // Duplicate within the import batch itself.
        for (auto& pd : batch)
            if (pd.name == dp.name) {
                std::cerr << "Duplicate profile '" << dp.name
                          << "' in import file — refusing to import.\n";
                return 1;
            }
        batch.push_back(dp);
    }

    for (auto& dp : batch) {
        cfg.profiles.push_back(dp);
        std::cout << "Imported profile: " << dp.name << "\n";
    }
    if (!safe_save(cfg, config_path)) return 1;
    return daemon_apply_if_enabled(cfg);
}

static int cmd_reload() {
    auto r = daemon_reload_via_any_path();
    if (r == signal_result::sent) {
        std::cout << "Daemon reloaded.\n";
        return 0;
    }
    print_signal_failure(r, "reload", "HUP");
    return 1;
}

static int cmd_rename(app_config& cfg, const std::string& config_path, const std::string& old_name, const std::string& new_name) {
    // Reject empty new name
    if (new_name.empty()) {
        std::cerr << "Profile name must not be empty.\n";
        return 1;
    }
    // P82-MED-1: symmetric cap with load-side (MAX_NAME_LEN).
    if (new_name.size() > MAX_NAME_LEN) {
        std::cerr << "Profile name too long: " << new_name.size()
                  << " chars (max " << MAX_NAME_LEN << ").\n";
        return 1;
    }
    // Reject if new name already exists
    for (auto& dp : cfg.profiles) {
        if (dp.name == new_name) {
            std::cerr << "Profile already exists: " << new_name << "\n";
            return 1;
        }
    }
    // Find and rename
    bool found = false;
    for (auto& dp : cfg.profiles) {
        if (dp.name == old_name) {
            dp.name = new_name;
            // P130-R49 (BUG): renaming the ACTIVE profile used to leave a
            // dangling active_profile → the saved config instantly failed
            // `validate` ("Active profile not found") even though rc was 0.
            // Keep the pointer in sync like cmd_delete does.
            if (cfg.active_profile == old_name)
                cfg.active_profile = new_name;
            found = true;
            break;
        }
    }
    if (!found) {
        std::cerr << "Profile not found: " << old_name << "\n";
        return 1;
    }
    if (!safe_save(cfg, config_path)) return 1;
    std::cout << "Renamed profile: '" << old_name << "' → '" << new_name << "'\n";
    return daemon_apply_if_enabled(cfg);
}

static int cmd_stop() {
    auto r = send_signal_to_daemon(SIGTERM);
    if (r == signal_result::sent) {
        std::cout << "Daemon stopped.\n";
        return 0;
    }
    print_signal_failure(r, "stop", "TERM");
    return 1;
}

static bool daemon_running() {
    std::vector<std::string> paths;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0') paths.push_back(std::string(xdg) + "/rawaccel.pid");
    paths.push_back("/run/rawaccel.pid");
    paths.push_back("/tmp/rawaccel.pid");
    for (auto& path : paths) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        pid_t pid = 0;
        f >> pid;
        // R5-S-1: a recycled/spoofed PID must not be reported as the daemon.
        if (pid_is_rawaccel_daemon(pid)) return true;
    }
    return false;
}

static int cmd_status_json(const std::string& config_path) {
    nlohmann::json out;
    bool running = daemon_running();
    out["daemon"] = running ? "running" : "stopped";
    out["config"] = config_path;
    out["profiles"] = nlohmann::json::array();
    bool config_ok = true;
    try {
        auto cfg = load_config(config_path);
        out["active_profile"] = cfg.active_profile;
        out["use_raw_input"] = cfg.use_raw_input;
        for (auto& p : cfg.profiles) {
            nlohmann::json po;
            po["name"]      = p.name;
            po["active"]    = (p.name == cfg.active_profile);
            po["device_id"] = p.device_id;
            po["dpi"]       = p.dev_cfg.dpi;
            po["polling_rate"] = p.dev_cfg.polling_rate;
            // P115-A5-09: expose the per-profile disable flag so status --json
            // can't claim a bypassed profile is active.
            po["disable"]   = p.dev_cfg.disable;
            const char* mode_s = "noaccel";
            if (p.prof.raw_passthrough) {
                mode_s = "raw";
            } else {
                switch (p.prof.accel_x.mode) {
                case accel_mode::classic:     mode_s = "classic";     break;
                case accel_mode::power:       mode_s = "power";       break;
                case accel_mode::natural:     mode_s = "natural";     break;
                case accel_mode::jump:        mode_s = "jump";        break;
                case accel_mode::synchronous: mode_s = "synchronous"; break;
                case accel_mode::lookup:      mode_s = "lookup";      break;
                default: break;
                }
            }
            po["mode"] = mode_s;
            out["profiles"].push_back(po);
        }
        if (running) {
            try {
                auto resp = daemon_ipc_query("status");
                nlohmann::json j = nlohmann::json::parse(resp);
                if (j.contains("devices") && j["devices"].is_array())
                    out["devices"] = j["devices"];
            } catch (const std::exception& e) {
                out["device_error"] = e.what();
            }
        }
    } catch (...) {
        config_ok = false;
        out["config_error"] = true;
    }
    std::cout << out.dump(2) << "\n";
    // P115-A5-07: config_error must be a failure exit, not daemon-status-only.
    // Exit 0 = daemon running + config OK; 1 = config error; 2 = daemon stopped.
    if (!config_ok) return 1;
    if (!running)   return 2;
    return 0;
}

static int cmd_status(const std::string& config_path) {
    if (g_json) return cmd_status_json(config_path);
    bool running = daemon_running();
    std::cout << "Daemon:  " << (running ? "running" : "stopped") << "\n";
    bool config_ok = true;
    try {
        auto cfg = load_config(config_path);
        std::cout << "Config:  " << config_path << "\n";
        std::cout << "Active:  " << cfg.active_profile << "\n";
        // P115-A5-09: surface the global raw-input mode; it silently stayed
        // hidden when set (status only showed per-profile fields before).
        if (cfg.use_raw_input)
            std::cout << "Raw-input passthrough flag: true  (stored flag — dormant: daemon hot path "
                         "ignores it; real 1:1 raw = set-param <profile> raw true)\n";
        std::cout << "Profiles (" << cfg.profiles.size() << "):\n";
        for (auto& p : cfg.profiles) {
            bool is_active = (p.name == cfg.active_profile);
            std::cout << "  " << (is_active ? "* " : "  ") << p.name;
            // P115-A5-09: flag disabled profiles in status output too.
            if (p.dev_cfg.disable)
                std::cout << "  [disabled]";
            // Show device assignment if set
            if (!p.device_id.empty())
                std::cout << "  [device: " << p.device_id << "]";
            // Show mode summary
            const char* mode_s = "noaccel";
            if (p.prof.raw_passthrough) {
                mode_s = "raw";
            } else {
                switch (p.prof.accel_x.mode) {
                case accel_mode::classic:     mode_s = "classic";     break;
                case accel_mode::power:       mode_s = "power";       break;
                case accel_mode::natural:     mode_s = "natural";     break;
                case accel_mode::jump:        mode_s = "jump";        break;
                case accel_mode::synchronous: mode_s = "synchronous"; break;
                case accel_mode::lookup:      mode_s = "lookup";      break;
                default: break;
                }
            }
            std::cout << "  (mode: " << mode_s
                      << ", DPI: " << p.dev_cfg.dpi
                      << ", poll: " << p.dev_cfg.polling_rate << "Hz)\n";
        }

        // Live device details from a running daemon (detected DPI/poll/battery,
        // effective profile match).  Only available when the daemon is reachable.
        if (running) {
            try {
                auto resp = daemon_ipc_query("status");
                nlohmann::json j = nlohmann::json::parse(resp);
                if (j.contains("devices") && j["devices"].is_array()) {
                    size_t ndev = j["devices"].size();
                    std::cout << "\nDevices (" << ndev << "):\n";
                    for (auto& d : j["devices"]) {
                        std::string name    = d.value("name", "");
                        std::string path    = d.value("path", "");
                        std::string dev_id  = d.value("device_id", "");
                        int dpi             = d.value("dpi", 0);
                        int poll            = d.value("poll_rate", 0);
                        int det_dpi         = d.value("detected_dpi", 0);
                        int det_poll        = d.value("detected_polling_rate", 0);
                        int battery         = d.value("detected_battery", -1);

                        std::cout << "  - " << (name.empty() ? "(unnamed)" : name) << "\n";
                        std::cout << "    path       : " << path << "\n";
                        std::cout << "    device_id  : " << dev_id << "\n";
                        std::cout << "    config dpi/poll: " << dpi << " / " << poll << " Hz";
                        if (det_dpi > 0 || det_poll > 0)
                            std::cout << "    detected: "
                                      << (det_dpi > 0 ? std::to_string(det_dpi) + " dpi" : "? dpi")
                                      << " / " << (det_poll > 0 ? std::to_string(det_poll) + " Hz" : "? Hz");
                        std::cout << "\n";
                        if (battery >= 0 && battery <= 100)
                            std::cout << "    battery    : " << battery
                                      << (battery <= 20 ? "% (LOW)" : "%") << "\n";

                        // Effective profile: same priority as the daemon's find_profile
                        // (device-specific → empty-device_id catch-all → active → first).
                        const device_profile* matched = nullptr;
                        for (auto& p : cfg.profiles)
                            if (!p.device_id.empty() && p.device_id == dev_id) { matched = &p; break; }
                        // BUG-NEW-60: the daemon's find_profile() checks empty-device_id
                        // ("All devices") catch-all profiles BEFORE active_profile; the
                        // CLI previously skipped this step, showing a different profile
                        // than the daemon actually applied.
                        if (!matched) {
                            for (auto& p : cfg.profiles)
                                if (p.device_id.empty()) { matched = &p; break; }
                        }
                        if (!matched) {
                            for (auto& p : cfg.profiles)
                                if (p.name == cfg.active_profile) { matched = &p; break; }
                        }
                        if (!matched && !cfg.profiles.empty()) matched = &cfg.profiles[0];
                        if (matched) {
                            std::cout << "    profile    : " << matched->name;
                            if (!matched->device_id.empty())
                                std::cout << "  (device-specific)";
                            else if (matched->name == cfg.active_profile)
                                std::cout << "  (active)";
                            else
                                std::cout << "  (first-profile fallback)";
                            std::cout << "\n";
                        }

                        // Latency stats (if any were recorded).
                        if (d.contains("lat_samples") && d.value("lat_samples", static_cast<uint64_t>(0)) > 0) {
                            std::cout << "    latency    : "
                                      << d.value("lat_samples", static_cast<uint64_t>(0)) << " samples, "
                                      << "p50 " << d.value("lat_p50_us", 0.0) << " µs, "
                                      << "p95 " << d.value("lat_p95_us", 0.0) << " µs, "
                                      << "max " << d.value("lat_max_us", 0.0) << " µs\n";
                        }
                    }
                } else {
                    std::cout << "\nDevices: daemon reports none grabbed.\n";
                }
            } catch (const std::exception& e) {
                std::cout << "\n(daemon unreachable for live device details: "
                          << e.what() << ")\n";
            }
        }
        if (running)
            std::cout << "\nTip: rawaccel-cli latency  → dump latency stats\n";
    } catch (...) {
        config_ok = false;
        std::cerr << "Config:  " << config_path << " (unreadable or missing)\n";
    }
    // P115-A5-07: a broken config used to print to STDOUT and exit 0 whenever
    // the daemon happened to be running — scripts saw "success" while the
    // effective config was actually unreachable.  Report on stderr and fail.
    if (!config_ok) return 1;
    if (!running)   return 2;
    return 0;
}

static int cmd_receivers() {
    const auto receivers = discover_logitech_receivers();
    if (g_json) {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& r : receivers) {
            out.push_back({
                {"sysfs_path", r.sysfs_path}, {"product", r.product},
                {"vendor_id", r.vendor_id}, {"product_id", r.product_id},
                {"kind", logitech_receiver_kind_name(r.kind)},
                {"max_paired_devices", r.max_paired_devices},
                {"hidpp_supported", r.hidpp_supported},
            });
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }
    if (receivers.empty()) {
        std::cout << "No Logitech USB receivers found.\n";
        return 0;
    }
    for (const auto& r : receivers) {
        std::cout << r.product << " ("
                  << std::hex << std::setfill('0') << std::setw(4) << r.vendor_id
                  << ":" << std::setw(4) << r.product_id << std::dec << ")\n"
                  << "  Type: " << logitech_receiver_kind_name(r.kind)
                  << " | Pairing slots: " << r.max_paired_devices
                  << " | HID++: " << (r.hidpp_supported ? "supported" : "not supported")
                  << "\n  Path: " << r.sysfs_path << "\n";
    }
    return 0;
}

static int cmd_hidpp() {
    auto hidraw_devices = discover_logitech_hidraw_devices();
    if (g_json) {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& path : hidraw_devices) {
            nlohmann::json entry;
            entry["hidraw_path"] = path;
            if (auto dev = identify_logitech_device(path)) {
                entry["connected"] = true;
                entry["vendor_id"] = dev->vendor_id;
                entry["product_id"] = dev->product_id;
                entry["device_index"] = dev->device_index;
                entry["name"] = dev->info.name;
                entry["friendly_name"] = dev->info.friendly_name;
                entry["serial"] = dev->info.serial;
                entry["firmware_version"] = dev->info.firmware_version;
                entry["firmware_name"] = dev->info.firmware_name;
                entry["firmware_major"] = dev->info.firmware_major;
                entry["firmware_minor"] = dev->info.firmware_minor;
                entry["firmware_build"] = dev->info.firmware_build;
                entry["unit_id"] = dev->info.unit_id;
                entry["model_id"] = dev->info.model_id;
                entry["transport_flags"] = dev->info.transport_flags;
                entry["device_kind"] = dev->info.device_kind;
                entry["protocol_version"] = dev->info.protocol_version;
                entry["bluetooth_id"] = dev->info.bluetooth_id;
                entry["bluetooth_le_id"] = dev->info.bluetooth_le_id;
                entry["wireless_pid"] = dev->info.wireless_pid;
                entry["usb_id"] = dev->info.usb_id;
                nlohmann::json firmware_records = nlohmann::json::array();
                for (const auto& record : dev->info.firmware_records) {
                    firmware_records.push_back({
                        {"level", record.level},
                        {"name", record.name},
                        {"major", record.major},
                        {"minor", record.minor},
                        {"build", record.build},
                        {"transport_flags", record.transport_flags},
                        {"unit_id", record.unit_id},
                        {"model_id", record.model_id},
                        {"extras_hex", record.extras_hex}
                    });
                }
                entry["firmware_records"] = firmware_records;
                nlohmann::json feature_json = nlohmann::json::array();
                for (const auto& feature : dev->feature_metadata) {
                    const auto feature_id = feature.feature_id;
                    feature_json.push_back({
                        {"id", feature_id},
                        {"id_hex", "0x" + [&] {
                            std::ostringstream s;
                            s << std::hex << std::uppercase << std::setw(4)
                              << std::setfill('0') << feature_id;
                            return s.str();
                        }()},
                        {"name", hidpp_feature_name(feature_id)},
                        {"index", feature.index},
                        {"flags", feature.flags},
                        {"flags_hex", "0x" + [&] {
                            std::ostringstream s;
                            s << std::hex << std::uppercase << std::setw(2)
                              << std::setfill('0') << static_cast<unsigned>(feature.flags);
                            return s.str();
                        }()},
                        {"version", feature.version}
                    });
                }
                entry["features"] = feature_json;

                // Query additional info using a transport with the correct device index
                HidppTransport transport(path);
                if (transport.is_open()) {
                    transport.set_device_index(dev->device_index);
                    if (auto battery = transport.get_battery_status(dev->device_index)) {
                        entry["battery"]["level"] = battery->level;
                        entry["battery"]["charging"] = battery->charging;
                        entry["battery"]["online"] = battery->online;
                    }
                    if (auto dpi = transport.get_dpi_info(dev->device_index)) {
                        entry["dpi"]["min"] = dpi->dpi_min;
                        entry["dpi"]["max"] = dpi->dpi_max;
                        entry["dpi"]["default"] = dpi->dpi_default;
                        entry["dpi"]["current"] = dpi->dpi_current;
                        entry["dpi"]["current_y"] = dpi->dpi_current_y;
                        entry["dpi"]["extended"] = dpi->extended;
                        entry["dpi"]["supports_lift_off_distance"] =
                            dpi->supports_lift_off_distance;
                        if (dpi->supports_lift_off_distance)
                            entry["dpi"]["lift_off_distance"] = dpi->lift_off_distance;
                        entry["dpi"]["levels"] = dpi->dpi_levels;
                    }
                    if (auto rate = transport.get_polling_rate(dev->device_index))
                        entry["polling_rate_hz"] = *rate;
                    if (auto onboard = transport.get_onboard_profile_info(
                            dev->device_index)) {
                        entry["onboard_profiles"]["memory"] = onboard->memory;
                        entry["onboard_profiles"]["active_profile"] =
                            onboard->active_profile;
                        entry["onboard_profiles"]["macro_profile"] =
                            onboard->macro_profile;
                        entry["onboard_profiles"]["profile_count"] =
                            onboard->profile_count;
                        entry["onboard_profiles"]["button_count"] =
                            onboard->button_count;
                        entry["onboard_profiles"]["sector_count"] =
                            onboard->sector_count;
                        entry["onboard_profiles"]["profile_size"] =
                            onboard->profile_size;
                        entry["onboard_profiles"]["shift"] = onboard->shift;
                        nlohmann::json headers = nlohmann::json::array();
                        for (const auto& header : transport.get_onboard_profile_headers(
                                 dev->device_index)) {
                            headers.push_back({
                                {"profile", header.profile},
                                {"sector", header.sector},
                                {"enabled", header.enabled}
                            });
                        }
                        entry["onboard_profiles"]["headers"] = headers;
                    }
                    // Pairing info is queried on the receiver (0xFF)
                    auto slots = transport.get_pairing_info(0xFF);
                    nlohmann::json slots_json = nlohmann::json::array();
                    for (const auto& slot : slots) {
                        slots_json.push_back({
                            {"slot", slot.slot},
                            {"occupied", slot.occupied},
                            {"pid", slot.pid},
                            {"name", slot.name},
                            {"serial", slot.serial},
                            {"connection_type", slot.connection_type}
                        });
                    }
                    entry["pairing_slots"] = slots_json;
                }
            } else {
                entry["connected"] = false;
            }
            out.push_back(entry);
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    if (hidraw_devices.empty()) {
        std::cout << "No Logitech hidraw devices found.\n";
        return 0;
    }

    for (const auto& path : hidraw_devices) {
        std::cout << "Device: " << path << "\n";
        if (auto dev = identify_logitech_device(path)) {
            std::cout << "  Name: " << dev->info.name << "\n"
                      << "  Friendly name: " << dev->info.friendly_name << "\n"
                      << "  Serial: " << dev->info.serial << "\n"
                      << "  VID:PID = " << std::hex << std::setfill('0')
                      << std::setw(4) << dev->vendor_id << ":"
                      << std::setw(4) << dev->product_id << std::dec << "\n"
                      << "  Firmware: " << dev->info.firmware_version << "\n"
                      << "  Firmware record: " << dev->info.firmware_name
                      << " " << static_cast<int>(dev->info.firmware_major)
                      << "." << static_cast<int>(dev->info.firmware_minor)
                      << " build=" << dev->info.firmware_build << "\n"
                      << "  Unit/model: " << dev->info.unit_id << "/"
                      << dev->info.model_id << "\n"
                      << "  Transport IDs: BT=" << dev->info.bluetooth_id
                      << " BLE=" << dev->info.bluetooth_le_id
                      << " WPID=" << dev->info.wireless_pid
                      << " USB=" << dev->info.usb_id << "\n"
                      << "  Protocol: " << static_cast<int>(dev->info.protocol_version) << "\n";
             if (!dev->features.empty()) {
                 std::cout << "  Features:";
                 for (const auto& feature : dev->feature_metadata)
                     std::cout << " " << hidpp_feature_name(feature.feature_id)
                              << "=0x" << std::hex << std::setw(4)
                              << std::setfill('0') << feature.feature_id
                              << "@0x" << std::setw(2) << static_cast<int>(feature.index)
                              << "/v" << std::dec << static_cast<int>(feature.version)
                              << "/f0x" << std::hex << std::setw(2)
                              << static_cast<int>(feature.flags);
                 std::cout << std::dec << std::setfill(' ') << "\n";
             }

            HidppTransport transport(path);
            if (transport.is_open()) {
                transport.set_device_index(dev->device_index);
                if (auto battery = transport.get_battery_status(dev->device_index)) {
                    std::cout << "  Battery: " << (battery->level == 255 ? "unknown" : std::to_string(battery->level) + "%")
                              << (battery->charging ? " (charging)" : "")
                              << (battery->online ? "" : " (offline)") << "\n";
                }
                if (auto dpi = transport.get_dpi_info(dev->device_index)) {
                    std::cout << "  DPI: current=" << dpi->dpi_current
                              << " default=" << dpi->dpi_default
                              << " range=[" << dpi->dpi_min << "," << dpi->dpi_max << "]\n";
                    if (!dpi->dpi_levels.empty()) {
                        std::cout << "  DPI levels: ";
                        for (size_t i = 0; i < dpi->dpi_levels.size(); ++i) {
                            if (i) std::cout << ", ";
                            std::cout << dpi->dpi_levels[i];
                        }
                        std::cout << "\n";
                    }
                    if (dpi->supports_lift_off_distance) {
                        static constexpr const char* lod_names[] = {"low", "medium", "high"};
                        const auto lod = dpi->lift_off_distance < 3
                            ? lod_names[dpi->lift_off_distance] : "unknown";
                        std::cout << "  Lift-off distance: " << lod << "\n";
                    }
                }
                if (auto rate = transport.get_polling_rate(dev->device_index))
                    std::cout << "  Polling rate: " << *rate << " Hz\n";
                if (auto onboard = transport.get_onboard_profile_info(
                        dev->device_index)) {
                    std::cout << "  Onboard profiles: count="
                              << static_cast<int>(onboard->profile_count)
                              << " active="
                              << static_cast<int>(onboard->active_profile)
                              << " sectors="
                              << static_cast<int>(onboard->sector_count)
                              << " size=" << onboard->profile_size << " bytes\n";
                    for (const auto& header :
                         transport.get_onboard_profile_headers(dev->device_index))
                        std::cout << "    Profile "
                                  << static_cast<int>(header.profile)
                                  << ": sector=" << header.sector
                                  << " enabled=" << static_cast<int>(header.enabled)
                                  << "\n";
                }
                auto slots = transport.get_pairing_info(0xFF);
                if (!slots.empty()) {
                    std::cout << "  Pairing slots:\n";
                    for (const auto& slot : slots) {
                        std::cout << "    Slot " << static_cast<int>(slot.slot)
                                  << ": " << (slot.occupied ? "occupied" : "empty");
                        if (slot.occupied) {
                            std::cout << " (PID=0x" << std::hex << std::setw(4) << std::setfill('0')
                                      << slot.pid << std::dec << std::setfill(' ')
                                      << ", conn_type=" << static_cast<int>(slot.connection_type);
                            if (!slot.name.empty())
                                std::cout << ", name=" << slot.name;
                            if (!slot.serial.empty())
                                std::cout << ", serial=" << slot.serial;
                            std::cout << ")";
                        }
                        std::cout << "\n";
                    }
                }
            }
        } else {
            std::cout << "  (Not a HID++ device or unable to communicate)\n";
        }
        std::cout << "\n";
    }
    return 0;
}

static bool parse_hidpp_uint(const std::string& text, uint32_t& value) {
    if (text.empty() || text[0] == '-') return false;
    char* end = nullptr;
    errno = 0;
    // Base 10 only.  strtoul(..., 0) auto-detects bases, so "0100" would be
    // read as octal 64 and "0x100" as hex 256 — surprising for a DPI/rate
    // argument and inconsistent with the GUI's always-decimal widgets.
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
        parsed > UINT32_MAX)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

static std::optional<uint8_t> hidpp_target_from_args(const std::string& path,
                                                     const std::vector<std::string>& args,
                                                     size_t index) {
    if (args.size() > index) {
        uint32_t value = 0;
        if (parse_hidpp_uint(args[index], value) && value <= 0xFF)
            return static_cast<uint8_t>(value);
        return std::nullopt;
    }
    // Directly connected mice normally use 0x00.  A receiver is better
    // handled with an explicit paired-device index, but use the first target
    // returned by the normal identification probe when available.
    if (auto device = identify_logitech_device(path))
        return device->device_index;
    return 0x00;
}

static int cmd_hidpp_set_dpi(const std::vector<std::string>& args) {
    uint32_t dpi = 0;
    // Same 100..32000 window the GUI's Hardware DPI spin enforces
    // (gui/hidpp_panel.inl HW_DPI_MIN/HW_DPI_MAX).  The device itself will
    // reject values outside its advertised DPI list, but accepting 1..65535
    // here lets the CLI promise settings the GUI can never make.
    if (!parse_hidpp_uint(args[2], dpi) || dpi < 100 || dpi > 32000) {
        std::cerr << "DPI must be an integer from 100 to 32000.\n";
        return 1;
    }
    const auto target_value = hidpp_target_from_args(args[1], args, 3);
    if (!target_value) {
        std::cerr << "Device index must be an integer from 0 to 255.\n";
        return 1;
    }
    const uint8_t target = *target_value;
    HidppTransport transport(args[1]);
    if (!transport.is_open() || transport.vendor_id() != 0x046d) {
        std::cerr << "Unable to open HID++ device: " << args[1] << "\n";
        return 1;
    }
    transport.set_device_index(target);
    const bool ok = transport.set_dpi(static_cast<uint16_t>(dpi), target);
    if (g_json) std::cout << nlohmann::json{{"ok", ok}, {"dpi", dpi}}.dump() << "\n";
    else if (ok) std::cout << "DPI set to " << dpi << ".\n";
    else std::cerr << "DPI is not supported or the device rejected the value.\n";
    return ok ? 0 : 1;
}

static int cmd_hidpp_set_rate(const std::vector<std::string>& args) {
    uint32_t hz = 0;
    // Same polarity set the GUI's Hardware Polling Rate dropdown offers
    // (gui/hidpp_panel.inl HW_RATES: 125/250/500/1000/2000/4000/8000).
    // A rate the GUI cannot represent would bypass the dropdown's guarantee
    // and, worse, fall through to a misleading "unsupported" rejection.
    static constexpr uint32_t supported_rates[] = {125, 250, 500, 1000, 2000, 4000, 8000};
    const bool rate_supported = [&] {
        if (!parse_hidpp_uint(args[2], hz) || hz == 0) return false;
        for (uint32_t r : supported_rates)
            if (hz == r) return true;
        return false;
    }();
    if (!rate_supported) {
        std::cerr << "Polling rate must be one of: 125, 250, 500, 1000, "
                     "2000, 4000, 8000 Hz.\n";
        return 1;
    }
    const auto target_value = hidpp_target_from_args(args[1], args, 3);
    if (!target_value) {
        std::cerr << "Device index must be an integer from 0 to 255.\n";
        return 1;
    }
    const uint8_t target = *target_value;
    HidppTransport transport(args[1]);
    if (!transport.is_open() || transport.vendor_id() != 0x046d) {
        std::cerr << "Unable to open HID++ device: " << args[1] << "\n";
        return 1;
    }
    transport.set_device_index(target);
    const bool ok = transport.set_polling_rate(hz, target);
    if (g_json) std::cout << nlohmann::json{{"ok", ok}, {"polling_rate_hz", hz}}.dump() << "\n";
    else if (ok) std::cout << "Polling rate set to " << hz << " Hz.\n";
    else std::cerr << "Polling rate is unsupported or the device rejected the value.\n";
    return ok ? 0 : 1;
}

static int cmd_hidpp_set_lod(const std::vector<std::string>& args) {
    std::string value = args[2];
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::optional<hidpp_lift_off_distance> distance;
    if (value == "low") distance = hidpp_lift_off_distance::low;
    else if (value == "medium" || value == "med")
        distance = hidpp_lift_off_distance::medium;
    else if (value == "high") distance = hidpp_lift_off_distance::high;
    else {
        std::cerr << "Lift-off distance must be low, medium, or high.\n";
        return 1;
    }
    const auto target_value = hidpp_target_from_args(args[1], args, 3);
    if (!target_value) {
        std::cerr << "Device index must be an integer from 0 to 255.\n";
        return 1;
    }
    const uint8_t target = *target_value;
    HidppTransport transport(args[1]);
    if (!transport.is_open() || transport.vendor_id() != 0x046d) {
        std::cerr << "Unable to open HID++ device: " << args[1] << "\n";
        return 1;
    }
    transport.set_device_index(target);
    const bool ok = transport.set_lift_off_distance(*distance, target);
    if (g_json)
        std::cout << nlohmann::json{{"ok", ok}, {"lift_off_distance", value}}.dump() << "\n";
    else if (ok) std::cout << "Lift-off distance set to " << value << ".\n";
    else std::cerr << "Lift-off distance is not supported by this device.\n";
    return ok ? 0 : 1;
}

// ── Help ──────────────────────────────────────────────────────────────────────

static void print_help() {
    std::cout <<
R"(rawaccel-cli v)" << VERSION << R"( — Raw Accel Linux

Usage: rawaccel-cli [OPTIONS] <COMMAND> [ARGS...]

Commands:
  list                          List all profiles
  show <profile>                Show profile details
  set <profile>                 Set active profile
  create <profile>              Create new profile with defaults
  create-preset <preset> <name> Create profile from preset (gaming, office, precision, disable, cs2, valorant, apex, fps)
  delete <profile>              Delete a profile
  rename <old> <new>            Rename a profile
  duplicate <src> <dst>         Duplicate a profile (clears device_id)
  set-param <profile> <key> <value>
                                 Set a parameter in a profile
  export [profile]              Export profile as JSON to stdout
  import <file.json>            Import profile from JSON file
  status                        Show daemon status, profiles, and device assignments
  receivers                     List detected Logitech receivers and HID++ capability
  hidpp                         List detected Logitech HID++ devices and query info
  hidpp-set-dpi <hidraw> <dpi> [device-index]
                                Set Logitech HID++ DPI (HID++ 2.0), 100-32000
  hidpp-set-rate <hidraw> <hz> [device-index]
                                Set Logitech HID++ polling/report rate
                                (125, 250, 500, 1000, 2000, 4000, 8000 Hz)
  hidpp-set-polling-rate <hidraw> <hz> [device-index]
                                Alias for hidpp-set-rate
  hidpp-set-report-rate <hidraw> <hz> [device-index]
                                Alias for hidpp-set-rate
  hidpp-set-lod <hidraw> <low|medium|high> [device-index]
                                Set sensor lift-off distance when supported
                                (HID++ 0x2202 LOD capability only)
  validate                      Validate config file for errors/warnings
  reload                        Reload daemon config (SIGHUP)
  stop                          Stop daemon (SIGTERM)
  latency                       Dump per-device processing latency stats (SIGUSR1)

Options:
  -c, --config PATH             Config file path
  -h, --help                    Show this help
  -V, --version                 Show version
  --json                        Emit machine-readable JSON for list/show/status
  -n, --no-daemon, --dry-run    Save config changes locally only — do NOT push
                                them to the running daemon (default: live-apply)

Presets (values loaded by `create-preset <preset> <name>`):
  preset      mode            gain  exp-power  cap [in,out]  output-offset
  gaming      classic         on    —          —             —
  office      natural         on    —          —             —
  precision   classic         on    —          —             —
  disable     raw-passthrough (noaccel, all processing bypassed)
  cs2         classic         on    —          [18.0, 1.6]   —
  valorant    natural         on    —          [30.0, 2.0]   —
  apex        power           on    0.8        [28.0, 2.2]   0.9
  fps         classic         on    —          [20.0, 1.8]   —
  exp-power = power-mode exponent; classic/natural presets use
  exponent_classic/other defaults instead.  All presets: dpi 800, poll 1000.

Parameters (for set-param). Domain = accepted range — out-of-range values are
REJECTED (exit 1, config untouched); default = fresh `create` profile value:
  raw               true|false|1|0  (raw passthrough — bypass all processing). Default false.
  mode              classic|power|natural|jump|synchronous|lookup|noaccel. Default noaccel.
  device_id         Assign profile to a device ("usb:VVVV:PPPP:serial", by-id path,
                    or event node); empty string = all mice. Hint: run `status`
                    to list devices with their device_id values. Default: empty.
  gain              true|false|1|0  (gain mode on/off). Default true.
  acceleration      Acceleration multiplier. Domain any finite (negative = classic
                    decel). Default 0.005.
  exponent_classic  Classic exponent. Domain 1–10. Default 2.
  exponent_power    Power exponent (synchronous ignores it). Domain 1e-4–5. Default 0.05.
  limit             Upper multiplier asymptote (natural mode). Domain ≥ 0. Default 1.5.
  decay_rate        Natural decay rate. Domain ≥ 0. Default 0.1.
  motivity          Synchronous motivity. Domain ≥ 0. Default 1.5.
  gamma             Synchronous gamma. Domain ≥ 0. Default 1.
  input_offset      Speed offset before acceleration starts. Domain ≥ 0. Default 0.
  output_offset     Output offset (power mode). Domain 0–100. Default 0.
  scale             Scale factor (power mode). Domain 0–100. Default 1.
  sync_speed        Synchronous sync speed. Domain ≥ 1e-4. Default 5.
  smooth            Jump/synchronous smoothness. Domain ≥ 0. Default 0.5.
  cap_x             Input speed cap. Domain 0–500. Default 15.
  cap_y             Output gain cap. Domain 0–100. Default 1.5.
  cap_mode          out|in|io  (cap mode). Default out.
  rotation          Rotation in degrees. Domain any finite (normalized mod 360:
                    −45 → 315, 400 → 40). Default 0.
  snap              Snap angle in degrees. Domain 0–45. Default 0.
  dpi               Mouse DPI. Domain 1–32000 (integer). Default 800.
  polling_rate      Mouse polling rate (Hz). Domain 125–8000 (integer). Default 1000.
  speed_min         Minimum speed clamp (ips). Domain ≥ 0. Default 0 (off).
  speed_max         Maximum speed clamp (ips). Domain ≥ 0; if both set, max ≥ min.
                    Default 0 (off).
  output_dpi        Output DPI normalization value. Domain 1–32000. Default 1000.
  lr_ratio          Left/right output DPI ratio. Domain 0.01–100. Default 1 (off).
  ud_ratio          Up/down output DPI ratio. Domain 0.01–100. Default 1 (off).
  yx_ratio          Y-axis output DPI ratio (relative to X). Domain 0.01–100. Default 1.
  distance_mode     euclidean|max|lp|separate  (speed calculation method). Default euclidean.
  lp_norm           Lp-norm value (when distance_mode=lp). Domain > 0. Default 2.
  input_smooth_halflife   Input speed EMA halflife (ms). Domain ≥ 0; 0=off. Default 0.
  scale_smooth_halflife   Scale EMA halflife (ms). Domain ≥ 0; 0=off. Default 0.
  output_smooth_halflife  Output speed EMA halflife (ms). Domain ≥ 0; 0=off. Default 0.
  domain_weights    Both-axis domain weight. Domain 0–1e6. Default 1.
  domain_weight_x / domain_weight_y
                    Per-axis domain weight. Domain 0–1e6. Default 1.
  range_weights     Both-axis range weight. Domain 0–1e6. Default 1.
  range_weight_x / range_weight_y
                    Per-axis range weight. Domain 0–1e6. Default 1.

Examples:
  rawaccel-cli list
  rawaccel-cli create gaming
  rawaccel-cli set-param gaming mode classic
  rawaccel-cli set-param gaming acceleration 0.005
  rawaccel-cli set-param gaming exponent_classic 2
  rawaccel-cli set-param gaming limit 1.8
  rawaccel-cli set gaming
  rawaccel-cli export gaming > backup.json
)";
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string config_path;
    bool config_path_explicit = false;  // set when the user passed -c/--config at all
    std::vector<std::string> args;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            // Trailing "-c" with no value used to be pushed into `args` and then
            // reported as "Unknown command: -c".  Say what's actually wrong.
            if (i + 1 >= argc) {
                std::cerr << "Option '" << argv[i] << "' requires a path argument.\n";
                return 1;
            }
            config_path = argv[++i];
            config_path_explicit = true;
        } else if (strncmp(argv[i], "--config=", 9) == 0) {
            // P115-A5-10: accept the `--config=path` (= form) like the daemon's
            // parser does (P53).  Previously this was rejected as an unknown
            // command with a 101-line help dump to stdout — CLI/daemon parity gap.
            config_path = argv[i] + 9;
            config_path_explicit = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            std::cout << "rawaccel-cli " << VERSION << "\n";
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            g_json = true;
        } else if (strcmp(argv[i], "-n") == 0 ||
                   strcmp(argv[i], "--no-daemon") == 0 ||
                   strcmp(argv[i], "--dry-run") == 0) {
            g_no_daemon = true;
        } else {
            args.push_back(argv[i]);
        }
    }

    if (args.empty()) { print_help(); return 0; }

    // P99: an explicit `-c ""` used to be indistinguishable from "no -c given"
    // and silently fell back to find_config_path() — mutating the real
    // ~/.config/rawaccel/settings.json (or /etc) when the user fat-fingered an
    // empty path.  An explicit empty path is a user error: reject it.
    if (config_path_explicit && config_path.empty()) {
        std::cerr << "Option '-c/--config' requires a non-empty path (empty string "
                     "would silently edit the default config).\n";
        return 1;
    }

    // Targeted arity errors (P99): every subcommand has a min/max argument count.
    // Too few → "missing N argument(s)", too many → "takes at most N" — both exit
    // 1 with a clear message instead of silently ignoring the extras (previously
    // `rawaccel-cli create foo bar` created "foo" and dropped "bar").
    struct arity_spec { const char* cmd; int min; int max; };
    static constexpr arity_spec arities[] = {
        { "show",          1, 1 },
        { "set",           1, 1 },
        { "create",        1, 1 },
        { "delete",        1, 1 },
        { "rename",        2, 2 },
        { "duplicate",     2, 2 },
        { "create-preset", 2, 2 },
        { "set-param",     3, 3 },
        { "import",        1, 1 },
        { "export",        0, 1 },
        { "list",          0, 0 },
        { "validate",      0, 0 },
        { "status",        0, 0 },
        { "receivers",     0, 0 },
        { "hidpp",         0, 0 },
        { "hidpp-set-dpi", 2, 3 },
        { "hidpp-set-rate", 2, 3 },
        { "hidpp-set-polling-rate", 2, 3 },
        { "hidpp-set-report-rate", 2, 3 },
        { "hidpp-set-lod", 2, 3 },
        { "reload",        0, 0 },
        { "stop",          0, 0 },
        { "latency",       0, 0 },
    };
    int min_args = -1, max_args = -1;
    for (auto& a : arities) {
        if (a.cmd == args[0]) { min_args = a.min; max_args = a.max; break; }
    }
    if (min_args >= 0) {
        // `args` includes the command word itself; the table counts positional
        // arguments only (matching the usage lines in --help).
        const int nargs = static_cast<int>(args.size()) - 1;
        if (nargs < min_args) {
            std::cerr << "Command '" << args[0] << "' is missing "
                      << (min_args - nargs) << " argument(s).\n";
            std::cerr << "Usage: rawaccel-cli " << args[0];
            if      (args[0] == "show" || args[0] == "set" ||
                     args[0] == "create" || args[0] == "delete")
                std::cerr << " <profile>";
            else if (args[0] == "rename")          std::cerr << " <old> <new>";
            else if (args[0] == "duplicate")       std::cerr << " <src> <dst>";
            else if (args[0] == "create-preset")   std::cerr << " <preset> <name>";
            else if (args[0] == "set-param")       std::cerr << " <profile> <key> <value>";
            else if (args[0] == "import")          std::cerr << " <file.json>";
            else if (args[0] == "export")          std::cerr << " [profile]";
            else if (args[0] == "hidpp-set-dpi")   std::cerr << " <hidraw> <dpi> [device-index]";
            else if (args[0] == "hidpp-set-rate" ||
                     args[0] == "hidpp-set-polling-rate" ||
                     args[0] == "hidpp-set-report-rate")
                std::cerr << " <hidraw> <hz> [device-index]";
            else if (args[0] == "hidpp-set-lod")   std::cerr << " <hidraw> <low|medium|high> [device-index]";
            std::cerr << "\n";
            return 1;
        }
        if (nargs > max_args) {
            std::cerr << "Command '" << args[0] << "' takes at most " << max_args
                      << " argument(s), but got " << nargs << ".\n";
            std::cerr << "Usage: rawaccel-cli " << args[0];
            if      (args[0] == "set-param")       std::cerr << " <profile> <key> <value>";
            else if (args[0] == "rename")          std::cerr << " <old> <new>";
            else if (args[0] == "duplicate")       std::cerr << " <src> <dst>";
            else if (args[0] == "create-preset")   std::cerr << " <preset> <name>";
            else if (args[0] == "import")          std::cerr << " <file.json>";
            else if (args[0] == "export")          std::cerr << " [profile]";
            else if (args[0] == "hidpp-set-dpi")   std::cerr << " <hidraw> <dpi> [device-index]";
            else if (args[0] == "hidpp-set-rate" ||
                     args[0] == "hidpp-set-polling-rate" ||
                     args[0] == "hidpp-set-report-rate")
                std::cerr << " <hidraw> <hz> [device-index]";
            else if (args[0] == "hidpp-set-lod")   std::cerr << " <hidraw> <low|medium|high> [device-index]";
            else if (args[0] == "show" || args[0] == "set" ||
                     args[0] == "create" || args[0] == "delete")
                std::cerr << " <profile>";
            std::cerr << "\n";
            return 1;
        }
    }

    // P130-R49 (BUG): an UNKNOWN command must fail before any config access.
    // Previously `rawaccel-cli <typo>` fell through to the config-load block,
    // which CREATED a fresh default settings.json as a side effect of a typo
    // (verified: `rawaccel-cli -c /tmp/x/settings.json bogus` wrote a file).
    // Reject up front, before find_config_path() / load / default-creation.
    bool known_cmd = false;
    for (auto& a : arities) {
        if (a.cmd == args[0]) { known_cmd = true; break; }
    }
    if (!known_cmd) {
        std::cerr << "Unknown command: " << args[0] << "\n";
        print_help();
        return 1;
    }

    if (!config_path_explicit && config_path.empty()) config_path = find_config_path();

    // Commands that don't need config loaded
    if (args[0] == "reload") return cmd_reload();
    if (args[0] == "stop")   return cmd_stop();
    if (args[0] == "status") return cmd_status(config_path);
    if (args[0] == "receivers") return cmd_receivers();
    if (args[0] == "hidpp") return cmd_hidpp();
    if (args[0] == "hidpp-set-dpi") return cmd_hidpp_set_dpi(args);
    if (args[0] == "hidpp-set-rate") return cmd_hidpp_set_rate(args);
    if (args[0] == "hidpp-set-polling-rate") return cmd_hidpp_set_rate(args);
    if (args[0] == "hidpp-set-report-rate") return cmd_hidpp_set_rate(args);
    if (args[0] == "hidpp-set-lod") return cmd_hidpp_set_lod(args);
    if (args[0] == "validate") return cmd_validate(config_path);
    if (args[0] == "latency") {
        // Try the IPC "latency" command first — works for any user in the
        // input group regardless of who owns the daemon.  Falls back to
        // SIGUSR1 if the running daemon doesn't speak that command yet.
        std::string ipc_resp = daemon_ipc_query("latency");
        if (ipc_resp.find("\"ok\":true") != std::string::npos) {
            std::cout << "Latency dump scheduled. View it with: journalctl -u rawaccel -n 30\n";
            return 0;
        }
        auto r = send_signal_to_daemon(SIGUSR1);
        if (r == signal_result::sent) {
            std::cout << "SIGUSR1 sent. View stats with: journalctl -u rawaccel -n 30\n";
            return 0;
        }
        print_signal_failure(r, "request latency stats from", "USR1");
        return 1;
    }

    // Load config (create default only if the file is genuinely missing).
    // Overwriting on *any* load failure would silently destroy a config that
    // is merely corrupt (bad JSON) or unreadable — a preventable data loss.
    const bool config_exists = ::access(config_path.c_str(), F_OK) == 0;
    app_config cfg;
    if (!config_exists) {
        // Create minimal default.  C-1: keep active_profile non-empty and
        // pointing at the very profile we just created so the saved file is
        // self-consistent (a dangling/empty active_profile makes the first
        // `rawaccel-cli delete default` fail confusingly).
        device_profile dp;
        dp.name = "default";
        dp.dev_cfg.dpi = 800;
        dp.dev_cfg.polling_rate = 1000;
        cfg.profiles.push_back(dp);
        cfg.active_profile = dp.name;
        try { save_config(cfg, config_path); }
        catch (const std::exception& e) {
            // C-9: a missing default config is fatal — without it every
            // later command runs against an empty/phantom in-memory config.
            std::cerr << "ERROR: could not save default config: " << e.what() << "\n";
            return 1;
        } catch (...) {
            std::cerr << "ERROR: could not save default config.\n";
            return 1;
        }
    } else {
        try {
            cfg = load_config(config_path);
        } catch (const std::exception& e) {
            std::cerr << "Config is unreadable or invalid: " << config_path << "\n"
                      << "  (" << e.what() << ")\n"
                      << "  Refusing to overwrite it — run `rawaccel-cli validate` for details.\n";
            return 1;
        } catch (...) {
            std::cerr << "Config is unreadable or invalid: " << config_path << "\n"
                      << "  Refusing to overwrite it — run `rawaccel-cli validate` for details.\n";
            return 1;
        }
    }

    const std::string& cmd = args[0];

    if (cmd == "list") {
        if (g_json) return cmd_list_json(cfg);
        return cmd_list(cfg);
    }
    if (cmd == "show") {
        if (g_json) {
            // P99: --json is a global flag — honor it for show too, not just list.
            for (auto& dp : cfg.profiles) {
                if (dp.name == args[1]) {
                    std::cout << profile_to_json(dp) << "\n";
                    return 0;
                }
            }
            std::cerr << "Profile not found: " << args[1] << "\n";
            return 1;
        }
        return cmd_show(cfg, args[1]);
    }
    if (cmd == "set")    return cmd_set(cfg, config_path, args[1]);
    if (cmd == "create") return cmd_create(cfg, config_path, args[1]);
    if (cmd == "delete") return cmd_delete(cfg, config_path, args[1]);
    if (cmd == "rename") return cmd_rename(cfg, config_path, args[1], args[2]);
    if (cmd == "duplicate") return cmd_duplicate(cfg, config_path, args[1], args[2]);
    if (cmd == "create-preset") return cmd_create_preset(cfg, config_path, args[1], args[2]);
    if (cmd == "set-param") return cmd_set_param(cfg, config_path, args[1], args[2], args[3]);
    if (cmd == "export") return cmd_export(cfg, args.size() >= 2 ? args[1] : "");
    if (cmd == "import") return cmd_import(cfg, config_path, args[1]);

    std::cerr << "Unknown command: " << cmd << "\n";
    print_help();
    return 1;
}
