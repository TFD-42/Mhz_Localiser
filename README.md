# Geo-Flip

<img width="1254" height="1254" alt="Geo-Flip" src="https://github.com/user-attachments/assets/0504201a-e1e5-4770-84fc-4c2617cee623" />

**RF signal triangulation system** — Flipper Zero streams live RSSI over USB to an Android app that logs GPS + signal and estimates the transmitter location on a map.

> **Honest disclaimer:** This is a low-cost, mobile RSSI-based approach. It gives *approximate* results, not GPS-grade precision. Read the [Physical Limitations](#physical-limitations) section before drawing conclusions from the output.

```
Mhz_Localise/
├── README.md
├── Build/
│   ├── rf_logger/          ← Flipper Zero FAP source (compile with fbt)
│   │   ├── rf_logger.c
│   │   ├── application.fam
│   │   └── rf_logger_icon.png
│   └── android_app/
│       ├── www/            ← Web UI (Capacitor)
│       │   ├── index.html
│       │   ├── app.js
│       │   └── styles.css
│       └── plugin/
│           └── FlipperSerialPlugin.java  ← Native USB serial plugin
└── Ready_To_Go/
    ├── RF_Triangulator.apk ← Install directly on Android
    └── rf_logger.fap       ← Copy to Flipper SD card
```

---

## How it works

1. **Flipper Zero** runs `rf_logger.fap` — a Sub-GHz RSSI logger that continuously samples signal strength on a chosen frequency and streams readings over USB as CSV.
2. **Android app** (`RF_Triangulator.apk`) connects to the Flipper via USB-C, reads the live RSSI stream, and logs GPS + signal captures on a map.
3. With **3 or more captures** from different positions, the app runs a Nelder-Mead least-squares solver to estimate the transmitter location.

---

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


<img width="1080" height="2340" alt="Screenshot_20260516_042105_RF Triangulator" src="https://github.com/user-attachments/assets/a599f565-cc7c-48fc-8fb9-d90e0f309d09" />

<img width="1080" height="2340" alt="Screenshot_20260516_042056_RF Triangulator" src="https://github.com/user-attachments/assets/91eed134-afd7-4c2c-b25a-fe03c9385fc4" />

<img width="592" height="277" alt="Capture d’écran 2026-05-16 à 04 24 04" src="https://github.com/user-attachments/assets/403ff643-b638-4202-bbda-3b5e558f921a" />


---

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
   └─ Low RMS (<30 m) = circles agree well, high confidence
      High RMS (>100 m) = noisy environment or bad geometry
      Re-capture from better positions if RMS is high
```

---

## Field Validation

Tests conducted with a **433.92 MHz transmitter** (generic keyfob, ~10 dBm output), Flipper Zero hardware, and a Pixel 7 with GPS lock (accuracy ±5–8 m).

| Environment | Captures | Real distance | Estimated distance | RMS error | Notes |
|---|---|---|---|---|---|
| Open field | 5 | 120 m | 133 m | **13 m** | Clean line-of-sight, flat terrain |
| Open field | 4 | 80 m | 91 m | **22 m** | Light wind, same conditions |
| Suburban street | 5 | 65 m | 88 m | **38 m** | Parked cars, low buildings |
| Dense urban | 6 | 80 m | 210 m | **130 m** | Multi-storey buildings, reflections |
| Dense urban | 5 | 50 m | 145 m | **95 m** | Corner geometry, NLOS |
| Indoor (office) | 4 | 30 m | unstable | **—** | Walls, furniture, multipath — unusable |
| Indoor (warehouse) | 5 | 40 m | 67 m | **51 m** | Large open space, concrete walls |

**Key takeaways:**
- Open field with good capture geometry: **error under 25 m** consistently.
- Suburban environments: **30–60 m error** is realistic.
- Dense urban or indoor: results degrade sharply. Use as a search area, not a precise fix.
- Captures in a straight line (bad geometry) inflated RMS by 3–4× vs. well-spread captures.

---

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

This model **assumes** that signal decays smoothly and predictably with distance. In practice:

| Assumption | Reality |
|---|---|
| Free propagation | Reflections from buildings, vehicles, ground |
| Single path | Multipath — multiple copies of signal arrive with different phases |
| Static environment | People walking, doors opening change RSSI by 5–15 dB |
| Isotropic antenna | Flipper's PCB antenna has directional variation |
| Known Tx power | Actual output may differ from nominal |

**Multipath** is the primary failure mode. In urban environments, a signal reflected off a building wall can arrive *stronger* than the direct path, making the solver place the estimated source in completely the wrong direction. This is why the dense urban results above show 130 m error for an 80 m actual distance.

**The path-loss exponent `n`** is the biggest tuning knob:
- `n = 2.0` — free space, vacuum or very open field
- `n = 2.7–3.5` — typical outdoor urban
- `n = 3.5–5.0` — indoor, heavy obstruction

Wrong `n` = systematically biased distance estimates = bad triangulation even with good geometry.

### What this tool cannot do

- Real-time tracking of a moving transmitter
- Sub-10 m precision in any environment
- Direction finding (it has no antenna array)
- Work reliably indoors without tuning `n` per-room
- Replace dedicated DF equipment for serious applications

---

## Comparison with Real RF Equipment

| Solution | Method | Typical accuracy | Cost |
|---|---|---|---|
| **Geo-Flip** (this project) | RSSI trilateration, mobile | 15–150 m depending on environment | ~$60 (Flipper + phone) |
| KrakenSDR | 5-element coherent SDR phase array | 2–10 m outdoors | ~$500 |
| HackRF + directional antenna | Manual DF, SDR | 5–20 m with skill | ~$350 + antenna |
| RTL-SDR + Doppler DF | Software Doppler shift | 10–30 m | ~$30 + antenna |
| Professional TDOA systems | Time-difference of arrival | <1 m | $5,000–$50,000 |

Geo-Flip is the **lowest cost and lowest barrier** option. It trades precision for accessibility — the entire system fits in a pocket and requires no RF expertise to operate. For hobbyist fox-hunting, lost-sensor searches, or educational RF experiments, the 15–50 m accuracy in open environments is genuinely useful.

---

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

---

## Quick start

### Step 1 — Install the Flipper app

1. Copy `Ready_To_Go/rf_logger.fap` to your Flipper SD card:
   ```
   /ext/apps/Sub-GHz/rf_logger.fap
   ```
2. On the Flipper: **Apps → Sub-GHz → RF Logger**
3. Select a frequency from the menu (or choose **Manual MHz…** to enter one).
4. The Flipper screen shows frequency, live RSSI (dBm), a signal bar, and sample count.
5. Press **OK** to toggle SD card logging on/off. Press **Back** to stop.

### Step 2 — Install the Android app

1. Enable **Install unknown apps** in Android settings (or use ADB):
   ```bash
   adb install Ready_To_Go/RF_Triangulator.apk
   ```
2. Grant **Location** permission when prompted.

### Step 3 — Connect and capture

1. Plug the Flipper into your Android phone with a **USB-C cable**.
2. Open **RF Triangulator** → tap **☰** → **Connect Flipper**.
3. Accept the USB permission dialog.
4. The top panel mirrors the Flipper display: frequency, RSSI, signal bar.
5. Walk to a position and tap **Capture here** — the app logs GPS + live RSSI.
6. Repeat from **3+ different positions** surrounding the suspected transmitter.
7. The drawer shows the triangulation estimate with RMS error once you have 3+ captures.

### Auto-capture mode

In the menu, tap **Auto-capture** to log a reading automatically at a set interval (1 s / 2 s / 5 s / 10 s) while you move around. Tap **Stop Auto** to end.

### Import Flipper files

If you captured data directly on the Flipper SD card, tap **☰ → Load .sub / .log** to import. Supports `.sub`, `.log`, `.csv`, `.txt` with `RSSI:` and `Latitude/Longitude:` fields.

### Export

- **CSV** — one row per capture: `id, lat, lon, rssi_dbm, freq_hz, source`
- **JSON** — full capture list + triangulation estimate

---

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

---


## USB Serial Protocol

Flipper streams CSV over USB CDC-ACM at **115200 baud**:

```
# RF_LOGGER_DBG req=433920000 act=433920000
ts_ms,req_hz,act_hz,rssi_dbm,rssi_raw,lqi,n
1234567,433920000,433920000,-85,0x57,12,1
1234767,433920000,433920000,-83,0x59,14,2
```

| Column | Type | Description |
|---|---|---|
| `ts_ms` | uint64 | Uptime milliseconds |
| `req_hz` | uint32 | Requested frequency Hz |
| `act_hz` | uint32 | Actual tuned frequency Hz |
| `rssi_dbm` | int | Signal strength dBm |
| `rssi_raw` | hex | Raw CC1101 RSSI register byte |
| `lqi` | uint8 | Link Quality Indicator |
| `n` | uint32 | Sample counter |

Sample rate: **200 ms (5 Hz)**. Frequency is auto-detected by the Android app from the `req_hz` field — no manual input needed.

---

## Requirements

| Component | Requirement |
|---|---|
| Flipper Zero | Firmware 0.97+ (official or Unleashed) |
| Android | 8.0+ (API 26), USB OTG support |
| USB cable | USB-C to USB-C (or USB-A OTG adapter) |
| GPS | Required for automatic capture; indoor = poor accuracy |


