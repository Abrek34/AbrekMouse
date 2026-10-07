#pragma once
// ── Common includes for all GUI translation units ─────────────────────────────
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <cairo.h>
#include <cmath>
#include <limits>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstdint>   // std::int64_t (STALE-1 config mtime)
#include <algorithm>
#include <filesystem>
#include <functional>
#include <pwd.h>
#include <initializer_list>

// Forward declaration for AppState::hw_pending_apply (defined in
// gui/hidpp_panel.inl).
struct HwApplyTask;

#include "../include/rawaccel.hpp"
#include "../include/config.hpp"
#include "../include/logitech_hidpp.hpp"
#include "../include/logitech_quirks.hpp"

using namespace rawaccel;
namespace fs = std::filesystem;

// ── Constants (dark theme colours) ───────────────────────────────────────────
static const double C_BG[3]    = {0.11, 0.11, 0.13};
static const double C_GRID[4]  = {0.25, 0.25, 0.28, 0.5};
static const double C_CURVE[3] = {0.18, 0.72, 1.00};
static const double C_CURVE2[3]= {1.00, 0.55, 0.18};
static const double C_REF[4]   = {0.45, 0.45, 0.48, 0.6};
static const double C_DOT[3]   = {1.00, 0.30, 0.30};
static const double C_TEXT[3]  = {0.80, 0.80, 0.85};

// Graph margins (shared between graph.cpp and ui_builder.cpp)
static constexpr double GRAPH_ML = 58, GRAPH_MR = 16, GRAPH_MT = 16, GRAPH_MB = 44;

// ── Mouse device info ─────────────────────────────────────────────────────────
struct InputDeviceInfo {
    std::string name;
    std::string event_node;
    std::string uniq;
    std::string stable_id;   // "usb:VVVV:PPPP:serial" if vendor+product, else uniq/resolved event_node (daemon parity)
    uint16_t    vendor  = 0;
    uint16_t    product = 0;
    bool        has_rel_xy  = false;
    bool        is_rawaccel = false;

    bool operator==(const InputDeviceInfo& o) const {
        return event_node == o.event_node && name == o.name && stable_id == o.stable_id;
    }
};

// ── Application state ─────────────────────────────────────────────────────────

// STALE-1 (AJ1, 1 Oct 2026): "unreadable" sentinel for a config-file mtime.
//
// ⭐ WHY NOT -1, AND WHY NOT A SIGN TEST.  `fs::file_time_type` is NOT
// guaranteed to be signed-positive, and on this platform it is not: measured
// `fs::last_write_time()` on a live file returns
//     -4646858292110006316
// (libstdc++'s file_clock epoch sits AFTER the represented times, so every
// real mtime is negative).  A first version of the STALE-1 guard tested
// `stamp >= 0` to mean "the read succeeded", which on Linux is FALSE FOR EVERY
// REAL FILE — so the whole stale-config guard was dead code: the
// `config_mtime_loaded >= 0` precondition never held, the guard was never
// entered, and the silent-clobber behaviour it was written to prevent still
// happened.  Found by the mutation proof in `olcum/aj1/stale1_proof.cpp`, which
// printed two DIFFERENT stamps and then still reported "mtime aynı".
//
// The sentinel is therefore a value no real mtime can equal, and validity is
// decided by COMPARING TO IT — never by the sign.  `std::numeric_limits<…>::min()`
// is used because `last_write_time().count()` is a signed 64-bit tick count and
// a real mtime at any plausible tick magnitude cannot reach it.
constexpr std::int64_t CONFIG_STAMP_UNREADABLE = std::numeric_limits<std::int64_t>::min();

struct AppState {
    app_config  config;
    std::string config_path;
    std::string lang_path;          // <config_dir>/gui_lang — language preference
    int         lang_override = -1; // -1 = auto (locale), 0 = English, 1 = Türkçe
    int         current_profile_idx = 0;
    bool        xy_linked  = true;
    bool        updating   = false;
    bool        unsaved    = false;

