// ─────────────────────────────────────────────────────────────────────────────
//  rawaccel-installer — GTK4 setup assistant for RawAccel Linux
//
//  This is a SOURCE-TREE bootstrap tool: it wraps the canonical `setup.sh`
//  instead of re-implementing package management, builds, or systemd setup.
//  Keeping one installer implementation (setup.sh) means the GUI cannot drift
//  from the CLI path — it only adds a friendly front-end, progress log and
//  pkexec privilege prompt.
//
//  Usage:
//      rawaccel-installer --source /path/to/rawaccel-linux
//      rawaccel-installer --detect
//      rawaccel-installer --print-command [--uninstall] [--no-deps]
//
//  The GUI never runs privileged work itself.  When started as a normal user it
//  invokes `pkexec env … bash <source>/setup.sh`; when started as root (e.g.
//  from a root terminal) it runs setup.sh directly.  The child's stdin is
//  /dev/null so a prompt in setup.sh can never freeze the GTK main loop.
// ─────────────────────────────────────────────────────────────────────────────

#include <gtk/gtk.h>
#include <glib.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rawaccel-base.hpp"
#include "log_scan.hpp"

namespace {

// ── Localisation (auto: Turkish locale → Turkish, otherwise English) ─────────
bool g_turkish = false;

const char* T(const char* english, const char* turkish) {
    return g_turkish ? turkish : english;
}

bool locale_is_turkish() {
    const char* names[] = {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"};
    for (const char* name : names) {
        const char* value = g_getenv(name);
        if (!value || !*value) continue;
        std::string s(value);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // LANGUAGE is a priority list ("en:tr"); only the first entry wins.
        const std::string::size_type colon = s.find(':');
        if (colon != std::string::npos) s.resize(colon);
        if (s.rfind("tr", 0) == 0 || s.find("_tr") == 0)
            return true;
    }
    return false;
}

// GTK4 can emit cosmetic "GtkGizmo slider reported min width -2" warnings
// during layout (the same harmless GTK bug the main GUI suppresses).  Keep the
// installer's terminal output clean so real warnings remain visible.
void install_gtk_log_filter() {
    g_log_set_writer_func(
        [](GLogLevelFlags level, const GLogField* fields, gsize n_fields,
           gpointer) -> GLogWriterOutput {
            for (gsize i = 0; i < n_fields; ++i) {
                if (g_strcmp0(fields[i].key, "MESSAGE") == 0 && fields[i].value) {
                    const char* message = static_cast<const char*>(fields[i].value);
                    if (std::strstr(message, "GtkGizmo"))
                        return G_LOG_WRITER_HANDLED;
                }
            }
            return g_log_writer_default(level, fields, n_fields, nullptr);
        },
        nullptr, nullptr);
}

std::string env_string(const char* name) {
    const char* value = g_getenv(name);
    return value ? value : "";
}

std::string current_user() {
    const char* sudo_user = g_getenv("SUDO_USER");
    if (sudo_user && *sudo_user && std::strcmp(sudo_user, "root") != 0)
        return sudo_user;
    struct passwd* pw = getpwuid(getuid());
    return pw && pw->pw_name ? pw->pw_name : "";
}

std::string find_program(const char* name) {
    // Prefer the normal system locations even if PATH was narrowed (e.g. by a
    // desktop launcher).  Then fall back to PATH for custom prefixes.
    const char* fixed[] = {"/usr/bin/", "/usr/local/bin/", "/bin/", "/sbin/", "/usr/sbin/"};
    for (const char* dir : fixed) {
        std::string candidate = std::string(dir) + name;
        if (g_file_test(candidate.c_str(), G_FILE_TEST_IS_EXECUTABLE))
            return candidate;
    }
    gchar* found = g_find_program_in_path(name);
    if (!found) return {};
    std::string result(found);
    g_free(found);
    return result;
}

std::string executable_dir() {
    char buffer[4096];
    ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n <= 0) return {};
    buffer[n] = '\0';
    gchar* dir = g_path_get_dirname(buffer);
    std::string result(dir ? dir : "");
    g_free(dir);
    return result;
}

std::string parent_dir(const std::string& path) {
    if (path.empty()) return {};
    gchar* dir = g_path_get_dirname(path.c_str());
    std::string result(dir ? dir : "");
    g_free(dir);
    return result;
}

std::string canonical_dir(const std::string& path) {
    if (path.empty()) return {};
    gchar* canonical = g_canonicalize_filename(path.c_str(), nullptr);
    std::string result(canonical ? canonical : "");
    g_free(canonical);
    return result;
}

bool is_source_dir(const std::string& path) {
    if (path.empty()) return false;
    return g_file_test((path + "/setup.sh").c_str(), G_FILE_TEST_IS_REGULAR) &&
           g_file_test((path + "/scripts/build.sh").c_str(), G_FILE_TEST_IS_REGULAR) &&
           g_file_test((path + "/config/default.json").c_str(), G_FILE_TEST_IS_REGULAR);
}

constexpr const char* kPacmanLock = "/var/lib/pacman/db.lck";

bool pacman_lock_exists() {
    return g_file_test(kPacmanLock, G_FILE_TEST_EXISTS);
}

bool is_package_manager_name(const std::string& name) {
    static const char* const managers[] = {
        "pacman", "paru", "yay", "pamac",
        "pakku", "pikaur", "trizen", "aura", "pacaur", "arch-update",
    };
    for (const char* manager : managers) {
        if (name == manager) return true;
    }
    return false;
}

std::string path_basename(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// `sudo pacman -Syu` waits for the password with comm="sudo"; scanning only
// comm would miss it and could delete the lock just as pacman starts.  Inspect
// the executable/script name for sudo/pkexec/doas and arch-update wrappers.
std::string command_line_package_manager(const std::string& cmdline_path) {
    gchar* contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(cmdline_path.c_str(), &contents, &length, nullptr))
        return {};
    std::vector<std::string> args;
    for (gsize i = 0; i < length;) {
        const char* arg = contents + i;
        const std::size_t arg_length = std::strlen(arg);
        args.emplace_back(arg, arg_length);
        i += arg_length + 1;
    }
    g_free(contents);
    if (args.empty()) return {};

    const std::string exe = path_basename(args[0]);
    if (is_package_manager_name(exe)) return exe;
    if (exe == "sudo" || exe == "pkexec" || exe == "doas") {
        for (std::size_t i = 1; i < args.size(); ++i) {
            const std::string candidate = path_basename(args[i]);
            if (is_package_manager_name(candidate)) return candidate;
        }
    }
    // arch-update runs pacman from a bash script; catch the pre-sudo window.
    if (exe == "bash" || exe == "sh" || exe == "dash" || exe == "zsh") {
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (path_basename(args[i]) == "arch-update") return "arch-update";
        }
    }
    return {};
}

// A lock file left behind by an interrupted pacman/paru transaction is common
// on Arch/CachyOS.  Never offer to delete it while a package manager is alive;
// scan /proc's comm names (readable without root) plus the sudo/bash command
// lines.  If /proc itself is unreadable, stay conservative and report "busy"
// so the caller does not remove a lock it cannot reason about.
bool package_manager_running(std::string* name = nullptr) {
    // With hidepid=2 (or a similarly restricted /proc) other users' package
    // managers are invisible to this scan.  Probe PID 1; if even that is not
    // readable, report "busy" rather than risk deleting an active lock.
    gchar* probe = nullptr;
    gsize probe_length = 0;
    const bool proc_readable =
        g_file_get_contents("/proc/1/comm", &probe, &probe_length, nullptr);
    g_free(probe);
    if (!proc_readable) {
        if (name) *name = "?";
        return true;
    }
    GDir* proc = g_dir_open("/proc", 0, nullptr);
    if (!proc) {
        if (name) *name = "?";
        return true;
    }
    bool found = false;
    const char* entry = nullptr;
    while ((entry = g_dir_read_name(proc)) != nullptr) {
        bool numeric = entry[0] != '\0';
        for (const char* p = entry; *p; ++p) {
            if (!std::isdigit(static_cast<unsigned char>(*p))) {
                numeric = false;
                break;
            }
        }
        if (!numeric) continue;

        const std::string proc_path = std::string("/proc/") + entry;
        gchar* contents = nullptr;
        gsize length = 0;
        if (!g_file_get_contents((proc_path + "/comm").c_str(), &contents,
                                 &length, nullptr))
            continue;
        std::string comm(contents, length);
        g_free(contents);
        while (!comm.empty() &&
               (comm.back() == '\n' || comm.back() == '\r' || comm.back() == ' '))
            comm.pop_back();

        if (is_package_manager_name(comm)) {
            if (name) *name = comm;
            found = true;
            break;
        }
        if (comm == "sudo" || comm == "pkexec" || comm == "doas" ||
            comm == "bash" || comm == "sh" || comm == "dash" || comm == "zsh") {
            const std::string detected =
                command_line_package_manager(proc_path + "/cmdline");
            if (!detected.empty()) {
                if (name) *name = detected;
                found = true;
                break;
            }
        }
    }
    g_dir_close(proc);
    return found;
}

