/*
 * rf_logger.c — Sub-GHz RSSI logger for Flipper Zero
 *
 * CSV over USB CDC ch1 (ch0 stays as CLI). Takes over the USB stack with
 * dual-CDC when entering Running/Rolling state, restores on exit.
 *
 * The same CSV lines are also streamed over BLE: at launch the app swaps
 * the BLE stack to its own Serial profile instance (same GATT service the
 * stock CLI-over-BLE uses, so phone-side code is identical for both) and
 * transmits every line to the connected central. Default profile is
 * restored on exit. Requires Bluetooth ON in Flipper settings to advertise.
 *
 * Rolling scan (v3): two passes over the three CC1101 bands only.
 *   pass C (coarse): 650 kHz RX filter, 600 kHz steps, 100 ms peak-hold dwell,
 *                    no chip reset between steps (idle → set freq+cal → rx).
 *   pass F (fine)  : 58 kHz RX filter, 20 kHz steps, +/-300 kHz around each
 *                    coarse cluster → centre frequency + occupied width.
 * Detection threshold = max(-93 dBm, band noise floor (median) + 10 dB).
 * Pure scan logic lives in rf_scan.c (host unit-tested).
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_subghz.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_cdc.h>
#include <furi_hal_bt.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include <bt/bt_service/bt.h>
#include <profiles/serial_profile.h>
#include <lib/subghz/devices/cc1101_configs.h>

#include "rf_scan.h"

#define TAG               "rf_logger"
#define SAMPLE_PERIOD_MS  200u
#define VCP_DATA_CH       1u
#define LOG_DIR           EXT_PATH("apps_data/rf_logger")
#define CDC_TX_WAIT_MS    5u
#define ROLL_POS_EVERY    16u

typedef enum {
    StateManualEntry,
    StateDbList,
    StateRolling,
    StateRollResults,
    StateRunning,
} AppState;

/* ---- Frequency DB (embedded, Sub-GHz focus) ----
 * Only CC1101-tunable entries: 300-348, 387-464, 779-928 MHz. Entries in the
 * holes (169.40 Sigfox, 380.5 Polycom UL, 466.025 POCSAG) were removed: the
 * radio cannot tune them, so selecting them could never produce a reading. */
typedef struct {
    uint32_t    hz;
    const char* label;  // up to 21 chars visible on Flipper screen
    const char* region; // e.g. "EU/CH", "US", "ALL"
} FreqEntry;

/* hz=0 is the sentinel for the "Start Roll Scan" action */
static const FreqEntry FREQ_DB[] = {
    { 0u,          "\x10 START ROLL SCAN",  "2-pass"}, // \x10 = right-arrow glyph
    /* ISM / SRD — ALL regions */
    { 315000000u, "315 ISM (US/JP)",     "US"    },
    { 433920000u, "433.92 ISM/SRD",      "EU/CH" },
    { 434000000u, "434.00 SRD",          "EU"    },
    { 868000000u, "868.00 LoRa/SRD",     "EU/CH" },
    { 868300000u, "868.30 LoRa CH1",     "EU/CH" },
    { 868500000u, "868.50 LoRa CH2",     "EU/CH" },
    { 869525000u, "869.52 LoRa RX2",     "EU/CH" },
    { 915000000u, "915.00 LoRa (US)",    "US"    },
    { 916800000u, "916.80 LoRa (US)",    "US"    },
    { 917000000u, "917.00 SRD (US)",     "US"    },
    { 928000000u, "928.00 SRD (US)",     "US"    },
    /* TPMS / automotive RKE */
    { 433920000u, "433.92 TPMS/RKE",     "EU/CH" },
    { 315000000u, "315.00 TPMS/RKE",     "US/JP" },
    /* Alarms / security */
    { 433920000u, "433.92 Alarm",        "EU/CH" },
    { 868350000u, "868.35 Alarm",        "EU/CH" },
    { 869400000u, "869.40 Alarm HP",     "EU/CH" },
    /* Weather / radiosondes */
    { 403000000u, "403.00 Radiosonde",   "ALL"   },
    { 405000000u, "405.00 Radiosonde",   "ALL"   },
    /* PMR / walkie-talkie */
    { 446006250u, "446.006 PMR446 ch1",  "EU/CH" },
    { 446093750u, "446.093 PMR446 ch8",  "EU/CH" },
    /* OpenGarage / Metering */
    { 433920000u, "433.92 SmartMeter",   "EU/CH" },
    /* Aircraft / ADS-B adjacent */
    { 868000000u, "868.00 FLARM",        "EU/CH" },
    /* Polycom / TETRA (CH emergency) — UL 380.5 is in the CC1101 hole */
    { 390500000u, "390.5 Polycom DL",    "CH"    },
    /* GSM-R (SBB) */
    { 877500000u, "877.5 GSM-R UL SBB", "CH"    },
    { 922500000u, "922.5 GSM-R DL SBB", "CH"    },
    /* Swiss ISM / BAKOM */
    { 433050000u, "433.05 SRD low CH",   "CH"    },
    { 434790000u, "434.79 SRD high CH",  "CH"    },
    { 863000000u, "863.00 SRD low EU",   "EU/CH" },
    { 870000000u, "870.00 SRD high EU",  "EU/CH" },
    /* Sub-1GHz IoT */
    { 868800000u, "868.80 Sigfox EU",    "EU/CH" },
    { 300000000u, "300.00 ---- min ----","ALL"   },
};
#define FREQ_DB_LEN ((int)(sizeof(FREQ_DB) / sizeof(FREQ_DB[0])))

// Manual entry: edit a XXX.XX MHz value digit by digit.
// 5 editable positions: [0]=100s [1]=10s [2]=1s . [3]=0.1 [4]=0.01
#define MANUAL_DIGITS 5
static const uint32_t MANUAL_DIGIT_HZ[MANUAL_DIGITS] = {
    100000000u, // hundreds of MHz
     10000000u, // tens of MHz
      1000000u, // ones of MHz
       100000u, // 0.1 MHz
        10000u, // 0.01 MHz
};
#define MANUAL_MIN_HZ 300000000u
#define MANUAL_MAX_HZ 928000000u

