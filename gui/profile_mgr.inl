// ── Profile management: CRUD dialogs (new / rename / delete / duplicate / reset) ──

#include "../include/presets.hpp"

#include <fstream>
#include <cerrno>

/// Write a text file at `path`, atomically via a temp file + rename (same
/// discipline as save_config). Returns true on success, false on I/O failure.
///
/// BUG-NEW-07 (aj4): the temp file is opened with O_EXCL|O_NOFOLLOW so an
/// attacker pre-placing a symlink at "path.tmp" cannot redirect the write
/// onto an arbitrary target (save_config uses the same protection).
static bool write_text_file(const char* path, const std::string& content) {
    std::string tmp = std::string(path) + ".tmp";
    // FINDING-21-1: no pre-open unlink() — the EEXIST branch below already
    // covers a stale temp from an earlier aborted write (same as save_config).
    int fd = ::open(tmp.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0 && errno == EEXIST) {
        ::unlink(tmp.c_str());
        fd = ::open(tmp.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    }
    if (fd < 0) return false;
    const char* p = content.data();
    size_t left = content.size();
    while (left > 0) {
        ssize_t w = ::write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            ::close(fd); ::unlink(tmp.c_str());
            return false;
        }
        p += w; left -= (size_t)w;
    }
    if (::fsync(fd) != 0) {
        ::close(fd); ::unlink(tmp.c_str());
        return false;
    }
    ::close(fd);
    if (::rename(tmp.c_str(), path) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

/// Compact, localized one-line description of a built-in preset. Rendered from
/// make_preset() (the single source of truth) so the preview can never drift
/// from the values actually applied to the profile. Empty for unknown presets.
static std::string preset_preview_text(const std::string& preset) {
    auto dp = rawaccel::make_preset(preset, "_"); // non-empty name marks known presets
    if (dp.name.empty()) return ""; // unknown preset — no preview
    const auto& m = dp.prof.accel_x;
    std::string mode;
    switch (m.mode) {
    case accel_mode::jump:        mode = tr("Jump");         break;
    case accel_mode::synchronous: mode = tr("Synchronous");  break;
    case accel_mode::lookup:      mode = tr("Lookup (LUT)"); break;
    case accel_mode::power:       mode = tr("Power");        break;
    case accel_mode::classic:     mode = tr("Classic");      break;
    case accel_mode::natural:     mode = tr("Natural");      break;
    case accel_mode::noaccel:     mode = tr("None (1:1)");   break;
    default:                      mode = tr("Unknown");      break;
    }
    auto dfmt = [](double v) {
        std::ostringstream os;
        os << std::setprecision(3) << v; // defaultfloat, '.' decimal separator
        return os.str();
    };
    std::string summary = std::string(tr("mode")) + ": " + mode;
    if (dp.prof.raw_passthrough) {
        summary += std::string(", ") + tr("no acceleration (1:1 raw)");
        return trf("%s → %s", preset.c_str(), summary.c_str());
    }
    if (m.gain) summary += std::string(", ") + tr("gain");
    switch (m.mode) {
    case accel_mode::classic:
        summary += std::string(", ") + tr("exp") + " " + dfmt(m.exponent_classic)
                + std::string(", ") + tr("cap") + " " + dfmt(m.cap.x) + "x" + dfmt(m.cap.y);
        break;
    case accel_mode::power:
        summary += std::string(", ") + tr("scale") + " " + dfmt(m.scale)
                + std::string(", ") + tr("exp") + " " + dfmt(m.exponent_power);
        break;
    case accel_mode::natural:
        summary += std::string(", ") + tr("limit") + " " + dfmt(m.limit);
        break;
    default:
        break;
    }
    return trf("%s → %s", preset.c_str(), summary.c_str());
}

// "New Profile" dialog: name entry + optional build-in preset dropdown. A
// chosen preset seeds the profile via make_preset() (single source shared with
// the CLI); "None (blank)" keeps the current behaviour (defaults).
void show_new_profile_dialog(AppState* S) {
    GtkWidget* dlg  = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dlg), tr("New Profile"));
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(S->window));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 340, -1);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dlg), vbox);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), tr("Profile name (e.g. gaming)"));
    gtk_entry_set_max_length(GTK_ENTRY(entry), 256); // match CLI/load 256-char cap
    gtk_box_append(GTK_BOX(vbox), entry);

    GtkWidget* preset_label = gtk_label_new(tr("Preset"));
    gtk_widget_set_halign(preset_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), preset_label);

    GtkStringList* sl = gtk_string_list_new(nullptr);
    gtk_string_list_append(sl, tr("None (blank)"));
    for (int i = 0; i < rawaccel::PRESET_COUNT; ++i)
        gtk_string_list_append(sl, rawaccel::PRESET_NAMES[i]);
    GtkWidget* combo = gtk_drop_down_new(G_LIST_MODEL(sl), nullptr);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), 0);
    g_object_unref(sl); // dropdown holds its own reference
    gtk_box_append(GTK_BOX(vbox), combo);

    // Preset preview: a compact read-only summary of the selected preset,
    // updated from make_preset() (single source of truth) on every selection.
    GtkWidget* preset_info = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(preset_info), 0.0);
    gtk_label_set_wrap(GTK_LABEL(preset_info), TRUE);
    gtk_widget_set_margin_top(preset_info, 2);
    gtk_widget_set_visible(preset_info, FALSE); // "None (blank)" selected initially
    gtk_box_append(GTK_BOX(vbox), preset_info);
    g_signal_connect(combo, "notify::selected",
        G_CALLBACK(+[](GtkWidget* dd, GParamSpec*, gpointer data) {
            GtkWidget* info = GTK_WIDGET(data);
            int idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
            std::string preset =
                (idx > 0 && idx <= rawaccel::PRESET_COUNT) ? rawaccel::PRESET_NAMES[idx - 1] : "";
            std::string txt = preset.empty() ? "" : preset_preview_text(preset);
            if (txt.empty()) {
                gtk_widget_set_visible(info, FALSE);
            } else {
                gtk_label_set_wrap(GTK_LABEL(info), TRUE);
                gtk_label_set_text(GTK_LABEL(info), txt.c_str());
                gtk_widget_set_visible(info, TRUE);
            }
        }), preset_info);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), hbox);

    GtkWidget* cancel_btn = gtk_button_new_with_label(tr("Cancel"));
    GtkWidget* ok_btn     = gtk_button_new_with_label(tr("OK"));
    gtk_widget_add_css_class(ok_btn, "suggested-action");
    gtk_box_append(GTK_BOX(hbox), cancel_btn);
    gtk_box_append(GTK_BOX(hbox), ok_btn);

    struct NewProfileCtx {
        std::function<void(const std::string&, const std::string&)> cb;
        GtkWidget* entry;
        GtkWidget* combo;
    };
    auto* ctx = new NewProfileCtx{};
    ctx->entry = entry;
    ctx->combo = combo;
    ctx->cb = [S](const std::string& name, const std::string& preset) {
            for (auto& p : S->config.profiles)
                if (p.name == name) { set_status(S, tr("A profile with that name already exists.")); return; }
            device_profile dp;
            if (!preset.empty())
                dp = rawaccel::make_preset(preset, name);
            else {
                dp.name = name;
                dp.dev_cfg.dpi = 800;
                dp.dev_cfg.polling_rate = 1000;
            }
            S->config.profiles.push_back(dp);
            S->current_profile_idx = (int)S->config.profiles.size() - 1;
            S->config.active_profile = name;
            rebuild_profile_combo(S);
            save_config_now(S);
        };
    g_object_set_data_full(G_OBJECT(dlg), "ctx", ctx,
                           [](gpointer p) { delete (NewProfileCtx*)p; });

    auto do_ok = +[](GtkWidget*, gpointer dlg_ptr) {
        GtkWidget* d = GTK_WIDGET(dlg_ptr);
        auto* ctx = static_cast<NewProfileCtx*>(g_object_get_data(G_OBJECT(d), "ctx"));
        std::string name = gtk_editable_get_text(GTK_EDITABLE(ctx->entry));
        int idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(ctx->combo));
        std::string preset =
            (idx > 0 && idx <= rawaccel::PRESET_COUNT) ? rawaccel::PRESET_NAMES[idx - 1] : "";
        if (!name.empty() && ctx->cb) ctx->cb(name, preset);
        gtk_window_destroy(GTK_WINDOW(d));
    };

    g_signal_connect(ok_btn,     "clicked", G_CALLBACK(do_ok), dlg);
    g_signal_connect(cancel_btn, "clicked",
        G_CALLBACK(+[](GtkWidget*, gpointer d){ gtk_window_destroy(GTK_WINDOW(d)); }), dlg);
    g_signal_connect(entry, "activate", G_CALLBACK(do_ok), dlg);

    gtk_window_present(GTK_WINDOW(dlg));
}