std::string resolve_source(const std::string& requested) {
    // An explicit --source is authoritative: never silently fall back to the
    // cwd or the executable directory, or a typo would install a different
    // checkout than the one the user selected.
    if (!requested.empty()) {
        const std::string canonical = canonical_dir(requested);
        return is_source_dir(canonical) ? canonical : std::string();
    }

    std::vector<std::string> candidates;
    const std::string from_env = env_string("RAWACCEL_SOURCE");
    if (!from_env.empty()) candidates.push_back(from_env);

    gchar* cwd = g_get_current_dir();
    if (cwd) {
        candidates.emplace_back(cwd);
        g_free(cwd);
    }

    const std::string exe_dir = executable_dir();
    if (!exe_dir.empty()) {
        candidates.push_back(exe_dir);
        const std::string one_up = parent_dir(exe_dir);
        if (!one_up.empty()) {
            candidates.push_back(one_up);
            const std::string two_up = parent_dir(one_up);
            if (!two_up.empty()) candidates.push_back(two_up);
        }
    }

    for (const std::string& candidate : candidates) {
        const std::string canonical = canonical_dir(candidate);
        if (is_source_dir(canonical)) return canonical;
    }
    return {};
}

std::string shell_quote(const std::string& value) {
    if (value.empty()) return "''";
    bool safe = true;
    for (unsigned char c : value) {
        if (!(std::isalnum(c) || c == '/' || c == '.' || c == '_' || c == '-' ||
              c == ':' || c == '=' || c == '+' || c == ','))
            safe = false;
    }
    if (safe) return value;
    std::string result = "'";
    for (char c : value) {
        if (c == '\'')
            result += "'\\''";
        else
            result += c;
    }
    result += "'";
    return result;
}

std::string join_command(const std::vector<std::string>& argv) {
    std::string result;
    for (const std::string& arg : argv) {
        if (!result.empty()) result += ' ';
        result += shell_quote(arg);
    }
    return result;
}

// ── State detection ───────────────────────────────────────────────────────────
struct InstallState {
    std::string daemon_path;
    std::string cli_path;
    std::string gui_path;
    std::string version;
    bool installed = false;
    bool service_active = false;
    bool service_known = false;
};

int run_sync(const std::vector<std::string>& args, std::string* output = nullptr) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    gchar* stdout_buf = nullptr;
    GError* error = nullptr;
    gint wait_status = 0;
    const GSpawnFlags flags = static_cast<GSpawnFlags>(
        G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL);
    const gboolean ok = g_spawn_sync(nullptr, argv.data(), nullptr, flags,
                                     nullptr, nullptr, &stdout_buf, nullptr,
                                     &wait_status, &error);
    if (!ok) {
        if (error) g_error_free(error);
        g_free(stdout_buf);
        return -1;
    }
    if (output) *output = stdout_buf ? stdout_buf : "";
    g_free(stdout_buf);
    if (WIFEXITED(wait_status)) return WEXITSTATUS(wait_status);
    return -1;
}

InstallState detect_state() {
    InstallState state;
    state.daemon_path = find_program("rawaccel-daemon");
    state.cli_path = find_program("rawaccel-cli");
    state.gui_path = find_program("rawaccel-gui");
    state.installed = !state.daemon_path.empty() && !state.cli_path.empty();

    if (!state.cli_path.empty()) {
        std::string output;
        if (run_sync({state.cli_path, "--version"}, &output) == 0) {
            const std::string::size_type newline = output.find('\n');
            state.version = output.substr(0, newline);
            while (!state.version.empty() &&
                   (state.version.back() == '\r' || state.version.back() == ' '))
                state.version.pop_back();
        }
    }

    if (find_program("systemctl").empty()) {
        state.service_known = false;
    } else {
        state.service_known = true;
        state.service_active =
            run_sync({"systemctl", "is-active", "--quiet", "rawaccel.service"}) == 0;
    }
    return state;
}

// ── Command construction ──────────────────────────────────────────────────────
enum class Mode { Install, Uninstall };

struct Command {
    std::vector<std::string> argv;
    std::string error;
};

Command make_command(const std::string& source, Mode mode, bool skip_deps) {
    Command command;
    if (!is_source_dir(source)) {
        command.error = T("The selected folder is not a RawAccel source tree (setup.sh not found).",
                          "Seçilen klasör bir RawAccel kaynak ağacı değil (setup.sh bulunamadı).");
        return command;
    }

    std::string bash = find_program("bash");
    if (bash.empty()) bash = "/bin/bash";

    if (geteuid() != 0) {
        const std::string pkexec = find_program("pkexec");
        if (pkexec.empty()) {
            command.error = T("pkexec was not found. Administrator permission cannot be requested, so the installation cannot start.",
                              "pkexec bulunamadı. Yönetici izni istenemediği için kurulum başlatılamıyor.");
            return command;
        }
        const std::string env = find_program("env");
        if (env.empty()) {
            command.error = T("The 'env' program was not found.", "'env' programı bulunamadı.");
            return command;
        }

        command.argv.push_back(pkexec);
        command.argv.push_back(env);
        command.argv.push_back("SUDO_USER=" + current_user());
        command.argv.push_back("SUDO_UID=" + std::to_string(getuid()));
        command.argv.push_back("SUDO_GID=" + std::to_string(getgid()));
        // setup.sh's KDE fix needs the calling session, not root's.  pkexec
        // starts from a clean environment, so these have to be passed
        // explicitly; setup.sh forwards them via runuser (or the sudo
        // fallback) to $REAL_USER.
        const char* forwarded[] = {
            "XDG_CURRENT_DESKTOP", "DESKTOP_SESSION", "DBUS_SESSION_BUS_ADDRESS",
            "XDG_RUNTIME_DIR", "WAYLAND_DISPLAY", "DISPLAY"
        };
        for (const char* name : forwarded) {
            const std::string value = env_string(name);
            if (!value.empty())
                command.argv.push_back(std::string(name) + "=" + value);
        }
    }

    command.argv.push_back(bash);
    command.argv.push_back(source + "/setup.sh");
    if (mode == Mode::Uninstall) {
        command.argv.push_back("--uninstall");
    } else {
        command.argv.push_back("--reinstall");
        if (skip_deps) command.argv.push_back("--no-deps");
    }
    return command;
}

// ── Installer state ───────────────────────────────────────────────────────────
struct Installer {
    GtkApplication* app = nullptr;
    GtkWidget* window = nullptr;

    GtkWidget* version_value = nullptr;
    GtkWidget* daemon_value = nullptr;
    GtkWidget* service_value = nullptr;
    GtkWidget* gui_value = nullptr;
    GtkWidget* source_label = nullptr;
    GtkWidget* source_button = nullptr;

    GtkWidget* install_button = nullptr;
    GtkWidget* uninstall_button = nullptr;
    GtkWidget* launch_button = nullptr;
    GtkWidget* skip_deps_check = nullptr;
    GtkWidget* refresh_button = nullptr;
    GtkWidget* clear_button = nullptr;
    GtkWidget* copy_button = nullptr;

