# Pinout & Wiring Notes

Bench-verified on the as-built machine, September 2026.

## Limit switches

| Pin | Function | Notes |
|---|---|---|
| 7 | BOTTOM limit | NC switch: idle LOW, tripped HIGH |
| 8 | TOP limit | NC switch: idle LOW, tripped HIGH |

Traverse direction: **CW = toward TOP** (`TRAVERSE_DIR_INVERT = false` is correct for this build).

## Load cell (HX711)

| Pin | Function |
|---|---|
| 30 | DT (data) |
| 31 | SCK (clock) |

Bit-banged GPIO, not a hardware SPI/I2C peripheral. Originally 22/23 in early firmware — moved to 30/31 in the current build.

## Winder motor coil pairing (23HS45-4204S)

Verified with a multimeter — **do not assume by wire color convention**:

- Coil A: black + green
- Coil B: red + blue

(The usual convention of black/red as one pair and blue/green as the other does **not** hold for this motor.)

## Stepper driver (DM542T)

- DIP switches set for **4.20 A peak**, **1600 microsteps/rev** at the driver
- Bench-verified actual winder motion: 1600 pulses = 4.000 revolutions → **400 steps/rev**, corresponding to microstep setting 2 (SW5 OFF / SW6 ON / SW7 ON / SW8 ON)
- Traverse axis steps/rev not yet verified — its DIP switches differ from the winder axis

## Logic level shifting

Teensy 4.1 I/O is 3.3 V; DM542T STEP/DIR inputs are spec'd 4–5 V. A 74AHCT125 buffer is planned/used on these lines. Hardware margin note: DIR setup time measured at exactly the 5 µs minimum — suspected root cause of traverse drift at direction reversals (see `docs/bugs-and-fixes.md`).

## Safety / interlocks

- Run-permit and snag-fault inputs are **not wired** in the current build
- E-stop is the LRS-350 power switch
- Snag-detection code is retained in firmware behind a compile flag for future use
