# RawAccel Linux 0.6.4 — Security & Bug Audit

Audit date: 2026-09-10. Codebase: rawaccel-linux 0.6.4 (C++20, GTK4 GUI + systemd daemon + CLI). Every source, header, script, packaging and config file was read line-by-line; the remaining files (`gui/ui_builder.inl`, `gui/graph.inl`, `gui/mouse_test.inl`, `gui/tr.inl`, `tests/test_accel.cpp` tail) were pattern-audited for risky API usage.

Severity scale: CRITICAL / HIGH / MEDIUM / LOW / INFO. No CRITICAL or HIGH findings. Two MEDIUM robustness/availability issues, two LOW privilege-design notes, rest are confirmed-safe items.

---

## 1. Threat model (what the attack surface is)

- Touch model is **multi-user with a shared input group**, not untrusted remote.
- Attackers of interest: any local, non-root user who is **not** in the `input` group (must read the IPC socket / config / `/dev/input`), and `input`-group members (can write the IPC socket + `/dev/input/*`), plus a **malicious USB HID++ device** (can stall protocol loops).
- Privileged executables: `rawaccel-daemon` (systemd, root, hardened), `setup.sh` / `scripts/*.sh` (root, run interactively by an admin), `pkexec` starts.
- Assets: root daemon integrity/availability, users' config files, kernel `uinput`/`hidraw`/`evdev` access, the X11 pointer.

---

## 2. Findings

### F‑1 (MEDIUM) HID++ identify loops have no overall timeout — a single hung device stalls the whole HID++ worker for minutes
File: `src/logitech_hidpp.cpp`.

- `get_feature_metadata()` enumerates FEATURE_SET: `for (uint16_t i = 1; i < count; ++i)` (line 1014), `count = count_response->params[0] + 1` → **up to 256 iterations**, each `send_feature_request(..., 900 ms)` (lines 1003, 1022). Worst case ≈ **230 s**.
- `get_device_info()` firmware records: `for (uint8_t index = 0; index < (*count)[0]; ++index)` (~line 1175), `count[0]` is a device-supplied byte → **≤ 255 iterations × 700 ms ≈ 178 s**.
- `get_dpi_info()` GetDpiList: `for (uint16_t chunk = 0; chunk < 256; ++chunk)` (~line 1418) × 700 ms ≈ **179 s**.
- Name/friendly-name loops are bounded to ≤ 16 chunks × 500 ms (~8 s) — tolerable.
- These all run sequentially inside `identify_logitech_device()` (line 1742), which the **daemon's single `run_hidpp_worker` thread** (and every CLI `hidpp` / GUI scan) calls. The worker has no overall deadline and no iteration cap beyond the USB-side per-request timeout.

Impact: a device that answers slowly/partially (or a cheap wireless receiver that never replies to one function) blocks battery, link, and notification handling **for every device** for minutes. Actual mouse motion is unaffected (separate event loop thread), but the daemon's monitoring (60 s battery cadence, status updates, re-identify on notification) freezes, and `rawaccel-cli hidpp` / the GUI HID++ panel appear hung.

Suggested fix: overall identify budget (e.g. 20 s) + early-exit on N consecutive timeouts + a "device busy — postpone" fast-path (query first battery feature only). Not exploitable for memory corruption (bounded buffers), purely availability/robustness.

### F‑2 (LOW) Passwordless `input`-group daemon-start polkit action is installed but unused — latent privilege path
Files: `scripts/polkit/org.rawaccel.policy`, `scripts/polkit/49-rawaccel.rules`.

- The policy declares `org.rawaccel.daemon.start` with `allow_active=auth_admin_keep`; the rule grants active, local `input`-group users `YES` (no password) for that action, annotated to `/usr/bin/rawaccel-daemon`.
- **No in-tree launcher uses `pkexec --action org.rawaccel.daemon.start`** (verified by grep across the whole tree). The GUI starts the daemon with plain `pkexec rawaccel-daemon -v -c <path>` (gui/widgets_sync.inl:559-573) → `org.freedesktop.policykit.exec`, which hits system default polkit policy (`auth_admin` = password prompt); the `.desktop` file only launches the GUI.
- If any future or external code does invoke the action, an `input`-group user could start a second root daemon with `-c <any existing regular ≤4 MB .json>`. `validate_path()` (daemon/main.cpp) rejects only `/proc/`, `/sys/`, `/dev/` prefixes **and only when the target file does not exist**; an existing arbitrary `.json` (any location with a writable-regular-file check) is accepted. That root daemon then honors `set_config` IPC writes to that path (config save is root-executed), and grabs the mouse — instance confusion / config-file write at a user-chosen path.

