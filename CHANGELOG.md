# Changelog

All notable changes to **rawaccel-linux** are documented here.

The canonical version string lives in `include/rawaccel-base.hpp`
(`RAWACCEL_VERSION`) and must stay in sync with `CMakeLists.txt` and
`packaging/PKGBUILD` — bump all three together.

## [1.2.5] — 2026-10-01

Sürüm notundaki abartı düzeltildi: 1.2.4 "kurulu bir sistemde doğrulandı,
bilinen bir kusur yok" diyordu. İkisi de doğrulanamıyordu (aşağıda). Bu
sürüm yedi kapının yeşil olduğunu **yalnızca doğru ifade eder**, ve kullanıma
hazırlık iddiasını taşımaz.

### Fixed: the GUI stale-config guard was dead code on Linux (STALE-1)

`gui/main.cpp` loads the config once at startup and `save_config_now()` writes
`S->config` back in full, so any edit made since — by `rawaccel-cli`, a second
GUI, or the daemon persisting an IPC `set_config` to `/etc` — was silently
discarded with nothing in any log. The fix records the file mtime at load and
refuses the save when the file moved underneath.

**The first version of that guard did nothing.** It used `-1` as the
"unreadable" sentinel and tested `stamp >= 0` to mean "the read succeeded".
`fs::file_time_type` is not signed-positive, and on libstdc++/Linux it is not:
measured `fs::last_write_time()` on a live file returns
`-4646858292110006316`, i.e. **every real mtime is negative**. So
`config_mtime_loaded >= 0` never held, the guard was never entered, and the
silent clobber it was written to prevent still happened. Found by
`olcum/aj1/stale1_proof.cpp`, which printed two *different* stamps and then
still reported "mtime aynı". Validity is now decided by comparing against a
sentinel no real mtime can equal (`CONFIG_STAMP_UNREADABLE`), never by a sign
test. Proof: without the guard the CLI's `mode=power` is overwritten back to
`mode=classic`; with it, the save is refused and `mode=power` survives.

The guard is **detect-and-warn, not auto-reload**: reloading would discard the
user's own unsaved widget edits, which is a strictly worse data-loss path.
`config_mtime_saved` was removed — it had no reader.

### Fixed: `run_tests_asan.sh` was conditionally green

Two sources, both measured: the binary lived at a **shared** path
(`build-manual/test_accel_asan`), so a second copy overwrote a running one
(`ETXTBSY` → rc 126), and `TMPDIR` was never exported, so both copies used the
same fixed `/tmp/test_p99_c_dir` and one copy's `remove_all` threw under the
other (`SIGABRT` → rc 134). Each run now gets its own `TMPDIR` and its own
binary. The cleanup gained a delete-before-verify guard, because during
mutation testing the trap was reached with an empty variable and expanded to
`rm -rf /tmp` — the proof harness destroyed its own evidence, and the failure
was initially misattributed to a server restart. That guard exists in both
`run_tests.sh` and `run_tests_asan.sh`.

### Fixed: a skipped perf gate in CI looked identical to a passing one

