# RawAccel Linux — Developer Notes

This file documents build, test, and verification commands.

## Install (canonical one-shot setup)

The **single source of truth for installation is `setup.sh`** at the repo root.
It installs ALL system dependencies, cleans any previous install, builds,
installs binaries/systemd/udev/polkit/desktop/libinput-quirk, enables the
service, and applies the KDE Plasma flat-acceleration fix.

```bash
sudo bash setup.sh             # full install (deps + build + system-wide + KDE fix)
sudo bash setup.sh --no-deps   # skip system dependency installation
sudo bash setup.sh --uninstall # fully remove (keeps ~/.config/rawaccel)
sudo bash setup.sh --reinstall # clean the old install, then reinstall (default)
```

`scripts/install.sh` is a thin wrapper that forwards to `setup.sh`
(kept for backwards compatibility — do not add install logic there).

### Dependency policy (IMPORTANT — do not cause install errors again)

- Every dependency the project compiles or runs against MUST be present in
  `setup.sh` → `install_deps()` for **each** of the three distro branches
  (pacman / apt / dnf). Missing entries = "dependency error" for the user.
- Current tool chain: `g++/clang++`, `make`, `cmake`, `pkg-config`/`pkgconf`.
- Current libraries: `libevdev` (build+runtime), `gtk4` (GUI build+runtime).
- Runtime/aux: `systemd`, `polkit`, `python3` (kwinrc KDE fix), `qt6-tools`
  (provides `qdbus6` — live KWin reconfigure on Plasma 6).
- `nlohmann/json.hpp` is vendored (`include/nlohmann/`) — do NOT add an
  external nlohmann-json dependency.
- After the install, `install_deps()` verifies every tool/library via
  `command -v` / `pkg-config --exists` and aborts with an exact hint if
  anything is still missing, so the user never sees a cryptic build error.
- If you introduce a NEW build or runtime dependency: add its package to all
  three distro branches in `setup.sh` AND update this section.

## Build

```bash
# Standard build (-march=native, fastest)
bash scripts/build.sh

# Portable build (works on other CPU architectures)
RAWACCEL_PORTABLE=1 bash scripts/build.sh

# Custom compiler
CXX=clang++ bash scripts/build.sh
```

Output binaries: `build-manual/rawaccel-daemon`, `build-manual/rawaccel-cli`, `build-manual/rawaccel-gui`

Both `scripts/build.sh` and the CMake target apply the same hardening flags
(`-fstack-protector-strong`, `-fstack-clash-protection`, `-D_FORTIFY_SOURCE=2`,
`-D_GLIBCXX_ASSERTIONS`, `-fPIE`+`-pie`, `-Wl,-z relro,now,noexecstack,separate-code`,
`-fcf-protection=full` on x86). `RAWACCEL_PORTABLE=1` turns off `-march=native`.

## Test

```bash
# All unit tests (compile + run)
bash tests/run_tests.sh

# Same tests under AddressSanitizer + UBSan (slower, catches memory/UB bugs)
bash tests/run_tests_asan.sh

# End-to-end: REAL daemon against synthetic uinput source/sink (needs root +
# /dev/uinput). SIGSTOPs a running system daemon for the duration and SIGCONTs
# it on exit (trap), so it cannot steal the synthetic source's grab.
# accel phase: frame/SYN structure, SM-2 button buffering, LOW-1 coalesced
# deferral, ×3 classic-linear gain. raw phase: byte-identical 1:1 passthrough.
sudo bash tests/run_e2e.sh          # exit 0 = pass, 1 = failed check, 77 = env unusable

# Translation coverage: every translatable UI string must have a Turkish entry
bash tests/run_tr_coverage.sh

# Expected output: "=== Sonuç: N/N geçti ===" (N/N passed)
# Exits with code 1 if any FAIL line appears.
```

## Oracle (reference cross-check)

```bash
# Differential check: local port vs the OFFICIAL RawAccel reference (vendored)
bash tests/oracle/run_oracle.sh            # exit 0 = matches, outside known deviations
bash tests/oracle/run_oracle.sh --verbose # print every mismatching row
TOL=1e-9 bash tests/oracle/run_oracle.sh  # tighten/loosen relative gain tolerance
```