Practical exposure today: an admin still has to type a password for `pkexec`, so impact is limited to (a) dormant dangerous policy installed on hosts, (b) defense-in-depth concern. Recommend either wiring the GUI to `pkexec --action org.rawaccel.daemon.start /usr/bin/rawaccel-daemon ...` and locking `-c` to `/etc/rawaccel/settings.json`, or dropping the policy + rule.

Related informational context: udev rules give `input` group `0660` rw on `/dev/input/event*`, `/dev/hidraw*`, and **`/dev/uinput`**. Members of `input` can therefore (by design) create arbitrary virtual input devices (keyboard injection) and read other users' keystrokes from `/dev/input/event*`. The installer (`setup.sh`) adds the user to `input` automatically (setup.sh:268-277). This is the documented trust boundary of the project; flag for anyone adding non-admin users to `input`.

### F‑3 (LOW) CLI IPC response is truncated at 64 KiB; daemon treats EINTR as disconnect
- `cli/main.cpp` `daemon_ipc_send()` caps the read at 65536 bytes; a `status` response with many devices + LUTs truncates and then fails JSON parse (handled gracefully, but `status --json` degrades). (GUI caps at 4 MiB, gui/daemon_comm.inl.)
- `daemon/daemon.cpp` `handle_ipc_client()`: a `recv()` interrupted by `EINTR` aborts the frame → the peer receives an explicit error, so no corruption, but a signal-happy client gets rejected. Cosmetic robustness item.

### F‑4 (INFO) Config reload with no matching live device does a full re-grab
`daemon/daemon.cpp` `apply_new_config()`: when no currently-open device matches any profile (`!any_live`), it performs `teardown_devices()` + `setup_devices()` — closing and recreating the virtual `uinput` device (~100-150 ms). Every SIGHUP/`set_config` reload that lands while all profiles are `disable`d or device-mismatched causes a visible "mouse drop" for the user. Acceptable, but worth a fast-path that only re-applies when the live set is actually changing.

### F‑5 (INFO) Audited-but-unchanged behaviors worth documenting
- `use_raw_input` top-level flag in the config is **stored but dormant**: the hot path ignores it; real 1:1 raw is per-profile `raw_passthrough`. The GUI/CLI already label it as stored-only (cli/main.cpp `status`). Not a bug — parity gap with the Windows original's global toggle.
- An `input`-group member can push an arbitrary (sanitized) config to the running daemon via IPC `set_config` and save it to `/etc/rawaccel/settings.json` (live re-config of system-wide acceleration). All values are validated/sanitized (finite checks, LUT caps, name/device-id 256-char caps), so no injection; it is a by-design capability of the trusted group.
- `latency` IPC command / `SIGUSR1` dump is available to any IPC caller (the group). Mirrors upstream signal semantics.

---

## 3. Verified safe — no action (checklist)

**Memory / integer safety**
- All event buffers fixed-size; `read()` of exactly 24-byte `input_event`, short reads skipped (daemon.cpp `process_device`).
- `snprintf` with bounded buffers throughout (`device_id` composition, sysfs paths); `realpath(...PATH_MAX)` everywhere symlinks are resolved.
- `nlohmann::json` used everywhere; parse wrapped in try/catch; config load/import can never throw uncaught.
- IPC command read is 1 byte at a time, max 256; `set_config` body capped at 1 MiB, whole-request deadline 10 s, body deadline 5 s; response frames newline-terminated; GUI treats mid-line timeout as incomplete and discards (no partial-JSON parse).
- Integer ranges: `read_be16`, explicit `uint16→uint32` guards in DPI step expansion (logitech_hidpp.cpp `get_dpi_info`); `strtoul`/`strtol`/`strtod` with `errno`+end-pointer range checks everywhere a user/kwinrc string is parsed (BUG-6/16 fixes); `atoi`/`atof`/`fscanf("%d")`/`scanf` eliminated from all paths (only `sscanf` on /proc line in ui_builder.inl, 3 fields, host-provided).
- PID parsing: `strtol` + range check, `/proc/<pid>/comm` must equal `rawaccel-daemon` before signaling (guards PID recycling, gui/daemon_comm.inl).

