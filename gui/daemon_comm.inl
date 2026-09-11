// ── Daemon communication: PID lookup, status display, signal sending ──────────
#include <sys/socket.h>
#include <sys/un.h>
#include <cerrno>
#include <climits>

// ── KDE / libinput acceleration detection ────────────────────────────────────

/// Returns true if the current desktop session is KDE Plasma.
static bool is_kde_session() {
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    if (desktop && (strstr(desktop, "KDE") || strstr(desktop, "plasma")))
        return true;
    const char* session = std::getenv("DESKTOP_SESSION");
    if (session && (strstr(session, "plasma") || strstr(session, "kde")))
        return true;
    return false;
}

/// Returns true if the current session is Wayland.
static bool is_wayland_session() {
    const char* wt = std::getenv("WAYLAND_DISPLAY");
    if (wt && wt[0] != '\0') return true;
    const char* xdg_st = std::getenv("XDG_SESSION_TYPE");
    if (xdg_st && strcmp(xdg_st, "wayland") == 0) return true;
    return false;
}

/// Check KDE's kwinrc to see if pointer acceleration is set to flat/none.
/// Returns:
///   0 = acceleration is disabled (flat) — OK
///   1 = acceleration is enabled / not flat — WARNING
///  -1 = cannot determine (kwinrc not found or key missing)
static int kde_libinput_accel_state() {
    // kwinrc location: $XDG_CONFIG_HOME/kwinrc  or  ~/.config/kwinrc
    std::string kwinrc_path;
    const char* xdg_cfg = std::getenv("XDG_CONFIG_HOME");
    if (xdg_cfg && xdg_cfg[0] != '\0') {
        kwinrc_path = std::string(xdg_cfg) + "/kwinrc";
    } else {
        const char* home = std::getenv("HOME");
        if (!home || home[0] == '\0') return -1;
        kwinrc_path = std::string(home) + "/.config/kwinrc";
    }

    FILE* f = fopen(kwinrc_path.c_str(), "r");
    if (!f) return -1;

    // Scan for [Libinput] section and PointerAcceleration key
    bool in_libinput = false;
    // M-BUG-6: real kwinrc lines are short, but a long nested device section
    // header (e.g. "[Libinput][3][1133][50498][Logitech ... (RawAccel)]") plus a
    // long key=value can exceed 512 bytes; fgets would truncate mid-pair and the
    // key would be mis-read.  Generous limit that still bounds hostile files.
    char line[8192];
    int result = -1;
    while (fgets(line, sizeof(line), f)) {
        // Strip newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (line[0] == '[') {
            // strncmp(...,10) only confirms the prefix matches; a section header
            // like "[LibinputSomething]" would otherwise be treated as Libinput.
            // Require the closing ']' right after the 10-byte section name.
            in_libinput = (strncmp(line, "[Libinput]", 10) == 0 && line[10] == ']');
            continue;
        }
        if (!in_libinput) continue;

        // PointerAccelerationProfile=1  (1=flat/none, 2=adaptive)
        if (strncmp(line, "PointerAccelerationProfile=", 27) == 0) {
            // BUG-6: atoi() invokes UB on out-of-range input (a malicious
            // kwinrc with "1e26" would propagate UB through the GUI).
            // strtol with explicit range/errno handling is safe.
            errno = 0;
            char* end = nullptr;
            long val = strtol(line + 27, &end, 10);
            int v = (end != line + 27 && errno == 0 &&
                     val >= INT_MIN && val <= INT_MAX) ? (int)val : 0;
            // 1 = flat (disabled) — good; 2 = adaptive (enabled) — bad
            result = (v == 1) ? 0 : 1;
        }
        // PointerAcceleration=0  (0 = no extra gain on top of flat)
        if (strncmp(line, "PointerAcceleration=", 20) == 0) {
            // BUG-16: atof() invokes UB on out-of-range input (C99 7.20.1.1).
            // A malicious kwinrc with "PointerAcceleration=1e1000" would
            // propagate UB through the GUI.  Use strtod + errno check; treat
            // unparseable / non-finite values as "non-zero" (i.e. accel on).
            errno = 0;
            char* end = nullptr;
            double val = std::strtod(line + 20, &end);
            bool parsed = (end != line + 20 && errno == 0 && std::isfinite(val));
            // A near-zero PointerAcceleration with flat profile is fine
            if (result == 0 && (!parsed || std::fabs(val) > 0.05)) result = 1;
        }
    }
    fclose(f);
    return result;
}

