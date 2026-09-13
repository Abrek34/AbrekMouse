// ── Logitech HID++ hardware panel (onboard DPI / report rate / LOD) ───────────
//
// GUI counterpart of the CLI `hidpp-set-dpi` / `hidpp-set-rate` /
// `hidpp-set-polling-rate` and `hidpp-set-lod` commands.  It writes directly
// to /dev/hidraw* (HID++ 2.0 transport in src/logitech_hidpp.cpp).  These are
// PHYSICAL device settings stored on the mouse; they are independent of the
// software profile DPI / polling-rate above.
//
// All HID++ I/O (open, feature discovery, queries, writes, per-request 500 ms
// response timeouts) runs on a GLib worker thread so the GTK main loop never
// blocks.  Results are marshalled back to the main thread with g_idle_add
// (runs on the default GTK main context; available in all GTK4-era GLib).
// Widget values are only read/written on the main thread; workers carry
// plain-data structs.
//
// Permission model: scripts/99-rawaccel.rules grants the 'input' group (plus
// uaccess for the active seat user) access to /dev/hidraw*, so a normal user
// can use this panel without root.  On failure the panel shows a hint.
//
// Widgets are constructed in ui_builder.inl; this file provides the model
// range and the signal handlers.

#include <cerrno>
#include <climits>

static constexpr uint32_t HW_RATES[] = {125, 250, 500, 1000, 2000, 4000, 8000};
static constexpr int HW_NRATES = (int)(sizeof(HW_RATES) / sizeof(HW_RATES[0]));
static constexpr int HW_DPI_MIN = 100;
static constexpr int HW_DPI_MAX = 32000;

// ── String / status helpers (read-only; safe from any thread) ────────────────
static std::string cur_dpi_text(int dpi) {
    return dpi > 0 ? std::to_string(dpi) : tr("unknown");
}

static std::string lod_text(int lod, bool supported) {
    if (!supported) return tr("unsupported");
    const char* en[] = {"Low", "Medium", "High"};
    return tr(en[std::clamp(lod, 0, 2)]);
}

static void hw_set_status(AppState* S, const std::string& text) {
    if (S->hw_status_lbl)
        gtk_label_set_text(GTK_LABEL(S->hw_status_lbl), text.c_str());
}

// ── P169: battery / capability renderers (main thread only) ─────────────────

static const char* battery_source_name(logitech_battery_source s) {
    switch (s) {
    case logitech_battery_source::unified_battery: return tr("Unified Battery");
    case logitech_battery_source::centurion_battery: return tr("Centurion Battery");
    case logitech_battery_source::battery_status:  return tr("Battery Status");
    case logitech_battery_source::battery_voltage: return tr("Battery Voltage");
    case logitech_battery_source::legacy10:        return tr("HID++ 1.0 register");
    case logitech_battery_source::none: default:   return tr("not advertised");
    }
}

/// Show the queried (or notification-fed) battery for the selected device.
/// Guards against a stale worker result overwriting a newer selection.
void hw_set_battery(AppState* S, int idx,
                    const std::optional<hidpp_battery_info>& battery,
                    logitech_battery_source source) {
    if (!S->hw_battery_lbl) return;
    const int selected = S->hw_dev_combo
        ? (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->hw_dev_combo)) : -1;
    if (selected != idx) return;
    if (!battery || source == logitech_battery_source::none) {
        gtk_label_set_markup(GTK_LABEL(S->hw_battery_lbl),
            trf("<b>Battery:</b> %s", tr("not advertised")).c_str());
        return;
    }
    const std::string level = battery->level == 255
        ? tr("Unknown") : std::to_string((int)battery->level) + "%";
    const char* state = !battery->online ? tr("Offline")
        : (battery->charging ? tr("Charging") : tr("Discharging"));
    gtk_label_set_markup(GTK_LABEL(S->hw_battery_lbl),
        trf("<b>Battery:</b> %s (%s) · %s",
            level.c_str(), state, battery_source_name(source)).c_str());
}