void on_new_profile(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    show_new_profile_dialog(S);
}

void rebuild_profile_combo(AppState* S) {
    // Block on_profile_changed while we rebuild the model to avoid double-update
    S->updating = true;

    GtkStringList* sl = gtk_string_list_new(nullptr);
    for (auto& p : S->config.profiles) {
        // Mark the active profile with ★ so it's visible at a glance
        std::string label = p.name;
        if (p.name == S->config.active_profile)
            label = "\xe2\x98\x85 " + p.name;  // UTF-8 ★
        gtk_string_list_append(sl, label.c_str());
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(S->profile_combo), G_LIST_MODEL(sl));
    g_object_unref(sl); // drop our ref — the dropdown keeps the model alive

    int idx = S->current_profile_idx;
    if (idx >= (int)S->config.profiles.size())
        idx = (int)S->config.profiles.size() - 1;
    S->current_profile_idx = std::max(0, idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(S->profile_combo),
                               S->current_profile_idx);

    S->updating = false;
    profile_to_widgets(S);
}

void show_input_dialog(AppState* S,
                              const char* title, const char* placeholder,
                              const char* initial,
                              std::function<void(const std::string&)> cb) {
    GtkWidget* dlg  = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dlg), title);
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(S->window));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 320, -1);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);
    gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dlg), vbox);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    gtk_entry_set_max_length(GTK_ENTRY(entry), 256); // match CLI/load 256-char cap
    if (initial && strlen(initial) > 0)
        gtk_editable_set_text(GTK_EDITABLE(entry), initial);
    gtk_box_append(GTK_BOX(vbox), entry);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), hbox);

    GtkWidget* cancel_btn = gtk_button_new_with_label(tr("Cancel"));
    GtkWidget* ok_btn     = gtk_button_new_with_label(tr("OK"));
    gtk_widget_add_css_class(ok_btn, "suggested-action");
    gtk_box_append(GTK_BOX(hbox), cancel_btn);
    gtk_box_append(GTK_BOX(hbox), ok_btn);

    // Store callback in a heap-allocated wrapper
    auto* cb_ptr = new std::function<void(const std::string&)>(cb);
    g_object_set_data_full(G_OBJECT(dlg), "cb", cb_ptr,
                           [](gpointer p) { delete (std::function<void(const std::string&)>*)p; });
    g_object_set_data(G_OBJECT(dlg), "entry", entry);

    auto do_ok = +[](GtkWidget*, gpointer dlg_ptr) {
        GtkWidget* e = GTK_WIDGET(g_object_get_data(G_OBJECT(dlg_ptr), "entry"));
        auto* cb_p   = (std::function<void(const std::string&)>*)
                        g_object_get_data(G_OBJECT(dlg_ptr), "cb");
        std::string val = gtk_editable_get_text(GTK_EDITABLE(e));
        if (!val.empty() && cb_p) (*cb_p)(val);
        gtk_window_destroy(GTK_WINDOW(dlg_ptr));
    };

    g_signal_connect(ok_btn,     "clicked", G_CALLBACK(do_ok),   dlg);
    g_signal_connect(cancel_btn, "clicked",
        G_CALLBACK(+[](GtkWidget*, gpointer d){ gtk_window_destroy(GTK_WINDOW(d)); }), dlg);

    // Enter key in entry = ok
    g_signal_connect(entry, "activate", G_CALLBACK(do_ok), dlg);

    gtk_window_present(GTK_WINDOW(dlg));
}

