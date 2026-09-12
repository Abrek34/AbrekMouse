# RawAccel Linux

A Linux port of [Windows Raw Accel](https://github.com/a1xd/rawaccel), using the same acceleration algorithms.

> **Current state: v1.1.0** — native Logitech HID++ hardware controls for DPI,
> report rate, and sensor lift-off distance. Release notes: see
> [CHANGELOG.md](CHANGELOG.md).

## Features

- **Same algorithms as Raw Accel**: Classic, Power, Natural, Jump, Synchronous, Lookup Table
- **Userspace daemon**: kernel-level feel via `libevdev` + `uinput`
- **GTK4 GUI**: similar interface to Raw Accel, with a real-time gain curve graph
- **CLI**: full control via `rawaccel-cli`
- **JSON config**: `~/.config/rawaccel/settings.json`
- **Multi-profile + per-device assignment**: different settings per mouse
- **systemd service**: automatic startup support
- **Hot-plug**: automatically picks up mice connected/disconnected at runtime
- **Live reload**: config changes apply instantly without releasing the mouse grab

## Dependencies

```bash
# Arch Linux
sudo pacman -S --needed libevdev gtk4 base-devel cmake pkgconf polkit systemd python qt6-tools

# Debian/Ubuntu
sudo apt install build-essential cmake pkg-config libevdev-dev libgtk-4-dev libpolkit-gobject-1-dev systemd udev policykit-1 python3 qt6-tools-dev-tools

# Fedora/RHEL
sudo dnf install gcc-c++ make cmake pkgconf-pkg-config libevdev-devel gtk4-devel polkit polkit-devel systemd python3 qt6-qttools
```

`pkgconf`/`pkg-config` is required to build (`scripts/build.sh` and the CMake
target use `pkg-config` for `libevdev`/`gtk4`); `polkit`, `systemd`, `python3`
and the qt6 tools (qdbus6) are runtime/aux deps for the GUI's systemd control
and the KDE flat-acceleration fix. `setup.sh` installs exactly this set and
verifies it — prefer it over manual package setup.

## Build

```bash
# Quick build (no cmake required)
bash scripts/build.sh

# Portable binary (no -march=native, runs on other CPUs)
RAWACCEL_PORTABLE=1 bash scripts/build.sh

# Custom compiler
CXX=clang++ bash scripts/build.sh
```

Both build paths (`scripts/build.sh` and the CMake `Release` target) apply the
same hardening flags: stack protector/clash protection, `-D_FORTIFY_SOURCE=2`,
`-D_GLIBCXX_ASSERTIONS`, `-fPIE`+`-pie`, `-Wl,-z relro,now,noexecstack,separate-code`
(and `-fcf-protection` on x86).

```bash
# With CMake (optional)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
# Install: use /usr (NOT the /usr/local default) so binaries land where the
# systemd service expects them.  The install preserves an existing
# /etc/rawaccel/settings.json, installs udev + modules-load.d + quirks, and
# a /usr/local prefix emits a configure-time warning.
sudo cmake --install . --prefix /usr
```

Compiled binaries are placed in `build-manual/`.

## Installation

The **canonical one-shot installer is `setup.sh`** at the repo root. It installs
all system dependencies (for your distro), cleans any previous install, builds,
installs binaries + systemd/udev/desktop/libinput-quirk (no polkit policy is
installed — RawAccel 1.1.0+ relies on the `input` group and default pkexec),
enables the service, and applies the KDE Plasma flat-acceleration fix:

```bash
sudo bash setup.sh                # full install (deps + build + system-wide + KDE fix)
sudo bash setup.sh --no-deps      # skip system dependency installation
sudo bash setup.sh --reinstall    # clean old install, then reinstall (default)
```

`scripts/install.sh` is a thin wrapper that forwards to `setup.sh` (kept for
backwards compatibility).

> **Arch package coexistence**: on Arch-based distros you can instead install
> the packaged binary (`packaging/rawaccel-linux-1.1.0-1-x86_64.pkg.tar.zst`,
> `sudo pacman -U ...`) or publish it via an AUR package. Do **not** mix
> `setup.sh` and the pacman package: both install the systemd unit to the same
> path (`/usr/lib/systemd/system/rawaccel.service`) and both install the
> binaries to `/usr/bin`, so the last install wins — a stale copy on PATH keeps
> grabbing your mouse after the other install. Since 0.5.0-2 the package
> auto-cleans legacy setup.sh files in its pre-install hook (the old
> `/etc/systemd/system` shadow unit plus `/usr/local/bin` and `~/.local/bin`
> binaries) and never overwrites your existing `/etc/rawaccel/settings.json`.
> If you have an old manual install, either run `setup.sh --uninstall` first,
> or let the package's pre-install hook clean it.

Uninstall (keeps your `~/.config/rawaccel` and `/etc/rawaccel/settings.json`):

```bash
sudo bash setup.sh --uninstall
```

Manual install (equivalent steps, for reference):
```bash
# Build
bash scripts/build.sh

sudo cp build-manual/rawaccel-daemon /usr/bin/
sudo cp build-manual/rawaccel-cli    /usr/bin/
sudo cp build-manual/rawaccel-gui    /usr/bin/

# System config dir + default config (used by the systemd service)
sudo mkdir -p /etc/rawaccel
sudo cp config/default.json /etc/rawaccel/settings.json   # if not already present

# udev rule (keeps /dev/uinput accessible) + uinput module
sudo install -m644 scripts/99-rawaccel.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
echo uinput | sudo tee /etc/modules-load.d/rawaccel.conf
sudo modprobe uinput

# libinput quirk (R7-README parity — disables libinput's flat-pointing
# acceleration for the virtual device, else you get double acceleration)
sudo install -m644 scripts/rawaccel.quirks /usr/share/libinput/50-rawaccel.quirks

# GUI launcher
sudo install -m644 scripts/rawaccel.desktop /usr/share/applications/

# Start the daemon at boot
sudo systemctl enable --now rawaccel
```

> **Note:** the systemd service runs the daemon as **root** (it needs to grab
> `/dev/input/event*` and create `/dev/uinput`, and the service file drops its
> capability bounding set to only the minimum required).  The **GUI and CLI run as
> your user** — see [Config file sync](#config-file-sync) for how the two stay in
> agreement.

## Usage

### GUI

```bash
rawaccel-gui
```

**GUI language:** the header-bar dropdown switches the interface live between
**Auto (locale)** / **English** / **Türkçe** without restarting. The choice is
persisted to `~/.config/rawaccel/gui_lang` (`auto` / `en` / `tr`). In **Auto**
mode the system locale is used (`LANG`/`setlocale`), so a Turkish system starts
in Türkçe automatically. Every translatable UI string has a Turkish entry —
the translation-coverage tool (`tests/run_tr_coverage.sh`) enforces this.

### CLI

```bash
# List all profiles
rawaccel-cli list

# Show a profile's details
rawaccel-cli show gaming

# Create / clone / rename profiles
rawaccel-cli create gaming                # new profile with defaults
rawaccel-cli create-preset gaming pro1    # from preset (gaming, office, precision, disable, cs2, valorant, apex, fps)
rawaccel-cli duplicate gaming backup      # clone (clears device_id)
rawaccel-cli rename gaming fps            # rename
rawaccel-cli delete fps                   # delete

# Set parameters
rawaccel-cli set-param gaming mode classic
rawaccel-cli set-param gaming acceleration 0.005
rawaccel-cli set-param gaming exponent_classic 2
rawaccel-cli set-param gaming limit 1.8
rawaccel-cli set-param gaming dpi 800
rawaccel-cli set-param gaming yx_ratio 1.0    # Y-axis output DPI ratio (vs X)
# Partial key list — the most-used subset.  The complete set-param key set —
# raw, distance_mode, lp_norm, input/scale/output_smooth_halflife,
# domain_weights/domain_weight_x/_y, range_weights/range_weight_x/_y, plus
# every key below — is documented in docs/research/parameter_index.md
# ("CLI erişilebilirlik denetimi (set-param)") and in `rawaccel-cli --help`.

# Switch active profile (signals daemon to reload)
rawaccel-cli set gaming

# Live status: profiles + per-device detected DPI / polling rate / battery,
# which profile each connected mouse resolves to, plus per-device live last-motion
# telemetry (telem_in_ips / telem_out_ips / telem_gain / telem_dx / telem_dy /
# telem_wall_ms — sample timestamp in CLOCK_MONOTONIC_RAW ms since boot)
rawaccel-cli status

# Export/import as JSON
rawaccel-cli export gaming > backup.json
rawaccel-cli import backup.json

# Config validation, reload, control
rawaccel-cli validate            # check config for errors/warnings
rawaccel-cli reload              # reload daemon config (SIGHUP)
rawaccel-cli stop                # stop daemon (SIGTERM)
rawaccel-cli latency             # per-device processing latency stats (IPC first, SIGUSR1 fallback)
```

### Logitech HID++ hardware controls

RawAccel includes a native C++ HID++ control path for compatible Logitech
devices. It does not add a Python dependency and does not run in the motion
processing hot path. First discover the hidraw node and capabilities:

```bash
rawaccel-cli hidpp
```

When the device exposes the corresponding HID++ feature, the following
commands change the hardware setting directly:

```bash
rawaccel-cli hidpp-set-dpi /dev/hidrawN 1600
rawaccel-cli hidpp-set-rate /dev/hidrawN 1000
rawaccel-cli hidpp-set-lod /dev/hidrawN low
```

Report rates and DPI values are restricted to values advertised by the device.
Lift-off distance requires HID++ Extended Adjustable DPI support; older mice
and receivers may report that the feature is unavailable. These commands do
not change the RawAccel profile's software calibration fields, so set the
profile `dpi` and `polling_rate` to the new physical values separately when
using acceleration.

The **GTK4 GUI** exposes the same hardware controls as panel **Hardware
Settings (Logitech HID++)**: it enumerates `/dev/hidraw*` nodes, writes the
onboard DPI, report/polling rate and lift-off distance to the physical device,
and reports rejected values in the status line. It also shows the friendly
device name, full read-only firmware/device-ID metadata, and consumes bounded
HID++ battery notifications on a background worker. New or removed
`/dev/hidraw*` nodes refresh the panel automatically. Like the CLI, it is
independent of the software profile fields and never touches the
motion-processing hot path.

For the selected device the panel also shows two read-only rows backed by the
same worker threads:

- **Battery** — live level, charging/online state and the battery facility the
  device actually advertises (`Unified Battery` 0x1004 / `Battery Status`
  0x1000 / `Battery Voltage` 0x1001 / HID++ 1.0 register fallback), refreshed
  by HID++ battery notifications. When the daemon is running, its own live
  battery telemetry is shown separately as `Daemon live battery: N%` so the two
  feeds are never confused.
- **Capabilities** — a feature-derived summary (DPI/extended X/Y, lift-off
  distance, report-rate class, onboard profiles, battery source, composited
  device `Model ID`) plus known Solaar-validated model quirks. Fields a device
  does not advertise are *disabled* in the panel and spelled out here as
  `not advertised`.

Capabilities are derived from the device's own FEATURE_SET discovery — see
`include/logitech_quirks.hpp` for the independent per-model table (encoded from
the behaviours Solaar's `device_quirks.py` validated on real hardware, used
with a **default-DENY** policy: unlisted models are write-protected).

The HID++ integration deliberately remains read-only for device discovery,
battery, firmware metadata, receiver slots, and onboard-profile inspection.
Pairing/authentication, remapping/LED/SmartShift writes, profile flash writes,
firmware/DFU, and the Solaar Centurion bridge are not enabled.

### Daemon

```bash
# Start manually
sudo rawaccel-daemon

# Verbose mode: shows device open/uinput creation details
sudo rawaccel-daemon -v

# Custom config file / JSON log format (--config=PATH form is also accepted)
sudo rawaccel-daemon --config /etc/rawaccel/settings.json
sudo rawaccel-daemon --log-format json -v    # one JSON object per log line

# Run as a systemd service (also enables start-on-boot)
sudo systemctl enable --now rawaccel

# Status / logs
systemctl status rawaccel
journalctl -u rawaccel -f

# Reload config without restarting
rawaccel-cli reload
# or
kill -HUP $(cat /run/rawaccel.pid)

# Stop / disable
sudo systemctl stop rawaccel
sudo systemctl disable rawaccel
```

> Editing config with the **GUI or `rawaccel-cli` does not require a manual
> reload** — those tools push the updated config to the daemon over IPC
> (`set_config`), which the daemon persists and live-applies.  `reload`/`SIGHUP`
> is only needed when you hand-edit a config file on disk.

### Config file sync

There are **two** config locations:

| Location | Purpose |
|----------|---------|
| `~/.config/rawaccel/settings.json` | The working copy the **GUI and CLI** read/write |
| `/etc/rawaccel/settings.json` | The copy the **systemd daemon** reads at boot |

The sync flow works automatically:

1. You edit a profile with `rawaccel-gui` or `rawaccel-cli` → saved to your
   user config `~/.config/rawaccel/settings.json`.
2. The tool sends the full config to the running daemon over its **IPC socket**
   (`set_config` RPC).  The root daemon persists it to `/etc/rawaccel/settings.json`
   and live-applies it — no logout, no SIGHUP, no binary divergence.
3. Falls back: if the daemon doesn't speak the RPC (or the socket is unreachable),
   the tool sends `SIGHUP`/`reload` so the daemon re-reads its own copy.

The GUI shows exactly which of the three outcomes happened after every save:

- **"Applied & reloaded: …"** — the daemon received your config over IPC and
  live-applied it (normal case).
- **"Saved locally, but the daemon was not updated: …"** — daemon is running but
  the IPC push was rejected/failed; your file is saved but not yet live.
- **"Saved locally, but the daemon is not running: …"** — no daemon, nothing to
  push to; your file is saved and will be used at next daemon start.

So the two files are kept identical automatically whenever you edit via the
GUI/CLI.  Hand-editing `/etc/rawaccel/settings.json` directly still requires a
`rawaccel-cli reload` (or the GUI **Reload** shortcut `Ctrl+R`).

Verify they match:
```bash
cmp ~/.config/rawaccel/settings.json /etc/rawaccel/settings.json && echo IDENTICAL
```

If both `~/.config/rawaccel/settings.json` and `/etc/rawaccel/settings.json`
are missing, GUI/CLI create a default config automatically.

## Parameters

### Acceleration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `mode` | `classic`, `power`, `natural`, `jump`, `synchronous`, `lookup`, `noaccel` | `noaccel` |
| `gain` | Enable gain mode (recommended) | `true` |
| `acceleration` | Acceleration multiplier | `0.005` |
| `exponent_classic` | Classic mode exponent | `2.0` |
| `exponent_power` | Power mode exponent | `0.05` |
| `limit` | Natural mode upper gain asymptote | `1.5` |
| `decay_rate` | Natural mode decay rate | `0.1` |
| `motivity` | Synchronous mode motivity | `1.5` |
| `gamma` | Synchronous mode gamma | `1.0` |
| `input_offset` | Speed threshold before acceleration starts (ips) — **classic/natural only**; inert in power/jump/synchronous (stored, never read) | `0` |
| `output_offset` | Output offset (power mode) | `0` |
| `scale` | Scale factor (power mode) | `1.0` |
| `sync_speed` | Synchronous mode reference speed (ips) | `5.0` |
| `smooth` | Jump + synchronous sigmoid smoothing | `0.5` |
| `cap_x` | Input speed cap (ips) | `15` |
| `cap_y` | Output gain cap | `1.5` |
| `cap_mode` | Cap mode: `out`, `in`, `io` | `out` |

### Motion

| Parameter | Description | Default |
|-----------|-------------|---------|
| `rotation` | Axis rotation in degrees | `0` |
| `snap` | Angle snap in degrees | `0` |
| `speed_min` | Minimum speed clamp (ips), `0` = disabled | `0` |
| `speed_max` | Maximum speed clamp (ips), `0` = disabled | `0` |

### Speed Processor

| Parameter | Description | Default |
|-----------|-------------|---------|
| `distance_mode` | How speed is computed: `euclidean`, `max`, `lp`, `separate` | `euclidean` |
| `lp_norm` | Lp-norm exponent (only when `distance_mode=lp`) | `2.0` |
| `input_smooth_halflife` | EMA half-life for input speed smoothing (ms, `0` = off) | `0` |
| `scale_smooth_halflife` | EMA half-life for scale smoothing (ms, `0` = off) | `0` |
| `output_smooth_halflife` | EMA half-life for output speed smoothing (ms, `0` = off) | `0` |

### Device

| Parameter | Description | Default |
|-----------|-------------|---------|
| `dpi` | Mouse DPI | `800` |
| `polling_rate` | Mouse polling rate (Hz) | `1000` |
| `output_dpi` | Output DPI normalization value | `1000` |
| `lr_ratio` | Left/right output DPI ratio (`1.0` = off) | `1.0` |
| `ud_ratio` | Up/down output DPI ratio (`1.0` = off) | `1.0` |
| `yx_ratio` | Y-axis output DPI ratio, relative to X (`1.0` = off) | `1.0` |

The per-direction ratios only scale the *negative* axis direction
(`lr_ratio` scales leftward motion, `ud_ratio` scales downward motion), matching
Raw Accel's "sens multiplier" behavior; the `yx_ratio` scales all of the Y axis.

> **JSON field names:** in `settings.json` these ratios are stored as
> `lr_output_dpi_ratio`, `ud_output_dpi_ratio` and `yx_output_dpi_ratio`.
> The CLI spells them `lr_ratio` / `ud_ratio` / `yx_ratio`
> (e.g. `rawaccel-cli set-param <p> yx_ratio 1.1`).

## Multi-Mouse / Per-Device Profile Assignment

RawAccel Linux supports assigning different profiles to different mice.

### How it works

The daemon identifies each physical mouse using two methods (in order of preference):

1. **USB serial number** (`EVIOCGUNIQ` ioctl) — stable across reboots, unique per device
2. **Event node path** (`/dev/input/eventN`) — fallback when serial is empty; may change across reboots

When a mouse connects, the daemon selects its profile using this priority:

1. **Device-specific profile**: a profile whose `device_id` matches the mouse's identifier
2. **Active profile**: the currently selected profile (applies to all unmatched mice)
3. **First profile**: the first profile in the list (last resort fallback)

### Setting up per-device profiles

**Via GUI:**
1. Open RawAccel GUI
2. Create or select a profile
3. In the **Device Assignment** section, choose the target mouse from the dropdown
4. Save — the daemon will apply this profile only to that mouse

**Via CLI:**
```bash
# List connected mice with their detected device_id values
rawaccel-cli status

# Assign "office-mouse" profile to a specific mouse.
# Use the exact device_id shown by `status` (composite "usb:VVVV:PPPP:serial",
# a /dev/input/by-id/... path, or an event node).
rawaccel-cli set-param office-mouse device_id "usb:0e0f:0002:A1B2"

# Clear the assignment -> profile applies to all unmatched mice
rawaccel-cli set-param office-mouse device_id ""

# Or edit the JSON directly:
# ~/.config/rawaccel/settings.json
# Set "device_id" in the profile object
```

**Via JSON:**
```json
{
  "active_profile": "gaming",
  "profiles": [
    {
      "name": "gaming",
      "device_id": "",
      "dev_cfg": { "dpi": 1600, "polling_rate": 1000 },
      "prof": { ... }
    },
    {
      "name": "office",
      "device_id": "/dev/input/event4",
      "dev_cfg": { "dpi": 800, "polling_rate": 125 },
      "prof": { ... }
    }
  ]
}
```

### Notes

- An **empty `device_id`** means the profile applies to all mice not matched by another profile
- If multiple profiles have the same `device_id`, the first match wins
- The active profile (selected via GUI or `rawaccel-cli set`) applies to all mice with no specific assignment
- Event node paths (`/dev/input/eventN`) can change across reboots if you plug/unplug devices; USB serial numbers are more reliable when available

## Setup

### Add user to `input` group (run daemon without root)

```bash
sudo usermod -aG input $USER
# Log out and back in for the change to take effect
```

### Load the `uinput` module

```bash
sudo modprobe uinput
# Make it persistent:
echo "uinput" | sudo tee /etc/modules-load.d/rawaccel.conf
```

## How it works

1. `rawaccel-daemon` scans `/dev/input/event*` for physical mice
2. Each mouse is grabbed with `EVIOCGRAB` (raw events go only to the daemon)
3. A virtual mouse is created via `libevdev-uinput`
4. For each movement event, the daemon computes speed in ips (using DPI + polling rate)
5. The Raw Accel algorithm is applied (gain multiplier)
6. The result is written to the virtual device → seen by XOrg/Wayland
7. Config reloads are **live** (no grab release): settings update in-place without any mouse dropout

## Architecture

```
/dev/input/eventX  (physical mouse)
        │
        ▼ EVIOCGRAB
   rawaccel-daemon
        │ acceleration algorithm
        ▼
/dev/input/uinput  (virtual mouse)
        │
        ▼
   XOrg / Wayland
```

## File structure

```
rawaccel-linux/
├── include/
│   ├── math-vec2.hpp          # Vector math
│   ├── rawaccel-base.hpp      # Core types, structs, RAWACCEL_VERSION
│   ├── config.hpp             # Config structs
│   ├── accel-*.hpp            # Acceleration algorithms (classic, power, natural, jump, synchronous, lookup, noaccel, union)
│   ├── rawaccel.hpp           # Main modifier + EMA smoother engine
│   └── nlohmann/              # Vendored nlohmann/json
├── src/
│   └── config.cpp             # JSON serialization (nlohmann/json)
├── daemon/
│   ├── daemon.hpp/.cpp        # AccelDaemon: evdev/uinput + hot-plug
│   ├── main.cpp               # Daemon entry point, PID file, signal handling
│   ├── motion_math.hpp        # Speed/IPS computation
│   └── lat_stats.hpp          # Per-device latency stats
├── cli/
│   └── main.cpp               # rawaccel-cli
├── gui/
│   ├── main.cpp               # rawaccel-gui entry point
│   ├── app_state.hpp          # AppState + shared state
│   ├── tr.inl                 # Localization dictionary (tr()/trf()), language switch
│   ├── ui_builder.inl         # Layout / UI construction
│   ├── devices.inl            # Mouse discovery + hot-plug (inotify)
│   ├── daemon_comm.inl        # Daemon PID lookup, IPC, status display
│   ├── graph.inl              # Cairo curve rendering, LUT editor
│   ├── profile_mgr.inl        # Profile CRUD dialogs
│   └── widgets_sync.inl       # Widget ↔ profile sync, GTK callbacks
├── scripts/
│   ├── build.sh               # Quick build script
│   ├── install.sh             # Thin wrapper → setup.sh
│   ├── uninstall.sh           # Remove installed files
│   ├── rawaccel.service       # systemd service (boot start, hardening)
│   ├── rawaccel.desktop       # .desktop file
│   ├── rawaccel.quirks        # libinput quirk (mouse resolution)
│   ├── 99-rawaccel.rules      # udev rule (keeps /dev/uinput accessible)
│   ├── kde-fix-accel.sh       # Plasma flat-acceleration fix
│   └── virtmouse-game.c       # Live game-speed harness (P64, see Testing)
├── tests/
│   ├── test_accel.cpp         # Unit + integration tests
│   ├── tr_coverage.cpp        # Translation coverage audit (find strings)
│   ├── run_tests.sh           # Unit test runner
│   ├── run_tests_asan.sh      # Unit test runner under ASan/UBSan
│   ├── run_tr_coverage.sh     # Translation coverage runner
│   ├── run_fuzz.sh            # libFuzzer runner
│   ├── fuzz_accel.cpp         # Fuzz harness — acceleration pipeline
│   ├── fuzz_config.cpp        # Fuzz harness — config JSON parsing
│   ├── corpus_config/         # Fuzz seed corpus
│   └── oracle/                # Differential oracle + vendored RawAccel ref
├── .github/workflows/ci.yml   # GitHub Actions CI (build + tests + oracle + sanitizers + fuzz)
├── setup.sh                   # Canonical one-shot installer
└── CMakeLists.txt
```

## Testing

```bash
# All unit tests (compile + run)
bash tests/run_tests.sh

# Same tests under ASan + UBSan (catches memory/UB bugs)
bash tests/run_tests_asan.sh

# Translation coverage: every UI string must have a Turkish entry
bash tests/run_tr_coverage.sh

# Differential check: local port vs the OFFICIAL RawAccel reference (vendored)
bash tests/oracle/run_oracle.sh   # exit 0 = matches, outside known deviations

# Fuzz harnesses (libFuzzer, 60 s per harness)
bash tests/run_fuzz.sh

# Game-speed live harness (requires a running daemon and /dev/uinput access):
# injects synthetic motion via a uinput virtual mouse (grabbed by the daemon —
# the name/phys deliberately avoid the "uinput"/"(RawAccel)" markers the daemon
# filters out) and populates the per-device lat histogram at game speeds.
# Compile once, then run a scenario: flick | pan | mix
gcc -O2 -o build-manual/virtmouse-game scripts/virtmouse-game.c
sudo build-manual/virtmouse-game pan 10          # then: rawaccel-cli latency
```

Expected: `=== Sonuç: N/N geçti ===` (exits 1 on any FAIL). The translation
coverage tool exits 1 if any translatable UI string is missing from the Turkish
dictionary (see `tests/run_tr_coverage.sh`). The oracle runs automatically in
CI and must not drift outside `tests/oracle/known_deviations.txt`.

## GUI Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+S` | Save profile |
| `Ctrl+N` | New profile |
| `Ctrl+D` | Duplicate current profile |
| `Ctrl+R` | Reload daemon config |
| `F5`     | Refresh device list |

## Player profile (oyuncu profili)

> New-user onboarding: see [docs/user_guide.md](docs/user_guide.md).
> Real-hardware feel validation: see [docs/real_hardware_test.md](docs/real_hardware_test.md).

RawAccel Linux ships game-specific presets for common competitive titles. These
are tuned starting points — research-backed, then adjust by feel:

| Preset | Mode | Use case |
|--------|------|----------|
| `gaming` | classic | General FPS / aim trainers: classic curve, limit 1.8 |
| `cs2` | classic | Tactical shooter (pro eDPI band 560–1000): early kick-in, cap 1.6 |
| `valorant` | natural | Smooth entry/exit (TenZ-style base): light gain, high cap 2.0 |
| `apex` | power | Tracking + verticality: fast 180° flicks, output offset 0.9 |
| `fps` | classic | Balanced FPS starting point (moderate accel + cap 1.8) |
| `office` | natural | Light desktop acceleration (limit 1.3) |
| `precision` | classic | CAD / design work (low accel 0.002) |
| `disable` | noaccel (raw passthrough) | Raw passthrough — 1:1, no acceleration |

```bash
# Create from preset (CLI)
rawaccel-cli create-preset cs2 cs2-pro        # CS2 tactical
rawaccel-cli create-preset valorant val       # Valorant smooth
rawaccel-cli create-preset apex apex          # Apex tracking
rawaccel-cli create-preset fps fps            # generic FPS
rawaccel-cli set cs2-pro                      # activate
```

Or in the GUI: create a profile, set both axes to the preset's mode (classic /
natural / power), enable **Gain mode** and start from the value ranges above.
Use the **Ham Geçiş** (Raw Passthrough) toggle to A/B-test raw vs accelerated
until the curve *feels* right (param meanings in `## Parameters`).

Tips:
- Set `dpi` / `polling_rate` to your mouse's real values — ips math is only accurate
  when these are correct (most CS2 pros: 400 DPI, 1000 Hz; Valorant: 400–1600 DPI.
  Compare eDPI, not DPI alone).
- Valorant: start with `acceleration ≈ 0.05` (GUI slider), `limit 1.2–1.5`,
  `input_offset 0.02–0.05` so micro-corrections stay 1:1.
- CS2: keep `exponent_classic` at 2.0 and test `limit` between 1.4–1.8; raise
  `input_offset` if slow aim feels floaty.
- Lower `exponent_classic` → subtler curve; higher `limit` → more speed at high motion.
- Per-game presets are meant as a *reference* — spend 30–60 min per config before judging

## Performance

> Deep-dive on the syscall model, polling/DPI/smoothing effect tables, and the
> native-vs-portable build choice: [docs/performance_tuning.md](docs/performance_tuning.md).

RawAccel Linux processes mouse events with sub-microsecond latency in the daemon's hot path:

- **Processing latency** (modifier math + uinput write): typically **< 5 µs** per event
- **Event loop**: epoll-based 10 ms timeout — that timeout is for *housekeeping only*
  (hot-plug, IPC, signals); motion events are processed synchronously as they arrive
- **Algorithm overhead**: all algorithms are header-only, compiler-inlined
- **Subpixel accumulation**: no micro-movements are silently lost
- **Hot path contract**: single loop thread, no per-event allocations; `dpi_factor`
  pre-computed per device; single `CLOCK_MONOTONIC_RAW` source → 2 syscalls per event
  (clock reads only; the batched `REL_X`+`REL_Y` write adds one more — 3 hot-path
  syscalls per motion event)
- **Grab safety**: `EVIOCGRAB` takes raw input; live config reload never releases the
  grab → no dropout window; `SYN_DROPPED` discards events until the next `SYN_REPORT`
  so nothing partially-read reaches uinput; uinput write failure marks the device
  disconnected instead of wedging the grab

### Latency statistics

Per-device histogram (µs) snapshot via `rawaccel-cli latency` (tries the daemon's
IPC `latency` RPC first — works for any user in the `input` group; falls back to
`SIGUSR1` only if the running daemon doesn't speak that command yet):

```bash
rawaccel-cli latency
```

Measured on this machine (n=2454, synthetic input, Aj 1 live data — the basis of the
P-round queue analysis):

| Metric | Value |
|--------|-------|
| Min    | 26 µs |
| Avg    | 45 µs |
| p50    | 33 µs |
| p95    | 86 µs |
| p99    | 266 µs |
| Max    | 2020 µs (14 samples > 500 µs) |

p50/avg run at ~1/3 of a 125 µs (8 kHz) frame budget — interactive feel is fine; the
rare **p99/max queue spikes** felt behind "flicks" are under investigation (P-round).

### Safe defaults

Everything ships safe out of the box:
- Default profile: **noaccel** — acceleration-free (RAW 1:1 is NOT the default; the literal 1:1 bypass is the `disable` preset, `raw_passthrough=true`), output normalized to 1000 DPI, speed processor on
- Every loaded profile passes range validation + NaN/Inf sanitisation — no bad value
  and no overflow escapes the pipeline
- Config writes are atomic (tmp + rename + fsync) — the daemon never reads a half-written file
- systemd unit is hardened; GUI/CLI talk to the daemon over a dedicated Unix socket

## KDE Plasma Setup

KDE applies its own **libinput acceleration curve** on top of raw mouse input.
When RawAccel is active, this causes **double-acceleration** — KDE's curve runs
before RawAccel, compounding both effects.  You must set KDE's pointer
acceleration to **Flat** (disabled) for RawAccel to be the sole accelerator.

### Automatic fix (recommended)

```bash
# One-time fix — no logout required
bash scripts/kde-fix-accel.sh

# Check current state
bash scripts/kde-fix-accel.sh --check

# Undo (restore KDE adaptive acceleration)
bash scripts/kde-fix-accel.sh --undo
```

The fix script:
1. Sets `PointerAccelerationProfile=1` (Flat) in `~/.config/kwinrc`
2. Reloads KWin input settings immediately via D-Bus (`qdbus org.kde.KWin /KWin reconfigure`)

### Manual fix

**KDE System Settings:**
1. Open **System Settings → Input Devices → Mouse**
2. Set **Pointer Acceleration** to **Flat** (the leftmost preset)
3. Click **Apply**

**Or via CLI (kwriteconfig5 / kwriteconfig6):**
```bash
# Plasma 5
kwriteconfig5 --file kwinrc --group Libinput --key PointerAccelerationProfile 1
kwriteconfig5 --file kwinrc --group Libinput --key PointerAcceleration 0
qdbus org.kde.KWin /KWin reconfigure

# Plasma 6
kwriteconfig6 --file kwinrc --group Libinput --key PointerAccelerationProfile 1
kwriteconfig6 --file kwinrc --group Libinput --key PointerAcceleration 0
qdbus6 org.kde.KWin /KWin reconfigure
```

### New mouse after the fix (per-device overrides)

KDE stores libinput settings per device (nested `[Libinput][bus][vendor][product][Name]`
sections) **in addition to** the global `[Libinput]` section.  `kde-fix-accel.sh` writes a
per-device **Flat** override for every RawAccel virtual mouse present at the time it runs.

What this means in practice:

- A mouse attached **after** the fix is still covered by the **global** Flat section
  (libinput falls back to it when no per-device override exists) — so double-acceleration
  stays off automatically in the default configuration.
- Exception: if you set a per-device acceleration for a new mouse in **System Settings →
  Input Devices → Mouse**, KDE writes an override for that device that beats the global
  Flat — on that device RawAccel's curve would be double-applied again.

Manual step when you add a new mouse (both cases above resolve it):

```bash
# Idempotent — picks up any newly attached RawAccel virtual device
# and re-writes its per-device Flat override:
bash scripts/kde-fix-accel.sh
```

No other action is required after a normal plug/unplug; only re-run this when the fix's
per-device coverage should include the newest hardware.

### GUI warning

The **RawAccel GUI** automatically detects KDE sessions and checks whether
libinput acceleration is disabled.  If it detects double-acceleration is likely,
an orange warning banner appears with a **Fix Now** button that applies the fix
without requiring any manual steps.

### KDE + Wayland

On **KDE Wayland** sessions the systemd service starts after `plasma-kwin_wayland.service`
(soft dependency via `Wants=`/`After=`) to ensure the virtual uinput device created
by the daemon is visible to the compositor on first boot.

## Wayland / X11 Compatibility

RawAccel Linux works at the kernel input layer (evdev + uinput), **below** the display server:

- ✅ **X11**: fully compatible — works with any X11 compositor/WM
- ✅ **Wayland**: fully compatible — Wayland compositors receive already-accelerated events from uinput
- ✅ **Display-server agnostic**: no compositor or display server patches required
- ⚠️ **libinput note**: some Wayland compositors apply their own acceleration on top of rawaccel output. Disable compositor acceleration in your DE settings.
  - **KDE**: see [KDE Plasma Setup](#kde-plasma-setup) above
  - **GNOME**: System Settings → Mouse & Touchpad → disable "Mouse Acceleration"

## Known Issues

- **Device ID instability**: on kernels without by-id udev rules, `eventN` numbers can change across reboots. RawAccel GUI resolves `/dev/input/by-id/...` stable paths automatically — but only if the device has a unique USB ID.
- **Multi-DPI sensors**: some mice report different DPI values than configured. Always verify DPI with a measurement tool.
- **Large LUT tables**: LUT mode is capped at 257 points (514 floats). Larger tables are silently truncated.

## Troubleshooting

**Daemon won't start / no mice found:**
- Add yourself to the `input` group: `sudo usermod -aG input $USER`, then re-login
- Load uinput: `sudo modprobe uinput`
- Stop conflicting software: `sudo systemctl stop abrek`

**Mouse is grabbed but no output:**
- Check `rawaccel-daemon -v` for virtual device creation errors
- Make sure `uinput` module is loaded

**Settings not applying:**
- Run `rawaccel-cli status` to check if daemon is running and see profile details
- Run `rawaccel-cli reload` to trigger a config reload
- Check `~/.config/rawaccel/settings.json` exists and is valid JSON

**Wayland compositor applies double acceleration (KDE):**
- Run `bash scripts/kde-fix-accel.sh` (automatic one-step fix)
- Or: System Settings → Input Devices → Mouse → Pointer Acceleration = **Flat**
- The GUI will show an orange warning banner with a **Fix Now** button

**Wayland compositor applies double acceleration (GNOME):**
- System Settings → Mouse & Touchpad → disable "Mouse Acceleration"

**Config file location:**
- Default: `~/.config/rawaccel/settings.json`
- When run with `sudo`: resolves to the real user's home via `$SUDO_USER`
- Override: `rawaccel-daemon -c /path/to/settings.json` (or `rawaccel-daemon --config=/path/to/settings.json`)
