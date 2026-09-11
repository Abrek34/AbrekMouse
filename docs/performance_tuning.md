# Performance Tuning Guide

Everything RawAccel Linux does per mouse event happens in one daemon loop
thread, and every measurement in this project says the same thing:

> **The acceleration math is a rounding error. The real cost is kernel
> syscalls (evdev read / uinput write), and even that is a tiny fraction of
> one polling frame.**

This document collects the numbers behind that claim — the hot-path syscall
model, where time actually goes, the latency measurement workflow, build-time
choices (native vs portable), and what the *settings you can feel* (polling,
DPI, smoothing) do to responsiveness. It is the user-facing companion to the
developer-side contract in `AGENTS.md` ("Low-latency motion contract") and the
research notes in `docs/research/precision.md` (§8).

---

## 1. The hot path: 4 syscalls per motion frame (2× clock + 1 read + 1 write)

The canonical per-motion-frame cost (see `AGENTS.md`, "Low-latency motion
contract") now includes the batched evdev read + the P93-BATCH single-output write:

1. **1 batched `read()`** — one or several of a 32-`input_event` batch comes
   off the evdev node; a typical 3-event frame (REL_X, REL_Y, SYN) costs one read.
2. **`clock_gettime(CLOCK_MONOTONIC_RAW)` #1** — at `flush_motion()` entry:
   starts the interval and latency measurement (vDSO, no kernel round-trip).
3. **Math + EMA** (a few ns — §2) producing the accelerated `out_x/out_y`.
4. **1 batched `write()`** — the whole frame (motion REL, any queued buttons,
   and the closing SYN_REPORT) is accumulated in a stack `write_batch`
   (`daemon/daemon.cpp`) and submitted in **one** write at the real SYN_REPORT
   (P93-BATCH). The kernel uinput driver injects every event found in the
   buffer, so the stream is byte-identical — while collapsing (typically 3+)
   syscalls into 1. A full 16-event buffer flushes in place (nothing dropped).
5. **`clock_gettime` #2** — at the end, to compute the measured µs.

That is the canonical **4 hot-path syscalls per motion frame** (1 read + 1
write + 2× clock reads). Everything outside the read/write is a thin epoll
cycle whose 10 ms timeout is **housekeeping only** (hot-plug, IPC, signals) —
motion events are processed synchronously as they arrive, never delayed to the
timeout. All the syscalls are kernel-bound; the math is `uinput_write_rel()`-free
and allocation-free, `dpi_factor` is pre-computed once per profile, and both
timers share the same `CLOCK_MONOTONIC_RAW` source.

## 2. Where the microseconds actually go

Independent measurements (three runs, two tools):

| Layer | Cost per event | Verdict |
|-------|---------------|---------|
| Accel math + EMA smooths (modifier) | **20–425 ns** (loose config: 19.8 ns noaccel, 37.9 ns power, 425 ns worst dual-axis + 4 EMA) | not the bottleneck |
| Subpixel accumulation + `apply_motion_math` | ~43 ns | negligible |
| Kernel read (evdev) | p50 ~7.5 µs (VM; lower on real hardware) | syscall-dominated |
| Kernel write (uinput delivery) | p50 ~34 µs grab handoff in VM, ~25–35 µs kernel steady-state on hardware | the dominant term |

Even the worst reachable configuration (both axes + four EMA smooths)
consumes **~0.34% of a CPU core at 8000 Hz** — a ~300× margin below the
125 µs frame budget. R50's independent oracle micro-benchmark (35
cycles ≈ 10 ns per `apply()` over the shared oracle grid, `tests/oracle/
oracle_perf`) lands in that same band from a third tool. The R52 fix round
(P155/P156/P157) was measured against the R50 baseline under
`scripts/bench_hotpath.sh`: all 6 configs within **+0.6%** (worst:
power-whole +0.61%, dual+4EMA +0.32%, classic +0.46%, noaccel −0.87%);
the accuracy/CLI/GUI fixes are perf-neutral and live p50 stayed ≈ 2.25 µs.
There is **no SIMD** in the hot path and none is needed:
the pipeline is scalar double-precision, and its cost is invisible next to
syscalls. `-march=native`'s only remotely relevant effect is FMA contraction
(compiler-chosen, default `-ffp-contract=fast`), which touches accuracy
margins, not latency — see `docs/research/precision.md`.

