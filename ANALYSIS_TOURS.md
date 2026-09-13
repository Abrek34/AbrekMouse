# Analysis Tours - 10 Iterations

Following the project's established pattern (FIX_LOG.md), here are 10 analysis iterations documenting findings and suggested improvements:

## Tour 1: Hot-path Performance & Latency
**Status**: Analysis complete

**Findings**:
- Hot path processes mouse events with sub-microsecond latency (typically <5 µs per event)
- Event loop uses epoll with 10 ms timeout for housekeeping only; motion events processed synchronously
- Algorithm overhead is header-only, compiler-inlined
- Subpixel accumulation ensures no micro-movements are lost
- Hot path contract: single loop thread, no per-event allocations; `dpi_factor` pre-computed per device
- 2 syscalls per event (clock reads) + 1 batched `REL_X`+`REL_Y` write = 3 hot-path syscalls
- P99/max queue spikes are the "P-round" analysis target

**Suggested actions**:
- Verify batched write (P93) implementation: `process_device()` accumulates frame in stack `write_batch` and submits in single `write()` syscall
- Monitor `real_polling_rate` data race (daemon.hpp:37 - plain `int`, not atomic)
- Test with varying poll rates (125-8000 Hz) to validate frame-budget calculations

---

## Tour 2: Config Migration & Version Handling
**Status**: Analysis complete

**Findings**:
- `RAWACCEL_VERSION = "1.2.0"` lives in `include/rawaccel-base.hpp` and propagates to all components
- `migrate_config()` (config.cpp:1079) is critical for backward compatibility
- P43-BF1: `version` field must be read from JSON (line 665-666) - without it, `migrate_lookup_gain()` re-runs on every load
- Version comparison `version_lt()` (line 1028) has edge cases with corrupt versions
- `migrate_lookup_gain()` (line 1000) handles 0.3.x→0.4.0 lookup+gain semantics change

**Suggested actions**:
- Verify `app_config_from_json_obj()` always reads version field
- Test migration round-trips: load old config → save → verify data integrity
- Ensure `version_lt()` handles all version string formats robustly

---

## Tour 3: Acceleration Algorithm Accuracy
**Status**: Analysis complete

**Findings**:
- Classic mode: legacy vs GAIN variants, cap_mode (io/in/out), exponent_classic ≤ 1 "linear path"
- Power mode: gain/legacy variants, cap_mode, output_offset handling (P155 fix)
- Lookup mode: LUT data serialization, binary search in `lookup::operator()`
- P155: `io cap.y ≤ 0` now evaluates to identity scale (no dead mouse)
- P155: Power GAIN cap order - `speed < cap.x` cap branch decided before output-offset plateau

**Suggested actions**:
- Run oracle comparison (`tests/oracle/run_oracle.sh`) after any accel algorithm changes
- Verify classic mode `exponent_classic ≤ 1` "linear path" against reference implementation
- Test power mode `io cap.y ≤ 0` degenerate case ensures mouse is not dead

---

## Tour 4: GUI Localization & Internationalization
**Status**: Analysis complete

**Findings**:
- GUI supports 3 languages: Auto (locale), English, Turkish
- Language switch persisted to `~/.config/rawaccel/gui_lang`
- `tr()` / `trf()` / `trlbl()` / `trmlbl()` / `trbtn()` macros i18n
- Translation coverage tool (`tests/run_tr_coverage.sh`) exits 1 on any missing Turkish entry
- `refresh_language()` re-applies every registered widget in place on switch

**Suggested actions**:
- Run `bash tests/run_tr_coverage.sh` after any GUI string changes
- Verify all translatable strings have Turkish entries in `gui/tr.inl`
- Test language switching live in GUI (header-bar dropdown)

---

## Tour 5: CLI Validation & Parameter Sanitization
**Status**: Analysis complete

**Findings**:
- CLI `set-param` has extensive domain validation (P107)
- Key constraints: `cap_x ≥ input_offset`, `scale 0.01-100`, `output_dpi {0}∪[1,32000]`
- `output_dpi = 0` sentinel for "no output-DPI normalization" (CFG-1)
- CLI domain texts aligned with sanitize floors (acceleration decel note, input_offset 0-500 ≤ cap_x)
- `output_dpi` accepts fractional DPI via `range_ok` instead of int cast

