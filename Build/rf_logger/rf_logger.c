#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_subghz.h>
#include <furi_hal_usb_cdc.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/number_input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <lib/subghz/devices/cc1101_configs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG        "RFLogger"
#define LOG_DIR    "/ext/rf_logger"
#define TX_BUF     256
#define FREQ_N     8
#define SAMPLE_MS  200

typedef enum {
    ViewMenu,
    ViewLogging,
    ViewManualFreq,
} AppView;

typedef struct {
    uint32_t hz;
    const char* label;
} FreqPreset;

#define IDX_MANUAL  FREQ_N
#define IDX_SD      (FREQ_N + 1)

static const FreqPreset FREQS[FREQ_N] = {
    {315000000,  "315 MHz"},
    {433920000,  "433.92 MHz"},
    {434000000,  "434 MHz"},
    {868000000,  "868 MHz"},
    {868350000,  "868.35 MHz"},
    {915000000,  "915 MHz"},
    {868800000,  "868.8 MHz"},
    {433050000,  "433.05 MHz"},
};

/* Model stored inside the View — updated via with_view_model() so the GUI
   framework knows to redraw. */
typedef struct {
    int16_t  rssi;       /* dBm as integer */
    uint8_t  lqi;
    uint32_t n;
    uint32_t freq_hz;
    uint32_t actual_hz;
    bool     sd_log;
    /* debug */
    uint8_t  rssi_raw;   /* raw byte back-computed from dBm */
    char     dbg[48];    /* one-line debug string written by thread */
} LogViewModel;

/* Top-level app state — not the view model */
typedef struct {
    Gui*             gui;
    ViewDispatcher*  view_dispatcher;
    Submenu*         submenu;
    View*            log_view;
    NumberInput*     num_input;
    NotificationApp* notif;
    Storage*         storage;

    uint32_t freq_hz;
    bool     logging;
    bool     sd_log;

    FuriThread* thread;
    File*       file;
} App;

/* ── helpers ────────────────────────────────────────────────────── */
static void cdc_send(const char* s) {
    furi_hal_cdc_send(0, (uint8_t*)s, (uint16_t)strlen(s));
}

static void fmt_freq(char* buf, size_t sz, uint32_t hz) {
    uint32_t mhz  = hz / 1000000;
    uint32_t frac = (hz % 1000000) / 10000;
    if(frac)
        snprintf(buf, sz, "%lu.%02lu MHz", mhz, frac);
    else
        snprintf(buf, sz, "%lu MHz", mhz);
}

/* Reverse HAL formula: dBm → raw CC1101 byte (for debug display) */
static uint8_t rssi_to_raw(float dbm) {
    int16_t r = (int16_t)((dbm + 74.0f) * 2.0f);
    if(r < 0) r += 256;
    return (uint8_t)(r & 0xFF);
}

/* ── logger thread ──────────────────────────────────────────────── */
static int32_t logger_thread(void* ctx) {
    App* app = ctx;
    char line[TX_BUF];

    /* exact init sequence from subghz_cli rx_carrier — confirmed working */
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_650khz_async_regs);
    uint32_t actual = furi_hal_subghz_set_frequency_and_path(app->freq_hz);
    furi_hal_subghz_rx();

    /* USB header */
    snprintf(line, TX_BUF,
        "# RF_LOGGER_DBG req=%lu act=%lu\r\n"
        "ts_ms,req_hz,act_hz,rssi_dbm,rssi_raw,lqi,n\r\n",
        app->freq_hz, actual);
    cdc_send(line);

    /* SD log */
    File* f = NULL;
    if(app->sd_log) {
        char path[64];
        storage_common_mkdir(app->storage, LOG_DIR);
        snprintf(path, sizeof(path), "%s/dbg_%lu.csv", LOG_DIR,
            (uint32_t)(furi_get_tick() / furi_kernel_get_tick_frequency()));
        f = storage_file_alloc(app->storage);
        if(!storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            storage_file_free(f); f = NULL;
        } else {
            storage_file_write(f, line, strlen(line));
        }
    }
    app->file = f;

    uint32_t loop = 0;
    while(app->logging) {
        uint64_t ts = (uint64_t)furi_get_tick() * 1000ULL /
                      (uint64_t)furi_kernel_get_tick_frequency();
        float   r = furi_hal_subghz_get_rssi();
        uint8_t q = furi_hal_subghz_get_lqi();
        loop++;

        uint8_t raw = rssi_to_raw(r);

        /* update view model — triggers a redraw */
        with_view_model(app->log_view, LogViewModel* vm, {
            vm->rssi      = (int16_t)r;
            vm->lqi       = q;
            vm->n         = loop;
            vm->freq_hz   = app->freq_hz;
            vm->actual_hz = actual;
            vm->sd_log    = app->sd_log;
            vm->rssi_raw  = raw;
            snprintf(vm->dbg, sizeof(vm->dbg),
                "raw=0x%02X act=%luM #%lu", raw, actual / 1000000, loop);
        }, true);

        snprintf(line, TX_BUF,
            "%llu,%lu,%lu,%d,0x%02X,%u,%lu\r\n",
            ts, app->freq_hz, actual, (int)r, raw, q, loop);
        cdc_send(line);
        if(f) storage_file_write(f, line, strlen(line));

        furi_delay_ms(SAMPLE_MS);
    }

    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();

    if(f) { storage_file_close(f); storage_file_free(f); app->file = NULL; }
    return 0;
}