/// Candidate IPC socket paths.  Same rationale as in cli/main.cpp:
/// when the daemon is a *system* service it doesn't have XDG_RUNTIME_DIR
/// set so it lands on /run/rawaccel.sock, while the GUI is started from a
/// user session that *does* have XDG_RUNTIME_DIR — without walking both,
/// the GUI's IPC reload silently failed and fell back to SIGHUP (which
/// itself fails with EPERM against a root daemon).
static std::vector<std::string> daemon_sock_candidates() {
    std::vector<std::string> v;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0') v.push_back(std::string(xdg) + "/rawaccel.sock");
    v.push_back("/run/rawaccel.sock");
    return v;
}

/// True if any candidate IPC socket file exists on disk.  Cheap (stat-only):
/// lets tick-driven callers skip the IPC entirely — and its socket timeouts —
/// when the daemon is down (BUG-05 / BUG-10).
static bool daemon_socket_exists() {
    for (const auto& s : daemon_sock_candidates())
        if (access(s.c_str(), F_OK) == 0) return true;
    return false;
}

/// Raw IPC send: connect, write the full request bytes, read the response.
/// `timeout_ms` bounds BOTH the send and receive timeouts (default 150 ms —
/// deliberately smaller than the GUI's 250 ms telemetry tick, so a dead-but-
/// listening daemon can never wedge the main thread for 2×1 s, BUG-05).
/// The config-push RPC uses a much longer timeout because the daemon serves
/// set_config only after fsync'ing a root-owned file.
///
/// Response-end detection (BUG-06): the daemon terminates every reply with a
/// single '\n' (one JSON line per response), so the reader stops at the
/// newline or at a clean peer close — NOT when the socket timeout fires.  A
/// positive read that times out mid-line is treated as "incomplete" and
/// discarded, so a truncated JSON is never parsed (each call also builds a
/// fresh local buffer, so nothing carries over to the next tick).
/// Returns an empty string on failure.
static std::string daemon_ipc_send_raw(const std::string& req, int timeout_ms = 150) {
    for (const auto& sock : daemon_sock_candidates()) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) continue;

        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (sock.size() >= sizeof(addr.sun_path)) { close(fd); continue; }
        strncpy(addr.sun_path, sock.c_str(), sizeof(addr.sun_path) - 1);
        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close(fd); continue; // try the next candidate
        }

        // Send the full request, tolerating partial sends and EINTR (loop until done or real error).
        // An incomplete payload would otherwise be rejected by the daemon's
        // "incomplete config payload" guard even though retrying here is trivial.
        const char* p = req.data();
        size_t left = req.size();
        while (left > 0) {
            ssize_t w = send(fd, p, left, MSG_NOSIGNAL);
            if (w < 0) {
                if (errno == EINTR) continue; // interrupted — retry
                break; // real error / timeout / peer closed
            }
            if (w == 0) break; // peer closed
            p += w;
            left -= (size_t)w;
        }
        if (left > 0) { close(fd); continue; } // could not send fully — next candidate

        // Read until the newline-terminated response.  The 4 MiB cap is far
        // beyond any real payload (status + many devices + LUT) but keeps
        // memory bounded on a misbehaving peer.
        constexpr size_t MAX_IPC_RESPONSE_BYTES = 4 * 1024 * 1024;
        std::string resp;
        resp.reserve(8192);
        char buf[4096];
        bool complete = false;
        while (resp.size() < MAX_IPC_RESPONSE_BYTES) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n == 0) { complete = true; break; } // peer closed — clean EOF
            if (n < 0)  break;                      // timeout / error — incomplete
            resp.append(buf, (size_t)n);
            if (resp.find('\n') != std::string::npos) { complete = true; break; }
        }
        close(fd);
        return complete ? resp : std::string();
    }
    return {};
}

/// Send a one-line command to the daemon socket and return the response.
/// Returns an empty string on failure.
static std::string daemon_ipc_query(const std::string& cmd, int timeout_ms = 150) {
    return daemon_ipc_send_raw(cmd + "\n", timeout_ms);
}