**Suggested actions**:
- Test CLI `set-param` edge cases: negative values, out-of-range, zero DPI
- Verify `rawaccel-cli validate` catches all config errors
- Test profile import/export round-trip preserves all parameters

---

## Tour 6: Daemon Device Management & Hot-plug
**Status**: Analysis complete

**Findings**:
- Device discovery: `find_mice()` scans `/dev/input/event*` using stable by-id paths
- P131/BUG-02: deny lists (`path_deny_until_ms_`, `dev_deny_until_ms_`) prevent ~2s churn loop
- HP-3: `opened_device_ids_` tracking prevents double-grab on HID-composite mice
- PAS-1: `raw_input_enabled_` atomic flag honoured as master switch
- PAS-2: per-device `dev_cfg.disable` flag releases device in safe mode

**Suggested actions**:
- Test hot-plug: connect/disconnect mice while daemon running
- Verify deny list pruning works correctly across reboots
- Test multi-interface HID mice don't get double-grabbed

---

## Tour 7: LUT (Lookup Table) Handling
**Status**: Analysis complete

**Findings**:
- LUT capped at 257 points (514 float entries) - `LUT_RAW_DATA_CAPACITY`
- `sort_lut_data()` uses insertion sort for small n (max 257)
- LUT round-trip: `profile_to_json()` uses compact `.dump()` format
- `check_import_lut_size()` clamps over-capacity LUT and floors odd element count
- Odd-length trailing element left as-is (no x to multiply for velocity division guard)

**Suggested actions**:
- Test LUT import with exactly 257 points (max capacity)
- Test LUT import with odd element count (should floor to even)
- Verify lookup mode round-trip preserves curve data through mode switches

---

## Tour 8: Memory & Resource Management
**Status**: Analysis complete

**Findings**:
- `save_config()` uses atomic tmp+rename pattern (config.cpp:727)
- `O_NOFOLLOW|O_EXCL` temp file creation prevents symlink races
- `teardown_devices()` collects devices outside lock, then destroys
- `fd_to_dev_` map provides O(1) fd→device index lookup
- Hard link backup (`path.bak`) preserves old config generation

**Suggested actions**:
- Test save_config with symlinked config paths
- Verify fd_to_dev_ map is correctly updated on device removal/rename
- Test concurrent save operations don't corrupt config file

---

## Tour 9: Error Handling & Edge Cases
**Status**: Analysis complete

**Findings**:
- `modifier::modify()` has defense-in-depth NaN/Inf checks (line 499-500)
- `sanitize_accel_args()` has both lower and upper bounds clamping
- `json_get_int_safe()` handles UB from large ints (libFuzzer caught 1e26 input)
- `sanitize_device_profile()` clamps all fields to safe ranges
- Some config fields may not be properly validated on import

**Suggested actions**:
- Test with extreme parameter values (scale=1e6, exponent_power=1e9)
- Verify NaN/Inf propagation is handled at every pipeline stage
- Test JSON config with malformed/type-wrong fields degrade to defaults

---

## Tour 10: Build System & CI Integration
**Status**: Analysis complete

**Findings**:
- CI workflow (`ci.yml`) runs: build-and-test, sanitizers, fuzz-smoke, perf-gate
- `perf-gate` compares hot-path bench against `tests/perf_baseline.json` (5% regression gate)
- Translation coverage must pass (0 MISSING = PASS)
- Oracle compares 1071 gain rows against official reference (67 documented deviations)
- Fuzz harnesses: 60s default, 300s available via `bash tests/run_fuzz.sh 300`

**Suggested actions**:
- Ensure `tests/perf_baseline.json` is committed and up-to-date
- Run full CI suite before merging significant changes
- Monitor fuzz harness for crash inputs (upload as artifacts on failure)
- Keep oracle `known_deviations.txt` updated when accel math changes

---

## Overall Recommendations Priority

1. **Critical**: Oracle consistency after any accel algorithm changes
2. **High**: Config version migration and sanitization
3. **Medium**: GUI i18n coverage and hot-plug reliability
4. **Low**: Performance optimization and edge case handling