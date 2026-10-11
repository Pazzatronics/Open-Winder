# Firmware Changelog

## Public release r2 -- 2026-10-11 (traverse lead screw 5 mm/rev)
- `TRAV_LEAD_UM_PER_REV` 4000 -> 5000. The reference machine's traverse lead screw is 5 mm/rev; the 4 mm value was an error, so every traverse distance typed in mm moved the carriage 1.25x what it said (`space 1.0` actually wound at 1.25 mm/turn).
- The traverse drive stays at 400 steps/rev, so no DIP switch change. One step is now 12.5 um. Step <-> distance math runs in nanometres (`TRAV_STEP_NM`), so a lead and steps/rev pair that is not a whole micron per step now builds. The whole-micron `static_assert` became a whole-nanometre one.
- Now true millimetres: `space`, `offtop`, `offbottom`, `softdist`, `mid`, `nudget` / `nudgeb`, and the calibration back-off (5.5 mm; it was 6.875 mm on the machine).
- The `defcal` window lands in the same physical place: insets stay 1621 / 214 steps, and `DEFAULT_CAL_OFFTOP_UM` becomes 4063 (the same 325 steps that 3250 meant before).
- Unchanged on the machine: everything counted in steps (hard limits, insets, saved bounds, `driftcomp`) and every speed set in rpm (calibration, jog, nudge). Their mm readouts in `stat` go up 1.25x.
- Most pitch the traverse can deliver: 400 x 12.5 um = 5.0 mm per spindle turn.
- To repeat a coil wound on the previous public build, multiply `space` and `softdist` by 1.25 (`space 1.25`, `softdist 0.625`).
- Compiles clean for Teensy 4.1 (arduino-cli, Teensy core 1.62.0, all warnings on). Not yet confirmed on the machine.

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