All four SKIP paths in `.github/workflows/ci.yml` were `echo` + `exit 0`.
`exit 0` is the right decision — an unusable host is not a regression, and
failing the pipeline for it would teach everyone to ignore the job — but on
its own it renders as a **green tick**: the appearance of "verified" for a gate
that measured nothing. That is the class this repo forbids elsewhere
(`run_cli_sanitized.sh:20` and `run_simd_parity.sh` both "skip loudly, never
quietly"). Every SKIP path now also emits a `::warning::` annotation, which
GitHub shows as a yellow warning on the job summary, while a genuinely measured
pass stays a plain green tick — so "green = actually measured" is readable from
the UI.

Proven, not asserted: `olcum/aj1/perf_gate_gorunurluk.sh` extracts the **real**
`run: |` block from `ci.yml` (it is not hand-copied) and drives all four paths
with stub bench scripts. On the fixed tree all four match expectation. Mutated
back to the pre-fix file, the 77 and 126 paths come out `annotation=(none)` —
**silently green** — and the tool exits 1. Raw numbers:
`olcum/aj1/perf_gate_kanit.txt`.

### Fixed: perf gate measured against a load average that never applied

`scripts/bench_hotpath.sh` guarded on `loadavg`, which on this host sat at
144–158 permanently, so the guard never fired. It now runs a real control
config (`noaccel`, named in `tests/perf_baseline.json`) and compares it to
`_meta.threshold_percent`, exiting 77 when the host is too busy to measure.
A positive control is what exposed the original number: with the control
active, `noaccel` measures **−0.40 %**; without it the harness reported a
**+84 %** false regression on that same config.

The two `_meta` fields that record the control (`control_config`,
`control_max_percent`) were **written but never read** — dead data. They are now
consumed, validated, and rejected if unknown, and `control_max_percent` is
required to be *below* `threshold_percent` (a control that fires only after the
gate it guards is not a control). Verified by three mutations: an unknown
control config and an over-threshold value both fail; a control too small to
trip fails with exit 77, which proves the baseline is really being read.

### Fixed: `use_raw_input` was documented backwards nowhere, i.e. not at all

The name is inverted with respect to behaviour: `true` lets the daemon
intercept and accelerate the device, `false` makes it skip the device entirely
via `ioctl(EVIOCGRAB, 0)`. Nothing said so. Now documented in
`rawaccel-cli --help` and in the README's Global switches table. Distinct from
the per-profile `raw` / `raw_passthrough` (default `false`), which bypass only
one profile.

### Measured: the acceleration curve is provably monotonic (not a defect)

Live telemetry on the physical G502 after the user moved the mouse reported
gain 1.265–1.581 against a `limit` of 1.8, but the sampled points were **not
monotonic**, which looked like a bug. It is not:

- The isolated `classic::operator()` curve over 0.001–40 ips at 40 000 points:
  39 999 increasing steps, **0** decreasing steps. The curve is smooth.
- All `speed_processor` half-lives in the live profile are `0.0`, so the EMA
  and trend blocks are inert — the smoothing hypothesis is excluded by
  configuration, not by argument.
- The reported `telem_gain` is `Σout/Σin` over a telemetry window, while the
  curve is applied per event at instantaneous speed. Real mouse input is
  bursty, so the windowed ratio is an x-weighted average of per-event gains and
  is not required to be monotonic. `olcum/aj1/dalgalanma_kaynak.cpp` reproduces
  the observed 1.27–1.58 band from lognormal event speeds (μ 12–45 ips,
  CV 0.8–2.5) with no defect assumed.

A side result worth keeping: across that whole sweep the p98 gain never
exceeded the configured `limit` (max 1.7966), so the cap holds under input
variance far beyond normal use.

### Reverted: denormal deadband (PERF6) — no gain, measured cost

`kill_denormal()` on the trend accumulators measured **+4.08 ns/event
(+1.37 %)** over 5+5 interleaved runs: a regression, not a speedup. The
deadband only flushed the *stored* trend value; the subnormals that matter are
produced in the intermediate `x *= trendDampening` accumulation, so the
intervention missed them while adding four `fabs`+compare per call. Reverted.

The subnormals are real and worth recording where they come from, because the
first explanation was wrong. `0.75^N` does **not** underflow to zero — measured:
still `4.0e-203` at N = 1620, and not zero at N = 5000. A microscopic count
per candidate path (`olcum/aj1/subnormal_nerede.cpp`, 200 000 iterations) puts
them in the accumulation (`76 850` subnormal results) and the `windowTotal`
sum (`5 650`), first reached after **2 462** calls — about 2.46 s of no mouse
movement — and **not** in the `1 - exp2(log2(0.75)·t)` coefficient, which never
goes subnormal because it tends to 1.0. Turning on FTZ/DAZ drops the hot-path
cost from ~151 ns to ~15 ns per call (−90 %, 3 runs each), so a global FTZ/DAZ
remains the real lever; its measured numerical effect is 0.0032 % of 100 ips and
only after that 2.46 s of stillness.

### Still not verified — do not read the green gates as "ready to ship"

- **No CI run exists for any commit.** Every workflow run dies in ~5 s with
  "The job was not started because your account is locked due to a billing
  issue." This is an account problem, not a code problem, but it means nothing
  pushed here has been verified on GitHub.
- **The installed binaries are stale.** `/usr/bin/rawaccel-gui` predates the
  stale-config fix (verified by string search: the new warning text is absent
  from the installed binary and present in the fresh build). Reinstalling
  requires root, which this environment does not have.
- **The seven gates are not a sanitized run of `test_accel.cpp`.** Per
  `AGENTS.md`, that translation unit is sanitized only by the separate
  `run_tests_asan.sh`, which is not one of the seven. The `sanitizers` CI job
  is a different workflow that has never executed.
- **Clean install is unexercised**: `setup.sh` needs root.

## [1.2.4] — 2026-09-30

Kullanıma hazır sürüm: yedi kapının tamamı yeşil, kurulu sistemde de.

### Fixed: gate 2 turned red whenever the product was installed

`tests/run_tests.sh` gave each run its own `XDG_RUNTIME_DIR`, but that only
changes the PID file the daemon *writes* — `daemon/main.cpp:458-459` checks
liveness against the **union** of three candidates
(`$XDG_RUNTIME_DIR` || `/run` || `/tmp`), deliberately, so a daemon started
without `XDG_RUNTIME_DIR` is still caught. With a root-owned live
`/run/rawaccel.pid`, the test's own daemon refused to start with
"Another instance may already be running" and the gate failed for the wrong
reason. The fix isolates the test environment (`unshare -Urm` with an empty
tmpfs over `/run`, falling back to `bwrap`; if neither is available the gate
FAILS loudly rather than skipping silently). Scope is `/run` only — masking
`/tmp` breaks the legitimate-config-path case.

### Fixed: gate 6 depended on whether the product was installed

`tests/run_cli_sanitized.sh` tested "config directory does not exist" against
the hardcoded system path `/etc/rawaccel/settings.json`. Once installed, that
directory exists, so the case's precondition was unsatisfiable and the gate
read red on a correct build — and green on an uninstalled one. Now uses a
deliberately absent directory under the test's own `$WORK`.

### Reverted: exp2 coefficient cache (PERF1)

Measured, not assumed. 5+5 interleaved runs: `power-dual+4ema` −1.04 %,
`classic` **+0.59 % regression**; an independent 3-run measurement gave
−1.05 ns (−0.35 %), i.e. inside noise. Cause: `linear_ema_smoother` carries
5 cache fields × 2 axes = 20 live doubles → x86 register spill. The oracle is
bit-identical after the revert (1408 rows / 79 documented deviations).

### Measured: denormal stall in the trend accumulator

`windowTrendTotal` / `cutoffTrendTotal` decay geometrically (`× 0.75`) and
never reach exactly zero, sticking at the smallest subnormal. Same binary,
only the CPU's FTZ/DAZ bits toggled: **145.11 → 13.23 ns/call (−91 %)**.
Measurement tools live in `olcum/aj4/` and `olcum/aj5/` and do not touch
production code.

### Gates

All seven green with the daemon installed and running:
build (0 warnings) · 34164/34164 · oracle 1408 rows / 79 deviations ·
SIMD parity · tr coverage · CLI ASan/UBSan 31/31 · tracker bridge.

## [1.2.3] — 2026-09-13

### Fixed: asymmetric hardware DPI (X ≠ Y) after profile apply

**Bug (DPIFIX-1):** `set_dpi()` wrote the requested DPI to the **X** axis and
PRESERVED whatever Y was currently set to whenever the device supports a
separate Y DPI (`supports_y`). Every profile apply — daemon start and the GUI
hardware panel — passes a single DPI value, so on dual-axis devices a stale
Y left the cursor anisotropic. On the PRO X 2 this produced **X=400 /
Y=1600**: vertically moving the physical mouse the same distance moved the
cursor four times farther than horizontally, in every mode including Raw
Passthrough ("Ham Geçiş") because the hardware already reported different
counts per centimetre per axis.

- `src/logitech_hidpp.cpp` — `set_dpi()` now writes the same DPI to both axes
  (symmetric single-value set, matching Solaar's `dpi_extended` semantics).
  There is no X-only caller in the app; the previous "keep Y" behaviour had no
  UI purpose and only generated the asymmetry.

**Reported:** Abrek34 — "mouse ham geçiş tıklı ama sağa/sola ile aşağı/yukarı
hareketler eşit değil". Diagnosed live on the PRO X 2 (`GetDpi` read)
before/after the fix; hardware verified back to symmetric X=Y=400.

## [1.2.2] — 2026-09-13

### Fixed
- **LOD off-by-one (PRO X 2 / Hidpp 0x2202).** HID++ 0x2202 ve 0x8100 profil
  DPI satırı, kalkış mesafesini **1=Low, 2=Medium, 3=High** olarak kodluyor
  (OpenLogi `Lod` ve donanımda doğrulanmış G HUB/Onboard Memory Manager
  dökümleri: "Medium→Low" `02→01`, "Low→High" `01→03`). Değer **0 bir seviye
  değil** — "destek yok / satır boş" sentinel'idir ve firmware yazımı
  reddeder. Eski Solaar-mirası `{0=Low,1=Medium,2=High}` eşlemesi:
  - `hidpp-set-lod low` byte `0` gönderiyordu → cihaz "not supported"
    döndürüyordu, düşük LOD asla ayarlanamıyordu;
  - okuma da bir basamak kayık gösteriyordu (gerçek "High"(3) hiç
    gösterilemiyor, orta değerde yanlış etiket basılıyordu).
  - `hidpp_lift_off_distance` enum'ı `{low=1, medium=2, high=3}`'e çekildi;
    `set_dpi` LOD baytını 1..3'e clamp'liyor, `get/set_lift_off_distance`
    yalnız 1..3'ü geçerli sayıyor (0/0xFF çöpü "desteklenmiyor" olarak
    gösteriliyor); CLI dump ve GUI paneli (combo seçimi, durum satırı,
    apply) senkron düzeltildi.
  - Doğrulama (PRO X 2, `40A9`, Unifying `046d:c54d`): low/medium/high
    yazımı + okuması artık çalışıyor; yalnız byte 0 reddediliyor.
- `set_dpi` artık geçersiz LOD baytı varken tüm DPI yazımını düşürmüyor:
  bayt 1..3'e clamp'lenerek cihazın DPI değişimini de kabul etmesi
  sağlanıyor (PRO X 2'ye LOD=0'lı bir yazım bütün yazımı reddettiriyordu).

## [1.2.1] — 2026-09-13

### Added
- **HID++ ayarları etkinleştirildi.** Onboard-profile modunda çalışan gaming
  farelerde (PRO X 2 / G Pro X vb.) DPI / polling-rate / LOD yazmaları HID++
  tarafından reddediliyordu; yazmadan önce onboard modu kapatılıyor
  (`disable_onboard_profiles_for_write`).
- `setup.sh` artık `hidraw` alt sistemini de tetikliyor; HID++ hidraw
  düğümlerine udev kuralları paket kurulumunda anında uygulanıyor.

### Fixed
- **MATH-1:** negatif ivmelenme + aktif cap (`cap.y > 0`) dejenere eğri
  üretiyordu (cap.x negatife dönüyor, gain eğrisi geriye gidiyordu). Bu
  kombinasyon artık identity eğrisine düşürülüyor.
- Test corpus (`tests/corpus_accel`, `tests/corpus_config`) haznelere eklendi.

## [1.2.0] — 2026-09-13

### Fixed
- **HID++ ayarları (donanım paneli düzeltildi).** Wired G502 HERO SE gibi
  HID++ 2.0 cihazlarda DPI / polling-rate / LOD ayarları cihaza yazılamıyordu:
  - `ROOT.GetFeature`, `FEATURE_SET.GetCount` ve `resolve_feature_index`
    artık `send_short` yerine `send_feature_request` kullanıyor. Bu cihazlar
    kısa `0x10` isteklerine **uzun `0x11`** raporuyla cevap veriyor;
    `send_short` yalnızca kısa yanıtı çözebildiği için özellik keşfi boş
    dönüyor, yazma yolları (`set_dpi` / `set_polling_rate`) reddediliyordu.
    Artık özellik keşfi (feature yağı), DPI listesi, mevcut DPI, polling rate
    ve HID++ panel kontrolleri tam çalışıyor.
  - `0x2201` (non-extended) `AdjustableDpi` **SetDPI fonksiyon kodu `0x03`'e
    düzeltildi** (OpenLogi `set_sensor_dpi`, `[sensor_index, dpi_hi, dpi_lo]`).
    Önceki `0x01` aslında DPI *listesini* (GetSensorDpiList) okuyan fonksiyondu;
    değer gönderse de hızı değiştirmiyordu. Doğrulama (G502 HERO SE,
    `046d:c08b`): 2400 → 400 → 2400 DPI ve 1000 → 500 → 125 → 1000 Hz yazımları
    yazıldıktan hemen sonra okunarak teyit edildi.
  - `gui/hidpp_panel.inl` `hw_query_thread` içindeki `g_idle_add` lambda'sının
    eksik `});` kapanışı onarıldı (derleme hatası).
  - **Centurion batarya `/0x0104` desteği:** PRO X 2 LIGHTSPEED, G515 LS TKL
    gibi cihazların [soc, soc_duplicate, charging_status] dizilimli 3-bayt pil
    yükü artık `UNIFIED_BATTERY`'den önce deneniyor.
- GUI HID++ uygulama sonucu artık yazıldıktan sonra cihazdan yeniden okunarak
  durum satırına yansıtılıyor; "kabul edildi ama uygulanmadı" görüntüsü kalmadı.
  Eski açık GUI process'i bellekteki eski binary'yi kullandığı için "(reddedildi)"
  gösterebilir — kapatıp yeniden açın.

## [1.1.0] — 2026-09-11

### Added
- End-to-end daemon test (`tests/e2e_harness.cpp` + `tests/run_e2e.sh`): drives the
  REAL daemon against a synthetic uinput mouse and a virtual sink. Accel phase
  checks frame/SYN structure, SM-2 button buffering, LOW-1 coalesced deferral, and
  ×3 classic-linear gain (T-A1..T-A4); raw phase verifies byte-identical 1:1
  passthrough (T-B1). Needs root + `/dev/uinput`; not run in CI.

### Changed
- P93-BATCH (hot path): `process_device()` accumulates each frame — motion REL,
  queued non-motion events, and the closing SYN_REPORT — in a stack `write_batch`
  and submits it in a SINGLE `write()` syscall at the real SYN_REPORT. With the
  companion 32-event batched evdev reads, the canonical per-motion-frame cost is
  1 `read()` + 1 `write()` + 2×`clock_gettime` (was ~3-8 syscalls).
- CFG-1: `output_dpi = 0` now means "no output-DPI normalization" (1:1 counts)
  instead of being clamped to 1 (which silently produced a near-dead cursor).
  `sanitize_device_profile()` and the CLI `set-param` domain/`check` warning now
  treat 0 as a valid sentinel.

### Fixed
- E2E harness debug: flushed whole-batches produce exactly one SYN per frame (no
  duplicated trailing SYN).

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