/// Capability summary for the selected device.  Feature-derived only: fields
/// whose feature the device does not advertise are spelled out as unsupported
/// (the matching widgets stay disabled in hw_update_ui_state).
void hw_render_caps(AppState* S, int idx) {
    if (!S->hw_caps_lbl) return;
    // No HID++ hardware found at all (e.g. a non-Logitech or virtual mouse):
    // never leave the panel silently greyed — spell out WHY the onboard
    // controls are disabled and point at the always-active software settings.
    if (S->hidpp_devs.empty()) {
        gtk_label_set_text(GTK_LABEL(S->hw_caps_lbl),
            (std::string(tr("The connected mouse is not a Logitech HID++ device, so the\n"
                            "hardware controls below (DPI / polling rate / lift-off) stay disabled.")) +
             "\n\n" +
             tr("These need a physical Logitech HID++ mouse (wired, receiver or Bluetooth).") +
             "\n\n" +
             tr("Change sensitivity on any mouse with the software settings instead:\n"
                "Device section → DPI, Polling Rate, Output DPI — always active."))
            .c_str());
        return;
    }
    if (idx < 0 || idx >= (int)S->hidpp_devs.size()) {
        gtk_label_set_text(GTK_LABEL(S->hw_caps_lbl), tr("Capabilities: —"));
        return;
    }
    const auto& dev = S->hidpp_devs[idx];
    const logitech_controls c = logitech_controls_for(dev.features);
    std::string m;

    // A Logitech hidraw node that implements neither HID++ 2.0 nor HID++ 1.0
    // (e.g. the 046d:c542 Nano receiver) is listed so the panel is never
    // silently empty, but no onboard control can apply to it.
    if (dev.info.protocol_version == 0) {
        m += tr("HID++ protocol: not implemented by this device");
        m += "\n";
        m += tr("Onboard DPI / report rate / LOD are not available on this hardware.");
        m += "\n";
        gtk_label_set_text(GTK_LABEL(S->hw_caps_lbl), m.c_str());
        return;
    }

    if (!c.dpi) m += tr("DPI: not advertised");
    else if (c.dpi_xy) m += tr("DPI: adjustable + X/Y (extended)");
    else m += tr("DPI: adjustable");
    m += "\n";

    m += c.lod ? tr("LOD: supported (0x2202)")
               : tr("LOD: not advertised");
    m += "\n";

    if (c.report_rate_extended) m += tr("Rate: extended (up to 8 kHz)");
    else if (c.report_rate)     m += tr("Rate: legacy (1..8 ms)");
    else                        m += tr("Rate: not advertised");
    m += "\n";

    m += c.onboard_profiles ? tr("Onboard profiles: advertised (read-only)")
                            : tr("Onboard profiles: not advertised");
    m += "\n";

    const logitech_battery_source bs = preferred_battery_source(
        dev.features, dev.info.protocol_version < 2);
    m += std::string(tr("Battery source: ")) + battery_source_name(bs);
    m += "\n";

    const std::string mid = logitech_compose_model_id(dev.info);
    if (!mid.empty()) {
        m += std::string(tr("Model ID: ")) + mid + "\n";
        if (find_logitech_quirks(dev.info))
            m += std::string(tr("Model quirks: known (write-protect policy)")) + "\n";
    }
    if (S->hw_daemon_battery >= 0 && S->hw_daemon_battery <= 100)
        m += trf("Daemon live battery: %d%%", S->hw_daemon_battery) + "\n";

    while (!m.empty() && m.back() == '\n') m.pop_back();
    gtk_label_set_text(GTK_LABEL(S->hw_caps_lbl), m.c_str());
}

// ── Forward declarations (defined below the worker threads) ─────────────────
void hw_update_ui_state(AppState* S);
void hw_query_current(AppState* S);

// ── Worker task payloads ─────────────────────────────────────────────────────

struct HwScanTask {
    AppState* S;
};

struct HwQueryTask {
    AppState* S;
    int idx;
    std::string hidraw_path;   // snapshot taken on the main thread
    int device_index;          // snapshot taken on the main thread
    std::vector<std::pair<uint16_t, uint8_t>> features; // P169 battery source
    bool hidpp10 = false;      // P169: HID++ 1.0 register battery fallback
    // R2-08: hidpp_devs generation captured at schedule time.  The idle
    // callback drops the result when the vector was rebuilt in the meantime
    // (same index may now name a different physical device).
    int devs_version = 0;
    // Device capability data for per-device widget ranges
    std::vector<uint16_t> dpi_levels;
    bool dpi_xy = false;
    std::vector<uint32_t> rate_codes_hz;
    bool supports_lod = false;
};

struct HwApplyTask {
    AppState* S;
    int idx;
    std::string hidraw_path;   // snapshot taken on the main thread
    int device_index;          // snapshot taken on the main thread
    uint16_t dpi;
    uint32_t rate_hz;
    int lod; // hidpp_lift_off_distance value
    int devs_version = 0;
    // Capability data for validation before write
    std::vector<uint16_t> dpi_levels;
    std::vector<uint32_t> rate_codes_hz;
    bool supports_lod = false;
};

struct HwNotificationTask {
    AppState* S;
    int idx;
    std::string hidraw_path;
    int device_index;
    std::vector<std::pair<uint16_t, uint8_t>> features;
    // R2-08: capture the legacy-protocol flag at schedule time.  The idle
    // callback must NOT re-read S->hidpp_devs at the old index — a completed
    // rescan may have replaced the vector, mapping idx to a different device.
    bool legacy_protocol = false;
    int devs_version = 0;
};