/// P-APP: report the currently focused application's WM_CLASS (lowercased) to
/// the daemon so app-scoped profiles (`match_app`) can be live-applied.  The
/// daemon stashes the value on the loop thread and re-applies profiles only
/// when the app actually changed.  Empty string = "no app focused".
static void daemon_ipc_set_active_app(const std::string& wm_class) {
    std::string norm;
    norm.reserve(wm_class.size());
    for (char c : wm_class) {
        // Keep it to safe ASCII: WM_CLASS class is lowercase ASCII already, but
        // guard against hostile/spread values from the compositor.
        if (c >= 'a' && c <= 'z') norm.push_back(c);
        else if (c >= 'A' && c <= 'Z') norm.push_back(static_cast<char>(c + ('a' - 'A')));
        else if (c == '.' || c == '_' || c == '-' || c == '/' || c == '@')
            norm.push_back(c);
        // anything else dropped — substring matching in the daemon only ever
        // sees a trimmed, URL-safe-ish class token.
    }
    daemon_ipc_query("set_active_app " + (norm.empty() ? std::string("none") : norm),
                     /*timeout_ms=*/150);
}

/// Push the full config to the daemon over IPC (the daemon's "set_config" RPC).
/// The daemon persists it to ITS OWN config path — a root systemd daemon writes
/// /etc/rawaccel/settings.json even though the GUI's working copy lives in the
/// user's ~/.config — and live-applies it.  Uses a 5 s timeout: the daemon
/// blocks only until it has fsync'd its root-owned config file (that path was
/// tighter than 100 ms in the original design).  Works for any input-group user.
/// Returns true only if the daemon acknowledged the config.
bool daemon_ipc_push_config(const std::string& json) {
    std::string req = "set_config " + std::to_string(json.size()) + "\n" + json;
    return daemon_ipc_send_raw(req, 5000)
        .find("\"ok\":true") != std::string::npos;
}

/// True when the process at /proc/<pid> is genuinely the rawaccel daemon.
/// Guards against stale PID files whose PID was recycled by an unrelated
/// process (BUG-07) AND against the R5-S-1 comm-spoofing vector: /proc/<pid>/comm
/// can be faked by any process with prctl(PR_SET_NAME), but /proc/<pid>/exe is a
/// kernel-maintained symlink to the actual executable — it cannot be redirected.
///
/// The /proc/<pid>/exe readlink check is best-effort: on a per-user daemon (or
/// hidepid=0 setups) it reads fine; against a root daemon on a restricted /proc
/// it can return EACCES.  In that case we fall back to the comm check alone so a
/// permission restriction never silently kills daemon detection — the attack
/// surface is still reduced wherever the kernel lets us verify the real binary.
static bool pid_is_rawaccel_daemon(pid_t pid) {
    char proc_root[64];
    snprintf(proc_root, sizeof(proc_root), "/proc/%d", (int)pid);

    // 1) Kernel-maintained binary link — strongest identity check.
    {
        char link[96];
        snprintf(link, sizeof(link), "%s/exe", proc_root);
        char exe[4096];
        ssize_t n = readlink(link, exe, sizeof(exe) - 1);
        if (n > 0) {
            exe[n] = '\0';
            const char* base = strrchr(exe, '/');
            base = base ? base + 1 : exe;
            if (strcmp(base, "rawaccel-daemon") != 0) return false;
        } else if (errno == ENOENT || errno == ESRCH) {
            return false; // process gone
        }
        // else EACCES/EPERM (restricted /proc): fall through to the comm check.
    }

    // 2) /proc/<pid>/comm must also name the daemon.
    char comm_path[96];
    snprintf(comm_path, sizeof(comm_path), "%s/comm", proc_root);
    FILE* cf = fopen(comm_path, "r");
    if (!cf) return false; // no such process (or no permission)
    char comm[64] = {};
    // Zero-init guarantees a NUL terminator even if fgets yields nothing.
    (void)!fgets(comm, sizeof(comm), cf);
    fclose(cf);
    size_t len = strlen(comm);
    if (len > 0 && comm[len-1] == '\n') comm[len-1] = '\0';
    return strcmp(comm, "rawaccel-daemon") == 0;
}

