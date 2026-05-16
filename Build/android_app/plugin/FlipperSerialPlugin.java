package io.scooby.rftriangulator.plugins;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.Build;

import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;

import com.hoho.android.usbserial.driver.CdcAcmSerialDriver;
import com.hoho.android.usbserial.driver.UsbSerialDriver;
import com.hoho.android.usbserial.driver.UsbSerialPort;
import com.hoho.android.usbserial.driver.UsbSerialProber;

import java.util.HashMap;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

@CapacitorPlugin(name = "FlipperSerial")
public class FlipperSerialPlugin extends Plugin {

    private static final int    FLIPPER_VID    = 0x0483;
    private static final int    FLIPPER_PID    = 0x5740;
    private static final String ACTION_USB_PERM = "io.scooby.rftriangulator.USB_PERMISSION";

    private UsbManager          usbManager;
    private UsbSerialPort       serialPort;
    private UsbDeviceConnection connection;
    private PluginCall          pendingConnectCall;

    private final AtomicBoolean streaming = new AtomicBoolean(false);
    private Thread              readerThread;

    private volatile float   latestRssi = -120f;
    private volatile int     latestLqi  = 0;
    private volatile long    latestTs   = 0;
    private volatile long    latestFreq = 433920000L;
    private volatile long    latestN    = 0;
    private volatile boolean connected  = false;