    GtkWidget* spinner = nullptr;
    GtkWidget* progress = nullptr;
    GtkWidget* status_label = nullptr;
    GtkWidget* log_view = nullptr;
    GtkTextBuffer* log_buffer = nullptr;
    GtkTextMark* log_end = nullptr;

    std::string source_dir;
    std::string user;
    InstallState state;
    Mode mode = Mode::Install;
    bool initial_no_deps = false;

    bool running = false;
    bool cleanup_running = false;
    bool finished = false;
    bool child_done = false;
    bool out_eof = false;
    bool err_eof = false;
    int child_status = 0;
    bool saw_error = false;
    bool saw_pacman_lock = false;
    std::string log_text;

    GPid child_pid = 0;
    guint child_watch = 0;
    GIOChannel* out_channel = nullptr;
    GIOChannel* err_channel = nullptr;
    guint out_watch = 0;
    guint err_watch = 0;
};

struct ChannelData {
    Installer* installer = nullptr;
    bool is_error = false;
};

void set_status(Installer* installer, const std::string& text, bool error) {
    if (!installer->status_label) return;
    gtk_label_set_text(GTK_LABEL(installer->status_label), text.c_str());
    gtk_widget_remove_css_class(installer->status_label, "ok");
    gtk_widget_remove_css_class(installer->status_label, "bad");
    gtk_widget_remove_css_class(installer->status_label, "warn");
    gtk_widget_add_css_class(installer->status_label, error ? "bad" : "ok");
}

void set_value(GtkWidget* label, const std::string& text, const char* css_class) {
    if (!label) return;
    gtk_label_set_text(GTK_LABEL(label), text.c_str());
    gtk_widget_remove_css_class(label, "ok");
    gtk_widget_remove_css_class(label, "bad");
    gtk_widget_remove_css_class(label, "warn");
    if (css_class) gtk_widget_add_css_class(label, css_class);
}

void scroll_log_to_end(Installer* installer) {
    if (!installer->log_view || !installer->log_end) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(installer->log_buffer, &end);
    gtk_text_buffer_move_mark(installer->log_buffer, installer->log_end, &end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(installer->log_view),
                                       installer->log_end);
}

void update_progress_from_log(Installer* installer) {
    // Once the child has finished, the final "Done"/"Failed" text is
    // authoritative; do not let the final log append re-parse an old
    // "[n/7]" step and overwrite it.
    if (!installer->progress || installer->mode != Mode::Install || installer->finished)
        return;
    const std::string::size_type pos = installer->log_text.rfind('[');
    if (pos == std::string::npos) return;
    int step = 0;
    int total = 0;
    if (std::sscanf(installer->log_text.c_str() + pos, "[%d/%d]", &step, &total) != 2)
        return;
    if (total <= 0 || step < 0 || step > total) return;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(installer->progress),
                                  static_cast<double>(step) / total);
    char text[64];
    std::snprintf(text, sizeof(text), T("Step %d/%d", "Adım %d/%d"), step, total);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(installer->progress), text);
}

void append_log(Installer* installer, const char* data, gsize length) {
    if (!installer->log_buffer || length == 0) return;
    // `data` is a raw pipe buffer, not NUL-terminated: append with the explicit
    // length (strstr() here would read past `length` into uninitialised
    // memory).  Keep a 2-byte overlap so a UTF-8 "✗" split across two reads is
    // still detected.
    const std::string::size_type old_size = installer->log_text.size();
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(installer->log_buffer, &end);
    gtk_text_buffer_insert(installer->log_buffer, &end, data, static_cast<int>(length));
    installer->log_text.append(data, length);
    const std::string::size_type search_from = old_size > 2 ? old_size - 2 : 0;
    // setup.sh's err() prints "✗"; verify_install() uses err() for every
    // missing item, so this is a real failure even when setup.sh's final exit
    // code is 0.  See log_scan.hpp for why the bare word "EKSİK" is not used.
    if (rawaccel_installer::log_has_error(installer->log_text, search_from))
        installer->saw_error = true;
    if (rawaccel_installer::log_has_package_lock(installer->log_text))
        installer->saw_pacman_lock = true;
    if (installer->log_text.size() > 4u * 1024u * 1024u)
        installer->log_text.erase(0, installer->log_text.size() - 4u * 1024u * 1024u);
    scroll_log_to_end(installer);
    update_progress_from_log(installer);
}

void append_log(Installer* installer, const std::string& text) {
    append_log(installer, text.c_str(), text.size());
}

// ── UI helpers ────────────────────────────────────────────────────────────────
GtkWidget* make_label(const char* text, const char* css_class) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    if (css_class) gtk_widget_add_css_class(label, css_class);
    return label;
}

void add_row(GtkWidget* grid, int row, const char* english, const char* turkish,
             GtkWidget** value_out) {
    GtkWidget* name = make_label(T(english, turkish), nullptr);
    gtk_widget_set_margin_end(name, 18);
    GtkWidget* value = make_label("—", "value");
    gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_selectable(GTK_LABEL(value), TRUE);
    gtk_widget_set_hexpand(value, TRUE);
    gtk_grid_attach(GTK_GRID(grid), name, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), value, 1, row, 1, 1);
    if (value_out) *value_out = value;
}

void install_css() {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "window, .background { background-color: #1b1b1e; }"
        "headerbar { background-color: #141416; }"
        "label { color: #dcdcdf; }"
        ".card { background-color: #242428; border-radius: 10px; padding: 14px; }"
        ".title-label { font-size: 21px; font-weight: 700; }"
        ".subtitle { color: #9a9aa4; font-size: 12px; }"
        ".card-title { font-size: 14px; font-weight: 700; }"
        ".value { font-weight: 600; }"
        ".ok { color: #5bd68a; }"
        ".bad { color: #ff7b72; }"
        ".warn { color: #f2c14e; }"
        ".status { font-weight: 600; }"
        "textview { background-color: #101012; color: #d6d6da; }"
        "textview text { background-color: #101012; color: #d6d6da; }"
        "progressbar text { color: #dcdcdf; }"
    );
    GdkDisplay* display = gdk_display_get_default();
    if (display)
        gtk_style_context_add_provider_for_display(
            display, GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

// ── Forward declarations ──────────────────────────────────────────────────────
void refresh_state(Installer* installer);
void update_sensitivity(Installer* installer);
void start_install(Installer* installer, Mode mode);
void show_info(Installer* installer, const std::string& title,
               const std::string& message);
void show_confirm(Installer* installer, Mode mode);
void maybe_finish(Installer* installer);
void start_lock_cleanup(Installer* installer);
void show_stale_lock_confirm(Installer* installer);
void show_lock_failure_help(Installer* installer);

// ── Buttons ───────────────────────────────────────────────────────────────────
void on_refresh_clicked(GtkButton*, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    if (installer->running || installer->cleanup_running) {
        set_status(installer, T("An installation is already running.",
                                "Zaten bir kurulum çalışıyor."), true);
        return;
    }
    refresh_state(installer);
    // refresh_state() already shows the actionable error when the source or
    // pkexec is missing; do not paint that over with a green message.
    if (installer->source_dir.empty()) return;
    if (geteuid() != 0 && find_program("pkexec").empty()) return;
    set_status(installer, T("Status refreshed.", "Durum yenilendi."), false);
}

void on_clear_log(GtkButton*, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    if (!installer->log_buffer) return;
    gtk_text_buffer_set_text(installer->log_buffer, "", -1);
    installer->log_text.clear();
    installer->saw_error = false;
    installer->saw_pacman_lock = false;
    update_sensitivity(installer);
    if (installer->progress && !installer->running) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(installer->progress), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(installer->progress),
                                  T("Ready", "Hazır"));
    }
}

void on_copy_log(GtkButton*, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    if (installer->log_text.empty() || !installer->window) return;
    GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(installer->window));
    if (!display) return;
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    if (!clipboard) return;
    gdk_clipboard_set_text(clipboard, installer->log_text.c_str());
    set_status(installer,
               T("Log copied to the clipboard.", "Günlük panoya kopyalandı."),
               false);
}

void on_install_clicked(GtkButton*, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    if (installer->running || installer->cleanup_running) return;
    if (installer->state.installed)
        show_confirm(installer, Mode::Install);
    else
        start_install(installer, Mode::Install);
}

