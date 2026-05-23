# PLAN — Add Bluetooth (BLE) transport alongside USB

Target: support **both** USB-CDC (existing) and **BLE serial** so the user picks
the transport at runtime on the phone and on the Flipper. Flipper target:
third-party firmware (Unleashed / Momentum / RogueMaster), which exposes the
"Serial" BLE profile and lets a FAP register a custom GATT service.

The current data path is hardcoded in three places:
- `flipper/rf_logger.c` — writes CSV via `furi_hal_cdc_send(VCP_DATA_CH, …)`.
- `android/plugin/FlipperSerialPlugin.java` — opens the Flipper as USB-CDC.
- `android/www/app.js` — single Capacitor bridge (`window.Capacitor.Plugins.FlipperSerial`).

The work below introduces a **transport abstraction** at all three layers.

---

## 1. Flipper FAP — `flipper/rf_logger.c`

### 1.1 Add transport enum and v-table
Insert near the top of the file, after the `AppState` enum:

```c
typedef enum { TransportUsb, TransportBle } Transport;

typedef struct {
    bool (*open)(void);
    void (*close)(void);
    void (*write)(const uint8_t* buf, uint16_t len);
} TransportOps;
```

Wrap the two existing helpers and add a BLE pair:

- `usb_take` / `usb_release` / `cdc_write` / `cdc_printf` already exist
  (rf_logger.c:62-85) — group them into a `transport_usb_*` set.
- Add `transport_ble_open/close/write` that:
  - calls `furi_hal_bt_start_advertising()` and registers the Serial profile
    (`ble_profile_serial`) via `furi_hal_bt_change_app(BleProfileSerial, …)` on
    Momentum/Unleashed (API is in `applications/services/bt/bt_service/`).
  - on `write`, push bytes through `furi_hal_bt_serial_tx(buf, len)`.
  - on `close`, restore previous profile and stop advertising.

Replace every direct `furi_hal_cdc_send(...)` (currently at rf_logger.c:76, 84,
158) and `cdc_write/cdc_printf` callers (rf_logger.c:118-120) with
`app->transport->write(...)`.

### 1.2 Add transport selector to manual entry UI
Currently `RfLoggerApp` (rf_logger.c:40-60) holds USB state only. Add:

```c
Transport transport;          // currently selected
const TransportOps* ops;      // points at usb_ops or ble_ops
bool transport_focus;         // true = OK on manual screen toggles transport
```

In `draw_manual` (rf_logger.c:164-205), render an extra line under the digits:

```
Transport: [USB]   <- highlight when transport_focus
```

In `handle_manual_input` (rf_logger.c:280-303), reuse `InputKeyLeft/Right` when
`transport_focus` is set (e.g. long-press Left from cursor 0 enters focus) to
switch USB ↔ BLE. Re-bind OK so it always starts streaming once a transport is
chosen.

### 1.3 Lifecycle changes
- In `start_running` (rf_logger.c:269-277), replace `usb_take(app)` with
  `app->ops->open()`; reject start if open returns false.
- In the cleanup tail of `rf_logger_app` (rf_logger.c:366-378), replace
  `usb_release(app)` with `app->ops->close()`.
- The `# RF_LOGGER_DBG …` header and CSV header (rf_logger.c:118-120) must still
  be emitted on transport open so the phone can resync after re-pair.

### 1.4 BLE specifics for third-party firmware
- Add `requires = ["bt"]` to `flipper/application.fam` so the FAP links the BT
  service.
- Advertised name should include `RF_LOGGER_<short_id>` so the phone scanner can
  pick it from a list of paired Flippers.
- MTU on the Flipper Serial profile is ~244 bytes per write — each CSV line
  (~70 bytes) fits in one frame, so no fragmentation logic is required.

---

## 2. Android — split `FlipperSerialPlugin` into transport plugins

Rename `android/plugin/FlipperSerialPlugin.java` to
`android/plugin/UsbSerialPlugin.java` (Capacitor name stays `FlipperSerial` for
backwards compat, or rename to `FlipperUsb`). Then add a second plugin file.

### 2.1 New `android/plugin/FlipperBlePlugin.java`
Capacitor plugin exposing the same JS surface as the USB one:

| Method | Behaviour |
|---|---|
| `scan({timeoutMs})` | BLE scan with `ScanFilter` on advertised name prefix `RF_LOGGER_` or service UUID, returns `{devices:[{name,address,rssi}]}` |
| `connect({address})` | Bond if needed, open `BluetoothGatt`, discover services, enable notifications on the Flipper Serial RX characteristic |
| `disconnect()` | `gatt.disconnect()` + `gatt.close()` |
| `isConnected()` | reports last GATT connection state |
| `addListener('data', …)` | emits `{line:"…"}` per `\n`-terminated chunk |
| `addListener('status', …)` | `connecting`, `connected`, `disconnected`, `error` |

Reuse the line buffer logic at FlipperSerialPlugin.java:259-275 — same parser,
different byte source.

UUIDs to target on Momentum/Unleashed Serial profile:
- Service: `00008651-A123-4567-89AB-CDEF12345678` (placeholder — confirm against
  `bt_service` source in the firmware you ship; the official Serial profile
  defines RX/TX UUIDs that the FAP inherits when it calls
  `furi_hal_bt_change_app(BleProfileSerial, …)`).
