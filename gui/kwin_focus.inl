// ── kwin_focus.inl — KDE Wayland active-window focus relay ───────────────────
//
// Loads a tiny KWin script that watches `workspace.windowActivated` and
// forwards `window.resourceClass` to a GUI-owned GDBus service
// (`org.rawaccel.Focus`).  The GUI then reports it to the daemon via the
// existing `set_active_app` IPC.  Best-effort: if KWin is not running, the
// session bus is unreachable, or the name is already taken, focus tracking
// silently degrades to no-op (per-app profiles only work when configured
// manually via the "App match" field).
//
// Lifecycle:
//   kwin_focus_install(AppState*)  — call once from on_activate after build_ui
//   (implicit)                     — uninstall on window-destroy callback
//
// ── Embedded KWin script ─────────────────────────────────────────────────────

static constexpr const char* KWIN_FOCUS_SCRIPT = R"EOF(
var rawaccel_last = "";
function rawaccel_focus_report() {
    var w = workspace.activeWindow;
    var cls = "";
    if (w) cls = w.resourceClass || w.windowClass || "";
    if (cls === rawaccel_last) return;
    rawaccel_last = cls;
    callDBus("org.rawaccel.Focus", "/Focus", "org.rawaccel.Focus",
             "setActiveApp", cls);
}
workspace.windowActivated.connect(rawaccel_focus_report);
rawaccel_focus_report();
)EOF";

// ── DBus introspection for the relay object ──────────────────────────────────

static const char* FOCUS_NODE_XML =
    "<node>"
    "  <interface name='org.rawaccel.Focus'>"
    "    <method name='setActiveApp'>"
    "      <arg direction='in' type='s' name='wm_class'/>"
    "    </method>"
    "  </interface>"
    "</node>";

// ── Internal state ───────────────────────────────────────────────────────────

#include <thread>
#include <atomic>

struct kwin_focus_ctx {
    GDBusConnection* session_conn  = nullptr;
    guint            obj_reg_id    = 0;   // g_dbus object registration id
    guint            bus_name_id   = 0;   // g_bus_own_name id
    std::atomic<int> kwin_script_id{-1};  // script id (set by worker thread)
    bool             installed     = false;
    // R8-RESEND: last WM_CLASS the relay reported (cache so the poll can
    // re-send it after a daemon down/up cycle).
    std::string      last_app;
    // GUI-Y2: keep the worker joinable instead of detached.  kwin_focus_uninstall
    // MUST join it before releasing session_conn — otherwise the worker may still
    // be mid-Call on a connection we have already unref'd (UAF).
    std::thread      worker;
};

// ── GDBus method handler ─────────────────────────────────────────────────────

static void focus_method_call(GDBusConnection* conn, const gchar*,
                              const gchar*, const gchar* interface,
                              const gchar* method, GVariant* params,
                              GDBusMethodInvocation* inv, gpointer user_data) {
    if (g_strcmp0(interface, "org.rawaccel.Focus") == 0 &&
        g_strcmp0(method, "setActiveApp") == 0) {
        // R13-KWINARG: harden the handler.  (a) Type-check the signature — a
        // hostile caller passing a non-"(s)" variant could make g_variant_get
        // read past the variant.  (b) Best-effort sender filter: only accept
        // calls whose sender resolves to a KWin process.  Any app on the
        // session bus can otherwise flip the daemon's active-app profile to a
        // spoofed class.  If the sender CANNOT be identified (bus denial,
        // /proc restricted) we accept — preserving the old best-effort
        // contract rather than breaking legit setups.
        if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(s)"))) {
            g_dbus_method_invocation_return_value(inv, nullptr);
            return;
        }
        const gchar* sender = g_dbus_method_invocation_get_sender(inv);
        if (sender) {
            bool accepted = false;
            GError* derr = nullptr;
            GVariant* r = g_dbus_connection_call_sync(
                conn, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"),
                G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &derr);
            if (r) {
                guint32 pid = 0;
                g_variant_get(r, "(u)", &pid);
                g_variant_unref(r);
                char comm_path[64];
                snprintf(comm_path, sizeof(comm_path), "/proc/%u/comm", pid);
                FILE* cf = fopen(comm_path, "r");
                if (cf) {
                    char comm[64] = {};
                    (void)!fgets(comm, sizeof(comm), cf);
                    fclose(cf);
                    accepted = strncmp(comm, "kwin", 4) == 0;
                } else {
                    accepted = true; // PID unreachable — fall back to accepting
                }
            } else {
                if (derr) g_error_free(derr);
                accepted = true; // bus refused the lookup — fall back to accepting
            }
            if (!accepted) {
                // Non-KWin caller — spoofed or stale; ignore silently.
                g_dbus_method_invocation_return_value(inv, nullptr);
                return;
            }
        }
        const gchar* wm_class = nullptr;
        g_variant_get(params, "(&s)", &wm_class);
        auto* ctx = static_cast<kwin_focus_ctx*>(user_data);
        if (ctx) ctx->last_app = wm_class ? wm_class : "";
        {
            // R12-STALERT: unify dropped-report handling for BOTH app-scoped
            // and clear ("", no app focused) reports.  R11-FOCR only retried
            // non-empty classes, so a dropped CLEAR left the previous
            // app-scoped profile applied until the next focus change.
            std::string reported = wm_class ? wm_class : "";
            if (!daemon_ipc_set_active_app(reported)) {
                // A dropped report (the one-shot 150 ms IPC read timed out
                // against a busy serial daemon) is retried once after the
                // contention has likely cleared — but ONLY if focus is STILL
                // the class we queued at fire time.  A newer report that
                // already reached the daemon must never be overwritten by this
                // stale replay (that would REVERT the correct app-scoped
                // profile until the next focus change).  Retries stay bounded
                // to one: a failed replay is dropped, not re-queued.
                struct FocusRetry { kwin_focus_ctx* ctx; std::string queued; };
                auto* r = new FocusRetry{ctx, reported};
                g_timeout_add_full(G_PRIORITY_DEFAULT, 1000,
                    [](gpointer p) -> gboolean {
                        auto* R = static_cast<FocusRetry*>(p);
                        std::string now = R->ctx ? R->ctx->last_app : R->queued;
                        bool same = (now == R->queued);
                        if (same) daemon_ipc_set_active_app(now);
                        return G_SOURCE_REMOVE; // destroy-notify then reclaims R
                    }, r, [](gpointer p) { delete static_cast<FocusRetry*>(p); });
            }
        }
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }
    g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod",
                                               "Unknown method");
}