void on_rename_profile(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->config.profiles.empty()) return;
    const int prof_idx = S->current_profile_idx;
    std::string old_name = cur_prof(S).name;
    show_input_dialog(S, tr("Rename Profile"), tr("New name"), old_name.c_str(),
        [S, old_name, prof_idx](const std::string& name) {
            if (name.empty()) return;
            if (prof_idx < 0 || prof_idx >= (int)S->config.profiles.size()) return;
            // Duplicate check
            for (auto& p : S->config.profiles)
                if (p.name == name && p.name != old_name) {
                    set_status(S, tr("A profile with that name already exists."));
                    return;
                }
            S->config.profiles[prof_idx].name = name;
            // Update active_profile if we renamed the active one
            if (S->config.active_profile == old_name)
                S->config.active_profile = name;
            rebuild_profile_combo(S);
            save_config_now(S);
        });
}

void on_delete_profile(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->config.profiles.size() <= 1) {
        set_status(S, tr("At least one profile is required."));
        return;
    }

    // Confirm dialog
    GtkWidget* dlg = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dlg), tr("Delete Profile"));
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(S->window));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 300, -1);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 16); gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);   gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dlg), vbox);

    std::string msg = trf("Delete profile \"%s\"?", cur_prof(S).name.c_str());
    GtkWidget* lbl = gtk_label_new(msg.c_str());
    gtk_box_append(GTK_BOX(vbox), lbl);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), hbox);

    GtkWidget* cancel_btn = gtk_button_new_with_label(tr("Cancel"));
    GtkWidget* del_btn    = gtk_button_new_with_label(tr("Delete"));
    gtk_widget_add_css_class(del_btn, "destructive-action");
    gtk_box_append(GTK_BOX(hbox), cancel_btn);
    gtk_box_append(GTK_BOX(hbox), del_btn);

    g_signal_connect(cancel_btn, "clicked",
        G_CALLBACK(+[](GtkWidget*, gpointer d){ gtk_window_destroy(GTK_WINDOW(d)); }), dlg);

    // Store AppState* in the dialog so the lambda can access it
    g_object_set_data(G_OBJECT(dlg), "app-state", S);
    g_signal_connect(del_btn, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
        auto* dlg_w = GTK_WIDGET(d);
        auto* S2 = static_cast<AppState*>(g_object_get_data(G_OBJECT(dlg_w), "app-state"));
        int idx = S2->current_profile_idx;
        S2->config.profiles.erase(S2->config.profiles.begin() + idx);
        S2->current_profile_idx = std::max(0, idx - 1);
        S2->config.active_profile = S2->config.profiles[S2->current_profile_idx].name;
        rebuild_profile_combo(S2);
        save_config_now(S2);
        gtk_window_destroy(GTK_WINDOW(d));
    }), dlg);

    gtk_window_present(GTK_WINDOW(dlg));
}

