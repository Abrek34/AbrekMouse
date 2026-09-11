// ── UI construction: layout helpers, build_ui(), window-close, on_activate() ──

// Forward declarations for KDE helpers defined later in this file (after on_activate)
static void on_kde_fix_clicked(GtkButton*, gpointer);
static void update_kde_warn_bar(AppState*);
static void on_kde_open_settings(GtkWidget*, gpointer);

GtkWidget* make_spin(double mn, double mx, double step, double val, int digits = 3) {
    GtkWidget* s = gtk_spin_button_new_with_range(mn, mx, step);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s), digits);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), val);
    gtk_widget_set_hexpand(s, TRUE);
    return s;
}

void connect_spin(GtkWidget* s, gpointer user_data) {
    g_signal_connect(s, "value-changed", G_CALLBACK(on_param_changed), user_data);
}

void grid_row(GtkWidget* grid, int row, const char* label, GtkWidget* w) {
    GtkWidget* lbl = gtk_label_new(tr(label));
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_set_margin_end(lbl, 6);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w,   1, row, 1, 1);
    tr_register(lbl, TR_LABEL, label);
}

// grid_row variant that also stores the generated label, so the row can be
// shown/hidden together with its widget when the accel mode changes.
void grid_row2(GtkWidget* grid, int row, const char* label, GtkWidget* w, GtkWidget** label_out) {
    GtkWidget* lbl = gtk_label_new(tr(label));
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_set_margin_end(lbl, 6);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w,   1, row, 1, 1);
    if (label_out) *label_out = lbl;
    tr_register(lbl, TR_LABEL, label);
}

GtkWidget* make_section(const char* markup_title) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* lbl = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(lbl), tr(markup_title));
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_set_margin_top(lbl, 10);
    gtk_box_append(GTK_BOX(box), lbl);
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);
    tr_register(lbl, TR_MARKUP, markup_title);
    return box;
}

GtkWidget* make_grid() {
    GtkWidget* g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 8);
    gtk_grid_set_row_spacing(GTK_GRID(g), 4);
    gtk_widget_set_margin_start(g, 4);
    return g;
}

// ── Build UI ──────────────────────────────────────────────────────────────────

// Forward declaration — defined below (after on_activate)
gboolean on_window_close_request(GtkWindow*, gpointer);

// inotify hot-plug callback — forward declaration (defined in devices.inl)
gboolean on_inotify_event(GIOChannel*, GIOCondition, gpointer);
gboolean on_hidraw_inotify_event(GIOChannel*, GIOCondition, gpointer);

