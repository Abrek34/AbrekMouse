// ── Mouse device discovery (no root needed) ───────────────────────────────────
#include <linux/input.h>
#include <fcntl.h>
#include <iomanip>
#include <sstream>

/// Resolve a /dev/input/eventN path to its stable /dev/input/by-id/... symlink.
/// Returns the by-id path if found, otherwise returns the original event_node.
/// Stable IDs survive reboots; eventN numbers can change on unplug/replug.
static std::string resolve_stable_id(const std::string& event_node) {
    const char* by_id_dir = "/dev/input/by-id";
    DIR* dir = opendir(by_id_dir);
    if (!dir) return event_node; // by-id not available (unusual)

    // Resolve the event_node to its canonical real path for comparison
    char real_event[PATH_MAX] = {};
    if (!realpath(event_node.c_str(), real_event)) {
        closedir(dir);
        return event_node;
    }

    struct dirent* ent;
    std::string best;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string link = std::string(by_id_dir) + "/" + ent->d_name;
        char target[PATH_MAX] = {};
        if (!realpath(link.c_str(), target)) continue;
        if (std::string(target) == std::string(real_event)) {
            // Prefer the "-event-mouse" suffix over "-mouse" (more specific).
            // Keep the first -event-mouse match (do not let a later non-event-
            // mouse, or a second -event-mouse, clobber it).  Mirrors the daemon's
            // resolve_stable_id so GUI and daemon store identical device_ids.
            if (best.empty())
                best = link;                      // first match
            else if (link.find("-event-mouse") != std::string::npos &&
                     best.find("-event-mouse") == std::string::npos)
                best = link;                      // prefer -event-mouse over other
        }
    }
    closedir(dir);
    return best.empty() ? event_node : best;
}

/// Parse /proc/bus/input/devices and return entries that look like mice.
static std::vector<InputDeviceInfo> list_mice() {
    std::vector<InputDeviceInfo> result;
    FILE* f = fopen("/proc/bus/input/devices", "r");
    if (!f) return result;

    InputDeviceInfo cur;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        // Strip trailing newline
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();

        if (s.empty()) {
            // Blank line = end of block
            if (cur.has_rel_xy && !cur.is_rawaccel && !cur.event_node.empty())
                result.push_back(cur);
            cur = {};
            continue;
        }

        if (s.size() < 3) continue;
        char type = s[0];
        std::string val = s.substr(3); // after "X: "

        if (type == 'N') {
            // N: Name="..."
            auto q1 = val.find('"');
            auto q2 = val.rfind('"');
            if (q1 != std::string::npos && q2 != q1)
                cur.name = val.substr(q1 + 1, q2 - q1 - 1);
            if (cur.name.find("RawAccel") != std::string::npos)
                cur.is_rawaccel = true;
        } else if (type == 'U') {
            // U: Uniq=xx:xx:...
            auto eq = val.find('=');
            if (eq != std::string::npos)
                cur.uniq = val.substr(eq + 1);
        } else if (type == 'H') {
            // H: Handlers=event3 mouse0 ...
            auto eq = val.find('=');
            std::string handlers = (eq != std::string::npos) ? val.substr(eq + 1) : val;
            std::istringstream iss(handlers);
            std::string tok;
            while (iss >> tok) {
                if (tok.rfind("event", 0) == 0)
                    cur.event_node = "/dev/input/" + tok;
            }
        } else if (type == 'B') {
            // B: REL=... — bit 0=REL_X, bit 1=REL_Y
            // REL_X=0, REL_Y=1 → value must have bits 0 and 1 set → last hex digit & 3 == 3
            auto eq = val.find('=');
            if (eq != std::string::npos && val.substr(0, eq) == "REL") {
                // The bitmask is a hex number. If the last (least-significant) word has bit 0+1 set:
                std::string bits = val.substr(eq + 1);
                // Trim leading/trailing spaces
                while (!bits.empty() && bits.front() == ' ') bits = bits.substr(1);
                // Last space-separated word is LS word
                auto sp = bits.rfind(' ');
                std::string lsw = (sp != std::string::npos) ? bits.substr(sp + 1) : bits;
                try {
                    unsigned long v = std::stoul(lsw, nullptr, 16);
                    if ((v & 0x3) == 0x3) cur.has_rel_xy = true; // REL_X(0) + REL_Y(1)
                } catch (...) {}
            }
        }
    }
    // Last block (no trailing blank line)
    if (cur.has_rel_xy && !cur.is_rawaccel && !cur.event_node.empty())
        result.push_back(cur);

    fclose(f);

    // Replace volatile eventN paths with stable /dev/input/by-id/... paths where possible.
    for (auto& m : result)
        m.event_node = resolve_stable_id(m.event_node);

    // Build stable_id: open each device briefly to read vendor+product via EVIOCGID.
    // If available, use "usb:VVVV:PPPP:serial" — same format as the daemon.
    for (auto& m : result) {
        int tfd = open(m.event_node.c_str(), O_RDONLY | O_NONBLOCK);
        if (tfd < 0) {
            // Try resolving through /dev/input/eventN if by-id is not accessible
            m.stable_id = m.event_node;
            continue;
        }
        struct input_id iid = {};
        if (ioctl(tfd, EVIOCGID, &iid) >= 0 && (iid.vendor || iid.product)) {
            m.vendor  = iid.vendor;
            m.product = iid.product;
            // M-BUG-5: a fixed char[] buffer truncated very long `uniq` serial
            // strings (kernel serials are short in practice, but an attacker or
            // exotic USB descriptor could overflow the snprintf truncation and
            // produce a stable_id that no longer matches the daemon's).  Build
            // the string dynamically instead.
            std::ostringstream ss;
            ss << "usb:" << std::hex << std::setfill('0') << std::nouppercase
               << std::setw(4) << iid.vendor << ":"
               << std::setw(4) << iid.product << ":" << m.uniq;
            m.stable_id = ss.str();
        } else {
            m.stable_id = m.event_node; // fallback
        }
        close(tfd);
    }

    return result;
}

