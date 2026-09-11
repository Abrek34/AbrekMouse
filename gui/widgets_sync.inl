// ── Widget ↔ profile synchronisation and GTK signal callbacks ────────────────

// R13-refactor: extracted from widgets_to_profile and profile_to_widgets
// to eliminate 15-line duplication.  Greys out pipeline widgets when raw
// passthrough is active.
static void update_raw_sensitivity(AppState* S, bool raw) {
    if (S->accel_params_frame) gtk_widget_set_sensitive(S->accel_params_frame, !raw);
    if (S->lut_frame)          gtk_widget_set_sensitive(S->lut_frame, !raw);
    if (S->mode_combo)         gtk_widget_set_sensitive(S->mode_combo, !raw);
    if (S->gain_check)         gtk_widget_set_sensitive(S->gain_check, !raw);
    if (S->xy_link_btn)        gtk_widget_set_sensitive(S->xy_link_btn, !raw);
    if (S->y_axis_frame)       gtk_widget_set_sensitive(S->y_axis_frame, !raw && !S->xy_linked);
    if (S->rotation_spin)      gtk_widget_set_sensitive(S->rotation_spin, !raw);
    if (S->snap_spin)          gtk_widget_set_sensitive(S->snap_spin, !raw);
    if (S->speed_min_spin)     gtk_widget_set_sensitive(S->speed_min_spin, !raw);
    if (S->speed_max_spin)     gtk_widget_set_sensitive(S->speed_max_spin, !raw);
    if (S->lr_ratio_spin)      gtk_widget_set_sensitive(S->lr_ratio_spin, !raw);
    if (S->ud_ratio_spin)      gtk_widget_set_sensitive(S->ud_ratio_spin, !raw);
    if (S->dist_mode_combo)    gtk_widget_set_sensitive(S->dist_mode_combo, !raw);
    if (S->lp_norm_spin)       gtk_widget_set_sensitive(S->lp_norm_spin, !raw);
    if (S->input_hl_spin)      gtk_widget_set_sensitive(S->input_hl_spin, !raw);
    if (S->scale_hl_spin)      gtk_widget_set_sensitive(S->scale_hl_spin, !raw);
    if (S->output_hl_spin)     gtk_widget_set_sensitive(S->output_hl_spin, !raw);
    // PAS-3: DPI resolution is an acceleration-pipeline element (it feeds
    // dpi_factor).  In raw passthrough it is not applied — the previously
    // missed widget would keep telling the user a value is live that isn't.
    if (S->output_dpi_spin)    gtk_widget_set_sensitive(S->output_dpi_spin, !raw);
}

// Which modes consume each curve parameter. Derived from the actual accel_args
// usage in include/accel-*.hpp (reference-aligned):
//   classic    → acceleration, exponent_classic, input_offset, cap.x/y, cap_mode
//   power      → scale, output_offset, exponent_power, cap.x/y, cap_mode
//   natural    → limit, decay_rate, input_offset, gain (LEGACY/GAIN variants)
//   jump       → cap.x (step position), cap.y (step amount), smooth
//   synchronous→ sync_speed, motivity, gamma, smooth
//   lookup     → LUT points only (edited on the graph)
//   noaccel    → nothing
// gain (LEGACY/GAIN switch) is meaningful for unchanged modes: classic
// (classic<GAIN>) and jump/synchronous (activation_framework<GAIN>); lookup
// gain toggles the velocity (y/x) interpretation.
static bool mode_uses(accel_mode m, std::initializer_list<accel_mode> modes) {
    for (accel_mode x : modes) if (m == x) return true;
    return false;
}

static void row_set_visible(GtkWidget* label, GtkWidget* widget, bool vis) {
    if (label)  gtk_widget_set_visible(label,  vis);
    if (widget) gtk_widget_set_visible(widget, vis);
}