void build_ui(AppState* S, GtkApplication* gapp) {
    S->window = gtk_application_window_new(gapp);
    gtk_window_set_title(GTK_WINDOW(S->window),
                         (std::string("RawAccel Linux  v") + RAWACCEL_VERSION).c_str());
    gtk_window_set_default_size(GTK_WINDOW(S->window), 1020, 680);

    // ── Header bar ────────────────────────────────────────────────────────────
    GtkWidget* hbar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(S->window), hbar);

    // Profile combo
    {
        GtkStringList* sl = gtk_string_list_new(nullptr);
        // Same ★-marking as rebuild_profile_combo() so the very first paint is
        // consistent with every later rebuild (BUG-13: initial model used plain
        // names while any subsequent refresh marked the active profile).
        for (auto& p : S->config.profiles) {
            std::string label = p.name;
            if (p.name == S->config.active_profile)
                label = "\xe2\x98\x85 " + p.name; // UTF-8 ★
            gtk_string_list_append(sl, label.c_str());
        }
        S->profile_combo = gtk_drop_down_new(G_LIST_MODEL(sl), nullptr);
        g_object_unref(sl); // GUI-D1: local model ref must be dropped
        // Select the persisted active profile (set before the notify::selected
        // handler is connected so no callback fires on a half-built UI).
        gtk_drop_down_set_selected(GTK_DROP_DOWN(S->profile_combo),
                                   (guint)S->current_profile_idx);
        g_signal_connect(S->profile_combo, "notify::selected",
                         G_CALLBACK(on_profile_changed), S);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(hbar), S->profile_combo);
    }

    // Profile buttons
    struct { const char* icon; GCallback cb; const char* tip; } pbts[] = {
        {"list-add-symbolic",        G_CALLBACK(on_new_profile),       "New profile"},
        {"edit-copy-symbolic",       G_CALLBACK(on_duplicate_profile), "Duplicate profile"},
        {"document-edit-symbolic",   G_CALLBACK(on_rename_profile),    "Rename profile"},
        {"edit-clear-symbolic",      G_CALLBACK(on_reset_profile),     "Reset to defaults"},
        {"edit-delete-symbolic",     G_CALLBACK(on_delete_profile),    "Delete profile"},
        {"document-import-symbolic", G_CALLBACK(on_import_profile),    "Import profile"},
        {"document-save-symbolic",   G_CALLBACK(on_export_profile),    "Export profile"},
    };
    for (auto& b : pbts) {
        GtkWidget* btn = gtk_button_new_from_icon_name(b.icon);
        trtip(btn, b.tip);
        g_signal_connect(btn, "clicked", b.cb, S);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(hbar), btn);
    }

    // Right side: daemon controls + save/apply + language selector
    S->daemon_status = trlbl("● Checking...");
    gtk_widget_set_margin_end(S->daemon_status, 8);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), S->daemon_status);

    // Language selector — items stay untranslated so the control always reads
    // "Auto (locale) / English / Türkçe" regardless of the active language.
    {
        static const char* LANG_KEYS[] = {"Auto (locale)", "English", "Türkçe", nullptr};
        GtkStringList* lsl = gtk_string_list_new(nullptr);
        // Deliberately NOT tr(): the model is built once at startup and never
        // rebuilt on language switch, so translating here would make the entry
        // language-dependent (and stale after switching) — the control must
        // always read "Auto (locale) / English / Türkçe".
        for (int i = 0; LANG_KEYS[i]; i++) gtk_string_list_append(lsl, LANG_KEYS[i]);
        S->lang_combo = gtk_drop_down_new(G_LIST_MODEL(lsl), nullptr);
        g_object_unref(lsl);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(S->lang_combo), (guint)(S->lang_override + 1));
        g_signal_connect(S->lang_combo, "notify::selected",
                         G_CALLBACK(on_lang_changed), S);
        gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), S->lang_combo);
    }

    // Daemon control buttons — store pointers in AppState
    // so update_daemon_status() can set the correct sensitivity
    S->daemon_reload_btn = trbtn("Reload");
    g_signal_connect(S->daemon_reload_btn, "clicked", G_CALLBACK(on_daemon_reload), S);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), S->daemon_reload_btn);

    S->daemon_stop_btn = trbtn("Stop");
    g_signal_connect(S->daemon_stop_btn, "clicked", G_CALLBACK(on_daemon_stop), S);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), S->daemon_stop_btn);

    S->daemon_start_btn = trbtn("Start");
    gtk_widget_add_css_class(S->daemon_start_btn, "suggested-action");
    g_signal_connect(S->daemon_start_btn, "clicked", G_CALLBACK(on_daemon_start), S);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), S->daemon_start_btn);

    S->apply_btn = trbtn("Apply & Reload");
    gtk_widget_add_css_class(S->apply_btn, "suggested-action");
    g_signal_connect(S->apply_btn, "clicked", G_CALLBACK(on_apply_clicked), S);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), S->apply_btn);

    GtkWidget* save_btn = trbtn("Save");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), S);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), save_btn);

    // ── Main split ────────────────────────────────────────────────────────────
    GtkWidget* outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(S->window), outer_vbox);

    GtkWidget* hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(hpaned, TRUE);
    gtk_paned_set_position(GTK_PANED(hpaned), 360);
    gtk_box_append(GTK_BOX(outer_vbox), hpaned);

    // ── LEFT PANEL ────────────────────────────────────────────────────────────
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 330, -1);
    gtk_paned_set_start_child(GTK_PANED(hpaned), scroll);
    gtk_paned_set_shrink_start_child(GTK_PANED(hpaned), FALSE);

    GtkWidget* lvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(lvbox, 12);
    gtk_widget_set_margin_end(lvbox, 12);
    gtk_widget_set_margin_top(lvbox, 6);
    gtk_widget_set_margin_bottom(lvbox, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), lvbox);

    auto append_section = [&](const char* markup) {
        gtk_box_append(GTK_BOX(lvbox), make_section(markup));
    };
    auto append_grid = [&]() -> GtkWidget* {
        GtkWidget* g = make_grid();
        gtk_box_append(GTK_BOX(lvbox), g);
        return g;
    };

    // ── Raw Passthrough ───────────────────────────────────────────────────────
    S->raw_check = trchk("Raw Passthrough (bypass all acceleration)");
    trtip(S->raw_check,
        "When enabled, the entire acceleration pipeline is bypassed.\n"
        "No rotation, snap, speed clamp, weights, or sub-pixel accumulation —\n"
        "raw kernel counts are written directly to uinput (1:1 passthrough).");
    gtk_widget_set_margin_top(S->raw_check, 6);
    g_signal_connect(S->raw_check, "toggled", G_CALLBACK(on_param_changed), S);
    gtk_box_append(GTK_BOX(lvbox), S->raw_check);

    // ── Acceleration X ────────────────────────────────────────────────────────
    append_section("<b>Acceleration — X Axis</b>");
    GtkWidget* ag = append_grid();

    const char* mode_names[] = {
        "None (1:1)", "Classic", "Power", "Natural", "Jump", "Synchronous",
        "Lookup (LUT)", nullptr };
    S->mode_combo = gtk_drop_down_new(nullptr, nullptr);
    tr_combo_fill(S->mode_combo, mode_names);
    gtk_widget_set_hexpand(S->mode_combo, TRUE);
    g_signal_connect(S->mode_combo, "notify::selected", G_CALLBACK(on_notify_param_changed), S);
    grid_row(ag, 0, "Mode:", S->mode_combo);

    S->gain_check = trchk("Gain mode (recommended)");
    g_signal_connect(S->gain_check, "toggled", G_CALLBACK(on_param_changed), S);
    gtk_grid_attach(GTK_GRID(ag), S->gain_check, 0, 1, 2, 1);
    trtip(S->gain_check,
        "Classic/Jump/Natural/Synchronous: use the integral (output-speed) form of the curve.\n"
        "Lookup: gain points are treated as output speeds (gain = y / speed).");

    // Mode hint — a one-line note on which params the selected mode actually uses
    S->mode_hint_lbl = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(S->mode_hint_lbl), 0.0);
    gtk_label_set_wrap(GTK_LABEL(S->mode_hint_lbl), TRUE);
    gtk_widget_set_margin_top(S->mode_hint_lbl, 2);
    gtk_grid_attach(GTK_GRID(ag), S->mode_hint_lbl, 0, 2, 2, 1);

    S->accel_spin      = make_spin(0,    20,    0.001, 0.005);
    S->exponent_spin   = make_spin(1,    10,    0.05,  2.0);
    S->power_exp_spin  = make_spin(0.01, 5,     0.01,  0.05);
    S->limit_spin      = make_spin(0.1,  100,   0.05,  1.5);
    S->offset_spin     = make_spin(0,    100,   0.5,   0.0);
    S->decay_spin      = make_spin(0,    10,    0.01,  0.1);
    S->cap_x_spin      = make_spin(0,    500,   1,     15, 0);
    S->cap_y_spin      = make_spin(0,    100,   0.05,  1.5);
    S->sync_speed_spin = make_spin(0.1,  100,   0.5,   5.0);
    S->smooth_spin     = make_spin(0,    1,     0.01,  0.5);
    S->motivity_spin      = make_spin(0.01, 10,    0.01,  1.5);
    S->gamma_spin         = make_spin(0.01, 10,    0.01,  1.0);
    S->output_offset_spin = make_spin(0,    100,   0.001, 0.0);
    S->scale_spin         = make_spin(0.01, 100,   0.01,  1.0);

    for (auto* s : {S->accel_spin, S->exponent_spin, S->power_exp_spin,
                    S->limit_spin, S->offset_spin, S->decay_spin,
                    S->cap_x_spin, S->cap_y_spin,
                    S->sync_speed_spin, S->smooth_spin,
                    S->motivity_spin, S->gamma_spin,
                    S->output_offset_spin, S->scale_spin})
        connect_spin(s, S);

    // Tooltips state which mode(s) each parameter belongs to.
    trtip(S->accel_spin,
        "Classic: acceleration coefficient of the power curve.");
    trtip(S->exponent_spin,
        "Classic: exponent of the power curve.");
    trtip(S->power_exp_spin,
        "Power: exponent of the curve.");
    trtip(S->limit_spin,
        "Natural: gain limit above the 1.0 baseline.");
    trtip(S->offset_spin,
        "Classic/Natural: speeds below this map to 1.0 (no acceleration).");
    trtip(S->decay_spin,
        "Natural: how quickly gain approaches the limit.");
    trtip(S->cap_x_spin,
        "Classic/Power: cap input speed (ips) — combined with Cap Mode.\n"
        "Jump: step position — input speed where the jump occurs.");
    trtip(S->cap_y_spin,
        "Classic/Power: cap output gain/DPI multiplier — combined with Cap Mode.\n"
        "Jump: step amount — gain after the jump.");
    trtip(S->sync_speed_spin,
        "Synchronous: speed where the multiplier = 1.");
    trtip(S->smooth_spin,
        "Jump: sigmoid steepness. Synchronous: sharpness of the tanh blend (smaller = smoother).");
    trtip(S->output_offset_spin,
        "Power: raises the curve on the output side.");
    trtip(S->scale_spin,
        "Power: scale factor of the curve.");
    trtip(S->motivity_spin,
        "Synchronous: maximum multiplier (minimum = 1/motivity).");
    trtip(S->gamma_spin,
        "Synchronous: width of the activation curve in log space.");

    const char* cap_names[] = {"Output (out)", "Input (in)", "I/O (io)", nullptr};
    S->cap_mode_combo = gtk_drop_down_new(nullptr, nullptr);
    tr_combo_fill(S->cap_mode_combo, cap_names);
    gtk_widget_set_hexpand(S->cap_mode_combo, TRUE);
    g_signal_connect(S->cap_mode_combo, "notify::selected", G_CALLBACK(on_notify_param_changed), S);
    // BUG-17 fix: the cap_mode tooltip was registered on a NULL widget (the
    // combo was created AFTER the trtip call) — the tooltip never appeared and
    // refresh_language() re-applied it to NULL on every language switch
    // (repeated GLib critical warnings). Register it on the live widget.
    trtip(S->cap_mode_combo,
        "Classic/Power: out = clamp gain, in = clamp speed, io = clamp both at a selected point.");

    // Wrap normal parameter widgets in a frame (hidden in LUT mode)
    S->accel_params_frame = gtk_frame_new(nullptr);
    gtk_widget_set_margin_top(S->accel_params_frame, 2);
    {
        GtkWidget* pg = make_grid();
        gtk_widget_set_margin_start(pg, 4); gtk_widget_set_margin_end(pg, 4);
        gtk_widget_set_margin_top(pg, 4);   gtk_widget_set_margin_bottom(pg, 4);
        gtk_frame_set_child(GTK_FRAME(S->accel_params_frame), pg);
        grid_row2(pg, 0, "Accel:",         S->accel_spin,        &S->accel_row_label[0]);
        grid_row2(pg, 1, "Exp (cls):",     S->exponent_spin,     &S->accel_row_label[1]);
        grid_row2(pg, 2, "Exp (pwr):",     S->power_exp_spin,    &S->accel_row_label[2]);
        grid_row2(pg, 3, "Limit:",         S->limit_spin,        &S->accel_row_label[3]);
        grid_row2(pg, 4, "Input Offset:",  S->offset_spin,       &S->accel_row_label[4]);
        grid_row2(pg, 5, "Decay Rate:",    S->decay_spin,        &S->accel_row_label[5]);
        grid_row2(pg, 6, "Cap X:",         S->cap_x_spin,        &S->accel_row_label[6]);
        grid_row2(pg, 7, "Cap Y:",         S->cap_y_spin,        &S->accel_row_label[7]);
        grid_row2(pg, 8, "Cap Mode:",      S->cap_mode_combo,    &S->accel_row_label[8]);
        grid_row2(pg, 9, "Sync Speed:",    S->sync_speed_spin,   &S->accel_row_label[9]);
        grid_row2(pg, 10, "Smoothing:",    S->smooth_spin,       &S->accel_row_label[10]);
        grid_row2(pg, 11, "Motivity:",     S->motivity_spin,     &S->accel_row_label[11]);
        grid_row2(pg, 12, "Gamma:",        S->gamma_spin,        &S->accel_row_label[12]);
        grid_row2(pg, 13, "Out Offset:",   S->output_offset_spin,&S->accel_row_label[13]);
        grid_row2(pg, 14, "Scale:",        S->scale_spin,        &S->accel_row_label[14]);
    }
    gtk_box_append(GTK_BOX(lvbox), S->accel_params_frame);

    // ── LUT editor frame ──────────────────────────────────────────────────
    S->lut_frame = gtk_frame_new(nullptr);
    gtk_widget_set_margin_top(S->lut_frame, 2);
    gtk_widget_set_visible(S->lut_frame, FALSE); // hidden initially
    {
        GtkWidget* lut_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_start(lut_vbox, 4);
        gtk_widget_set_margin_end(lut_vbox, 4);
        gtk_widget_set_margin_top(lut_vbox, 6);
        gtk_widget_set_margin_bottom(lut_vbox, 6);
        gtk_frame_set_child(GTK_FRAME(S->lut_frame), lut_vbox);

        // Bilgi etiketi
        GtkWidget* info_lbl = trmlbl(
            "<small>Left click: add point on graph\n"
            "Right click: remove point on graph\n"
            "Points are speed (ips) → gain pairs.</small>");
        gtk_label_set_xalign(GTK_LABEL(info_lbl), 0.0);
        gtk_label_set_wrap(GTK_LABEL(info_lbl), TRUE);
        gtk_box_append(GTK_BOX(lut_vbox), info_lbl);

        // Nokta listesi (scrollable)
        GtkWidget* lut_scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lut_scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_size_request(lut_scroll, -1, 180);
        gtk_box_append(GTK_BOX(lut_vbox), lut_scroll);

        S->lut_list_box = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(S->lut_list_box), GTK_SELECTION_NONE);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(lut_scroll), S->lut_list_box);
        // Attach AppState* to the list_box so LUT row callbacks can retrieve it
        g_object_set_data(G_OBJECT(S->lut_list_box), "app-state", S);

        // "Nokta Ekle" butonu
        GtkWidget* add_btn = trbtn("+ Add Point");
        gtk_widget_add_css_class(add_btn, "suggested-action");
        g_signal_connect(add_btn, "clicked", G_CALLBACK(on_lut_add_point), S);
        gtk_box_append(GTK_BOX(lut_vbox), add_btn);

        // "Sort" button — reorder points by speed
        GtkWidget* sort_btn = trbtn("Sort");
        trtip(sort_btn, "Sort points by speed value (ascending)");
        g_signal_connect(sort_btn, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            auto* S2 = static_cast<AppState*>(ud);
            auto& ax = cur_prof(S2).prof.accel_x;
            auto pts = lut_get_points(ax);
            lut_set_points(ax, pts); // lut_set_points already sorts
            if (S2->xy_linked) cur_prof(S2).prof.accel_y = ax;
            S2->unsaved = true; // P-BUG-4: sort also mutates the LUT
            rebuild_lut_list(S2);
            gtk_widget_queue_draw(S2->graph_area);
        }), S);
        gtk_box_append(GTK_BOX(lut_vbox), sort_btn);
    }
    gtk_box_append(GTK_BOX(lvbox), S->lut_frame);

    // ── XY Link ───────────────────────────────────────────────────────────────
    append_section("<b>Y Axis</b>");
    S->xy_link_btn = trchk("Same as X (linked)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(S->xy_link_btn), TRUE);
    g_signal_connect(S->xy_link_btn, "toggled", G_CALLBACK(on_xy_link_toggled), S);
    gtk_box_append(GTK_BOX(lvbox), S->xy_link_btn);

    S->y_axis_frame = gtk_frame_new(nullptr);
    gtk_widget_set_sensitive(S->y_axis_frame, FALSE);
    gtk_box_append(GTK_BOX(lvbox), S->y_axis_frame);

    GtkWidget* yg = make_grid();
    gtk_widget_set_margin_start(yg, 6); gtk_widget_set_margin_end(yg, 6);
    gtk_widget_set_margin_top(yg, 6);   gtk_widget_set_margin_bottom(yg, 6);
    gtk_frame_set_child(GTK_FRAME(S->y_axis_frame), yg);

    S->mode_combo_y = gtk_drop_down_new(nullptr, nullptr);
    tr_combo_fill(S->mode_combo_y, mode_names);
    gtk_widget_set_hexpand(S->mode_combo_y, TRUE);
    g_signal_connect(S->mode_combo_y, "notify::selected", G_CALLBACK(on_notify_param_changed), S);

    S->accel_spin_y      = make_spin(0,   20,   0.001, 0.005);
    S->exponent_spin_y   = make_spin(1,   10,   0.05,  2.0);
    S->limit_spin_y      = make_spin(0.1, 100,  0.05,  1.5);
    S->offset_spin_y     = make_spin(0,   100,  0.5,   0.0);
    S->cap_y_spin_y      = make_spin(0,   100,  0.05,  1.5);
    for (auto* s : {S->accel_spin_y, S->exponent_spin_y,
                    S->limit_spin_y, S->offset_spin_y, S->cap_y_spin_y})
        connect_spin(s, S);

    grid_row2(yg, 0, "Mode:",          S->mode_combo_y,    &S->y_row_label[0]);
    grid_row2(yg, 1, "Accel:",         S->accel_spin_y,    &S->y_row_label[1]);
    grid_row2(yg, 2, "Exp:",           S->exponent_spin_y, &S->y_row_label[2]);
    grid_row2(yg, 3, "Limit:",         S->limit_spin_y,    &S->y_row_label[3]);
    grid_row2(yg, 4, "Input Offset:",  S->offset_spin_y,   &S->y_row_label[4]);
    grid_row2(yg, 5, "Cap Y:",         S->cap_y_spin_y,    &S->y_row_label[5]);

    // ── Rotasyon & Snap ───────────────────────────────────────────────────────
    append_section("<b>Rotation &amp; Snap</b>");
    GtkWidget* rg = append_grid();
    S->rotation_spin  = make_spin(-360, 360, 0.5, 0, 1);
    S->snap_spin      = make_spin(0, 45, 0.5, 0, 1);
    S->lr_ratio_spin  = make_spin(0.01, 100, 0.01, 1.0);
    S->ud_ratio_spin  = make_spin(0.01, 100, 0.01, 1.0);
    connect_spin(S->rotation_spin, S);
    connect_spin(S->snap_spin, S);
    connect_spin(S->lr_ratio_spin, S);
    connect_spin(S->ud_ratio_spin, S);
    grid_row(rg, 0, "Rotation (°):", S->rotation_spin);
    grid_row(rg, 1, "Snap (°):",     S->snap_spin);
    grid_row(rg, 2, "LR Ratio:",     S->lr_ratio_spin);
    grid_row(rg, 3, "UD Ratio:",     S->ud_ratio_spin);
    trtip(S->lr_ratio_spin,
        "Left/right output DPI ratio (1.0 = off). Values >1 amplify rightward movement.");
    trtip(S->ud_ratio_spin,
        "Up/down output DPI ratio (1.0 = off). Values >1 amplify downward movement.");

    // ── Speed Limit ───────────────────────────────────────────────────────────
    append_section("<b>Speed Limit</b>");
    GtkWidget* sg = append_grid();
    S->speed_min_spin = make_spin(0, 500, 1, 0, 0);
    S->speed_max_spin = make_spin(0, 500, 1, 0, 0);
    connect_spin(S->speed_min_spin, S);
    connect_spin(S->speed_max_spin, S);
    grid_row(sg, 0, "Min (ips):", S->speed_min_spin);
    grid_row(sg, 1, "Max (ips):", S->speed_max_spin);
    trtip(S->speed_min_spin, "Minimum speed clamp (ips). 0 = disabled.");
    trtip(S->speed_max_spin, "Maximum speed clamp (ips). Set to 0 to disable clamping.");
    {
        GtkWidget* speed_hint = trmlbl("<small>Set Max to 0 to disable speed clamping.</small>");
        gtk_label_set_xalign(GTK_LABEL(speed_hint), 0.0);
        gtk_box_append(GTK_BOX(lvbox), speed_hint);
    }

    // ── Speed Processor ───────────────────────────────────────────────────────
    append_section("<b>Speed Processor</b>");
    GtkWidget* spg = append_grid();
    {
        const char* dist_names[] = {"Euclidean", "Max", "Lp", "Separate", nullptr};
        S->dist_mode_combo = gtk_drop_down_new(nullptr, nullptr);
        tr_combo_fill(S->dist_mode_combo, dist_names);
        gtk_widget_set_hexpand(S->dist_mode_combo, TRUE);
        g_signal_connect(S->dist_mode_combo, "notify::selected", G_CALLBACK(on_notify_param_changed), S);
        trtip(S->dist_mode_combo,
            "How speed is calculated from X/Y input:\n"
            "  Euclidean — √(x²+y²)  (default)\n"
            "  Max — max(|x|,|y|)\n"
            "  Lp — generalized norm\n"
            "  Separate — X and Y processed independently");
        grid_row(spg, 0, "Distance:", S->dist_mode_combo);

        S->lp_norm_spin = make_spin(1.0, MAX_NORM - 0.5, 0.5, 2.0);
        connect_spin(S->lp_norm_spin, S);
        trtip(S->lp_norm_spin,
            "Lp-norm exponent (only used when Distance = Lp). 2 = Euclidean, large values → Max.");
        // Build the lp_norm row manually so we can get the label widget for show/hide
        S->lp_norm_label = trlbl("Lp Norm:");
        gtk_label_set_xalign(GTK_LABEL(S->lp_norm_label), 0.0);
        gtk_widget_set_margin_end(S->lp_norm_label, 6);
        gtk_grid_attach(GTK_GRID(spg), S->lp_norm_label, 0, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(spg), S->lp_norm_spin,  1, 1, 1, 1);
        gtk_widget_set_visible(S->lp_norm_label, FALSE); // hidden until Lp is selected
        gtk_widget_set_visible(S->lp_norm_spin,  FALSE);

        S->input_hl_spin  = make_spin(0, 200, 0.5, 0.0);
        S->scale_hl_spin  = make_spin(0, 200, 0.5, 0.0);
        S->output_hl_spin = make_spin(0, 200, 0.5, 0.0);
        connect_spin(S->input_hl_spin, S);
        connect_spin(S->scale_hl_spin, S);
        connect_spin(S->output_hl_spin, S);
        trtip(S->input_hl_spin,
            "EMA half-life for input speed smoothing (ms). 0 = off.");
        trtip(S->scale_hl_spin,
            "EMA half-life for scale smoothing (ms). 0 = off.");
        trtip(S->output_hl_spin,
            "EMA half-life for output speed smoothing (ms). 0 = off.");
        grid_row(spg, 2, "Input HL:", S->input_hl_spin);
        grid_row(spg, 3, "Scale HL:", S->scale_hl_spin);
        grid_row(spg, 4, "Output HL:", S->output_hl_spin);
    }
    {
        GtkWidget* sp_hint = trmlbl(
            "<small>HL = EMA half-life in ms. 0 = smoothing off.\n"
            "Separate: X and Y each processed by their own axis.</small>");
        gtk_label_set_xalign(GTK_LABEL(sp_hint), 0.0);
        gtk_label_set_wrap(GTK_LABEL(sp_hint), TRUE);
        gtk_box_append(GTK_BOX(lvbox), sp_hint);
    }

    // ── Device ────────────────────────────────────────────────────────────────
    append_section("<b>Device</b>");
    GtkWidget* dg = append_grid();
    S->dpi_spin        = make_spin(100, 32000, 50, 800, 0);
    S->polling_spin    = make_spin(125, 8000, 125, 1000, 0);
    S->output_dpi_spin = make_spin(0, 32000, 50, 1000, 0);
    connect_spin(S->dpi_spin, S);
    connect_spin(S->polling_spin, S);
    connect_spin(S->output_dpi_spin, S);
    trtip(S->output_dpi_spin,
        "Output DPI normalization value (default: 1000).\n"
        "Change this to match your monitor's effective DPI scaling.");
    grid_row(dg, 0, "DPI:", S->dpi_spin);
    grid_row(dg, 1, "Polling Rate:", S->polling_spin);
    grid_row(dg, 2, "Output DPI:", S->output_dpi_spin);

    // ── Logitech HID++ hardware settings ────────────────────────────────────
    // Onboard device controls (physical DPI / report rate / lift-off distance),
    // implemented in hidpp_panel.inl.  These write directly to the mouse via
    // /dev/hidraw* and are independent of the software profile settings above.
    append_section("<b>Hardware Settings (Logitech HID++)</b>");
    {
        GtkWidget* hg = append_grid();

        S->hw_dev_combo = gtk_drop_down_new(nullptr, nullptr);
        gtk_widget_set_hexpand(S->hw_dev_combo, TRUE);
        g_signal_connect(S->hw_dev_combo, "notify::selected",
                         G_CALLBACK(on_hw_dev_selected), S);
        grid_row(hg, 0, "HID++ Device:", S->hw_dev_combo);

        S->hw_refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
        trtip(S->hw_refresh_btn, "Rescan Logitech HID++ devices");
        g_signal_connect(S->hw_refresh_btn, "clicked",
                         G_CALLBACK(on_hw_refresh_clicked), S);
        gtk_grid_attach(GTK_GRID(hg), S->hw_refresh_btn, 2, 0, 1, 1);

        S->hw_dpi_spin = make_spin(HW_DPI_MIN, HW_DPI_MAX, 50, 1000, 0);
        trtip(S->hw_dpi_spin,
            "Physical sensor DPI stored on the Logitech mouse (HID++).\n"
            "Independent of the software DPI above.");
        grid_row(hg, 1, "Hardware DPI:", S->hw_dpi_spin);

        S->hw_rate_combo = gtk_drop_down_new(nullptr, nullptr);
        {
            GtkStringList* sl = gtk_string_list_new(nullptr);
            for (int i = 0; i < HW_NRATES; ++i)
                gtk_string_list_append(sl,
                    (std::to_string(HW_RATES[i]) + " Hz").c_str());
            gtk_drop_down_set_model(GTK_DROP_DOWN(S->hw_rate_combo), G_LIST_MODEL(sl));
            g_object_unref(sl);
            gtk_drop_down_set_selected(GTK_DROP_DOWN(S->hw_rate_combo), 3); // 1000 Hz
        }
        trtip(S->hw_rate_combo,
            "Report/polling rate of the Logitech mouse (HID++).\n"
            "Values the device rejects are reported in the status line.");
        grid_row(hg, 2, "Hardware Polling Rate:", S->hw_rate_combo);

        S->hw_lod_combo = gtk_drop_down_new(nullptr, nullptr);
        {
            const char* lod_names[] = {"Low", "Medium", "High"};
            GtkStringList* sl = gtk_string_list_new(nullptr);
            for (const char* n : lod_names) gtk_string_list_append(sl, tr(n));
            gtk_drop_down_set_model(GTK_DROP_DOWN(S->hw_lod_combo), G_LIST_MODEL(sl));
            g_object_unref(sl);
            gtk_drop_down_set_selected(GTK_DROP_DOWN(S->hw_lod_combo), 0);
        }
        trtip(S->hw_lod_combo,
            "Lift-off distance of the sensor (HID++ 0x2202).\n"
            "Only devices that support it accept this value.");
        grid_row(hg, 3, "Lift-off Distance:", S->hw_lod_combo);

        S->hw_apply_btn = trbtn("Apply to Device");
        trtip(S->hw_apply_btn,
            "Write DPI, polling rate and lift-off distance to the physical mouse.");
        g_signal_connect(S->hw_apply_btn, "clicked",
                         G_CALLBACK(on_hw_apply_clicked), S);
        gtk_widget_set_margin_top(S->hw_apply_btn, 2);
        gtk_grid_attach(GTK_GRID(hg), S->hw_apply_btn, 1, 4, 1, 1);

        S->hw_status_lbl = trlbl("—");
        gtk_label_set_xalign(GTK_LABEL(S->hw_status_lbl), 0.0);
        gtk_label_set_wrap(GTK_LABEL(S->hw_status_lbl), TRUE);
        gtk_widget_set_margin_top(S->hw_status_lbl, 2);
        gtk_grid_attach(GTK_GRID(hg), S->hw_status_lbl, 0, 5, 3, 1);

        // P169 — live battery + capability summary for the selected device.
        // Both are read-only and updated from the same worker threads as the
        // status line (scan/query/notification); feature-derived capability
        // widgets that a device does not advertise are disabled in
        // hidpp_panel.inl and the reason is spelled out here.
        S->hw_battery_lbl = trlbl("Battery: —");
        gtk_label_set_xalign(GTK_LABEL(S->hw_battery_lbl), 0.0);
        gtk_label_set_wrap(GTK_LABEL(S->hw_battery_lbl), TRUE);
        gtk_widget_set_margin_top(S->hw_battery_lbl, 2);
        gtk_grid_attach(GTK_GRID(hg), S->hw_battery_lbl, 0, 6, 3, 1);

        S->hw_caps_lbl = trlbl("Capabilities: —");
        gtk_label_set_xalign(GTK_LABEL(S->hw_caps_lbl), 0.0);
        gtk_label_set_wrap(GTK_LABEL(S->hw_caps_lbl), TRUE);
        gtk_label_set_justify(GTK_LABEL(S->hw_caps_lbl), GTK_JUSTIFY_LEFT);
        gtk_widget_set_margin_top(S->hw_caps_lbl, 2);
        gtk_grid_attach(GTK_GRID(hg), S->hw_caps_lbl, 0, 7, 3, 1);

        // Start the initial device scan (background).
        hw_start_scan(S);
        // Read-only HID++ notifications update this panel asynchronously.
        // The one-second cadence is intentionally outside the input path.
        S->hidpp_notify_poll_id = g_timeout_add(
            1000, hw_notification_tick, S);
    }

    // ── Device assignment ─────────────────────────────────────────────────
    append_section("<b>Device Assignment</b>");
    {
        // Fareleri tara
        S->mice_list = list_mice();

        // Dropdown model: "All devices" + discovered mice
        GtkStringList* mlist = gtk_string_list_new(nullptr);
        gtk_string_list_append(mlist, tr("All devices (default)"));
        for (auto& m : S->mice_list) {
            // Display: "Name  [/dev/input/eventN]"
            std::string label = m.name + "  [" + m.event_node + "]";
            gtk_string_list_append(mlist, label.c_str());
        }

        S->device_id_combo = gtk_drop_down_new(G_LIST_MODEL(mlist), nullptr);
        g_object_unref(mlist); // GUI-D1: local model ref must be dropped
        gtk_widget_set_hexpand(S->device_id_combo, TRUE);
        g_signal_connect(S->device_id_combo, "notify::selected",
                         G_CALLBACK(on_notify_param_changed), S);

        GtkWidget* da_grid = append_grid();
        grid_row(da_grid, 0, "Mouse:", S->device_id_combo);

        // P-APP: per-application profile binding.  Empty = apply always;
        // non-empty = only while the focused app's WM_CLASS matches.  Works
        // automatically on KDE Wayland (KWin focus relay) and X11; on other
        // desktops the daemon uses the last value pushed via IPC.
        S->match_app_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(S->match_app_entry),
                                       tr("App class (e.g. firefox) — optional"));
        gtk_entry_set_max_length(GTK_ENTRY(S->match_app_entry), 128);
        gtk_widget_set_hexpand(S->match_app_entry, TRUE);
        // GUI-K1: "changed" is a 2-arg GtkEditable signal (instance, user_data).
        // Binding the 3-arg on_notify_param_changed here feeds a GParamSpec* in
        // place of AppState* → SIGSEGV on the first keystroke in the App field.
        g_signal_connect(S->match_app_entry, "changed",
                         G_CALLBACK(on_param_changed), S);
        grid_row(da_grid, 1, "App:", S->match_app_entry);

        // Yenile butonu — fare listesini yeniden tara
        GtkWidget* refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
        trtip(refresh_btn, "Rescan connected mice");
        g_signal_connect(refresh_btn, "clicked",
            G_CALLBACK(+[](GtkWidget*, gpointer ud) {
                refresh_mice_combo(static_cast<AppState*>(ud), /*is_auto=*/false);
            }), S);
        gtk_grid_attach(GTK_GRID(da_grid), refresh_btn, 2, 0, 1, 1);

        // ── inotify: auto-detect /dev/input hot-plug events ───────────────
        S->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (S->inotify_fd >= 0) {
            S->inotify_wd = inotify_add_watch(S->inotify_fd, "/dev/input",
                                               IN_CREATE | IN_DELETE);
            if (S->inotify_wd >= 0) {
                GIOChannel* chan = g_io_channel_unix_new(S->inotify_fd);
                g_io_channel_set_encoding(chan, nullptr, nullptr); // binary
                g_io_channel_set_buffered(chan, FALSE);
                S->inotify_src = g_io_add_watch(chan, G_IO_IN,
                                                 on_inotify_event, S);
                g_io_channel_unref(chan);
            }

            // HID++ devices are exposed through /dev/hidraw*.  Watch the parent
            // directory so a receiver reconnect refreshes the read-only
            // capability panel without polling the motion path.
            S->hidraw_inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
            if (S->hidraw_inotify_fd >= 0) {
                S->hidraw_inotify_wd = inotify_add_watch(
                    S->hidraw_inotify_fd, "/dev", IN_CREATE | IN_DELETE);
                if (S->hidraw_inotify_wd >= 0) {
                    GIOChannel* chan = g_io_channel_unix_new(S->hidraw_inotify_fd);
                    g_io_channel_set_encoding(chan, nullptr, nullptr);
                    g_io_channel_set_buffered(chan, FALSE);
                    S->hidraw_inotify_src = g_io_add_watch(
                        chan, G_IO_IN, on_hidraw_inotify_event, S);
                    g_io_channel_unref(chan);
                }
            }
        }

        // Small informational label
        GtkWidget* hint = trmlbl(
            "<small>This profile applies only to the selected mouse.\n"
            "The daemon uses a stable USB composite ID as device_id.</small>");
        gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
        gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
        gtk_widget_set_margin_top(hint, 2);
        gtk_box_append(GTK_BOX(lvbox), hint);
    }

    // ── KDE double-acceleration warning bar ──────────────────────────────────
    // Only constructed; visibility is set later in update_kde_warn_bar().
    {
        GtkWidget* warn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(warn_box, 8);
        gtk_widget_set_margin_end(warn_box, 8);
        gtk_widget_set_margin_top(warn_box, 4);
        gtk_widget_set_margin_bottom(warn_box, 2);

        // Orange warning background via CSS class
        GtkCssProvider* warn_css = gtk_css_provider_new();
        gtk_css_provider_load_from_string(warn_css,
            ".kde-warn-bar { background-color: #7a4000; border-radius: 6px; padding: 4px 8px; }"
            ".kde-warn-bar label { color: #ffcc80; }"
            ".kde-warn-bar button { background: #ff8c00; color: white; border-radius: 4px; }");
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(warn_css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        GtkWidget* warn_inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(warn_inner, "kde-warn-bar");
        gtk_widget_set_hexpand(warn_inner, TRUE);

        GtkWidget* warn_icon = gtk_label_new("⚠");
        gtk_box_append(GTK_BOX(warn_inner), warn_icon);

        GtkWidget* warn_lbl = trmlbl(
            "<b>KDE: Mouse acceleration is NOT disabled!</b>  "
            "KDE will apply its own curve on top of RawAccel → double acceleration.");
        gtk_label_set_wrap(GTK_LABEL(warn_lbl), TRUE);
        gtk_widget_set_hexpand(warn_lbl, TRUE);
        gtk_box_append(GTK_BOX(warn_inner), warn_lbl);

        GtkWidget* fix_btn = trbtn("Fix Now");
        trtip(fix_btn,
            "Sets PointerAccelerationProfile=Flat in ~/.config/kwinrc\n"
            "and reloads KWin input settings immediately (no logout needed).");
        g_signal_connect(fix_btn, "clicked", G_CALLBACK(on_kde_fix_clicked), S);
        gtk_box_append(GTK_BOX(warn_inner), fix_btn);

        GtkWidget* manual_btn = trbtn("Manual");
        trtip(manual_btn,
            "Open KDE System Settings → Input Devices → Mouse\n"
            "and set Pointer Acceleration to Flat.");
        g_signal_connect(manual_btn, "clicked", G_CALLBACK(on_kde_open_settings), nullptr);
        gtk_box_append(GTK_BOX(warn_inner), manual_btn);

        gtk_box_append(GTK_BOX(warn_box), warn_inner);
        S->kde_warn_bar = warn_box;
        gtk_widget_set_visible(warn_box, FALSE); // initially hidden; shown by update_kde_warn_bar
        gtk_box_append(GTK_BOX(lvbox), warn_box);
    }

    // ── RIGHT PANEL (Graph) ───────────────────────────────────────────────────
    GtkWidget* rvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(rvbox, TRUE);
    gtk_widget_set_vexpand(rvbox, TRUE);
    gtk_widget_set_margin_start(rvbox, 8);
    gtk_widget_set_margin_end(rvbox, 12);
    gtk_widget_set_margin_top(rvbox, 8);
    gtk_widget_set_margin_bottom(rvbox, 4);
    gtk_paned_set_end_child(GTK_PANED(hpaned), rvbox);

    GtkWidget* graph_lbl = trmlbl("<b>Gain Curve</b>  <small>(scroll = zoom)</small>");
    gtk_label_set_xalign(GTK_LABEL(graph_lbl), 0.0);
    gtk_box_append(GTK_BOX(rvbox), graph_lbl);

    S->graph_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(S->graph_area, TRUE);
    gtk_widget_set_vexpand(S->graph_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(S->graph_area),
                                   on_graph_draw, S, nullptr);

    // Scroll controller for zoom
    GtkEventController* scroll_ctrl =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll_ctrl, "scroll", G_CALLBACK(on_graph_scroll), S);
    gtk_widget_add_controller(S->graph_area, scroll_ctrl);

    // ── LUT graph interaction: left click = add point, right click = remove ─
    // Use GRAPH_ML/MR/MT/MB constants for graph margins (single source of truth)

    // Left click: add a new point in LUT mode
    GtkGesture* lclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(lclick), 1);
    g_signal_connect(lclick, "pressed",
        G_CALLBACK(+[](GtkGestureClick*, int /*n*/, double cx, double cy, gpointer ud) {
            auto* S2 = static_cast<AppState*>(ud);
            if (!S2->lut_graph_mode) return;
            int w = gtk_widget_get_width(S2->graph_area);
            int h = gtk_widget_get_height(S2->graph_area);
            double PW = w - GRAPH_ML - GRAPH_MR;
            double PH = h - GRAPH_MT - GRAPH_MB;
            if (PW <= 0 || PH <= 0) return;
            if (cx < GRAPH_ML || cx > GRAPH_ML + PW || cy < GRAPH_MT || cy > GRAPH_MT + PH) return;

            double max_speed = 50.0 / S2->graph_zoom + S2->graph_pan_x;
            max_speed = std::max(max_speed, 5.0);

            double max_gain = compute_max_gain(S2, max_speed);

            double spd  = (cx - GRAPH_ML) / PW * max_speed;
            double gain = (1.0 - (cy - GRAPH_MT) / PH) * max_gain;
            spd  = std::max(0.0, spd);
            gain = std::max(0.01, gain);

            auto& ax = cur_prof(S2).prof.accel_x;
            if (ax.length / 2 >= (int)LUT_POINTS_CAPACITY) {
                set_status(S2, tr("Maximum number of points reached."));
                return;
            }
            auto pts = lut_get_points(ax);
            // BUG-67: in velocity mode the stored value is an output speed,
            // not the gain the graph/edit shows — store gain·speed so the
            // point lands exactly where the user clicked (graph.inl draws
            // stored/speed).  Matches on_lut_spin_changed / on_lut_add_point.
            pts.push_back({spd, lut_gain_to_stored(spd, gain, ax.gain)});
            lut_set_points(ax, pts);
            if (S2->xy_linked) cur_prof(S2).prof.accel_y = ax;
            S2->unsaved = true; // P-BUG-3: graph add must count as unsaved too
            rebuild_lut_list(S2);
            gtk_widget_queue_draw(S2->graph_area);
            set_status(S2, trf("LUT point added: speed=%d gain=%s",
                               (int)spd, std::to_string(gain).substr(0, 5).c_str()));
        }), S);
    gtk_widget_add_controller(S->graph_area, GTK_EVENT_CONTROLLER(lclick));

    // Right click: remove the nearest point in LUT mode
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), 3);
    g_signal_connect(rclick, "pressed",
        G_CALLBACK(+[](GtkGestureClick*, int /*n*/, double cx, double cy, gpointer ud) {
            auto* S2 = static_cast<AppState*>(ud);
            if (!S2->lut_graph_mode) return;
            int w = gtk_widget_get_width(S2->graph_area);
            int h = gtk_widget_get_height(S2->graph_area);
            double PW = w - GRAPH_ML - GRAPH_MR;
            double PH = h - GRAPH_MT - GRAPH_MB;
            if (PW <= 0 || PH <= 0) return;

            double max_speed = 50.0 / S2->graph_zoom + S2->graph_pan_x;
            max_speed = std::max(max_speed, 5.0);
            double max_gain = compute_max_gain(S2, max_speed);

            auto& ax = cur_prof(S2).prof.accel_x;
            auto  pts = lut_get_points(ax);
            if (pts.empty()) return;

            // Find the LUT point nearest to the clicked pixel position.
            // BUG-67: the graph draws points at *gain* (graph.inl:211 uses
            // lut_stored_to_gain), so the hit-test must compare against the
            // gain coordinate too — in velocity mode stored y is an output
            // speed and raw stored/max_gain lands on the wrong pixel.
            int best_idx = -1;
            double best_dist2 = 20.0 * 20.0; // 20px threshold
            bool vel_hit = ax.gain;
            for (int i = 0; i < (int)pts.size(); i++) {
                double px = GRAPH_ML + (pts[i].first  / max_speed) * PW;
                double py = GRAPH_MT + PH - (lut_stored_to_gain(pts[i].first,
                                                               pts[i].second,
                                                               vel_hit) / max_gain) * PH;
                double d2 = (cx - px) * (cx - px) + (cy - py) * (cy - py);
                if (d2 < best_dist2) { best_dist2 = d2; best_idx = i; }
            }
            if (best_idx < 0) return;
            pts.erase(pts.begin() + best_idx);
            lut_set_points(ax, pts);
            if (S2->xy_linked) cur_prof(S2).prof.accel_y = ax;
            S2->unsaved = true; // P-BUG-3: graph remove must count as unsaved too
            rebuild_lut_list(S2);
            gtk_widget_queue_draw(S2->graph_area);
            set_status(S2, tr("LUT point removed."));
        }), S);
    gtk_widget_add_controller(S->graph_area, GTK_EVENT_CONTROLLER(rclick));

    // Left-drag = pan (disabled in LUT mode — left click adds points there)
    GtkGesture* drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 1);
    g_signal_connect(drag, "drag-begin",  G_CALLBACK(on_graph_drag_begin),  S);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_graph_drag_update), S);
    g_signal_connect(drag, "drag-end",    G_CALLBACK(on_graph_drag_end),    S);
    gtk_widget_add_controller(S->graph_area, GTK_EVENT_CONTROLLER(drag));

    // Fare hareketi = LUT nokta hover cursor
    GtkEventController* motion_ctrl = gtk_event_controller_motion_new();
    g_signal_connect(motion_ctrl, "motion", G_CALLBACK(on_graph_motion), S);
    gtk_widget_add_controller(S->graph_area, motion_ctrl);

    gtk_box_append(GTK_BOX(rvbox), S->graph_area);

    // ── Status bar ────────────────────────────────────────────────────────────
    GtkWidget* status_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(status_hbox, 10);
    gtk_widget_set_margin_bottom(status_hbox, 4);
    gtk_widget_set_margin_top(status_hbox, 2);

    S->status_bar = gtk_label_new(tr("Ready."));
    gtk_label_set_xalign(GTK_LABEL(S->status_bar), 0.0);
    gtk_widget_set_hexpand(S->status_bar, TRUE);
    gtk_box_append(GTK_BOX(status_hbox), S->status_bar);

    S->battery_detected_lbl = gtk_label_new(tr("<b>Battery: unknown</b>"));
    gtk_label_set_xalign(GTK_LABEL(S->battery_detected_lbl), 0.0);
    gtk_widget_add_css_class(S->battery_detected_lbl, "battery-label");
    gtk_box_append(GTK_BOX(status_hbox), S->battery_detected_lbl);

    // Latency stats view: read-only display + a button that pulls the daemon's
    // latency snapshot over IPC (see on_perf_clicked in widgets_sync.inl).
    S->latency_lbl = trlbl("Latency: —");
    gtk_label_set_xalign(GTK_LABEL(S->latency_lbl), 0.0);
    gtk_widget_add_css_class(S->latency_lbl, "latency-label");
    gtk_widget_set_margin_end(S->latency_lbl, 4);
    gtk_box_append(GTK_BOX(status_hbox), S->latency_lbl);

    GtkWidget* perf_btn = trbtn("Performance");
    trtip(perf_btn, "Read the daemon's latency snapshot (Avg/p50/p95/p99/Max)");
    g_signal_connect(perf_btn, "clicked", G_CALLBACK(on_perf_clicked), S);
    gtk_box_append(GTK_BOX(status_hbox), perf_btn);

    // Mouse lock test window (P104): pointer lock + live speed/gain readout.
    GtkWidget* test_btn = trbtn("Mouse Test");
    trtip(test_btn, "Mouse lock test window — locks the pointer inside and shows live speed/gain. ESC releases.");
    g_signal_connect(test_btn, "clicked", G_CALLBACK(on_mouse_test_clicked), S);
    gtk_box_append(GTK_BOX(status_hbox), test_btn);

    gtk_box_append(GTK_BOX(outer_vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(outer_vbox), status_hbox);

    // ── Init ──────────────────────────────────────────────────────────────────
    profile_to_widgets(S);
    update_daemon_status(S);
    update_kde_warn_bar(S);   // KDE: show warning if libinput acceleration is not disabled

    // Poll daemon status every 3s — pass S as user_data
    S->daemon_poll_id = g_timeout_add_seconds(3, poll_daemon_status, S);

    // Prompt before closing if there are unsaved changes
    g_signal_connect(S->window, "close-request",
                     G_CALLBACK(on_window_close_request), S);

    // Cancel the poll timer when the window is destroyed (prevents stale S->window access)
    g_signal_connect(S->window, "destroy",
        G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            auto* S2 = static_cast<AppState*>(ud);
            if (S2->daemon_poll_id) {
                g_source_remove(S2->daemon_poll_id);
                S2->daemon_poll_id = 0;
            }
            // clean up the inotify source
            if (S2->inotify_src) {
                g_source_remove(S2->inotify_src);
                S2->inotify_src = 0;
            }
            if (S2->inotify_wd >= 0) {
                inotify_rm_watch(S2->inotify_fd, S2->inotify_wd);
                S2->inotify_wd = -1;
            }
            if (S2->inotify_fd >= 0) {
                close(S2->inotify_fd);
                S2->inotify_fd = -1;
            }
            if (S2->hidraw_inotify_src) {
                g_source_remove(S2->hidraw_inotify_src);
                S2->hidraw_inotify_src = 0;
            }
            if (S2->hidraw_inotify_wd >= 0) {
                inotify_rm_watch(S2->hidraw_inotify_fd,
                                 S2->hidraw_inotify_wd);
                S2->hidraw_inotify_wd = -1;
            }
            if (S2->hidraw_inotify_fd >= 0) {
                close(S2->hidraw_inotify_fd);
                S2->hidraw_inotify_fd = -1;
            }
            if (S2->hidpp_notify_poll_id) {
                g_source_remove(S2->hidpp_notify_poll_id);
                S2->hidpp_notify_poll_id = 0;
            }
            // GUI-O4: drop the pkexec child-watch source.  Without this a
            // pkexec+systemctl child that exits AFTER the window is gone would
            // fire pkexec_child_report() → set_status() into destroyed widgets.
            if (S2->pkexec_watch_id) {
                g_source_remove(S2->pkexec_watch_id);
                S2->pkexec_watch_id = 0;
            }
            // Signal all HID++ idle callbacks to bail (prevents UAF on widgets)
            S2->hw_cancel = true;
            S2->hw_pending_query = -1;
            // P-APP: unload the KWin focus script, release the GDBus name
            kwin_focus_uninstall(S2);
        }), S);

    gtk_window_present(GTK_WINDOW(S->window));
}