The oracle builds the project's own acceleration headers AND verbatim vendored
`RawAccelOfficial/rawaccel` headers (MIT, `tests/oracle/ref/LICENSE`) over a
shared parameter grid (`tests/oracle/oracle_cases.hpp`) and compares every
gain row. Rows that intentionally deviate (classic exponent<=1 "linear path"
constant gain, `power`/`synchronous` identity at speed 0, and the power
`io` cap.y=0 identity guard — ref yields NaN/0 for that degenerate input,
P155) are listed in `tests/oracle/known_deviations.txt` and do not fail the
run. Current grid: **1071 rows compared, 68 documented deviations** (R3-NEW-2
added `power_tinyexp_floor` with exponent_power=5e-4 inside the BUG-02 floor
band — the local port evaluates a shared exponent floored at 1e-3, the
reference the raw 5e-4, so 23 of its 24 rows drift; spd=1 is force-checked
because both sides reduce to 1^n with scale=1 exactly).
The `io`-gain cap.y=0 rows are intentionally NOT listed: the reference emits
NaN there and `NaN > tol` is never true, so they can never appear as a
deviation — listing them would raise a stale-row error. Run this after
EVERY change to `include/accel-*.hpp`.

## Translation Coverage

`tests/tr_coverage.cpp` + `tests/run_tr_coverage.sh` statically scans the GUI
sources (`gui/*.inl`, `gui/main.cpp`) for every translatable call —
tr / trf / trlbl / trmlbl / trbtn / trchk / trtip / tr_combo_fill /
grid_row / grid_row2 — collects the keys and cross-checks them against the
dictionary in `gui/tr.inl`.

- **Key position aware:** `trtip(widget, key)` reads the key from the **2nd**
  argument, `grid_row`/`grid_row2` from the **3rd** label argument.
- Exits **1** on any MISSING key (a UI string without a Turkish entry);
  orphaned dictionary entries are warnings (some are used dynamically).
- `TRC_DEBUG=1` enables verbose scanning diagnostics on stderr.
- Run it after EVERY change to GUI strings. Expected output — all MISSING/key
  summary lines then `Result: PASS` (exit code 0).

Test file: `tests/test_accel.cpp`
- No external dependencies (standard C++20 + project headers)
- Each `SECTION()` is an independent test group
- Assertions use `EXPECT` / `EXPECT_NEAR` macros
- 184 test groups, 33764 runtime assertions covering: algorithms, JSON round-trips,
  file I/O, input validation, multi-profile round-trip, atomic write, IPC JSON,
  config error paths, LUT sort, int overflow guard, NaN/Inf remainder guard,
  accel_args sanitize, fuzz tests, extreme speeds, EMA stability, subpixel
  accumulation, modifier flags, stress tests, DPI ratio div-by-zero guard,
  lp_distance zero-vector guard, EMA extreme halflife, NaN pipeline injection,
  classic io degenerate cap, power output offset, directional weight boundary,
  1M-iteration subpixel drift, classic monotonicity, natural gain formula,
  EMA smoother half-life/convergence, linear EMA smoother, NaN propagation all
  modes, pathological params, event batching accumulation/split, speed processor
  distance modes + smoothing, SYN_DROPPED event-sequence machine
  (clean flush / drop-sustained / clear / button discard / leak-free), config empty profiles / missing
  active profile / extreme values / duplicate device IDs,
  sanitize NaN/Inf in all fields, subnormal time guard,
  classic sign flip (io cap.y < 1), classic linear path (exp<=1) cap,
  power cap branch (all 3 cap modes), modifier rotation + snap combined,
  speed_processor Lp/max/separate modes, lookup LUT max capacity,
  EMA coefficient=0 path, directional weight cos/sin blend,
  speed clamp min/max, dir mul negative direction, synchronous power<1 guard,
  natural legacy (non-gain) mode, output_dpi NaN sanitize,
  lat_stats move semantics, dpi_factor pre-compute consistency,
  magnitude hypot overflow safety, lookup LUT length clamp to capacity,
  atomic config save (pid-suffix + O_NOFOLLOW/O_EXCL), config type/boolean
  guards + 256-char name/device_id caps, version-stamped config migration
  runs exactly once, lookup zero-width segment denominator guard,
  lat_stats non-finite/negative-sample guard

## Fuzz Testing

```bash
# Requires clang++ with libFuzzer support
bash tests/run_fuzz.sh          # 60 seconds per harness (default)
bash tests/run_fuzz.sh 300      # 300 seconds per harness
```

