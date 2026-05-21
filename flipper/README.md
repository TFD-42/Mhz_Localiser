# flipper — Flipper Zero FAP source

Source for `rf_logger.fap`, the Sub-GHz RSSI logger that runs on the Flipper Zero.

## Build

With [ufbt](https://github.com/flipperdevices/flipperzero-ufbt):

```bash
ufbt              # build → dist/rf_logger.fap
ufbt launch       # build + flash + run on connected Flipper
```

Output: `dist/rf_logger.fap`. Copy to your Flipper SD card under `/ext/apps/Sub-GHz/`.

## Files

| File | Description |
|---|---|
| `rf_logger.c` | Application logic: state machine, viewport drawing, input handling, USB CDC, SD logging, sub-GHz tuning |
| `application.fam` | ufbt manifest: appid, name, category, icon, version, dependencies |
| `rf_logger_icon.png` | 10x10 1-bit icon shown in the Apps menu |

## Architecture

```
StateMenu                       (preset list: 315 / 433.92 / 868.35 / 915 / Manual)
    │
    └─[OK on "Manual MHz…"]──► StateManualEntry  (XXX.XX MHz digit editor)
                                    │
                                    └─[OK]────────────┐
StateMenu                                              │
    │                                                  ▼
    └─[OK on preset]────────────────────────────► StateRunning  (scan + log + stream)
                                                       │
                                                       └─[Back]──► StateMenu
```

## Manual MHz digit editor

Layout: `XXX.XX MHz` with a 1-pixel underline under the active digit.

| Input | Action |
|---|---|
| ↑ | Increment selected digit (0→1→…→9→0) |
| ↓ | Decrement selected digit |
| ← | Move cursor left |
| → | Move cursor right |
| OK | Start scan at the displayed frequency |
| Back | Cancel, return to preset menu |

Range is clamped to **300.00 – 928.00 MHz** (CC1101 Sub-GHz). Digit changes are independent — rolling a `9` up gives `0` of the same rank, never carries into the neighbour digit. This matches user expectation for a position-by-position editor (calculator-style, not number-spinner-style).

Cursor starts on the **ones-of-MHz** digit (position 2), with the initial value at `433.92 MHz`.

## SD logging

When you press OK in StateRunning the app creates (or appends to) a CSV file:
```
/ext/apps_data/rf_logger/log_<tick>.csv
ts_ms,req_hz,act_hz,rssi_dbm,rssi_raw,lqi,n
…
```

The directory is created on first use via `storage_simply_mkdir`.

## USB CDC

When entering StateRunning, the app takes over the USB stack with `usb_cdc_dual` (two CDC ACM interfaces). Interface 0 keeps the Flipper CLI live (so `qFlipper` etc. keep working); interface 1 streams the CSV at 5 Hz. On exit, the original USB config is restored.

## Code size

```
329 lines  — original (preset-only)
448 lines  — current (preset + manual digit editor)
```

Compiled binary: ~8 700 bytes (.fap), well within the ~64 KB FAP limit.

## License

MIT — see top-level LICENSE.
