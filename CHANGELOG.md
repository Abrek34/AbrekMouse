# Changelog

All notable changes to **rawaccel-linux** are documented here.

The canonical version string lives in `include/rawaccel-base.hpp`
(`RAWACCEL_VERSION`) and must stay in sync with `CMakeLists.txt` and
`packaging/PKGBUILD` — bump all three together.

## [0.6.6] — 2026-09-11

### Added
- Per-application profiles (`match_app`): a profile can be bound to the focused
  application's WM_CLASS so it applies only while that app is active. The
  daemon live-switches profiles without dropping the mouse grab (case-insensitive
  substring matching; empty = always). Config, JSON round-trip, sanitize (128-char
  cap), and legacy compatibility are covered by tests (33786 assertions).
- KDE Wayland active-window focus relay: an embedded KWin script watches
  `workspace.windowActivated` and forwards `resourceClass` to the GUI-owned
  GDBus service `org.rawaccel.Focus`, which pushes it to the daemon via the new
  `set_active_app` IPC. Works on X11/KDE too; degrades gracefully on other
  desktops (GUI "App:" field is then a manual per-profile hint only).
- GUI "App:" match field on each profile's Device Assignment row.
- HID++ transport layer (Easy-Switch/LED/reprog groundwork): `get_change_host_info`,
  `set_change_host`, `get_led_brightness`, `set_led_brightness` (0x8040/0x1982/0x1981),
  `get_reprog_controls`, and `write_onboard_profile_sector`. Queued for the next
  GUI hardware panel.

### Fixed
- Graph pan-zoom: wheel-zooming mid-drag no longer shifts the pan baseline
  (`drag_zoom_start` is frozen at drag start).
- Profile name gate now strips surrounding whitespace so a name of only spaces
  can no longer create a blank/ambiguous profile row.

## [0.6.4] — 2026-09-08

### Added
- Added native C++ Logitech HID++ controls for supported devices:
  hardware DPI, discrete report/polling rate, and extended sensor lift-off distance.
- Added CLI commands `hidpp-set-dpi`, `hidpp-set-rate`, and `hidpp-set-lod`.
- Added protocol rate conversion and HID++ feature-ID regression tests.
- Unsupported devices reject hardware changes without modifying RawAccel profiles.

## [0.6.3] — 2026-09-08

### Fixed
- Made live telemetry fields atomic and kept snapshots consistent across daemon and IPC threads.
- Fixed IPC shutdown descriptor reuse, strict `set_config` framing, and startup config-path races.
- Prevented stale PID cleanup from allowing a second daemon to start alongside a live instance.
- Rejected non-finite event intervals before they can poison stateful speed smoothers.
- Added regression coverage for telemetry/lifecycle boundaries and invalid timing recovery.

## [0.6.2] — 2026-09-08

### Fixed
- Corrected HID++ 2.0 feature-set discovery through the dynamic FEATURE_SET index.
- Added HID++ report validation, error-response rejection, target-aware feature
  caching, and safer hidraw polling/read handling.
- Improved direct Logitech device probing without assuming every endpoint is a receiver.
- Added deterministic HID++ packet regression coverage for malformed and null input.

## [0.6.1] — 2026-09-08

### Bug Fixes & Security Hardening
- **Atomic config backup**: `save_config()` now snapshots the old file to
  `.bak` with a private hard link before replacing it.  The live config path
  is no longer briefly absent during a save, so concurrent daemon/GUI/CLI
  reads cannot spuriously fail with a missing-file error.
