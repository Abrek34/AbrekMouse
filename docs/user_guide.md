# RawAccel Linux — User Guide (Hızlı Başlangıç)

A quick onboarding guide: install, pick a preset per game, first-run tuning,
and how to get back to raw 1:1. Everything here uses `rawaccel-cli`; the GUI
(`rawaccel-gui`) mirrors the same settings.

---

## 1. Install

```bash
sudo bash setup.sh
```

This installs all dependencies, builds, installs binaries + systemd/udev/polkit
daemon, enables the service, and applies the KDE Plasma flat-acceleration fix.

Verify:

```bash
systemctl status rawaccel      # active (running)
rawaccel-cli status            # your mouse listed with its DPI/polling
```

> **KDE users**: if you get double-acceleration, run `bash scripts/kde-fix-accel.sh`
> (see README → "KDE Plasma Setup"). Disable compositor mouse acceleration on
> GNOME too (Settings → Mouse & Touchpad).

---

## 2. Quick tour of the CLI

```bash
rawaccel-cli list                      # all profiles
rawaccel-cli create-preset <preset> <name>   # build a profile from a preset
rawaccel-cli set <name>                # activate it (live)
rawaccel-cli show <name>               # inspect its parameters
rawaccel-cli latency                   # schedules a latency histogram dump (daemon log)
```

The 8 presets: `gaming`, `office`, `precision`, `disable`, `cs2`, `valorant`,
`apex`, `fps`. A fresh install's default profile has **acceleration off**
(noaccel, linear); for a *strict* raw 1:1 baseline use the `disable` preset
(raw passthrough — events forwarded 1:1 to uinput).

---

## 3. Picking a preset per game

| What you play | Preset | Why |
|---------------|--------|-----|
| **CS2 / tactical FPS (pro)** | `cs2` | Classic curve, early kick-in, cap 1.6 — micro-adjust headshots stay 1:1, flicks ramp up. Suits the pro eDPI band 560–1000. |
| **Valorant** | `valorant` | Natural curve (TenZ-style base): smooth entry/exit, light gain, high cap 2.0 so panic flicks stay controlled. |
| **Apex Legends** | `apex` | Power mode: fast 180° flicks, output offset 0.9 keeps slow tracking near 1:1. |
| **Generic FPS / aim trainers** | `fps` | Balanced classic curve — safe starting point for most shooters. |
| **General / browser / desktop** | `office` | Light natural acceleration (limit 1.3). |
| **CAD / design / pixel work** | `precision` | Very low acceleration (0.002) for precise work. |
| **No acceleration** | `disable` | Raw passthrough, 1:1. Also the way to reset. |

**Example:**

```bash
rawaccel-cli create-preset cs2 my-cs2     # CS2 tactical
rawaccel-cli set my-cs2                   # activate
```

---

## 4. First-run parameter tuning tips

Set your mouse's real hardware values first — ips math is only accurate when
DPI/polling are correct (most CS2 pros: 400 DPI / 1000 Hz):

```bash
rawaccel-cli set-param <profile> dpi 400
rawaccel-cli set-param <profile> polling_rate 1000
```

Common knobs (see README → "Parameters" for the full list):

- **`input_offset`** (ips) — raise it if slow aim feels "floaty"/insta-
  accelerating. `0.01–0.05` keeps micro-corrections near 1:1.
- **`limit`** (max gain) — higher = more speed at high motion; lower = subtler.
- **`exponent_classic`** — lower = subtler curve. CS2: keep at 2.0, tune
  `limit` 1.4–1.8 instead.
- **`cap` / `cap` cap_mode** — the speed ceiling. If flicks hit a *cap plateau*
  (cursor stops accelerating at high hand speed), raise the cap.
- **`acceleration`** — main gain amount in classic mode.

Value ranges to start from (see the table below for exact preset numbers).

**Valorant:** start `acceleration ≈ 0.05` (GUI slider), `limit 1.2–1.5`,
`input_offset 0.02–0.05`.

**CS2:** `exponent_classic` at 2.0, test `limit` between 1.4–1.8; raise
`input_offset` if slow aim feels floaty.

> **A/B trick**: use the **Ham Geçiş / Raw Passthrough** toggle (or switch to
> the `disable` preset) to compare accelerated vs raw 1:1 while you dial it in.
> Spend 30–60 min per config before judging a preset.

---

## 5. First-try target values (per preset)

Exact starting values each preset ships with (from `include/presets.hpp`
`make_preset` — the real numbers, not guesses). Common to all: DPI 800,
polling rate 1000 Hz, output_dpi 1000, `gain = true`, raw_passthrough off.

| Parameter | `gaming` | `cs2` | `valorant` | `apex` | `fps` | `office` | `precision` | `disable` |
|-----------|----------|-------|------------|--------|-------|----------|-------------|-----------|
| mode | classic | classic | natural | power | classic | natural | classic | noaccel |
| acceleration | 0.005 | 0.004 | — | — | 0.005 | — | 0.002 | — |
| exponent_classic | 2.0 | 2.0 | — | — | 2.0 | — | 1.5 | — |
| exponent_power | — | — | — | 0.8 | — | — | — | — |
| scale | — | — | — | 2.2 | — | — | — | — |
| limit | 1.8 | 1.6 | 1.3 | — | 1.8 | 1.3 | 1.2 | — |
| decay_rate | — | — | 0.08 | — | — | 0.08 | — | — |
| motivity | — | — | 1.2 | — | — | 1.2 | — | — |
| input_offset | 0 | 0 | 0.02 | 0.02 | 0.01 | — | — | — |
| output_offset | — | — | — | 0.9 | — | — | — | — |
| cap (in, gain) | — | 18.0, 1.6 | 30.0, 2.0 | 28.0, 2.2 | 20.0, 1.8 | — | — | — |
| cap_mode | — | out | out | out | out | — | — | — |
| raw_passthrough | off | off | off | off | off | off | off | **on** |

`—` = left at the global default for that field. `--help` lists them all.

Fresh-install sanity: the shipped "default" profile has `acceleration = 0` /
noaccel (no acceleration, linear); a strict raw 1:1 baseline is the `disable`
preset (raw passthrough, `raw_passthrough = on`).

**How to use it:** hand a player the row for their game as a *starting point*.
Tell them to keep `input_offset` until slow aim feels 1:1 (not muddy), keep the
cap high enough that flicks never plateau, and adjust from there.

---

## 6. Reset to raw 1:1

Two ways:

```bash
# Easy — switch to the raw "disable" preset (raw passthrough, noaccel)
rawaccel-cli create-preset disable my-raw
rawaccel-cli set my-raw

# Or force raw on an existing profile
rawaccel-cli set-param <profile> raw true
rawaccel-cli set <profile>
```

Use raw 1:1 as your sanity baseline whenever a preset "feels wrong" — compare
against it, not against memory.

---

## 7. Troubleshooting in 3 commands

```bash
rawaccel-cli status            # daemon up? mouse detected? DPI/polling right?
rawaccel-cli validate          # config warnings/errors?
journalctl -u rawaccel -n 30   # daemon log (the latency histogram prints here)
```

If a game feels wrong but everything checks out, re-check the compositor
double-acceleration (step 1) — it's the #1 cause of "preset feels too fast".
