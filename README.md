# Open Winder

A DIY guitar pickup coil winder built around a Teensy 4.1: standalone touchscreen operation (planned), tension feedback via load cell, EEPROM/flash profile storage, and wind logging.

This repo is open source. Ready-made/printed components based on this design are also sold separately by [Pazzatronics](https://www.pazzatronics.com) — building it yourself from these files is always free and unrestricted under the terms below.

## Repo layout

```
firmware/       Teensy 4.1 sketch and any supporting libraries
hardware/
  fusion360/    Native Fusion 360 project files
  stl/          Print-ready STL files (all parts printed in PETG)
  step/         Exported STEP files (winder body, brackets, load-cell pulley, etc.)
  wiring/       Pinout tables, wiring notes, and open_winder_wiring.csv
  bom.md        Bill of materials with sourcing links
docs/           Bug write-ups, firmware architecture notes, command protocol
```

## Hardware stack

| Part | Component |
|---|---|
| MCU | Teensy 4.1 (3.3 V logic) |
| Stepper driver | DM542T |
| Winder motor | 23HS45-4204S (4.2 A, 3 Nm) |
| PSU | LRS-350-24 (24 V) |
| Load cell | HX711 amplifier + parallel-beam 1 kg load cell |
| Flash storage | Winbond W25Q64JV (QSPI, LittleFS) |
| Level shifting | 74AHCT125 (Teensy 3.3 V → driver 5 V logic) |

Full sourcing and links: [`hardware/bom.md`](hardware/bom.md)

## Status

This is an active, evolving hobby project — see [`docs/bugs-and-fixes.md`](docs/bugs-and-fixes.md) for known firmware issues and their fix status, and [`docs/rebuild-architecture.md`](docs/rebuild-architecture.md) for the planned ISR-based firmware rewrite.

## License

- **Firmware** (`firmware/`): [MIT](LICENSE-FIRMWARE) — do whatever you want with it, attribution appreciated.
- **Hardware** (`hardware/`): [CERN-OHL-S v2](LICENSE-HARDWARE) — you're free to build, modify, and even sell things made from these designs, but if you distribute a modified version of the hardware design itself, you need to share those modifications under the same license.

Questions about licensing or commercial use beyond what these licenses already allow: open an issue or reach out directly.
