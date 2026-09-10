// ── Mouse lock test window (P104) ─────────────────────────────────────────────
//
// Popup for live-testing the acceleration feel: it LOCKS the pointer inside one
// window while a 250 ms poll reads the daemon's live telemetry from the status
// JSON (per-device telem_in_ips / telem_out_ips / telem_gain emitted by
// daemon.cpp status_json).  While the test runs, the cursor can never
// click/scroll/hover into another window — the user's exact request
// ("mouse test edilirken bütün sayfaları birbirine sokuyor").  ESC — or closing
// the window — releases and tears the popup down.
//
// GDK4 CONSTRAINT: GTK4 REMOVED pointer/seat grab APIs entirely
// (gdk_seat_grab / gdk_pointer_grab / gtk_grab_add / gdk_device_grab /
// gdk_device_warp are all gone from the public headers; verified against the
// installed gtk-4.0 dev package).  The dependency-free emulation is therefore:
//
//   Tier 1 (X11 — REAL grab): gdk_x11_display_get_xdisplay() hands us the Xlib
//       Display*, then XGrabPointer grabs the pointer with
//       owner_events=False + event_mask=0.  The X server redirects EVERY
//       pointer event to us and (mask 0) swallows it, so no other window on ANY
//       monitor receives clicks/scrolls/hover; confine_to = the fullscreen
//       toplevel keeps the visible cursor inside too.  Keyboard is NOT grabbed,
//       so ESC always works.  We auto-release on focus loss so Alt+Tab can never
//       strand the user.
//   Tier 2 (X11 — grab refused): confine loop — a frame-clock tick re-warps the
//       pointer into the window whenever it leaves, while the window has focus.
//   Tier 3 (Wayland / no X11 backend): GDK has no grab and no warp at all here.
//       Fall back to the fullscreen HUD (covers the active monitor — best
//       effort) and warn openly.
//   Degrade rule: if the grab fails at open time the popup STILL opens, shows
//       live telemetry and warns (imleç kilidi yok = "no pointer lock").
//
// Why no new build dependency: libX11.so.6 is already loaded in-process because
// libgtk-4's X11 backend links it, so the few Xlib entry points below are
// resolved with dlopen/dlsym at runtime (dl* lives in glibc; -ldl is a no-op
// stub on glibc ≥ 2.34 and required on older glibc, so it was added to the GUI
// link line for portability).  GDK_WINDOWING_X11 guards the widget code so a
// GTK4 built without the X11 backend still compiles (Tier 3).

#include <gdk/gdkkeysyms.h>
#include <dlfcn.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#endif

// ── X11 pointer-lock bridge (dlopen'd, never link-time) ──────────────────────
#ifdef GDK_WINDOWING_X11
// Xlib entry points used by the lock.  The types come from Xlib.h (pulled in by
// gdkx.h above); the symbols are resolved at runtime, never linked.
typedef int (*PF_XGrabPointer)(Display* dpy, Window grab_window, Bool owner_events,
                               unsigned int event_mask, int pointer_mode,
                               int keyboard_mode, Window confine_to,
                               Cursor cursor, Time time_);
typedef int (*PF_XUngrabPointer)(Display* dpy, Time time_);
typedef int (*PF_XWarpPointer)(Display* dpy, Window src_w, Window dest_w,
                               int src_x, int src_y, unsigned int src_width,
                               unsigned int src_height, int dest_x, int dest_y);

// Compile-time sanity on the X protocol constants the grab relies on — if the
// X server ever changed these the lock would silently misbehave.
static_assert(GrabModeAsync == 1, "X11 GrabModeAsync must be 1");
static_assert(GrabSuccess   == 0, "X11 GrabSuccess must be 0");
static_assert(CurrentTime   == 0L, "X11 CurrentTime must be 0");

struct XLockBridge {
    void*      handle = nullptr;
    PF_XGrabPointer   grab   = nullptr;
    PF_XUngrabPointer ungrab = nullptr;
    PF_XWarpPointer   warp   = nullptr;
    bool       loaded = false;
};