/* ── logging view draw ──────────────────────────────────────────── */
static void log_draw(Canvas* canvas, void* model) {
    LogViewModel* vm = (LogViewModel*)model;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "RF Logger");
    canvas_set_font(canvas, FontSecondary);
    if(vm->sd_log) canvas_draw_str(canvas, 96, 10, "[SD]");

    /* frequency */
    char fbuf[24];
    fmt_freq(fbuf, sizeof(fbuf), vm->freq_hz ? vm->freq_hz : 433920000);
    canvas_draw_str(canvas, 0, 21, fbuf);

    /* RSSI — big numbers */
    char rbuf[16];
    snprintf(rbuf, sizeof(rbuf), "%d dBm", (int)vm->rssi);
    canvas_set_font(canvas, FontBigNumbers);
    uint16_t rw = (uint16_t)canvas_string_width(canvas, rbuf);
    canvas_draw_str(canvas, (uint8_t)((128 - rw) / 2), 40, rbuf);

    /* signal bar: –120..0 dBm → 0..124 px */
    int bar = 0;
    int ri  = (int)vm->rssi;
    if(ri > -120 && ri < 0) bar = (ri + 120) * 124 / 120;
    if(bar > 124) bar = 124;
    canvas_draw_frame(canvas, 2, 43, 124, 7);
    if(bar > 0) canvas_draw_box(canvas, 2, 43, (uint8_t)bar, 7);

    /* debug + LQI line */
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 57, vm->dbg);

    char lbuf[20];
    snprintf(lbuf, sizeof(lbuf), "LQI:%u", vm->lqi);
    canvas_draw_str(canvas, 0, 64, lbuf);
}

static bool log_input(InputEvent* ev, void* ctx) {
    App* app = (App*)ctx;
    if(ev->type == InputTypeShort) {
        if(ev->key == InputKeyBack) {
            app->logging = false;
            view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
            return true;
        }
        if(ev->key == InputKeyOk) {
            app->sd_log = !app->sd_log;
            with_view_model(app->log_view, LogViewModel* vm,
                { vm->sd_log = app->sd_log; }, true);
            return true;
        }
    }
    return false;
}

static uint32_t log_prev(void* ctx) {
    UNUSED(ctx);
    return ViewMenu;
}

/* ── start logging ──────────────────────────────────────────────── */
static void start_logging(App* app) {
    app->logging = true;

    /* seed the model so the screen isn't blank while the thread starts */
    with_view_model(app->log_view, LogViewModel* vm, {
        vm->freq_hz   = app->freq_hz;
        vm->actual_hz = 0;
        vm->rssi      = -120;
        vm->lqi       = 0;
        vm->n         = 0;
        vm->sd_log    = app->sd_log;
        vm->rssi_raw  = 0;
        snprintf(vm->dbg, sizeof(vm->dbg), "starting...");
    }, true);

    if(app->thread) {
        furi_thread_join(app->thread);
        furi_thread_free(app->thread);
    }
    app->thread = furi_thread_alloc_ex("RFLog", 3072, logger_thread, app);
    furi_thread_start(app->thread);
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewLogging);
}

