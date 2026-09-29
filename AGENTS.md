# RawAccel Linux — Developer Notes

This file documents build, test, and verification commands.

## Install (canonical one-shot setup)

The **single source of truth for installation is `setup.sh`** at the repo root.
It installs ALL system dependencies, cleans any previous install, builds,
installs binaries/systemd/udev/desktop-file/libinput-quirk, enables the
service, and applies the KDE Plasma flat-acceleration fix.

> DOC-1: the polkit integration is GONE since 0.6.4 (BUG-02 — the pkexec action
> and rules.d were inert and are no longer installed; `clean_old_install`
> removes them and `do_install` never installs them).  The `polkit` *package*
> dependency in `install_deps()`/PKGBUILD intentionally remains — the GUI still
> uses `pkexec` (default action) for privileged daemon control — but there is
> no `org.rawaccel.policy`/`49-rawaccel.rules` integration.

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

# Baseline build (no -march=native, no AVX2/FMA) — this is what CI uses
RAWACCEL_PORTABLE=1 bash scripts/build.sh

# Custom compiler
CXX=clang++ bash scripts/build.sh
```

Output binaries: `build-manual/rawaccel-daemon`, `build-manual/rawaccel-cli`, `build-manual/rawaccel-gui`

Both `scripts/build.sh` and the CMake target apply the same hardening flags
(`-fstack-protector-strong`, `-fstack-clash-protection`, `-D_FORTIFY_SOURCE=2`,
`-D_GLIBCXX_ASSERTIONS`, `-fPIE`+`-pie`, `-Wl,-z relro,now,noexecstack,separate-code`,
`-fcf-protection=full` on x86).

**`RAWACCEL_PORTABLE=1` means "no `-march=native`", not "runs on any CPU".**
It exists so a build does not depend on which CPU the builder happens to be —
GitHub runners do not guarantee a CPU level, and `-march=native` bakes whatever
the runner has into the binary. The shipped release is the `-march=native` one
(`packaging/PKGBUILD`); the flag is a CI/reproducibility knob, **not** a
distribution-portability promise, and the docs used to claim otherwise in three
places at once (fixed in `cece865a`'s follow-up: `scripts/build.sh:41`,
`README.md`, `docs/performance_tuning.md`).

**The flag was two switches, and the two build systems disagreed about it.**
`scripts/build.sh` controlled `-march=native` via `MARCH` but appended
`SIMD_FLAGS` unconditionally, so the flag left `-mavx2 -mfma` in. Meanwhile
`CMakeLists.txt:68` and `scripts/bench_hotpath.sh` add no ISA flags at all, so
there the same flag honestly produced the x86-64 baseline. That disagreement —
not the missing `-march` — was the actual defect, and it hit the common path:
both `setup.sh` and CI build through `build.sh`. `SIMD_FLAGS` now follows the
flag (`-mfpmath=sse -msse2`), so all three entry points mean the same thing.
SSE2 is the x86-64 baseline ABI floor, so naming it is a no-op on this target;
the load-bearing half is **dropping** `-mavx2 -mfma`.

Why that mattered: backend selection is compile-time only
(`include/simd_math.hpp:48-60` keys off `__AVX2__`/`__SSE2__`; the tree has
**zero** uses of `__builtin_cpu_supports` / `cpuid` / `xgetbv`, measured, so
there is no runtime dispatch to fall back on). An AVX2 instruction on a CPU
without AVX2 is `#UD` → SIGILL. Measured on `RAWACCEL_PORTABLE=1`: VEX `%ymm`
instructions daemon 20258 → **0**, cli 14668 → **0**, gui → **0**; backend
actually selected SSE2 / 16-byte `v2d` / no `__FMA__`, versus AVX2 / 32-byte /
`__FMA__` natively; both builds exit 0 on `--help`. ARM/aarch64 was never
affected (the `uname` case adds nothing there). CI never caught it because
ubuntu-24.04 runners are post-Haswell — the flag's one consumer was the CI job
that could not observe the failure it existed to prevent.

Side effect worth keeping: the baseline build is the one that matches the
oracle's own `-O2 -mavx2` numbers (0 differing rows at `%.17g`), so it is also
free of the `-mfma` drift class documented in `tests/run_simd_parity.sh`.

## Test — Canonical Seven Gates

These seven gates are the canonical verification pipeline. Run them in order;
each must exit 0 before the next runs. Missing or skipped gates are not
acceptable — "yedi kapı yeşil" means all seven.

```bash
# 1. Build with zero warnings
bash scripts/build.sh

# 2. Unit + integration tests (includes SIMD parity gate)
bash tests/run_tests.sh

# 3. Oracle (reference cross-check)
bash tests/oracle/run_oracle.sh

# 4. SIMD backend parity (AVX2 / SSE2 / scalar)
bash tests/run_simd_parity.sh

# 5. Translation coverage (every UI string has Turkish entry)
bash tests/run_tr_coverage.sh

# 6. CLI under ASan + UBSan (31 real commands)
bash tests/run_cli_sanitized.sh

# 7. Tracker bridge (record↔code consistency)
bash tests/run_tracker_bridge.sh
```

**Measured gate times on this machine (HEAD, portable build):**

| gate | command | time (s) |
|------|---------|----------|
| 1 | `scripts/build.sh` | 71.0 |
| 2 | `tests/run_tests.sh` | 43.2 |
| 3 | `tests/oracle/run_oracle.sh` | 1.8 |
| 4 | `tests/run_simd_parity.sh` | 3.3 |
| 5 | `tests/run_tr_coverage.sh` | 1.1 |
| 6 | `tests/run_cli_sanitized.sh` | 48.4 |
| 7 | `tests/run_tracker_bridge.sh` | 0.1 |
| **total** | | **~168.9** |