Two harnesses:
- `tests/fuzz_config.cpp` — JSON parser + sanitize (config round-trip)
- `tests/fuzz_accel.cpp` — acceleration pipeline (modifier + motion_math)

Seed corpus: `tests/corpus_config/`

## Continuous Integration

GitHub Actions workflow: `.github/workflows/ci.yml`

Three jobs run on every push/PR (Ubuntu 24.04):
- **build-and-test** — portable build (`RAWACCEL_PORTABLE=1`), warning-as-failure gate
  via `grep -E "warning:|error:"`, then `tests/run_tests.sh`, then the differential
  oracle (`bash tests/oracle/run_oracle.sh`) which fails if any gain row drifts
  outside `tests/oracle/known_deviations.txt`.
- **sanitizers** — rebuilds tests with `-fsanitize=address,undefined` and runs them
  with `halt_on_error=1` so any leak/UB fails CI.
- **fuzz-smoke** — 60 s per harness via `tests/run_fuzz.sh 60`. Skipped on PRs to
  keep them fast; runs on push to main and on `workflow_dispatch`. Crash inputs
  are uploaded as artifacts on failure.

Concurrency group cancels superseded runs on the same ref.

## Lint / Warning Check

```bash
# Build with -Wall -Wextra (should produce 0 warnings)
bash scripts/build.sh 2>&1 | grep -E "warning:|error:"
```

## Version Update

Version number lives in `include/rawaccel-base.hpp` → `RAWACCEL_VERSION` (propagates to
daemon, CLI, and GUI at build time) and must be mirrored in `CMakeLists.txt` →
`project(rawaccel-linux VERSION ...)`. Bump both together.

## File Responsibilities

| File/Directory | Contents |
|----------------|----------|
| `include/accel-*.hpp` | Acceleration algorithms (header-only) |
| `include/rawaccel.hpp` | Modifier + EMA smoother engine |
| `include/rawaccel-base.hpp` | Core types, structs, RAWACCEL_VERSION |
| `include/config.hpp` | Config structs |
| `include/presets.hpp` | Built-in game/FPS presets — single source shared by CLI `create-preset` and GUI "New Profile" preset dropdown |
| `src/config.cpp` | JSON serialization (nlohmann/json) |
| `daemon/daemon.cpp` | evdev/uinput implementation, hot-plug |
| `daemon/main.cpp` | Daemon entry point, PID file, signal handling |
| `cli/main.cpp` | rawaccel-cli commands |
| `gui/main.cpp` | rawaccel-gui entry point + helpers (~150 lines) |
| `gui/app_state.hpp` | AppState struct, shared includes, forward declarations |
| `gui/tr.inl` | Lightweight localization (tr()/trf() dict, TR_EN/TR), language switch, runtime registry |
| `gui/devices.inl` | Mouse discovery (stable by-id paths), inotify hot-plug |
| `gui/daemon_comm.inl` | Daemon PID lookup, status display, signal sending |
| `gui/graph.inl` | Cairo curve rendering, LUT editor |
| `gui/widgets_sync.inl` | Widget ↔ profile sync, GTK callbacks |
| `gui/profile_mgr.inl` | Profile CRUD dialogs |
| `gui/ui_builder.inl` | Layout helpers, build_ui(), window-close, on_activate() |
| `tests/test_accel.cpp` | Unit + integration tests (33764 assertions, 184 groups) |
| `tests/fuzz_config.cpp` | libFuzzer harness — config JSON parsing |
| `tests/fuzz_accel.cpp` | libFuzzer harness — acceleration pipeline |
| `tests/run_fuzz.sh` | Fuzz test runner (both harnesses) |
| `tests/run_tests.sh` | Unit test runner (compile + run) |
| `tests/e2e_harness.cpp` | E2E harness: synthetic uinput mouse + REAL daemon + virtual sink; accel (T-A1..T-A4) + raw (T-B1) phase checks |
| `tests/run_e2e.sh` | E2E runner (root): builds harness, SIGSTOP/SIGCONT-sensitive system-daemon isolation, runs both phases, propagates 0/1/77 |
| `tests/run_tests_asan.sh` | Unit test runner under ASan + UBSan |
| `tests/oracle/` | Differential oracle: `run_oracle.sh`, grid `oracle_cases.hpp`, local side `local.cpp`, official-ref side `reference.cpp`, `ref/` (vendored MIT), `known_deviations.txt` |
| `tests/tr_coverage.cpp` | Translation coverage audit (extracts all tr*()/grid_row keys) |
| `tests/run_tr_coverage.sh` | Translation coverage runner (exit 1 on MISSING) |
| `scripts/build.sh` | Quick build script |
| `setup.sh` | Canonical one-shot installer (all deps + build + system install + KDE fix) |
| `.github/workflows/ci.yml` | GitHub Actions CI (build + tests + oracle + sanitizers + fuzz smoke) |