// Show/hide the parameter rows so only the settings that apply to the selected
// mode are visible. Pipeline-level settings (rotation, snap, speed clamps,
// weights, DPI, speed processor) apply in every mode and are handled by
// update_raw_sensitivity() instead — they must NOT be hidden here.
static void update_mode_sensitivity(AppState* S) {
    if (!S->mode_combo || S->config.profiles.empty()) return;
    accel_mode mode = idx_to_mode((int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->mode_combo)));
    bool raw = cur_prof(S).prof.raw_passthrough;

    // Accel X parameter rows (index == grid row inside accel_params_frame)
    row_set_visible(S->accel_row_label[0],  S->accel_spin,         mode_uses(mode, {accel_mode::classic}));
    row_set_visible(S->accel_row_label[1],  S->exponent_spin,      mode_uses(mode, {accel_mode::classic}));
    row_set_visible(S->accel_row_label[2],  S->power_exp_spin,     mode_uses(mode, {accel_mode::power}));
    row_set_visible(S->accel_row_label[3],  S->limit_spin,         mode_uses(mode, {accel_mode::natural}));
    row_set_visible(S->accel_row_label[4],  S->offset_spin,        mode_uses(mode, {accel_mode::classic, accel_mode::natural}));
    row_set_visible(S->accel_row_label[5],  S->decay_spin,         mode_uses(mode, {accel_mode::natural}));
    row_set_visible(S->accel_row_label[6],  S->cap_x_spin,         mode_uses(mode, {accel_mode::classic, accel_mode::power, accel_mode::jump}));
    row_set_visible(S->accel_row_label[7],  S->cap_y_spin,         mode_uses(mode, {accel_mode::classic, accel_mode::power, accel_mode::jump}));
    row_set_visible(S->accel_row_label[8],  S->cap_mode_combo,     mode_uses(mode, {accel_mode::classic, accel_mode::power}));
    row_set_visible(S->accel_row_label[9],  S->sync_speed_spin,    mode_uses(mode, {accel_mode::synchronous}));
    row_set_visible(S->accel_row_label[10], S->smooth_spin,        mode_uses(mode, {accel_mode::jump, accel_mode::synchronous}));
    row_set_visible(S->accel_row_label[11], S->motivity_spin,      mode_uses(mode, {accel_mode::synchronous}));
    row_set_visible(S->accel_row_label[12], S->gamma_spin,         mode_uses(mode, {accel_mode::synchronous}));
    row_set_visible(S->accel_row_label[13], S->output_offset_spin, mode_uses(mode, {accel_mode::power}));
    row_set_visible(S->accel_row_label[14], S->scale_spin,         mode_uses(mode, {accel_mode::power}));

    // The Gain toggle switches between the LEGACY multiplier form and the
    // integral (output-speed) GAIN form. Power is not gain-capable yet, so it
    // stays disabled there; noaccel has no curve at all.
    bool gain_capable = !mode_uses(mode, {accel_mode::noaccel, accel_mode::power});
    if (S->gain_check)
        gtk_widget_set_sensitive(S->gain_check, !raw && gain_capable);

    // The curve frame is pointless when there are no curve parameters.
    bool show_frame = !mode_uses(mode, {accel_mode::noaccel, accel_mode::lookup});
    if (S->accel_params_frame) gtk_widget_set_visible(S->accel_params_frame, show_frame);
    // (lookup additionally toggles lut_frame via update_lut_visibility)

    // Y axis — same rule, driven by the Y mode combo when X/Y are unlinked.
    accel_mode ymode = S->xy_linked ? mode
        : idx_to_mode((int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->mode_combo_y)));
    row_set_visible(S->y_row_label[1], S->accel_spin_y,    mode_uses(ymode, {accel_mode::classic}));
    row_set_visible(S->y_row_label[2], S->exponent_spin_y, mode_uses(ymode, {accel_mode::classic}));
    row_set_visible(S->y_row_label[3], S->limit_spin_y,    mode_uses(ymode, {accel_mode::natural}));
    row_set_visible(S->y_row_label[4], S->offset_spin_y,   mode_uses(ymode, {accel_mode::classic, accel_mode::natural}));
    row_set_visible(S->y_row_label[5], S->cap_y_spin_y,    mode_uses(ymode, {accel_mode::classic, accel_mode::power, accel_mode::jump}));

    // Mode hint — restate which parameters this mode actually uses.
    if (S->mode_hint_lbl) {
        const char* hint = "";
        switch (mode) {
        case accel_mode::noaccel:     hint = "1:1 output — no curve parameters."; break;
        case accel_mode::classic:     hint = "Uses: Accel, Exp (cls), Input Offset, Cap X/Y, Cap Mode."; break;
        case accel_mode::power:       hint = "Uses: Scale, Exp (pwr), Output Offset, Cap X/Y, Cap Mode."; break;
        case accel_mode::natural:     hint = "Uses: Limit, Decay Rate, Input Offset, Gain."; break;
        case accel_mode::jump:        hint = "Uses: Cap X (step position), Cap Y (step amount), Smooth."; break;
        case accel_mode::synchronous: hint = "Uses: Sync Speed, Motivity, Gamma, Smooth."; break;
        case accel_mode::lookup:      hint = "Edit gain points on the curve (right panel)."; break;
        default: break;
        }
        gtk_label_set_markup(GTK_LABEL(S->mode_hint_lbl),
                             (std::string("<small>") + tr(hint) + "</small>").c_str());
    }
}

accel_mode idx_to_mode(int i) {
    static const accel_mode M[] = {
        accel_mode::noaccel, accel_mode::classic, accel_mode::power,
        accel_mode::natural, accel_mode::jump,    accel_mode::synchronous,
        accel_mode::lookup,
    };
    return (i >= 0 && i < 7) ? M[i] : accel_mode::noaccel;
}

int mode_to_idx(accel_mode m) {
    switch (m) {
    case accel_mode::classic:     return 1;
    case accel_mode::power:       return 2;
    case accel_mode::natural:     return 3;
    case accel_mode::jump:        return 4;
    case accel_mode::synchronous: return 5;
    case accel_mode::lookup:      return 6;
    default:                      return 0;
    }
}