pid_t read_daemon_pid() {
    // 1. Try PID files first (fastest)
    // N1: daemon prefers $XDG_RUNTIME_DIR/rawaccel.pid — must check it here too.
    std::vector<std::string> pid_paths;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0') pid_paths.push_back(std::string(xdg) + "/rawaccel.pid");
    pid_paths.push_back("/run/rawaccel.pid");
    pid_paths.push_back("/tmp/rawaccel.pid");

    for (auto& path : pid_paths) {
        FILE* fp = fopen(path.c_str(), "r");
        if (!fp) continue;
        pid_t pid = 0;
        // BUG-1 fix: strtol + range check instead of fscanf("%d") to avoid
        // UB when the PID value exceeds INT_MAX (C11 §7.21.6.2).
        char buf[32] = {};
        if (fgets(buf, sizeof(buf), fp)) {
            errno = 0;
            char* end = nullptr;
            long v = strtol(buf, &end, 10);
            if (end != buf && errno == 0 && v > 0 && v <= INT_MAX)
                pid = static_cast<pid_t>(v);
        }
        fclose(fp);
        if (pid > 0 && pid_is_rawaccel_daemon(pid)) return pid;
        // BUG-07: stale PID file (PID recycled, or a dead daemon left it
        // behind) — remove it so future lookups never signal the wrong
        // process and the /proc fallback below gets a clean slate.
        if (pid > 0) unlink(path.c_str());
    }

    // 2. Fallback: scan /proc for rawaccel-daemon process name
    //    Handles the case where daemon runs but PID file write failed (e.g. /run not writable).
    DIR* proc = opendir("/proc");
    if (proc) {
        struct dirent* ent;
        while ((ent = readdir(proc)) != nullptr) {
            // Only numeric entries are PIDs
            bool is_num = true;
            for (const char* p = ent->d_name; *p; p++)
                if (*p < '0' || *p > '9') { is_num = false; break; }
            if (!is_num) continue;

            // BUG-6: atoi(d_name) is UB if the directory name doesn't fit in
            // `int`.  /proc only exposes numeric PIDs (pid_t, typically
            // 4194304 max) but be defensive — strtol + range check first, then
            // delegate to pid_is_rawaccel_daemon() (identical identity logic to
            // the PID-file path, including the /proc/<pid>/exe anti-spoof check).
            errno = 0;
            char* end = nullptr;
            long v = strtol(ent->d_name, &end, 10);
            if (end != ent->d_name && errno == 0 && v > 0 && v <= INT_MAX) {
                pid_t pid = static_cast<pid_t>(v);
                if (pid_is_rawaccel_daemon(pid)) {
                    closedir(proc);
                    return pid;
                }
            }
        }
        closedir(proc);
    }
    return 0;
}

bool daemon_running() {
    // Fast path: try IPC ping first (socket exists only while daemon is running).
    // Skip the ping entirely when no socket file is present (BUG-10 — no query).
    if (daemon_socket_exists()) {
        std::string pong = daemon_ipc_query("ping");
        if (!pong.empty() && pong.find("pong") != std::string::npos) return true;
    }
    // Fallback: check PID file / /proc scan
    return read_daemon_pid() > 0;
}

// ── Per-device status JSON selection (BUG-02) ────────────────────────────────
// The daemon's status_json() emits one object per opened mouse under
// "devices":[...].  The GUI previously read the FIRST occurrence of a given
// key in the whole response, which silently bound every readout to the first
// (arbitrarily opened) device — wrong DPI/battery/telemetry whenever the active
// profile targets a different mouse.  These helpers slice out the device whose
// "device_id" matches the active profile, falling back to the first device in
// the array, so all per-device readouts agree with what the active profile
// actually drives.

/// Skip one JSON string literal at text[pos] == '"' (handles \" and passes
/// back pos past the closing quote). Returns std::string::npos on malformed.
static size_t json_skip_string(const std::string& text, size_t pos) {
    if (pos >= text.size() || text[pos] != '"') return std::string::npos;
    for (size_t i = pos + 1; i < text.size(); i++) {
        if (text[i] == '\\') { i++; continue; }
        if (text[i] == '"')  return i + 1;
    }
    return std::string::npos;
}