// Fire-and-forget GLib thread.  We do not join; GLib frees the handle once the
// thread has finished (g_thread_unref drops our reference).
static GThread* hw_thread(const char* name, GThreadFunc fn, gpointer task) {
    GThread* t = g_thread_new(name, fn, task);
    g_thread_unref(t);
    return t;
}

// Enumerate Logitech hidraw devices.  Runs off the main thread.
static gpointer hw_scan_thread(gpointer data) {
    auto* task = static_cast<HwScanTask*>(data);
    AppState* S = task->S;

    std::vector<hidpp_device> devs;
    for (const auto& path : discover_logitech_hidraw_devices()) {
        for (auto& dev : identify_logitech_devices(path))
            devs.push_back(std::move(dev));
    }

    struct Result { AppState* S; std::vector<hidpp_device> devs; };
    auto* res = new Result();
    res->S = S;
    res->devs = std::move(devs);
    delete task;
    g_idle_add(+[](gpointer p) -> gboolean {
        auto* r = static_cast<Result*>(p);
        AppState* S = r->S;
        if (S->hw_cancel) { delete r; return G_SOURCE_REMOVE; }
        S->hw_busy = false;
        // GUI-Y3: the combo model was rebuilt below, so any stashed pending
        // selection indexes the OLD vector and is stale — discard it; the
        // hw_query_current() at the end re-queries the re-populated combo.
        S->hw_pending_query = -1;
        S->hidpp_devs = std::move(r->devs);
        // R2-08: invalidate any in-flight query/notification results — they
        // were snapshotted against the previous vector generation.
        ++S->hw_devs_version;
        if (S->hw_dev_combo) {
            GtkStringList* sl = gtk_string_list_new(nullptr);
            for (const auto& d : S->hidpp_devs) {
                const std::string& display_name = d.info.friendly_name.empty()
                    ? (d.info.name.empty() ? d.hidraw_path : d.info.name)
                    : d.info.friendly_name;
                std::string label = display_name.empty()
                    ? d.hidraw_path
                    : display_name + "  [" + d.hidraw_path + "]";
                gtk_string_list_append(sl, label.c_str());
            }
            GtkDropDown* dd = GTK_DROP_DOWN(S->hw_dev_combo);
            gtk_drop_down_set_model(dd, G_LIST_MODEL(sl));
            g_object_unref(sl);
            if (S->hidpp_devs.empty())
                gtk_drop_down_set_selected(dd, GTK_INVALID_LIST_POSITION);
            else
                gtk_drop_down_set_selected(dd, 0);
        }
        hw_update_ui_state(S);
        if (S->hidpp_devs.empty()) {
            hw_set_status(S, tr("No Logitech HID++ devices found."));
            hw_render_caps(S, -1); // explain why the controls are disabled
        } else {
            hw_query_current(S); // trigger a query (no-op if notify already did)
        }
        delete r;
        return G_SOURCE_REMOVE;
    }, res);
    return nullptr;
}