cap_mode idx_to_cap(int i) {
    if (i == 1) return cap_mode::in;
    if (i == 2) return cap_mode::io;
    return cap_mode::out;
}
int cap_to_idx(cap_mode c) {
    if (c == cap_mode::in)  return 1;
    if (c == cap_mode::io)  return 2;
    return 0;
}

void widgets_to_profile(AppState* S) {
    if (S->updating || S->config.profiles.empty()) return;
    S->unsaved = true;
    auto& dp = cur_prof(S);
    auto& ax = dp.prof.accel_x;
    auto& ay = dp.prof.accel_y;

#define SPIN(w)  gtk_spin_button_get_value(GTK_SPIN_BUTTON(S->w))
#define CHECK(w) gtk_check_button_get_active(GTK_CHECK_BUTTON(S->w))
#define DD(w)    (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->w))

    ax.mode           = idx_to_mode(DD(mode_combo));
    ax.gain           = CHECK(gain_check);
    ax.acceleration   = SPIN(accel_spin);
    ax.exponent_classic = SPIN(exponent_spin);
    ax.exponent_power = SPIN(power_exp_spin);
    ax.limit          = SPIN(limit_spin);
    ax.input_offset   = SPIN(offset_spin);
    ax.decay_rate     = SPIN(decay_spin);
    ax.cap.x          = SPIN(cap_x_spin);
    ax.cap.y          = SPIN(cap_y_spin);
    ax.cap_mode_val   = idx_to_cap(DD(cap_mode_combo));
    ax.sync_speed     = SPIN(sync_speed_spin);
    ax.smooth         = SPIN(smooth_spin);
    ax.motivity       = SPIN(motivity_spin);
    ax.gamma          = SPIN(gamma_spin);
    ax.output_offset  = SPIN(output_offset_spin);
    ax.scale          = SPIN(scale_spin);

    if (S->xy_linked) {
        ay = ax;
    } else {
        // R13: copy fields that have no dedicated Y-axis widget from X,
        // so they stay in sync rather than going stale when Y is unlinked.
        ay.gain             = ax.gain;
        ay.cap_mode_val     = ax.cap_mode_val;
        ay.cap.x            = ax.cap.x;
        ay.exponent_power   = ax.exponent_power;
        ay.decay_rate       = ax.decay_rate;
        ay.scale            = ax.scale;
        ay.output_offset    = ax.output_offset;
        ay.motivity         = ax.motivity;
        ay.gamma            = ax.gamma;
        ay.smooth           = ax.smooth;
        ay.sync_speed       = ax.sync_speed;
        // Y-axis-specific widgets:
        ay.mode             = idx_to_mode(DD(mode_combo_y));
        ay.acceleration     = SPIN(accel_spin_y);
        ay.exponent_classic = SPIN(exponent_spin_y);
        ay.limit            = SPIN(limit_spin_y);
        ay.input_offset     = SPIN(offset_spin_y);
        ay.cap.y            = SPIN(cap_y_spin_y);
    }

    dp.prof.raw_passthrough     = CHECK(raw_check);
    dp.prof.degrees_rotation    = SPIN(rotation_spin);
    dp.prof.degrees_snap        = SPIN(snap_spin);
    dp.prof.speed_min           = SPIN(speed_min_spin);
    dp.prof.speed_max           = SPIN(speed_max_spin);
    dp.prof.output_dpi          = SPIN(output_dpi_spin);
    dp.prof.lr_output_dpi_ratio = SPIN(lr_ratio_spin);
    dp.prof.ud_output_dpi_ratio = SPIN(ud_ratio_spin);
    dp.dev_cfg.dpi              = (int)SPIN(dpi_spin);
    dp.dev_cfg.polling_rate     = (int)SPIN(polling_spin);

    // Speed processor args
    {
        auto& sp = dp.prof.speed_processor_args;
        int dist_sel = DD(dist_mode_combo);
        // 0=Euclidean, 1=Max, 2=Lp, 3=Separate
        if      (dist_sel == 3) { sp.whole = false; }
        else if (dist_sel == 1) { sp.whole = true;  sp.lp_norm = 9999; }
        else if (dist_sel == 2) { sp.whole = true;  sp.lp_norm = SPIN(lp_norm_spin); }
        else                    { sp.whole = true;  sp.lp_norm = 2; } // euclidean
        sp.input_speed_smooth_halflife  = SPIN(input_hl_spin);
        sp.scale_smooth_halflife        = SPIN(scale_hl_spin);
        sp.output_speed_smooth_halflife = SPIN(output_hl_spin);
        // Show/hide lp_norm row
        bool show_lp = (dist_sel == 2);
        if (S->lp_norm_label) gtk_widget_set_visible(S->lp_norm_label, show_lp);
        if (S->lp_norm_spin)  gtk_widget_set_visible(S->lp_norm_spin,  show_lp);
    }

    // device_id: index 0 = "All devices" (empty string), 1+ = mice_list[idx-1].stable_id
    // Prefer stable_id ("usb:VVVV:PPPP:serial") over event_node for reboot stability.
    if (S->device_id_combo) {
        int sel = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->device_id_combo));
        if (sel <= 0 || sel - 1 >= (int)S->mice_list.size()) {
            // "All devices" selected (or invalid index).
            // Clear the binding ONLY when the stored device is currently listed
            // (user deliberately chose "All devices" while the mouse is present).
            // When the stored device is NOT in the list (unplugged), the combo
            // was forced to index 0 — keep the binding so it re-attaches on
            // re-plug.
            bool stored_device_present = false;
            if (!dp.device_id.empty()) {
                for (auto& m : S->mice_list) {
                    std::string sid = m.stable_id.empty() ? m.event_node : m.stable_id;
                    if (sid == dp.device_id) { stored_device_present = true; break; }
                }
            }
            if (stored_device_present) dp.device_id = "";
        } else {
            auto& m = S->mice_list[sel - 1];
            dp.device_id = m.stable_id.empty() ? m.event_node : m.stable_id;
        }
    }

