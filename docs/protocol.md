# Command Protocol

ASCII text protocol over USB serial and Serial1, unchanged across the patched firmware and the planned rebuild.

> **Status:** partial — this file has the commands referenced so far in project notes. Fill in the rest of the command set (full syntax, arguments, and responses) as you formalize it. Consider this a living reference, not a spec written up front.

## Known commands

| Command | Purpose |
|---|---|
| `reset` | Reset run state; preserves tare |
| `resetall` | Full reset — re-seeds ISR timing state, restores motion after a dead-motor freeze |
| `driftcomp <steps> <turns>` | Manually set learned per-reversal traverse drift correction |
| `saveprofile` | Save current profile/preset/tension calibration to EEPROM |

## Reporting

| Report | Trigger |
|---|---|
| `RECAL_DRIFT` | Emitted at every auto-recalibration |
| `STATUS` | Periodic telemetry (target: fixed 10 Hz, gated by `availableForWrite()`) |
| `FAULT_STALL` | Raised when hall-index-derived RPM/turn count disagrees with commanded step count |

## Turn-boundary concepts

- **Chaos** — pattern variation applied to traverse spacing
- **Mid** — mid-point traverse behavior
- **Taps** — intermediate turn-count targets within a wind
- **Total** — final target turn count for the wind