**Acceleration math edge cases** (all modes, incl. gain↔velocity forms)
- `classic`: exponent clamped [1,10]; `1 + gain*...` can't go negative below cap 1.5 default.
- `power`: exponent floored at `max(ep, 1e-3)`; all `pow`/inverse results guarded `isfinite` → fall back to identity/plateau (BUG-NEW-11/16); io-cap degenerate (`cap.x <= 0` or gain_mode & `cap.x <= 0`) → `DBL_MAX` caps (no cap).
- `natural`: `Inf` decay → gain clamped to 0; decay/exp underflow → 0; no NaN (BUG-NEW-14/16).
- `jump`: GAIN antiderivative evaluated from stable saturation terms `log1p(exp(-|y|))`; denormal `x` overflow caught by `isfinite` → returns 1.0 (BUG-NEW-17). Literally no overflow/NaN path.
- `synchronous` GAIN: LUT sized `(stop-start)*num+1 = 97 ≤ capacity 514`; index clamped to `[0, size-2]` and `idx < capacity-1` gated before `data[idx+1]`; zero value → fallback 1.0.
- `lookup`: binary search over float pairs; zero-width segment → deterministic "later point" (no Inf lerp); velocity divide guarded by `x0 <= 0 → 0.0`; odd length → floor pairs only.
- `motion_math`: subpixel accumulation clamped; rotation/snap/speed-clamps all bounded; no NaN propagation (fuzz harnesses `fuzz_accel` + UBSan pin this).
- Config sanitization (src/config.cpp `sanitize_accel_args`): all fields `finite_or(default)`; LUT raw length capped at `LUT_RAW_DATA_CAPACITY` before sort; `sort_lut_data` never reads out of bounds after the length clamp.

**Concurrency**
- `devices_` mutated only on the event-loop thread; T8 guard prevents redundant re-grabs; IPC/config threads read under `devices_mutex_` keyed on `gen` snapshot; new-live profile map swapped out atomically.
- `lat_stats::copy()` locks `lat.mtx` outside `devices_mutex_` (no deadlock); seqlock telemetry (writer: volatile bump + relaxed stores + even bump; reader: ≤8 attempts, odd→retry, s1==s2→snapshot).
- `ipc_serve_loop` uses `poll()` (never `select`/`FD_SETSIZE`), `accept4` with `SOCK_CLOEXEC`, local-fd copy before accept (no TOCTOU on shutdown), worker joined before close.
- HID++ transport `feature_mutex_` + `device_index_` atomic; workers marshal results back to the GTK main thread only via `g_idle_add` with `hw_cancel`/`hw_busy` guards; widget access is main-thread only.
- GUI: single `AppState` per process; `flock` single-instance guard across users (main.cpp BUG-01).

**File / privsep discipline**
- Config save: O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC temp, `fsync(fd)`, `rename()`, parent-directory `fsync()`, hardlink timestamped backup, never `copy_file` over a live config (src/config.cpp). Export/language files use the same discipline (gui/profile_mgr.inl `write_text_file`, ui_builder/tr.inl).
- IPC socket: stale-socket connect probe before bind; `bind`+`chown root:input`+`chmod 0660` (fallback 0600 if no input group) (daemon.cpp 1814-1830); `listen(8)`; `unlink` on teardown; `UMask=0077` in the unit.
- DAEMON systemd unit hardening (scripts/rawaccel.service): `PrivateTmp`, `ProtectSystem=strict`, `ProtectHome`, `ProtectKernel*`, `ProtectProc=invisible ProcSubset=pid`, `RestrictNamespaces`, `RestrictRealtime`, `RestrictSUIDSGID`, `LockPersonality`, `NoNewPrivileges`, `MemoryDenyWriteExecute`, `SystemCallArchitectures=native`, **`PrivateNetwork` + `RestrictAddressFamilies=AF_UNIX` + `IPAddressDeny=any`**, `CapabilityBoundingSet=CAP_CHOWN CAP_DAC_READ_SEARCH CAP_DAC_OVERRIDE CAP_FOWNER`, `AmbientCapabilities=` empty, `SupplementaryGroups=input`, `ReadWritePaths=/etc/rawaccel /run`.
- Build hardening in both paths: `-fstack-protector-strong`, `-fstack-clash-protection`, `-fcf-protection=full` (x86), `-D_FORTIFY_SOURCE=2` (unless already ≥3), `-D_GLIBCXX_ASSERTIONS`, `-fPIE -pie -Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code`, `-Wformat-security`. Applies to all three binaries (CMake `add_compile_options` + per-target in build.sh).
- Installer/scripts: `set -euo pipefail`; pkill with `-x` exact-name match; config copy from `~/.config` to `/etc/rawaccel` skips **symlink** user configs and backs up before overwrite; uninstall preserves `/etc/rawaccel` and `~/.config/rawaccel`; KDE trace removal runs as the real user; polkit/udev installed with 644.