// Read the selected device's current onboard settings.  Off the main thread.
static gpointer hw_query_thread(gpointer data) {
    auto* task = static_cast<HwQueryTask*>(data);
    AppState* S = task->S;

    struct Current {
        bool ok = false;
        int dpi = 0;              // current DPI, 0 = unknown
        int rate_hz = 0;          // current rate in Hz, 0 = unknown
        int lod = 0;              // hidpp_lift_off_distance value
        bool supports_lod = false;
        std::optional<hidpp_battery_info> battery; // P169 live battery
        logitech_battery_source bsrc = logitech_battery_source::none;
    };
    Current cur;
    if (!task->hidraw_path.empty()) {
        HidppTransport transport(task->hidraw_path);
        if (transport.is_open()) {
            transport.set_device_index(task->device_index);
            if (auto dpi = transport.get_dpi_info(task->device_index)) {
                cur.ok = true;
                cur.dpi = dpi->dpi_current;
                cur.supports_lod = dpi->supports_lift_off_distance;
                // R6-8: capability bit set but the current-LOD byte is
                // out-of-range (transport's get_lift_off_distance rejects
                // > 2) — do NOT present the garbage value or write it back.
                // A byte that isn't a valid 0..2 LOD means the read wasn't
                // populated; treat the device as LOD-unsupported so the combo
                // stays disabled and no sentinel "High" is ever written.
                if (dpi->supports_lift_off_distance) {
                    if (dpi->lift_off_distance <= 2)
                        cur.lod = dpi->lift_off_distance;
                    else
                        cur.supports_lod = false;
                }
                // Capture device-specific DPI levels for the UI
                task->dpi_levels = dpi->dpi_levels;
                task->dpi_xy = dpi->supports_y;
            }
            if (auto rate = transport.get_polling_rate(task->device_index)) {
                cur.rate_hz = (int)*rate;
                // R5-C: previously cur.ok was only set by the DPI query above —
                // a rate-only device (no DPI feature supported, e.g. hardware
                // DPI) reported a *successful* query as "Could not query the
                // device's current settings."  Report success once any query
                // landed so the rate control populates and the status line
                // reflects what was actually read.
                cur.ok = true;
            }
            // Get supported rate codes from the device
            task->rate_codes_hz.clear();
            {
                // EXTENDED_ADJUSTABLE_REPORT_RATE (0x8061): GetSupportedRates is
                // fn 0x01 and returns a big-endian support mask, one bit per code
                // (code 0..6 = 125..8000 Hz) — NOT the fn 0x00 "count+list" payload
                // some devices use.  PRO X 2 replies `00 7f` (all seven rates).
                if (auto ext_mask = transport.feature_request(
                        static_cast<uint16_t>(hidpp_feature_index::extended_adjustable_report_rate), 0x01,
                        nullptr, 0, std::chrono::milliseconds(500), task->device_index);
                    ext_mask && ext_mask->size() >= 1) {
                    const uint16_t bits = ext_mask->size() >= 2
                        ? (static_cast<uint16_t>((*ext_mask)[0]) << 8) | (*ext_mask)[1]
                        : (*ext_mask)[0];
                    for (uint8_t c = 0; c < 7; ++c)
                        if ((bits & (static_cast<uint16_t>(1) << c)) != 0)
                            if (auto hz = hidpp_rate_code_to_hz(true, c))
                                task->rate_codes_hz.push_back(*hz);
                }
            }
            // If extended rate not available, fall back to legacy report-rate
            // feature (0x8060, fn 0x00): a bit mask over 1..8 ms periods.
            if (task->rate_codes_hz.empty()) {
                if (auto legacy_mask = transport.feature_request(
                        static_cast<uint16_t>(hidpp_feature_index::report_rate), 0x00,
                        nullptr, 0, std::chrono::milliseconds(500), task->device_index);
                    legacy_mask && !legacy_mask->empty()) {
                    const uint8_t bits = (*legacy_mask)[0];
                    for (uint8_t i = 0; i < 8; ++i)
                        if ((bits & (static_cast<uint8_t>(1) << i)) != 0)
                            if (auto hz = hidpp_rate_code_to_hz(false, i + 1))
                                task->rate_codes_hz.push_back(*hz);
                }
            }
            // P169 — read-only battery for the capability/source display.
            cur.bsrc = preferred_battery_source(task->features, task->hidpp10);
            if (auto b = transport.get_battery_status(task->device_index))
                cur.battery = b;
            task->supports_lod = cur.supports_lod;
        }
    }

    struct Result {
        AppState* S;
        int idx;
        Current cur;
        int devs_version = 0;
        // Device capability data for UI ranges
        std::vector<uint16_t> dpi_levels;
        bool dpi_xy = false;
        std::vector<uint32_t> rate_codes_hz;
        bool supports_lod = false;
    };
    auto* res = new Result();
    res->S = S;
    res->idx = task->idx;
    res->cur = cur;
    res->devs_version = task->devs_version;
    res->dpi_levels = task->dpi_levels;
    res->dpi_xy = task->dpi_xy;
    res->rate_codes_hz = task->rate_codes_hz;
    res->supports_lod = task->supports_lod;
    delete task;
    g_idle_add(+[](gpointer p) -> gboolean {
        auto* r = static_cast<Result*>(p);
        AppState* S = r->S;
        if (S->hw_cancel) { delete r; return G_SOURCE_REMOVE; }
        S->hw_busy = false;
        const int selected = S->hw_dev_combo
            ? (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->hw_dev_combo))
            : -1;
        // P169 — capability summary is independent of the query result.
        hw_render_caps(S, selected);
        // R2-08: the device list was rebuilt while this query was in flight —
        // r->idx may now name a different physical device.  Drop the values.
        if (r->devs_version != S->hw_devs_version) {
            hw_update_ui_state(S);
            delete r;
            return G_SOURCE_REMOVE;
        }
        hw_set_battery(S, r->idx, r->cur.battery, r->cur.bsrc);
        if (r->idx == selected) {
            hw_update_ui_state(S);
            // Update DPI spin with device-specific range
            if (S->hw_dpi_spin && !r->dpi_levels.empty()) {
                uint16_t dpi_min = r->dpi_levels.front();
                uint16_t dpi_max = r->dpi_levels.back();
                uint16_t dpi_step = 50; // default step
                if (r->dpi_levels.size() >= 2)
                    dpi_step = std::max<uint16_t>(50, r->dpi_levels[1] - r->dpi_levels[0]);
                gtk_spin_button_set_range(GTK_SPIN_BUTTON(S->hw_dpi_spin),
                                          dpi_min, dpi_max);
                gtk_spin_button_set_increments(GTK_SPIN_BUTTON(S->hw_dpi_spin),
                                               dpi_step, dpi_step * 5);
                if (r->cur.dpi > 0)
                    gtk_spin_button_set_value(GTK_SPIN_BUTTON(S->hw_dpi_spin),
                                              (double)r->cur.dpi);
            }
            // Update rate combo with device-specific supported rates
            if (S->hw_rate_combo && !r->rate_codes_hz.empty()) {
                GtkStringList* sl = gtk_string_list_new(nullptr);
                for (uint32_t hz : r->rate_codes_hz) {
                    gtk_string_list_append(sl, (std::to_string(hz) + " Hz").c_str());
                }
                GtkDropDown* dd = GTK_DROP_DOWN(S->hw_rate_combo);
                gtk_drop_down_set_model(dd, G_LIST_MODEL(sl));
                g_object_unref(sl);
                // Select the current rate
                if (r->cur.rate_hz > 0) {
                    int best = 0, bd = INT_MAX;
                    for (size_t i = 0; i < r->rate_codes_hz.size(); ++i) {
                        int d = std::abs((int)r->rate_codes_hz[i] - r->cur.rate_hz);
                        if (d < bd) { bd = d; best = (int)i; }
                    }
                    gtk_drop_down_set_selected(dd, (guint)best);
                } else {
                    gtk_drop_down_set_selected(dd, 0);
                }
            }
            if (S->hw_lod_combo) {
                gtk_widget_set_sensitive(S->hw_lod_combo, r->supports_lod);
                if (r->supports_lod)
                    gtk_drop_down_set_selected(
                        GTK_DROP_DOWN(S->hw_lod_combo),
                        (guint)std::clamp(r->cur.lod, 0, 2));
            }
            if (r->cur.ok) {
                hw_set_status(S, trf("Current: DPI %s · %d Hz · LOD %s",
                                     cur_dpi_text(r->cur.dpi).c_str(),
                                     r->cur.rate_hz,
                                     lod_text(r->cur.lod, r->supports_lod).c_str()));
            } else {
                hw_set_status(S, tr("Could not query the device's current settings."));
            }
        } else {
            hw_update_ui_state(S);
        }
    // GUI-Y3: user selected another device while this query was in flight
    // — re-issue the query now that we are free (stale pending cleared by
    // the fresh hw_query_current()).
    if (S->hw_pending_query >= 0) {
        int pq = S->hw_pending_query;
        S->hw_pending_query = -1;
        if (pq < (int)S->hidpp_devs.size())
            hw_query_current(S);
    }
    delete r;
    return G_SOURCE_REMOVE;
    }, res);
    return nullptr;
}

