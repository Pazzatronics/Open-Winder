# Bill of Materials

**Buying parts? Please use these links.** The Amazon links below are affiliate links: Amazon pays me a small commission and your price doesn't change. That's what keeps new projects and videos coming for the channel.

*As an Amazon Associate I earn from qualifying purchases.*

Check the listing's pack size against the Qty column before you order. Rows marked "Amazon search" aren't affiliate links; they're plain searches for parts I don't have a set listing for yet.

| Qty | Part | Key specs | Source | Notes |
|---|---|---|---|---|
| 1 | Teensy 4.1 | ARM Cortex-M7 @ 600 MHz, 3.3 V logic | [Amazon](https://link.amazon/B0ixilNMe) · also sold direct by [PJRC](https://www.pjrc.com/store/teensy41.html) | PJRC is the manufacturer |
| 2 | Stepper driver | DM542T, 4–5 V input, up to 50 V / 4.2 A | [Amazon](https://link.amazon/B08cbkJBz) | One per motor (spindle and traverse). DIP-configured for 4.20 A peak, 1600 µsteps in this build |
| 2 | Stepper motors | NEMA23 stepper, 23HS45-4204S, 4.2 A, 3 Nm, 0.88 Ω, 3.4 mH | [Amazon](https://link.amazon/B0akogcUt) | Spindle and traverse. Coil pairing bench-verified: black+green = coil 1, red+blue = coil 2 (not the usual black/red + blue/green convention) |
| 1 | Traverse | Moves the wire guide back and forth across the bobbin | [Amazon](https://link.amazon/B0cS9iVm1) | 5 mm lead (travel per screw turn) is the firmware default |
| 1 | Coupling | Connects a motor shaft to the part it drives | [Amazon](https://link.amazon/B0jcq9Sza) | |
| 1 | Power supply | LRS-350-24, 24 V rail, 350 W enclosed switching PSU | [Amazon](https://link.amazon/B0ceYB90f) | Confirmed 24 V variant (not 36/48 V) |
| 1 | Power cable | Mains power to the supply | [Amazon](https://link.amazon/B05tHGsYr) | |
| 1 | Load cell + amp | HX711 amplifier board + parallel-beam 1 kg load cell | [Amazon](https://link.amazon/B0f6mvZfB) | Bit-banged GPIO; pins DT=30, SCK=31 in this build |
| 1 | Flash storage | Winbond W25Q64JV, QSPI NOR flash, 64 Mbit | [Amazon search](https://www.amazon.com/s?k=W25Q64JV+flash+chip) | For wind-log storage via LittleFS |
| 1+ | Level shifter | 74AHCT125, quad buffer/line driver | [Amazon search](https://www.amazon.com/s?k=74AHCT125) | Needed on STEP/DIR lines: Teensy is 3.3 V, DM542T inputs are spec'd 4–5 V |
| 2 | Limit switches | Mechanical/optical, NC configuration | [Amazon](https://link.amazon/B0gYRXbEl) | Bench-verified: pin 7 = BOTTOM, pin 8 = TOP; idle LOW, tripped HIGH |
| — | Fasteners / misc | — | — | See Fusion 360 assembly BOM export for exact hardware call-outs |

## Not yet finalized

- Touchscreen: leaning ESP32-S3 + capacitive LCD (LVGL over UART) vs. Nextion NX4827P043-011C vs. Teensy-driven SPI TFT — see `docs/rebuild-architecture.md`
- Laser/LED module for the optical sub-project — see project notes