So the performance story is: **make sure the daemon is the only accelerator**
(KDE/GNOME flat fix — double-acceleration feels like lag/speed, and is the #1
"it feels slower than Windows" report) and **keep your polling/DPI/smoothing
within the frame budget in §3**.

## 3. The settings that matter

### 3.1 Polling rate — frame budget

The daemon's per-event cost is a few µs. The question is how much of the
*polling frame* that eats:

| Polling rate | Frame budget | p50 1–3 µs as % of frame | headroom |
|--------------|-------------|---------------------------|----------|
| 125 Hz       | 8000 µs     | ~0.02–0.04%               | enormous |
| 500 Hz       | 2000 µs     | ~0.05–0.15%               | enormous |
| 1000 Hz (default) | 1000 µs | ~0.1–0.3%            | enormous |
| 2000 Hz      | 500 µs      | ~0.2–0.6%                 | huge |
| 4000 Hz      | 250 µs      | ~0.4–1.2%                 | huge |
| 8000 Hz      | 125 µs      | ~0.8–2.4%                 | 40×+ |

Higher polling means more frames per second and therefore more total syscalls
per second, but each frame's cost is unchanged — the daemon keeps up linearly.
Reported gamut is **125–8000 Hz** (`POLL_RATE_MIN`/`POLL_RATE_MAX`), default
1000 Hz; profile field `polling_rate`, set with
`rawaccel-cli set-param <profile> polling_rate N`.

**Tuning rule:** choose the polling rate your mouse natively supports. There
is no latency win from *raising* the daemon's declared value above the sensor's
real rate (the ips normalization uses it), only from matching reality — a
wrong value makes `status`/telemetry ips math inaccurate.

### 3.2 DPI — curve fidelity, not latency

The accel curve is **DPI-agnostic by design**: raw counts are normalized to
inches-first speed (`input_ips = (counts/s) · (1000 / device_dpi)`,
`NORMALIZED_DPI = 1000`) before the curve applies, and output counts are
renormalized by `output_dpi` afterwards. Result: for the same physical hand
movement, gain and on-screen distance are identical at 400 / 800 / 1600 DPI
**as long as** the profile's `dpi` matches the mouse's real hardware setting
(validated live by the DPI ladder in `docs/real_hardware_test.md` §3.2).

What DPI *does* change is **granularity**: higher DPI → more counts per unit
time → finer subdivision of the input signal, so the slow-speed register and
the subpixel accumulator operate with less integer quantization. That is an
*accuracy/fidelity* benefit, not a speed benefit — the per-event math cost is
identical. Practical guidance:

- Keep `dpi` synced to your mouse's real setting (`rawaccel-cli status` shows
  what the daemon reads).
- Prefer the sensor's **native** DPI step; avoid DPI steps where the sensor
  interpolates/smooths (that reads as "washed out" — hardware behavior, not an
  accel bug).