// Write the requested onboard settings.  Off the main thread.
static gpointer hw_apply_thread(gpointer data) {
    auto* task = static_cast<HwApplyTask*>(data);
    AppState* S = task->S;

    struct Outcome {
        bool opened = false;
        bool dev_invalid = false;
        std::string hidraw_path;
        bool ok_dpi = false;
        bool ok_rate = false;
        bool ok_lod = false;
        uint16_t dpi = 0;
        uint32_t rate_hz = 0;
        int lod = 0;
    };
    Outcome out;
    out.dpi = task->dpi;
    out.rate_hz = task->rate_hz;
    out.lod = task->lod;

    if (task->hidraw_path.empty()) {
        out.dev_invalid = true;
    } else {
        out.hidraw_path = task->hidraw_path;
        HidppTransport transport(task->hidraw_path);
        if (!transport.is_open()) {
            // permission / node missing — message built on the main thread
        } else {
            transport.set_device_index(task->device_index);
            out.opened = true;
            // Validate DPI against device levels before sending
            std::vector<uint16_t> dpi_levels = task->dpi_levels;
            if (dpi_levels.empty()) {
                // Query the device for DPI levels if not provided
                if (auto dpi = transport.get_dpi_info(task->device_index)) {
                    dpi_levels = dpi->dpi_levels;
                }
            }
            bool dpi_valid = true;
            if (!dpi_levels.empty()) {
                auto it = std::find(dpi_levels.begin(), dpi_levels.end(), task->dpi);
                if (it == dpi_levels.end()) {
                    dpi_valid = false;
                }
            }
            if (dpi_valid) {
                out.ok_dpi = transport.set_dpi(task->dpi, task->device_index);
            } else {
                out.ok_dpi = false;
            }
            // Validate rate against device-supported rates
            std::vector<uint32_t> rate_codes_hz = task->rate_codes_hz;
            if (rate_codes_hz.empty() && task->rate_hz != 0) {
                // Query the device for supported rates if not provided —
                // EXTENDED_ADJUSTABLE_REPORT_RATE.GetSupportedRates is fn 0x01
                // and returns a big-endian bitmask (code 0..6 = 125..8000 Hz).
                if (auto ext_mask = transport.feature_request(
                        static_cast<uint16_t>(hidpp_feature_index::extended_adjustable_report_rate), 0x01,
                        nullptr, 0, std::chrono::milliseconds(500), task->device_index);
                    ext_mask && ext_mask->size() >= 1) {
                    const uint16_t bits = ext_mask->size() >= 2
                        ? (static_cast<uint16_t>((*ext_mask)[0]) << 8) | (*ext_mask)[1]
                        : (*ext_mask)[0];
                    for (uint8_t c = 0; c < 7; ++c)
                        if ((bits & (static_cast<uint16_t>(1) << c)) != 0)
                            if (auto hz = hidpp_rate_code_to_hz(true, c))
                                rate_codes_hz.push_back(*hz);
                }
                if (rate_codes_hz.empty()) {
                    // Legacy REPORT_RATE fn 0x00: bit mask over 1..8 ms periods.
                    if (auto legacy_rate = transport.feature_request(
                            static_cast<uint16_t>(hidpp_feature_index::report_rate), 0x00,
                            nullptr, 0, std::chrono::milliseconds(500), task->device_index);
                        legacy_rate && !legacy_rate->empty()) {
                        const uint8_t bits = (*legacy_rate)[0];
                        for (uint8_t i = 0; i < 8; ++i)
                            if ((bits & (static_cast<uint8_t>(1) << i)) != 0)
                                if (auto hz = hidpp_rate_code_to_hz(false, i + 1))
                                    rate_codes_hz.push_back(*hz);
                    }
                }
            }
            bool rate_valid = true;
            if (task->rate_hz != 0 && !rate_codes_hz.empty()) {
                auto it = std::find(rate_codes_hz.begin(), rate_codes_hz.end(), task->rate_hz);
                if (it == rate_codes_hz.end()) {
                    rate_valid = false;
                }
            }
            if (rate_valid && task->rate_hz != 0) {
                out.ok_rate = transport.set_polling_rate(task->rate_hz, task->device_index);
            } else if (task->rate_hz == 0) {
                out.ok_rate = true; // no change requested
            } else {
                out.ok_rate = false;
            }
            // LOD only if supported
            if (task->supports_lod) {
                out.ok_lod = transport.set_lift_off_distance(
                    (hidpp_lift_off_distance)task->lod, task->device_index);
            } else {
                out.ok_lod = true; // no change requested
            }
        }

    }

    struct Result { AppState* S; Outcome out; };
    auto* res = new Result();
    res->S = S;
    res->out = out;
    delete task;
    g_idle_add(+[](gpointer p) -> gboolean {
        auto* r = static_cast<Result*>(p);
        AppState* S = r->S;
        if (S->hw_cancel) { delete r; return G_SOURCE_REMOVE; }
        S->hw_busy = false;

        std::string parts;
        if (r->out.dev_invalid) {
            parts = tr("Select a Logitech HID++ device first.");
        } else if (!r->out.opened) {
            parts = trf("Cannot open %s — make sure you are in the 'input' "
                        "group (sudo usermod -aG input $USER) and reinstall "
                        "the udev rule.",
                        r->out.hidraw_path.c_str());
        } else {
            const char* lod_en[] = {"Low", "Medium", "High"};
            parts = trf("DPI→%d%s", r->out.dpi,
                        r->out.ok_dpi ? "" : tr("(rejected)"));
            parts += " · " + trf("Rate→%d Hz%s", r->out.rate_hz,
                                 r->out.ok_rate ? "" : tr("(rejected)"));
            parts += " · " + std::string(tr("LOD→"))
                          + tr(lod_en[std::clamp(r->out.lod, 0, 2)])
                          + (r->out.ok_lod ? "" : tr("(rejected)"));
        }
        hw_set_status(S, parts);
        hw_update_ui_state(S);
        hw_query_current(S); // refresh device-reported values after write
        delete r;
        return G_SOURCE_REMOVE;
    }, res);
    return nullptr;
}