**Exit codes:** 0 = pass, 1 = fail, 77 = environment unusable (skip with message).
All seven must pass before push. `run_tests_asan.sh` is an **internal mode** of
gate 2 (invoked by `run_tests.sh`), not a separate gate. `run_e2e.sh` requires
root + `/dev/uinput` and is not in CI.

NOT: `scripts/bench_hotpath.sh` yedi kapidan biri **DEGILDIR** - CI'daki
`perf-gate` isidir (kirilim olcum kapisi, dogruluk kapisi degil).
NOT: `tests/run_fuzz.sh` ve `tests/run_e2e.sh` de yedi kapidan biri **DEGILDIR**
(sira sirasiyla CI disi ve root + /dev/uinput gerektirir).

```bash
# All unit tests (compile + run)
bash tests/run_tests.sh

# Same tests under AddressSanitizer + UBSan (slower, catches memory/UB bugs)
bash tests/run_tests_asan.sh

# The CLI itself under ASan + UBSan. run_tests_asan.sh only runs
# tests/test_accel.cpp, so cli/main.cpp (arg parsing, config-path validation,
# JSON round-trip, print_profile, diff/export) was executed by NO sanitizer.
# run_tests.sh runs the real CLI but without sanitizers — the two jobs together
# left cli/main.cpp uncovered. Exit 0 = clean, 1 = sanitizer report or a command
# that stopped doing real work, 77 = host compiler has no ASan/UBSan (skips
# loudly, never quietly). Needs no root, no /dev/uinput, no daemon.
bash tests/run_cli_sanitized.sh

# End-to-end: REAL daemon against synthetic uinput source/sink (needs root +
# /dev/uinput). SIGSTOPs a running system daemon for the duration and SIGCONTs
# it on exit (trap), so it cannot steal the synthetic source's grab.
# accel phase: frame/SYN structure, SM-2 button buffering, LOW-1 coalesced
# deferral, ×3 classic-linear gain. raw phase: byte-identical 1:1 passthrough.
sudo bash tests/run_e2e.sh          # exit 0 = pass, 1 = failed check, 77 = env unusable

# Translation coverage: every translatable UI string must have a Turkish entry
bash tests/run_tr_coverage.sh

# SIMD backend parity: compile tests/simd_parity.cpp once per backend
# (AVX2 / SSE2 / scalar), assert a Y-only input survives modifier::modify()
# AND the extreme-value/IEEE contract (see tests/simd_parity.cpp), then diff
# the three backends' numeric output against each other.
# Exit 0 = all pass AND agree; 1 = failure or backend mismatch; 77 = host
# cannot run the AVX2 binary (skips gracefully).
#
# NOTE: run_tests.sh runs this gate internally, so the two commands below are
# not independent — `bash tests/run_tests.sh` alone DOES cover all three
# backends. Run it separately only to iterate on simd_math.hpp without paying
# for the full 34k-assertion suite.
bash tests/run_simd_parity.sh
```

## SIMD backend coverage (READ BEFORE TOUCHING include/simd_math.hpp)

`include/simd_math.hpp` selects one of **three** backends at compile time:
AVX2 (`__AVX2__`), SSE2 (`__SSE2__`), or a pure-scalar fallback. They are NOT
interchangeable, and a bug can live in only one of them.

**Lane layout (AVX2 backend).** `v2d_set(x, y)` → `_mm256_set_pd(0, 0, y, x)`,
so **X is lane 0 and Y is lane 1**; lanes 2,3 are zero padding. Every
accessor must therefore read/write lanes 0,1 only:

```cpp
v2d_store: _mm_storeu_pd(ptr, _mm256_castpd256_pd128(v));
v2d_get_y: _mm_cvtsd_f64(_mm_unpackhi_pd(lo, lo));   // NOT _mm256_extractf128_pd(a,1)
hmin/hmax : reduce lanes 0,1 only (folding in the padding makes hmin 0.0)
```

This exact mistake shipped once: `v2d_store`/`v2d_get_y` read lane 2, so every
AVX2 build wrote `0.0` into the Y component and **vertical mouse movement was
silently dead in the shipped daemon**. It reached production because at the time
`tests/run_tests.sh` had no `-march` flag, `tests/oracle/run_oracle.sh`
deliberately drops `-march`, and CI sets `RAWACCEL_PORTABLE=1` — so all three
exercised SSE2 only, while `CMakeLists.txt` / `scripts/build.sh` add
`-march=native` and shipped AVX2. `tests/run_simd_parity.sh` closed that gap;
it now runs **inside `tests/run_tests.sh`** as well as standalone in CI, so the
documented "just run the tests" path exercises all three backends. A host that
cannot execute the AVX2 binary makes the gate exit 77, and `run_tests.sh` prints
a loud `DİKKAT: SIMD parity ATLANDI` line rather than passing quietly.

**The gate also audits its own coverage (envanter denetimi).** Comparing the
backends only proves the functions somebody remembered to put in
`tests/simd_parity.cpp` agree. It says nothing about a `v2d_*` that was added to
`simd_math.hpp` and never called from the gate — and that is exactly the shape
the original Y-axis bug had. So the gate now classifies every `v2d_*` in
`simd_math.hpp` into one of three buckets and fails on a violation:

