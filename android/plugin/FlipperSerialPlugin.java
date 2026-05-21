package com.mhzlocalise.rftriangulator;

/*
 * FlipperSerialPlugin
 * -------------------
 * Capacitor native plugin that bridges the WebView UI to a USB CDC-ACM
 * serial device (Flipper Zero). Uses the usb-serial-for-android library.
 *
 * JS API (exposed via Capacitor):
 *   FlipperSerial.listDevices()     -> { devices: [{vid, pid, productName, deviceId}] }
 *   FlipperSerial.connect()         -> { connected: true, deviceName }
 *   FlipperSerial.disconnect()      -> { connected: false }
 *   FlipperSerial.isConnected()     -> { connected: boolean }
 *   addListener('data', cb)         -> emits { line: "csv,row,..." } per newline-terminated line
 *   addListener('status', cb)       -> emits { state: "connected"|"disconnected"|"error", message? }
 *
 * The plugin parses the Flipper CSV stream (ts_ms,req_hz,act_hz,rssi_dbm,...)
 * and forwards each complete line to JS. It also requests USB permission
 * via the standard Android intent on first connect.
 */

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.util.Log;

import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import com.hoho.android.usbserial.driver.CdcAcmSerialDriver;
import com.hoho.android.usbserial.driver.ProbeTable;
import com.hoho.android.usbserial.driver.UsbSerialDriver;
import com.hoho.android.usbserial.driver.UsbSerialPort;
import com.hoho.android.usbserial.driver.UsbSerialProber;
import com.hoho.android.usbserial.util.SerialInputOutputManager;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;

@CapacitorPlugin(name = "FlipperSerial")
public class FlipperSerialPlugin extends Plugin implements SerialInputOutputManager.Listener {

    private static final String TAG = "FlipperSerial";
    private static final String ACTION_USB_PERMISSION = "com.mhzlocalise.USB_PERMISSION";
    private static final int BAUD = 115200;

    /* Flipper Zero USB IDs */
    private static final int VID_FLIPPER = 0x0483;
    private static final int PID_FLIPPER = 0x5740;

    /* Custom prober that recognises the Flipper Zero CDC interface even when
     * the device-class descriptor doesn't pin it as CDC (some Android versions
     * report the interface class instead of the device class, which makes
     * the default prober miss it). */
    private static UsbSerialProber buildProber() {
        ProbeTable table = UsbSerialProber.getDefaultProbeTable();
        table.addProduct(VID_FLIPPER, PID_FLIPPER, CdcAcmSerialDriver.class);
        return new UsbSerialProber(table);
    }

    private UsbSerialPort port;
    private SerialInputOutputManager ioManager;
    private final StringBuilder lineBuf = new StringBuilder(256);
    private PluginCall pendingConnect;