/* ---- Rolling scan state ---- */
typedef enum { RollPhaseIdle, RollPhaseCoarse, RollPhaseFine } RollPhase;

typedef struct {
    RollPhase phase;
    uint32_t plan_len;
    uint32_t idx;                 // coarse plan index
    RfScanStat* stats;            // plan_len entries (heap, coarse pass only)
    int16_t* scratch;             // plan_len entries (heap, median)
    int16_t floor_dbm[RF_SCAN_BAND_COUNT];
    RfScanCluster clusters[RF_SCAN_MAX_CLUSTERS];
    uint8_t cluster_count;
    uint8_t cluster_idx;
    RfScanWindow win;
    uint32_t win_idx;
    int16_t fine_peak[RF_SCAN_FINE_MAX_STEPS];
    int16_t fine_mean[RF_SCAN_FINE_MAX_STEPS];
    uint32_t fine_samples;
    uint8_t fine_preset[RF_SCAN_PRESET_MAX];
    bool fine_preset_ok;
    uint32_t cur_hz;              // display: frequency being measured
    int16_t cur_peak_dbm;         // display: last peak
    uint32_t tune_errors;
    uint32_t seq;                 // ROLL line sequence (gap = lost line)
    uint32_t t_start;
    uint32_t t_coarse_ms;
    uint8_t result_cursor;
} RollScan;

typedef struct {
    AppState state;
    int      db_cursor;   // index in FREQ_DB[]

    RollScan roll;

    uint32_t freq_req_hz;
    uint32_t freq_act_hz;
    uint32_t manual_hz;      // current editable frequency in Hz
    uint8_t  manual_cursor;  // 0..MANUAL_DIGITS-1
    int rssi_dbm;
    uint8_t rssi_raw;
    uint8_t lqi;
    uint32_t n;
    bool sd_logging;
    File* log_file;
    FuriMutex* mutex;
    Gui* gui;
    ViewPort* viewport;
    FuriMessageQueue* input_queue;
    NotificationApp* notifications;
    Storage* storage;
    FuriHalUsbInterface* prev_usb;
    bool usb_taken;
    FuriSemaphore* cdc_tx_sem;
    uint32_t cdc_drops;
    Bt* bt;
    FuriHalBleProfileBase* ble_profile;
    volatile BtStatus bt_status;
    uint32_t next_adv_kick;
    uint32_t next_profile_retry;
} RfLoggerApp;

static const char RUN_CSV_HEADER[] = "ts_ms,req_hz,act_hz,rssi_dbm,rssi_raw,lqi,n\r\n";
static const char ROLL_CSV_HEADER[] =
    "ROLL,ts_ms,freq_hz,rssi_avg,rssi_peak,samples,flag,pass,floor_dbm,width_hz,cluster,seq\r\n";

/* Forward declarations (defined later in file) */
static void draw_signal_bar(Canvas* canvas, int x, int y, int w, int h, int rssi_dbm);
static void manual_clamp(RfLoggerApp* app);
static void start_running(RfLoggerApp* app, uint32_t hz);

/* ---- BLE serial streaming ---- */

static void bt_status_cb(BtStatus status, void* ctx) {
    RfLoggerApp* app = ctx;
    app->bt_status = status;
}

static void ble_start(RfLoggerApp* app) {
    app->bt = furi_record_open(RECORD_BT);
    bt_disconnect(app->bt);
    furi_delay_ms(200); // let the stack settle before the profile swap
    app->ble_profile = bt_profile_start(app->bt, ble_profile_serial, NULL);
    if(app->ble_profile) {
        bt_set_status_changed_callback(app->bt, bt_status_cb, app);
        furi_hal_bt_start_advertising();
    }
}

/* Advertising is not fire-and-forget: a settings toggle, key wipe
 * ("Forget All Paired Devices") or stack restart silently kills it.
 * Called from the main loop — re-kicks advertising while not connected,
 * and retries the profile swap if it failed at launch (e.g. BT was off). */
static void ble_keepalive(RfLoggerApp* app) {
    uint32_t now = furi_get_tick();
    if(!app->bt) return;
    if(!app->ble_profile) {
        if(now >= app->next_profile_retry) {
            app->next_profile_retry = now + 10000;
            app->ble_profile = bt_profile_start(app->bt, ble_profile_serial, NULL);
            if(app->ble_profile) {
                bt_set_status_changed_callback(app->bt, bt_status_cb, app);
                furi_hal_bt_start_advertising();
            }
        }
        return;
    }
    if(now >= app->next_adv_kick) {
        app->next_adv_kick = now + 2000;
        if(app->bt_status != BtStatusConnected) furi_hal_bt_start_advertising();
    }
}

static void ble_stop(RfLoggerApp* app) {
    if(!app->bt) return;
    bt_set_status_changed_callback(app->bt, NULL, NULL);
    bt_disconnect(app->bt);
    furi_delay_ms(200);
    bt_profile_restore_default(app->bt);
    furi_record_close(RECORD_BT);
    app->bt = NULL;
    app->ble_profile = NULL;
}

/* ---- USB CDC (interface 1) with flow control ----
 * The STM32 endpoint writer copies the whole buffer into a 64-byte packet
 * buffer without clamping and drops the write if the endpoint is still busy
 * (libusb_stm32 ep_write). So: chunk at CDC_DATA_SZ and only send while
 * holding the single TX token, which the TX-complete callback hands back. */

static void cdc_tx_done_cb(void* ctx) {
    RfLoggerApp* app = ctx;
    furi_semaphore_release(app->cdc_tx_sem);
}

static void cdc_state_cb(void* ctx, CdcState state) {
    RfLoggerApp* app = ctx;
    /* (re)enumeration: no packet in flight any more → token is free */
    if(state == CdcStateConnected) furi_semaphore_release(app->cdc_tx_sem);
}