void on_uninstall_clicked(GtkButton*, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    if (!installer->running && !installer->cleanup_running)
        show_confirm(installer, Mode::Uninstall);
}

void on_launch_clicked(GtkButton*, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    const std::string gui = find_program("rawaccel-gui");
    if (gui.empty()) {
        show_info(installer, T("RawAccel GUI", "RawAccel GUI"),
                  T("rawaccel-gui was not found in PATH.",
                    "rawaccel-gui PATH içinde bulunamadı."));
        return;
    }
    std::vector<char*> argv{const_cast<char*>(gui.c_str()), nullptr};
    GError* error = nullptr;
    if (!g_spawn_async(nullptr, argv.data(), nullptr, G_SPAWN_DEFAULT,
                       nullptr, nullptr, nullptr, &error)) {
        std::string message = T("Could not start rawaccel-gui.",
                                "rawaccel-gui başlatılamadı.");
        if (error) {
            message += "\n";
            message += error->message;
            g_error_free(error);
        }
        show_info(installer, T("RawAccel GUI", "RawAccel GUI"), message);
    }
}

void on_source_selected(GObject* source, GAsyncResult* result, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    GtkFileDialog* dialog = GTK_FILE_DIALOG(source);
    GError* error = nullptr;
    GFile* file = gtk_file_dialog_select_folder_finish(dialog, result, &error);
    // GtkFileDialog is a plain GObject: the caller owns the reference.
    g_object_unref(dialog);
    if (!file) {
        g_clear_error(&error);
        return; // cancelled
    }
    gchar* path = g_file_get_path(file);
    g_object_unref(file);
    if (!path) {
        g_clear_error(&error);
        return;
    }
    const std::string canonical = canonical_dir(path);
    g_free(path);
    g_clear_error(&error);
    if (is_source_dir(canonical)) {
        installer->source_dir = canonical;
        refresh_state(installer);
        set_status(installer, T("Source folder selected.",
                                "Kaynak klasörü seçildi."), false);
    } else {
        show_info(installer,
                  T("Invalid source folder", "Geçersiz kaynak klasörü"),
                  T("That folder does not contain setup.sh, scripts/build.sh and config/default.json.\n"
                    "Select the rawaccel-linux source directory.",
                    "Bu klasörde setup.sh, scripts/build.sh ve config/default.json bulunmuyor.\n"
                    "rawaccel-linux kaynak dizinini seçin."));
    }
}

void on_choose_source(GtkButton*, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(
        dialog, T("Select the RawAccel source folder",
                  "RawAccel kaynak klasörünü seçin"));
    if (!installer->source_dir.empty()) {
        GFile* current = g_file_new_for_path(installer->source_dir.c_str());
        gtk_file_dialog_set_initial_folder(dialog, current);
        g_object_unref(current);
    }
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(installer->window),
                                  nullptr, on_source_selected, installer);
}

// ── Confirmation / information windows ────────────────────────────────────────
void on_confirm_ok(GtkButton* button, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    const int encoded = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "mode"));
    GtkRoot* root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root)) gtk_window_destroy(GTK_WINDOW(root));
    start_install(installer, encoded == 0 ? Mode::Install : Mode::Uninstall);
}

void show_confirm(Installer* installer, Mode mode) {
    const bool uninstall = mode == Mode::Uninstall;
    GtkWidget* window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window),
                         uninstall ? T("Uninstall RawAccel", "RawAccel'i kaldır")
                                   : T("Reinstall RawAccel", "RawAccel'i yeniden kur"));
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(installer->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 120);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget* message = gtk_label_new(
        uninstall
            ? T("RawAccel will be stopped and removed from the system.\n"
                "Your profiles in ~/.config/rawaccel and /etc/rawaccel are kept.",
                "RawAccel durdurulup sistemden kaldırılacak.\n"
                "~/.config/rawaccel ve /etc/rawaccel içindeki profilleriniz korunur.")
            : T("The daemon will be stopped, the project rebuilt and reinstalled.\n"
                "Your configuration files are preserved.",
                "Daemon durdurulacak, proje yeniden derlenip kurulacak.\n"
                "Yapılandırma dosyalarınız korunur."));
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget* cancel = gtk_button_new_with_label(T("Cancel", "Vazgeç"));
    GtkWidget* ok = gtk_button_new_with_label(
        uninstall ? T("Uninstall", "Kaldır") : T("Reinstall", "Yeniden Kur"));
    gtk_widget_add_css_class(ok, uninstall ? "destructive-action" : "suggested-action");
    g_object_set_data(G_OBJECT(ok), "mode", GINT_TO_POINTER(uninstall ? 1 : 0));
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), window);
    g_signal_connect(ok, "clicked", G_CALLBACK(on_confirm_ok), installer);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), ok);
    gtk_box_append(GTK_BOX(box), buttons);

    gtk_window_present(GTK_WINDOW(window));
}

void show_info(Installer* installer, const std::string& title,
               const std::string& message) {
    GtkWidget* window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    if (installer->window)
        gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(installer->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 150);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget* label = gtk_label_new(message.c_str());
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget* close = gtk_button_new_with_label(T("OK", "Tamam"));
    gtk_widget_add_css_class(close, "suggested-action");
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_destroy), window);
    gtk_box_append(GTK_BOX(buttons), close);
    gtk_box_append(GTK_BOX(box), buttons);
    gtk_window_present(GTK_WINDOW(window));
}

// ── Package-manager lock help (all inside the GUI) ──────────────────────────
void close_dialog_for_widget(GtkWidget* widget) {
    GtkRoot* root = gtk_widget_get_root(widget);
    if (root && GTK_IS_WINDOW(root)) gtk_window_destroy(GTK_WINDOW(root));
}

void on_cleanup_exit(GPid pid, gint status, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    installer->cleanup_running = false;
    g_spawn_close_pid(pid);
    if (installer->spinner) {
        gtk_spinner_set_spinning(GTK_SPINNER(installer->spinner), FALSE);
        gtk_widget_set_visible(installer->spinner, FALSE);
    }
    update_sensitivity(installer);

    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (ok) {
        set_status(installer,
                   T("Stale package lock removed — restarting the installation…",
                     "Bayat paket kilidi temizlendi — kurulum yeniden başlatılıyor…"),
                   false);
        start_install(installer, Mode::Install);
    } else {
        const char* message =
            T("The package lock could not be removed. If the authorization "
              "dialog was cancelled, try again. If a package manager is running, "
              "wait for it to finish and retry.",
              "Paket kilidi temizlenemedi. Yetkilendirme penceresini iptal "
              "ettiyseniz tekrar deneyin. Bir paket yöneticisi çalışıyorsa "
              "bitmesini bekleyip yeniden deneyin.");
        set_status(installer, message, true);
        show_info(installer, T("Package lock", "Paket kilidi"), message);
    }
}