    // STALE-1 (AJ1, 30 Sep 2026): the config file is read ONCE at startup
    // (`state.config = load_config(...)`, gui/main.cpp), and `save_config_now`
    // writes `S->config` back in FULL.  Any change made elsewhere in the
    // meantime — `rawaccel-cli set-param`, a second GUI, a root daemon that
    // persisted an IPC `set_config` to /etc — is therefore SILENTLY DISCARDED
    // on the next GUI save, with nothing in any log.  Measured live: a CLI
    // repair of `mode`/`device_id` was overwritten by a later GUI write, and
    // the GUI has no way to know it is about to do it.
    //
    // This records the mtime observed at load / after each save so
    // save_config_now() can DETECT that the file moved underneath us.  The fix
    // is detect-and-warn, NOT auto-reload: reloading would discard the user's
    // own unsaved widget edits, which is a strictly worse data-loss path.
    std::int64_t config_mtime_loaded = CONFIG_STAMP_UNREADABLE;  // mtime at load / last save

    // Daemon state
    guint       daemon_poll_id = 0;

    // inotify — /dev/input hot-plug monitoring
    int         inotify_fd  = -1;
    int         inotify_wd  = -1;
    guint       inotify_src = 0;

    // hidraw hot-plug monitoring is deliberately separate from the evdev
    // watch above.  HID++ probing runs in worker threads and never touches
    // the motion/daemon hot path.
    int         hidraw_inotify_fd = -1;
    int         hidraw_inotify_wd = -1;
    guint       hidraw_inotify_src = 0;
    guint       hidpp_notify_poll_id = 0;
    // GUI-O4: the pkexec child-watch source is a GLib main-context source that
    // outlives the window (pkexec+systemctl can take seconds).  Its callback
    // touches status-bar widgets, so it must be torn down with the window or a
    // late pkexec exit would write into destroyed widgets.  Tracked so the
    // destroy handler can g_source_remove() it.
    guint       pkexec_watch_id = 0;
    // P-LEAK: the pkexec child-watch user-data (pkexec_watch_ctx*) + child pid
    // are tracked so a replaced or window-destroyed watch can free the context
    // and prompt-reap the abandoned pkexec child instead of leaking both.
    GPid        pkexec_watch_pid  = 0;
    void*       pkexec_watch_ctx  = nullptr;

    // Graph interaction
    double graph_zoom     = 1.0;
    double graph_pan_x    = 0.0;
    bool   graph_drag     = false;
    double drag_pan_start = 0.0;
    double drag_zoom_start = 1.0; // zoom at drag start — pan must not re-read live zoom
    // Cached Y-axis max gain from the last draw — on_graph_motion reuses it
    // instead of re-running the full 200-sample compute_max_gain() on every
    // mouse move (L-BUG-17).  Any graph-visible state change triggers a
    // queue_draw(), so between draws this always matches the rendered frame.
    double graph_last_max_gain = 2.0;

    // KDE / libinput double-acceleration warning
    bool   is_kde          = false;   // true if running under KDE Plasma
    bool   is_wayland      = false;   // true if Wayland session
    bool   kde_accel_ok    = true;    // false = libinput accel NOT disabled → double-accel!
    bool   kde_fix_running = false;   // an async KDE fix worker is in flight
    GtkWidget* kde_warn_bar = nullptr; // infobar shown when kde_accel_ok == false
    // O31-G1: set in the window destroy handler.  The async KDE-fix worker
    // marshals its result back through an idle callback that touches widgets;
    // after destroy those are gone, so the idle callback must bail (it still
    // releases its heap task) instead of writing into unparented widgets.
    bool   window_destroyed = false;

    // Auto-detected device properties (from daemon status_json)
    int    detected_dpi          = 0;  // 0 = unknown
    int    detected_polling_rate = 0;  // 0 = unknown
    int    detected_battery      = -1; // -1 = unknown, 0-100 = percent
    // M-8/R2-04: non-empty when the persisted config could not be parsed at
    // startup.  The offending file is stashed aside (settings.json.corrupt-N);
    // a default profile is loaded and the warning is surfaced in the status
    // bar so the GUI never silently overwrites the corrupt file's contents.
    std::string config_load_warn;
    // Auto-fill hint labels and buttons (Device section)
    GtkWidget* dpi_detected_lbl      = nullptr;
    GtkWidget* polling_detected_lbl  = nullptr;
    GtkWidget* dpi_autofill_btn      = nullptr;
    GtkWidget* polling_autofill_btn  = nullptr;
    GtkWidget* battery_detected_lbl  = nullptr; // battery % label
    GtkWidget* latency_lbl           = nullptr; // read-only latency stats display