// ── Window close confirmation ─────────────────────────────────────────────

/// GTK4: returning TRUE from close-request prevents the window from closing.
/// Show a confirmation dialog if there are unsaved changes.
gboolean on_window_close_request(GtkWindow* win, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (!S->unsaved) return FALSE; // no unsaved changes — close immediately

    GtkWidget* dlg = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dlg), tr("Unsaved Changes"));
    gtk_window_set_transient_for(GTK_WINDOW(dlg), win);
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 320, -1);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 16); gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);   gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dlg), vbox);

    GtkWidget* lbl = gtk_label_new(tr("You have unsaved changes.\nDo you want to quit?"));
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_box_append(GTK_BOX(vbox), lbl);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), hbox);

    GtkWidget* cancel_btn  = gtk_button_new_with_label(tr("Cancel"));
    GtkWidget* discard_btn = gtk_button_new_with_label(tr("Quit Without Saving"));
    GtkWidget* save_btn    = gtk_button_new_with_label(tr("Save and Quit"));
    gtk_widget_add_css_class(save_btn,    "suggested-action");
    gtk_widget_add_css_class(discard_btn, "destructive-action");
    gtk_box_append(GTK_BOX(hbox), cancel_btn);
    gtk_box_append(GTK_BOX(hbox), discard_btn);
    gtk_box_append(GTK_BOX(hbox), save_btn);

    // Cancel: close the dialog, leave the window open
    g_signal_connect(cancel_btn, "clicked",
        G_CALLBACK(+[](GtkWidget*, gpointer d) {
            gtk_window_destroy(GTK_WINDOW(d));
        }), dlg);

    // Quit without saving: close both dialog and main window
    // Store AppState* in dialog for the lambda
    g_object_set_data(G_OBJECT(dlg), "app-state", S);
    g_signal_connect(discard_btn, "clicked",
        G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* S2 = static_cast<AppState*>(g_object_get_data(G_OBJECT(d), "app-state"));
            gtk_window_destroy(GTK_WINDOW(d));
            S2->unsaved = false;
            gtk_window_destroy(GTK_WINDOW(S2->window));
        }), dlg);

    // Save and quit
    g_signal_connect(save_btn, "clicked",
        G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* S2 = static_cast<AppState*>(g_object_get_data(G_OBJECT(d), "app-state"));
            gtk_window_destroy(GTK_WINDOW(d));
            save_config_now(S2);
            gtk_window_destroy(GTK_WINDOW(S2->window));
        }), dlg);

    // Escape = Cancel (L-BUG-18): a modal no-button-behavior dialog in GTK4 has
    // no implicit dismiss key — without this the user can only reach Cancel by
    // mouse, and there is no way to back out via keyboard.
    GtkEventController* esc_ctrl = gtk_event_controller_key_new();
    g_signal_connect(esc_ctrl, "key-pressed",
        G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint /*keycode*/,
                       GdkModifierType /*state*/, gpointer d) -> gboolean {
            if (keyval == GDK_KEY_Escape) {
                gtk_window_destroy(GTK_WINDOW(d));
                return true;
            }
            return false;
        }), dlg);
    gtk_widget_add_controller(dlg, esc_ctrl);

    gtk_window_present(GTK_WINDOW(dlg));
    return TRUE; // prevent close; window stays open until the dialog is dismissed
}