static CdcCallbacks cdc_callbacks = {
    .tx_ep_callback = cdc_tx_done_cb,
    .rx_ep_callback = NULL,
    .state_callback = cdc_state_cb,
    .ctrl_line_callback = NULL,
    .config_callback = NULL,
};

static void usb_take(RfLoggerApp* app) {
    if(app->usb_taken) return;
    app->prev_usb = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    if(furi_hal_usb_set_config(&usb_cdc_dual, NULL)) {
        app->usb_taken = true;
        furi_hal_cdc_set_callbacks(VCP_DATA_CH, &cdc_callbacks, app);
    } else {
        FURI_LOG_E(TAG, "usb_cdc_dual config refused");
    }
}

static void usb_release(RfLoggerApp* app) {
    if(!app->usb_taken) return;
    furi_hal_cdc_set_callbacks(VCP_DATA_CH, NULL, NULL);
    furi_hal_usb_set_config(app->prev_usb, NULL);
    app->usb_taken = false;
}

static void cdc_send(RfLoggerApp* app, const char* s, size_t len) {
    if(!app->usb_taken || !s) return;
    size_t off = 0;
    while(off < len) {
        if(furi_semaphore_acquire(app->cdc_tx_sem, furi_ms_to_ticks(CDC_TX_WAIT_MS)) !=
           FuriStatusOk) {
            app->cdc_drops++; // host not draining: drop the rest of the line
            return;
        }
        size_t chunk = len - off;
        if(chunk > CDC_DATA_SZ) chunk = CDC_DATA_SZ;
        furi_hal_cdc_send(VCP_DATA_CH, (uint8_t*)(s + off), (uint16_t)chunk);
        off += chunk;
    }
}

/* ---- SD logging ---- */