static XLockBridge& mouse_test_x11_bridge() {
    static XLockBridge b;
    if (!b.loaded) {
        b.loaded = true;
        b.handle = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        if (b.handle) {
            b.grab   = reinterpret_cast<PF_XGrabPointer>  (dlsym(b.handle, "XGrabPointer"));
            b.ungrab = reinterpret_cast<PF_XUngrabPointer>(dlsym(b.handle, "XUngrabPointer"));
            b.warp   = reinterpret_cast<PF_XWarpPointer>  (dlsym(b.handle, "XWarpPointer"));
        }
    }
    return b;
}

/// True when the display is the X11 backend and the lock bridge is usable.
static bool mouse_test_x11_ready(GdkDisplay* d) {
    auto& b = mouse_test_x11_bridge();
    return d && b.grab && b.ungrab && GDK_IS_X11_DISPLAY(d);
}

// Open test window — semantic text-staters (forward decls; defined in the
// status/hint section, used by grab/lock/poll handlers declared above it).
static void mouse_test_set_status(AppState* S, int state);
static void mouse_test_set_hint(AppState* S, int state);

/// Tier 1: real X server pointer grab.  owner_events=False + event_mask=0 means
/// every pointer event is redirected to our toplevel and NOT reported anywhere
/// (no other window ever sees input); confine_to = the toplevel keeps the
/// visible cursor inside the window.  Returns true only on X GrabSuccess.
static bool mouse_test_try_grab(AppState* S, Display* xd, Window xw) {
    auto& b = mouse_test_x11_bridge();
    if (!b.grab || !xd || xw == 0) return false;
    int st = b.grab(xd, xw,
                    /*owner_events=*/False, /*event_mask=*/0,
                    /*pointer_mode=*/GrabModeAsync, /*keyboard_mode=*/GrabModeAsync,
                    /*confine_to=*/xw, /*cursor=*/None, /*time_=*/CurrentTime);
    if (st != GrabSuccess) return false;
    S->test_grabbed = true;
    return true;
}

/// Grab path used once the window is mapped/viewable (a grab on an unmapped
/// window returns GrabNotViewable).  Sets the Tier-1 hint on success; safe to
/// call multiple times (guarded by test_grabbed).
static bool mouse_test_try_grab_locked(AppState* S) {
    if (S->test_grabbed) return true;
    GtkWidget* win = S->mouse_test_win;
    if (!win) return false;
    GtkNative*  nat = gtk_widget_get_native(win);
    GdkSurface* surf = nat ? gtk_native_get_surface(nat) : nullptr;
    if (!surf) return false;
    GdkDisplay* dd = gtk_widget_get_display(win);
    if (!mouse_test_x11_ready(dd)) return false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    Display* xd = gdk_x11_display_get_xdisplay(dd);
    Window   xw = gdk_x11_surface_get_xid(surf);
#pragma GCC diagnostic pop
    if (!xd || xw == 0) return false;
    if (!mouse_test_try_grab(S, xd, xw)) return false;
    mouse_test_set_hint(S, 0); // Tier-1 hint
    return true;
}

/// Release an active Tier-1 grab.  Needs only the Display*, so it is safe even
/// during window destruction (the surface may be gone, the X connection is not).
static void mouse_test_release(AppState* S) {
    if (!S->test_grabbed) return;
    auto& b = mouse_test_x11_bridge();
    GdkDisplay* dd = S->mouse_test_win ? gtk_widget_get_display(S->mouse_test_win)
                                       : gdk_display_get_default();
    if (b.ungrab && dd && GDK_IS_X11_DISPLAY(dd)) {
        // Deprecated in 4.18 but the only X11 accessor GTK4 still exports.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        Display* xd = gdk_x11_display_get_xdisplay(dd);
#pragma GCC diagnostic pop
        if (xd) b.ungrab(xd, CurrentTime);
    }
    S->test_grabbed = false;
}
#else // !GDK_WINDOWING_X11 — Tier 3 only; Wayland has no grab/warp API at all.
static bool mouse_test_x11_ready(GdkDisplay*) { return false; }
static void mouse_test_release(AppState* S) { S->test_grabbed = false; }
#endif

// ── Status / hint text management (BUG-09) ────────────────────────────────────
// The test window's transient labels are NOT registered in the refresh_language
// widget registry, so a language switch while the window is open left them in
// the old language.  Every text assignment now goes through these two setters,
// which remember the semantic STATE rather than the translated string; on a
// language switch mouse_test_refresh_language() replays the states in the new
// language.

