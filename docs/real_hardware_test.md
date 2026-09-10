# Real-Hardware Test Kit (P71 base + P94 expansion)

This document is the step-by-step protocol for validating RawAccel Linux's
"feel" and **processing latency** on **real hardware**. This VM contains only
virtual mice, so real feel must be assessed on your physical machine. Run this
session on the machine and mouse you actually game with.

> Play/test each profile long enough to judge it. Per README: "spend
> 30–60 min per config before judging" — for this fast A/B pass we use the
> compact 5–10 min window below, but lengthen it if you are unsure.

**What P94 added to this kit** (real-user validation screws): (a) A/B at
multiple mouse DPI settings so accel effects can be told apart from sensor
smoothing (Section 3.2); (b) a per-preset *expected behavior* table derived
from `include/presets.hpp` so you know what "normal" feels like at slow vs fast
flicks (Section 3.3); (c) a Windows/macOS → Linux "what changes" note (Section
3.4); (d) a step-by-step P57 latency methodology for real hardware with
p50/p95/p99 interpretation (Section 4).

---

## 1. Scope

We verify **5 presets** against their stated design intent, plus check latency
per profile:

| Preset | Mode | Design intent |
|--------|------|---------------|
| `gaming` | classic | General FPS: classic curve, limit 1.8 |
| `cs2` | classic | Tactical shooter: early kick-in, cap 1.6 |
| `valorant` | natural | Smooth entry/exit: light gain, high cap 2.0 |
| `apex` | power | Tracking + verticality: fast 180°, output offset 0.9 |
| `fps` | classic | Balanced FPS starting point (moderate accel + cap 1.8) |

`office`, `precision` and `disable` are low-intensity/utility presets and are
out of scope for the latency-focused feel pass (they are still covered by the
user guide).

---

## 2. Pre-flight checklist

Do this once before starting.

```bash
# 1. Install (canonical one-shot installer)
sudo bash setup.sh

# 2. Daemon running?
systemctl status rawaccel
# expect: active (running)

# 3. Note the default / active profile — a fresh install's "default" is a
#    noaccel (linear, no acceleration) profile; it is NOT raw passthrough.
rawaccel-cli list          # note the default / active profile
rawaccel-cli status        # confirm daemon sees your real mouse (DPI/polling)

# 4. Confirm your mouse's actual DPI + polling rate and that the *baseline*
#    we use below is strict raw 1:1 (the `disable` preset IS raw passthrough —
#    events forwarded 1:1). The fresh `default` profile is noaccel but still
#    runs output_DPI normalization, so it is linear, not literally passthrough.
#    If DPI/polling shown by `status` differ from your mouse's real numbers,
#    correct them — ips math is only accurate when they are right.
```

Double-acceleration check: if you are on **KDE**, ensure pointer acceleration
is flat (see README → "KDE Plasma Setup"):

```bash
bash scripts/kde-fix-accel.sh --check
```

> **GNOME users** get the same double-acceleration risk: set the mouse
> acceleration profile to *flat* (GNOME Settings → Mouse & Touchpad, or
> `gsettings set org.gnome.desktop.peripherals.mouse accel-profile flat`).

Create the profiles you will test (one-shot):

```bash
# Create from presets, then activate a raw 1:1 baseline for the session
rawaccel-cli create-preset gaming  p71-gaming
rawaccel-cli create-preset cs2     p71-cs2
rawaccel-cli create-preset valorant p71-valorant
rawaccel-cli create-preset apex    p71-apex
rawaccel-cli create-preset fps     p71-fps
rawaccel-cli create-preset disable p71-raw      # raw 1:1 reference
rawaccel-cli set p71-raw
```

The **raw 1:1 baseline** (`p71-raw`) is your reference point for every compare.

---

## 3. A/B test protocol

### 3.1 Per-preset feel pass

For each of the 5 presets, in order:

```bash
rawaccel-cli set p71-<preset>
```

Play **5–10 minutes** in your chosen game / aim trainer, then switch back to
the raw baseline and play 1 minute. Judge the differences.

**Focus tasks during each window:**