- TX (Flipper → phone, notify): paired RX characteristic.
- RX (phone → Flipper, write-no-response): paired TX characteristic.

### 2.2 Manifest + Gradle
Add to `android/app/src/main/AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
                 android:usesPermissionFlags="neverForLocation"/>
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT"/>
<!-- Pre-API-31 only -->
<uses-permission android:name="android.permission.BLUETOOTH"
                 android:maxSdkVersion="30"/>
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN"
                 android:maxSdkVersion="30"/>
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"
                 android:maxSdkVersion="30"/>
<uses-feature android:name="android.hardware.bluetooth_le" android:required="false"/>
```

Add runtime-permission flow at first BLE scan (request BLUETOOTH_SCAN +
BLUETOOTH_CONNECT on Android 12+, FINE_LOCATION below).

Register the new plugin in `MainActivity.java` next to the existing one:
```java
registerPlugin(FlipperSerialPlugin.class);  // existing - USB
registerPlugin(FlipperBlePlugin.class);     // new - BLE
```

No new Gradle dependency required — pure `android.bluetooth.*`. The
`usb-serial-for-android` dep stays for USB.

---

## 3. Web UI — `android/www/app.js` + `android/www/index.html`

### 3.1 Transport bridge
Replace the single bridge constant at app.js:56:

```js
const UsbBridge = window.Capacitor?.Plugins?.FlipperSerial;
const BleBridge = window.Capacitor?.Plugins?.FlipperBle;
let Bridge = UsbBridge;  // default
```

Add `setTransport('usb'|'ble')` that:
- disconnects current bridge,
- swaps `Bridge`,
- re-binds `addListener('data', handleSerialLine)` and `addListener('status', …)`
  on the new bridge,
- persists choice via `localStorage.setItem('transport', …)` and reads it on boot.

The existing `handleSerialLine` (app.js:75) is transport-agnostic — no changes.

### 3.2 Drawer additions (`index.html`)
The drawer currently has `#connectBtn` / `#disconnectBtn` near
app.js:330-336. Add above them:

```html
<label>Transport
  <select id="transportSel">
    <option value="usb">USB</option>
    <option value="ble">Bluetooth</option>
  </select>
</label>
<button id="scanBtn" hidden>Scan BLE devices</button>
<select id="bleDeviceSel" hidden></select>
```

Wiring in `app.js`:
- `transportSel.onchange` calls `setTransport(value)` and toggles the BLE-only
  controls.
- For BLE: `scanBtn.onclick` → `BleBridge.scan({timeoutMs:5000})`, populate
  `#bleDeviceSel`. `connectBtn.onclick` passes `{address: bleDeviceSel.value}`
  to `BleBridge.connect`.
- For USB: existing flow (`Bridge.connect()` with no args) is preserved.

Status dot logic (app.js:59-71) stays unchanged — both bridges emit the same
`status` event shape.

### 3.3 Auto-reconnect
The USB plugin already auto-reconnects every 2s (README claim). Replicate on the
BLE side using GATT's `STATE_DISCONNECTED` → re-`connectGatt(...)` with the last
known address, backing off 2s/4s/8s. Cap at 5 retries before surfacing an error.

---

## 4. Docs

- `README.md` — update the "RF Architecture" diagram (lines 65-95) to show two
  arrows from Flipper to phone (USB-CDC and BLE), and add a "Transport" row to
  the v2 table at lines 42-50.
- `SETUP.md` — extend "USB Flipper IDs" section (line 78) with the BLE service
  UUID and a "Pairing the Flipper over BLE" subsection.
- `android/README.md` — add a `FlipperBlePlugin` row to the JS method table at
  line 56.

---

## 5. Order of work / commits

1. **Flipper transport abstraction (USB only)** — refactor `rf_logger.c` to use
   `TransportOps` with just the USB impl. Confirm parity with current build.
2. **Flipper BLE transport** — add `transport_ble_*`, FAM dep, UI selector.
3. **Android BLE plugin** — `FlipperBlePlugin.java` + manifest + permissions.
4. **Web UI transport picker + scan/connect flow**.
5. **Docs + screenshots**.

Each step is independently testable: step 1 ships a no-op refactor; step 2
lets you verify BLE with `nRF Connect`; step 3 lets you verify from the phone
before any UI changes; step 4 lights it up end-to-end.

---

## 6. Open questions / risks

- **BLE Serial profile API stability**: Momentum and Unleashed expose
  `furi_hal_bt_serial_*` but the symbol names drift between releases. Pin a
  firmware version in `SETUP.md` and gate the BLE code on a `#ifdef
  FW_HAS_BT_SERIAL` so the FAP still builds on stock.
- **Throughput**: 5 Hz × ~70 byte CSV lines = 350 B/s — well under BLE 4.2
  notification throughput (~5 KB/s). No concern. Keep an eye on connection
  interval; request 15 ms interval on connect for low latency.
- **Battery**: BLE advertising plus subghz RX will roughly double the FAP's
  current draw vs. USB-powered-and-charging. Mention in README "Use USB for long
  sessions, BLE for mobility".
- **Pairing UX**: Bonding requires a system pairing prompt the first time. The
  app can't suppress it. Document the one-time pair step.