- **accel-classic (`gain_inverse`) division-by-zero protection**: guarded against `accel == 0` and `power <= 1.0` when calculating inverted gain thresholds, preventing `Inf`/`NaN` poisoning in GAIN mode.
- **accel-power (`scale_from_output_point`) overflow protection**: added negative difference check and `std::isfinite` fallback guarding against non-finite scale multipliers under small exponents.
- **CLI IPC partial send loop**: wrapped `send()` in a retry loop with `EINTR` handling to ensure large JSON configurations are completely transmitted without socket truncation.
- **GUI zombie process fix**: replaced manual `fork()` and 5-second `waitpid` timeout in `on_kde_open_settings` with `g_spawn_command_line_async`, delegating process lifecycle and cleanup entirely to GLib.
- **GUI profile duplicate parity**: cleared `device_id` when duplicating profiles in GUI (`profile_mgr.inl`) matching CLI behavior, preventing duplicate device binding collision warnings.
- **setup.sh symlink hardening**: added symlink checks before synchronizing user configuration to `/etc/rawaccel/` and used secure file installation (`install -Dm644 -o root -g root`).
- **Packaging (Arch PKGBUILD & CMakeLists.txt)**: included installation of `scripts/rawaccel.quirks` to `/usr/share/libinput/50-rawaccel.quirks` to prevent compositors/libinput from applying double pointer acceleration over the virtual uinput device.

## [0.6.0] — 2026-09-07

> **Version decision:** 0.6.0 **MINOR** bump (maintainer-approved, R50). The
> release covers the R50 performance round (daemon `status_json` lock-narrowing
> + hot-path benchmark/perf-gate tooling + docs) and the R51–R52 fix rounds
> (accuracy, CLI, GUI and oracle-hardening) under the same version — all
> additive / non-breaking, hence MINOR rather than MAJOR. The canonical bump
> text lives in `include/rawaccel-base.hpp`, `CMakeLists.txt`,
> `packaging/PKGBUILD` (P146), pkgrel carried to 3 for the final artifact.