- Don't chase "more DPI = faster" — on-screen speed is normalized; a suddenly
  faster/slower profile at one DPI is a **profile DPI mismatch or sensor
  smoothing**, not the curve (see the ladder's result table).

### 3.3 Smoothing halflifes — the *only* "feel latency" lever

In `synchronous` mode, the input speed smooth (`input_speed_smooth_halflife`)
is a **low-pass filter on the speed used by the curve**, and its settling
time *is* perceivable lag. Measured (precision.md §8.3, P118):

| input halflife | settle 95% | settle 99% | perceivable wrong-gain window |
|----------------|-----------|-----------|-------------------------------|
| 0 (default)    | 0         | 0         | none — instant |
| 10 ms          | ~14 ms    | ~18–35 ms | **~19 ms** (≈ one 60 Hz frame, near-imperceptible) |
| 100 ms         | ~148 ms   | ~243–383 ms | **~190 ms** of wrong gain at the knee (floaty/gliding) |

Key facts:

- Halflife is the entire story for synchronous responsiveness — 10 ms ≈ one
  frame, 100 ms ≈ "floaty". Halving the halflife halves the muddy window.
- The EMA settle is ~3.5× the halflife (99% in ~383 ms for 100 ms).
- **A knee parked on your tracking speed multiplies the cost ~3.5×**:
  `sync_speed` sitting on your typical tracking speed turns the *same*
  halflife into a much longer wrong-gain window (100 ms: 42 ms away-from-knee
  vs 148 ms on-knee). "`sync_speed` is latency-neutral" is only true away from
  the operating speed.

**Latency-safest synchronous recipe** (from precision.md):

```
input_speed_smooth_halflife = 0      # or ≤ 10 ms, never 100 ms
scale_smooth_halflife / output_speed_smooth_halflife = small, if anything
smooth = 0.25–0.5, motivity ≤ 2, gamma = 1
sync_speed slightly OFF your tracking band (or on it with halflife = 0)
```

`natural`/`classic` use no speed-halflife smooth by default (all halflifes
default to 0 in every non-synchronous profile); the same halflife knobs exist
but only drag if you deliberately set them.

The halflife knobs have **no per-event math cost** in the daemon (an EMA add
is a handful of ns) — the cost is *feeling*, entirely inside the smoothing
window, which is why the table above talks in milliseconds, not µs.

### 3.4 Everything else

- **epoll 10 ms timeout**: housekeeping only — never delays motion, do not
  tune it.
- **Subpixel accumulation**: prevents micro-movement loss; costs ~nothing.
- **Cap / gain / modules**: curve-shape parameters change math *content*, not
  order of complexity — hot-path ns differences between modes are all inside
  the 20–50 ns ballpark (single-axis; worst-case dual-axis + 4 EMA ≈ 425 ns).

## 4. Latency: measure it

Per-device processing latency histogram (µs), captured on the daemon's hot
path (entry → modifier → last uinput write):

```bash
rawaccel-cli latency                 # IPC RPC first, SIGUSR1 fallback
journalctl -u rawaccel -n 30         # dump lands in the daemon log
```

- GUI: the **"Performance"** button in the status bar shows a live
  Avg/p50/p95/p99/Max readout from the daemon `status` JSON (same data).
- The histogram only fills while **accelerated** motion flows — on a
  `disable`/raw profile it stays empty by design.
- The dump resets counters, so repeat 3× while playing and take the middle
  value of each percentile (full protocol: `docs/real_hardware_test.md` §4).

**Live example** — an actual dump from a running daemon (R50 §P140 validation,
middle of three rounds; test mouse + VM):

```
=== RawAccel Processing Latency ===
  Device: P57 GameSpeed Test Mouse
    Samples  : 7158
    Min      : 0.62 µs
    Avg      : 2.29 µs
    p50      : 2.25 µs
    p95      : 3.25 µs
    p99      : 4.25 µs
    Max      : 96.14 µs
===================================
```

Mid-single-digit p50/p95 with an occasional p99 step — the shape the reference
rows below predict. The GUI **Performans** button served the *same* numbers
from the `status` JSON `lat_*` fields on the active profile's device slice
(p50 2.25, p95 3.25, p99 3.75 µs at recount), confirming button/label wiring:
`on_perf_clicked()` → `daemon_device_field()` → `latency_lbl`.

**What "good" looks like** (project reference runs, µs):

| Data set | p50 | p95 | p99 |
|----------|-----|-----|-----|
| P31 synthetic hot-path (VM, old tooling) | 33 | 86 | 266 |
| P57/P64/P73 live daemon + uinput (VM) | 1.75–2.25 | 2.75–3.75 | 3.75–5.25 |
| P94/P101 precision ramp (VM) | 1.75 | 3.25–3.75 | 4.75 |
| Real hardware target | **low single digits** | low single digits | < ~10 |

Mobile/VM jitter shows up as p95/p99 humps and rare `> 500 µs` Max spikes; on
real hardware p50/p95 land in the single digits — a p50 in the tens of µs
(without a KDE/GNOME double-accel config issue) is worth reporting.

### 4.1 Status / IPC path — live reads are now 8.2× cheaper under the lock

The latency readout lands in the daemon via the `status` JSON, which the
**IPC thread** prepares under `devices_mutex_` — a lock the hot-path loop
thinks about only through its seqlock. Reading a device slice while holding
that lock used to cost **471 ns** (percentile math included). R50's P136
narrowed it: under the lock the thread now only grabs a fast `lat.copy()`
snapshot, and the avg/p50/p95/p99/max math runs **after the lock is
released** — the lock-held read dropped to **57.7 ns (8.2×)**. The motion hot
path is untouched (it never took that mutex); this is a *readout-speed*
improvement, so the GUI **Performans** button and `rawaccel-cli status`
answer faster, at no cost to the per-event numbers in §2.

Reference costs measured alongside (P136): `lat.record()` 5.9 ns/event,
`clock_gettime` 16.4 ns/call (vDSO), mutex lock+unlock 4.5 ns — all far
inside the 125 µs budget at 8 kHz.

## 5. Build-time choices

| Choice | When | Effect |
|--------|------|--------|
| `bash scripts/build.sh` (default) | you build for the machine you're on | `-march=native`: fastest + same hardening; may FMA-contract (accuracy nuance, see precision.md) |
| `RAWACCEL_PORTABLE=1 bash scripts/build.sh` | you distribute/share the binary across CPUs | disables `-march=native`; generic x86-64 baseline; same hardening; **measurable difference in the hot path is negligible** because math is ns-scale, syscalls dominate |
| Arch/CachyOS package (`packaging/PKGBUILD`) | you install a packaged release | shipped with native `-march=native`, hardened, PIE; PGO **evaluated and not adopted** — ~0.4% gain, noise level (R50, Aj 4) |
| CI (`ci.yml`) | automated verification | builds portable + runs tests/oracle — confirms portability never regresses behavior |

Because the accel math is 20–50 ns/event (single-axis; worst dual-axis + 4 EMA
≈ 425 ns), native vs portable is a **~ns-level
difference per event on a µs-level total** — you cannot feel it. Pick portable
when you must run on unknown CPUs, native anywhere else (the CI already
guards both). PGO would target cold/startup code paths, not the hot loop.

PGO was actually tried (R50, Aj 4) with the production flag set
(`g++ -O3 -march=native`): `-fprofile-generate` → 120k `apply()` calls →
`-fprofile-use` (zero missing-profile warnings) gave 11.01 ns/apply vs
11.05 ns/apply plain — **~0.4%, inside run-to-run noise**, so PGO is not
recommended (build time + toolchain fragility buy no measurable hot-path
gain). The same tool cross-checked our implementation against the vendored
official RawAccel reference over the oracle grid: **35 cycles/apply (10.13 ns)
vs 40 cycles/apply (11.38 ns) → local is ~11% faster** — same math, fewer
init/object costs — another sign there is nothing left to squeeze in hot math.

## 6. Quick answers (FAQ)

- *"It feels slower/heavier than the same curve in Windows RawAccel."*
  → Most likely desktop double-acceleration or an in-game mouse-smoothing
  setting, not the pipeline. Run `bash scripts/kde-fix-accel.sh` (or the GNOME
  flat profile), disable in-game smoothing, then re-measure.
- *"My p99 jumped / occasional 500 µs spike."* → VM host jitter or a scheduler
  hiccup; recurring on real hardware is the flag to report. p99 < ~10 µs is fine.
- *"Higher DPI feels faster/slower."* → Fix the profile `dpi` to the real
  sensor value; the curve is DPI-agnostic (see §3.2).
- *"Should I lower the halflifes?"* → If you're on `synchronous` and feel
  floaty: halflife 100 ms → 10 ms or 0 (§3.3). That is the single biggest
  "latency" setting you control.
- *"Native or portable build?"* → Native if it's your own machine (default);
  you won't feel the difference.
- *"Where's the SIMD?"* → There isn't any and it isn't needed: the entire
  math is 20–50 ns/event in typical single-axis use (425 ns worst dual-axis
  + 4 EMA), dwarfed by the ~µs syscall cost. Adding SIMD would
  save nanoseconds on a µs budget (precision.md/learning.md agree).

## 7. Sources

- `AGENTS.md` — "Low-latency motion contract" (canonical 3-syscall count),
  "P93 batched REL write", "P100 unified clock".
- `daemon/daemon.cpp` — `flush_motion()`, `write_batch`, `now_ns()`.
- `docs/research/precision.md` — §8.3 responsive-table (halflife×knee),
  FMA/`-ffp-contract` notes, interaction-aware defaults.
- `docs/real_hardware_test.md` — §3.2 DPI ladder, §4 latency methodology +
  reference tables.
- `aihaberlesme.md` — T22a synthetic benchmark (ns/event per mode, %0.24 CPU),
  P31/P94/P101 hot-path measurements.