static void mouse_test_set_status(AppState* S, int state) {
    S->test_status_state = state;
    if (!S->test_status_lbl) return;
    const char* txt = "";
    switch (state) {
        case 0: txt = ""; break;                         // live — no note
        case 1: txt = tr("Awaiting motion…"); break;
        case 2: txt = tr("Daemon is not running."); break;
        case 3: txt = tr("Mouse test: pointer lock released (window unfocused)."); break;
        default: txt = ""; break;
    }
    gtk_label_set_text(GTK_LABEL(S->test_status_lbl), txt);
}

static void mouse_test_set_hint(AppState* S, int state) {
    S->test_hint_state = state;
    if (!S->test_hint_lbl) return;
    const char* txt = "";
    switch (state) {
        case 0: txt = tr("Pointer is locked inside this fullscreen test window.\nMove the mouse to see live speed/gain.\nPress ESC to release."); break;
        case 1: txt = tr("Pointer grab failed — the cursor is confined as best effort (imleç kilidi yok).\nMove the mouse to see live speed/gain.\nPress ESC to release."); break;
        case 2: txt = tr("Pointer lock unavailable — imleç kilidi yok: this Wayland session cannot grab the pointer.\nThe cursor is NOT locked inside this window (fullscreen coverage only).\nMove the mouse, then press ESC to close."); break;
        default: txt = ""; break;
    }
    gtk_label_set_text(GTK_LABEL(S->test_hint_lbl), txt);
}

/// Re-apply every translated string of an open test window (BUG-09).  Called
/// from refresh_language() (tr.inl) on language switch; a no-op while the
/// window is closed.
void mouse_test_refresh_language(AppState* S) {
    if (!S->mouse_test_win) return;
    const char* names[3] = { tr("In (ips):"), tr("Out (ips):"), tr("Gain (×):") };
    for (int i = 0; i < 3 && S->test_name_lbls[i]; i++)
        gtk_label_set_text(GTK_LABEL(S->test_name_lbls[i]), names[i]);
    if (S->test_title_lbl)
        gtk_label_set_markup(GTK_LABEL(S->test_title_lbl),
            (std::string("<b><span size='xx-large'>") +
             tr("Mouse Lock Test") + "</span></b>").c_str());
    gtk_window_set_title(GTK_WINDOW(S->mouse_test_win), tr("Mouse Lock Test"));
    mouse_test_set_status(S, S->test_status_state);
    mouse_test_set_hint(S, S->test_hint_state);
}

/// Remove the telemetry poll timer (the poll re-arms nothing and self-stops).
static void mouse_test_stop_poll(AppState* S) {
    if (S->test_poll_id) {
        g_source_remove(S->test_poll_id);
        S->test_poll_id = 0;
    }
}

/// 250 ms poll: refresh live speed/gain readouts from the daemon status JSON.
/// The socket timeout (150 ms in daemon_comm.inl) is smaller than this tick, so
/// a dead-but-listening daemon can never wedge the UI (BUG-05).  One status
/// query per tick does double duty as the up-check — no separate ping round
/// trip.  A missing/unreachable daemon shows "—" but keeps the timer alive.
static gboolean mouse_test_poll(gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (!S->mouse_test_win) return G_SOURCE_REMOVE; // popup closed earlier

    // Skip the IPC entirely when no socket exists (cheap stat) — BUG-10 gate.
    if (!daemon_socket_exists()) {
        if (S->test_speed_lbl) gtk_label_set_text(GTK_LABEL(S->test_speed_lbl), "—");
        if (S->test_out_lbl)   gtk_label_set_text(GTK_LABEL(S->test_out_lbl),   "—");
        if (S->test_gain_lbl)  gtk_label_set_text(GTK_LABEL(S->test_gain_lbl),  "—");
        mouse_test_set_status(S, 2); // daemon not running
        return G_SOURCE_CONTINUE;
    }
    std::string resp = daemon_ipc_query("status");
    if (resp.empty()) {
        // No complete response within the timeout — treat as daemon down.
        if (S->test_speed_lbl) gtk_label_set_text(GTK_LABEL(S->test_speed_lbl), "—");
        if (S->test_out_lbl)   gtk_label_set_text(GTK_LABEL(S->test_out_lbl),   "—");
        if (S->test_gain_lbl)  gtk_label_set_text(GTK_LABEL(S->test_gain_lbl),  "—");
        mouse_test_set_status(S, 2);
        return G_SOURCE_CONTINUE;
    }
    // BUG-02: read the ACTIVE profile's device slice, not the first device.
    double in  = daemon_device_field(resp, S, "telem_in_ips");
    double out = daemon_device_field(resp, S, "telem_out_ips");
    double gain = daemon_device_field(resp, S, "telem_gain");
    if (in < 0) {
        // Daemon answered but has not seen a motion sample yet.
        mouse_test_set_status(S, 1); // awaiting motion
        return G_SOURCE_CONTINUE;
    }
    if (S->test_speed_lbl) gtk_label_set_text(GTK_LABEL(S->test_speed_lbl), fmt_us(in).c_str());
    if (S->test_out_lbl)   gtk_label_set_text(GTK_LABEL(S->test_out_lbl),   fmt_us(out).c_str());
    if (S->test_gain_lbl)  gtk_label_set_text(GTK_LABEL(S->test_gain_lbl),  fmt_us(gain).c_str());
    mouse_test_set_status(S, 0); // live
    return G_SOURCE_CONTINUE;
}