    // Mouse lock test window (P104) — popup that locks the pointer inside it
    // (X11 XGrabPointer via gdkx; GDK4 has no grab API, see mouse_test.inl)
    // while showing live speed/gain telemetry (daemon status JSON). ESC, focus
    // loss or window destroy releases the lock.
    GtkWidget* mouse_test_win    = nullptr;
    GtkWidget* test_device_lbl   = nullptr; // L17-3: name of the measured device
    GtkWidget* test_speed_lbl    = nullptr; // live "In (ips)" value
    GtkWidget* test_out_lbl      = nullptr; // live "Out (ips)" value
    GtkWidget* test_gain_lbl     = nullptr; // live "Gain (×)" value
    GtkWidget* test_status_lbl   = nullptr; // daemon-down / awaiting-motion note
    GtkWidget* test_lat_lbl      = nullptr; // live "Latency p50/p95 (µs)" value
    GtkWidget* test_poll_lbl     = nullptr; // live "Poll rate (Hz)" value
    GtkWidget* test_hint_lbl     = nullptr; // lock-tier explanation (persistent)
    GtkWidget* test_name_lbls[6] = {};      // "In/Out/Gain/Latency/Poll/Device:" name labels (Bug-09, L17-3)
    GtkWidget* test_title_lbl    = nullptr; // "Mouse Lock Test" heading (Bug-09)
    int        test_hint_state   = 0;       // hint: 0=Tier-1 locked, 1=Tier-2 confine, 2=Tier-3 n/a
    int        test_status_state = -1;      // status: -1=unset, 0=live, 1=awaiting, 2=daemon down, 3=unfocused
    guint      test_poll_id      = 0;       // 250 ms telemetry poll source
    guint      test_confine_id   = 0;       // warp-confine tick (Tier-2 grab fallback)
    bool       test_grabbed      = false;   // Tier-1 X11 pointer grab active?
    bool       test_x11          = false;   // X11 backend + lock bridge usable

    // ── Widgets ──────────────────────────────────────────────────────────────
    GtkWidget* window            = nullptr;
    GtkWidget* profile_combo     = nullptr;
    GtkWidget* status_bar        = nullptr;
    GtkWidget* daemon_status     = nullptr;
    bool       daemon_prev_running = false; // R8-RESEND: last poll's daemon state (up-transition trigger)
    double     lp_norm_mem = 2.0;           // R9-LPNRM: last real Lp norm (before the Max=9999 sentinel)
    GtkWidget* graph_area        = nullptr;
    GtkWidget* apply_btn         = nullptr;
    GtkWidget* daemon_start_btn  = nullptr;
    GtkWidget* daemon_stop_btn   = nullptr;
    GtkWidget* daemon_reload_btn = nullptr;
    GtkWidget* lang_combo        = nullptr; // header-bar language selector

    // Accel X
    GtkWidget* mode_combo         = nullptr;
    GtkWidget* gain_check         = nullptr;
    GtkWidget* mode_hint_lbl      = nullptr; // one-line "which params apply" note
    GtkWidget* accel_spin         = nullptr;
    GtkWidget* exponent_spin      = nullptr;
    GtkWidget* power_exp_spin     = nullptr;
    GtkWidget* limit_spin         = nullptr;
    GtkWidget* offset_spin        = nullptr;
    GtkWidget* decay_spin         = nullptr;
    GtkWidget* cap_x_spin         = nullptr;
    GtkWidget* cap_y_spin         = nullptr;
    GtkWidget* cap_mode_combo     = nullptr;
    GtkWidget* sync_speed_spin    = nullptr;
    GtkWidget* smooth_spin        = nullptr;
    GtkWidget* motivity_spin      = nullptr;
    GtkWidget* gamma_spin         = nullptr;
    GtkWidget* output_offset_spin = nullptr;
    GtkWidget* scale_spin         = nullptr;
    // Labels of the Accel X params grid rows (index = grid row) — for per-mode show/hide
    GtkWidget* accel_row_label[15] = {};