1. **Micro-aim** — slow corrections, **2–20 mm/s** of physical hand movement.
   - Watch for *sub-1:1 muddy micro-aim*: is the crosshair moving less than
     your hand at very low speed, feeling "stuck" or "washed out"? A good
     preset keeps slow corrections near 1:1 (or slightly above) — never below.
2. **Flicks** — fast 180° / panic flicks.
   - Watch for a *cap plateau feeling*: at high hand speed, does the cursor stop
     accelerating and feel like it hits a speed ceiling mid-flick?
3. **Tracking** — smooth, consistent-speed tracking.
   - Watch for *tracking smoothness*: any hitch, micro-stutter, or non-linear
     jump while following a moving target at constant hand speed.
4. **DPI mismatch** — does the on-screen sensitivity match what your hand does?
   - If a profile feels radically faster/slower than raw at *low* speed, flag a
     DPI/output_dpi mismatch rather than "an accel artifact".

**Record** a line per preset in the Feel Score table below.

### 3.2 DPI ladder A/B — separate accel from sensor smoothing

The accel curve is **DPI-agnostic by design**: the daemon converts raw counts
to *input speed in ips* with `input_ips = (counts/s) · (1000 / device_dpi)`
(`NORMALIZED_DPI = 1000`). For the same physical hand motion:

- high mouse DPI → more counts/sec → identical ips (because the profile DPI is
  raised to match) → **exactly the same gain**;
- output counts are then re-normalized by `output_dpi`, so on-screen distance
  for a given hand movement is also the same at any input DPI.

Consequence you can test on **your** hardware: if a preset behaves differently
at 400 / 800 / 1600 DPI, the culprit is **not the accel math** — it is either a
wrong profile DPI, or the *sensor's* own DPI-dependent behavior (smoothing,
interpolation, native resolution).

**Protocol (run per preset you care about, plus the raw baseline):**

1. Set your mouse's hardware DPI to **400** (button/software), then sync the
   profile:

   ```bash
   rawaccel-cli set-param p71-<preset> dpi 400
   rawaccel-cli set p71-<preset>
   ```

2. Do the 3 focus tasks (micro-aim / flick / tracking), 2–3 min each. Note
   *where* on the curve the gain seems to kick in and how the cap feels.
3. Repeat at **800** and **1600** (update `dpi` first). Keep the same game,
   sensitivity, resolution, and mousepad surface.
4. Compare. The accel *fingerprint* (kick-in point, curve shape, cap feel)
   should be **identical** across all three DPI settings.

**Reading the results:**

| Observation across 400/800/1600 | Meaning | What to do |
|----------------------------------|---------|-----------|
| Gain curve feels the same at all three DPI | True accel, DPI-correct | Nothing — proceed to Section 4 latency |
| Curve shifts with DPI (feels faster/slower per DPI even though DPI is synced) | Profile DPI mismatch, or accel misreading the count stream | Re-check `rawaccel-cli status` DPI vs the mouse's real setting; re-sync `dpi` |
| Only *one* DPI feels "washed out"/muddy/wobbly, others clean | Sensor smoothing / interpolation at that DPI (hardware) | Not an accel bug — avoid that DPI for gaming; re-test on the clean DPI |
| Sub-1:1 muddy micro-aim at every DPI | accel `input_offset` too high / output offset floor | Tune per `docs/user_guide.md`, re-run |

> The 400/800/1600 ladder is also a quick "is my profile DPI right?" sanity
> check before the full feel pass. If you only have time for one DPI, use the
> sensor's **native or most commonly shipped** step and keep it constant.

### 3.3 Per-preset expected behavior (slow vs fast flicks)

Values below are computed from `include/presets.hpp` `make_preset()` — the
**single source of truth** the CLI `create-preset` and the GUI dropdown both
use. If this table ever disagrees with what the release ships, fix the doc,
not the preset. Gain = multiplier on the raw count stream (1.00 = raw 1:1).
Speeds are *input* speed in ips (same hand-speed axis the curve uses).

Common to all game presets: `gain = true`, DPI 800, polling 1000 Hz,
`output_dpi = 1000`, `raw_passthrough = off`.