// Bounded, read-only notification polling.  This is deliberately independent
// of the daemon motion loop: opening hidraw and draining a handful of queued
// status packets can never add work to the evdev/uinput hot path.
static gpointer hw_notification_thread(gpointer data) {
    auto* task = static_cast<HwNotificationTask*>(data);
    struct Result {
        AppState* S;
        int idx;
        std::string path;
        std::optional<hidpp_battery_info> battery;
        size_t count = 0;
        // R2-08: device snapshot captured at schedule time.
        bool legacy_protocol = false;
        std::vector<std::pair<uint16_t, uint8_t>> features;
        int devs_version = 0;
    };
    auto* result = new Result{task->S, task->idx, task->hidraw_path,
                              std::nullopt, 0, task->legacy_protocol,
                              task->features, task->devs_version};
    HidppTransport transport(task->hidraw_path);
    if (transport.is_open()) {
        transport.set_device_index(task->device_index);
        for (const auto& notification : transport.drain_notifications(
                 8, std::chrono::milliseconds(2))) {
            ++result->count;
            if (notification.type == hidpp_notification::kind::legacy_battery) {
                if (notification.sub_id == 0x0D)
                    result->battery = hidpp_parse_battery_charge(
                        notification.payload);
                else
                    result->battery = hidpp_parse_legacy_battery(
                        notification.payload.data(),
                        notification.payload.size());
            } else if (notification.type == hidpp_notification::kind::hidpp20) {
                const auto it = std::find_if(
                    task->features.begin(), task->features.end(),
                    [&notification](const auto& feature) {
                        return feature.second == notification.feature_index;
                    });
                if (it != task->features.end()) {
                    if (it->first == static_cast<uint16_t>(
                            hidpp_feature_index::battery_status))
                        result->battery = hidpp_parse_battery_status(
                            notification.payload);
                    else if (it->first == static_cast<uint16_t>(
                                 hidpp_feature_index::unified_battery))
                        result->battery = hidpp_parse_unified_battery(
                            notification.payload);
                }
            }
        }
    }
    delete task;
    g_idle_add(+[](gpointer p) -> gboolean {
        auto* r = static_cast<Result*>(p);
        AppState* S = r->S;
        if (S->hw_cancel) { delete r; return G_SOURCE_REMOVE; }
        S->hw_notify_busy = false;
        const int selected = S->hw_dev_combo
            ? (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->hw_dev_combo))
            : -1;
        if (selected == r->idx && r->battery &&
            r->idx >= 0 && r->idx < (int)S->hidpp_devs.size() &&
            // R2-08: only paint when no rescan replaced the device list since
            // this tick was scheduled (the index may point at new hardware).
            r->devs_version == S->hw_devs_version) {
            const std::string level = r->battery->level == 255
                ? tr("unknown") : std::to_string(r->battery->level) + "%";
            hw_set_status(S, trf("Notification: battery %s%s",
                                 level.c_str(),
                                 r->battery->charging
                                     ? tr(" (charging)") : ""));
            // R2-08: use the device snapshot captured when the tick scheduled
            // this task — never S->hidpp_devs[r->idx] after a rescan rebuilt
            // the vector (the index may now point at a different device).
            hw_set_battery(S, r->idx, r->battery,
                preferred_battery_source(r->features, r->legacy_protocol));
        }
        // Keep the capability row fresh (daemon-feed battery line etc.).
        hw_render_caps(S, selected);
        delete r;
        return G_SOURCE_REMOVE;
    }, result);
    return nullptr;
}