static const GDBusInterfaceVTable focus_vtable = {
    focus_method_call, nullptr, nullptr, { nullptr, nullptr, nullptr, nullptr } };

// ── KWin script loading helpers ──────────────────────────────────────────────

/// Synchronously load + run the KWin script via the session bus.  Returns the
/// script id on success, -1 on failure.  Must be called from a worker thread
/// (blocks until KWin has evaluated the script).  Best-effort: fails silently.
static int kwin_script_load_and_run_sync(GDBusConnection* conn) {
    if (!conn) return -1;
    GError* err = nullptr;

    // 1. Write the script to a temp file.
    const char* runtime = g_get_user_runtime_dir();
    std::string dir  = std::string(runtime ? runtime : "/tmp") + "/rawaccel";
    g_mkdir_with_parents(dir.c_str(), 0700);
    std::string path = dir + "/kwin_focus_relay.js";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return -1;
    fprintf(f, "%s", KWIN_FOCUS_SCRIPT);
    fclose(f);

    // 2. loadScript(path, pluginId) → (int id)
    GVariant* result = g_dbus_connection_call_sync(
        conn, "org.kde.KWin", "/Scripting",
        "org.kde.kwin.Scripting", "loadScript",
        g_variant_new("(ss)", path.c_str(), "rawaccel_focus_relay"),
        G_VARIANT_TYPE("(i)"),
        G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &err);
    if (!result) { g_error_free(err); return -1; }
    gint script_id = 0;
    g_variant_get(result, "(i)", &script_id);
    g_variant_unref(result);

    // 3. If already loaded, unload first and reload (idempotent update).
    if (script_id < 0) {
        result = g_dbus_connection_call_sync(
            conn, "org.kde.KWin", "/Scripting",
            "org.kde.kwin.Scripting", "unloadScript",
            g_variant_new("(s)", "rawaccel_focus_relay"),
            nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &err);
        if (result) g_variant_unref(result);
        if (err) g_error_free(err);
        result = g_dbus_connection_call_sync(
            conn, "org.kde.KWin", "/Scripting",
            "org.kde.kwin.Scripting", "loadScript",
            g_variant_new("(ss)", path.c_str(), "rawaccel_focus_relay"),
            G_VARIANT_TYPE("(i)"),
            G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &err);
        if (!result) return -1;
        g_variant_get(result, "(i)", &script_id);
        g_variant_unref(result);
        if (script_id < 0) return -1;
    }

    // 4. run() the script: /Scripting/Script<id> → org.kde.kwin.Script.run
    std::string obj_path = "/Scripting/Script" + std::to_string(script_id);
    result = g_dbus_connection_call_sync(
        conn, "org.kde.KWin", obj_path.c_str(),
        "org.kde.kwin.Script", "run",
        nullptr, nullptr,
        G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &err);
    if (result) g_variant_unref(result);
    if (err) g_error_free(err);  // may fail if KWin has no Workspace yet, ignore

    return script_id;
}