// ── Mice combo refresh (shared by manual button + inotify auto-refresh) ───────

/// R2-03: Select the combo entry matching `device_id` in the current model.
/// Returns the connected mouse index (1-based) on match, 0 for "All devices".
/// If the profile is bound to a device that is NOT currently connected, append
/// a single "(unplugged)" placeholder entry (idempotent — a stale placeholder
/// from a previous device is swapped in place) and return its index.  This
/// keeps the persistent binding visible instead of silently showing the
/// "All devices (default)" fallback while the device is unplugged.
static int device_combo_select(AppState* S, const std::string& device_id) {
    if (!S->device_id_combo) return 0;
    for (int i = 0; i < (int)S->mice_list.size(); i++) {
        auto& m = S->mice_list[i];
        if ((!m.stable_id.empty() && m.stable_id == device_id) ||
            m.event_node == device_id)
            return i + 1;
    }
    if (device_id.empty()) return 0;
    GListModel* mdl = gtk_drop_down_get_model(GTK_DROP_DOWN(S->device_id_combo));
    if (mdl && G_IS_LIST_MODEL(mdl)) {
        GtkStringList* sl = GTK_STRING_LIST(mdl);
        gsize n = g_list_model_get_n_items(mdl);
        const std::string want = device_id + "  [" + tr("(unplugged)") + "]";
        if (n > 0) {
            const char* s = gtk_string_list_get_string(sl, (guint)(n - 1));
            if (s && std::string(s).find(tr("(unplugged)")) != std::string::npos) {
                gtk_string_list_remove(sl, (guint)(n - 1)); // swap stale placeholder
                n = g_list_model_get_n_items(mdl);
            }
        }
        gtk_string_list_append(sl, want.c_str());
        return (int)g_list_model_get_n_items(mdl) - 1;
    }
    return 0;
}

