// E2E daemon harness — runs the REAL rawaccel-daemon against a synthetic
// uinput mouse source and validates the output byte stream on a virtual sink.
//
//   Built by tests/run_e2e.sh; requires root (or the input group) and /dev/uinput.
//
//   Phases:
//     accel  → daemon applies classic gain: verify frame structure (one SYN per
//              input frame), SM-2 button buffering, LOW-1 coalesced deferral.
//     raw    → daemon forwards 1:1: verify byte-identical event stream.
//
// Exit: 0 = all checks passed, 1 = a check failed, 77 = environment not
// usable (no /dev/uinput, daemon could not grab, no output device appeared).
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <glob.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// ── tiny result accounting ────────────────────────────────────────────────────
static int g_checks = 0;
static bool g_ok = true;

static void report(bool pass, const char* name, const std::string& detail) {
    ++g_checks;
    g_ok = g_ok && pass;
    printf("[%s] %s%s%s\n", pass ? "PASS" : "FAIL", name,
           detail.empty() ? "" : " — ", detail.c_str());
    fflush(stdout);
}

// ── helpers ──────────────────────────────────────────────────────────────────
static std::string find_node_by_name(const std::string& name_contains) {
    glob_t g{};
    if (glob("/dev/input/event*", 0, nullptr, &g) != 0) { globfree(&g); return {}; }
    std::string found;
    for (size_t i = 0; i < g.gl_pathc; ++i) {
        int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        struct libevdev* e = nullptr;
        if (libevdev_new_from_fd(fd, &e) == 0) {
            const char* name = libevdev_get_name(e);
            if (name && std::string(name).find(name_contains) != std::string::npos)
                found = g.gl_pathv[i];
            libevdev_free(e);
        }
        close(fd);
    }
    globfree(&g);
    return found;
}

static struct libevdev_uinput* create_source(const std::string& node_name) {
    struct libevdev* dev = libevdev_new();
    libevdev_set_name(dev, node_name.c_str());
    libevdev_enable_event_type(dev, EV_REL);
    libevdev_enable_event_code(dev, EV_REL, REL_X, nullptr);
    libevdev_enable_event_code(dev, EV_REL, REL_Y, nullptr);
    libevdev_enable_event_code(dev, EV_REL, REL_WHEEL, nullptr);
    libevdev_enable_event_type(dev, EV_KEY);
    libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, nullptr);
    libevdev_enable_event_code(dev, EV_KEY, BTN_RIGHT, nullptr);
    libevdev_enable_event_code(dev, EV_KEY, BTN_MIDDLE, nullptr);
    libevdev_enable_event_code(dev, EV_SYN, SYN_REPORT, nullptr);
    struct libevdev_uinput* u = nullptr;
    int err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &u);
    libevdev_free(dev);
    if (err != 0) { fprintf(stderr, "uinput create failed: %s\n", strerror(-err)); return nullptr; }
    // Wait for the kernel udev node to settle before the daemon is started.
    for (int i = 0; i < 20; ++i) {
        if (!find_node_by_name(node_name).empty()) break;
        usleep(100000);
    }
    return u;
}

// Spawn the daemon in the foreground, redirect its output to a log file.
static pid_t spawn_daemon(const char* daemon_path, const std::string& config_path,
                          const std::string& log_path) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
        // E2E-PID: run with a private runtime dir so the test daemon's PID file
        // and IPC socket land in the harness workdir instead of /run.  The PID
        // liveness gate (PID-1/PID-3) would otherwise refuse to start while the
        // paused system daemon still owns /run/rawaccel.pid — and the private
        // socket keeps the two daemons' IPC from colliding.
        std::string rd = config_path; // <workdir>/cfg.json → strip basename
        const size_t slash = rd.find_last_of('/');
        if (slash != std::string::npos) rd.resize(slash);
        setenv("XDG_RUNTIME_DIR", rd.c_str(), 1);
        execl(daemon_path, daemon_path, "-v", "-c", config_path.c_str(), (char*)nullptr);
        _exit(127);
    }
    return pid;
}

static bool wait_for_output_device(const std::string& prefix, int timeout_ms,
                                   std::string& out_node) {
    const std::string needle = prefix + " Mouse (RawAccel)";
    const int steps = timeout_ms / 100;
    for (int i = 0; i < steps; ++i) {
        std::string n = find_node_by_name(needle);
        if (!n.empty()) { out_node = n; return true; }
        usleep(100000);
    }
    return false;
}

// ── event capture ────────────────────────────────────────────────────────────
struct out_event {
    uint16_t type, code;
    int32_t  value;
    uint64_t time_us; // ev.time seconds*1e6 + usec (kernel frame timestamp)
};