    // Accel Y
    GtkWidget* xy_link_btn     = nullptr;
    GtkWidget* mode_combo_y    = nullptr;
    GtkWidget* accel_spin_y    = nullptr;
    GtkWidget* exponent_spin_y = nullptr;
    GtkWidget* limit_spin_y    = nullptr;
    GtkWidget* offset_spin_y   = nullptr;
    GtkWidget* cap_y_spin_y    = nullptr;
    GtkWidget* y_axis_frame    = nullptr;
    // Labels of the Y params grid rows (index = grid row) — for per-mode show/hide
    GtkWidget* y_row_label[6] = {};

    // Device
    GtkWidget* dpi_spin        = nullptr;
    GtkWidget* polling_spin    = nullptr;
    GtkWidget* output_dpi_spin = nullptr;
    GtkWidget* lr_ratio_spin   = nullptr;
    GtkWidget* ud_ratio_spin   = nullptr;
    GtkWidget* yx_ratio_spin   = nullptr;

    // ── Logitech HID++ hardware panel (onboard DPI / report rate / LOD) ──
    // Implementation in gui/hidpp_panel.inl.  These widgets write directly
    // to the physical Logitech device via /dev/hidraw* (HID++ 2.0), which
    // is independent of the software profile settings above.
    std::vector<hidpp_device> hidpp_devs;     // live discovery cache
    // R2-08: bumped whenever S->hidpp_devs is replaced by a scan result.
    // Notification/query workers capture the version when they start and the
    // idle callback drops its result if it changed in the meantime, so a stale
    // worker can never paint battery/caps onto a device that replaced the
    // one it probed (same dropdown index, different hardware).
    int        hw_devs_version  = 0;
    GtkWidget* hw_dev_combo     = nullptr;    // device dropdown (/dev/hidrawX)
    GtkWidget* hw_dpi_spin      = nullptr;    // onboard DPI spin
    GtkWidget* hw_rate_combo    = nullptr;    // polling/report rate combo
    GtkWidget* hw_lod_combo     = nullptr;    // lift-off distance combo
    GtkWidget* hw_refresh_btn   = nullptr;    // rescan hidraw devices
    GtkWidget* hw_apply_btn     = nullptr;    // write current widget values
    GtkWidget* hw_status_lbl    = nullptr;    // read-only current/result label
    bool       hw_busy          = false;      // a scan/apply thread is running
    bool       hw_notify_busy   = false;      // bounded notification poll worker
    bool       hw_cancel        = false;      // set on destroy: idle callbacks must bail
    // GUI-Y3: combo index of a device the user selected WHILE hw_busy was set.
    // hw_query_current() used to silently drop such selections, leaving the
    // onboard DPI/rate/LOD widgets showing the previous device's values.  The
    // completion idle callback re-runs the query for this index once free.
    int        hw_pending_query = -1;
    // T48-01: an Apply clicked while another scan/query/apply worker was
    // running used to spawn a second worker on the same hidraw node.  The
    // task is stashed here and started when the in-flight worker's idle
    // callback clears hw_busy.  Deleted on teardown via the same destroy
    // path that sets hw_cancel.
    struct HwApplyTask* hw_pending_apply = nullptr;
    // P169 — battery / capability rows (read-only, updated by the same worker
    // threads that run the scan/query/notification loops).
    GtkWidget* hw_battery_lbl   = nullptr;    // live level/charging/online + family
    GtkWidget* hw_caps_lbl      = nullptr;    // capability summary / unsupported hints
    int        hw_daemon_battery = -1;        // daemon-reported battery for the
                                              // daemon-tracked device (-1 = none)

    // Speed processor
    GtkWidget* dist_mode_combo = nullptr;
    GtkWidget* lp_norm_spin    = nullptr;
    GtkWidget* lp_norm_label   = nullptr;
    GtkWidget* input_hl_spin   = nullptr;
    GtkWidget* scale_hl_spin   = nullptr;
    GtkWidget* output_hl_spin  = nullptr;