// P-APP: app match entry → match_app (trimmed, lowercased by the daemon's
    // ascii_icontains; keep it store-friendly and cap at the JSON limit).
    if (S->match_app_entry) {
        const gchar* txt = gtk_editable_get_text(GTK_EDITABLE(S->match_app_entry));
        std::string app = txt ? txt : "";
        // trim leading/trailing whitespace
        while (!app.empty() && std::isspace((unsigned char)app.back())) app.pop_back();
        size_t b = 0; while (b < app.size() && std::isspace((unsigned char)app[b])) ++b;
        app = app.substr(b);
        if (app.size() > 128) app.resize(128);
        dp.match_app = app;
    }

#undef SPIN
#undef CHECK
#undef DD

    update_raw_sensitivity(S, dp.prof.raw_passthrough);

    gtk_widget_queue_draw(S->graph_area);
}

void profile_to_widgets(AppState* S) {
    if (S->config.profiles.empty()) return;
    S->updating = true;
    auto& dp = cur_prof(S);
    auto& ax = dp.prof.accel_x;
    auto& ay = dp.prof.accel_y;

#define SET_SPIN(w, v)  gtk_spin_button_set_value(GTK_SPIN_BUTTON(S->w), v)
#define SET_DD(w, v)    gtk_drop_down_set_selected(GTK_DROP_DOWN(S->w), v)
#define SET_CHK(w, v)   gtk_check_button_set_active(GTK_CHECK_BUTTON(S->w), v)

    SET_DD(mode_combo,      mode_to_idx(ax.mode));
    SET_CHK(gain_check,     ax.gain);
    SET_SPIN(accel_spin,    ax.acceleration);
    SET_SPIN(exponent_spin, ax.exponent_classic);
    SET_SPIN(power_exp_spin,ax.exponent_power);
    SET_SPIN(limit_spin,    ax.limit);
    SET_SPIN(offset_spin,   ax.input_offset);
    SET_SPIN(decay_spin,    ax.decay_rate);
    SET_SPIN(cap_x_spin,    ax.cap.x);
    SET_SPIN(cap_y_spin,    ax.cap.y);
    SET_DD(cap_mode_combo,  cap_to_idx(ax.cap_mode_val));
    SET_SPIN(sync_speed_spin, ax.sync_speed);
    SET_SPIN(smooth_spin,   ax.smooth);
    SET_SPIN(motivity_spin,      ax.motivity);
    SET_SPIN(gamma_spin,         ax.gamma);
    SET_SPIN(output_offset_spin, ax.output_offset);
    SET_SPIN(scale_spin,         ax.scale);

    // Y axis — update both the checkbox and the frame together.
    // P157: `same = (ax == ay)` must NOT blindly overwrite the user's live
    // choice.  If the profile happens to carry equal values (unlinked but
    // never diverged yet), re-deriving xy_linked = same silently re-links the
    // axes and re-disables the Y widgets the user just unlocked.  Only force
    // "unlinked" when the profile actually holds distinct values; a truly
    // identical pair keeps whatever state the user last selected.
    bool same = (ax == ay);
    if (!same) S->xy_linked = false;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(S->xy_link_btn), S->xy_linked);
    gtk_widget_set_sensitive(S->y_axis_frame, !S->xy_linked);

    SET_DD(mode_combo_y,      mode_to_idx(ay.mode));
    SET_SPIN(accel_spin_y,    ay.acceleration);
    SET_SPIN(exponent_spin_y, ay.exponent_classic);
    SET_SPIN(limit_spin_y,    ay.limit);
    SET_SPIN(offset_spin_y,   ay.input_offset);
    SET_SPIN(cap_y_spin_y,    ay.cap.y);

    SET_CHK(raw_check,        dp.prof.raw_passthrough);
    SET_SPIN(rotation_spin,   dp.prof.degrees_rotation);
    SET_SPIN(snap_spin,       dp.prof.degrees_snap);
    SET_SPIN(speed_min_spin,  dp.prof.speed_min);
    SET_SPIN(speed_max_spin,  dp.prof.speed_max);
    SET_SPIN(output_dpi_spin, dp.prof.output_dpi);
    SET_SPIN(lr_ratio_spin,   dp.prof.lr_output_dpi_ratio);
    SET_SPIN(ud_ratio_spin,   dp.prof.ud_output_dpi_ratio);
    SET_SPIN(dpi_spin,        dp.dev_cfg.dpi);
    SET_SPIN(polling_spin,    dp.dev_cfg.polling_rate);

    // Speed processor
    {
        auto& sp = dp.prof.speed_processor_args;
        int dist_idx;
        if (!sp.whole)                              dist_idx = 3; // separate
        else if (sp.lp_norm >= 16 || sp.lp_norm <= 0) dist_idx = 1; // max
        else if (std::fabs(sp.lp_norm - 2.0) > 1e-9)  dist_idx = 2; // lp
        else                                        dist_idx = 0; // euclidean
        SET_DD(dist_mode_combo, (guint)dist_idx);
        SET_SPIN(lp_norm_spin,  sp.lp_norm > 0 ? sp.lp_norm : 2.0);
        SET_SPIN(input_hl_spin,  sp.input_speed_smooth_halflife);
        SET_SPIN(scale_hl_spin,  sp.scale_smooth_halflife);
        SET_SPIN(output_hl_spin, sp.output_speed_smooth_halflife);
        bool show_lp2 = (dist_idx == 2);
        if (S->lp_norm_label) gtk_widget_set_visible(S->lp_norm_label, show_lp2);
        if (S->lp_norm_spin)  gtk_widget_set_visible(S->lp_norm_spin,  show_lp2);
    }

    // device_id dropdown: match on stable_id or event_node; R2-03 handles a
    // bound-but-unplugged device (trailing "(unplugged)" entry) instead of the
    // "All devices" fallback.
    if (S->device_id_combo)
        gtk_drop_down_set_selected(GTK_DROP_DOWN(S->device_id_combo),
                                   (guint)device_combo_select(S, dp.device_id));

    // P-APP: match_app entry (only when not mid-update to avoid clearing user input)
    if (S->match_app_entry)
        gtk_editable_set_text(GTK_EDITABLE(S->match_app_entry),
                              dp.match_app.c_str());