// Read from the daemon's output node for up to timeout_ms, collecting frames.
// A "frame" ends at SYN_REPORT.  Returns true if calls did not error out.
static bool read_output_events(struct libevdev* e, int timeout_ms,
                               std::vector<std::vector<out_event>>& frames,
                               std::vector<std::pair<int,int>>& rel_total) {
    const int fd = libevdev_get_fd(e);
    const long deadline_ms = timeout_ms;
    long waited_ms = 0;
    std::vector<out_event> cur;
    int64_t cur_rx = 0, cur_ry = 0;
    frames.clear();
    rel_total.clear();
    for (;;) {
        struct pollfd pfd{ fd, POLLIN, 0 };
        int pr = poll(&pfd, 1, waited_ms >= deadline_ms ? 0 : deadline_ms - waited_ms);
        if (pr == 0) break;                      // no more activity
        if (pr < 0) { if (errno == EINTR) continue; return false; }
        struct input_event ev;
        int rc;
        while ((rc = libevdev_next_event(e, LIBEVDEV_READ_FLAG_NORMAL, &ev)) == 0) {
            if (ev.type == EV_REL && ev.code == REL_X) cur_rx += ev.value;
            if (ev.type == EV_REL && ev.code == REL_Y) cur_ry += ev.value;
            out_event o{ ev.type, ev.code, ev.value,
                         static_cast<uint64_t>(ev.time.tv_sec) * 1000000ULL +
                         static_cast<uint64_t>(ev.time.tv_usec) };
            cur.push_back(o);
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                frames.push_back(cur);
                rel_total.push_back({ (int)cur_rx, (int)cur_ry });
                cur.clear(); cur_rx = cur_ry = 0;
            }
        }
        if (rc < 0 && rc != -EAGAIN) return false;
        waited_ms += 20;
    }
    return true;
}

static void send_events(struct libevdev_uinput* src,
                        const std::vector<std::tuple<uint16_t,uint16_t,int32_t>>& evs) {
    for (auto& [t, c, v] : evs)
        libevdev_uinput_write_event(src, t, c, v);
}

// ── config generation ────────────────────────────────────────────────────────
// Classic LINEAR path (exponent_classic <= 1) has a speed-independent constant
// gain = 1 + acceleration (oracle-locked "linear path"); with acceleration=2.0
// every frame is scaled by exactly ×3 regardless of how the daemon measures
// dtime, so the asserts below are deterministic.  gain is a BOOLEAN in the
// JSON schema (config.cpp rejects numbers silently).
static std::string config_json(const std::string& device_id, bool raw) {
    auto accel = [&](double a, double scale) -> std::string {
        std::ostringstream j;
        j << "{\"mode\":\"classic\",\"gain\":true"
          << ",\"input_offset\":0.0,\"output_offset\":0.0,\"acceleration\":" << a
          << ",\"decay_rate\":1.0,\"gamma\":1.0,\"motivity\":1.0"
          << ",\"exponent_classic\":1.0,\"scale\":" << scale
          << ",\"exponent_power\":2.0,\"limit\":0.0,\"sync_speed\":100.0"
          << ",\"smooth\":0.0,\"cap\":[0.0,0.0],\"cap_mode\":\"out\"}";
        return j.str();
    };
    std::ostringstream j;
    j << "{\"active_profile\":\"E2E\",\"use_raw_input\":true,\"profiles\":["
      << "{\"name\":\"E2E\",\"device_id\":\"" << device_id
      << "\",\"dpi\":800,\"polling_rate\":1000,\"disable\":false,\"profile\":{"
      << "\"name\":\"E2E\",\"raw_passthrough\":" << (raw ? "true" : "false")
      << ",\"domain_weights\":[1.0,1.0],\"range_weights\":[1.0,1.0]"
      << ",\"accel_x\":" << accel(raw ? 0.0 : 2.0, raw ? 1.0 : 60.0)
      << ",\"accel_y\":" << accel(raw ? 0.0 : 2.0, raw ? 1.0 : 60.0)
      << ",\"output_dpi\":" << (raw ? 0 : 800) << ",\"yx_output_dpi_ratio\":1.0"
      << ",\"degrees_rotation\":0.0,\"degrees_snap\":0.0"
      << ",\"speed_min\":0.0,\"speed_max\":0.0"
      << ",\"lr_output_dpi_ratio\":1.0,\"ud_output_dpi_ratio\":1.0"
      << ",\"speed_processor\":{\"whole\":true,\"lp_norm\":2.0,"
      << "\"input_speed_smooth_halflife\":0.0,\"scale_smooth_halflife\":0.0,"
      << "\"output_speed_smooth_halflife\":0.0}}}]}";
    return j.str();
}

