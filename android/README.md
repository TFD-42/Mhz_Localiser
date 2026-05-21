# android — Capacitor sources for RF Triangulator

This is the project-specific Capacitor content: the web UI in `www/` and the native Java plugin in `plugin/`. The full Android Studio project structure (gradle files, AndroidManifest.xml, MainActivity.java, etc.) is regenerated from `package.json` + `npx cap add android` — see top-level `SETUP.md`.

## Files

```
www/
├── index.html       App shell with two tabs (Triangulator, Allocation List)
├── app.js           Logic: USB plugin bridge, map, capture, Nelder-Mead solver, allocation lookup
├── styles.css       Dark theme, mobile-first
└── spectrum.csv     ~2 450 spectrum allocations (offline lookup table)

plugin/
└── FlipperSerialPlugin.java   Capacitor plugin: USB host, CDC-ACM driver, CSV parser
```

## Build

See top-level `SETUP.md` for the full Capacitor bootstrap. Once the project is scaffolded:

```bash
# In your Capacitor project root after copying www/ + plugin
npx cap sync android
cd android && ./gradlew assembleDebug
# Output: app/build/outputs/apk/debug/app-debug.apk
```

Install on a connected device:
```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## www/ — UI

### Triangulator tab (default)
- Leaflet map fullscreen
- Top bar mirrors live Flipper readout: frequency + RSSI + signal bar
- Bottom bar: **Capture here**, **Auto-capture**, **Solve**
- Drawer (☰): connect/disconnect Flipper, import/export, capture list, estimate readout

### Allocation List tab
Two modes selectable from a dropdown:

| Mode | Inputs | Behaviour |
|---|---|---|
| **List by Region / Country** | Country select, Region select | Live filter as you change selections |
| **List by MHz** | Text input (`433.92`, `2.4 GHz`, `88-108 MHz`, `868 kHz`) + Country select | Search on button click or Enter. Suggests nearest covered bands if value falls in a gap |

Results table columns: **MHz range / Country / Region / Service / Status / Application / Source**. PRIMARY in green, Secondary in orange. Tooltip on hover shows the typical-devices list and the regulator note.

Limited to 500 rows on screen for performance; refine filters to see more.

## plugin/ — FlipperSerialPlugin.java

Capacitor plugin bridging the WebView to USB-CDC serial. Methods exposed to JS:

| Method | Purpose |
|---|---|
| `connect()` | Find a Flipper (VID 0x0483 / PID 0x5740), request USB permission, open the second CDC port |
| `disconnect()` | Stop reader thread, close port |
| `startStream()` | Spawn a background thread that reads bytes, splits on `\n`, parses CSV, emits `rssiData` events |
| `stopStream()` | Kill the reader thread |
| `sendCommand({cmd})` | Write a text command to the Flipper (reserved for future use) |
| `getLatestRssi()` | Synchronous getter for the last parsed sample |

### CSV parser
Accepts both formats the firmware has used over time:
- v1 (decimal MHz):   `1234,433.920,433.920,-85,0x57,12,1`
- v2 (integer Hz):    `1234,433920000,433920000,-85,0x57,12,1`

Detection: presence of `.` in the second field. The plugin also recognises the firmware retune marker (`# RF_LOGGER_DBG req=… act=…`) and emits a freq-change event so the UI updates instantly without waiting for the next sample.

### Dependencies
Add to `android/app/build.gradle`:
```gradle
dependencies {
    implementation 'com.github.mik3y:usb-serial-for-android:3.7.0'
}
```

Register the plugin in `android/app/src/main/java/com/mhzlocalise/rftriangulator/MainActivity.java`:
```java
import com.mhzlocalise.rftriangulator.FlipperSerialPlugin;

@Override
public void onCreate(Bundle savedInstanceState) {
    registerPlugin(FlipperSerialPlugin.class);
    super.onCreate(savedInstanceState);
}
```

## License

MIT — see top-level LICENSE.
