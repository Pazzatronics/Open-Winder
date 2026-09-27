# Firmware Changelog

## Public release (from 2026-09-16b)
- Product-specific winding presets removed; jobs are set up with dir / rpm / turns / space and optional taps
- File renamed to `open_winder_public.ino`
- Wiring table added: `../hardware/wiring/open_winder_wiring.csv`

## 2026-09-16b
- Ported August-audit fixes: stop-over-pause priority, decel anticipation (taps/total land exactly on target), single pending tap, chained-fault guard, jog limit stop, zero/tare refused while moving, spin ramps, in-flight step counted, tension baseline decay fix, 500 ns HX711 pulses, watchdog optional (off by default)
- Not yet confirmed on the machine. Note: taps/total now land on the exact number rather than ~28 turns past it.

## 2026-09-16
- Idle-freeze micros()-wrap re-seed fix (dead-motor bug -- see ../docs/bugs-and-fixes.md)
- Winder set to 400 steps/rev (bench-verified, was assumed 1600)
- HX711 moved to pins 30/31 (was 22/23)
- Run-permit and snag-fault checks compiled out (not wired on this build)
- reset now preserves tare
- RECAL_DRIFT report added at every auto-recalibration
- driftcomp <steps> <turns> command added
- Confirmed working on the machine ("works well")