    /* ── USB permission broadcast receiver ───────────────────────── */
    private final BroadcastReceiver usbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!ACTION_USB_PERM.equals(intent.getAction())) return;
            UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            if (device == null || pendingConnectCall == null) return;

            boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
            PluginCall call = pendingConnectCall;
            pendingConnectCall = null;

            if (!granted) {
                call.resolve(err("USB permission denied by user"));
                return;
            }
            openPort(device, call);
        }
    };

    @Override
    public void load() {
        usbManager = (UsbManager) getContext().getSystemService(Context.USB_SERVICE);
        IntentFilter filter = new IntentFilter(ACTION_USB_PERM);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            getContext().registerReceiver(usbReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            getContext().registerReceiver(usbReceiver, filter);
        }
    }

    @Override
    protected void handleOnDestroy() {
        stopReaderThread();
        closePort();
        try { getContext().unregisterReceiver(usbReceiver); } catch (Exception ignored) {}
    }

    /* ── connect ─────────────────────────────────────────────────── */
    @PluginMethod
    public void connect(PluginCall call) {
        UsbDevice device = findFlipper();
        if (device == null) {
            call.resolve(err("Flipper Zero not found. Plug in via USB-C and ensure the RF Logger app is running."));
            return;
        }
        if (usbManager.hasPermission(device)) {
            openPort(device, call);
        } else {
            pendingConnectCall = call;
            PendingIntent pi = PendingIntent.getBroadcast(
                getContext(), 0,
                new Intent(ACTION_USB_PERM),
                PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
            usbManager.requestPermission(device, pi);
            /* result comes back via usbReceiver */
        }
    }

    private void openPort(UsbDevice device, PluginCall call) {
        try {
            UsbSerialDriver driver = getDriver(device);
            connection = usbManager.openDevice(device);
            if (connection == null) {
                call.resolve(err("Could not open USB device (try replug)"));
                return;
            }
            serialPort = driver.getPorts().get(0);
            serialPort.open(connection);
            serialPort.setParameters(115200,
                UsbSerialPort.DATABITS_8,
                UsbSerialPort.STOPBITS_1,
                UsbSerialPort.PARITY_NONE);
            connected = true;
            JSObject r = new JSObject();
            r.put("success", true);
            r.put("message", "Connected to Flipper Zero");
            call.resolve(r);
        } catch (Exception e) {
            call.resolve(err("Open error: " + e.getMessage()));
        }
    }

    /* ── disconnect ──────────────────────────────────────────────── */
    @PluginMethod
    public void disconnect(PluginCall call) {
        stopReaderThread();
        closePort();
        JSObject r = new JSObject();
        r.put("success", true);
        call.resolve(r);
    }

    /* ── getLatestRssi ───────────────────────────────────────────── */
    @PluginMethod
    public void getLatestRssi(PluginCall call) {
        JSObject r = new JSObject();
        r.put("rssi",      latestRssi);
        r.put("lqi",       latestLqi);
        r.put("ts",        latestTs);
        r.put("freq",      latestFreq);
        r.put("n",         latestN);
        r.put("connected", connected);
        call.resolve(r);
    }

    /* ── startStream ─────────────────────────────────────────────── */
    @PluginMethod
    public void startStream(PluginCall call) {
        if (!connected || serialPort == null) {
            call.resolve(err("Not connected"));
            return;
        }
        streaming.set(true);
        readerThread = new Thread(() -> {
            byte[] buf = new byte[256];
            StringBuilder sb = new StringBuilder();
            while (streaming.get()) {
                try {
                    int len = serialPort.read(buf, 300);
                    if (len > 0) {
                        sb.append(new String(buf, 0, len));
                        int nl;
                        while ((nl = sb.indexOf("\n")) >= 0) {
                            String line = sb.substring(0, nl).trim();
                            sb.delete(0, nl + 1);
                            JSObject ev = parseLine(line);
                            if (ev != null) notifyListeners("rssiData", ev);
                        }
                    }
                } catch (Exception e) {
                    if (streaming.get()) {
                        connected = false;
                        streaming.set(false);
                        JSObject ev = new JSObject();
                        ev.put("error", e.getMessage());
                        notifyListeners("rssiData", ev);
                    }
                }
            }
        });
        readerThread.setDaemon(true);
        readerThread.start();
        JSObject r = new JSObject();
        r.put("success", true);
        call.resolve(r);
    }

    /* ── stopStream ──────────────────────────────────────────────── */
    @PluginMethod
    public void stopStream(PluginCall call) {
        stopReaderThread();
        JSObject r = new JSObject();
        r.put("success", true);
        call.resolve(r);
    }

    /* ── private helpers ─────────────────────────────────────────── */
    // CSV format from Flipper: ts_ms, req_hz, act_hz, rssi_dbm, rssi_raw(0xHH), lqi, n
    private JSObject parseLine(String line) {
        if (line.startsWith("#") || line.startsWith("ts_ms") || line.isEmpty()) return null;
        String[] p = line.split(",");
        if (p.length < 7) return null;
        try {
            long  ts   = Long.parseLong(p[0].trim());
            long  freq = Long.parseLong(p[1].trim());   // req_hz
            // p[2] = act_hz (skip), p[3] = rssi_dbm, p[4] = rssi_raw (hex), p[5] = lqi, p[6] = n
            float rssi = Float.parseFloat(p[3].trim());
            int   lqi  = Integer.parseInt(p[5].trim());
            long  n    = Long.parseLong(p[6].trim());
            latestTs   = ts;
            latestFreq = freq;
            latestRssi = rssi;
            latestLqi  = lqi;
            latestN    = n;
            JSObject ev = new JSObject();
            ev.put("ts",   ts);
            ev.put("freq", freq);
            ev.put("rssi", rssi);
            ev.put("lqi",  lqi);
            ev.put("n",    n);
            return ev;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    private UsbDevice findFlipper() {
        HashMap<String, UsbDevice> devices = usbManager.getDeviceList();
        for (UsbDevice d : devices.values()) {
            if (d.getVendorId() == FLIPPER_VID && d.getProductId() == FLIPPER_PID) return d;
        }
        return null;
    }

    private UsbSerialDriver getDriver(UsbDevice device) {
        List<UsbSerialDriver> drivers =
            UsbSerialProber.getDefaultProber().findAllDrivers(usbManager);
        for (UsbSerialDriver d : drivers) {
            if (d.getDevice().getDeviceId() == device.getDeviceId()) return d;
        }
        return new CdcAcmSerialDriver(device);
    }

    private void stopReaderThread() {
        streaming.set(false);
        if (readerThread != null) {
            readerThread.interrupt();
            readerThread = null;
        }
    }

    private void closePort() {
        connected = false;
        if (serialPort != null) {
            try { serialPort.close(); } catch (Exception ignored) {}
            serialPort = null;
        }
        if (connection != null) {
            connection.close();
            connection = null;
        }
    }

    private JSObject err(String msg) {
        JSObject r = new JSObject();
        r.put("success", false);
        r.put("message", msg);
        return r;
    }
}