void start_lock_cleanup(Installer* installer) {
    if (installer->running || installer->cleanup_running) return;

    std::string manager;
    if (package_manager_running(&manager)) {
        const std::string message =
            T("A package manager is still running", "Bir paket yöneticisi hâlâ çalışıyor") +
            std::string(" (") + (manager.empty() ? "?" : manager) + ").\n" +
            T("Wait for it to finish, then retry. The lock was not touched.",
              "Bitmesini bekleyip yeniden deneyin. Kilide dokunulmadı.");
        set_status(installer, message, true);
        show_info(installer, T("Package manager busy", "Paket yöneticisi meşgul"),
                  message);
        return;
    }
    if (!pacman_lock_exists()) {
        // A previous attempt or the package manager already removed it.
        start_install(installer, Mode::Install);
        return;
    }

    std::string rm = find_program("rm");
    if (rm.empty()) rm = "/bin/rm";
    std::vector<std::string> args;
    if (geteuid() == 0) {
        args = {rm, "-f", kPacmanLock};
    } else {
        const std::string pkexec = find_program("pkexec");
        if (pkexec.empty()) {
            const char* message =
                T("Administrator permission requires pkexec, which was not found.",
                  "Yönetici izni için pkexec gerekli, ancak bulunamadı.");
            set_status(installer, message, true);
            show_info(installer, T("Permission required", "İzin gerekli"), message);
            return;
        }
        args = {pkexec, rm, "-f", kPacmanLock};
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    installer->cleanup_running = true;
    if (installer->spinner) {
        gtk_widget_set_visible(installer->spinner, TRUE);
        gtk_spinner_set_spinning(GTK_SPINNER(installer->spinner), TRUE);
    }
    update_sensitivity(installer);
    set_status(installer,
               T("Removing the stale package lock…", "Bayat paket kilidi temizleniyor…"),
               false);

    GPid pid = 0;
    GError* error = nullptr;
    const GSpawnFlags flags = static_cast<GSpawnFlags>(
        G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH);
    if (!g_spawn_async(nullptr, argv.data(), nullptr, flags,
                       nullptr, nullptr, &pid, &error)) {
        installer->cleanup_running = false;
        if (installer->spinner) {
            gtk_spinner_set_spinning(GTK_SPINNER(installer->spinner), FALSE);
            gtk_widget_set_visible(installer->spinner, FALSE);
        }
        update_sensitivity(installer);
        std::string message = T("Could not remove the package lock.",
                                "Paket kilidi temizlenemedi.");
        if (error) {
            message += "\n";
            message += error->message;
            g_error_free(error);
        }
        set_status(installer, message, true);
        show_info(installer, T("Package lock", "Paket kilidi"), message);
        return;
    }
    g_child_watch_add(pid, on_cleanup_exit, installer);
}

void on_preflight_skip_deps_clicked(GtkButton* button, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    close_dialog_for_widget(GTK_WIDGET(button));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(installer->skip_deps_check), TRUE);
    start_install(installer, Mode::Install);
}

void on_preflight_clean_clicked(GtkButton* button, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    close_dialog_for_widget(GTK_WIDGET(button));
    start_lock_cleanup(installer);
}

void show_stale_lock_confirm(Installer* installer) {
    GtkWidget* window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window),
                         T("Package lock detected", "Paket kilidi bulundu"));
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(installer->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 620, 210);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget* message = gtk_label_new(
        T("A stale pacman database lock was found at /var/lib/pacman/db.lck. "
          "No pacman/paru/yay process is running. If the build dependencies "
          "are already installed, skipping dependency installation is the "
          "fastest and safest option. Alternatively, only the lock file can be "
          "removed.\n\nRemoving the lock does not touch the package database.",
          "Bayat bir pacman veritabanı kilidi bulundu: /var/lib/pacman/db.lck. "
          "Çalışan pacman/paru/yay süreci yok. Derleme bağımlılıkları zaten "
          "kuruluysa bağımlılık kurulumunu atlamak en hızlı ve en güvenli "
          "yoldur. Alternatif olarak yalnızca kilit dosyası silinebilir.\n\n"
          "Kilit dosyasını silmek paket veritabanına dokunmaz."));
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget* cancel = gtk_button_new_with_label(T("Cancel", "Vazgeç"));
    GtkWidget* clean = gtk_button_new_with_label(
        T("Remove lock and continue", "Kilidi temizle ve devam et"));
    GtkWidget* skip = gtk_button_new_with_label(
        T("Skip deps and continue", "Bağımlılıkları atla ve devam et"));
    gtk_widget_add_css_class(skip, "suggested-action");
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), window);
    g_signal_connect(clean, "clicked", G_CALLBACK(on_preflight_clean_clicked), installer);
    g_signal_connect(skip, "clicked", G_CALLBACK(on_preflight_skip_deps_clicked), installer);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), clean);
    gtk_box_append(GTK_BOX(buttons), skip);
    gtk_box_append(GTK_BOX(box), buttons);

    gtk_window_set_default_widget(GTK_WINDOW(window), skip);
    gtk_window_present(GTK_WINDOW(window));
}

void on_retry_install_clicked(GtkButton* button, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    close_dialog_for_widget(GTK_WIDGET(button));
    start_install(installer, Mode::Install);
}

void on_retry_skip_deps_clicked(GtkButton* button, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    close_dialog_for_widget(GTK_WIDGET(button));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(installer->skip_deps_check), TRUE);
    start_install(installer, Mode::Install);
}

void on_retry_clean_clicked(GtkButton* button, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    close_dialog_for_widget(GTK_WIDGET(button));
    start_lock_cleanup(installer);
}

void show_lock_failure_help(Installer* installer) {
    std::string manager;
    const bool busy = package_manager_running(&manager);
    const bool lock = pacman_lock_exists();

    std::string message;
    if (busy) {
        message = T("A package manager is still running", "Bir paket yöneticisi hâlâ çalışıyor");
        message += std::string(" (") + (manager.empty() ? "?" : manager) + ").\n";
        message += T("Wait for it to finish, then retry. The lock was not touched.",
                     "Bitmesini bekleyip yeniden deneyin. Kilide dokunulmadı.");
    } else if (lock) {
        message = T("The installation stopped because the pacman database is "
                    "locked. You can skip dependency installation (the build "
                    "dependencies are already present in most repaired installs) "
                    "or remove the stale lock file and retry.",
                    "Kurulum pacman veritabanı kilitli olduğu için durdu. "
                    "Bağımlılık kurulumunu atlayabilir (onarılan kurulumların "
                    "çoğunda derleme bağımlılıkları zaten vardır) veya bayat "
                    "kilit dosyasını silip yeniden deneyebilirsiniz.");
    } else {
        message = T("The package-manager step failed. Retry the installation; "
                    "the full log is available below and can be copied.",
                    "Paket yöneticisi adımı başarısız oldu. Kurulumu yeniden "
                    "deneyin; tam günlük aşağıda ve kopyalanabilir.");
    }

    GtkWidget* window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window),
                         T("Package manager problem", "Paket yöneticisi sorunu"));
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(installer->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 620, lock ? 230 : 190);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget* label = gtk_label_new(message.c_str());
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget* cancel = gtk_button_new_with_label(T("Cancel", "Vazgeç"));
    GtkWidget* retry = gtk_button_new_with_label(T("Retry", "Yeniden Dene"));
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), window);
    g_signal_connect(retry, "clicked", G_CALLBACK(on_retry_install_clicked), installer);
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), retry);

    if (!busy && lock) {
        GtkWidget* clean = gtk_button_new_with_label(
            T("Remove lock and retry", "Kilidi temizle ve dene"));
        GtkWidget* skip = gtk_button_new_with_label(
            T("Skip deps and retry", "Bağımlılıkları atla ve dene"));
        gtk_widget_add_css_class(skip, "suggested-action");
        g_signal_connect(clean, "clicked", G_CALLBACK(on_retry_clean_clicked), installer);
        g_signal_connect(skip, "clicked", G_CALLBACK(on_retry_skip_deps_clicked), installer);
        gtk_box_append(GTK_BOX(buttons), clean);
        gtk_box_append(GTK_BOX(buttons), skip);
        gtk_window_set_default_widget(GTK_WINDOW(window), skip);
    } else {
        gtk_widget_add_css_class(retry, "suggested-action");
        gtk_window_set_default_widget(GTK_WINDOW(window), retry);
    }

    gtk_box_append(GTK_BOX(box), buttons);
    gtk_window_present(GTK_WINDOW(window));
}

// ── Child process plumbing ───────────────────────────────────────────────────
void update_sensitivity(Installer* installer) {
    const bool have_source = is_source_dir(installer->source_dir);
    const bool have_privilege = geteuid() == 0 || !find_program("pkexec").empty();
    const bool idle = !installer->running && !installer->cleanup_running;
    gtk_widget_set_sensitive(installer->install_button,
                             idle && have_source && have_privilege);
    gtk_widget_set_sensitive(installer->uninstall_button,
                             idle && have_source && installer->state.installed);
    gtk_widget_set_sensitive(installer->skip_deps_check, idle);
    gtk_widget_set_sensitive(installer->source_button, idle);
    gtk_widget_set_sensitive(installer->refresh_button, idle);
    gtk_widget_set_sensitive(installer->clear_button, idle);
    gtk_widget_set_sensitive(installer->launch_button,
                             idle && installer->state.installed);
    if (installer->copy_button)
        gtk_widget_set_sensitive(installer->copy_button, !installer->log_text.empty());
}