    private final BroadcastReceiver usbReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context ctx, Intent intent) {
            if (!ACTION_USB_PERMISSION.equals(intent.getAction())) return;
            UsbDevice dev = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
            if (granted && dev != null) {
                openDevice(dev);
            } else {
                emitStatus("error", "USB permission denied");
                if (pendingConnect != null) {
                    pendingConnect.reject("USB permission denied");
                    pendingConnect = null;
                }
            }
        }
    };

    @Override
    public void load() {
        IntentFilter f = new IntentFilter(ACTION_USB_PERMISSION);
        int flags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                ? Context.RECEIVER_NOT_EXPORTED : 0;
        getContext().registerReceiver(usbReceiver, f, flags);
    }

    @Override
    protected void handleOnDestroy() {
        try { getContext().unregisterReceiver(usbReceiver); } catch (Exception ignored) {}
        closePort();
    }

    /* ---------------- JS-exposed methods ---------------- */

    @PluginMethod
    public void listDevices(PluginCall call) {
        UsbManager mgr = (UsbManager) getContext().getSystemService(Context.USB_SERVICE);
        List<UsbSerialDriver> drivers = buildProber().findAllDrivers(mgr);
        JSONArray arr = new JSONArray();
        for (UsbSerialDriver d : drivers) {
            UsbDevice dev = d.getDevice();
            JSONObject o = new JSONObject();
            try {
                o.put("vid", dev.getVendorId());
                o.put("pid", dev.getProductId());
                o.put("productName", dev.getProductName());
                o.put("deviceId", dev.getDeviceId());
                arr.put(o);
            } catch (Exception e) { /* skip */ }
        }
        JSObject ret = new JSObject();
        ret.put("devices", arr);
        call.resolve(ret);
    }

    @PluginMethod
    public void connect(PluginCall call) {
        UsbManager mgr = (UsbManager) getContext().getSystemService(Context.USB_SERVICE);
        List<UsbSerialDriver> drivers = buildProber().findAllDrivers(mgr);

        UsbSerialDriver chosen = null;
        for (UsbSerialDriver d : drivers) {
            UsbDevice dev = d.getDevice();
            if (dev.getVendorId() == VID_FLIPPER && dev.getProductId() == PID_FLIPPER) {
                chosen = d;
                break;
            }
        }
        if (chosen == null && !drivers.isEmpty()) chosen = drivers.get(0);
        if (chosen == null) {
            /* No serial driver matched. Dump every USB device we can see so
             * the JS side can tell the user what to do. */
            HashMap<String, UsbDevice> all = mgr.getDeviceList();
            StringBuilder sb = new StringBuilder("No USB serial device found.");
            if (all.isEmpty()) {
                sb.append(" Phone reports no attached USB devices at all - check OTG/cable.");
            } else {
                sb.append(" Attached devices: ");
                for (UsbDevice d : all.values()) {
                    sb.append(String.format(" [vid=%04x pid=%04x %s]",
                            d.getVendorId(), d.getProductId(), d.getProductName()));
                }
            }
            call.reject(sb.toString());
            return;
        }

        UsbDevice dev = chosen.getDevice();
        if (!mgr.hasPermission(dev)) {
            pendingConnect = call;
            int flags = PendingIntent.FLAG_IMMUTABLE;
            PendingIntent pi = PendingIntent.getBroadcast(
                    getContext(), 0, new Intent(ACTION_USB_PERMISSION).setPackage(getContext().getPackageName()), flags);
            mgr.requestPermission(dev, pi);
            return;
        }
        openDevice(dev);
        JSObject ret = new JSObject();
        ret.put("connected", port != null);
        ret.put("deviceName", dev.getProductName());
        call.resolve(ret);
    }

    @PluginMethod
    public void disconnect(PluginCall call) {
        closePort();
        emitStatus("disconnected", null);
        JSObject ret = new JSObject();
        ret.put("connected", false);
        call.resolve(ret);
    }

    @PluginMethod
    public void isConnected(PluginCall call) {
        JSObject ret = new JSObject();
        ret.put("connected", port != null);
        call.resolve(ret);
    }

    /* ---------------- internals ---------------- */

    private void openDevice(UsbDevice dev) {
        UsbManager mgr = (UsbManager) getContext().getSystemService(Context.USB_SERVICE);
        List<UsbSerialDriver> drivers = buildProber().findAllDrivers(mgr);
        UsbSerialDriver driver = null;
        for (UsbSerialDriver d : drivers) {
            if (d.getDevice().getDeviceId() == dev.getDeviceId()) { driver = d; break; }
        }
        if (driver == null) { emitStatus("error", "Driver not found"); return; }

        UsbDeviceConnection conn = mgr.openDevice(dev);
        if (conn == null) { emitStatus("error", "openDevice failed"); return; }

        try {
            /* Flipper exposes dual CDC in this app: port 0 is the CLI, port 1
             * is the data stream the FAP writes to. Pick the highest-index
             * port available, fall back to 0 if only one exists. */
            java.util.List<UsbSerialPort> ports = driver.getPorts();
            int idx = ports.size() > 1 ? ports.size() - 1 : 0;
            port = ports.get(idx);
            port.open(conn);
            port.setParameters(BAUD, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE);
            port.setDTR(true);
            port.setRTS(true);
            ioManager = new SerialInputOutputManager(port, this);
            Executors.newSingleThreadExecutor().submit(ioManager);
            emitStatus("connected", dev.getProductName());
            if (pendingConnect != null) {
                JSObject ret = new JSObject();
                ret.put("connected", true);
                ret.put("deviceName", dev.getProductName());
                pendingConnect.resolve(ret);
                pendingConnect = null;
            }
        } catch (Exception e) {
            Log.e(TAG, "open failed", e);
            emitStatus("error", e.getMessage());
            closePort();
            if (pendingConnect != null) { pendingConnect.reject(e.getMessage()); pendingConnect = null; }
        }
    }

    private synchronized void closePort() {
        if (ioManager != null) { ioManager.stop(); ioManager = null; }
        if (port != null) {
            try { port.close(); } catch (Exception ignored) {}
            port = null;
        }
        lineBuf.setLength(0);
    }

    private void emitStatus(String state, String message) {
        JSObject o = new JSObject();
        o.put("state", state);
        if (message != null) o.put("message", message);
        notifyListeners("status", o);
    }

    /* ---- SerialInputOutputManager.Listener ---- */

    @Override
    public void onNewData(byte[] data) {
        for (byte b : data) {
            char c = (char) (b & 0xFF);
            if (c == '\n') {
                String line = lineBuf.toString().trim();
                lineBuf.setLength(0);
                if (!line.isEmpty()) {
                    JSObject o = new JSObject();
                    o.put("line", line);
                    notifyListeners("data", o);
                }
            } else if (c != '\r') {
                lineBuf.append(c);
                if (lineBuf.length() > 1024) lineBuf.setLength(0); /* runaway guard */
            }
        }
    }

    @Override
    public void onRunError(Exception e) {
        Log.w(TAG, "serial run error", e);
        emitStatus("error", e.getMessage());
        closePort();
    }
}