// ── App activate ─────────────────────────────────────────────────────────────

/// Register application-level keyboard shortcuts via GAction.
/// Ctrl+S = Save, Ctrl+N = New profile, Ctrl+D = Duplicate profile,
/// Ctrl+R = Reload daemon, F5 = Refresh device list.
static void register_shortcuts(AppState* S, GtkApplication* gapp) {
    // Ctrl+S — Save
    {
        GSimpleAction* a = g_simple_action_new("save", nullptr);
        g_signal_connect(a, "activate",
            G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer ud) {
                on_save_clicked(nullptr, ud);
            }), S);
        g_action_map_add_action(G_ACTION_MAP(gapp), G_ACTION(a));
        const char* accels[] = {"<Control>s", nullptr};
        gtk_application_set_accels_for_action(gapp, "app.save", accels);
    }
    // Ctrl+N — New profile
    {
        GSimpleAction* a = g_simple_action_new("new-profile", nullptr);
        g_signal_connect(a, "activate",
            G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer ud) {
                on_new_profile(nullptr, ud);
            }), S);
        g_action_map_add_action(G_ACTION_MAP(gapp), G_ACTION(a));
        const char* accels[] = {"<Control>n", nullptr};
        gtk_application_set_accels_for_action(gapp, "app.new-profile", accels);
    }
    // Ctrl+D — Duplicate profile
    {
        GSimpleAction* a = g_simple_action_new("dup-profile", nullptr);
        g_signal_connect(a, "activate",
            G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer ud) {
                on_duplicate_profile(nullptr, ud);
            }), S);
        g_action_map_add_action(G_ACTION_MAP(gapp), G_ACTION(a));
        const char* accels[] = {"<Control>d", nullptr};
        gtk_application_set_accels_for_action(gapp, "app.dup-profile", accels);
    }
    // Ctrl+R — Reload daemon
    {
        GSimpleAction* a = g_simple_action_new("reload-daemon", nullptr);
        g_signal_connect(a, "activate",
            G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer ud) {
                on_daemon_reload(nullptr, ud);
            }), S);
        g_action_map_add_action(G_ACTION_MAP(gapp), G_ACTION(a));
        const char* accels[] = {"<Control>r", nullptr};
        gtk_application_set_accels_for_action(gapp, "app.reload-daemon", accels);
    }
    // F5 — Refresh device list
    {
        GSimpleAction* a = g_simple_action_new("refresh-devices", nullptr);
        g_signal_connect(a, "activate",
            G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer ud) {
                refresh_mice_combo(static_cast<AppState*>(ud), false);
            }), S);
        g_action_map_add_action(G_ACTION_MAP(gapp), G_ACTION(a));
        const char* accels[] = {"F5", nullptr};
        gtk_application_set_accels_for_action(gapp, "app.refresh-devices", accels);
    }
}