static void log_open(RfLoggerApp* app, const char* header) {
    storage_simply_mkdir(app->storage, LOG_DIR);
    char path[96];
    snprintf(path, sizeof(path), LOG_DIR "/log_%lu.csv", (unsigned long)furi_get_tick());
    app->log_file = storage_file_alloc(app->storage);
    if(!storage_file_open(app->log_file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FURI_LOG_E(TAG, "cannot open %s", path);
        storage_file_free(app->log_file);
        app->log_file = NULL;
        app->sd_logging = false;
        notification_message(app->notifications, &sequence_error);
        return;
    }
    app->sd_logging = true;
    if(header) storage_file_write(app->log_file, header, strlen(header));
}

static void log_close(RfLoggerApp* app) {
    if(app->log_file) {
        storage_file_close(app->log_file);
        storage_file_free(app->log_file);
        app->log_file = NULL;
    }
    app->sd_logging = false;
}

static void log_toggle(RfLoggerApp* app, const char* header) {
    if(app->sd_logging) log_close(app);
    else log_open(app, header);
}

/* One line → USB CDC + BLE + SD (identical bytes on every transport). */
static void stream_line(RfLoggerApp* app, const char* line, size_t len) {
    if(!line || len == 0) return;
    cdc_send(app, line, len);
    /* Don't gate BLE TX on a cached status enum — that callback proved
     * unreliable for a custom profile, silently starving the phone.
     * The serial profile itself returns false when there's no subscriber,
     * so an unconditional call is safe and self-correcting. */
    if(app->ble_profile && len <= BLE_PROFILE_SERIAL_PACKET_SIZE_MAX)
        ble_profile_serial_tx(app->ble_profile, (uint8_t*)line, (uint16_t)len);
    if(app->sd_logging && app->log_file) storage_file_write(app->log_file, line, len);
}

static void stream_printf(RfLoggerApp* app, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if(n <= 0) return;
    if((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1; // truncated, still a line
    stream_line(app, buf, (size_t)n);
}

/* ---- Radio ---- */

static bool subghz_running = false;

/* Retune without resetting the chip: IDLE → FREQ + calibration (inside the
 * HAL) → RX. The HAL crashes on a frequency whose quantized value leaves the
 * path windows, so both gates are checked first. */
static bool radio_tune(uint32_t hz, uint32_t* act_hz) {
    if(!furi_hal_subghz_is_frequency_valid(hz) || !rf_scan_hz_path_safe(hz)) return false;
    furi_hal_subghz_idle();
    uint32_t act = furi_hal_subghz_set_frequency_and_path(hz);
    furi_hal_subghz_rx();
    if(act_hz) *act_hz = act;
    return true;
}

/* Peak-hold + mean over dwell_ms, ~1 sample/ms after the RSSI settles. */
static void radio_measure(uint32_t dwell_ms, RfScanStat* out) {
    furi_delay_ms(RF_SCAN_SETTLE_MS);
    uint32_t t0 = furi_get_tick();
    uint32_t span = furi_ms_to_ticks(dwell_ms);
    int32_t sum = 0;
    int16_t peak = RF_SCAN_NO_SIGNAL_DBM;
    uint16_t n = 0;
    do {
        int16_t r = rf_scan_dbm_round(furi_hal_subghz_get_rssi());
        sum += r;
        if(r > peak) peak = r;
        n++;
        furi_delay_ms(1);
    } while((furi_get_tick() - t0) < span && n < 0xFFFFu);
    out->mean_dbm = (int16_t)(sum / (int32_t)n);
    out->peak_dbm = peak;
    out->samples = n;
}

static void radio_power_on(void) {
    if(!subghz_running) {
        furi_hal_power_suppress_charge_enter();
        subghz_running = true;
    }
}

static void subghz_stop(void) {
    if(!subghz_running) return;
    furi_hal_subghz_idle();
    furi_hal_subghz_set_path(FuriHalSubGhzPathIsolate);
    furi_hal_subghz_sleep();
    furi_hal_power_suppress_charge_exit();
    subghz_running = false;
}

/* Single-frequency mode: one preset load (it already resets the chip). */
static bool subghz_start(RfLoggerApp* app, uint32_t hz) {
    if(!furi_hal_subghz_is_frequency_valid(hz) || !rf_scan_hz_path_safe(hz)) return false;
    radio_power_on();
    furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_650khz_async_regs);
    app->freq_req_hz = hz;
    if(!radio_tune(hz, &app->freq_act_hz)) return false;
    app->n = 0;
    stream_printf(app, "# RF_LOGGER_DBG req=%lu act=%lu\r\n",
                  (unsigned long)hz, (unsigned long)app->freq_act_hz);
    stream_line(app, RUN_CSV_HEADER, strlen(RUN_CSV_HEADER));
    return true;
}

static void sample_once(RfLoggerApp* app) {
    float rssi_f = furi_hal_subghz_get_rssi();
    app->rssi_dbm = (int)rssi_f;
    int raw = (int)(rssi_f + 128.0f);
    if(raw < 0) raw = 0; else if(raw > 255) raw = 255;
    app->rssi_raw = (uint8_t)raw;
    app->lqi = furi_hal_subghz_get_lqi();
    app->n++;
    char line[128];
    int len = snprintf(line, sizeof(line),
                       "%lu,%lu,%lu,%d,0x%02X,%u,%lu\r\n",
                       (unsigned long)furi_get_tick(),
                       (unsigned long)app->freq_req_hz,
                       (unsigned long)app->freq_act_hz,
                       app->rssi_dbm, app->rssi_raw,
                       (unsigned)app->lqi, (unsigned long)app->n);
    if(len > 0) stream_line(app, line, (size_t)len);
}

/* ---- Rolling scan engine ---- */

static void roll_free(RollScan* r) {
    if(r->stats) { free(r->stats); r->stats = NULL; }
    if(r->scratch) { free(r->scratch); r->scratch = NULL; }
}

static void roll_emit(RfLoggerApp* app, const RfScanCluster* c, uint8_t id, bool fine) {
    RollScan* r = &app->roll;
    uint32_t hz = fine ? c->center_hz : c->peak_hz;
    int16_t peak = fine ? c->fine_peak_dbm : c->peak_dbm;
    int16_t mean = fine ? c->fine_mean_dbm : c->mean_dbm;
    uint32_t width = fine ? c->width_hz : (c->hi_hz - c->lo_hz) + RF_SCAN_COARSE_STEP_HZ;
    uint32_t samples = fine ? c->fine_samples : c->samples;
    stream_printf(app, "ROLL,%lu,%lu,%d,%d,%lu,%s,%c,%d,%lu,%u,%lu\r\n",
                  (unsigned long)furi_get_tick(), (unsigned long)hz,
                  mean, peak, (unsigned long)samples, rf_scan_flag(peak),
                  fine ? 'F' : 'C', c->floor_dbm, (unsigned long)width,
                  (unsigned)id, (unsigned long)r->seq++);
}

static void roll_finish(RfLoggerApp* app, bool aborted) {
    RollScan* r = &app->roll;
    uint32_t total_ms = furi_get_tick() - r->t_start;
    stream_printf(app, "# RF_LOGGER_RSCAN end status=%s clusters=%u coarse_ms=%lu total_ms=%lu "
                       "tune_err=%lu usb_drops=%lu\r\n",
                  aborted ? "aborted" : "ok", (unsigned)r->cluster_count,
                  (unsigned long)r->t_coarse_ms, (unsigned long)total_ms,
                  (unsigned long)r->tune_errors, (unsigned long)app->cdc_drops);
    subghz_stop();
    log_close(app);
    roll_free(r);
    r->phase = RollPhaseIdle;
    notification_message(app->notifications, &sequence_blink_stop);
    if(!aborted && r->cluster_count > 0) {
        r->result_cursor = 0;
        app->state = StateRollResults;
        notification_message(app->notifications, &sequence_success);
    } else {
        app->state = StateManualEntry;
    }
}

static bool roll_begin(RfLoggerApp* app) {
    RollScan* r = &app->roll;
    roll_free(r);
    memset(r, 0, sizeof(*r));
    r->plan_len = rf_scan_plan_len(RF_SCAN_COARSE_STEP_HZ);
    r->stats = malloc(sizeof(RfScanStat) * r->plan_len);
    r->scratch = malloc(sizeof(int16_t) * r->plan_len);
    memset(r->stats, 0, sizeof(RfScanStat) * r->plan_len);
    r->fine_preset_ok = rf_scan_clone_preset(
                            subghz_device_cc1101_preset_ook_650khz_async_regs,
                            r->fine_preset, sizeof(r->fine_preset),
                            RF_SCAN_REG_MDMCFG4, RF_SCAN_MDMCFG4_58KHZ) > 0;

    usb_take(app);
    radio_power_on();
    /* Single chip reset + preset load for the whole coarse pass. */
    furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_650khz_async_regs);

    stream_printf(app,
                  "# RF_LOGGER_ROLL v=3 bands=%lu-%lu,%lu-%lu,%lu-%lu step=%lu bw=%lu dwell_ms=%lu "
                  "fine_step=%lu fine_bw=%lu fine_span=%lu fine_dwell_ms=%lu points=%lu\r\n",
                  (unsigned long)rf_scan_bands[0].lo_hz, (unsigned long)rf_scan_bands[0].hi_hz,
                  (unsigned long)rf_scan_bands[1].lo_hz, (unsigned long)rf_scan_bands[1].hi_hz,
                  (unsigned long)rf_scan_bands[2].lo_hz, (unsigned long)rf_scan_bands[2].hi_hz,
                  (unsigned long)RF_SCAN_COARSE_STEP_HZ, (unsigned long)RF_SCAN_COARSE_BW_HZ,
                  (unsigned long)RF_SCAN_COARSE_DWELL_MS, (unsigned long)RF_SCAN_FINE_STEP_HZ,
                  (unsigned long)RF_SCAN_FINE_BW_HZ, (unsigned long)RF_SCAN_FINE_SPAN_HZ,
                  (unsigned long)RF_SCAN_FINE_DWELL_MS, (unsigned long)r->plan_len);
    stream_line(app, ROLL_CSV_HEADER, strlen(ROLL_CSV_HEADER));
    if(!r->fine_preset_ok)
        stream_printf(app, "# RF_LOGGER_RSCAN warn fine_preset=unavailable (coarse only)\r\n");

    r->t_start = furi_get_tick();
    r->cur_peak_dbm = RF_SCAN_NO_SIGNAL_DBM;
    r->phase = RollPhaseCoarse;
    app->state = StateRolling;
    notification_message(app->notifications, &sequence_blink_start_cyan);
    return true;
}

static void roll_start_fine_window(RfLoggerApp* app) {
    RollScan* r = &app->roll;
    while(r->cluster_idx < r->cluster_count) {
        if(rf_scan_fine_window(&r->clusters[r->cluster_idx], &r->win)) {
            r->win_idx = 0;
            r->fine_samples = 0;
            stream_printf(app, "# RF_LOGGER_RSCAN pos pass=F idx=%u total=%u hz=%lu\r\n",
                          (unsigned)r->cluster_idx, (unsigned)r->cluster_count,
                          (unsigned long)r->clusters[r->cluster_idx].peak_hz);
            return;
        }
        r->cluster_idx++; // no valid window: keep the coarse estimate
    }
}

/* Coarse pass done: floors → clusters → C lines → fine pass or finish. */
static void roll_coarse_done(RfLoggerApp* app) {
    RollScan* r = &app->roll;
    r->t_coarse_ms = furi_get_tick() - r->t_start;
    for(uint8_t b = 0; b < RF_SCAN_BAND_COUNT; b++) {
        r->floor_dbm[b] = rf_scan_band_floor(r->stats, r->plan_len, RF_SCAN_COARSE_STEP_HZ, b,
                                             r->scratch);
    }
    stream_printf(app, "# RF_LOGGER_RSCAN floor b0=%d b1=%d b2=%d thr0=%d thr1=%d thr2=%d\r\n",
                  r->floor_dbm[0], r->floor_dbm[1], r->floor_dbm[2],
                  rf_scan_threshold(r->floor_dbm[0]), rf_scan_threshold(r->floor_dbm[1]),
                  rf_scan_threshold(r->floor_dbm[2]));
    r->cluster_count = (uint8_t)rf_scan_find_clusters(r->stats, r->plan_len,
                                                      RF_SCAN_COARSE_STEP_HZ, r->floor_dbm,
                                                      r->clusters, RF_SCAN_MAX_CLUSTERS);
    for(uint8_t i = 0; i < r->cluster_count; i++) roll_emit(app, &r->clusters[i], i, false);
    /* coarse buffers no longer needed */
    roll_free(r);

    if(r->cluster_count == 0 || !r->fine_preset_ok) {
        roll_finish(app, false);
        return;
    }
    furi_hal_subghz_idle();
    furi_hal_subghz_load_custom_preset(r->fine_preset); // one reset for the fine pass
    r->cluster_idx = 0;
    r->phase = RollPhaseFine;
    roll_start_fine_window(app);
    if(r->cluster_idx >= r->cluster_count) roll_finish(app, false);
}

/* One measurement step. Radio work runs WITHOUT the GUI mutex (only this
 * thread touches the radio); shared fields are committed under it. */
static void roll_step(RfLoggerApp* app) {
    RollScan* r = &app->roll;
    RfScanStat st = {RF_SCAN_NO_SIGNAL_DBM, RF_SCAN_NO_SIGNAL_DBM, 0};
    uint32_t hz = 0;

    if(r->phase == RollPhaseCoarse) {
        uint8_t band = 0;
        if(!rf_scan_plan_at(RF_SCAN_COARSE_STEP_HZ, r->idx, &hz, &band)) return;
        bool ok = radio_tune(hz, NULL);
        if(ok) radio_measure(RF_SCAN_COARSE_DWELL_MS, &st);

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(!ok) {
            r->tune_errors++;
            stream_printf(app, "# RF_LOGGER_RSCAN err tune hz=%lu\r\n", (unsigned long)hz);
        }
        r->stats[r->idx] = st; // samples==0 marks a failed step (excluded)
        r->cur_hz = hz;
        r->cur_peak_dbm = st.peak_dbm;
        r->idx++;
        if((r->idx % ROLL_POS_EVERY) == 0 || r->idx == r->plan_len)
            stream_printf(app, "# RF_LOGGER_RSCAN pos pass=C idx=%lu total=%lu hz=%lu\r\n",
                          (unsigned long)r->idx, (unsigned long)r->plan_len, (unsigned long)hz);
        if(r->idx >= r->plan_len) roll_coarse_done(app);
        furi_mutex_release(app->mutex);
        return;
    }

    if(r->phase == RollPhaseFine) {
        hz = r->win.start_hz + r->win_idx * RF_SCAN_FINE_STEP_HZ;
        bool ok = radio_tune(hz, NULL);
        if(ok) radio_measure(RF_SCAN_FINE_DWELL_MS, &st);

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(!ok) r->tune_errors++;
        r->fine_peak[r->win_idx] = st.peak_dbm;
        r->fine_mean[r->win_idx] = st.mean_dbm;
        r->fine_samples += st.samples;
        r->cur_hz = hz;
        r->cur_peak_dbm = st.peak_dbm;
        r->win_idx++;
        if(r->win_idx >= r->win.count) {
            RfScanCluster* c = &r->clusters[r->cluster_idx];
            RfScanFine f;
            if(rf_scan_fine_analyze(r->fine_peak, r->win.count, r->win.start_hz,
                                    RF_SCAN_FINE_STEP_HZ, &f)) {
                c->fine_done = true;
                c->center_hz = f.center_hz;
                c->width_hz = f.width_hz;
                c->fine_peak_dbm = f.peak_dbm;
                c->fine_mean_dbm = r->fine_mean[f.peak_index];
                c->fine_samples = r->fine_samples;
                roll_emit(app, c, r->cluster_idx, true);
            }
            r->cluster_idx++;
            roll_start_fine_window(app);
            if(r->cluster_idx >= r->cluster_count) roll_finish(app, false);
        }
        furi_mutex_release(app->mutex);
    }
}

/* Frequency to hand to single-frequency mode from a scan result. */
static uint32_t roll_result_hz(const RfScanCluster* c) {
    uint32_t hz = c->fine_done ? c->center_hz : c->peak_hz;
    if(!rf_scan_hz_path_safe(hz)) hz = c->peak_hz;
    return hz;
}

/* ---- Drawing ---- */

static void draw_rolling_scan(Canvas* canvas, RfLoggerApp* app) {
    RollScan* r = &app->roll;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, r->phase == RollPhaseFine ? "Roll 2/2 fine" : "Roll 1/2 coarse");
    canvas_set_font(canvas, FontSecondary);
    char hits[16];
    snprintf(hits, sizeof(hits), "hits:%u", (unsigned)r->cluster_count);
    if(r->phase == RollPhaseFine) canvas_draw_str(canvas, 92, 10, hits);

    canvas_set_font(canvas, FontBigNumbers);
    char freq[24];
    snprintf(freq, sizeof(freq), "%lu.%02lu",
             (unsigned long)(r->cur_hz / 1000000u),
             (unsigned long)((r->cur_hz / 10000u) % 100u));
    canvas_draw_str(canvas, 2, 32, freq);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 82, 32, "MHz");

    if(r->cur_peak_dbm > RF_SCAN_NO_SIGNAL_DBM) {
        char rssi[24];
        snprintf(rssi, sizeof(rssi), "pk %d dBm", r->cur_peak_dbm);
        canvas_draw_str(canvas, 2, 43, rssi);
        draw_signal_bar(canvas, 62, 37, 64, 6, r->cur_peak_dbm);
    }

    uint32_t done = 0, total = 1;
    if(r->phase == RollPhaseCoarse) {
        done = r->idx;
        total = r->plan_len ? r->plan_len : 1u;
    } else if(r->phase == RollPhaseFine) {
        done = r->cluster_idx;
        total = r->cluster_count ? r->cluster_count : 1u;
    }
    int prog = (int)(done * 120u / total);
    canvas_draw_frame(canvas, 2, 47, 120, 5);
    if(prog > 0) canvas_draw_box(canvas, 2, 47, prog, 5);

    canvas_draw_str(canvas, 2, 62, app->sd_logging ? "Back=stop OK=SD:on" : "Back=stop OK=SD:off");
}