- **KAPSAMLI** — appears in `simd_parity.cpp` **code** (comments are stripped
  before counting, so a mention in a comment does not count as coverage)
- **CANLI** — has a call site outside `simd_math.hpp` and outside `tests/`;
  a CANLI function that is not KAPSAMLI **fails the gate**
- **ÖLÜ** — neither; must be named explicitly in `BILINEN_OLUMLER` at the top of
  `run_simd_parity.sh`, otherwise it fails as unclassified

The dead list is self-maintaining in both directions: an entry that is deleted,
becomes covered, or gains a production caller is reported as stale. Current
inventory: **26 `v2d_*` — 16 covered, 8 live, 10 declared dead, 0 violations.**
All ten dead ones are genuinely uncalled (verified by a positive control — the
same search does find `v2d_mul` in `rawaccel.hpp`). `v2d_blend` and `v2d_hypot`
were removed for the same reason plus a real divergence: `v2d_blend` read three
different mask conventions across the three backends (AVX2 sign bit, SSE2 full
bitmask, scalar truthiness), and no caller existed from which to derive the
correct contract — see `simd_math.hpp:30-41`.

**Rule:** any change to `include/simd_math.hpp` (or to the SIMD block in
`include/rawaccel.hpp`) must be validated with all three of:

```bash
bash tests/run_simd_parity.sh        # per-backend + cross-backend agreement
bash tests/run_tests.sh              # SSE2-path unit/integration suite
bash tests/oracle/run_oracle.sh      # differential vs official reference
```

`run_simd_parity.sh` is the only one of the three that touches AVX2 or the
scalar fallback, so it is the one to run first while iterating.

# Expected output: "=== Sonuç: N/N geçti ===" (N/N passed)
# Exits with code 1 if any FAIL line appears.
```

```bash
# Record↔code bridge: catches "the tracker still says ⏸/AÇIK but the code
# already carries the fix" — the mechanical form of "there is a signal, nobody
# measured where it was written".  Only that direction is gated; the reverse
# measured ~15-17% false positive (23 closed records, 4 with no code marker,
# 3 of them fixed with the marker in the commit message instead), so it is
# reported, not failed.  No root, no /dev/uinput, no daemon.
bash tests/run_tracker_bridge.sh
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
run. Current grid: **1408 rows compared, 79 documented deviations** (R3-NEW-2
added `power_tinyexp_floor` with exponent_power=5e-4 inside the BUG-02 floor
band — the local port evaluates a shared exponent floored at 1e-3, the
reference the raw 5e-4, so 23 of its 24 rows drift; spd=1 is force-checked
because both sides reduce to 1^n with scale=1 exactly.  PRE-3 raised the apex
`output_offset` to 1.0, which made the power identity row at speed 0 line up
with the reference — `game_apex_power 0` was removed from the deviation list).
G-2 added 12 natural `limit ≥ 10` cases (+288 rows). G-3 fixed the oracle's
finiteness check so that **both** `p155_io_cap0` families are now listed:
the `legacy` family (ref=0) and the `gain` family (ref=NaN — the oracle now
explicitly checks for finiteness mismatch). `known_deviations.txt` holds 79
rows in five classes (23 `classic_gain_exp_le1` + 9 `power`/`sync` at speed 0
+ 23 `power_tinyexp_floor` + 12 `p155_io_cap0_legacy` + 12 `p155_io_cap0_gain`),
each documented in that file's header. Run this after EVERY change to
`include/accel-*.hpp`.

**Oracle domain boundary (reachability of the match):** The oracle grid covers
speeds up to `1e5` (`default_speeds()` in `oracle_cases.hpp`). The port's
production pipeline is bounded by `sanitize_device_config` (DPI ≤ 32 000,
`src/config.cpp:362`), `IPS_FACTOR_MAX = 1e6` (`rawaccel.hpp:351-353` — clamp
on `ips_factor`), and domain/range weights ≤ `1e6` (P86, `src/config.cpp:605-606`).
The Linux input subsystem uses `__s32 value` (`/usr/include/linux/input.h:44`),
so `|ev.value| ≤ INT32_MAX = 2,147,483,647`. The daemon batches 32 events
(`daemon/daemon.cpp:2573`), accumulating `dx`/`dy` up to `32 × INT32_MAX =
6.87e10`. Clamped `ips_factor ≤ 1e6` and `domain_weights ≤ 1e6` yield:
`|abs_vel| ≤ 32·INT32_MAX × IPS_FACTOR_MAX × 1e6 ≈ 6.87e22`. The reference's
`magnitude(v)` uses `sqrt(x*x + y*y)` which overflows to `inf` at
`|v| ≳ 1.34e154` (`~2^1023`). The port uses `std::hypot` (R14) and log-space
`lp_distance` (ORTA-BUG-MOTION-03), so it **deliberately diverges** from the
reference for `|v| ≫ 6.87e22` — but this region is **unreachable from any valid
config** (margin ≈ 1.95e131×). The oracle's 1408 rows (max speed `1e5`) sit
entirely inside the matching envelope. If a future change widens the envelope
(e.g. raises `IPS_FACTOR_MAX` or weight ceilings), the oracle grid must be
extended with a dedicated boundary case at the production ceiling (`6.87e22`
scale) so the gate detects divergence before it becomes reachable.

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
- 151 `test_` functions / 217 `SECTION` groups, 34164 runtime assertions (the
  runner's own count; the source has 1603 `EXPECT` call sites — `#define` lines
  excluded, loops multiply them)
  covering: algorithms, JSON round-trips,
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