**CLI/GUI behavior**
- Full arity checks for every subcommand + `-c ""` rejection (P99); `set-param` out-of-domain values are rejected (rc=1, config byte-identical) not silently clamped (P107); duplicate/empty profile names rejected; 256/257-char name gates under test; `import` rejects oversized LUT (>514 raw) instead of truncating.
- GUI: KDE double-accel detection (kwinrc parse w/ safe `strtol`/`strtod`), kwinrc fix writes + qdbus reconfigure run as the user, `systemsettings` spawn uses constants (no shell injection); X11 pointer-lock uses dlopen'd `libX11` talk-to-window only, released on focus loss/ESC.
- Tests: custom assertion harness (`tests/test_accel.cpp`, ~8.8k lines), fuzz harnesses (`fuzz_accel`, `fuzz_config`) with ASan/UBSan, hot-path benchmark, differential oracle for reference parity, ASan test runner (`run_tests_asan.sh`), tr-coverage scanner.

**Misc accepted**
- `PKGBUILD` marks `/etc/rawaccel/settings.json` as `backup=` (never overwritten by pacman); desktop/quirk/modprobe/udev/polkit files installed 644; quirks file is a dedicated `50-rawaccel.quirks` (local overrides never touched).
- udev rule uses `TAG+="uaccess"` + `MODE=0660 GROUP=input` (standard; noted in F‑2 informational).

---

## 4. Coverage summary

Read completely (line-by-line): `include/*.hpp` (all 15, incl. `accel-*.hpp` algorithms, `logitech_*`, `presets`, `rawaccel*`, `config`, `math-vec2`, `accel-union`), `src/config.cpp`, `src/logitech_receiver.cpp`, `src/logitech_hidpp.cpp`, `daemon/daemon.cpp`, `daemon/main.cpp`, `daemon/lat_stats.hpp`, `daemon/motion_math.hpp`, `cli/main.cpp`, `gui/main.cpp`, `gui/app_state.hpp`, `gui/daemon_comm.inl`, `gui/devices.inl`, `gui/profile_mgr.inl`, `gui/hidpp_panel.inl`, `gui/widgets_sync.inl` (pkexec/daemon-control parts), `setup.sh`, all of `scripts/*` (rules, service, polkit, desktop, quirks, install/uninstall/kde-fix/build/bench), `packaging/PKGBUILD`, `CMakeLists.txt`, `config/default.json`, `tests/*` (harnesses, run_tests.sh, fuzz drivers).

Pattern-audited (risky-call grep + targeted reads): `gui/ui_builder.inl`, `gui/graph.inl`, `gui/mouse_test.inl`, `gui/tr.inl`, `tests/test_accel.cpp` tail, `scripts/bench_hotpath.sh`, `scripts/virtmouse-game.c`.

## 5. Bottom line

- **No CRITICAL/HIGH.** The hard parts (memory safety, integer bounds, concurrency, IPC layout, config atomics, math edges) hold up: sanitizers+fuzzing cover the parsing and math surfaces, the daemon is heavily sandboxed by systemd, and IPC is bounded and validated.
- **Two MEDIUMs** to fix first: F‑1 (HID++ identify loop-stall) and F‑2 (dormant passwordless polkit action) — both robustness/availability or defense-in-depth, neither leads to code execution.
- **LOWs** F‑3/F‑4 are hygiene. Everything else is confirmed working as designed, with the trust boundary documented: **`input`-group membership == full hardware access + system-wide acceleration control**.