// ── phase implementations ────────────────────────────────────────────────────
static int g_daemon_pid = -1;
static pid_t g_sys_daemon = -1;

static void cleanup() {
    if (g_daemon_pid > 0) { kill(g_daemon_pid, SIGTERM); waitpid(g_daemon_pid, nullptr, 0); g_daemon_pid = -1; }
    if (g_sys_daemon > 0) { // resume the system daemon if we paused it
        kill(g_sys_daemon, SIGCONT);
        g_sys_daemon = -1;
    }
}

static int run_checks(const char* daemon_path, const std::string& prefix,
                      bool raw, const std::string& workdir) {
    struct libevdev_uinput* src = create_source(prefix + " Mouse");
    if (!src) return 77;

    // Wait and find the source node (device_id used by the daemon's profile).
    std::string source_node = find_node_by_name(prefix + " Mouse");
    if (source_node.empty()) {
        printf("source node not found\n");
        libevdev_uinput_destroy(src);
        return 77;
    }

    const std::string cfg_path = workdir + "/cfg.json";
    { std::ofstream f(cfg_path); f << config_json(source_node, raw); }

    const std::string log_path = workdir + "/daemon.log";
    g_daemon_pid = spawn_daemon(daemon_path, cfg_path, log_path);
    if (g_daemon_pid <= 0) { libevdev_uinput_destroy(src); return 77; }

    std::string out_node;
    if (!wait_for_output_device(prefix, 5000, out_node)) {
        report(false, "daemon output device appeared", "(check daemon.log)");
        cleanup();
        libevdev_uinput_destroy(src);
        return 1;
    }
    report(true, "daemon grabbed source + created output", out_node);

    int out_fd = open(out_node.c_str(), O_RDONLY | O_NONBLOCK);
    if (out_fd < 0) { report(false, "open output device", strerror(errno)); cleanup(); libevdev_uinput_destroy(src); return 1; }
    struct libevdev* out = nullptr;
    if (libevdev_new_from_fd(out_fd, &out) < 0) { close(out_fd); cleanup(); libevdev_uinput_destroy(src); return 1; }

    // Feeding helpers ---------------------------------------------------------
    std::vector<std::vector<out_event>> frames;
    std::vector<std::pair<int,int>> rel_totals;

    if (!raw) {
        // T-A1: single motion frame, one SYN, classic linear gain = 1+accel = ×3.
        // Input (20,10) → exactly (60,30) — speed independent, deterministic.
        send_events(src, { {EV_REL, REL_X, 20}, {EV_REL, REL_Y, 10},
                           {EV_SYN, SYN_REPORT, 0} });
        if (!read_output_events(out, 400, frames, rel_totals)) { report(false, "T-A1 read", "read error"); }
        bool syn_ok = frames.size() == 1;
        bool amp_ok = false;
        if (!frames.empty()) {
            int rx = rel_totals[0].first, ry = rel_totals[0].second;
            amp_ok = rx == 60 && ry == 30; // ×3.0 classic linear gain
        }
        report(syn_ok && amp_ok, "T-A1 accel motion frame (1 SYN, ×3 linear gain)", "");
        if (!syn_ok || !amp_ok) {
            printf("       output: %zu frames, rel=(%d,%d), expect (60,30)\n", frames.size(),
                   frames.empty() ? 0 : rel_totals[0].first,
                   frames.empty() ? 0 : rel_totals[0].second);
        }

        // T-A2: button between REL and SYN is buffered into the SAME frame
        // (SM-2) — one SYN, button present, order [REL, BTN, SYN].
        send_events(src, { {EV_REL, REL_X, 10}, {EV_KEY, BTN_LEFT, 1},
                           {EV_REL, REL_Y, 5}, {EV_SYN, SYN_REPORT, 0} });
        frames.clear(); rel_totals.clear();
        if (!read_output_events(out, 400, frames, rel_totals)) { report(false, "T-A2 read", "read error"); }
        bool t2ok = false;
        if (frames.size() == 1) {
            bool saw_rel = false, saw_btn = false, btn_after_rel = false;
            for (auto& ev : frames[0]) {
                if (ev.type == EV_REL) { saw_rel = true; }
                if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 1) {
                    saw_btn = true;
                    btn_after_rel = saw_rel;
                }
            }
            t2ok = saw_rel && saw_btn && btn_after_rel;
        }
        report(t2ok, "T-A2 mid-frame button buffered (SM-2, single SYN)", "");
        if (!t2ok) printf("       frames=%zu\n", frames.size());

        // T-A3: button-only frame (no motion) — still one SYN, no ghost motion.
        send_events(src, { {EV_KEY, BTN_RIGHT, 1}, {EV_SYN, SYN_REPORT, 0} });
        frames.clear(); rel_totals.clear();
        if (!read_output_events(out, 400, frames, rel_totals)) { report(false, "T-A3 read", "read error"); }
        bool t3ok = frames.size() == 1;
        if (t3ok) {
            bool any_rel = false;
            for (auto& ev : frames[0]) if (ev.type == EV_REL) { any_rel = true; break; }
            bool has_btn = false;
            for (auto& ev : frames[0]) if (ev.type == EV_KEY && ev.code == BTN_RIGHT) has_btn = true;
            t3ok = has_btn && !any_rel;
        }
        report(t3ok, "T-A3 button-only frame (no motion, single SYN)", "");

        // T-A4: LOW-1 coalesced second frame that arrives WITHOUT a trailing
        // SYN in one write; the deferred frame must flush at the *next* real
        // SYN in a separate frame, never with a synthetic/double SYN.
        send_events(src, { {EV_REL, REL_X, 6}, {EV_REL, REL_Y, 4},
                           {EV_SYN, SYN_REPORT, 0} });
        usleep(80000); // let the daemon process frame 1
        send_events(src, { {EV_REL, REL_X, 2} }); // half-frame: no SYN sent yet
        usleep(80000); // daemon sees the +2 with no SYN (deferred, LOW-1)
        send_events(src, { {EV_SYN, SYN_REPORT, 0} }); // real closing SYN
        frames.clear(); rel_totals.clear();
        if (!read_output_events(out, 700, frames, rel_totals)) { report(false, "T-A4 read", "read error"); }
        // Expect exactly 2 output frames: f1 = (6,4) amplified, f2 = (+2) amplified.
        bool t4ok = frames.size() == 2 && rel_totals[1].first > 0;
        report(t4ok, "T-A4 coalesced frame deferred, no double-SYN (LOW-1)", "");
        if (!t4ok) {
            printf("       output frames=%zu rel=[", frames.size());
            for (auto& r : rel_totals) printf("(%d,%d) ", r.first, r.second);
            printf("]\n");
        }
    } else {
        // T-B1: raw 1:1 — output stream must be byte-identical to input.
        send_events(src, { {EV_REL, REL_X, 7}, {EV_REL, REL_Y, 5},
                           {EV_KEY, BTN_LEFT, 1}, {EV_SYN, SYN_REPORT, 0} });
        frames.clear(); rel_totals.clear();
        if (!read_output_events(out, 400, frames, rel_totals)) { report(false, "T-B1 read", "read error"); }
        bool t1ok = frames.size() == 1 && rel_totals[0].first == 7 && rel_totals[0].second == 5;
        if (t1ok) {
            bool btn = false;
            for (auto& ev : frames[0])
                if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 1) btn = true;
            t1ok = btn;
        }
        report(t1ok, "T-B1 raw passthrough byte-identical (1:1)", "");
        if (!t1ok) {
            printf("       output: %zu frames rel=(%d,%d)\n", frames.size(),
                   frames.empty() ? 0 : rel_totals[0].first,
                   frames.empty() ? 0 : rel_totals[0].second);
        }
        printf("       (output dpi=0 ⇒ rel values must equal the input 7/5)\n");
    }

    libevdev_free(out);
    close(out_fd);
    libevdev_uinput_destroy(src);
    cleanup();
    return 0;
}

int main(int argc, char** argv) {
    const char* daemon_path = nullptr;
    std::string phase;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--daemon" && i + 1 < argc) daemon_path = argv[++i];
        else if (std::string(argv[i]) == "--phase" && i + 1 < argc) phase = argv[++i];
    }
    if (!daemon_path || (phase != "accel" && phase != "raw")) {
        fprintf(stderr, "usage: %s --daemon <daemon-binary> --phase accel|raw\n", argv[0]);
        return 2;
    }

    if (access("/dev/uinput", W_OK) != 0) {
        fprintf(stderr, "/dev/uinput not writable — run as root/input group\n");
        return 77;
    }

    const std::string prefix = "RAE2E-" + std::to_string(getpid());
    const std::string workdir = "/tmp/rawe2e-" + std::to_string(getpid());
    mkdir(workdir.c_str(), 0700);

    int rc = run_checks(daemon_path, prefix, phase == "raw", workdir);
    if (g_ok && rc == 0) printf("%s phase: ALL PASS\n", phase.c_str());
    else printf("%s phase: FAILURES\n", phase.c_str());
    printf("  checks run: %d\n", g_checks);
    rmdir(workdir.c_str());
    return (rc != 0) ? rc : (g_ok ? 0 : 1);
}