| Preset | mode | tuned params (presets.hpp) | Slow register (micro-aim / gentle micro-adjust, ~0.2–2 ips) | Mid register (tracking, ~5–30 ips) | Fast register (flick / 180°, ≥80–900 ips) | Cap plateau check |
|--------|------|----------------------------|--------------------------------------------------------------|------------------------------------|---------------------------------------------|-------------------|
| `cs2` | classic | accel 0.004, exp 2.0, input_offset 0, limit 1.6, cap {18.0, 1.6} out | near 1:1 — gain 1.00–1.01 (micro-corrections do NOT accelerate) | gentle rise 1.02→1.12 | 1.32 @80 ips → 1.58 @900 ips (asymptote ~1.6) | no plateau before ~1.6; ends just under limit |
| `valorant` | natural | limit 1.3, decay 0.08, motivity 1.2, input_offset 0.02, cap {30.0, 2.0} out | 1.01 → 1.07 @2 ips (soft entry) | smooth 1.13→1.26 | 1.29 @80 → 1.30 (gently saturates at ~1.3) | very flat ceiling ~1.3 — cap 2.0 effectively unused |
| `apex` | power | scale 2.2, exp_power 0.8, input_offset 0.02, output_offset 0.9, cap {28.0, 2.2} out | ~0.90 floor at the very slowest, → 1.95 @2 ips (fastest kick-in) | 2.10–2.18 | ~2.20 (reaches cap early, holds) | **early** plateau: ~2.10 @5 ips (95% of cap) — full 2.20 only asymptotically (~500 ips); deliberate for 180° speed |
| `fps` | classic | accel 0.005, exp 2.0, input_offset 0.01, limit 1.8, cap {20.0, 1.8} out | 1.00–1.01 | 1.02→1.15 | 1.40 @80 → 1.76 @900 ips (asymptote 1.8) | gentle climb to 1.8 — least aggressive plateau |

**How to read a row while playing:**

- `cs2` — a slightly larger than 1:1 micro-aim response is expected *feels
  right*; flicks should ramp but never shoot past ~1.6.
- `valorant` — the whole curve lives in a narrow band (1.0–1.3); it should feel
  calm, *not* "slippery", at both slow and fast flicks.
- `apex` — expects a *fast* ramp: the crosshair should get markedly faster on a
  180° flick while slow tracking stays near 1:1 (the 0.9 floor). If your flick
  feels *un*changed from raw, the power ramp is not engaging — suspect
  DPI issue or smoothing, not the curve.
- `fps` — the safe middle ground: firm but late ramp, high ending (1.8).

> If a row's numbers are what you *want* but the hand feel disagrees strongly,
> run the DPI ladder (3.2) before touching parameters — sensor behavior is the
> usual hidden variable.

### 3.4 Coming from Windows / macOS — what changes on Linux

**Where accel runs.** On Windows, RawAccel runs as a *kernel filter driver* and
rewrites mouse reports before the OS/game sees them. On Linux, the rawaccel
daemon does the equivalent job **kernel-side**: it grabs the real evdev node,
applies the modifier (per-event, µs-level), and re-emits an accelerated stream
through uinput. Either way, the accel sits **below** the game and the desktop —
it is not a surface macro or an in-game setting.

**What actually changes on Linux:**