// ── KDE double-acceleration fix helpers ──────────────────────────────────────

/// Enumerate currently-active RawAccel virtual mouse devices from
/// /proc/bus/input/devices.  Returns a vector of (bus, vendor, product, name).
/// Vendor/product/bus are decimal (Plasma stores them in decimal in kwinrc).
struct rawaccel_dev_t {
    int bus, vendor, product;
    std::string name;
};
static std::vector<rawaccel_dev_t> kde_enumerate_rawaccel_devices() {
    std::vector<rawaccel_dev_t> out;
    FILE* f = fopen("/proc/bus/input/devices", "r");
    if (!f) return out;
    char line[1024];
    rawaccel_dev_t cur{};
    bool have_id = false;
    auto flush_block = [&]() {
        if (have_id && cur.name.size() >= 10 &&
            cur.name.compare(cur.name.size() - 10, 10, "(RawAccel)") == 0) {
            out.push_back(cur);
        }
        cur = rawaccel_dev_t{};
        have_id = false;
    };
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        if (n == 0) { flush_block(); continue; }
        // I: Bus=0003 Vendor=046d Product=c542 Version=0111
        if (line[0] == 'I' && line[1] == ':') {
            unsigned bus = 0, ven = 0, prod = 0;
            if (sscanf(line, "I: Bus=%x Vendor=%x Product=%x", &bus, &ven, &prod) == 3) {
                cur.bus = (int)bus; cur.vendor = (int)ven; cur.product = (int)prod;
                have_id = true;
            }
        } else if (line[0] == 'N' && strncmp(line, "N: Name=\"", 9) == 0) {
            std::string s(line + 9);
            if (!s.empty() && s.back() == '"') s.pop_back();
            cur.name = s;
        }
    }
    flush_block();
    fclose(f);
    return out;
}

