# Bill of Materials

Links below are **search queries**, not pinned product listings — sellers, prices, and ASINs on generic electronics parts (steppers, drivers, PSUs) turn over too often for a fixed link to stay reliable. Click through, compare current listings, and confirm specs match the row before buying.

| Qty | Part | Key specs | Source | Notes |
|---|---|---|---|---|
| 1 | Teensy 4.1 | ARM Cortex-M7 @ 600 MHz, 3.3 V logic | **[pjrc.com](https://www.pjrc.com/store/teensy41.html)** (buy direct — see note) | Third-party Amazon listings routinely mark this up over PJRC's own price; PJRC is the manufacturer |
| 1 | Stepper driver | DM542T, 4–5 V input, up to 50 V / 4.2 A | [Amazon search](https://www.amazon.com/s?k=DM542T+stepper+driver) | DIP-configured for 4.20 A peak, 1600 µsteps in this build |
| 1 | Winder motor | NEMA23 stepper, 23HS45-4204S, 4.2 A, 3 Nm, 0.88 Ω, 3.4 mH | [Amazon search](https://www.amazon.com/s?k=23HS45-4204S+nema+23+stepper+motor) | Coil pairing bench-verified: black+green = coil 1, red+blue = coil 2 (not the usual black/red + blue/green convention) |
| 1 | Power supply | LRS-350-24, 24 V rail, 350 W enclosed switching PSU | [Amazon search](https://www.amazon.com/s?k=LRS-350-24+power+supply) | Confirmed 24 V variant (not 36/48 V) |
| 1 | Load cell + amp | HX711 amplifier board + parallel-beam 1 kg load cell | [Amazon search](https://www.amazon.com/s?k=HX711+load+cell+1kg+parallel+beam) | Bit-banged GPIO; pins DT=30, SCK=31 in this build |
| 1 | Flash storage | Winbond W25Q64JV, QSPI NOR flash, 64 Mbit | [Amazon search](https://www.amazon.com/s?k=W25Q64JV+flash+chip) | For wind-log storage via LittleFS |
| 1+ | Level shifter | 74AHCT125, quad buffer/line driver | [Amazon search](https://www.amazon.com/s?k=74AHCT125) | Needed on STEP/DIR lines: Teensy is 3.3 V, DM542T inputs are spec'd 4–5 V |
| — | Limit switches | Mechanical/optical, NC configuration | [Amazon search](https://www.amazon.com/s?k=limit+switch+NC) | Bench-verified: pin 7 = BOTTOM, pin 8 = TOP; idle LOW, tripped HIGH |
| — | Fasteners / misc | — | — | See Fusion 360 assembly BOM export for exact hardware call-outs |

## Not yet finalized

- Touchscreen: leaning ESP32-S3 + capacitive LCD (LVGL over UART) vs. Nextion NX4827P043-011C vs. Teensy-driven SPI TFT — see `docs/rebuild-architecture.md`
- Laser/LED module for the optical sub-project — see project notes