#undef SET_SPIN
#undef SET_DD
#undef SET_CHK

    S->updating = false;
    update_raw_sensitivity(S, dp.prof.raw_passthrough);
    update_mode_sensitivity(S); // show/hide mode-specific parameters
    update_lut_visibility(S); // show the correct panel when a profile is loaded

    gtk_widget_queue_draw(S->graph_area);

    // Warn if the loaded LUT had more points than the buffer capacity.
    // This can happen when importing a config created with a future version that
    // raised LUT_POINTS_CAPACITY, or when a hand-edited JSON has excess data.
    {
        auto& ax = cur_prof(S).prof.accel_x;
        auto& ay = cur_prof(S).prof.accel_y;
        bool over_x = ax.mode == accel_mode::lookup &&
                      ax.length / 2 > (int)LUT_POINTS_CAPACITY;
        bool over_y = !S->xy_linked && ay.mode == accel_mode::lookup &&
                      ay.length / 2 > (int)LUT_POINTS_CAPACITY;
        if (over_x || over_y) {
            std::string axis = (over_x && over_y) ? "X and Y" : (over_x ? "X" : "Y");
            set_status(S, trf("Warning: LUT (%s axis) was truncated to %d points (max capacity).",
                              axis.c_str(), (int)LUT_POINTS_CAPACITY));
        }
    }
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

static bool has_systemd_rawaccel_unit() {
    return fs::exists("/etc/systemd/system/rawaccel.service") ||
           fs::exists("/usr/lib/systemd/system/rawaccel.service");
}

// R2-06 / R4-L-11: pkexec could previously fail silently — a missing pkexec
// (exec → _exit(127)), a polkit auth cancel/refusal or an exec-permission
// error (126) was reported to the user as "Starting.../Stopping..." success.
// The child is reaped event-driven (no polling), and a non-zero wait status is
// surfaced in the status bar with the pkexec-specific meaning.
struct pkexec_watch_ctx {
    AppState* S;
    std::string what; // human action, e.g. "systemctl start rawaccel"
};

