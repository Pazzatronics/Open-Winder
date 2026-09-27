# Firmware Rebuild Architecture (planned, not yet coded)

The current machine runs the original firmware with surgical patches (see `bugs-and-fixes.md` — the dead-motor bug is fixed this way). A full rewrite was attempted once and failed on the machine, so the plan below is being implemented incrementally rather than as a single rewrite.

## PIT IntervalTimer ISR (step generation)

- Toggles winder STEP at 2× the step rate (one toggle = one edge; two toggles = one pulse)
- Traverse motion driven by a Bresenham-style accumulator: `acc += spacing; if (acc >= 16000) { pulse; acc -= 16000; }`
- Bounds/limit checks performed inside the ISR
- DIR line written one full ISR tick *before* the first step in a new direction — this is the fix for the traverse-drift root cause
- Position counted on the driver's active (rising) edge, not falling
- All arithmetic kept integer — avoids the float-precision issue described in `bugs-and-fixes.md`

## 1 kHz control tick

- Fixed-point milli-RPM ramp (replaces the lossy float accumulator)
- Spacing computed as soft-scale × chaos × mid factors
- Turn-boundary logic: chaos pattern, mid-point behavior, tap counting, auto-recalibration, total-turns stop
- Decel anticipation: begin slowing `RPM² / (120 × ramp)` turns before the target so taps/total land exactly on the number, not past it
- Stall detection

## `loop()` (non-timing-critical work only)

- USB and Serial1 command ports, same ASCII text protocol as today (see `protocol.md`)
- HX711 polling
- Telemetry via `snprintf`, gated by `availableForWrite()`, fixed at 10 Hz
- Logging to flash/SD
- Display updates

## Spindle hall index sensor (1 pulse/rev)

- Maintains the turn count of record and actual RPM independent of commanded step count
- Raises `FAULT_STALL` on mismatch: freezes the turn count and stops traverse
- Commanded step count is retained separately for traverse gearing
- Cross-checked against the ISR-driven count in the 1 kHz tick

## Storage

- EEPROM: profiles, presets, tension calibration — `saveprofile` command replaces the old DEFCAL-REBUILD-and-reflash workflow
- W25Q64 QSPI (LittleFS) or microSD: wind logs

## Traverse drift — diagnostics and correction hierarchy

Diagnostics to run **before** the rebuild lands, to further isolate the drift mechanism:
1. Print `HARD_TOP` / `HARD_BOTTOM` at every auto-recalibration
2. Add active bounds to `STATUS` output
3. Raise reversal DIR setup time to 50 µs to confirm the root-cause theory
4. Compare drift magnitude at 250 vs. 500 RPM

Correction approaches, in preferred order:
1. **Slotted-opto flag** on the traverse carriage — per-layer counter re-sync without stopping (preferred long-term fix)
2. **Quick re-home** to the bottom limit switch at taps/start/interval
3. **Learned `driftcomp`** per-reversal counter correction (`driftcomp <steps> <turns>`) as a fallback/interim measure

## Touchscreen (not yet started)

Options under consideration, in order of preference:
1. Separate ESP32-S3 + capacitive LCD running LVGL, communicating over UART using the existing command protocol
2. Nextion NX4827P043-011C over Serial1 — requires a 5 V external supply and a voltage divider on Nextion TX → Teensy RX, but plugs into the existing ASCII protocol with minimal Teensy-side changes
3. Last resort: Teensy-driven SPI TFT