#define RESULTS_VISIBLE 4

static void draw_roll_results(Canvas* canvas, RfLoggerApp* app) {
    RollScan* r = &app->roll;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Scan hits");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 58, 10, "OK=tune Bk=exit");

    int top = (int)r->result_cursor - RESULTS_VISIBLE / 2;
    if(top > (int)r->cluster_count - RESULTS_VISIBLE) top = (int)r->cluster_count - RESULTS_VISIBLE;
    if(top < 0) top = 0;
    for(int i = 0; i < RESULTS_VISIBLE && (top + i) < (int)r->cluster_count; i++) {
        int idx = top + i;
        const RfScanCluster* c = &r->clusters[idx];
        uint32_t hz = roll_result_hz(c);
        int16_t pk = c->fine_done ? c->fine_peak_dbm : c->peak_dbm;
        char buf[32];
        snprintf(buf, sizeof(buf), "%3lu.%03lu %4d %c %s",
                 (unsigned long)(hz / 1000000u), (unsigned long)((hz / 1000u) % 1000u),
                 pk, c->fine_done ? 'F' : 'C', pk >= RF_SCAN_STRONG_DBM ? "S" : "M");
        int y = 22 + i * 11;
        bool sel = (idx == r->result_cursor);
        if(sel) {
            canvas_draw_box(canvas, 0, y - 9, 124, 11);
            canvas_invert_color(canvas);
        }
        canvas_draw_str(canvas, 2, y, buf);
        if(sel) canvas_invert_color(canvas);
    }
    if(r->cluster_count > RESULTS_VISIBLE)
        elements_scrollbar(canvas, r->result_cursor, r->cluster_count);
}

