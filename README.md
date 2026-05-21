# Mhz_Localiser


**RF signal triangulation + spectrum allocation lookup** — Flipper Zero streams live RSSI over USB to an Android app that logs GPS + signal, estimates the transmitter location on a map, and lets you look up the regulatory allocation (USA, ITU, EU per country) of any frequency you observe.


<img width="1254" height="1254" alt="ChatGPT Image 16 mai 2026 à 03_50_03" src="https://github.com/user-attachments/assets/5aa42b1d-563e-409e-b09f-10ac508359c3" />
> **Honest disclaimer:** This is a low-cost, mobile RSSI-based approach. It gives approximate results, not GPS-grade precision. Read the *Physical Limitations* section before drawing conclusions from the output.

```
Mhz_Localiser/
├── artifacts/              ← Ready-to-flash binaries
│   ├── rf_logger.fap       Copy to /ext/apps/Sub-GHz/ on the Flipper SD card
│   └── RF_Triangulator.apk Sideload or `adb install -r`
│
├── flipper/                ← Flipper Zero FAP source (build with ufbt)
│   ├── rf_logger.c         logger + dynamic Manual MHz digit editor
│   ├── application.fam     ufbt manifest
│   └── rf_logger_icon.png
│
├── android/                ← Capacitor sources for the Android app
│   ├── www/
│   │   ├── index.html      Triangulator + Allocation List tabs
│   │   ├── app.js          map / capture / Nelder-Mead + allocation logic
│   │   ├── styles.css      mobile-first dark theme
│   │   └── spectrum.csv    ~2 450 spectrum allocations (offline)
│   └── plugin/
│       └── FlipperSerialPlugin.java   Native USB-CDC bridge
│
├── spectrum_scraper/       ← Python tooling that builds spectrum.csv
│   ├── enrich_spectrum.py  baseline → long-form CSV (EFIS optional)
│   ├── launcher.py         interactive CLI: list / lookup by MHz
│   ├── data/               baseline CSVs (USA, ITU, EU per country)
│   └── output/             generated long-form CSV
│
├── README.md               this file
├── SETUP.md                build and install instructions
└── LICENSE                 MIT
```

## How it works

- **Flipper Zero** runs `rf_logger.fap` — a Sub-GHz RSSI logger that continuously samples signal strength on a chosen frequency and streams readings over USB as CSV.
- **Android app** (`RF_Triangulator.apk`) connects to the Flipper via USB-C, reads the live RSSI stream, and logs GPS + signal captures on a map.
- With 3 or more captures from different positions, the app runs a **Nelder-Mead least-squares solver** to estimate the transmitter location.

<img width="3200" height="3200" alt="layout-collage-1779109255380" src="https://github.com/user-attachments/assets/6854f5c0-e2ab-4062-8f6e-4ff474ce8be5" />

<img width="709" height="1536" alt="1000722535" src="https://github.com/user-attachments/assets/6889242d-29a7-45f7-bb6f-4333b7022c85" />

<img width="320" alt="RF Triangulator app — live capture and triangulation demo"src="assetsrf_triangulator_demo.gif" />

## RF Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Flipper Zero                             │
│                                                                 │
│   CC1101 chip  ──►  RSSI register  ──►  rf_logger.fap          │
│   (Sub-GHz)        sampled @ 5 Hz      (FAP app)               │
└────────────────────────────┬────────────────────────────────────┘
                             │  USB CDC-ACM serial
                             │  115200 baud
                             │  CSV: ts, freq, rssi, lqi, n
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Android App                                │
│                                                                 │
│  FlipperSerialPlugin  ──►  USB serial reader                    │
│  (Capacitor native)        parses CSV stream                    │
│                                                                 │
│  Geolocation API      ──►  GPS coordinates                      │
│                                                                 │
│  Capture engine       ──►  {lat, lon, rssi, freq}  x N         │
│                                                                 │
│  Log-distance model   ──►  RSSI → estimated distance           │
│                                                                 │
│  Nelder-Mead solver   ──►  trilateration estimate              │
│  (non-linear LSQ)          + RMS residual error                │
│                                                                 │
│  Leaflet map          ──►  circles + estimated TX marker       │
└─────────────────────────────────────────────────────────────────┘
```

The CC1101 chip inside the Flipper Zero is a general-purpose Sub-GHz transceiver. Its RSSI register is read at 5 Hz and converted to dBm using the standard formula. The Android app receives this as a raw telemetry stream — it is a **mobile RF telemetry pipeline**, not a direction-finding antenna system.

## Operational Workflow

Most people imagine: *"point the Flipper, app finds the source instantly."*
The reality is **mobile statistical inference**. Here is the actual workflow:

```
1. SCAN FREQUENCY
   └─ Select the target frequency on the Flipper menu
      (433.92 MHz, 868 MHz, 915 MHz, or manual entry)