1. **Desktop double-acceleration.** Windows only has "Enhance pointer
   precision" to disable; **Linux desktops (KDE Plasma especially) accelerate
   the pointer themselves on top of your evdev stream**. If you skip the KDE
   flat fix, you get accel-on-accel and the preset feels "too fast"
   (this is the #1 reported symptom):

   ```bash
   bash scripts/kde-fix-accel.sh   # installer already ran it; re-run to be safe
   ```

   GNOME equivalent: set mouse accel profile to `flat` (see Section 2).
   Wayland compositors: the daemon stream is flat by definition (no compositor
   pointer accel on top of evdev apps), but check your compositor's mouse
   settings anyway.

2. **Raw input / in-game sensitivity.** Games read the accelerated evdev/uinput
   stream the same way they read the mouse on Windows, so "Raw Input: ON" needs
   no extra knob. If a game also applies its own "mouse smoothing", disable it
   in-game as you would on Windows.

3. **Sensor/DPI expectations are unchanged.** Your mouse's DPI, polling rate
   and native resolution behave identically. Sync the real values into the
   profile (`rawaccel-cli set-param <profile> dpi … polling_rate …`) exactly
   as you would in the Windows RawAccel GUI.

4. **Your old RawAccel numbers still make sense.** The parameter vocabulary
   (acceleration, exponent, limit, input_offset, output_offset, cap, cap_mode,
   mode classic/natural/power) is the same project lineage — if you exported a
   config on Windows (or know your community curve), re-enter the same values;
   the presets here are matched starting points (Section 3.3), not rules.

5. **macOS switchers.** macOS has no RawAccel and applies its own "smart"
   system acceleration. On Linux you get RawAccel-style control for the first
   time. Start from `precision` (very light) or `office` if macOS's default
   motion felt right, and use `disable` (raw 1:1) as your mental reference —
   that is the exact macOS-familiar 1:1 you may have been chasing.

---

## 4. Latency measurement on real hardware (P57 methodology)

The daemon keeps a per-device **processing latency histogram** (µs) on its
hot path. On Linux it is measured exactly as on the VM (P57/P64/P73 harness
work): `rawaccel-cli latency` pushes that histogram out; it is the per-event
cost of `flush_motion()` entry → modifier math → last uinput write. HID
polling (the sensor→USB delivery) is **NOT** included — that is dominated by
your mouse's polling rate and is a constant you already know.

### 4.1 Step-by-step (per profile)

```bash
# 1. Daemon up and your real mouse grabbed?
rawaccel-cli status                 # your mouse listed

# 2. Activate the profile under test
rawaccel-cli set p71-<preset>

# 3. Launch your game / aim trainer and play at real speed for ~30–60 s.
#    The histogram only fills while motion events flow, so play, don't idle.

# 4. Snapshot the live histogram (tries the IPC socket first, SIGUSR1 falls back)
rawaccel-cli latency

# 5. Read it — the dump lands in the daemon log
journalctl -u rawaccel -n 30
#   === RawAccel Processing Latency ===
#   Device: <your mouse>
#     Samples  : n
#     Min / Avg / p50 / p95 / p99 / Max  (µs)
#     Overflow : k samples > 500 µs
```

**Where the numbers print.** `rawaccel-cli latency` does **not** print the
histogram itself — it only *schedules* a dump that is printed from the daemon's
own stdout. With the `setup.sh` install the daemon is a systemd service, so the
dump lands in `journalctl -u rawaccel`. If you launched the daemon manually
(`sudo ./build-manual/rawaccel-daemon …`), read the terminal it runs in (or the
file you redirected its stdout to) — `journalctl` shows nothing in that case.

**Mechanism (no sudo confusion).** The CLI first asks the daemon for a latency
snapshot over its **IPC socket** (`/run/rawaccel.sock`); any `input`-group user
can do this, no sudo needed. If the socket is unavailable (older daemon/stale
socket), it falls back to **SIGUSR1** to the PID in the pid file — that signal
path needs root because the daemon itself runs as root, so run
`sudo rawaccel-cli latency` to cover both paths.

**"No motion events recorded yet."** If you dump before moving the mouse, each
device prints `No motion events recorded yet. (counters reset)` — that is
expected; play first, then dump. Also note that the histogram only fills on
*accelerated* motion: if your active profile is `disable`/raw passthrough,
events bypass `flush_motion()` entirely and the dump shows a raw-passthrough
note instead of samples — **activate the accelerated profile under test before
measuring** (step 2 does this).

Repeat **3× while playing** and take the middle value of each percentile (the
daemon resets counters on every dump, so each run is independent).

### 4.2 How to interpret p50 / p95 / p99

| Column | What it is | Your "normal" target |
|--------|------------|----------------------|
| p50 (median) | typical per-event processing cost | **low single-digit µs** (1–5 µs) — a tiny fraction of one polling frame |
| p95 | 95th-percentile tail | should stay within a few µs of p50 on the same surface |
| p99 | worst-99% tail (the "I'm still feeling ok" bound) | still < ~10 µs single-digit; small humps while gaming are expected |
| Max / Overflow | rare queue spikes (`> 500 µs`) | expect occasional VM/scheduler spikes **on this VM**; on real hardware, a *recurring* > 500 µs Max or a growing Overflow count is the flag to report |

**Frame-budget yardstick** (how small this really is):

- 1000 Hz mouse → one frame = 1000 µs. A p50 of 1–3 µs is **~0.1–0.3%** of the
  frame budget; the daemon cannot be your latency bottleneck.
- 8000 Hz (8k) mouse → frame = 125 µs. Still: p50 ≈ 1–3 µs ≈ **1–2%** of the
  frame.

**Reference measurements** (all from the project's own runs, µs):

| Data set | p50 | p95 | p99 | Notes |
|----------|-----|-----|-----|-------|
| P31 synthetic hot-path (VM, n=2454) | 33 | 86 | 266 | VM host jitter heavy; older tooling |
| P57 / P64 / P73 live daemon, pan~4000 cnt/s (VM) | 1.75–2.25 | 2.75–3.75 | 3.75–5.25 | uinput virtual mouse, consistent across runs |
| P94 precision ramp (VM, n=19487) | 1.75 | 3.25–3.75 | 4.75 | new `precision` scenario, reproducible |
| P101 precision ramp, deadline-driven harness (VM, n=27043) | 1.75 | 3.75 | 4.75 | fixed timing; ~2× P94 sample count (high end now truly exercised) |
| Per-mod acceptance, live daemon + uinput (VM) | 2.75–3.75 | 4.75–5.75 | 7–15 | one run per preset family (n≈850–5300); see §4.3 |

Your real-hardware numbers should be **in the low single digits at p50/p95**,
matching the P57/P64/P94 ballpark (VM jitter disappears on real hardware). A
p50 in the tens of µs, or p95/p99 that grow under load, points at a
per-device/grab issue worth a P-round report.

### 4.3 Optional: run the game-speed harness on your real machine too

The same scenarios used to produce the reference numbers can run on your box
(no real mouse needed — it isolates only the daemon, exactly as it did in the
VM):

```bash
gcc -O2 -o build-manual/virtmouse-game scripts/virtmouse-game.c
sudo build-manual/virtmouse-game precision 60 &   # keep it GRABBED while snapshotting
sleep 3 && rawaccel-cli latency                   # snapshot while the device is alive
```
If you run the harness in the foreground instead, it destroys the uinput device on
exit and the daemon resets that device's counters on removal — the dump then reads
empty ("No motion events recorded yet"). Snapshot **while the run is still going**.

Scenarios: `flick` (fast bursts), `pan` (sustained 4000 cnt/s), `mix`
(flick + micro-moves), `precision` (P94: 1 s sawtooth 120→4000 cnt/s,
≈150→5000 ips at 800 DPI, crossing the esport grid 2000/3000/4000 ips). The
harness writes to `/dev/uinput`; `sudo` always works, and any `input`-group
member can run it without (only one daemon may hold the grab). Then
do the real-mouse version (Section 4.1) while gaming — the two together give
you the full split: *daemon processing* vs *HID + game*. Compare the Max/tail
between them: on real hardware the harness Max should largely vanish, which
confirms the VM tail was host jitter.

### 4.4 Per-parameter acceptance runs (each preset family, live daemon)

Per-mod acceptance: create each preset family as its own profile, activate
it, inject motion with the harness while **still grabbed** (start the harness,
wait ~3 s, then snapshot — a dump taken after the harness exits reads an empty
histogram, since removal resets the counters), and check both the **latency
histogram** and the **live telemetry gain**:

```bash
rawaccel-cli create-preset cs2 p_cs2 && rawaccel-cli set p_cs2
sudo build-manual/virtmouse-game precision 10 &   # still running when you snapshot
sleep 3 && rawaccel-cli latency                    # IPC snapshot, live device
rawaccel-cli status --json | grep -A20 '"name": "P57'   # telem_in/out_ips, telem_gain
```

VM reference (23:42, live daemon PID 185957, uinput "P57" id
`usb:0e0f:1337:`, n from 10 s precision/pan runs):

| Family (profile) | mode | telem gain @ snapshot | p50 | p95 | p99 | Max | verdict |
|------------------|------|-----------------------|-----|-----|-----|-----|---------|
| cs2  | classic cap[18,1.6] | 2.0 (2.27→4.55 ips) | 3.75 | 5.25 | 14.25 | 59.5 | PASS |
| valorant | natural limit[30,2.0] | 2.0 (0.49→0.98 ips) | 3.25 | 5.75 | 15.25 | 508* | PASS |
| apex | power scale 2.2, exp 0.8 | 2.0 (1.11→2.22 ips) | 3.25 | 5.25 | 7.25 | 61.3 | PASS |
| fps | classic cap[20,1.8] | 2.0 (0.45→0.90 ips) | 3.25 | 5.25 | 7.75 | 116 | PASS |
| disable | raw passthrough | telem gain 1.0, no per-event eval | — | — | — | — | PASS (1:1) |

\* single VM host-jitter overflow (> 500 µs), counters otherwise clean — same
trait documented in §4.2. On any accelerated family, `telem_gain > 1` while the
sweep runs confirms the accel path is live; `disable` must hold exactly 1:1.

---

## 5. Feel score table

Fill in **1–5** for each row (1 = unusable, 3 = acceptable, 5 = excellent).
`overall` is your gut call to accept or reject. Leave latency as measured µs.

| Profile | Slow aim (2–20 mm/s) /5 | Flick (180°) /5 | Tracking /5 | Overall /5 | p50 µs | p95 µs | p99 µs | Notes |
|---------|-------------------------|-----------------|-------------|------------|--------|--------|--------|-------|
| raw 1:1 (baseline) | | | | | | | | |
| `gaming` | | | | | | | | |
| `cs2` | | | | | | | | |
| `valorant` | | | | | | | | |
| `apex` | | | | | | | | |
| `fps` | | | | | | | | |

**What to look for in each row:**

- **Slow aim low (1–2)** → likely sub-1:1 muddy micro-aim. Check the preset's
  `input_offset` / `output_offset`; raise `input_offset` if slow aim feels floaty.
- **Flick low (1–2)** → cap plateau (cap too low / cap_mode wrong). Raise the
  cap or check `limit`. Before tuning, cross-check the DPI ladder (3.2).
- **Tracking low (1–2)** → curve too aggressive mid-range, or compositor
  double-acceleration still on.
- **Any row with p95/p99 badly elevated** → note it for the latency P-round,
  independent of feel scores.

---

## 6. Conclusion → acceptance recommendation

After all rows are filled:

- **ACCEPT** a profile if: overall ≥ 4, no task scores below 3, and p95/p99
  are within the reference ballpark.
- **ADJUST** a profile if: overall 3, or one task score is 2 and the cause is
  identifiable (sub-1:1, cap plateau, etc.). Adjust the specific parameter per
  the tuning tips in `docs/user_guide.md`, then re-run only that profile's
  window.
- **REJECT** a profile if: overall ≤ 2, or it shows an unfixable defect —
  report the exact symptom and parameter to the manager (Aj 1) for a fix round.

Record the final verdict here:

| Profile | Verdict (ACCEPT / ADJUST + param / REJECT) | Owner |
|---------|---------------------------------------------|-------|
| `gaming` | | |
| `cs2` | | |
| `valorant` | | |
| `apex` | | |
| `fps` | | |

---

## 7. Session hygiene

- Always compare against the **raw 1:1 baseline** — never a preset vs memory.
- Test at your **real DPI/polling** and the same mousepad / surface you game on.
- Run on the same game, same settings, same resolution for all profiles.
- If KDE: re-run `kde-fix-accel.sh` before the session to be safe.
- One window at a time — reset the table if you rush two presets back to back.
- Keep a **fixed DPI for the 3.1 pass** (use the ladder in 3.2 only when you
  explicitly want to separate accel from sensor behavior).