/// Replace or append a section [header] with the given key=value lines.
/// `header` includes the brackets, e.g. "[Libinput]" or
/// "[Libinput][3][1133][50498][Logitech ... (RawAccel)]".
/// Section ends at the next line starting with '['.
/// R2-01: previously every non-key line inside the section (user comments
/// '#...'/'...', blank lines) was erased along with the keys, silently
/// destroying user comments.  Now comments/blank lines inside the section
/// are preserved and re-appended after the new keys.
static void kde_upsert_section(std::vector<std::string>& lines,
                               const std::string& header,
                               const std::vector<std::pair<std::string, std::string>>& kv) {
    // Find existing section
    size_t start = std::string::npos;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i] == header) { start = i; break; }
    }

    if (start == std::string::npos) {
        // Append at end (with blank separator if file isn't empty)
        if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
        lines.push_back(header);
        for (auto& [k, v] : kv) lines.push_back(k + "=" + v);
        return;
    }
    // Walk the section body, separating key=value lines from content that must
    // be preserved (comments, blanks).  INI keys are 'name=value'.
    size_t end = start + 1;
    std::vector<std::string> preserved;      // comments / blank lines, in order
    std::vector<std::string> body;           // new key=value lines
    body.reserve(kv.size());
    for (auto& [k, v] : kv) body.push_back(k + "=" + v);
    while (end < lines.size() && lines[end][0] != '[') {
        std::string& ln = lines[end];
        bool is_key = !ln.empty() && ln[0] != '#' && ln[0] != ';' &&
                      ln.find('=') != std::string::npos;
        if (!is_key)
            preserved.push_back(ln);         // keep user comments/blank lines
        end++;
    }
    // Replace [start+1, end) content: new keys, then preserved comments.
    lines.erase(lines.begin() + (long)start + 1, lines.begin() + (long)end);
    lines.insert(lines.begin() + (long)start + 1, body.begin(), body.end());
    lines.insert(lines.begin() + (long)start + 1 + (long)body.size(),
                 preserved.begin(), preserved.end());
}