static void kwin_script_unload_sync(GDBusConnection* conn) {
    if (!conn) return;
    GError* err = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        conn, "org.kde.KWin", "/Scripting",
        "org.kde.kwin.Scripting", "unloadScript",
        g_variant_new("(s)", "rawaccel_focus_relay"),
        nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &err);
    if (result) g_variant_unref(result);
    if (err) g_error_free(err);
}

// ── GBus callbacks ───────────────────────────────────────────────────────────

static void on_bus_acquired(GDBusConnection* conn, const gchar*, gpointer user_data) {
    auto* ctx = static_cast<kwin_focus_ctx*>(user_data);
    GError* err = nullptr;
    GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(FOCUS_NODE_XML, &err);
    if (!node) { g_error_free(err); return; }
    GDBusInterfaceInfo* iface = g_dbus_node_info_lookup_interface(node, "org.rawaccel.Focus");
    if (iface) {
        ctx->obj_reg_id = g_dbus_connection_register_object(
            conn, "/Focus", iface, &focus_vtable, ctx, nullptr, &err);
        if (ctx->obj_reg_id == 0) g_error_free(err);
    }
    g_dbus_node_info_unref(node);
}

static void on_name_lost(GDBusConnection*, const gchar*, gpointer user_data) {
    (void)user_data;
    // Name lost — another instance or race.  Focus relay just won't work this run.
}

// ── Public API ───────────────────────────────────────────────────────────────

/// R8-RESEND: forward-declared in app_state.hpp so daemon_comm.inl's 3 s poll
/// can call this on a daemon up-transition.  The KWin relay only fires on a
/// focus CHANGE (its script caches the last-reported class), so after a daemon
/// down/up cycle app-scoped profiles would stay dormant until the user switches
/// windows — re-send the last known WM_CLASS.
static void kwin_focus_resend_current(AppState* S) {
    auto* ctx = static_cast<kwin_focus_ctx*>(S->kwin_focus_ctx);
    if (!ctx || !ctx->installed || ctx->last_app.empty()) return;
    daemon_ipc_set_active_app(ctx->last_app);
}

/// Install the KWin focus relay.  Best-effort: returns true if relay is active.
static bool kwin_focus_install(AppState* S) {
    static kwin_focus_ctx ctx;
    if (ctx.installed) return true;

    // 1. Own the GDBus name on the session bus (non-blocking).
    ctx.bus_name_id = g_bus_own_name(
        G_BUS_TYPE_SESSION, "org.rawaccel.Focus",
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, nullptr, on_name_lost, &ctx, nullptr);
    if (ctx.bus_name_id == 0) return false;

    // 2. Connect to session bus (needed for KWin D-Bus calls and our relay).
    GError* err = nullptr;
    ctx.session_conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (!ctx.session_conn) {
        g_error_free(err);
        // R13-KWINOWN: we already own the name — release it so this failed
        // install cannot leave org.rawaccel.Focus owned for the whole session
        // (a second instance's g_bus_own_name would then always fail).
        if (ctx.bus_name_id) { g_bus_unown_name(ctx.bus_name_id); ctx.bus_name_id = 0; }
        return false;
    }

    // 3. Load + run the KWin script on a worker thread (can block ~200–500 ms).
    ctx.kwin_script_id.store(-1, std::memory_order_relaxed);
    ctx.worker = std::thread([ctxPtr = &ctx]() {
        ctxPtr->kwin_script_id = kwin_script_load_and_run_sync(ctxPtr->session_conn);
    });
    ctx.installed = true;

    S->kwin_focus_ctx = &ctx;
    return true;
}

/// Uninstall: unload the KWin script, release the GDBus name.
static void kwin_focus_uninstall(AppState* S) {
    auto* ctx = static_cast<kwin_focus_ctx*>(S->kwin_focus_ctx);
    if (!ctx || !ctx->installed) return;
    ctx->installed = false;
    // GUI-Y2: join the loader worker BEFORE touching session_conn (the worker
    // holds a raw pointer to it while running D-Bus calls).  Its D-Bus calls
    // carry a timeout so the join completes within a bounded time.
    if (ctx->worker.joinable()) ctx->worker.join();
    // Unload script (best-effort).
    int sid = ctx->kwin_script_id.load(std::memory_order_relaxed);
    if (ctx->session_conn && sid >= 0)
        kwin_script_unload_sync(ctx->session_conn);
    // Release GDBus name + unregister the /Focus object (R13-KWINOWN — symmetry
    // with on_bus_acquired; the object would otherwise die only at exit).
    if (ctx->obj_reg_id && ctx->session_conn)
        g_dbus_connection_unregister_object(ctx->session_conn, ctx->obj_reg_id);
    if (ctx->bus_name_id) g_bus_unown_name(ctx->bus_name_id);
    if (ctx->session_conn) g_object_unref(ctx->session_conn);
    ctx->session_conn = nullptr;
}