/// Given an opening '{' at text[start], return the offset one past the matching
/// '}' — tolerating nested objects and quoted strings. std::string::npos on
/// unbalanced input.
static size_t json_object_end(const std::string& text, size_t start) {
    if (start >= text.size() || text[start] != '{') return std::string::npos;
    int depth = 0;
    for (size_t i = start; i < text.size(); i++) {
        char c = text[i];
        if (c == '"') { i = json_skip_string(text, i); if (i == std::string::npos) return std::string::npos; i--; continue; }
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) return i + 1;
            if (depth < 0)  return std::string::npos;
        }
    }
    return std::string::npos;
}

/// String value of the "key" field inside a single JSON object {…} fragment.
/// Returns empty string if absent/malformed.
static std::string json_string_field(const std::string& obj, const char* key) {
    std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return {};
    pos = obj.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = obj.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos || obj[pos] != '"') return {};
    size_t e = json_skip_string(obj, pos);
    if (e == std::string::npos) return {};
    return obj.substr(pos + 1, e - pos - 2);
}

/// True if the object fragment contains a "key" member.
static bool json_has_field(const std::string& obj, const char* key) {
    return obj.find("\"" + std::string(key) + "\"") != std::string::npos;
}

/// device_id of the profile the GUI currently edits / the daemon applies.
/// config.active_profile is a profile NAME; we resolve it to the profile's
/// device_id (empty string = "all devices" catch-all).
static std::string active_profile_device_id(AppState* S) {
    for (const auto& p : S->config.profiles)
        if (p.name == S->config.active_profile) return p.device_id;
    return {};
}

/// Slice of the status response JSON for the device the active profile targets.
/// Selection order (all within "devices":[…]):
///   1. the first object whose device_id equals the active profile's;
///   2. the first object exposing telem_in_ips (a live telemetry source);
///   3. the first object in the array.
/// Returns the raw "{…}" fragment, or an empty string when there is no device
/// data.
static std::string daemon_device_slice(const std::string& resp, AppState* S) {
    std::string needle = "\"devices\"";
    size_t arr = resp.find(needle);
    if (arr == std::string::npos) return {};
    arr = resp.find('[', arr + needle.size());
    if (arr == std::string::npos) return {};

    std::string want = active_profile_device_id(S);
    std::string first, live, match;
    size_t i = arr + 1;
    while (i < resp.size()) {
        size_t obj_start = resp.find('{', i);
        if (obj_start == std::string::npos) break;
        size_t obj_end = json_object_end(resp, obj_start);
        if (obj_end == std::string::npos) break;
        std::string obj = resp.substr(obj_start, obj_end - obj_start);
        if (first.empty()) first = obj;
        if (!want.empty() && json_string_field(obj, "device_id") == want && match.empty())
            match = obj;
        if (live.empty() && json_has_field(obj, "telem_in_ips"))
            live = obj;
        if (!match.empty()) break;
        i = obj_end;
    }
    return !match.empty() ? match : !live.empty() ? live : first;
}

/// Numeric value of `key` inside the device slice the active profile targets.
/// Returns -1 when the device slice is absent, the key is missing, or the value
/// is not a finite number.
double daemon_device_field(const std::string& resp, AppState* S, const char* key) {
    std::string slice = daemon_device_slice(resp, S);
    if (slice.empty()) return -1;
    std::string needle = "\"" + std::string(key) + "\":";
    size_t pos = slice.find(needle);
    if (pos == std::string::npos) return -1;
    size_t start = slice.find_first_not_of(" \t", pos + needle.size());
    size_t end = slice.find_first_of(",}", start);
    if (end == std::string::npos || end <= start) return -1;
    std::string val = slice.substr(start, end - start);
    errno = 0;
    char* e = nullptr;
    double v = std::strtod(val.c_str(), &e);
    if (e == val.c_str() || errno != 0 || !std::isfinite(v)) return -1;
    return v;
}