/// Atomically write a kwinrc-style INI file (with possibly nested sections)
/// from the in-memory line buffer.
///
/// BUG-8: previously fputs/fputc/fclose return values were ignored — a full
/// disk (ENOSPC) or I/O error could silently truncate the temp file, then
/// rename() would publish the corrupt content as the user's kwinrc.  Now we
/// detect any write/flush failure, drop the temp file, and signal an error
/// to the caller so the original kwinrc is preserved.
static bool kde_atomic_write(const std::string& path,
                             const std::vector<std::string>& lines) {
    std::string tmp = path + ".tmp";
    // O_NOFOLLOW: never follow a symlink (prevents symlink-follow attack).
    // O_EXCL:     fail if tmp already exists (prevents two-writer race).
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    FILE* fw = fdopen(fd, "w");
    if (!fw) { close(fd); unlink(tmp.c_str()); return false; }
    for (auto& l : lines) {
        if (fputs(l.c_str(), fw) == EOF || fputc('\n', fw) == EOF) {
            fclose(fw); unlink(tmp.c_str());
            return false;
        }
    }
    if (fflush(fw) != 0 || ferror(fw) || fsync(fileno(fw)) != 0 || fclose(fw) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

/// Read all lines (without trailing CR/LF) from path. Empty vector if missing.
static std::vector<std::string> kde_read_lines(const std::string& path) {
    std::vector<std::string> lines;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return lines;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        lines.emplace_back(line);
    }
    fclose(f);
    return lines;
}

/// Resolve $XDG_CONFIG_HOME/<name> or ~/.config/<name>.
static std::string kde_xdg_path(const char* name) {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') return std::string(xdg) + "/" + name;
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0') return {};
    return std::string(home) + "/.config/" + name;
}

/// Write [Libinput] global + per-device overrides into kwinrc and kcminputrc,
/// with the requested acceleration profile (1 = Flat, 2 = Adaptive).
///
/// KEY INSIGHT — Plasma 6 split-brain storage:
///   • kwinrc    → KWin compositor reads on startup/reconfigure
///                 Section format includes bus: [Libinput][bus][vid][pid][Name]
///   • kcminputrc → System Settings KCM (kcm_mouse) reads + applies via libinput
///                 Section format omits bus: [Libinput][vid][pid][Name]
///                 Only key written is `PointerAccelerationProfile`
/// Manual toggle in System Settings only updates kcminputrc and calls
/// libinput's runtime API directly. Writing kwinrc alone is not enough on
/// Plasma 6 — we MUST also write kcminputrc and trigger KCM init.
static bool kde_write_kwinrc_accel(const char* profile, const char* accel) {
    auto devs = kde_enumerate_rawaccel_devices();

    // ── kwinrc (compositor-level config) ─────────────────────────────────────
    std::string kwinrc_path = kde_xdg_path("kwinrc");
    if (kwinrc_path.empty()) return false;
    auto kwinrc_lines = kde_read_lines(kwinrc_path);

    const std::vector<std::pair<std::string, std::string>> kv_kwin = {
        {"PointerAccelerationProfile", profile},
        {"PointerAcceleration",        accel},
    };
    kde_upsert_section(kwinrc_lines, "[Libinput]", kv_kwin);
    for (auto& d : devs) {
        std::string header = "[Libinput][" + std::to_string(d.bus) + "][" +
                             std::to_string(d.vendor) + "][" +
                             std::to_string(d.product) + "][" + d.name + "]";
        kde_upsert_section(kwinrc_lines, header, kv_kwin);
    }
    if (!kde_atomic_write(kwinrc_path, kwinrc_lines)) return false;

    // ── kcminputrc (KCM-applied per-device config — the one libinput uses) ──
    std::string kcm_path = kde_xdg_path("kcminputrc");
    if (kcm_path.empty()) return true; // kwinrc done — kcminputrc is bonus
    auto kcm_lines = kde_read_lines(kcm_path);

    // KCM only writes the profile key — match its format exactly.
    const std::vector<std::pair<std::string, std::string>> kv_kcm = {
        {"PointerAccelerationProfile", profile},
    };
    for (auto& d : devs) {
        // KCM section header omits the bus index.
        std::string header = "[Libinput][" + std::to_string(d.vendor) + "][" +
                             std::to_string(d.product) + "][" + d.name + "]";
        kde_upsert_section(kcm_lines, header, kv_kcm);
    }
    return kde_atomic_write(kcm_path, kcm_lines);
}

// Forward declaration — defined below
static void kde_reload_input_settings();

/// Apply Flat acceleration (no acceleration) to KDE Plasma libinput config
/// for the global section AND every active (RawAccel) virtual mouse.
///
/// IMPLEMENTATION NOTE — KWin per-device libinput "toggle dance":
/// Empirically on Plasma 6, writing kwinrc + `KWin reconfigure` does NOT
/// always re-apply per-device libinput settings to a device that's already
/// running. The KCM (System Settings → Mouse) bypasses kwinrc entirely and
/// calls libinput's runtime API; toggling the checkbox there fixes it.
/// To replicate that "applied" state without user interaction we:
///   1. Write Adaptive (profile=2) → reconfigure → small sleep
///   2. Write Flat     (profile=1) → reconfigure
/// KWin observes a real change between snapshots and forces re-application.
/// Idempotent: the final on-disk state is always Flat.
///
/// Generic across any mouse / any host (vendor/product/name read from kernel).
static bool kde_write_flat_accel() {
    // Step 1: temporarily set Adaptive so the next reconfigure sees a delta
    if (!kde_write_kwinrc_accel("2", "0")) return false;
    kde_reload_input_settings();
    // Brief pause so KWin processes the first reconfigure before the second.
    // 250 ms is enough on every machine I've tested without making startup
    // noticeably slower.
    struct timespec ts = { 0, 250 * 1000 * 1000 };
    nanosleep(&ts, nullptr);
    // Step 2: settle on Flat — this is the kept state.
    return kde_write_kwinrc_accel("1", "0");
}

// ── R2-02: one-shot auto-fix marker ───────────────────────────────────────────
// on_activate() used to run kde_write_flat_accel() unconditionally on every GUI
// startup — even when nothing changed, it rewrote kwinrc+kcminputrc and spawned
// qdbus6/kcminit processes.  Now a marker file in the rawaccel config dir lists
// the kwinrc section headers the fix has already written.  The full "toggle
// dance" only runs when there is actually something new: no marker yet, or a
// hot-plugged "(RawAccel)" device whose section header is missing.  The KDE
// warning bar ("Fix Now") still covers the case where a user re-enabled
// acceleration manually — update_kde_warn_bar() detects that and re-offers the
// one-click fix.

/// Marker path: <config_dir>/kde_fix_applied (same dir as settings.json).
static std::string kde_fix_marker_path(AppState* S) {
    return (fs::path(S->config_path).parent_path() / "kde_fix_applied").string();
}

/// Section headers currently recorded in the marker.
static std::vector<std::string> kde_fix_marker_read(AppState* S) {
    std::vector<std::string> out;
    for (auto& l : kde_read_lines(kde_fix_marker_path(S)))
        if (!l.empty() && l[0] == '[') out.push_back(l);
    return out;
}

/// Persist the freshly-fixed state (global section + every current device).
static void kde_fix_marker_write(AppState* S, const std::vector<rawaccel_dev_t>& devs) {
    std::vector<std::string> lines;
    lines.push_back("[Libinput]");
    for (auto& d : devs)
        lines.push_back("[Libinput][" + std::to_string(d.bus) + "][" +
                        std::to_string(d.vendor) + "][" +
                        std::to_string(d.product) + "][" + d.name + "]");
    kde_atomic_write(kde_fix_marker_path(S), lines);
}

/// True when every current RawAccel device already has a recorded flat
/// override — i.e. the marker covers the present device set, nothing to fix.
static bool kde_fix_already_done(AppState* S, const std::vector<rawaccel_dev_t>& devs) {
    std::vector<std::string> known = kde_fix_marker_read(S);
    if (known.empty()) return false;
    for (auto& d : devs) {
        std::string header = "[Libinput][" + std::to_string(d.bus) + "][" +
                             std::to_string(d.vendor) + "][" +
                             std::to_string(d.product) + "][" + d.name + "]";
        if (std::find(known.begin(), known.end(), header) == known.end())
            return false; // a device without a recorded override → re-fix
    }
    return true;
}

/// Run a single command via fork+exec, waiting for completion. Returns true
/// if the child exited with status 0.
static bool kde_run_cmd(const char* const* argv) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], const_cast<char* const*>(argv));
        _exit(127);
    }
    if (pid < 0) return false;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/// Reload KDE input settings at runtime: tells KWin compositor to re-read