void set_running(Installer* installer, bool running) {
    installer->running = running;
    if (installer->spinner) {
        gtk_widget_set_visible(installer->spinner, running);
        gtk_spinner_set_spinning(GTK_SPINNER(installer->spinner), running);
    }
    update_sensitivity(installer);
}

void maybe_finish(Installer* installer) {
    if (!installer->running || installer->finished) return;
    if (!installer->child_done || !installer->out_eof || !installer->err_eof) return;
    installer->finished = true;

    const bool exited_ok = WIFEXITED(installer->child_status) &&
                           WEXITSTATUS(installer->child_status) == 0;
    const bool ok = exited_ok && !installer->saw_error;

    set_running(installer, false);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(installer->progress), ok ? 1.0 : 0.0);

    std::string message;
    if (installer->mode == Mode::Uninstall) {
        message = ok ? T("Uninstall completed. User profiles were kept.",
                         "Kaldırma tamamlandı. Kullanıcı profilleri korundu.")
                     : T("Uninstall finished with errors — see the log.",
                         "Kaldırma hatalarla bitti — günlüğe bakın.");
    } else {
        message = ok ? T("Installation completed successfully. You can open RawAccel now.",
                         "Kurulum başarıyla tamamlandı. RawAccel'i şimdi açabilirsiniz.")
                     : T("Installation finished with errors — see the log.",
                         "Kurulum hatalarla bitti — günlüğe bakın.");
    }
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(installer->progress),
                              ok ? T("Done", "Tamamlandı") : T("Failed", "Başarısız"));
    append_log(installer, "\n" + message + "\n");
    refresh_state(installer);
    set_status(installer, message, !ok);
    if (!ok && installer->mode == Mode::Install && installer->saw_pacman_lock)
        show_lock_failure_help(installer);
}

gboolean on_channel_data(GIOChannel* channel, GIOCondition condition, gpointer data) {
    auto* info = static_cast<ChannelData*>(data);
    Installer* installer = info->installer;

    char buffer[4096];
    gsize bytes_read = 0;
    GError* error = nullptr;
    const GIOStatus status =
        g_io_channel_read_chars(channel, buffer, sizeof(buffer), &bytes_read, &error);

    if (status == G_IO_STATUS_NORMAL) {
        if (bytes_read) append_log(installer, buffer, bytes_read);
        g_clear_error(&error);
        return TRUE;
    }
    if (status == G_IO_STATUS_AGAIN) {
        g_clear_error(&error);
        return TRUE;
    }

    if (status == G_IO_STATUS_ERROR && error) {
        append_log(installer, std::string("\n[channel] ") + error->message + "\n");
        installer->saw_error = true;
        g_error_free(error);
    } else {
        g_clear_error(&error);
    }

    const bool is_error = info->is_error;
    if (is_error) {
        installer->err_eof = true;
        installer->err_watch = 0;
        installer->err_channel = nullptr;
    } else {
        installer->out_eof = true;
        installer->out_watch = 0;
        installer->out_channel = nullptr;
    }
    if (condition & G_IO_NVAL) {
        // A closed/invalid channel is also an end-of-stream for our purposes.
        if (is_error) installer->err_eof = true;
        else installer->out_eof = true;
    }
    g_io_channel_unref(channel);
    delete info;
    maybe_finish(installer);
    return FALSE;
}

void on_child_exit(GPid pid, gint status, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    installer->child_status = status;
    installer->child_done = true;
    installer->child_pid = 0;
    g_spawn_close_pid(pid);
    maybe_finish(installer);
}

void start_install(Installer* installer, Mode mode) {
    if (installer->running || installer->cleanup_running) return;
    if (!is_source_dir(installer->source_dir)) {
        show_info(installer, T("Source folder required", "Kaynak klasörü gerekli"),
                  T("Select the rawaccel-linux source folder before installing.",
                    "Kurulumdan önce rawaccel-linux kaynak klasörünü seçin."));
        return;
    }

    const bool skip_deps = gtk_check_button_get_active(
        GTK_CHECK_BUTTON(installer->skip_deps_check));

    // Dependency installation is the only step that touches pacman.  Catch a
    // busy/stale database before spawning setup.sh, so the GUI can offer a
    // click-through repair instead of failing in the log.
    if (mode == Mode::Install && !skip_deps) {
        if (geteuid() != 0 && find_program("pkexec").empty()) {
            const char* message =
                T("Administrator permission requires pkexec, which was not found. "
                  "The installation cannot start without it.",
                  "Yönetici izni için pkexec gerekli, ancak bulunamadı. "
                  "Kurulum bu araç olmadan başlatılamaz.");
            set_status(installer, message, true);
            show_info(installer, T("Permission required", "İzin gerekli"), message);
            return;
        }
        if (pacman_lock_exists()) {
            std::string manager;
            if (package_manager_running(&manager)) {
                const std::string message =
                    T("A package manager is running", "Bir paket yöneticisi çalışıyor") +
                    std::string(" (") + (manager.empty() ? "?" : manager) + ").\n" +
                    T("Wait for it to finish, then press Install/Reinstall again. "
                      "The package lock was not touched.",
                      "Bitmesini bekleyip Yeniden Kur'a tekrar basın. "
                      "Paket kilidine dokunulmadı.");
                set_status(installer, message, true);
                show_info(installer, T("Package manager busy", "Paket yöneticisi meşgul"),
                          message);
                return;
            }
            show_stale_lock_confirm(installer);
            return;
        }
    }

    const Command command = make_command(installer->source_dir, mode, skip_deps);
    if (!command.error.empty()) {
        show_info(installer, T("Cannot start installation", "Kurulum başlatılamıyor"),
                  command.error);
        return;
    }

    installer->mode = mode;
    installer->finished = false;
    installer->child_done = false;
    installer->out_eof = false;
    installer->err_eof = false;
    installer->child_status = 0;
    installer->saw_error = false;
    installer->saw_pacman_lock = false;
    installer->log_text.clear();
    gtk_text_buffer_set_text(installer->log_buffer, "", -1);

    set_running(installer, true);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(installer->progress), 0.0);
    gtk_progress_bar_set_text(
        GTK_PROGRESS_BAR(installer->progress),
        mode == Mode::Uninstall ? T("Uninstalling…", "Kaldırılıyor…")
                                : T("Installing…", "Kuruluyor…"));
    set_status(installer,
               mode == Mode::Uninstall ? T("Removing RawAccel…", "RawAccel kaldırılıyor…")
                                       : T("Installing RawAccel…", "RawAccel kuruluyor…"),
               false);

    append_log(installer, "$ " + join_command(command.argv) + "\n\n");
    update_sensitivity(installer);

    std::vector<char*> argv;
    argv.reserve(command.argv.size() + 1);
    for (const std::string& arg : command.argv)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    gint stdin_fd = -1;
    gint stdout_fd = -1;
    gint stderr_fd = -1;
    GPid pid = 0;
    GError* error = nullptr;
    const GSpawnFlags flags = static_cast<GSpawnFlags>(
        G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH);
    const gboolean ok = g_spawn_async_with_pipes(
        installer->source_dir.c_str(), argv.data(), nullptr, flags,
        nullptr, nullptr, &pid, &stdin_fd, &stdout_fd, &stderr_fd, &error);
    if (!ok) {
        if (stdin_fd >= 0) close(stdin_fd);
        set_running(installer, false);
        std::string message = T("Could not start setup.sh.", "setup.sh başlatılamadı.");
        if (error) {
            message += "\n";
            message += error->message;
            g_error_free(error);
        }
        set_status(installer, message, true);
        show_info(installer, T("Installation error", "Kurulum hatası"), message);
        return;
    }

    // EOF on stdin: no prompt in setup.sh can block the GTK main loop.
    if (stdin_fd >= 0) close(stdin_fd);
    installer->child_pid = pid;

    installer->out_channel = g_io_channel_unix_new(stdout_fd);
    installer->err_channel = g_io_channel_unix_new(stderr_fd);
    for (GIOChannel* channel : {installer->out_channel, installer->err_channel}) {
        g_io_channel_set_encoding(channel, nullptr, nullptr);
        g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, nullptr);
    }

    auto* out_data = new ChannelData{installer, false};
    auto* err_data = new ChannelData{installer, true};
    installer->out_watch = g_io_add_watch(
        installer->out_channel,
        static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL),
        on_channel_data, out_data);
    installer->err_watch = g_io_add_watch(
        installer->err_channel,
        static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL),
        on_channel_data, err_data);
    installer->child_watch = g_child_watch_add(pid, on_child_exit, installer);
}