static gboolean hw_notification_tick(gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (S->hw_cancel || S->hw_busy || S->hw_notify_busy || !S->hw_dev_combo ||
        S->hidpp_devs.empty())
        return G_SOURCE_CONTINUE;
    const int idx = (int)gtk_drop_down_get_selected(
        GTK_DROP_DOWN(S->hw_dev_combo));
    if (idx < 0 || idx >= (int)S->hidpp_devs.size())
        return G_SOURCE_CONTINUE;
    S->hw_notify_busy = true;
    auto* task = new HwNotificationTask();
    task->S = S;
    task->idx = idx;
    task->hidraw_path = S->hidpp_devs[idx].hidraw_path;
    task->device_index = S->hidpp_devs[idx].device_index;
    task->features = S->hidpp_devs[idx].features;
    task->legacy_protocol = S->hidpp_devs[idx].info.protocol_version < 2;
    task->devs_version = S->hw_devs_version;
    hw_thread("rawaccel-hw-notify", hw_notification_thread, task);
    return G_SOURCE_CONTINUE;
}

// ── Public API (used by ui_builder.inl) ──────────────────────────────────────

void hw_update_ui_state(AppState* S) {
    const bool scan = S->hw_busy;
    const bool have = !scan && !S->hidpp_devs.empty();
    // P169 — gate every onboard control by what the selected device advertises
    // (feature-derived; a receiver/keyboard that lacks a DPI/rate/LOD feature
    // gets the widget disabled and hw_caps_lbl explains why).
    logitech_controls c;
    const int idx = have ? (int)gtk_drop_down_get_selected(
                               GTK_DROP_DOWN(S->hw_dev_combo)) : -1;
    if (idx >= 0 && idx < (int)S->hidpp_devs.size())
        c = logitech_controls_for(S->hidpp_devs[idx].features);
    const bool dpi_ok  = have && c.dpi;
    const bool rate_ok = have && (c.report_rate || c.report_rate_extended);
    const bool lod_ok  = have && c.lod;
    if (S->hw_dpi_spin)    gtk_widget_set_sensitive(S->hw_dpi_spin,    dpi_ok);
    if (S->hw_rate_combo)  gtk_widget_set_sensitive(S->hw_rate_combo,  rate_ok);
    if (S->hw_lod_combo)   gtk_widget_set_sensitive(S->hw_lod_combo,   lod_ok);
    if (S->hw_apply_btn)   gtk_widget_set_sensitive(S->hw_apply_btn,
                                 have && (dpi_ok || rate_ok || lod_ok));
    if (S->hw_refresh_btn) gtk_widget_set_sensitive(S->hw_refresh_btn, !scan);
}