/// Rescans the mouse list and updates the device_id_combo model.
/// If is_auto==true (inotify-triggered), only rebuilds the model when the device
/// list actually changed — avoids the flicker from destroying/recreating the widget
/// model on every spurious inotify event.
void refresh_mice_combo(AppState* S, bool is_auto, bool quiet) {
    // O7 + D9: model/selection change triggers "notify::selected" and
    // on_param_changed() → unsaved=true can be a false positive here.
    // updating flag ile bu callback'leri sustur.
    bool prev_updating = S->updating;
    S->updating = true;

    auto new_list = list_mice();

    // Flicker fix: if auto-triggered and the list is identical, skip the
    // model rebuild entirely — only update the selection if needed.
    bool list_changed = (new_list != S->mice_list);
    S->mice_list = std::move(new_list);

    if (list_changed || !is_auto) {
        // Rebuild the GtkStringList model and assign it to the drop-down.
        GtkStringList* sl = gtk_string_list_new(nullptr);
        gtk_string_list_append(sl, tr("All devices (default)"));
        for (auto& m : S->mice_list) {
            std::string lbl = m.name + "  [" + m.event_node + "]";
            gtk_string_list_append(sl, lbl.c_str());
        }
        gtk_drop_down_set_model(GTK_DROP_DOWN(S->device_id_combo), G_LIST_MODEL(sl));
        g_object_unref(sl);
    }

    // O8: guard against UB in cur_prof() when the profile list is empty
    if (!S->config.profiles.empty())
        gtk_drop_down_set_selected(GTK_DROP_DOWN(S->device_id_combo),
                                   (guint)device_combo_select(S, cur_prof(S).device_id));

    S->updating = prev_updating;

    if (!is_auto && !quiet) {
        set_status(S, tr("Device list refreshed: ") +
                   std::to_string(S->mice_list.size()) + tr(" mouse(s) found."));
    }
}

/// GLib IO callback: reads /dev/input inotify events and refreshes the combo.
gboolean on_inotify_event(GIOChannel* chan, GIOCondition /*cond*/, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    // ERR-4: the buffer is reinterpret_cast to struct inotify_event* — without
    // alignas() that pointer is misaligned on strict-alignment targets (ARM),
    // which is UB.  Force the array to the type's alignment.
    alignas(struct inotify_event) char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    gsize bytes_read = 0;
    // Non-blocking read — drain all pending events
    while (true) {
        GError* err = nullptr;
        GIOStatus st = g_io_channel_read_chars(chan, buf, sizeof(buf), &bytes_read, &err);
        if (err) { g_error_free(err); break; }
        if (st != G_IO_STATUS_NORMAL || bytes_read < sizeof(struct inotify_event)) break;

        // BUG-NEW-14 (aj4): a single read() may pack several inotify events;
        // walk them all (fixed header + variable name), not just the first.
        size_t off = 0;
        while (off + sizeof(struct inotify_event) <= bytes_read) {
            const auto* ev = reinterpret_cast<const struct inotify_event*>(buf + off);
            off += sizeof(struct inotify_event) + ev->len;
            // Only process nodes starting with "event"
            if (ev->len > 0 && std::strncmp(ev->name, "event", 5) == 0) {
                // O8: skip refresh if the profile list is not yet loaded
                if (!S->config.profiles.empty())
                    refresh_mice_combo(S, /*is_auto=*/true);
                return TRUE; // one refresh per callback is enough (batches consecutive events)
            }
        }
    }
    return TRUE; // keep the source active
}

/// GLib IO callback for /dev hidraw create/delete events.  HID++ discovery is
/// intentionally kicked off outside the callback; the scan itself performs
/// all opens and feature requests on its worker thread.
gboolean on_hidraw_inotify_event(GIOChannel* chan, GIOCondition /*cond*/,
                                 gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    gsize bytes_read = 0;
    bool changed = false;
    while (true) {
        GError* err = nullptr;
        GIOStatus st = g_io_channel_read_chars(chan, buf, sizeof(buf),
                                                &bytes_read, &err);
        if (err) { g_error_free(err); break; }
        if (st != G_IO_STATUS_NORMAL ||
            bytes_read < sizeof(struct inotify_event))
            break;
        // Walk all packed inotify events in the buffer
        size_t offset = 0;
        while (offset + sizeof(struct inotify_event) <= bytes_read) {
            const auto* ev = reinterpret_cast<const struct inotify_event*>(buf + offset);
            if (ev->len > 0 && std::strncmp(ev->name, "hidraw", 6) == 0)
                changed = true;
            offset += sizeof(struct inotify_event) + ev->len;
        }
    }
    if (changed) hw_start_scan(S);
    return TRUE;
}