// ── Status refresh ────────────────────────────────────────────────────────────
void refresh_state(Installer* installer) {
    installer->state = detect_state();

    set_value(installer->version_value,
              installer->state.version.empty() ? "—" : installer->state.version,
              installer->state.version.empty() ? "warn" : "ok");
    set_value(installer->daemon_value,
              installer->state.daemon_path.empty()
                  ? T("Not installed", "Kurulu değil")
                  : T("Installed", "Kurulu"),
              installer->state.daemon_path.empty() ? "bad" : "ok");
    if (!installer->state.service_known) {
        set_value(installer->service_value, T("Unknown", "Bilinmiyor"), "warn");
    } else if (installer->state.service_active) {
        set_value(installer->service_value, T("Running", "Çalışıyor"), "ok");
    } else {
        set_value(installer->service_value, T("Stopped", "Durmuş"), "warn");
    }
    set_value(installer->gui_value,
              installer->state.gui_path.empty()
                  ? T("Not installed", "Kurulu değil")
                  : T("Installed", "Kurulu"),
              installer->state.gui_path.empty() ? "warn" : "ok");

    if (installer->source_dir.empty()) {
        set_value(installer->source_label,
                  T("Source folder not found — choose the rawaccel-linux directory.",
                    "Kaynak klasör bulunamadı — rawaccel-linux dizinini seçin."),
                  "bad");
        gtk_button_set_label(GTK_BUTTON(installer->source_button),
                             T("Choose…", "Seç…"));
    } else {
        set_value(installer->source_label, installer->source_dir, "ok");
        gtk_button_set_label(GTK_BUTTON(installer->source_button),
                             T("Change…", "Değiştir…"));
    }

    gtk_button_set_label(GTK_BUTTON(installer->install_button),
                         installer->state.installed
                             ? T("Reinstall", "Yeniden Kur")
                             : T("Install now", "Hemen Kur"));
    gtk_widget_set_visible(installer->launch_button, installer->state.installed);

    update_sensitivity(installer);

    if (installer->running) return;
    if (installer->source_dir.empty()) {
        set_status(installer,
                   T("Select the source folder to enable installation.",
                     "Kurulumu etkinleştirmek için kaynak klasörü seçin."),
                   true);
    } else if (geteuid() != 0 && find_program("pkexec").empty()) {
        set_status(installer,
                   T("pkexec not found — administrator permission cannot be requested.",
                     "pkexec bulunamadı — yönetici izni istenemiyor."),
                   true);
    } else {
        set_status(installer, T("Ready.", "Hazır."), false);
    }
}

// ── Window construction ──────────────────────────────────────────────────────
gboolean on_close_request(GtkWindow*, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    if (!installer->running && !installer->cleanup_running) return FALSE;
    set_status(installer,
               T("An operation is still running — please wait for it to finish.",
                 "Bir işlem hâlâ sürüyor — bitmesini bekleyin."),
               true);
    return TRUE;
}