/// ESC releases the lock and closes the HUD (keyboard is never grabbed, so ESC
/// always reaches us even while the pointer is locked).
static gboolean mouse_test_escape(GtkEventControllerKey*, guint keyval, guint,
                                  GdkModifierType, gpointer user_data) {
    if (keyval != GDK_KEY_Escape) return GDK_EVENT_PROPAGATE;
    auto* S = static_cast<AppState*>(user_data);
    if (S->mouse_test_win) {
        if (S->test_confine_id) {
            gtk_widget_remove_tick_callback(S->mouse_test_win, S->test_confine_id);
            S->test_confine_id = 0;
        }
        mouse_test_release(S); // ungrab BEFORE the surface disappears
        gtk_window_destroy(GTK_WINDOW(S->mouse_test_win));
    }
    set_status(S, tr("Mouse test: pointer released (ESC)."));
    return GDK_EVENT_STOP;
}

/// Tier 2 confine loop: while the window has focus, warp the pointer back into
/// the window whenever it leaves (XWarpPointer — GTK4 has no warp API).  No-op
/// while the Tier-1 grab holds.
static gboolean mouse_test_confine_tick(GtkWidget* widget, GdkFrameClock*,
                                        gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (!S->mouse_test_win || S->test_grabbed) return G_SOURCE_CONTINUE;
#ifdef GDK_WINDOWING_X11
    if (!gtk_window_is_active(GTK_WINDOW(widget))) return G_SOURCE_CONTINUE;
    auto& b = mouse_test_x11_bridge();
    if (!b.warp) return G_SOURCE_CONTINUE;
    GdkDisplay* dd = gtk_widget_get_display(widget);
    GtkNative*  nat = gtk_widget_get_native(widget);
    GdkSurface* surf = nat ? gtk_native_get_surface(nat) : nullptr;
    if (!dd || !surf) return G_SOURCE_CONTINUE;
    GdkSeat*   seat = gdk_display_get_default_seat(dd);
    GdkDevice* dev  = seat ? gdk_seat_get_pointer(seat) : nullptr;
    if (!dev) return G_SOURCE_CONTINUE;
    double px = -1, py = -1;
    gdk_surface_get_device_position(surf, dev, &px, &py, nullptr);
    int w = gdk_surface_get_width(surf);
    int h = gdk_surface_get_height(surf);
    if (px < 0 || py < 0 || px >= w || py >= h) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        Display* xd = gdk_x11_display_get_xdisplay(dd);
        Window   xw = gdk_x11_surface_get_xid(surf);
#pragma GCC diagnostic pop
        if (!xd || xw == 0) return G_SOURCE_CONTINUE;
        b.warp(xd, /*src_w=*/None, /*dest_w=*/xw, 0, 0, 0, 0, w / 2, h / 2);
    }
#else
    (void)widget;
#endif
    return G_SOURCE_CONTINUE;
}