/* ---- DB list screen ---- */
#define DB_VISIBLE 4  // rows visible at once on the 64px screen

static void draw_db(Canvas* canvas, RfLoggerApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Freq DB");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 60, 10, "OK=select Bk=exit");

    // Scrolling window: keep cursor in the middle
    int top = app->db_cursor - DB_VISIBLE / 2;
    if(top < 0) top = 0;
    if(top > FREQ_DB_LEN - DB_VISIBLE) top = FREQ_DB_LEN - DB_VISIBLE;
    if(top < 0) top = 0;

    for(int i = 0; i < DB_VISIBLE && (top + i) < FREQ_DB_LEN; i++) {
        int idx = top + i;
        int y   = 22 + i * 11;
        bool sel = (idx == app->db_cursor);

        if(sel) {
            canvas_draw_box(canvas, 0, y - 9, 128, 11);
            canvas_invert_color(canvas);
        }

        char buf[28];
        snprintf(buf, sizeof(buf), "%-22s %s",
                 FREQ_DB[idx].label, FREQ_DB[idx].region);
        canvas_draw_str(canvas, 2, y, buf);

        if(sel) canvas_invert_color(canvas);
    }

    // Scrollbar
    if(FREQ_DB_LEN > DB_VISIBLE) {
        int bar_h = 44 * DB_VISIBLE / FREQ_DB_LEN;
        if(bar_h < 4) bar_h = 4;
        int bar_y = 13 + (44 - bar_h) * top / (FREQ_DB_LEN - DB_VISIBLE);
        canvas_draw_line(canvas, 127, 13, 127, 57);
        canvas_draw_box(canvas, 126, bar_y, 2, bar_h);
    }
}