**These counts are machine-checked, not hand-maintained.** Every number in this
section and in the oracle section is recomputed by

```bash
python3 olcum/aj2/prove_kod_ayni.py --sayi olcum/aj2/sayisal_iddialar.txt
```

which exits 1 if this file disagrees with a live measurement, naming the claim's
line. The check, not the digits, is the fix: four of these numbers had rotted
while this file stated **1407 and 1408 about the same quantity two paragraphs
apart** — a class of error no amount of careful retyping prevents. The same
class produced a wrong ceiling that was written in the right value once and then
shortened to a different notation elsewhere, where `grep` for the correct value
cannot find it. Comparison is **numeric**, so `100000` vs `1e5` is not a
difference, but `3.3e16` vs `3.2767e16` is. Where the same number appears twice
it is claimed twice, deliberately: a claim repeated in two places is the only way
the second place cannot drift from the first. `prove_kod_ayni.py --atif` is the
sibling check for *line* citations, and it does **not** cover numbers.

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

Five jobs run on every push/PR (Ubuntu 24.04):
- **build-and-test** — baseline build (`RAWACCEL_PORTABLE=1`, so the result never depends on the runner's CPU), warning-as-failure gate
  via `grep -E "warning:|error:"`, then `tests/run_tests.sh`, then `tests/run_tr_coverage.sh`,
  then the differential oracle (`bash tests/oracle/run_oracle.sh`) which fails if any
  gain row drifts outside `tests/oracle/known_deviations.txt`.
- **sanitizers** — rebuilds tests with `-fsanitize=address,undefined` and runs them
  with `halt_on_error=1` so any leak/UB fails CI.
- **sanitize-cli** — runs `tests/run_cli_sanitized.sh`, which builds and executes
  `cli/main.cpp` under the same sanitizers. This TU is covered by no other
  sanitizer job; see the Test section above for why that hole mattered.
- **fuzz-smoke** — 60 s per harness via `tests/run_fuzz.sh 60`. Skipped on PRs to
  keep them fast; runs on any push (`!= pull_request`) and `workflow_dispatch`.
  Crash inputs are uploaded as artifacts on failure.
- **perf-gate** — hot-path performance regression gate (P137): runs
  `scripts/bench_hotpath.sh` against `tests/perf_baseline.json` (6 configs,
  median-of-3, dynamic iteration count from `_meta.iterations`); fails if any
  config is more than 5% slower than the baseline, otherwise skips on a missing baseline.

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
| `include/logitech_hidpp.hpp` | HID++ protocol + `HidppTransport` (hidraw I/O) and, beside it, the `hidpp_hw_sync` / `hidpp_hw_job` / `hidpp_hw_enqueue` / `hidpp_hw_take` / `hidpp_hw_plan_job` write-dedup queue. The queue is **owned by the daemon**, not here: `drain_hidpp_writes()` in `daemon/daemon.cpp` is the only consumer and takes **no** daemon mutex (leaf-lock contract), so the 5.8 s worst-case USB round-trip cannot stall the motion loop |
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
| `tests/test_accel.cpp` | Unit + integration tests (151 functions / 217 `SECTION` groups, 34164 runtime assertions) |
| `tests/fuzz_config.cpp` | libFuzzer harness — config JSON parsing |
| `tests/fuzz_accel.cpp` | libFuzzer harness — acceleration pipeline |
| `tests/run_fuzz.sh` | Fuzz test runner (both harnesses) |
| `tests/run_tests.sh` | Unit test runner (compile + run) |
| `tests/e2e_harness.cpp` | E2E harness: synthetic uinput mouse + REAL daemon + virtual sink; accel (T-A1..T-A4) + raw (T-B1) phase checks |
| `tests/run_e2e.sh` | E2E runner (root): builds harness, SIGSTOP/SIGCONT-sensitive system-daemon isolation, runs both phases, propagates 0/1/77 |
| `tests/run_tests_asan.sh` | Unit test runner under ASan + UBSan |
| `tests/run_cli_sanitized.sh` | Runs `cli/main.cpp` under ASan + UBSan over 31 real commands (8 presets, profile CRUD, JSON round-trip, config-path rejections); each command asserts its own expected output marker, so an arg rename can't silently turn the gate into 31 no-ops. Exit 0/1/77 |
| `tests/oracle/` | Differential oracle: `run_oracle.sh`, grid `oracle_cases.hpp`, local side `local.cpp`, official-ref side `reference.cpp`, `ref/` (vendored MIT), `known_deviations.txt` |
| `tests/tr_coverage.cpp` | Translation coverage audit (extracts all tr*()/grid_row keys) |
| `tests/run_tr_coverage.sh` | Translation coverage runner (exit 1 on MISSING) |
| `tests/simd_parity.cpp` | SIMD backend parity + Y-axis survival regression (compiled once per backend) |
| `tests/run_simd_parity.sh` | Runs `simd_parity.cpp` under AVX2/SSE2/scalar and diffs the three backends against each other; also audits its own coverage — every `v2d_*` in `simd_math.hpp` must be covered by the gate, live in production, or named in `BILINEN_OLUMLER` (a live-but-uncovered one fails, and stale dead-list entries fail) |
| `olcum/aj2/prove_kod_ayni.py` | Measurement tool: `--git` (code-unchanged proof), `--pc` (built-in positive control), `--dallar` (branch counts via `-fprofile-arcs`), `--atif` (audits its own `L<n>` line citations), `--sayi` (audits numeric claims in a doc) |
| `olcum/aj2/sayisal_iddialar.txt` | The numeric-claim list `--sayi` verifies: `target<TAB>regex<TAB>measurement command<TAB>note`. The claim itself stays in the document; this file only says how to check it, so a stale claim turns the check red instead of being silently trusted |
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
- **classic LEGACY `cap_mode::in` boundary (K1)**: the guard is
  `cap.x > 0 && cap.x >= input_offset` — the `>=` is load-bearing. The official
  reference guards ONLY on `cap.x > 0`; at `cap.x == input_offset` it computes
  `base_fn(input_offset) = 0` and freezes the whole curve at gain 1. A strict `>`
  skipped the cap and applied full unclipped acceleration instead — measured by
  the oracle at up to 500.9x the reference gain. The state is reachable from a
  real config because `sanitize_accel_args` clamps `cap.x < input_offset` UP to
  `input_offset`. Covered by `test_classic_legacy_in_cap_boundary()` and the
  `k1_legacy_in_*` oracle cases; the NaN guard for `cap.x < input_offset` (BUG-9)
  and the `cap.x == 0` 0/0 guard are asserted in the same test.
- **Logitech quirks keying is exact-match on a 12-char modelId**: `find_logitech_quirks()`
  has NO short-key/prefix/substring matching. The table's `"32"` row (G522) is a
  single model *byte*, not a model id, so it is unreachable — measured: over all
  16 transport-flag combinations x both response-size cases the composed id is
  only ever `""` or 12 chars. This is a PARKED decision (FIX_LOG.md O31-H2 —
  "gerçek 12-char id doğrulanmadan yazılmaz"), not an oversight: resolving it
  needs the physical G522. The row is kept (it is the only record of what was
  validated on that device) and `test_logitech_quirks_model_id_shape()` is the guard
  rail — it fails if the id parsing ever changes so a short key becomes
  reachable, so the decision is revisited instead of the row quietly mattering.
  A "short-key fallback" loop that used to sit in the lookup was removed as dead
  code: its match condition was a strict subset of the loop above it, proven
  identical over 1,510,738 inputs including an exhaustive sweep of every 1-4
  char string.
- **`LOGITECH_QUIRKS` is a data registry, NOT an enforced write gate (O31-H6)**:
  the `nvconfig`/`headset` rows have **zero consumers** — measured project-wide
  (AJ3 B-9, re-measured by AJ1 29 Sep 2026: `nvconfig` → **3** occurrences and
  `headset` → **3**, and **every** one is inside `logitech_quirks.hpp` — a
  comment (`:20`), a struct (`:56` / `:62`), and the `std::vector` member
  declaration (`:73` / `:74`). The search method is positive-controlled: the
  same `grep` does find `v2d_mul` in `rawaccel.hpp`, so the zero is a real zero
  and not a broken query.
  ⚠️ **Measurement trap, recorded because it produced a wrong number:** the
  first pass reported `nvconfig` **5×**, because `grep -rn` without path
  scoping also matched the agent's own report inside
  `.aihaberlesme/mesajlar/aj3.log`, which was quoting the same string twice.
  **A grep whose pattern appears in your own report will count your report.**
  Scope to `--include=*.hpp --include=*.cpp --include=*.inl` (or exclude
  `.aihaberlesme/`), and treat a count that jumps between two runs on an
  unchanged tree as a signal that the *query* changed, not the code).
  The **occurrence counts** below are what drift — the *conclusion* does not.
  `rgb_effects` (`0x8071`) is now **5** occurrences, not "exactly one outside
  this table": `include/logitech_hidpp.hpp:82` (the enum — the line moved from
  `:81`) plus four inside this table, of which **`:89` is a live data row**
  (G502 X PLUS), not a comment. `0x0622` is **4** occurrences and the earlier
  "only in comments" was **false**: `logitech_quirks.hpp:99` is a live data row
  (G522 LIGHTSPEED), alongside the comment at `:17`, the doc at `:61` and the
  member declaration at `:74`. Either way **no code reads either vector** — the
  extra hits are data, not consumers — so the "no enforced gate" hüküm stands
  (AJ3 B-10). Treat this paragraph as a *claim with a live count*, and re-measure
  the count before quoting it; the qualitative statement is the load-bearing
  half. The only HID++ writes in the tree are `set_dpi`,
  `set_polling_rate`, `set_lift_off_distance`, `set_led_brightness`,
  `set_change_host` and `write_onboard_profile_sector`, and none consults the
  table. The "default-DENY allowlist" the header and the HID++ panel label used
  to claim is therefore not enforced anywhere — it held only vacuously, because
  there is no such write to refuse. Both were corrected (O31-H6) because the
  failure mode is a *maintainer trap*: the rows read as a live gate, so the
  first person adding an RGB write path could reasonably assume one already
  exists. **Adding a 0x8071/0x0622 write path means adding the
  `find_logitech_quirks()` branch in the same change.**