/// Lock is honour-bound to focus: losing focus releases the grab (so Alt+Tab is
/// never a trap), regaining it re-acquires the lock when possible.
static void mouse_test_focus_changed(GtkWindow* win, GParamSpec*,
                                     gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (!S->mouse_test_win) return;
    if (gtk_window_is_active(win)) {
#ifdef GDK_WINDOWING_X11
        mouse_test_try_grab_locked(S); // re-lock on focus regain; sets Tier-1 hint on success
#endif
    } else if (S->test_grabbed) {
        mouse_test_release(S);
        mouse_test_set_status(S, 3); // pointer lock released (unfocused)
    }
}

/// Window destroy: ungrab first, stop the poll, remove the confine tick.
static void mouse_test_teardown(GtkWidget* widget, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    mouse_test_release(S); // release BEFORE the X window is gone
    if (S->test_confine_id) {
        gtk_widget_remove_tick_callback(widget, S->test_confine_id);
        S->test_confine_id = 0;
    }
    // Drop every transient-widget pointer so a language refresh (BUG-09) and a
    // possibly-still-queued poll tick can never dereference freed widgets.
    S->mouse_test_win   = nullptr;
    S->test_speed_lbl   = nullptr;
    S->test_out_lbl     = nullptr;
    S->test_gain_lbl    = nullptr;
    S->test_status_lbl  = nullptr;
    S->test_hint_lbl    = nullptr;
    S->test_title_lbl   = nullptr;
    for (int i = 0; i < 3; i++) S->test_name_lbls[i] = nullptr;
    S->test_hint_state   = 0;
    S->test_status_state = -1;
    mouse_test_stop_poll(S);
}

/// Window realized: decide the lock tier and apply it.
///   X11      → Tier-1 grab; on refusal → Tier-2 confine loop (both warn).
///   Wayland  → Tier 3: fullscreen-only, warning hint, read-only.
static void mouse_test_realize(GtkWidget* win, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (!S->mouse_test_win) return;
#ifdef GDK_WINDOWING_X11
    GdkDisplay* dd = gtk_widget_get_display(win);
    if (!mouse_test_x11_ready(dd)) {
        // Wayland backend on a both-backends GTK4 build (or no X display):
        // Tier 3 — GDK has no grab/warp here, warn that nothing is locked.
        S->test_x11 = false;
        mouse_test_set_hint(S, 2);
        return;
    }
    // X11 display: the window maps a moment later, and XGrabPointer on a not
    // yet viewable window returns GrabNotViewable — the actual Tier-1 attempt
    // therefore happens in the map/focus handlers (mouse_test_try_grab_locked).
    S->test_x11 = true;
#else
    (void)win;
    S->test_x11 = false;
    mouse_test_set_hint(S, 2);
#endif
}

/// Started on window "map": begin the 250 ms telemetry poll.
static void mouse_test_start(gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->mouse_test_win && S->test_poll_id == 0)
        S->test_poll_id = g_timeout_add(250, mouse_test_poll, S);
}

/// Window "map" = viewable — now the X11 grab can succeed.  Tier 1 on success,
/// otherwise arm the Tier-2 confine tick and its warning.
static void mouse_test_arm_lock(gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (!S->mouse_test_win) return;
#ifdef GDK_WINDOWING_X11
    if (S->test_x11 && mouse_test_try_grab_locked(S)) return; // Tier 1 — locked
    if (S->test_x11) { // grab refused (AlreadyGrabbed / NotViewable / other)
        if (S->test_confine_id == 0)
            S->test_confine_id = gtk_widget_add_tick_callback(S->mouse_test_win,
                mouse_test_confine_tick, S, nullptr);
        mouse_test_set_hint(S, 1); // Tier-2 confine warning
    }
#else
    (void)user_data;
#endif
}