static void handle_db_input(RfLoggerApp* app, InputEvent* ev) {
    bool is_short  = (ev->type == InputTypeShort);
    bool is_repeat = (ev->type == InputTypeRepeat);
    if(!is_short && !is_repeat) return;
    switch(ev->key) {
    case InputKeyUp:
        if(app->db_cursor > 0) app->db_cursor--;
        break;
    case InputKeyDown:
        if(app->db_cursor < FREQ_DB_LEN - 1) app->db_cursor++;
        break;
    case InputKeyOk:
        if(is_short) {
            uint32_t hz = FREQ_DB[app->db_cursor].hz;
            if(hz == 0u) {
                roll_begin(app);
            } else if(furi_hal_subghz_is_frequency_valid(hz)) {
                app->manual_hz = hz;
                manual_clamp(app);
                app->state = StateManualEntry;
            } else {
                notification_message(app->notifications, &sequence_error);
            }
        }
        break;
    case InputKeyBack:
        if(is_short) app->state = StateManualEntry;
        break;
    default: break;
    }
}

static void handle_results_input(RfLoggerApp* app, InputEvent* ev) {
    RollScan* r = &app->roll;
    bool is_short  = (ev->type == InputTypeShort);
    bool is_repeat = (ev->type == InputTypeRepeat);
    if(!is_short && !is_repeat) return;
    switch(ev->key) {
    case InputKeyUp:
        if(r->result_cursor > 0) r->result_cursor--;
        break;
    case InputKeyDown:
        if(r->result_cursor + 1u < r->cluster_count) r->result_cursor++;
        break;
    case InputKeyOk:
        if(is_short && r->cluster_count > 0) {
            uint32_t hz = roll_result_hz(&r->clusters[r->result_cursor]);
            app->manual_hz = hz; // Back from Running lands on this frequency
            start_running(app, hz);
        }
        break;
    case InputKeyBack:
        if(is_short) app->state = StateManualEntry;
        break;
    default: break;
    }
}

static void draw_manual(Canvas* canvas, RfLoggerApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 12, "Manual Frequency");

    // Build the 5 digits string of XXX.XX (no dot, dot rendered separately)
    char digits[6];
    uint32_t hz = app->manual_hz;
    digits[0] = '0' + (char)((hz / 100000000u) % 10u);
    digits[1] = '0' + (char)((hz /  10000000u) % 10u);
    digits[2] = '0' + (char)((hz /   1000000u) % 10u);
    digits[3] = '0' + (char)((hz /    100000u) % 10u);
    digits[4] = '0' + (char)((hz /     10000u) % 10u);
    digits[5] = '\0';

    canvas_set_font(canvas, FontBigNumbers);
    const int x0 = 16;
    const int y_text = 38;
    const int digit_w = 10;
    int x_positions[MANUAL_DIGITS];
    int x = x0;
    for(uint8_t i = 0; i < MANUAL_DIGITS; i++) {
        if(i == 3) x += 6; // gap for the decimal dot
        x_positions[i] = x;
        char one[2] = { digits[i], '\0' };
        canvas_draw_str(canvas, x, y_text, one);
        x += digit_w;
    }
    // Decimal point between [2] and [3]
    canvas_draw_str(canvas, x_positions[2] + digit_w, y_text, ".");
    // "MHz" suffix
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 2, y_text, "MHz");

    // Underline the active digit
    int ux = x_positions[app->manual_cursor];
    canvas_draw_line(canvas, ux, y_text + 2, ux + digit_w - 2, y_text + 2);

    // Hint footer — warn when the value sits in a CC1101 hole
    if(!furi_hal_subghz_is_frequency_valid(app->manual_hz)) {
        canvas_draw_str(canvas, 4, 56, "Hors bande CC1101!");
    } else {
        canvas_draw_str(canvas, 4, 56, "U/D chg  L/R mov  R>DB");
    }
    canvas_draw_str(canvas, 4, 64, "OK start   Back cancel");
}

static void manual_clamp(RfLoggerApp* app) {
    if(app->manual_hz < MANUAL_MIN_HZ) app->manual_hz = MANUAL_MIN_HZ;
    if(app->manual_hz > MANUAL_MAX_HZ) app->manual_hz = MANUAL_MAX_HZ;
}

static void manual_adjust(RfLoggerApp* app, int sign) {
    // Roll the single digit at the cursor 0..9 without overflowing neighbours.
    uint32_t step = MANUAL_DIGIT_HZ[app->manual_cursor];
    uint32_t cur = (app->manual_hz / step) % 10u;
    uint32_t next = (sign > 0) ? ((cur + 1u) % 10u) : ((cur + 9u) % 10u);
    app->manual_hz = app->manual_hz - cur * step + next * step;
    manual_clamp(app);
}

static void draw_signal_bar(Canvas* canvas, int x, int y, int w, int h, int rssi_dbm) {
    int span = 90;
    int v = rssi_dbm + 120;
    if(v < 0) v = 0;
    if(v > span) v = span;
    int fill = (v * w) / span;
    canvas_draw_frame(canvas, x, y, w, h);
    if(fill > 2) canvas_draw_box(canvas, x + 1, y + 1, fill - 2, h - 2);
}

static void draw_running(Canvas* canvas, RfLoggerApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    char freq[24];
    snprintf(freq, sizeof(freq), "%lu.%02lu MHz",
             (unsigned long)(app->freq_act_hz / 1000000u),
             (unsigned long)((app->freq_act_hz / 10000u) % 100u));
    canvas_draw_str(canvas, 4, 12, freq);

    canvas_set_font(canvas, FontBigNumbers);
    char rssi[12];
    snprintf(rssi, sizeof(rssi), "%d", app->rssi_dbm);
    canvas_draw_str(canvas, 4, 38, rssi);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 60, 38, "dBm");

    draw_signal_bar(canvas, 4, 44, 120, 8, app->rssi_dbm);

    const char* bt_s = app->bt_status == BtStatusConnected    ? "BT:ok" :
                       app->bt_status == BtStatusAdvertising ? "BT:adv" :
                                                               "BT:off";
    char info[48];
    snprintf(info, sizeof(info), "n=%lu lqi=%u %s %s",
             (unsigned long)app->n, (unsigned)app->lqi,
             app->sd_logging ? "SD:on" : "SD:off", bt_s);
    canvas_draw_str(canvas, 4, 62, info);
}