- **HID++ short-message payload budget is a named constant, not a literal**:
  `HIDPP_SHORT_PAYLOAD_MAX` (16) is the payload room in a 0x8110 request, and
  `send_feature_request()` rejects any `param_len` above it outright — a silent
  total failure, not a truncation. `HIDPP_ONBOARD_SECTOR_DATA_CHUNK` is derived
  as `MAX - 2` because the 0x8100 `write_sector` params are `sector(u16) + data`
  (O31-H1: a literal 16 made an 18-byte vector, so *every* onboard sector write
  was rejected and the path silently never wrote). A `static_assert` and
  `test_hidpp_short_payload_budget()` both hold the invariant. The full device
  round-trip is still untested — `send_feature_request`/`resolve_feature_index`
  are private and `HidppTransport` needs a real hidraw node; no test seam was
  added to the class for this.
- **`apply_profile()` pushes HID++ polling-rate/DPI only when the value changed**
  (O31-D1): `set_polling_rate()` + `set_dpi()` are USB round-trips that run on the
  motion loop thread while it holds `devices_mutex_` AND `hidpp_devs_mutex_`.
  Their worst case is derived from the timeouts, not estimated: 2.6 s + 3.2 s =
  **5.8 s blocked per device** (11.6 s for two), and the worst case is reached
  exactly when the device is *not* answering. `hidpp_hw_sync` (in
  `include/logitech_hidpp.hpp`, next to `HidppTransport`) records what was last
  pushed; `plan_for()` returns which fields still need a write. Measured over the
  real trigger sequence (initial grab, no-op SIGHUP, software-only profile edit,
  real rate change, no-op SIGHUP, real DPI change, replug) it skips 6 of the 10
  possible writes.
  **The transport-pointer comparison is the load-bearing part, not the `0`
  sentinel.** `plan_for()` short-circuits on `t != transport` and returns
  `{true, true}` *without reading either value*, so a fresh record yields the
  same plan whether it holds `0/0` or `800/1000` — measured, and the reason a
  first draft that seeded the record from `mouse_device`'s defaults
  (`dpi 800`/`poll_rate 1000`, `daemon.hpp:48-49` — the same numbers a default
  profile carries) would have skipped the very first write. The `0` sentinel is
  defence-in-depth behind that check; `test_hidpp_hw_sync_guard()` asserts the
  sentinel **fields directly**, because an assertion written against `plan_for()`
  passes for both seedings (proven: that draft's test was green while broken).
  A different transport re-pushes both fields — an unplugged mouse returns at its
  onboard DPI/rate, not at whatever was last pushed. The record is written on
  *attempt*, not on success (`mark_attempted`): recording only on success would
  re-pay the full timeout on every apply for a device that permanently lacks the
  requested rate; the cost is that a *transient* failure is not retried until the
  value changes, a profile is edited, or the device replugs (every attempt is
  logged, and the pre-fix code retried unconditionally).
  **DONE — the writes now run on `hidpp_thread_`, not the loop thread
  (`93ec6277`).** The note that used to sit here ("still not done … queue them
  onto `hidpp_thread_`") is obsolete. One wording note, because the sentence
  this note replaced **overclaimed and contradicted itself two lines later**:
  it said the drain "holds **no lock at all**", while `drain_hidpp_writes()`
  (`daemon/daemon.cpp:1736-1783`, 48 lines) takes **exactly one** lock —
  `hidpp_wq_mu_` at `:1740` — and `devices_mutex_` / `hidpp_devs_mutex_` are
  never taken (AJ3 P104-B-1, measured 29 Sep 2026). The load-bearing half is
  still exactly true and is what the second half of that paragraph said:
  **no daemon mutex, and the single leaf lock is scoped to the dequeue only**
  (`:1740-1743`), so the blocking writes at `:1756` (`set_polling_rate`) and
  `:1766` (`set_dpi`) run unlocked. "No lock at all" was the wrong way to say
  it — the same overclaim class as the `byte-identical` wording corrected in
  the config-path section below.
  onto `hidpp_thread_`") is obsolete. `apply_profile()` plans, marks the
  sentinel and enqueues; `drain_hidpp_writes()` performs the writes. Three
  things were load-bearing and are not obvious from the code:
  - **Ownership had to change with the timing.** `hidpp_transports_` was a map
    of `unique_ptr` that the periodic rescan erases on unplug, and a queued job
    is drained *after* the lock is gone — so a raw `HidppTransport*` in a job
    dangles onto a freed kernel hidraw fd. The map holds `shared_ptr` and
    `find_hidpp_transport()` returns an owning reference. A positive control
    confirms the alternative: a job holding a raw pointer makes the transport
    get destroyed and the test then **segfaults** (exit 139) — the production
    use-after-free, reproduced.
  - **`hidpp_hw_plan_job()` plans AND marks in one step.** The sentinel is the
    only dedup, and with the write now asynchronous a plan-then-mark split lets
    a second apply arriving mid-flight enqueue the same write again — a slider
    drag would queue one job per intermediate value. Folding them together
    makes that window unrepresentable.
  - **The drain holds no daemon mutex, and its single lock is scoped to the
    dequeue only** (measured 29 Sep 2026, AJ3 P104-B-1 — this sentence used to
    read "holds **no lock at all**", which was **false and contradicted itself
    two lines later**). `drain_hidpp_writes()` (`daemon/daemon.cpp:1736-1783`,
    48 lines) takes **exactly one** lock: `hidpp_wq_mu_` at `:1740`, whose scope
    is `:1740-1743` — the `hidpp_hw_take()` dequeue and nothing else.
    `devices_mutex_` and `hidpp_devs_mutex_` are **never** taken, and the two
    blocking writes (`set_polling_rate` at `:1756`, `set_dpi` at `:1766`) run
    **unlocked**, so the 5.8 s stall no longer blocks the motion loop.
    `hidpp_wq_mu_` is a LEAF mutex: never taken under a daemon mutex, never
    held across a blocking write.
  Two behaviour changes worth stating: the "Set polling rate to N Hz" log is
  now emitted by the worker *after* the write returns, so it reports a fact
  rather than an intention; and writes still queued at `stop()` are discarded
  rather than drained (draining would add 5.8 s per device to shutdown, and an
  unwritten value is harmless — `hidpp_hw_sync` is in-memory so the next start
  re-pushes). The discard is logged, never silent. The worker shortens its
  200 ms nap to 20 ms while a job is queued: a latency hint only — this
  codebase uses no `condition_variable`, and correctness never depends on the
  flag, only latency does.
- **HID++ function ids normalise at the packet boundary**: `to_bytes()` packs
  `normalize_function_id(fn) << 4 | software_id`, and `normalize_function_id`
  folds the request-id spelling (`0x48`) to the bare 4-bit selector (`0x4`).
  The round-trip is therefore a normalisation, not an identity — `from_bytes`
  returns `0x04` for a packet built with `0x48`. Asserted in
  `test_hidpp_short_payload_budget()`.
- **Config-path policy is applied to the RESOLVED path, on every entry point**
  (O31-S1): `validate_config_path()` lives in **two copies with byte-identical
  CONTROL FLOW but not byte-identical text** — `daemon/main.cpp` and
  `cli/main.cpp`. Measured 29 Sep 2026 (AJ3 B-6, independently re-measured by
  AJ1): `daemon/main.cpp` 1877 B vs `cli/main.cpp` 1723 B, so "byte-identical"
  was false; the **only** divergence is the log prefix on `std::cerr` lines
  (daemon emits a `[rawaccel]` prefix, CLI prints a plain user-facing message).
  The decision chain is identical in both: the 7-line `return` sequence
  (`return false;` ×2 → `if (!check_config_path_policy(canonical)) return
  false;` → `return false;` ×3 → `return true;`) matches exactly, and
  `check_config_path_policy` is called at the same point. So what must stay in
  sync is the **policy and its ordering**, not the message strings — a
  divergence in the `return` chain is a security bug; a divergence in the
  prefix is not. It runs
  two steps: `resolve_config_target()` canonicalises the deepest existing
  ancestor and re-appends the not-yet-created tail (so the check cannot be
  fooled by `..` or a symlink, without requiring the target to exist), then
  `check_config_path_policy()` applies the `.json` + `/proc/` `/sys/` `/dev/`
  rules to that resolved path. The rules are **not** re-derived per call branch:
  R5-S-9 applied them to the canonical path on the "file exists" branch and to
  the *raw string* on the other one, so `-c /tmp/x/../../../dev/shm/a.json`
  passed while the same target spelled directly was rejected — `stat()` resolves
  the `..` components the raw string still contained.
  It is called for **every** path, not only an explicit `-c` — `find_config_path()`
  derives the default from `XDG_CONFIG_HOME`/`SUDO_USER`/`HOME`, and that path used
  to skip validation entirely, so the same target was rejected with `-c` and
  accepted via `XDG_CONFIG_HOME`. Only the "parent directory must exist" rule
  stays exclusive to `-c` (`require_existing_parent`), because
  `~/.config/rawaccel/` legitimately does not exist on first run.
  Scope boundary: the policy is a *prefix* comparison and is not
  path-component-aware, so a directory literally named `dev` under a user
  directory is still allowed — same as before the fix; what the fix removed is
  the bypass, not a policy change. Gated by the SEC-2 config-path block in
  `tests/run_tests.sh` against the real binaries (both copies, checked
  separately); traversal depth is computed and verified with `readlink -f`, and
  a missing `/dev/shm` fails the gate rather than skipping it.
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
- **A missing config field falls back to the STRUCT DEFAULT, not to 0**: the
  defaults live in `device_config` (`include/config.hpp:24-28`, e.g. `dpi = 800`)
  and `load_config` simply does not assign when `j.contains("dpi")` is false, so
  "no key" and "key present but unusable" converge on the same value — which is
  why the type guards above can degrade instead of throwing. The fallback test
  asserted `dpi == 0 || dpi >= 0`, which is logically just `dpi >= 0`: it
  accepted 1, 800 and 99999 alike and could not detect the defaults being
  dropped, and its comment claimed the default was 0, which it is not. Any test
  of "falls back to the default" must compare against `device_config{}` — a
  literal pins the number and breaks when the default is retuned, a range
  check does not test the fallback at all. Note `sanitize` can move the
  observed value off the default (`if (dc.dpi < 1) dc.dpi = 1`), so a PC that
  writes 0 for a missing field lands on 1, not 0.
- **Config type guards**: on JSON load, scalar/string fields (`mode`, `gain`, `cap_mode`, `active_profile`, `use_raw_input`, `device_id`, `name`) are type-checked (`is_boolean`/`is_string` or a length-limited getter); `device_id` and `name` are capped at 256 chars; malformed types degrade to defaults instead of throwing (P54-B4)
- **CLI config safety**: `safe_save` writes atomically with a `.bak` rotate (the previous config is rotated to `path.bak` before the atomic tmp+rename+fsync overwrite) and exits cleanly (no SIGABRT) on I/O errors; a missing command argument reports a targeted error; a trailing bare `-c` is reported; an existing-but-corrupt config is never overwritten (P42, P82-MED-2)
- **CLI `--no-daemon` flag**: mutating commands (create, set, set-param, rename, duplicate, delete, import, create-preset) save the config locally and push it to the daemon by default; `rawaccel-cli --no-daemon` (or `--dry-run`) skips the daemon push so one-shot edits to a `-c /tmp/...` config never touch the live daemon config or /etc/rawaccel/settings.json. Apply later with `rawaccel-cli reload` (P82-CRIT-1)
- **Daemon option parsing**: `--config=PATH` / `--log-format=FMT` (`=` forms) are accepted next to `-c PATH` / `--config PATH`; a missing value is a hard parse error (exit 1); explicit `--config=` paths receive the same validation as `-c` (P53)
- **JSON log escaping**: `--log-format json` escapes log `message` strings (`\" \\ \n \r \t \b \f`, control chars → `\uXXXX`) so device names/paths/errno text can never corrupt the log stream (P53)
- **Subnormal time guard**: `modifier::modify()` clamps `ips_factor` to `IPS_FACTOR_MAX` (1e6) when `dpi_factor/time` overflows to Inf (subnormal time values like 1e-309)
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
- **exp2-based EMA (PERF, hot path, P0)**: the per-event `std::pow(base, time)` of the input/scale/output smoothers (2 or 4 calls per smooth, ~60–100 ns each) was the dominant smoothing cost. `init_coeff` now precomputes `log2(base)` once (two extra doubles per smoother) and each `smooth()` evaluates `exp2(log2(base) * time)` — the same result to ~1 ULP with a fraction of the cost. `exp2(-inf * t) == 0` for t>0 reproduces `pow(0, t) == 0` exactly, and `time == 0` gives `exp2(0) == 1` → coefficient 0, matching the old `pow(c, 0) == 1`. The branchless form also keeps the inlined smoother small for GCC's inline budget (a guarded ternary variant made GCC outline `std::visit`'s `__do_visit` as an `.isra.0` call in the non-smoothing apply path). Measured: power-dual+4ema ≈553 → ≈390 ns/event.
- **Switch dispatch for `accel_union::apply` (PERF, hot path, P0)**: replaced `std::visit` with an explicit `switch (impl.index())` + `std::get<N>` so the tiny per-event dispatch has no template machinery and the loop-invariant active index lets the inliner prune every branch but the active one.
- **Poll-rate median throttle (PERF, hot path)**: the `real_polling_rate` median (copy + insertion sort + divide) used to be recomputed on EVERY motion frame (~40–80 ns, 1–2× the classic no-smoothing cost). It now recomputes at most once per `POLL_RATE_SAMPLES` frames (`poll_median_tick`); the first 4 samples still emit an estimate in the first ~50 ms so SM-4 / the status JSON converge as before.
- **Y-axis unlinked field sync**: when X/Y axes are unlinked, fields without dedicated Y widgets (cap_mode, exponent_power, decay_rate, scale, output_offset, motivity, gamma, smooth, sync_speed) are copied from X to prevent stale values
- **Widget sensitivity refactor**: raw passthrough grey-out logic extracted to `update_raw_sensitivity()` — single source of truth for 18 widget enable/disable calls
- **GUI language resolution**: header-bar dropdown persists `auto`/`en`/`tr` to `<config_dir>/gui_lang`; an explicit preference wins (`load_lang_override`), otherwise `LANG`/`setlocale` decides (`sys_locale_is_turkish`). `tr()` returns the Turkish rendering only when the resolved language is Turkish — English is the dictionary key itself, so missing entries degrade to the source string. `refresh_language()` re-applies every registered widget in place on switch.
- **Low-latency motion contract (player focus)**: one loop thread processes every motion event synchronously (evdev read → modifier → uinput write); the epoll 10 ms timeout only serves housekeeping (hot-plug, IPC, signals). No per-event allocation; `mouse_device::dpi_factor` is pre-computed; both timers use `CLOCK_MONOTONIC_RAW`. Measure with the per-device `lat_stats` histogram (µs) via `rawaccel-cli latency` (SIGUSR1 → `snapshot_and_reset`); Min/Avg/p50/p95/p99/Max come from histogram bucket midpoints. p50/avg ≈ 1/3 of a 125 µs frame budget on hardware today; the rare p99/max queue spikes are the P-round analysis target.
- **CLI `monitor` (live telemetry)**: `rawaccel-cli monitor [interval-ms]` (default 500, domain 1–60000) polls the daemon `status` JSON and renders a per-device table — input/output IPS, gain, latency p50/p95 (µs), `real_polling_rate`, battery, plus a staleness marker from `telem_wall_ms`. Interactive TTY mode redraws in place; piped/redirected output is a timestamped line stream (first line = header, so `monitor | head` just works and SIGPIPE terminates it). Reads no config file (dispatch sits with reload/stop before config load) and exits 0 on Ctrl-C (SIGINT handler sets a flag; the inter-cycle wait is a deadline-loop of EINTR-retried `nanosleep` so stray signals can never truncate the interval).
- **CLI `diff` (profile comparison)**: `rawaccel-cli diff <a> <b>` compares two profiles — either a profile name in the config or a JSON file (the `export` format, including `export`'s line-stream). Prints only the fields that differ, using the same `accel_args` epsilon (`1e-9` doubles, `1e-5` LUT data) so round-trip precision noise never shows as a fake diff; exit 0 = identical, 1 = at least one difference. Read-only: never touches the config file, the daemon, or LUT contents. CLI gate tests in `tests/run_tests.sh`.
- **GUI mouse-test latency readout**: the Mouse Lock Test window (`gui/mouse_test.inl`) now shows live per-event processor latency (p50/p95 µs) and the real poll rate alongside in/out/gain, read from the same daemon status slice (`lat_*`, `real_polling_rate`). Blanked on every no-sample path via a shared `mouse_test_blank_values()` helper.

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