void build_ui(Installer* installer) {
    installer->window = gtk_application_window_new(installer->app);
    gtk_window_set_title(GTK_WINDOW(installer->window),
                         T("RawAccel Linux Setup", "RawAccel Linux Kurulum"));
    gtk_window_set_default_size(GTK_WINDOW(installer->window), 860, 720);
    gtk_window_set_icon_name(GTK_WINDOW(installer->window), "input-mouse");
    g_signal_connect(installer->window, "close-request",
                     G_CALLBACK(on_close_request), installer);

    GtkWidget* header = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(installer->window), header);

    GtkWidget* title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* title = make_label("RawAccel Linux", "title-label");
    GtkWidget* subtitle = make_label(
        T("Setup assistant", "Kurulum yardımcısı"), "subtitle");
    gtk_box_append(GTK_BOX(title_box), title);
    gtk_box_append(GTK_BOX(title_box), subtitle);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_box);

    installer->refresh_button = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(installer->refresh_button,
                                T("Refresh installation status", "Kurulum durumunu yenile"));
    g_signal_connect(installer->refresh_button, "clicked",
                     G_CALLBACK(on_refresh_clicked), installer);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), installer->refresh_button);

    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(outer, 18);
    gtk_widget_set_margin_end(outer, 18);
    gtk_widget_set_margin_top(outer, 16);
    gtk_widget_set_margin_bottom(outer, 14);
    gtk_window_set_child(GTK_WINDOW(installer->window), outer);

    GtkWidget* intro = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    GtkWidget* icon = gtk_image_new_from_icon_name("system-software-install-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_set_valign(icon, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(intro), icon);
    GtkWidget* intro_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* intro_title = make_label(
        T("Install RawAccel in a few clicks", "RawAccel'i birkaç tıkla kurun"),
        "title-label");
    GtkWidget* intro_body = make_label(
        T("Administrator permission is requested when installation starts. "
          "The official setup.sh does the real work, so the GUI uses the same installation logic.",
          "Kurulum başlarken yönetici izni istenir. Gerçek işi resmî setup.sh yapar; "
          "böylece GUI aynı kurulum mantığını kullanır."),
        "subtitle");
    gtk_label_set_wrap(GTK_LABEL(intro_body), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(intro_body), 90);
    gtk_box_append(GTK_BOX(intro_text), intro_title);
    gtk_box_append(GTK_BOX(intro_text), intro_body);
    gtk_box_append(GTK_BOX(intro), intro_text);
    gtk_box_append(GTK_BOX(outer), intro);

    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");
    GtkWidget* card_title = make_label(T("Installation status", "Kurulum durumu"),
                                       "card-title");
    gtk_box_append(GTK_BOX(card), card_title);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 24);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    add_row(grid, 0, "Version", "Sürüm", &installer->version_value);
    add_row(grid, 1, "Daemon", "Daemon", &installer->daemon_value);
    add_row(grid, 2, "Service", "Servis", &installer->service_value);
    add_row(grid, 3, "GUI", "GUI", &installer->gui_value);

    GtkWidget* source_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    installer->source_label = make_label("—", "value");
    gtk_label_set_ellipsize(GTK_LABEL(installer->source_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_selectable(GTK_LABEL(installer->source_label), TRUE);
    gtk_widget_set_hexpand(installer->source_label, TRUE);
    installer->source_button = gtk_button_new_with_label(T("Choose…", "Seç…"));
    g_signal_connect(installer->source_button, "clicked",
                     G_CALLBACK(on_choose_source), installer);
    gtk_box_append(GTK_BOX(source_row), installer->source_label);
    gtk_box_append(GTK_BOX(source_row), installer->source_button);
    GtkWidget* source_name = make_label(T("Source", "Kaynak"), nullptr);
    gtk_widget_set_margin_end(source_name, 18);
    gtk_grid_attach(GTK_GRID(grid), source_name, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), source_row, 1, 4, 1, 1);

    gtk_box_append(GTK_BOX(card), grid);
    gtk_box_append(GTK_BOX(outer), card);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    installer->install_button = gtk_button_new_with_label(T("Install now", "Hemen Kur"));
    gtk_widget_add_css_class(installer->install_button, "suggested-action");
    g_signal_connect(installer->install_button, "clicked",
                     G_CALLBACK(on_install_clicked), installer);
    installer->uninstall_button = gtk_button_new_with_label(T("Uninstall", "Kaldır"));
    gtk_widget_add_css_class(installer->uninstall_button, "destructive-action");
    g_signal_connect(installer->uninstall_button, "clicked",
                     G_CALLBACK(on_uninstall_clicked), installer);
    installer->launch_button = gtk_button_new_with_label(
        T("Open RawAccel GUI", "RawAccel GUI'yi Aç"));
    g_signal_connect(installer->launch_button, "clicked",
                     G_CALLBACK(on_launch_clicked), installer);
    gtk_widget_set_visible(installer->launch_button, FALSE);
    gtk_box_append(GTK_BOX(actions), installer->install_button);
    gtk_box_append(GTK_BOX(actions), installer->uninstall_button);
    gtk_box_append(GTK_BOX(actions), installer->launch_button);
    gtk_box_append(GTK_BOX(outer), actions);

    installer->skip_deps_check = gtk_check_button_new_with_label(
        T("Skip dependency installation (--no-deps)",
          "Bağımlılık kurulumunu atla (--no-deps)"));
    gtk_widget_set_tooltip_text(
        installer->skip_deps_check,
        T("Use this when the build dependencies are already installed.",
          "Derleme bağımlılıkları zaten kuruluysa bunu kullanın."));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(installer->skip_deps_check),
                                installer->initial_no_deps);
    gtk_box_append(GTK_BOX(outer), installer->skip_deps_check);

    installer->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(installer->progress), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(installer->progress),
                              T("Ready", "Hazır"));
    gtk_box_append(GTK_BOX(outer), installer->progress);

    GtkWidget* log_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* log_title = make_label(T("Installation log", "Kurulum günlüğü"),
                                      "card-title");
    gtk_widget_set_hexpand(log_title, TRUE);
    installer->copy_button = gtk_button_new_with_label(T("Copy log", "Günlüğü Kopyala"));
    gtk_widget_set_tooltip_text(
        installer->copy_button,
        T("Copy the installation log to the clipboard.",
          "Kurulum günlüğünü panoya kopyalar."));
    g_signal_connect(installer->copy_button, "clicked",
                     G_CALLBACK(on_copy_log), installer);
    installer->clear_button = gtk_button_new_with_label(T("Clear", "Temizle"));
    g_signal_connect(installer->clear_button, "clicked",
                     G_CALLBACK(on_clear_log), installer);
    gtk_box_append(GTK_BOX(log_header), log_title);
    gtk_box_append(GTK_BOX(log_header), installer->copy_button);
    gtk_box_append(GTK_BOX(log_header), installer->clear_button);
    gtk_box_append(GTK_BOX(outer), log_header);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    installer->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(installer->log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(installer->log_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(installer->log_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_margin_start(installer->log_view, 8);
    gtk_widget_set_margin_end(installer->log_view, 8);
    gtk_widget_set_margin_top(installer->log_view, 8);
    gtk_widget_set_margin_bottom(installer->log_view, 8);
    installer->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(installer->log_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(installer->log_buffer, &end);
    installer->log_end = gtk_text_buffer_create_mark(
        installer->log_buffer, nullptr, &end, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), installer->log_view);
    gtk_box_append(GTK_BOX(outer), scroll);

    GtkWidget* status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    installer->spinner = gtk_spinner_new();
    gtk_widget_set_visible(installer->spinner, FALSE);
    installer->status_label = make_label(T("Ready.", "Hazır."), "status");
    gtk_label_set_ellipsize(GTK_LABEL(installer->status_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(installer->status_label, TRUE);
    gtk_box_append(GTK_BOX(status_box), installer->spinner);
    gtk_box_append(GTK_BOX(status_box), installer->status_label);
    gtk_box_append(GTK_BOX(outer), status_box);
}

void on_activate(GtkApplication* application, gpointer data) {
    auto* installer = static_cast<Installer*>(data);
    if (installer->window) {
        gtk_window_present(GTK_WINDOW(installer->window));
        return;
    }
    installer->app = application;
    install_css();
    build_ui(installer);
    refresh_state(installer);
    gtk_window_present(GTK_WINDOW(installer->window));
}

// ── CLI ───────────────────────────────────────────────────────────────────────
struct Options {
    std::string source;
    bool detect = false;
    bool print_command = false;
    bool uninstall = false;
    bool no_deps = false;
    bool help = false;
    bool version = false;
};

void print_help() {
    std::printf(
        "RawAccel Linux GUI installer\n"
        "\n"
        "Usage: rawaccel-installer [options]\n"
        "  --source DIR       RawAccel source directory (contains setup.sh)\n"
        "  --detect           Print installed/daemon/service state and exit\n"
        "  --print-command    Print the setup.sh command that would run, then exit\n"
        "  --uninstall        With --print-command: print the uninstall command\n"
        "  --no-deps          With install: pass --no-deps to setup.sh\n"
        "  --help, -h         Show this help\n"
        "  --version, -V      Show version\n"
        "\n"
        "Without --detect/--print-command, opens the GTK4 installer window.\n");
}

bool parse_options(int argc, char** argv, Options* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options->help = true;
        } else if (arg == "--version" || arg == "-V") {
            options->version = true;
        } else if (arg == "--detect") {
            options->detect = true;
        } else if (arg == "--print-command") {
            options->print_command = true;
        } else if (arg == "--uninstall") {
            options->uninstall = true;
        } else if (arg == "--no-deps") {
            options->no_deps = true;
        } else if (arg == "--source") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "rawaccel-installer: --source requires a path\n");
                return false;
            }
            options->source = argv[++i];
        } else if (arg.rfind("--source=", 0) == 0) {
            options->source = arg.substr(9);
        } else {
            std::fprintf(stderr, "rawaccel-installer: unknown option: %s\n", arg.c_str());
            return false;
        }
    }
    return true;
}

void print_detect(const std::string& source) {
    const InstallState state = detect_state();
    std::printf("installed=%d\n", state.installed ? 1 : 0);
    std::printf("daemon=%s\n", state.daemon_path.c_str());
    std::printf("cli=%s\n", state.cli_path.c_str());
    std::printf("gui=%s\n", state.gui_path.c_str());
    std::printf("version=%s\n", state.version.c_str());
    std::printf("service=%s\n",
                state.service_known ? (state.service_active ? "active" : "inactive")
                                    : "unknown");
    std::string manager;
    const bool package_busy = package_manager_running(&manager);
    std::printf("pacman_lock=%d\n", pacman_lock_exists() ? 1 : 0);
    std::printf("pacman_busy=%d\n", package_busy ? 1 : 0);
    if (package_busy) std::printf("pacman_manager=%s\n", manager.c_str());
    std::printf("source=%s\n", source.c_str());
}

} // namespace

int main(int argc, char** argv) {
    // Match the GUI's locale policy: explicit Turkish locale gets Turkish,
    // everything else stays English.  No config file is read before install.
    setlocale(LC_ALL, "");
    g_turkish = locale_is_turkish();
    install_gtk_log_filter();

    Options options;
    if (!parse_options(argc, argv, &options)) {
        print_help();
        return 2;
    }
    if (options.help) {
        print_help();
        return 0;
    }
    if (options.version) {
        std::printf("rawaccel-installer %s\n", rawaccel::RAWACCEL_VERSION);
        return 0;
    }

    const std::string source = resolve_source(options.source);

    if (options.detect) {
        print_detect(source);
        return 0;
    }
    if (options.print_command) {
        if (source.empty()) {
            std::fprintf(stderr,
                         "rawaccel-installer: source folder not found; pass --source DIR\n");
            return 2;
        }
        const Command command = make_command(
            source, options.uninstall ? Mode::Uninstall : Mode::Install,
            options.no_deps);
        if (!command.error.empty()) {
            std::fprintf(stderr, "rawaccel-installer: %s\n", command.error.c_str());
            return 1;
        }
        std::printf("%s\n", join_command(command.argv).c_str());
        return 0;
    }

    Installer installer;
    installer.source_dir = source;
    installer.user = current_user();
    installer.initial_no_deps = options.no_deps;

    GtkApplication* app = gtk_application_new(
        "io.github.rawaccel.installer", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &installer);

    // Do not let GtkApplication parse our own options (--source, --detect, …).
    char* app_argv[] = {argv[0], nullptr};
    const int result = g_application_run(G_APPLICATION(app), 1, app_argv);
    g_object_unref(app);
    return result;
}