static void pkexec_child_report(GPid p, gint status, gpointer d) {
    auto* ctx = static_cast<pkexec_watch_ctx*>(d);
    if (ctx && ctx->S) {
        std::string detail;
        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code != 0) {
                detail = (code == 126)
                    ? tr("pkexec could not execute the command (126).")
                    : (code == 127)
                    ? tr("pkexec refused or the command was not found (127).")
                    : trf("pkexec exited with code %d.", code);
            }
        } else if (WIFSIGNALED(status)) {
            detail = trf("pkexec was terminated by signal %d.", WTERMSIG(status));
        }
        if (!detail.empty())
            set_status(ctx->S, ctx->what + " — " + detail);
    }
    delete ctx;
    g_spawn_close_pid(p);
}

/// fork+exec hijacking pkexec; child is watchdogged via the GLib SIGCHLD
/// mechanism.  Returns the child pid (>0) on success, -1 on fork failure.
static pid_t pkexec_spawn(char* const argv[], AppState* S, std::string what) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setsid();
        execvp(argv[0], argv);
        _exit(127); // execvp failed (e.g. pkexec not installed)
    }
    auto* ctx = new pkexec_watch_ctx{S, std::move(what)};
    g_child_watch_add(pid, pkexec_child_report, ctx);
    return pid;
}

static bool pkexec_systemctl_async(const char* action, AppState* S, std::string* err_out = nullptr) {
    char* args[] = {
        const_cast<char*>("pkexec"),
        const_cast<char*>("systemctl"),
        const_cast<char*>(action),
        const_cast<char*>("rawaccel"),
        nullptr
    };
    std::string what = std::string("systemctl ") + action + " rawaccel";
    pid_t pid = pkexec_spawn(args, S, what);
    if (pid < 0) {
        if (err_out) *err_out = "fork() failed.";
        return false;
    }
    if (err_out) err_out->clear();
    return pid > 0;
}

void on_param_changed(GtkWidget*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    widgets_to_profile(S);
    update_lut_visibility(S); // show/hide the LUT panel when the mode changes
    update_mode_sensitivity(S); // show/hide mode-specific parameters
}

// "notify::<prop>" signals use a 3-argument callback signature
// (GObject*, GParamSpec*, gpointer). Calling the 2-arg on_param_changed
// directly via G_CALLBACK corrupts user_data (it receives the GParamSpec*
// instead of AppState*), causing SIGSEGV on dropdown changes (Mode/Mouse).
void on_notify_param_changed(GObject*, GParamSpec*, gpointer user_data) {
    on_param_changed(nullptr, user_data);
}

void on_profile_changed(GtkDropDown* dd, GParamSpec*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->updating) return; // blocked during rebuild_profile_combo
    int idx = (int)gtk_drop_down_get_selected(dd);
    if (idx < 0 || idx >= (int)S->config.profiles.size()) return;

    // Warn in the status bar if switching away from a profile with unsaved changes.
    // A modal dialog here would be too disruptive for a dropdown switch.
    if (S->unsaved) {
        set_status(S, trf("Warning: unsaved changes to \"%s\" were discarded.",
                          cur_prof(S).name.c_str()));
        S->unsaved = false;
    }

    S->current_profile_idx = idx;
    S->config.active_profile = cur_prof(S).name;
    // Reset zoom/pan on profile change — different profiles may have different speed ranges
    S->graph_zoom  = 1.0;
    S->graph_pan_x = 0.0;
    profile_to_widgets(S);
}

void on_xy_link_toggled(GtkCheckButton* btn, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->updating) return; // block spurious triggers from profile_to_widgets programmatic sets
    S->xy_linked = gtk_check_button_get_active(btn);
    gtk_widget_set_sensitive(S->y_axis_frame, !S->xy_linked);
    widgets_to_profile(S);
}