## Live Telemetry & Seqlock

Per-device last-motion telemetry is published by the daemon in the `status` JSON:
`telem_in_ips`, `telem_out_ips`, `telem_gain`, `telem_dx`, `telem_dy` plus
`telem_wall_ms` (CLOCK_MONOTONIC_RAW ms since boot — P121/BUG-06, lets consumers
compute sample staleness). Design keeps the hot path lock-free:

- **Writer** — `flush_motion()` (loop thread): relaxed stores to the six doubles, then an
  atomic release-bump of `telem_samples`. No allocation, no extra syscall.
- **Reader** — `status_json()` (IPC thread, under `devices_mutex_`): seqlock-style — load
  `telem_samples`, copy the six doubles, reload the counter; a match means a consistent
  sample, otherwise bounded retry (fields omitted via `telem_ok=false` if it never stabilizes).
- **Movability** — `telem_samples` is `std::unique_ptr<std::atomic<uint64_t>>` (T30 fix) so
  `mouse_device` stays movable for the `devices_` vector (copy/move ops in `daemon.cpp`).
- **Semantics** — `telem_in_ips` = euclidean |(dx,dy)| of the RAW (pre-rotation,
  pre-weight, pre-smoothing) deltas · dpi_factor / dt.  This matches the
  modifier's internal speed ONLY for euclidean mode + domain_weights 1 + snap 0
  + no smoothing (BUG-25/aj2 — deliberately: telemetry reports the physical
  input, not the curve-equivalent coordinate); `telem_gain` = out/in (0 when in == 0). Raw 1:1 passthrough
  does NOT fill telemetry: REL_X/REL_Y are forwarded per-event inline in
  `process_device()` so `flush_motion()` is never reached → `telem_*` keep their
  previous values, `telem_ok=false`, and `lat_*` fields stay absent (P121/BUG-05 doc;
  clients should treat missing telemetry as "raw passthrough active", and use
  `telem_wall_ms` to judge freshness when it is present).
- **Cross-check** — live fields vs the P31 `hotpath_prof` synthetic results validate the
  per-event pipeline end to end on real hardware.

## Key Design Decisions