void update_daemon_status(AppState* S) {
    bool running = daemon_running();
    if (running) {
        gtk_label_set_markup(GTK_LABEL(S->daemon_status),
            tr("<span foreground='#40c040'>● Daemon running</span>"));
    } else {
        gtk_label_set_markup(GTK_LABEL(S->daemon_status),
            tr("<span foreground='#c04040'>● Daemon stopped</span>"));
    }
    // Update button sensitivity based on whether the daemon is running
    if (S->apply_btn)        gtk_widget_set_sensitive(S->apply_btn,        running);
    if (S->daemon_start_btn) gtk_widget_set_sensitive(S->daemon_start_btn, !running);
    if (S->daemon_stop_btn)  gtk_widget_set_sensitive(S->daemon_stop_btn,  running);
    if (S->daemon_reload_btn)gtk_widget_set_sensitive(S->daemon_reload_btn,running);

    // Query and display battery level from daemon — but ONLY when the daemon is
    // actually reachable (BUG-10): with the daemon down this previously fired a
    // pointless status IPC every refresh and showed "Battery: unknown" as if a
    // battery were attached.  The value comes from the CURRENT PROFILE's device
    // slice, not the first device in the array (BUG-02).
    if (S->battery_detected_lbl) {
        if (running) {
            std::string resp = daemon_ipc_query("status");
            int battery = (int)daemon_device_field(resp, S, "detected_battery");
            if (battery >= 0 && battery <= 100) {
                gtk_widget_set_visible(GTK_WIDGET(S->battery_detected_lbl), TRUE);
                S->hw_daemon_battery = battery; // P169: feed the HID++ caps row
                if (battery <= 20) {
                    // Low battery warning
                    gtk_label_set_markup(GTK_LABEL(S->battery_detected_lbl),
                        trf("<b><span foreground='red'>Battery: %d%% (Low!)</span></b>", battery).c_str());
                } else {
                    gtk_label_set_markup(GTK_LABEL(S->battery_detected_lbl),
                        trf("<b>Battery: %d%%</b>", battery).c_str());
                }
            } else {
                // A batteryless/wired mouse, a transient read failure, or a
                // daemon with no device open yet must NOT advertise a phantom
                // "Battery: unknown" — that is exactly the misleading readout
                // BUG-10 was meant to kill for the daemon-down case, and it
                // leaked here for every batteryless mouse with the daemon up.
                // Hide the label; the value is re-queried on the next 3 s tick.
                gtk_widget_set_visible(GTK_WIDGET(S->battery_detected_lbl), FALSE);
                S->hw_daemon_battery = -1;
            }
        } else {
            // Daemon down: no status to show — hide the battery row (BUG-10)
            // instead of leaving a stale/misleading "unknown" (and skip the IPC).
            gtk_widget_set_visible(GTK_WIDGET(S->battery_detected_lbl), FALSE);
            S->hw_daemon_battery = -1;
        }
        // P169 — push the daemon battery feed into the HID++ capability row
        // (forward-declared in app_state.hpp; defined in hidpp_panel.inl).
        if (S->hw_dev_combo && S->hw_caps_lbl) {
            const int idx = (int)gtk_drop_down_get_selected(
                GTK_DROP_DOWN(S->hw_dev_combo));
            if (idx >= 0 && idx < (int)S->hidpp_devs.size())
                hw_render_caps(S, idx);
        }
    }
}

gboolean poll_daemon_status(gpointer user_data) {
    update_daemon_status(static_cast<AppState*>(user_data));
    return G_SOURCE_CONTINUE;
}

/// Send a signal to the daemon.  Returns true on success.
/// On EPERM (user not in input group / daemon owned by different user) returns
/// false and sets a human-readable error in *err_out if non-null.
bool daemon_send_signal(int sig, std::string* err_out) {
    pid_t pid = read_daemon_pid();
    if (pid <= 0) {
        if (err_out) *err_out = tr("Daemon is not running.");
        return false;
    }
    if (kill(pid, sig) == 0) return true;

    // kill() failed — build a useful message
    if (err_out) {
        if (errno == EPERM) {
            *err_out =
                tr("Permission denied (EPERM) — cannot signal the daemon.\n"
                   "Fix: ensure you are in the 'input' group:\n"
                   "  sudo usermod -aG input $USER  (then log out and back in)\n"
                   "Or restart the daemon from the GUI using pkexec.");
        } else {
            *err_out = std::string(tr(" kill() failed: ")) + strerror(errno);
        }
    }
    return false;
}