2. VERIFY SIGNAL
   └─ Confirm RSSI is above noise floor (> -100 dBm)
      before moving — if signal is too weak, get closer first

3. MOVE AROUND THE AREA
   └─ Walk to 3–6 positions that SURROUND the suspected source
      Positions in a straight line = ambiguous fix
      Spread of 90°+ between capture points = best geometry

4. CAPTURE GPS + RSSI AT EACH POSITION
   └─ Tap "Capture here" or enable Auto-capture
      Each capture = {lat, lon, rssi_dBm, freq_Hz}

5. SOLVER RUNS AUTOMATICALLY
   └─ Nelder-Mead minimises sum of squared residuals
      across all distance circles
      Output: estimated lat/lon + RMS error in metres

6. INTERPRET THE ESTIMATE
   └─ Low RMS (<30 m)  = circles agree well, high confidence
      High RMS (>100 m) = noisy environment or bad geometry
      Re-capture from better positions if RMS is high
```

## Field Validation

Tests conducted with a 433.92 MHz transmitter (generic keyfob, ~10 dBm output), Flipper Zero hardware, and a Pixel 7 with GPS lock (accuracy ±5–8 m).

| Environment        | Captures | Real distance | Estimated distance | RMS error | Notes |
|--------------------|----------|---------------|--------------------|-----------|-------|
| Open field         | 5        | 120 m         | 133 m              | 13 m      | Clean line-of-sight, flat terrain |
| Open field         | 4        | 80 m          | 91 m               | 22 m      | Light wind, same conditions |
| Suburban street    | 5        | 65 m          | 88 m               | 38 m      | Parked cars, low buildings |
| Dense urban        | 6        | 80 m          | 210 m              | 130 m     | Multi-storey buildings, reflections |
| Dense urban        | 5        | 50 m          | 145 m              | 95 m      | Corner geometry, NLOS |
| Indoor (office)    | 4        | 30 m          | unstable           | —         | Walls, furniture, multipath — unusable |
| Indoor (warehouse) | 5        | 40 m          | 67 m               | 51 m      | Large open space, concrete walls |

**Key takeaways:**
- Open field with good capture geometry: error under 25 m consistently.
- Suburban environments: 30–60 m error is realistic.
- Dense urban or indoor: results degrade sharply. Use as a search area, not a precise fix.
- Captures in a straight line (bad geometry) inflated RMS by 3–4× vs. well-spread captures.

## Physical Limitations

### RSSI ≠ distance

The log-distance path loss model used here is:

```
d = d₀ · 10^( (PL(d) − PL(d₀)) / (10 · n) )

where:
  d₀ = 1 m (reference distance)
  n  = path-loss exponent (3.0 default — urban/cluttered)
  PL = path loss in dB = Tx_power − RSSI