void on_duplicate_profile(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->config.profiles.empty()) return;
    // BUG-08: naive "… (copy)" collided with an existing profile name, silently
    // leaving two profiles with the same name (ambiguous combo + daemon lookup).
    // Auto-uniquify: "<orig> (copy)", then "<orig> (copy) 2", 3, … — same rule
    // as the create/rename dialog's duplicate check, applied to the whole list.
    std::string base = cur_prof(S).name + tr(" (copy)");
    std::string name = base;
    for (int n = 1; n <= 1000; n++) {
        bool taken = false;
        for (const auto& p : S->config.profiles)
            if (p.name == name) { taken = true; break; }
        if (!taken) break;
        if (n == 1) name = base + " 2";
        else        name = base + " " + std::to_string(n + 1);
    }
    // L-BUG-32: if all 1000 generated names collided, give up instead of
    // silently adding a duplicate name (ambiguous combo + daemon lookup).
    for (const auto& p : S->config.profiles)
        if (p.name == name) {
            set_status(S, tr("Could not duplicate profile: no free name."));
            return;
        }
    device_profile copy = cur_prof(S);
    copy.name = name;
    copy.device_id.clear();
    S->config.profiles.push_back(copy);
    S->current_profile_idx = (int)S->config.profiles.size() - 1;
    S->config.active_profile = name;
    rebuild_profile_combo(S);
    save_config_now(S);
}

void on_reset_profile(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->config.profiles.empty()) return;

    // Confirm dialog
    GtkWidget* dlg = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dlg), tr("Reset to Defaults"));
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(S->window));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 320, -1);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 16); gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 16);   gtk_widget_set_margin_bottom(vbox, 16);
    gtk_window_set_child(GTK_WINDOW(dlg), vbox);