static void render_cb(Canvas* canvas, void* ctx) {
    RfLoggerApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    switch(app->state) {
    case StateManualEntry: draw_manual(canvas, app); break;
    case StateDbList:      draw_db(canvas, app); break;
    case StateRolling:     draw_rolling_scan(canvas, app); break;
    case StateRollResults: draw_roll_results(canvas, app); break;
    default:               draw_running(canvas, app); break;
    }
    furi_mutex_release(app->mutex);
}

static void input_cb(InputEvent* ev, void* ctx) {
    RfLoggerApp* app = ctx;
    furi_message_queue_put(app->input_queue, ev, 0);
}

static void start_running(RfLoggerApp* app, uint32_t hz) {
    app->freq_req_hz = hz;
    app->n = 0;
    usb_take(app);
    if(subghz_start(app, hz)) {
        app->state = StateRunning;
        notification_message(app->notifications, &sequence_blink_start_green);
    } else {
        subghz_stop();
        notification_message(app->notifications, &sequence_error);
    }
}

static void handle_manual_input(RfLoggerApp* app, InputEvent* ev) {
    bool is_short  = (ev->type == InputTypeShort);
    bool is_repeat = (ev->type == InputTypeRepeat);
    if(!is_short && !is_repeat) return;
    switch(ev->key) {
    case InputKeyUp:    manual_adjust(app, +1); break;
    case InputKeyDown:  manual_adjust(app, -1); break;
    case InputKeyLeft:
        if(is_short && app->manual_cursor > 0) app->manual_cursor--;
        break;
    case InputKeyRight:
        if(is_short) {
            if(app->manual_cursor + 1 < MANUAL_DIGITS) {
                app->manual_cursor++;
            } else {
                // at the last digit: open DB list
                app->state = StateDbList;
            }
        }
        break;
    case InputKeyOk:
        if(is_short) {
            manual_clamp(app);
            start_running(app, app->manual_hz); // errors blink red
        }
        break;
    default: break;
    }
}

static void handle_running_input(RfLoggerApp* app, InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    switch(ev->key) {
    case InputKeyOk:
        log_toggle(app, RUN_CSV_HEADER);
        break;
    case InputKeyBack:
        subghz_stop();
        log_close(app);
        notification_message(app->notifications, &sequence_blink_stop);
        app->state = StateManualEntry;
        break;
    default: break;
    }
}

static void handle_rolling_input(RfLoggerApp* app, InputEvent* ev) {
    if(ev->type != InputTypeShort) return;
    if(ev->key == InputKeyBack) {
        roll_finish(app, true);
    } else if(ev->key == InputKeyOk) {
        /* self-describing file: scan settings + columns up front */
        char hdr[256];
        snprintf(hdr, sizeof(hdr),
                 "# RF_LOGGER_ROLL v=3 step=%lu dwell_ms=%lu fine_step=%lu fine_dwell_ms=%lu\r\n%s",
                 (unsigned long)RF_SCAN_COARSE_STEP_HZ, (unsigned long)RF_SCAN_COARSE_DWELL_MS,
                 (unsigned long)RF_SCAN_FINE_STEP_HZ, (unsigned long)RF_SCAN_FINE_DWELL_MS,
                 ROLL_CSV_HEADER);
        log_toggle(app, hdr);
    }
}

int32_t rf_logger_app(void* p) {
    UNUSED(p);
    RfLoggerApp* app = malloc(sizeof(RfLoggerApp));
    memset(app, 0, sizeof(*app));
    app->state = StateManualEntry;
    app->manual_hz = 433920000u; // sensible default inside the valid range
    app->manual_cursor = 2; // start on the ones-of-MHz digit
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->cdc_tx_sem = furi_semaphore_alloc(1, 1);
    app->viewport = view_port_alloc();
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    view_port_draw_callback_set(app->viewport, render_cb, app);
    view_port_input_callback_set(app->viewport, input_cb, app);
    gui_add_view_port(app->gui, app->viewport, GuiLayerFullscreen);

    ble_start(app);

    bool exit = false;
    uint32_t next_sample = 0;
    while(!exit) {
        InputEvent ev;
        /* Rolling: each step blocks for its own dwell, so don't wait here. */
        uint32_t wait = (app->state == StateRolling) ? 0u : 50u;
        FuriStatus s = furi_message_queue_get(app->input_queue, &ev, wait);
        if(s == FuriStatusOk) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            switch(app->state) {
            case StateManualEntry:
                if(ev.key == InputKeyBack && ev.type == InputTypeShort) exit = true;
                else handle_manual_input(app, &ev);
                break;
            case StateDbList:      handle_db_input(app, &ev); break;
            case StateRolling:     handle_rolling_input(app, &ev); break;
            case StateRollResults: handle_results_input(app, &ev); break;
            default:               handle_running_input(app, &ev); break;
            }
            furi_mutex_release(app->mutex);
        }
        if(app->state == StateRunning) {
            uint32_t now = furi_get_tick();
            if(now >= next_sample) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                sample_once(app);
                furi_mutex_release(app->mutex);
                next_sample = now + SAMPLE_PERIOD_MS;
            }
        } else if(app->state == StateRolling) {
            roll_step(app);
        }
        ble_keepalive(app);
        view_port_update(app->viewport);
    }

    if(app->roll.phase != RollPhaseIdle) roll_finish(app, true);
    subghz_stop();
    log_close(app);
    usb_release(app);
    ble_stop(app);
    roll_free(&app->roll);
    gui_remove_view_port(app->gui, app->viewport);
    view_port_free(app->viewport);
    furi_message_queue_free(app->input_queue);
    furi_semaphore_free(app->cdc_tx_sem);
    furi_mutex_free(app->mutex);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