### Documentation / UX
- **New [docs/performance_tuning.md](docs/performance_tuning.md)** — user-facing
  performance guide: the 3-syscall hot-path model (2× `clock_gettime` +
  1 batched REL write), measurement data (math 20–425 ns/event vs syscall cost,
  worst config ~0.34% CPU at 8 kHz), polling-rate frame-budget table
  (125–8000 Hz), DPI granularity facts (curve is DPI-agnostic), the
  smoothing-halflife responsiveness table (halflife is the only real "feel
  latency" lever: 10 ms ≈ one frame, 100 ms ≈ 190 ms of wrong gain at the knee),
  native-vs-portable/PGO build notes, latency measurement workflow, and a
  latency FAQ (sources: AGENTS.md canonical syscall contract, precision.md §8.3,
  real_hardware_test.md §4).
- README: added a pointer from the **Performance** section to the new guide.
- **GUI latency panel live validation** (R50 §P140): the status-bar
  **Performans** button readout (Avg/p50/p95/p99/Max, P90) checked against a
  live daemon — the `status` JSON `lat_*` fields on the active profile's device
  slice were cross-verified against three `rawaccel-cli latency` dumps; code
  path `on_perf_clicked()` → `daemon_device_field()` → `latency_lbl` confirmed
  wired (no code change needed); the performance guide now ships a real
  `rawaccel-cli latency` dump as a live example.
- **R50 measurement results folded into the guide** (R50 §P142): the P136
  `status_json` lock-narrowing (471 ns → 57.7 ns under the lock, **8.2×**
  shorter, percentile math moved out) is now documented in §4.1; the P138 PGO
  evaluation (11.01 vs 11.05 ns/apply ≈ **0.4%, noise — not adopted**) and the
  oracle cross-check (local 35 cyc/apply vs official ref 40 cyc/apply ⇒ local
  ~11% faster) landed in §2 and §5.

### Performance / Internal
- **status_json lock-narrowing** (P136, Aj 6): the IPC reader now takes a fast
  `lat.copy()` snapshot (57.7 ns) under `devices_mutex_` and computes percentiles
  after releasing the lock instead of doing all math (471 ns) while holding it —
  8.2× shorter lock hold, hot path untouched (seqlock) and byte-identical stats.
- **Hot-path benchmark + CI perf gate** (P135/Aj 5, P137/Aj 2): new
  `scripts/bench_hotpath.sh` (+ `tests/bench_hotpath.cpp`) measures
  cycles/instructions/syscalls per event over the noaccel/power/classic
  baseline, ready for a +5% regression gate; `perf-gate` CI job added
  (runs `bench_hotpath.sh --compare tests/perf_baseline.json` once the
  baseline lands; auto-skips until then).
- **Fuzz hot-path seed** (P137, Aj 2): `fuzz_accel` now seeds from
  `tests/corpus_accel/` in addition to the config corpus.
- **PGO evaluated — not adopted** (P138, Aj 4): `-fprofile-generate`/`-fprofile-use`
  under production flags showed ~0.4% (noise) with build-time and toolchain
  fragility costs; `tests/oracle/oracle_perf.cpp` + `run_oracle_perf.sh` added
  as a reusable ns/cyc micro-benchmark (local ~11% faster than vendored official ref).

### Bug-fix / accuracy round (R51–R52)

- **Accuracy fix — power `io` cap.y ≤ 0** (P155): a zero/negative cap.y previously
  produced a *silent dead mouse* (scale 0 → gain 0). It now evaluates to identity
  scale with the cap disabled. Matches the official reference's degenerate-input
  semantics (ref itself yields NaN in GAIN / 0 in LEGACY for that input — the
  local guard avoids the dead curve and is a documented deviation).
- **Accuracy fix — power GAIN cap order** (P155): `operator()` now follows the
  reference ordering — the `speed < cap.x` cap branch is decided **before** the
  output-offset plateau, so an `output_offset > cap.y` tail no longer bypasses
  the cap (previously high speeds froze at the output offset, `output_offset=1e6`
  → flat 1e6 instead of the cap tail). Canonical cap setups are bit-identical
  (A/B verified); only the previously-broken region changed.
- **Sanitize fix — EMA half-life ceiling** (P155): smooth half-life values are
  clamped to `≤ 1e9` in `sanitize_*` (prevents EMA freeze on absurd inputs).
- **CLI fix — no more lying "Daemon reloaded."** (P156): `daemon_apply_config`
  now returns failure on a daemon `"ok": false` response and only falls back to
  SIGHUP for empty/unknown replies; previously a rejected live push was reported
  as success. Bonus: `delete` prints the "active profile changed" message only
  when it really changed; `copy_file` backup path fsyncs the destination; and
  `output_dpi` accepts fractional DPI (`range_ok` instead of an int cast).
- **GUI fix — event-driven child process reaping**: both daemon-start/apply
  child watchers now use `g_child_watch_add` to hook GLib's internal SIGCHLD mechanism,
  eliminating periodic 500ms CPU polling wakeups and preventing `EINTR`-induced zombie leaks.
- **GUI fix — `xy_linked` user choice preserved** (P157): an unlinked X/Y pair
  whose current values happen to be equal is no longer silently re-linked; the
  guard now only forces unlinking when the values truly differ.
- **Concurrency fix — daemon telemetry seqlock**: two-phase odd/even update protocol
  implemented for `dev.telem_samples`, preventing torn telemetry reads in `status_json`.
- **JSON robustness — locale-independent float formatting**: `append_fixed` now
  enforces RFC 8259 '.' decimal separators regardless of LC_NUMERIC and guards `NaN`/`Inf`.
- **Accuracy fix — power `io` cap.x ≤ 0**: non-positive `cap.x` in `io` mode now
  evaluates to identity base curve rather than freezing mouse gain to a flat constant.
- **CLI robustness — IPC socket timeout**: extended from 200 ms to 2000 ms to match
  daemon server timeout and avoid false timeouts during disk `fsync` operations.
- **Oracle expansion + regression lock** (P158): +7 power `io`/offset-tail cases
  (cap.y=0 identity, tiny cap.x, `output_offset>cap.y` tail — gain & legacy). The
  differential oracle now compares **1047 rows** against the official reference
  with **45 documented deviations**; `tests/test_accel.cpp` got a P155 SECTION
  pinning the fixed gain values (1e6 / 644495.723284 / 33.7247 / 1.50322).
- **Perf regression gate (P159)**: the R52 fix set measured against the R50
  baseline — power-dual+4EMA **+0.35%…+0.8%**, classic +0.2% (noise), all well
  under the +5% CI gate; live hot-path p50 ≈ 2.25 µs preserved.
- **R53 second-opinion round**: all 7 audit lanes re-verified the R52 fixes
  (0 functional CRIT); doc/CI gaps found there (CHANGELOG/AGENTS/perf-guide
  sync, single-run CI perf-gate noise → median-of-3) landed with this commit.

## [0.5.0] — 2026-09-05

### Player round (oyuncu turu, R29–R37)
- **`rawaccel-cli create-preset`**: research-based game presets `cs2`, `valorant`,
  `apex`, `fps` (plus existing `gaming`/`office`/`precision`/`disable`) map onto
  the acceleration engine with per-game parameter choices; a `.tar`/file import
  arity fix (`import` with a single file) closes a CRIT round-trip gap (P65).
- **Default profile is acceleration-free, not raw passthrough** (`noaccel` mode —
  no acceleration by default, user decision, R34); acceleration is opt-in per
  profile. `raw_passthrough` stays `false` and output is normalized to 1000 DPI,
  so the fresh default is **not** a literal 1:1 bypass — exact raw 1:1 is the
  `disable` preset (`raw_passthrough=true`; R46/P101 correction).
- **Apex preset tuning** (`output_offset 0.2 → 0.9`): fixes the sub-1:1 "muddy"
  feel on the power curve while keeping the fast 180° flick ramp (R37, user
  approved).
- **Player guide** in README: game-specific preset tutorial / tuning workflow
  for CS2, Valorant, Apex, generic FPS.
- **Oracle esport grid** (`tests/oracle/`): the speed sweep now covers the
  tournament band `2000 / 3000 / 4000` ips between the previously sampled
  `1000/5000`; the four game presets are mirrored as dedicated cases. The
  differential oracle now compares **915 gain rows** (31 documented intentional
  deviations) vs the official reference.
- **Virtmouse live harness** (`scripts/virtmouse-game.c`, R35/P64): injects
  synthetic game-speed mouse motion (uinput virtual mouse) for live end-to-end
  latency / hot-path verification.

### Live telemetry (IPC `status`)
- Per-device motion telemetry published in the `status` JSON from the daemon:
  `telem_in_ips`, `telem_out_ips`, `telem_gain`, `telem_dx`, `telem_dy`.
- Lock-free lane: `flush_motion()` (loop thread) does relaxed stores + a
  release-bump of the seqlock counter; `status_json()` (IPC thread) does the
  double-load verify with bounded retry. No measurable hot-path cost
  (independent latency gap +1.1% / −1.6% = noise, see P32/P38).
- Raw-passthrough still fills the counter and deltas; gain is 0 when
  `in == 0`. `rawaccel-cli status` / GUI connection state surface it as-is.

### Stability / correctness fixes (Bug-Hunt package)
- **CLI (`cli/main.cpp`)**
  - All 8 config-mutating commands route through `safe_save()`: a save or
    I/O failure now reports a clean error and exits 1 instead of
    `std::terminate` → SIGABRT (exit 134).
  - Missing command arguments produce a targeted
    `Command 'X' is missing N argument(s)` message instead of a misleading
    "Unknown command" plus full help.
  - A trailing bare `-c` reports `Option '-c' requires a path argument`.
  - An existing-but-corrupt config is never overwritten: load refuses with
    `Refusing to overwrite — run validate`; a default is created only when the
    file genuinely does not exist.
- **Config (`src/config.cpp`)**
  - (P43-BF1, critical) The schema `version` is now read back from the JSON
    instead of defaulting empty on every load. Previously `migrate_config()`
    re-ran on each load, so a stored `lookup` + `gain` config grew on every
    save (200 → 20000 → 2000000 in a round-trip probe). Migration now runs
    exactly once and is a no-op for current files.
  - LUT `length` is clamped to `LUT_RAW_DATA_CAPACITY` (no out-of-bounds
    access from a programmatic by-pass).
  - `save_config()` temp files use a PID-suffixed name opened with
    `O_NOFOLLOW | O_EXCL` — no symlink clobber, no two-writer race.
  - Scalar/string fields (`mode`, `gain`, `cap_mode`, `active_profile`,
    `use_raw_input`, `device_id`, `name`) are type-guarded; `device_id` and
    `name` are capped at 256 characters; malformed types degrade to defaults.
  - `lut_data` min computation is `size_t`-safe (no narrowing cast UB).
- **Daemon (`daemon/main.cpp`)**
  - JSON log output escapes every `message` field (`\" \\ \n \r \t \b \f`,
    control chars → `\uXXXX`), so device names/paths/errno text can never
    corrupt the log stream.
  - `--config=PATH` / `--log-format=FMT` (`=` forms) accepted next to
    `-c PATH` / `--config PATH`; a missing value is a hard parse error
    (exit 1); explicit `--config=` paths receive the same validation as `-c`.
- **Hot-path & latency safety**
  - `lat_stats::record()` (`daemon/lat_stats.hpp`) drops non-finite samples
    and clamps negative ones to 0 — histogram invariants stay valid.
  - Lookup zero-width segment (`include/accel-lookup.hpp`): a duplicated X
    (denominator 0) falls through to the next point's `by` instead of
    producing ±Inf; valid strictly-increasing tables are unaffected.
  - Removed a dead guard (`hi < capacity-1`); no behavior change.
- **GUI (`gui/widgets_sync.inl`)**
  - `pkexec_systemctl_async()` returns `pid > 0`, so the systemd start/stop
    branch is actually taken — closes the double-start window where both
    `pkexec systemctl` and a direct `pkexec rawaccel-daemon` were launched.

### Documentation / UX
- README: new "Player profile (oyuncu profili)" section (gaming preset
  table), low-latency & safe-defaults documentation, performance expansion,
  `rawaccel-cli latency` statistics, telemetry-via-`status`.
- AGENTS.md: "Live Telemetry & Seqlock" and "Low-latency motion contract"
  design decisions, translation-coverage rules, hardening parity between
  `scripts/build.sh` and CMake.

### Tooling / QA / packaging
- Differential oracle (`tests/oracle/run_oracle.sh`) vs the vendored official
  RawAccel reference: 915 gain rows — 884 matched at REL 1e-9 + 31 documented
  intentional deviations (expanded with the R35 esport grid); wired into CI
  (`ci.yml`) so accel-math drift fails the build.
- Translation coverage suite (`tests/run_tr_coverage.sh`): every translatable
  UI string must have a Turkish entry (0 MISSING = PASS).
- SYN_DROPPED event-sequence machine tests (8 scenarios) added — 132 test
  groups / 21627 runtime assertions green under ASan+UBSan too.
- Fuzz smoke (60 s) in CI; deep run 11.7M+ executions crash-free.
- PKGBUILD (Arch/CachyOS) with hardened, PIE, `BIND_NOW` binaries; canonical
  `setup.sh` one-shot installer enforcing the dependency policy on the
  pacman/apt/dnf branches.

## [0.4.0] — 2026-09-05

- **Turkish GUI** with a live language switch (Otomatik / English / Türkçe);
  preference persisted in `~/.config/rawaccel/gui_lang`.
- **IPC config push sync**: the GUI sends `set_config` to the root daemon,
  which writes `/etc/rawaccel/settings.json` and applies the change live
  (previously SIGHUP reloaded a stale copy).
- **KDE Plasma double-acceleration protection**: `scripts/kde-fix-accel.sh`
  sets per-device Flat + 0 acceleration in `kwinrc`/`kcminputrc`.
- Reference alignment of all acceleration modes (classic GAIN / jump /
  synchronous / lookup) and the differential oracle harness.
- Hardened build flags in both `scripts/build.sh` and CMake, with
  `RAWACCEL_PORTABLE=1` support.
- Canonical one-shot installer: `sudo bash setup.sh` (deps + build + system
  files + service + KDE fix), `--uninstall`, `--reinstall`; `scripts/install.sh`
  kept as a thin wrapper.
- First Arch/CachyOS package (`packaging/PKGBUILD`).