std::string msg = trf("Reset \"%s\" to default values?\nThis cannot be undone.",
                       cur_prof(S).name.c_str());
    GtkWidget* lbl = gtk_label_new(msg.c_str());
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_box_append(GTK_BOX(vbox), lbl);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), hbox);

    GtkWidget* cancel_btn = gtk_button_new_with_label(tr("Cancel"));
    GtkWidget* reset_btn  = gtk_button_new_with_label(tr("Reset"));
    gtk_widget_add_css_class(reset_btn, "destructive-action");
    gtk_box_append(GTK_BOX(hbox), cancel_btn);
    gtk_box_append(GTK_BOX(hbox), reset_btn);

    g_signal_connect(cancel_btn, "clicked",
        G_CALLBACK(+[](GtkWidget*, gpointer d){ gtk_window_destroy(GTK_WINDOW(d)); }), dlg);

    // Store AppState* in the dialog for the reset lambda
    g_object_set_data(G_OBJECT(dlg), "app-state", S);
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
        auto* dlg_w = GTK_WIDGET(d);
        auto* S2 = static_cast<AppState*>(g_object_get_data(G_OBJECT(dlg_w), "app-state"));
        // Keep name and device_id, reset everything else to defaults
        std::string saved_name = cur_prof(S2).name;
        std::string saved_id   = cur_prof(S2).device_id;
        device_profile fresh;
        fresh.name      = saved_name;
        fresh.device_id = saved_id;
        S2->config.profiles[S2->current_profile_idx] = fresh;
        profile_to_widgets(S2);
        S2->unsaved = true;
        set_status(S2, tr("Profile reset to defaults — press Save to keep."));
        gtk_window_destroy(GTK_WINDOW(d));
    }), dlg);

    gtk_window_present(GTK_WINDOW(dlg));
}

// ── Import / export profile (JSON) — CLI `import` / `export` parity ─────────

static void export_profile_done(GObject* src, GAsyncResult* res, gpointer ud) {
    auto* S = static_cast<AppState*>(ud);
    GtkFileDialog* dlg = GTK_FILE_DIALOG(src);
    GFile* file = gtk_file_dialog_save_finish(dlg, res, nullptr);
    if (!file) return; // user cancelled
    gchar* path = g_file_get_path(file);
    g_object_unref(file);
    if (!path) return;
    try {
        std::string json = profile_to_json(cur_prof(S)) + "\n";
        if (!write_text_file(path, json))
            set_status(S, trf("Export failed: cannot write %s", path));
        else
            set_status(S, trf("Exported profile \"%s\" → %s", cur_prof(S).name.c_str(), path));
    } catch (std::exception& e) {
        set_status(S, trf("Export failed: %s", e.what()));
    }
    g_free(path);
}

void on_export_profile(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->config.profiles.empty()) return;
    GtkFileDialog* dlg = gtk_file_dialog_new();
    std::string base = cur_prof(S).name + ".json";
    gtk_file_dialog_set_initial_name(dlg, base.c_str());
    gtk_file_dialog_save(dlg, GTK_WINDOW(S->window), nullptr,
                         export_profile_done, S);
}

static void import_profile_done(GObject* src, GAsyncResult* res, gpointer ud) {
    auto* S = static_cast<AppState*>(ud);
    GtkFileDialog* dlg = GTK_FILE_DIALOG(src);
    GFile* file = gtk_file_dialog_open_finish(dlg, res, nullptr);
    if (!file) return; // user cancelled
    gchar* path = g_file_get_path(file);
    g_object_unref(file);
    if (!path) return;

    GError* err = nullptr;
    gchar* data = nullptr;
    gsize   len  = 0;
    if (!g_file_get_contents(path, &data, &len, &err)) {
        set_status(S, trf("Import failed: %s", err ? err->message : "read error"));
        g_clear_error(&err);
        g_free(path);
        return;
    }
    constexpr gsize MAX_IMPORT_BYTES = 4 * 1024 * 1024; // 4 MiB
    if (len > MAX_IMPORT_BYTES) {
        set_status(S, tr("Import failed: file too large."));
        g_free(data);
        g_free(path);
        return;
    }
    std::string content(data, len);
    g_free(data);
    device_profile dp;
    try {
        dp = profile_from_json(content);
    } catch (std::exception& e) {
        set_status(S, trf("Import failed: invalid profile JSON — %s", e.what()));
        g_free(path);
        return;
    }
    if (dp.name.empty()) {
        set_status(S, tr("Import failed: profile has no \"name\"."));
        g_free(path);
        return;
    }
    for (auto& existing : S->config.profiles) {
        if (existing.name == dp.name) {
            set_status(S, trf("Import failed: profile \"%s\" already exists.", dp.name.c_str()));
            g_free(path);
            return;
        }
    }
    S->config.profiles.push_back(dp);
    rebuild_profile_combo(S);
    save_config_now(S);
    set_status(S, trf("Imported profile: %s", dp.name.c_str()));
    g_free(path);
}

void on_import_profile(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    GtkFileDialog* dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, tr("Import profile (JSON)"));
    gtk_file_dialog_open(dlg, GTK_WINDOW(S->window), nullptr,
                         import_profile_done, S);
}