/// kwinrc AND tells KCM to re-apply kcminputrc to libinput devices. Both are
/// needed on Plasma 6 (split-brain config — see kde_write_kwinrc_accel).
static void kde_reload_input_settings() {
    // 1) Compositor: qdbus reconfigure (Plasma 6 first, then 5 fallback)
    {
        const char* a1[] = {"qdbus6", "org.kde.KWin", "/KWin", "reconfigure", nullptr};
        const char* a2[] = {"qdbus",  "org.kde.KWin", "/KWin", "reconfigure", nullptr};
        if (!kde_run_cmd(a1)) kde_run_cmd(a2);
    }
    // 2) KCM init: forces kcm_mouse plugin to read kcminputrc and call
    //    libinput's runtime API on every active device — replicates the
    //    "click → apply → close" the user does manually in System Settings.
    {
        const char* args[] = {"kcminit", "kcm_mouse", nullptr};
        kde_run_cmd(args);
    }
}

// ── Async KDE fix ─────────────────────────────────────────────────────────────
// kde_run_cmd() forks and waitpid()s up to three subprocesses and
// kde_write_flat_accel() sleeps 250 ms mid-dance, so the whole fix sequence
// takes well over 300 ms.  Running it on the GTK main thread froze the window
// on every startup (on_activate) and on every "Fix Now" click.  The sequence
// touches only files and child processes — no widgets — so it runs on a worker
// thread; the finish idle marshals the result back to the main thread.
struct kde_fix_task {
    AppState* S = nullptr;
    bool ok = false;
};

static gboolean kde_fix_finish(gpointer p) {
    auto* t = static_cast<kde_fix_task*>(p);
    AppState* S = t->S;
    if (t->ok) {
        kde_fix_marker_write(S, kde_enumerate_rawaccel_devices());
        S->kde_accel_ok = true;
        if (S->kde_warn_bar) gtk_widget_set_visible(S->kde_warn_bar, FALSE);
        set_status(S, tr("KDE: libinput acceleration disabled. Changes applied immediately."));
    } else {
        set_status(S, tr("KDE: Could not write to kwinrc. Edit manually: System Settings → Input Devices → Mouse → Pointer Acceleration = Flat."));
    }
    S->kde_fix_running = false;
    delete t;
    return G_SOURCE_REMOVE;
}

static gpointer kde_fix_worker(gpointer p) {
    auto* t = static_cast<kde_fix_task*>(p);
    t->ok = kde_write_flat_accel();
    // Apply the final Flat state: kde_write_flat_accel() reloads KWin after
    // the temporary Adaptive step only; this second reload pushes the kept
    // Flat profile to the running compositor/the KCM (applies to "Fix Now").
    if (t->ok) kde_reload_input_settings();
    g_idle_add(kde_fix_finish, t);
    return nullptr;
}

/// Start the async KDE fix if one is not already running.  No-op otherwise.
static void kde_fix_start(AppState* S) {
    if (S->kde_fix_running) return;
    S->kde_fix_running = true;
    auto* t = new kde_fix_task;
    t->S = S;
    GThread* th = g_thread_new("kde-fix", kde_fix_worker, t);
    g_thread_unref(th); // worker is detached; it keeps itself alive via idle
}

/// Callback: "Manual" button — opens KDE System Settings mouse page.
static void on_kde_open_settings(GtkWidget*, gpointer) {
    GError* err = nullptr;
    // Try the specific KCM page first (Plasma 5 & 6)
    if (!g_spawn_command_line_async("systemsettings kcm_mouse", &err)) {
        if (err) g_error_free(err);
        err = nullptr;
        // Fallback: open plain systemsettings
        g_spawn_command_line_async("systemsettings", &err);
        if (err) g_error_free(err);
    }
}

/// Callback: "Fix Now" button in the KDE warning bar.
static void on_kde_fix_clicked(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    kde_fix_start(S); // async — result/status handled in kde_fix_finish
}

/// Check KDE state and show/hide the warning bar.
static void update_kde_warn_bar(AppState* S) {
    if (!S->kde_warn_bar) return;
    if (!S->is_kde) { gtk_widget_set_visible(S->kde_warn_bar, FALSE); return; }
    int state = kde_libinput_accel_state();
    // state == 0 → flat (OK); state == 1 → adaptive (bad); -1 → unknown
    bool bad = (state == 1);
    S->kde_accel_ok = !bad;
    gtk_widget_set_visible(S->kde_warn_bar, bad ? TRUE : FALSE);
}

void on_activate(GtkApplication* gapp, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);

    // Detect KDE / Wayland upfront
    S->is_kde     = is_kde_session();
    S->is_wayland = is_wayland_session();

    // Resolve UI language: explicit preference wins, else system locale.
    // setlocale() must be initialised first so auto-detection sees the real LANG.
    setlocale(LC_ALL, "");
    S->lang_override = load_lang_override(S->lang_path);
    g_lang = resolve_lang(S->lang_override);

    // Dark CSS
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        "window, .background { background-color: #1b1b1e; }"
        "label { color: #dcdcdf; }"
        "frame { border-radius: 6px; }"
        "spinbutton { min-width: 96px; color: #dcdcdf; }"
        "entry { color: #dcdcdf; }"
        "button.suggested-action { background: #0078d4; color: white; }"
        "button.destructive-action { background: #c0392b; color: white; }"
"separator { background-color: #333336; min-height: 1px; }"
    ".sidebar { background-color: #141416; }"
    ".battery-label { color: #e0e0e0; font-weight: bold; min-width: 80px; }"
    ".test-val { font-family: monospace; font-size: large; color: #7fd4ff; min-width: 110px; }"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    register_shortcuts(S, gapp);
    build_ui(S, gapp);

    // Auto-fix KDE acceleration — but ONLY when there is something new to fix
    // (R2-02): the first run, or a hot-plugged "(RawAccel)" mouse whose kwinrc
    // override the marker doesn't list yet.  This stops the silent kwinrc write
    // + qdbus6/kcminit subprocess spawn on every GUI startup while keeping the
    // freshly-hot-plugged-mouse safety net.
    if (S->is_kde) {
        std::vector<rawaccel_dev_t> kde_devs = kde_enumerate_rawaccel_devices();
        if (!kde_fix_already_done(S, kde_devs))
            kde_fix_start(S); // async: worker + idle (no main-thread freeze)
    }

    // P-APP: KDE Wayland active-window focus relay (best-effort; no-op when
    // KWin / session bus unavailable).  Per-app profiles (`match_app`) become
    // live even for native Wayland windows via the embedded KWin script.
    if (S->is_kde && S->is_wayland)
        kwin_focus_install(S);

    // Warn on startup if multiple profiles share the same device_id
    std::string dup_warn = check_duplicate_device_ids(S->config);
    if (!dup_warn.empty())
        set_status(S, dup_warn);
    // M-8/R2-04: surface a corrupt-config startup warning (dump earlier in
    // main()); shown after build_ui() so the status bar widget exists.
    if (!S->config_load_warn.empty())
        set_status(S, dup_warn.empty() ? S->config_load_warn
                                       : dup_warn + " | " + S->config_load_warn);
}

// ── main ─────────────────────────────────────────────────────────────────────