// L-BUG-30: on_save_clicked / on_apply_clicked were ~30 identical lines.
// Shared "Save As" flow: asks for a name, upserts the profile, persists it.
// save_config_now() auto-sends SIGHUP when the daemon is running, so both
// handlers end with the same persist call.
//
// R2-05: saving under a name that belongs to a DIFFERENT profile would
// silently overwrite that profile's on-disk settings (through Apply too, since
// it shares this dialog).  Overwrite now requires an explicit confirmation; a
// plain "save under my own current name" stays confirmation-free.
static void save_profile_as_dialog(AppState* S) {
    std::string cur_name = S->config.profiles.empty() ? "" : cur_prof(S).name;
    show_input_dialog(S, tr("Save Profile As"), tr("Profile name"), cur_name.c_str(),
        [S](const std::string& name) {
            if (name.empty()) return;
            widgets_to_profile(S);
            device_profile dp = cur_prof(S);
            dp.name = name;

            int existing = -1;
            for (int i = 0; i < (int)S->config.profiles.size(); ++i) {
                if (S->config.profiles[i].name == name) { existing = i; break; }
            }

            // Name already taken by a DIFFERENT profile → ask before overwriting.
            if (existing >= 0 && existing != S->current_profile_idx) {
                GtkWidget* dlg = gtk_window_new();
                gtk_window_set_title(GTK_WINDOW(dlg), tr("Overwrite Profile"));
                gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(S->window));
                gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
                gtk_window_set_default_size(GTK_WINDOW(dlg), 340, -1);

                GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
                gtk_widget_set_margin_start(vbox, 16); gtk_widget_set_margin_end(vbox, 16);
                gtk_widget_set_margin_top(vbox, 16);   gtk_widget_set_margin_bottom(vbox, 16);
                gtk_window_set_child(GTK_WINDOW(dlg), vbox);

                std::string msg = trf(
                    "Profile \"%s\" already exists.\nIts settings will be "
                    "permanently replaced with the current settings.\nContinue?",
                    name.c_str());
                GtkWidget* lbl = gtk_label_new(msg.c_str());
                gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
                gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_CENTER);
                gtk_box_append(GTK_BOX(vbox), lbl);

                GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_widget_set_halign(hbox, GTK_ALIGN_END);
                gtk_box_append(GTK_BOX(vbox), hbox);

                GtkWidget* cancel_btn = gtk_button_new_with_label(tr("Cancel"));
                GtkWidget* ov_btn     = gtk_button_new_with_label(tr("Overwrite"));
                gtk_widget_add_css_class(ov_btn, "destructive-action");
                gtk_box_append(GTK_BOX(hbox), cancel_btn);
                gtk_box_append(GTK_BOX(hbox), ov_btn);

                g_signal_connect(cancel_btn, "clicked",
                    G_CALLBACK(+[](GtkWidget*, gpointer d){ gtk_window_destroy(GTK_WINDOW(d)); }), dlg);

                struct overwrite_ctx { AppState* S; device_profile dp; int existing; };
                auto* ctx = new overwrite_ctx{ S, dp, existing };
                g_object_set_data_full(G_OBJECT(dlg), "ctx", ctx,
                    [](gpointer p) { delete (overwrite_ctx*)p; });
                g_signal_connect(ov_btn, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                    auto* dlg_w  = GTK_WIDGET(d);
                    auto* c      = static_cast<overwrite_ctx*>(g_object_get_data(G_OBJECT(dlg_w), "ctx"));
                    c->S->config.profiles[c->existing] = c->dp;
                    c->S->current_profile_idx = c->existing;
                    c->S->config.active_profile = c->dp.name;
                    rebuild_profile_combo(c->S);
                    save_config_now(c->S);
                    gtk_window_destroy(GTK_WINDOW(d));
                }), dlg);

                gtk_window_present(GTK_WINDOW(dlg));
                return;
            }

            // No conflict: overwrite self under the same name, or create new.
            if (existing >= 0) {
                S->config.profiles[existing] = dp;
                S->current_profile_idx = existing;
            } else {
                S->config.profiles.push_back(dp);
                S->current_profile_idx = (int)S->config.profiles.size() - 1;
            }
            S->config.active_profile = name;
            rebuild_profile_combo(S);
            save_config_now(S);
        });
}

void on_save_clicked(GtkButton*, gpointer user_data) {
    save_profile_as_dialog(static_cast<AppState*>(user_data));
}

void on_apply_clicked(GtkButton*, gpointer user_data) {
    // Apply & Reload: same Save As logic, then reload the daemon.
    // save_config_now() auto-sends SIGHUP when daemon is running.
    save_profile_as_dialog(static_cast<AppState*>(user_data));
}

void on_daemon_start(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (has_systemd_rawaccel_unit()) {
        std::string err;
        if (pkexec_systemctl_async("start", S, &err)) {
            g_timeout_add(1000, [](gpointer p) -> gboolean {
                update_daemon_status(static_cast<AppState*>(p));
                return G_SOURCE_REMOVE;
            }, S);
            set_status(S, tr("Starting daemon via systemd..."));
            return;
        }
        set_status(S, std::string(tr(err.c_str())) + tr(" Falling back to direct daemon start..."));
    }

    // Candidate paths in preference order
    static const char* candidates[] = {
        "/usr/local/bin/rawaccel-daemon",
        "/usr/bin/rawaccel-daemon",
        nullptr
    };
    // Also check next to the GUI binary (same dir)
    std::string self_dir;
    {
        char buf[1024] = {};
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            self_dir = fs::path(std::string(buf, n)).parent_path().string();
        }
    }

    std::string daemon_path;
    for (int i = 0; candidates[i]; i++) {
        if (fs::exists(candidates[i])) { daemon_path = candidates[i]; break; }
    }
    if (daemon_path.empty() && !self_dir.empty()) {
        std::string p = self_dir + "/rawaccel-daemon";
        if (fs::exists(p)) daemon_path = p;
    }
    if (daemon_path.empty()) {
        set_status(S, tr("rawaccel-daemon not found. Install with: sudo scripts/install.sh"));
        return;
    }

    // Use fork+exec to avoid shell injection — never pass paths through sh -c.
    // R2-06: route through the shared prijit reporter so a cancelled/refused
    // pkexec or a missing executable surfaces as a failure, not a lie.
    {
        char* args[] = {
            const_cast<char*>("pkexec"),
            const_cast<char*>(daemon_path.c_str()),
            const_cast<char*>("-v"),
            const_cast<char*>("-c"),
            const_cast<char*>(S->config_path.c_str()),
            nullptr
        };
        pid_t pid = pkexec_spawn(args, S,
            trf("pkexec rawaccel-daemon %s", S->config_path.c_str()));
        if (pid < 0) {
            set_status(S, tr("fork() failed."));
            return;
        }
    }
    // Update daemon status after a short delay (daemon needs time to start)
    g_timeout_add(1500, [](gpointer p) -> gboolean {
        update_daemon_status(static_cast<AppState*>(p));
        return G_SOURCE_REMOVE;
    }, S);
    set_status(S, tr("Starting daemon..."));
}