/// "Mouse Test" button in the status bar. Opens the test window once; a second
/// click just re-presents and focuses it.
void on_mouse_test_clicked(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->mouse_test_win) {
        gtk_window_present(GTK_WINDOW(S->mouse_test_win));
        gtk_widget_grab_focus(S->mouse_test_win);
        return;
    }

    GtkWidget* win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), tr("Mouse Lock Test"));
    S->mouse_test_win = win;

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(vbox, 24); gtk_widget_set_margin_end(vbox, 24);
    gtk_widget_set_margin_top(vbox, 24);   gtk_widget_set_margin_bottom(vbox, 24);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title),
        (std::string("<b><span size='xx-large'>") +
         tr("Mouse Lock Test") + "</span></b>").c_str());
    gtk_label_set_xalign(GTK_LABEL(title), 0.5);
    gtk_box_append(GTK_BOX(vbox), title);
    S->test_title_lbl = title;

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_top(grid, 16);
    gtk_box_append(GTK_BOX(vbox), grid);

    // Three telemetry rows.  The name labels are created with tr() directly so
    // the translation-coverage scanner sees the keys (grid_row-style lambdas
    // would hide them — the name labels are transient, not registered widgets).
    // Their pointers are kept in test_name_lbls so mouse_test_refresh_language()
    // can re-translate them on a language switch while the window is open (BUG-09).
    auto value_lbl = []() {
        GtkWidget* l = gtk_label_new("—");
        gtk_label_set_xalign(GTK_LABEL(l), 0.0);
        gtk_widget_add_css_class(l, "test-val");
        return l;
    };
    auto name_lbl = [](const char* t) {
        GtkWidget* l = gtk_label_new(t);
        gtk_label_set_xalign(GTK_LABEL(l), 0.0);
        return l;
    };

    S->test_name_lbls[0] = name_lbl(tr("In (ips):"));
    gtk_grid_attach(GTK_GRID(grid), S->test_name_lbls[0], 0, 0, 1, 1);
    S->test_speed_lbl = value_lbl();
    gtk_grid_attach(GTK_GRID(grid), S->test_speed_lbl, 1, 0, 1, 1);

    S->test_name_lbls[1] = name_lbl(tr("Out (ips):"));
    gtk_grid_attach(GTK_GRID(grid), S->test_name_lbls[1], 0, 1, 1, 1);
    S->test_out_lbl = value_lbl();
    gtk_grid_attach(GTK_GRID(grid), S->test_out_lbl, 1, 1, 1, 1);

    S->test_name_lbls[2] = name_lbl(tr("Gain (×):"));
    gtk_grid_attach(GTK_GRID(grid), S->test_name_lbls[2], 0, 2, 1, 1);
    S->test_gain_lbl = value_lbl();
    gtk_grid_attach(GTK_GRID(grid), S->test_gain_lbl, 1, 2, 1, 1);

    S->test_status_lbl = gtk_label_new(tr("Awaiting motion…"));
    gtk_label_set_xalign(GTK_LABEL(S->test_status_lbl), 0.5);
    gtk_box_append(GTK_BOX(vbox), S->test_status_lbl);
    mouse_test_set_status(S, 1); // awaiting motion

    S->test_hint_lbl = gtk_label_new(
        tr("Pointer is locked inside this fullscreen test window.\nMove the mouse to see live speed/gain.\nPress ESC to release."));
    gtk_label_set_xalign(GTK_LABEL(S->test_hint_lbl), 0.5);
    gtk_label_set_justify(GTK_LABEL(S->test_hint_lbl), GTK_JUSTIFY_CENTER);
    gtk_label_set_wrap(GTK_LABEL(S->test_hint_lbl), TRUE);
    gtk_widget_set_margin_top(S->test_hint_lbl, 12);
    gtk_box_append(GTK_BOX(vbox), S->test_hint_lbl);

    // ESC releases and closes the HUD.
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(mouse_test_escape), S);
    gtk_widget_add_controller(win, keys);

    // Lock tier decision once the window has a native surface.
    g_signal_connect(win, "realize", G_CALLBACK(mouse_test_realize), S);

    // Start the poll once the surface exists (the window is mapped).
    g_signal_connect(win, "map",
        G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            mouse_test_start(ud);
            mouse_test_arm_lock(ud);
        }), S);

    // Focus changes release/re-acquire the lock (never a trap).
    g_signal_connect(win, "notify::is-active",
        G_CALLBACK(mouse_test_focus_changed), S);

    // Clean up grab/poll/tick when the HUD goes away (ESC / close / quit).
    g_signal_connect(win, "destroy", G_CALLBACK(mouse_test_teardown), S);

    // Fullscreen covers the active monitor in every tier (baseline soft-lock);
    // the Tier-1 grab plus confine_to is what hard-blocks across monitors.
    gtk_window_fullscreen(GTK_WINDOW(win));
    gtk_window_present(GTK_WINDOW(win));
}