/* ── manual freq callback ───────────────────────────────────────── */
static void manual_freq_cb(void* ctx, int32_t mhz) {
    App* app = ctx;
    if(mhz < 300 || mhz > 928) {
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
        return;
    }
    app->freq_hz = (uint32_t)mhz * 1000000UL;
    start_logging(app);
}

/* ── menu callback ──────────────────────────────────────────────── */
static void menu_cb(void* ctx, uint32_t idx) {
    App* app = ctx;
    if(idx == (uint32_t)IDX_SD) {
        app->sd_log = !app->sd_log;
        submenu_change_item_label(app->submenu, IDX_SD,
            app->sd_log ? "SD Log: ON  [toggle]" : "SD Log: OFF [toggle]");
        return;
    }
    if(idx == (uint32_t)IDX_MANUAL) {
        int32_t cur = (int32_t)(app->freq_hz / 1000000);
        number_input_set_header_text(app->num_input, "Frequency MHz (300-928)");
        number_input_set_result_callback(app->num_input, manual_freq_cb, app, cur, 300, 928);
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewManualFreq);
        return;
    }
    app->freq_hz = FREQS[idx].hz;
    start_logging(app);
}

/* ── alloc / free ───────────────────────────────────────────────── */
/* Dummy n field so App compiles — threads use app->thread directly */
static App* app_alloc(void) {
    App* a = malloc(sizeof(App));
    memset(a, 0, sizeof(App));
    a->freq_hz = FREQS[1].hz;
    a->sd_log  = true;
    a->gui     = furi_record_open(RECORD_GUI);
    a->notif   = furi_record_open(RECORD_NOTIFICATION);
    a->storage = furi_record_open(RECORD_STORAGE);

    a->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(a->view_dispatcher, a->gui, ViewDispatcherTypeFullscreen);

    /* menu */
    a->submenu = submenu_alloc();
    for(int i = 0; i < FREQ_N; i++)
        submenu_add_item(a->submenu, FREQS[i].label, i, menu_cb, a);
    submenu_add_item(a->submenu, "Manual MHz...",        IDX_MANUAL, menu_cb, a);
    submenu_add_item(a->submenu, "SD Log: ON  [toggle]", IDX_SD,     menu_cb, a);
    view_dispatcher_add_view(a->view_dispatcher, ViewMenu, submenu_get_view(a->submenu));

    /* logging view — allocate a LogViewModel inside the view */
    a->log_view = view_alloc();
    view_allocate_model(a->log_view, ViewModelTypeLockFree, sizeof(LogViewModel));
    view_set_context(a->log_view, a);
    view_set_draw_callback(a->log_view, log_draw);
    view_set_input_callback(a->log_view, log_input);
    view_set_previous_callback(a->log_view, log_prev);
    view_dispatcher_add_view(a->view_dispatcher, ViewLogging, a->log_view);

    /* number input */
    a->num_input = number_input_alloc();
    view_dispatcher_add_view(a->view_dispatcher, ViewManualFreq,
        number_input_get_view(a->num_input));

    return a;
}

static void app_free(App* a) {
    a->logging = false;
    if(a->thread) {
        furi_thread_join(a->thread);
        furi_thread_free(a->thread);
    }
    if(a->file) { storage_file_close(a->file); storage_file_free(a->file); }
    view_dispatcher_remove_view(a->view_dispatcher, ViewMenu);
    view_dispatcher_remove_view(a->view_dispatcher, ViewLogging);
    view_dispatcher_remove_view(a->view_dispatcher, ViewManualFreq);
    view_free(a->log_view);
    submenu_free(a->submenu);
    number_input_free(a->num_input);
    view_dispatcher_free(a->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(a);
}

int32_t rf_logger_app(void* p) {
    UNUSED(p);
    App* a = app_alloc();
    view_dispatcher_switch_to_view(a->view_dispatcher, ViewMenu);
    view_dispatcher_run(a->view_dispatcher);
    app_free(a);
    return 0;
}