void on_daemon_stop(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (has_systemd_rawaccel_unit()) {
        std::string err;
        if (pkexec_systemctl_async("stop", S, &err)) {
            g_timeout_add(800, [](gpointer p) -> gboolean {
                update_daemon_status(static_cast<AppState*>(p));
                return G_SOURCE_REMOVE;
            }, S);
            set_status(S, tr("Stopping daemon via systemd..."));
            return;
        }
    }
    std::string err;
    if (!daemon_send_signal(SIGTERM, &err)) {
        set_status(S, tr(err.c_str()));
        return;
    }
    g_timeout_add(800, [](gpointer p) -> gboolean {
        update_daemon_status(static_cast<AppState*>(p));
        return G_SOURCE_REMOVE;
    }, S);
    set_status(S, tr("Stopping daemon..."));
}

void on_daemon_reload(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    // Prefer IPC reload (works without signal permissions); fall back to SIGHUP.
    std::string ipc_resp = daemon_ipc_query("reload");
    if (!ipc_resp.empty() && ipc_resp.find("ok") != std::string::npos) {
        set_status(S, tr("Daemon reloaded (IPC)."));
        update_daemon_status(S);
        return;
    }
    std::string err;
    if (!daemon_send_signal(SIGHUP, &err)) {
        if (has_systemd_rawaccel_unit()) {
            std::string serr;
            if (pkexec_systemctl_async("reload", S, &serr)) {
                // pkexec runs asynchronously — the polkit prompt may still be
                // open, so never claim success here.  The pkexec watchdog
                // (pkexec_child_report) surfaces any failure in the status bar;
                // refresh the daemon state only after the subprocess settles.
                set_status(S, tr("Requesting daemon reload via systemd..."));
                g_timeout_add(1800, [](gpointer p) -> gboolean {
                    update_daemon_status(static_cast<AppState*>(p));
                    return G_SOURCE_REMOVE;
                }, S);
                return;
            }
            set_status(S, std::string(tr(err.c_str())) + " | " + tr(serr.c_str()));
            return;
        } else {
            set_status(S, tr(err.c_str()));
            return;
        }
    }
    set_status(S, tr("Daemon reloaded (SIGHUP)."));
    update_daemon_status(S);
}

// ── Latency stats view ────────────────────────────────────────────────────────

/// Format a latency figure with 2 decimals ('' '.' decimal separator — C++
/// streams keep the "C" locale even after setlocale(LC_ALL, "")).
static std::string fmt_us(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << v;
    return os.str();
}

/// "Performans" button in the status bar: read the daemon's latency snapshot
/// and show Avg/p50/p95/p99/Max — from the ACTIVE PROFILE's device slice
/// (daemon_device_field, Bug-02) instead of the first device in the array.
/// The daemon status_json exposes these per-device µs stats (histogram bucket
/// midpoints); a Min is not part of the snapshot, so Max stands in as the tail
/// bound. Non-blocking: the IPC query itself carries a 150 ms socket timeout.
void on_perf_clicked(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (!daemon_running()) {
        set_status(S, tr("Daemon is not running."));
        return;
    }
    std::string resp = daemon_ipc_query("status");
    double samples = daemon_device_field(resp, S, "lat_samples");
    if (samples <= 0) {
        gtk_label_set_text(GTK_LABEL(S->latency_lbl),
            tr("Latency: no samples recorded yet."));
        return;
    }
    double avg = daemon_device_field(resp, S, "lat_avg_us");
    double p50 = daemon_device_field(resp, S, "lat_p50_us");
    double p95 = daemon_device_field(resp, S, "lat_p95_us");
    double p99 = daemon_device_field(resp, S, "lat_p99_us");
    double mx  = daemon_device_field(resp, S, "lat_max_us");
    gtk_label_set_text(GTK_LABEL(S->latency_lbl),
        trf("Latency: Avg %s · p50 %s · p95 %s · p99 %s · Max %s µs",
            fmt_us(avg).c_str(), fmt_us(p50).c_str(), fmt_us(p95).c_str(),
            fmt_us(p99).c_str(), fmt_us(mx).c_str()).c_str());
}