    // Raw passthrough
    GtkWidget* raw_check      = nullptr;
    // PAS-1: top-level use_raw_input master switch (daemon grabs no mouse
    // when off).  Bound to AppState::config.use_raw_input, not to a profile.
    GtkWidget* raw_input_check = nullptr;

    // Motion
    GtkWidget* rotation_spin  = nullptr;
    GtkWidget* snap_spin      = nullptr;
    GtkWidget* speed_min_spin = nullptr;
    GtkWidget* speed_max_spin = nullptr;

    // LUT editor
    GtkWidget* lut_frame          = nullptr;
    GtkWidget* accel_params_frame = nullptr;
    GtkWidget* lut_list_box       = nullptr;
    bool       lut_graph_mode     = false;

    // Device assignment
    GtkWidget* device_id_combo = nullptr;
    GtkWidget* match_app_entry = nullptr;  // P-APP: optional focused-app class
    std::vector<InputDeviceInfo> mice_list;
    // P-APP: KWin focus relay internal context (kwin_focus.inl)
    void* kwin_focus_ctx = nullptr;
};

// ── Global app state pointer (defined in main.cpp) ───────────────────────────
// NOTE: G is intentionally only accessible to main.cpp. All other code receives
// AppState* through function parameters or GTK callback gpointer user_data.
// Do NOT add 'extern AppState* G' here — use the AppState* parameter instead.

// ── Shared function declarations ─────────────────────────────────────────────
// All functions take AppState* explicitly; no implicit global access.

// helpers
device_profile& cur_prof(AppState* S);
void set_status(AppState* S, const std::string& msg);
void save_config_now(AppState* S);
/// Returns a warning string if two or more profiles share the same non-empty
/// device_id (first-match-wins in the daemon, so this is likely unintentional).
/// Returns empty string if no collision detected.
std::string check_duplicate_device_ids(const app_config& cfg);

// daemon
pid_t read_daemon_pid();
bool  daemon_running(bool* ipc_ok = nullptr);
bool  daemon_send_signal(int sig, std::string* err_out = nullptr);
int  daemon_ipc_push_config(const std::string& json, std::string* resp_out = nullptr);
void  update_daemon_status(AppState* S);
/// Re-send the last known WM_CLASS to a (re)started daemon (defined in
/// kwin_focus.inl, which is included AFTER daemon_comm.inl).
static void kwin_focus_resend_current(AppState* S);
/// Best-match device JSON slice of a daemon status response (Bug-02).
double daemon_device_field(const std::string& resp, AppState* S, const char* key);

// profile
void rebuild_profile_combo(AppState* S);
void show_input_dialog(AppState* S,
                       const char* title, const char* placeholder,
                       const char* initial,
                       std::function<void(const std::string&)> cb);
void on_import_profile(GtkButton*, gpointer user_data);
void on_export_profile(GtkButton*, gpointer user_data);

// widgets <-> profile sync
void widgets_to_profile(AppState* S);
void profile_to_widgets(AppState* S);

// device combo
void refresh_mice_combo(AppState* S, bool is_auto = false, bool quiet = false);
void hw_start_scan(AppState* S);
/// Rebuild the hardware-panel capability summary for `idx` (hidpp_panel.inl).
void hw_render_caps(AppState* S, int idx);
/// Update the hardware-panel battery row for `idx` (hidpp_panel.inl).
void hw_set_battery(AppState* S, int idx,
                    const std::optional<hidpp_battery_info>& battery,
                    logitech_battery_source source);

// graph / LUT
void rebuild_lut_list(AppState* S);
void update_lut_visibility(AppState* S);

// UI entry point
void build_ui(AppState* S, GtkApplication* gapp);

// mouse lock test window (implemented in mouse_test.inl, Bug-09 language refresh)
void mouse_test_refresh_language(AppState* S);

// GTK callbacks — forward declarations (implementations in .inl files)
void on_activate(GtkApplication* gapp, gpointer user_data); // user_data = AppState*
// Toggles the top-level use_raw_input master switch (widgets_sync.inl).
void on_raw_input_toggled(GtkCheckButton* btn, gpointer user_data);