/// Start a background scan of Logitech HID++ devices.  No-op if one is
/// already running.  Called at UI build time and by the refresh button.
void hw_start_scan(AppState* S) {
    if (S->hw_busy) return;
    S->hw_busy = true;
    hw_update_ui_state(S);
    hw_set_status(S, tr("Scanning HID++ devices…"));
    auto* task = new HwScanTask();
    task->S = S;
    hw_thread("rawaccel-hw-scan", hw_scan_thread, task);
}

/// Background query of the currently selected device's onboard settings.
void hw_query_current(AppState* S) {
    if (!S->hw_dev_combo) return;
    int idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->hw_dev_combo));
    if (idx < 0 || idx >= (int)S->hidpp_devs.size()) { S->hw_pending_query = -1; return; }
    // GUI-Y3: a busy scan/query/apply would previously drop this selection
    // silently, leaving stale onboard values on screen.  Stash it and let the
    // in-flight task's completion idle callback re-issue the query.
    if (S->hw_busy) {
        S->hw_pending_query = idx;
        return;
    }
    // Consume any stashed pending selection (a fresh query supersedes it).
    S->hw_pending_query = -1;
    S->hw_busy = true;
    hw_update_ui_state(S);
    auto* task = new HwQueryTask();
    task->S = S;
    task->idx = idx;
    task->hidraw_path = S->hidpp_devs[idx].hidraw_path;   // main-thread snapshot
    task->device_index = S->hidpp_devs[idx].device_index; // main-thread snapshot
    task->features = S->hidpp_devs[idx].features;         // P169 battery source
    task->hidpp10 = S->hidpp_devs[idx].info.protocol_version < 2;
    task->devs_version = S->hw_devs_version;
    hw_thread("rawaccel-hw-query", hw_query_thread, task);
}

void on_hw_dev_selected(GtkDropDown*, GParamSpec*, gpointer user_data) {
    hw_query_current(static_cast<AppState*>(user_data));
}

void on_hw_refresh_clicked(GtkButton*, gpointer user_data) {
    hw_start_scan(static_cast<AppState*>(user_data));
}

void on_hw_apply_clicked(GtkButton*, gpointer user_data) {
    auto* S = static_cast<AppState*>(user_data);
    if (!S->hw_dev_combo)
        return hw_set_status(S, tr("Select a Logitech HID++ device first."));
    int idx = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->hw_dev_combo));
    if (idx < 0 || idx >= (int)S->hidpp_devs.size())
        return hw_set_status(S, tr("Select a Logitech HID++ device first."));
    // Validate against device capabilities before sending
    const auto& dev = S->hidpp_devs[idx];
    const logitech_controls c = logitech_controls_for(dev.features);
    if (!c.dpi) {
        hw_set_status(S, tr("This device does not support DPI changes."));
        return;
    }
    S->hw_busy = true;
    hw_update_ui_state(S);
    auto* task = new HwApplyTask();
    task->S = S;
    task->idx = idx;
    task->hidraw_path = dev.hidraw_path;
    task->device_index = dev.device_index;
    task->dpi  = static_cast<uint16_t>(std::clamp((int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(S->hw_dpi_spin)), HW_DPI_MIN, HW_DPI_MAX));
    task->rate_hz = 0;
    if (c.report_rate || c.report_rate_extended) {
        int rate_sel = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->hw_rate_combo));
        GtkDropDown* dd = GTK_DROP_DOWN(S->hw_rate_combo);
        GListModel* model = gtk_drop_down_get_model(dd);
        if (model && rate_sel >= 0 && rate_sel < (int)g_list_model_get_n_items(model)) {
            gpointer item = g_list_model_get_item(model, rate_sel);
            if (item) {
                const char* str = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
                task->rate_hz = std::stoul(str);
                g_object_unref(item);
            }
        }
    }
    task->lod = std::clamp(
        (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(S->hw_lod_combo)), 0, 2);
    task->devs_version = S->hw_devs_version;
    task->supports_lod = c.lod;
    hw_thread("rawaccel-hw-apply", hw_apply_thread, task);
}