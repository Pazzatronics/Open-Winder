# Open Winder Firmware

Target: Teensy 4.1, built with Arduino IDE + Teensyduino.

`open_winder_public.ino` is the current public firmware (based on rev 2026-09-16b).

## Before your first run

- Set `TRAV_LEAD_UM_PER_REV` to your traverse lead screw (in microns per revolution).
- Set `WINDER_STEPS_PER_REV` and `TRAV_STEPS_PER_REV` to match the DM542T DIP switches on each drive (the defaults assume 400 steps/rev: SW5 OFF, SW6 ON, SW7 ON, SW8 ON).
- The `defcal` window constants (`DEFAULT_PROFILE_*`, `DEFAULT_CAL_OFF*`) came from the reference machine. Run `cal`, set your bounds with `jogt`/`jogb`/`savet`/`saveb`, send `donecal`, then `stat`, and copy `DEF_TOP_INSET_STEPS` / `DEF_BOTTOM_INSET_STEPS` into the sketch.

## Running a wind

Commands are typed over USB serial at 115200 baud. Send `help` for the full list. A minimal job:

```
dir cw
rpm 500
turns 8000
space 1.0
defcal
start
```

Wiring for every pin used here is in `../../hardware/wiring/open_winder_wiring.csv`.

See `../../docs/bugs-and-fixes.md` for known issues and `../../docs/rebuild-architecture.md` for the planned ISR-based rewrite.
