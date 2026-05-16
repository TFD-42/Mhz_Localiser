# Mhz_Localise


<img width="1254" height="1254" alt="ChatGPT Image 16 mai 2026 à 03_50_03" src="https://github.com/user-attachments/assets/0504201a-e1e5-4770-84fc-4c2617cee623" />


RF signal triangulation system — Flipper Zero streams live RSSI over USB to an Android app that logs GPS + signal and estimates the transmitter location on a map.

```
Mhz_Localise/
├── README.md               ← you are here
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

# If Auto Frequancy Freeze In APK Disconect USB From Android And Reconnect


---

## Build from source

### Flipper FAP

Requires the [Flipper Zero firmware repo](https://github.com/flipperdevices/flipperzero-firmware) with `fbt`.

```bash
# Place the rf_logger folder inside applications_user/
cp -r Build/rf_logger flipper-firmware/applications_user/

cd flipper-firmware
./fbt fap_rf_logger
# Output: build/f7-firmware-D/.extapps/rf_logger.fap
```

Flash to device:
```bash
./fbt launch APPSRC=applications_user/rf_logger
```

### Android APK

Requires Android Studio, Java 17+, Node.js 18+.

```bash
# From the rf-triangulator Capacitor project root:
npm install
npx cap sync android
cd android
./gradlew assembleDebug
# Output: app/build/outputs/apk/debug/app-debug.apk
```

The project uses:
- **Capacitor** — web-to-native bridge
- **usb-serial-for-android** (mik3y, CDC-ACM) — USB serial communication
- **Leaflet** — map rendering (OpenStreetMap tiles)

---

## USB serial protocol

The Flipper streams CSV over USB CDC-ACM at **115200 baud**:

```
# RF_LOGGER_DBG req=433920000 act=433920000
ts_ms,req_hz,act_hz,rssi_dbm,rssi_raw,lqi,n
1234567,433920000,433920000,-85,0x57,12,1
1234767,433920000,433920000,-83,0x59,14,2
...
```

| Column | Type | Description |
|--------|------|-------------|
| `ts_ms` | uint64 | Uptime in milliseconds |
| `req_hz` | uint32 | Requested frequency Hz |
| `act_hz` | uint32 | Actual tuned frequency Hz |
| `rssi_dbm` | int | Signal strength dBm |
| `rssi_raw` | hex | Raw CC1101 RSSI register byte |
| `lqi` | uint8 | Link Quality Indicator |
| `n` | uint32 | Sample counter |

Sample rate: **200 ms** (5 Hz).

---

## Propagation model

Distances are estimated using the **log-distance path loss model**:

```
PL(d) = PL(d₀) + 10 · n · log₁₀(d / d₀)
```

- Reference distance `d₀ = 1 m`
- Path-loss exponent `n = 3.0` (urban/cluttered environment)
- Tx power assumed `+10 dBm`
- Frequency auto-detected from Flipper stream

For best triangulation accuracy: capture from **well-separated positions** that surround the source. Three captures in a straight line will give an ambiguous fix. The RMS residual in the estimate panel indicates how well your circles agree.

---

## Requirements

| Component | Requirement |
|-----------|-------------|
| Flipper Zero | Firmware 0.97+ (official or Unleashed) |
| Android | 8.0+ (API 26), USB OTG support |
| USB cable | USB-C to USB-C (or USB-A OTG adapter) |
| GPS | Required for automatic capture; indoor = poor accuracy |