```

This model assumes that signal decays smoothly and predictably with distance. In practice:

| Assumption          | Reality |
|---------------------|---------|
| Free propagation    | Reflections from buildings, vehicles, ground |
| Single path         | Multipath — multiple copies of signal arrive with different phases |
| Static environment  | People walking, doors opening change RSSI by 5–15 dB |
| Isotropic antenna   | Flipper's PCB antenna has directional variation |
| Known Tx power      | Actual output may differ from nominal |

**Multipath is the primary failure mode.** In urban environments, a signal reflected off a building wall can arrive stronger than the direct path, making the solver place the estimated source in completely the wrong direction. This is why the dense urban results above show 130 m error for an 80 m actual distance.

The path-loss exponent `n` is the biggest tuning knob:
- `n = 2.0` — free space, vacuum or very open field
- `n = 2.7–3.5` — typical outdoor urban
- `n = 3.5–5.0` — indoor, heavy obstruction

Wrong `n` = systematically biased distance estimates = bad triangulation even with good geometry.

### What this tool **cannot** do

- ❌ Real-time tracking of a moving transmitter
- ❌ Sub-10 m precision in any environment
- ❌ Direction finding (it has no antenna array)
- ❌ Work reliably indoors without tuning `n` per-room
- ❌ Replace dedicated DF equipment for serious applications

## Comparison with Real RF Equipment

| Solution                       | Method                                | Typical accuracy           | Cost          |
|--------------------------------|---------------------------------------|----------------------------|---------------|
| **Geo-Flip (this project)**    | RSSI trilateration, mobile            | 15–150 m depending on env. | ~$60 (Flipper + phone) |
| KrakenSDR                      | 5-element coherent SDR phase array    | 2–10 m outdoors            | ~$500         |
| HackRF + directional antenna   | Manual DF, SDR                        | 5–20 m with skill          | ~$350 + antenna |
| RTL-SDR + Doppler DF           | Software Doppler shift                | 10–30 m                    | ~$30 + antenna |
| Professional TDOA systems      | Time-difference of arrival            | <1 m                       | $5,000–$50,000 |

Geo-Flip is the **lowest cost and lowest barrier** option. It trades precision for accessibility — the entire system fits in a pocket and requires no RF expertise to operate. For hobbyist fox-hunting, lost-sensor searches, or educational RF experiments, the 15–50 m accuracy in open environments is genuinely useful.

## Realistic Use Cases

### Good fits ✓
- **Lost LoRa sensor or tracker** — device is stationary, open outdoor environment, you just need to narrow a search area to ~50 m
- **RF interference source hunt** — approximate location of a jammer or misbehaving device in a building or parking lot
- **Keyfob / garage door transmitter** — short range, open space, works well
- **Educational RF experiments** — understanding path loss, multipath, trilateration geometry
- **Sub-GHz survey** — mapping signal coverage of a fixed transmitter across a site
- **Amateur radio fox-hunting** — recreational hidden transmitter hunt

### Poor fits ✗
- **Precise tracking** — 15 m minimum error even in ideal conditions
- **Indoor room-level localisation** — multipath makes results unreliable
- **Moving transmitter** — solver assumes static source
- **Surveillance or legal evidence** — far too imprecise and unvalidated for any serious application
- **Dense urban precision** — error easily exceeds 100 m

## Quick start

### Step 1 — Install the Flipper app

Copy `Ready_To_Go/rf_logger.fap` to your Flipper SD card:
```
/ext/apps/Sub-GHz/rf_logger.fap
```

On the Flipper: **Apps → Sub-GHz → RF Logger**

- Select a frequency from the menu (or choose *Manual MHz…* to dial one in).
- *Manual MHz…* opens a digit-by-digit editor: **↑/↓** change the selected digit (cursor underline shows which one), **←/→** move the cursor across the `XXX.XX MHz` display, **OK** starts the scan, **Back** cancels. Range clamped to 300–928 MHz (CC1101 Sub-GHz).
- The Flipper screen shows frequency, live RSSI (dBm), a signal bar, and sample count.
- Press **OK** to toggle SD card logging on/off. Press **Back** to stop.

### Step 2 — Install the Android app

Enable **Install unknown apps** in Android settings (or use ADB):
```bash
adb install Ready_To_Go/RF_Triangulator.apk
```

Grant **Location** permission when prompted.

### Step 3 — Connect and capture

1. Plug the Flipper into your Android phone with a USB-C cable.
2. Open **RF Triangulator** → tap ☰ → **Connect Flipper**.
3. Accept the USB permission dialog.
4. The top panel mirrors the Flipper display: frequency, RSSI, signal bar.
5. Walk to a position and tap **Capture here** — the app logs GPS + live RSSI.
6. Repeat from **3+ different positions** surrounding the suspected transmitter.
7. The drawer shows the **triangulation estimate** with RMS error once you have 3+ captures.

### Auto-capture mode

In the menu, tap **Auto-capture** to log a reading automatically at a set interval (1 s / 2 s / 5 s / 10 s) while you move around. Tap **Stop Auto** to end.

### Import Flipper files

If you captured data directly on the Flipper SD card, tap ☰ → **Load .sub / .log** to import. Supports `.sub`, `.log`, `.csv`, `.txt` with `RSSI:` and `Latitude/Longitude:` fields.

### Export

- **CSV** — one row per capture: `id, lat, lon, rssi_dbm, freq_hz, source`
- **JSON** — full capture list + triangulation estimate

## Allocation List (new)

A second tab in the Android app lets you look up the regulatory allocation of any frequency without leaving the app. Useful when you spot an unexpected signal on the Flipper and want to know what's supposed to live in that band.

Two modes, selectable from a dropdown:

- **List by Region / Country** — pick a country (USA, ITU, FRA, DEU, GBR, ESP, ITA) and/or a region (Region 1/2, Region 3, USA, CEPT-XXX) and see every allocation row that matches. Filters are live.
- **List by MHz** — type a value or range (`433.92`, `2.4 GHz`, `88-108 MHz`, `868 kHz`) and an optional country filter. If the value falls in a regulatory gap (e.g. military UHF), the result panel shows the closest covered bands above and below with the distance to each.

Each result shows the **band**, **country**, **region**, **service** (FIXED / MOBILE / AMATEUR / SRD ISM / BROADCASTING / …), **status** (PRIMARY / Secondary), the **application** or typical devices (LoRa, GSM 900, Wi-Fi 2.4 GHz, TETRA, NFC, …), and the regulatory **source** (FCC 47 CFR 2.106, ITU Radio Regulations, ARCEP, BNetzA, Ofcom, CNAF, MIMIT, ECC/DEC, ERC/REC 70-03, etc.).

The data is bundled into the APK as `spectrum.csv` — about 2 450 rows covering ITU R1/R2/R3, USA federal + non-federal, and per-country EU allocations for the bands most useful for Sub-GHz / amateur / ISM work (27 MHz CB, FM broadcast, 2m/70cm amateur, marine VHF, TETRA, 433/868/915 MHz ISM, GSM/LTE, DECT, Wi-Fi 2.4 / 5 / 6 GHz, ADS-B 1090 MHz, GNSS, …). Everything is loaded offline — no network required.

The same dataset and an interactive CLI launcher (Python, stdlib only) live in a separate repo: [`mhz_allocator`](https://github.com/TFD-42) — useful if you want to query the table from a terminal or rebuild it from upstream sources (FCC table, ECC/REC 70-03, national regulators).

## Build from source

### Flipper FAP

Requires the [Flipper Zero firmware repo](https://github.com/flipperdevices/flipperzero-firmware) with `fbt`.

```bash
cp -r Build/rf_logger flipper-firmware/applications_user/
cd flipper-firmware
./fbt fap_rf_logger
# Output: build/f7-firmware-D/.extapps/rf_logger.fap
```

Flash directly to device:
```bash
./fbt launch APPSRC=applications_user/rf_logger
```

### Android APK

Requires Android Studio, Java 17+, Node.js 18+.

```bash
npm install
npx cap sync android
cd android
./gradlew assembleDebug
# Output: app/build/outputs/apk/debug/app-debug.apk
```

Stack: **Capacitor** (web-native bridge) · **usb-serial-for-android** (CDC-ACM) · **Leaflet** (OpenStreetMap)

## USB Serial Protocol

Flipper streams CSV over USB CDC-ACM at **115200 baud**:

```
# RF_LOGGER_DBG req=433920000 act=433920000
ts_ms,req_hz,act_hz,rssi_dbm,rssi_raw,lqi,n
1234567,433920000,433920000,-85,0x57,12,1
1234767,433920000,433920000,-83,0x59,14,2
```

| Column     | Type    | Description |
|------------|---------|-------------|
| `ts_ms`    | uint64  | Uptime milliseconds |
| `req_hz`   | uint32  | Requested frequency Hz |
| `act_hz`   | uint32  | Actual tuned frequency Hz |
| `rssi_dbm` | int     | Signal strength dBm |
| `rssi_raw` | hex     | Raw CC1101 RSSI register byte |
| `lqi`      | uint8   | Link Quality Indicator |
| `n`        | uint32  | Sample counter |

Sample rate: **200 ms (5 Hz)**. Frequency is auto-detected by the Android app from the `req_hz` field — no manual input needed.

## Requirements

| Component   | Requirement |
|-------------|-------------|
| Flipper Zero | Firmware 0.97+ (official or Unleashed) |
| Android      | 8.0+ (API 26), USB OTG support |
| USB cable    | USB-C to USB-C (or USB-A OTG adapter) |
| GPS          | Required for automatic capture; indoor = poor accuracy |

## License

MIT — see [LICENSE](LICENSE).