- **PID file priority**: `$XDG_RUNTIME_DIR/rawaccel.pid` → `/run/rawaccel.pid` → `/tmp/rawaccel.pid`
- **Signal safety**: signal handler only sets an atomic flag (`request_stop()`), never joins threads
- **Sub-pixel accumulation**: `remainder_x/y` carries fractional movement so no micro-moves are lost
- **Float comparisons**: use epsilon (`1e-9`) instead of `!= 0` / `!= 1`
- **Atomic config write**: tmp file → `rename()` so the daemon never reads a half-written JSON; `save_config` uses a PID-suffixed temp name opened with `O_NOFOLLOW|O_EXCL` (no symlink clobber, no two-writer race)
- **Live reload (R5 fix)**: config reload updates settings in-place without releasing the mouse grab — no dropout window
- **Stable device IDs**: GUI and daemon both resolve `eventN` → `/dev/input/by-id/...` for reboot-stable profile assignment
- **Input validation**: `sanitize_device_profile()` clamps DPI (1–32 000), polling rate (125–8 000 Hz), rotation (0–360°), snap (0–45°), output DPI (0 = no output-DPI normalization sentinel, else 1–32 000), speed_max ≥ speed_min, accel_args fields (acceleration, scale, decay_rate, exponent_power ≥ 1e-4, offsets ≥ 0, limit ≥ 0, sync_speed ≥ 1e-4, smooth ≥ 0, motivity/gamma ≥ 0, cap ≥ 0), domain/range weights 0..1e6 (P86 ceiling; absurd magnitudes like 1e300 can't push accel-LUT lookups past representable indices), smooth halflifes ≥ 0, LUT `length` 0..max capacity — called on every JSON load
- **Systemd hardening**: `NoNewPrivileges`, `MemoryDenyWriteExecute`, `RestrictNamespaces`, `RestrictAddressFamilies=AF_UNIX`, `ProtectKernelModules/Tunables/ControlGroups`, `LockPersonality`, `RestrictRealtime` (netlink açıkça yok: daemon hot-plug için udev/netlink DEĞİL inotify kullanıyor — `daemon.cpp` `inotify_init1`)
- **Verbose log**: `daemon -v` shows device open/uinput creation details; `-f text|json` selects the log format (one JSON object per line when `json`)
- **uinput_write error handling**: all `libevdev_uinput_write_event()` calls are wrapped by `uinput_write()` which checks the return value and marks the device as disconnected on failure
- **SYN_DROPPED handling**: when kernel reports `SYN_DROPPED` (event buffer overflow), a `syn_dropped` flag is set; ALL subsequent events (motion, buttons, etc.) are discarded until the next `SYN_REPORT` clears the flag — per the Linux input protocol, events between SYN_DROPPED and SYN_REPORT are unreliable
- **IPC reload command**: `"reload\n"` via Unix socket schedules config reload (alternative to SIGHUP); GUI prefers IPC then falls back to SIGHUP
- **NaN sanitization**: `sanitize_accel_args()` and `sanitize_profile()` replace all NaN/Inf double fields (including `output_dpi`) with safe defaults before range-clamping (NaN silently passes `<`/`>` comparisons)
- **Version-stamped config migration**: every `save_config` stamps the current schema version; migration steps (`migrate_lookup_gain`, renamed fields) run only when a stored version is missing/stale — reloading a current file is a no-op (P43-BF1)
- **Config type guards**: on JSON load, scalar/string fields (`mode`, `gain`, `cap_mode`, `active_profile`, `use_raw_input`, `device_id`, `name`) are type-checked (`is_boolean`/`is_string` or a length-limited getter); `device_id` and `name` are capped at 256 chars; malformed types degrade to defaults instead of throwing (P54-B4)
- **CLI config safety**: `safe_save` writes atomically with a `.bak` rotate (the previous config is rotated to `path.bak` before the atomic tmp+rename+fsync overwrite) and exits cleanly (no SIGABRT) on I/O errors; a missing command argument reports a targeted error; a trailing bare `-c` is reported; an existing-but-corrupt config is never overwritten (P42, P82-MED-2)
- **CLI `--no-daemon` flag**: mutating commands (create, set, set-param, rename, duplicate, delete, import, create-preset) save the config locally and push it to the daemon by default; `rawaccel-cli --no-daemon` (or `--dry-run`) skips the daemon push so one-shot edits to a `-c /tmp/...` config never touch the live daemon config or /etc/rawaccel/settings.json. Apply later with `rawaccel-cli reload` (P82-CRIT-1)
- **Daemon option parsing**: `--config=PATH` / `--log-format=FMT` (`=` forms) are accepted next to `-c PATH` / `--config PATH`; a missing value is a hard parse error (exit 1); explicit `--config=` paths receive the same validation as `-c` (P53)
- **JSON log escaping**: `--log-format json` escapes log `message` strings (`\" \\ \n \r \t \b \f`, control chars → `\uXXXX`) so device names/paths/errno text can never corrupt the log stream (P53)
- **Subnormal time guard**: `modifier::modify()` clamps `ips_factor` to 0 when `dpi_factor/time` overflows to Inf (subnormal time values like 1e-309)
- **Modify output guard**: defense-in-depth `isfinite()` check at the end of `modifier::modify()` ensures no NaN/Inf escapes to motion_math
- **Duplicate device_id warning**: GUI warns on startup and on save if multiple profiles share the same device_id (first-match-wins in daemon)
- **lat_stats move safety**: move constructor/assignment lock the source mutex before copying data (defense-in-depth against concurrent record() during vector reallocation)
- **lat_stats finite guard**: non-finite latency samples are dropped and negative samples clamped to 0 before histogram insertion (P55-O1)
- **Lookup zero-width segment guard**: a duplicated X (denominator 0) falls through to the next point's `by` instead of producing ±Inf; valid strictly-increasing tables are unaffected (P55-O2)
- **Pre-computed dpi_factor**: `mouse_device::dpi_factor` is computed once in `apply_profile()` instead of dividing on every mouse event — eliminates a floating-point division from the hot path
- **Overflow-safe magnitude**: `magnitude()` uses `std::hypot(x,y)` instead of `sqrt(x*x+y*y)` — prevents intermediate overflow/underflow for extreme delta values
- **Unified clock source**: both `now_ms()` and `now_ns()` use `CLOCK_MONOTONIC_RAW` — eliminates drift between timing sources and reduces the per-event `clock_gettime` read count from 3 to 2 (start + end; P100)
- **P93-BATCH output (PERF, hot path)**: `process_device()` accumulates a whole frame — motion REL, any queued non-motion events, and the closing SYN_REPORT — in a small stack `write_batch` (`daemon/daemon.cpp`) and submits it in a SINGLE `write()` syscall at the real SYN_REPORT. Kernel uinput injects every `input_event` found in the write buffer, so the event stream is byte-identical to the old one-syscall-per-event path while collapsing (typically 3+) writes into 1. Zero-valued axes are skipped; a full buffer forces an early flush (nothing is dropped); overflow beyond 16 events flushes in place preserving order; non-SYN subtypes (SYN_MT_REPORT…) and raw-passthrough REL stay 1:1 per-event (libinput-feel contract, RAC-5). `flush_motion` still reads start/end (2 × `clock_gettime`, vDSO) plus 1 batched write; with batched evdev reads (below) the canonical hot-path cost per motion frame is small constant: 1 `read()` + 1 `write()` + 2×`clock_gettime`.
- **Batched evdev reads (PERF, hot path)**: `process_device()` reads up to 32 `input_event`s per `read()` into `read_batch` instead of one struct per syscall — a typical 3-event frame (REL_X, REL_Y, SYN) costs 1 read syscall. Short/torn reads are handled as before (misaligned stream → log + drop the tail); EAGAIN/EINTR/error semantics unchanged.
- **Y-axis unlinked field sync**: when X/Y axes are unlinked, fields without dedicated Y widgets (cap_mode, exponent_power, decay_rate, scale, output_offset, motivity, gamma, smooth, sync_speed) are copied from X to prevent stale values
- **Widget sensitivity refactor**: raw passthrough grey-out logic extracted to `update_raw_sensitivity()` — single source of truth for 18 widget enable/disable calls
- **GUI language resolution**: header-bar dropdown persists `auto`/`en`/`tr` to `<config_dir>/gui_lang`; an explicit preference wins (`load_lang_override`), otherwise `LANG`/`setlocale` decides (`sys_locale_is_turkish`). `tr()` returns the Turkish rendering only when the resolved language is Turkish — English is the dictionary key itself, so missing entries degrade to the source string. `refresh_language()` re-applies every registered widget in place on switch.
- **Low-latency motion contract (player focus)**: one loop thread processes every motion event synchronously (evdev read → modifier → uinput write); the epoll 10 ms timeout only serves housekeeping (hot-plug, IPC, signals). No per-event allocation; `mouse_device::dpi_factor` is pre-computed; both timers use `CLOCK_MONOTONIC_RAW`. Measure with the per-device `lat_stats` histogram (µs) via `rawaccel-cli latency` (SIGUSR1 → `snapshot_and_reset`); Min/Avg/p50/p95/p99/Max come from histogram bucket midpoints. p50/avg ≈ 1/3 of a 125 µs frame budget on hardware today; the rare p99/max queue spikes are the P-round analysis target.

## Known Limitations

- GUI uses `.inl` file compilation (single translation unit) — GTK4 C callback ABI makes true class-based split impractical without a full rewrite
- Test infrastructure is simple (no external framework) — no parallel test support
- E2E daemon test (`tests/e2e_harness.cpp` + `tests/run_e2e.sh`) drives the REAL
  daemon against a synthetic uinput source and a virtual sink; requires root +
  `/dev/uinput` and is therefore **not run in CI** (CI builds/tests only). It
  SIGSTOPs a running system daemon for the duration (trap → SIGCONT) so the
  hot-plug scan can't steal the synthetic source's grab. Config/validation/
  multi-profile logic stays covered by the integration tests.
- Device discovery (daemon + GUI) filters only on REL_X+REL_Y and physical/virtual
  status — no name/type-based exclusion for TrackPoint / touchpad / stylus / pad.
  Deliberate policy (BUG-26/aj2): auto-exclusion by name risks silently disabling
  legitimate mice (false positives), and per-device profile assignment already
  exists for users who don't want RawAccel on a given input. Revisit only if a
  real "All devices" false-positive is reported on hardware.
