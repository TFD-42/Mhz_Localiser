/*
 * Host unit tests for rf_scan.c (no Flipper SDK needed).
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -I.. ../rf_scan.c test_rf_scan.c -lm -o test_rf_scan
 *   ./test_rf_scan
 *
 * Or: make -C flipper/tests
 */
#include "../rf_scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if(!(cond)) {                                                       \
            failures++;                                                     \
            fprintf(stderr, "FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__);                                   \
            fputc('\n', stderr);                                            \
        }                                                                   \
    } while(0)

/* Reference firmware gate, written independently from rf_scan.c:
 * furi_hal_subghz_is_frequency_valid() + set_frequency_and_path() on the
 * quantized value (firmware 1.4.3 / dev). */
static int ref_window(uint32_t hz) {
    if(hz >= 299999755u && hz <= 348000335u) return 0;
    if(hz >= 386999938u && hz <= 464000000u) return 1;
    if(hz >= 778999847u && hz <= 928000000u) return 2;
    return -1;
}
static uint32_t ref_quantize(uint32_t hz) {
    unsigned long long reg = (unsigned long long)hz * 65536ull / 26000000ull;
    return (uint32_t)(reg * 26000000ull / 65536ull);
}
static int ref_would_crash(uint32_t hz) {
    int w = ref_window(hz);
    return w >= 0 && ref_window(ref_quantize(hz)) != w;
}

static void test_plan(void) {
    const uint32_t step = RF_SCAN_COARSE_STEP_HZ;
    uint32_t n = rf_scan_plan_len(step);
    /* 300..348 step .6 → 81 pts (348 hit exactly); 387..464 → 129 + edge = 130;
     * 779..928 → 249 + edge = 250 */
    CHECK(n == 461u, "plan_len=%u", (unsigned)n);

    uint32_t prev_hz = 0;
    uint8_t prev_band = 255;
    uint32_t per_band[RF_SCAN_BAND_COUNT] = {0};
    for(uint32_t i = 0; i < n; i++) {
        uint32_t hz;
        uint8_t band;
        CHECK(rf_scan_plan_at(step, i, &hz, &band), "plan_at(%u) false", (unsigned)i);
        per_band[band]++;
        CHECK(hz >= rf_scan_bands[band].lo_hz && hz <= rf_scan_bands[band].hi_hz,
              "idx %u hz %u outside band %u", (unsigned)i, (unsigned)hz, band);
        CHECK(rf_scan_hz_path_safe(hz), "idx %u hz %u not path-safe", (unsigned)i, (unsigned)hz);
        CHECK(!ref_would_crash(hz), "idx %u hz %u would crash firmware", (unsigned)i, (unsigned)hz);
        CHECK(hz > prev_hz, "plan not increasing at %u", (unsigned)i);
        if(band == prev_band) {
            CHECK(hz - prev_hz <= step, "gap %u > step at %u", (unsigned)(hz - prev_hz), (unsigned)i);
            /* contiguous coverage: neighbouring RX filters overlap */
            CHECK(hz - prev_hz <= RF_SCAN_COARSE_BW_HZ, "blind gap at %u", (unsigned)i);
        } else {
            CHECK(hz == rf_scan_bands[band].lo_hz, "band %u starts at %u", band, (unsigned)hz);
            if(prev_band != 255)
                CHECK(prev_hz == rf_scan_bands[prev_band].hi_hz, "band %u ends at %u", prev_band,
                      (unsigned)prev_hz);
        }
        prev_hz = hz;
        prev_band = band;
    }
    CHECK(prev_hz == rf_scan_bands[2].hi_hz, "last point %u", (unsigned)prev_hz);
    CHECK(per_band[0] == 81u && per_band[1] == 130u && per_band[2] == 250u,
          "per band %u/%u/%u", (unsigned)per_band[0], (unsigned)per_band[1], (unsigned)per_band[2]);
    CHECK(!rf_scan_plan_at(step, n, NULL, NULL), "plan_at past end returned true");
    CHECK(rf_scan_plan_len(0) == 0u, "zero step");
}

static void test_path_safety(void) {
    /* Equivalence with the reference gate over every kHz of 290–940 MHz,
     * plus every Hz within ±3 kHz of each firmware edge. */
    for(uint32_t hz = 290000000u; hz <= 940000000u; hz += 1000u) {
        int expect = ref_window(hz) >= 0 && !ref_would_crash(hz);
        if(rf_scan_hz_path_safe(hz) != expect) {
            CHECK(0, "path_safe mismatch at %u", (unsigned)hz);
            break;
        }
    }
    const uint32_t edges[] = {299999755u, 348000335u, 386999938u, 464000000u, 778999847u, 928000000u};
    uint32_t crash_cases = 0;
    for(size_t e = 0; e < sizeof(edges) / sizeof(edges[0]); e++) {
        for(uint32_t hz = edges[e] - 3000u; hz <= edges[e] + 3000u; hz++) {
            int expect = ref_window(hz) >= 0 && !ref_would_crash(hz);
            if(ref_would_crash(hz)) crash_cases++;
            if(rf_scan_hz_path_safe(hz) != expect) {
                CHECK(0, "edge mismatch at %u", (unsigned)hz);
                break;
            }
        }
    }
    /* The firmware lower bounds themselves pass is_frequency_valid() but
     * quantize below the window → furi_crash. The guard must reject them. */
    CHECK(crash_cases >= 3u, "expected crashing edge requests, found %u", (unsigned)crash_cases);
    CHECK(!rf_scan_hz_path_safe(299999755u), "299999755 must be rejected");
    CHECK(!rf_scan_hz_path_safe(386999938u), "386999938 must be rejected");
    CHECK(!rf_scan_hz_path_safe(778999847u), "778999847 must be rejected");
    CHECK(rf_scan_hz_path_safe(433920000u), "433.92 must be accepted");
    CHECK(!rf_scan_hz_path_safe(349000000u), "349 MHz hole");
    CHECK(!rf_scan_hz_path_safe(600000000u), "600 MHz hole");

    /* Every fine-pass point inside the scan bands is safe. */
    for(uint32_t b = 0; b < RF_SCAN_BAND_COUNT; b++) {
        for(uint32_t hz = rf_scan_bands[b].lo_hz; hz <= rf_scan_bands[b].hi_hz;
            hz += RF_SCAN_FINE_STEP_HZ) {
            if(!rf_scan_hz_path_safe(hz)) {
                CHECK(0, "fine point %u unsafe", (unsigned)hz);
                break;
            }
        }
    }
    CHECK(rf_scan_band_of(433920000u) == 1, "band_of 433.92");
    CHECK(rf_scan_band_of(600000000u) == -1, "band_of hole");
}

static void test_dbm_and_flags(void) {
    CHECK(rf_scan_dbm_round(-74.5f) == -75, "%d", rf_scan_dbm_round(-74.5f));
    CHECK(rf_scan_dbm_round(-74.4f) == -74, "%d", rf_scan_dbm_round(-74.4f));
    CHECK(rf_scan_dbm_round(0.4f) == 0, "%d", rf_scan_dbm_round(0.4f));
    CHECK(rf_scan_dbm_round(-1000.0f) == -300, "%d", rf_scan_dbm_round(-1000.0f));
    float nan = 0.0f / 0.0f;
    CHECK(rf_scan_dbm_round(nan) == RF_SCAN_NO_SIGNAL_DBM, "nan");
    CHECK(rf_scan_threshold(-110) == RF_SCAN_ABS_MIN_DBM, "%d", rf_scan_threshold(-110));
    CHECK(rf_scan_threshold(-90) == -80, "%d", rf_scan_threshold(-90));
    CHECK(strcmp(rf_scan_flag(-80), "STRONG") == 0, "strong edge");
    CHECK(strcmp(rf_scan_flag(-81), "MED") == 0, "med");
}

/* Synthetic coarse sweep: flat floor per band + injected peaks. */
static RfScanStat* make_sweep(uint32_t* n_out, const int16_t floor_db[3]) {
    uint32_t n = rf_scan_plan_len(RF_SCAN_COARSE_STEP_HZ);
    RfScanStat* st = calloc(n, sizeof(*st));
    for(uint32_t i = 0; i < n; i++) {
        uint8_t b;
        rf_scan_plan_at(RF_SCAN_COARSE_STEP_HZ, i, NULL, &b);
        /* deterministic ±2 dB ripple around the floor */
        int16_t ripple = (int16_t)((int)(i % 5u) - 2);
        st[i].mean_dbm = (int16_t)(floor_db[b] + ripple);
        st[i].peak_dbm = (int16_t)(floor_db[b] + ripple + 3);
        st[i].samples = 97;
    }
    *n_out = n;
    return st;
}

static uint32_t idx_of(uint32_t hz) {
    uint32_t n = rf_scan_plan_len(RF_SCAN_COARSE_STEP_HZ);
    for(uint32_t i = 0; i < n; i++) {
        uint32_t f;
        rf_scan_plan_at(RF_SCAN_COARSE_STEP_HZ, i, &f, NULL);
        if(f == hz) return i;
    }
    return UINT32_MAX;
}

static void test_floor_and_clusters(void) {
    const int16_t floors[3] = {-105, -100, -98};
    uint32_t n;
    RfScanStat* st = make_sweep(&n, floors);
    int16_t* scratch = calloc(n, sizeof(int16_t));

    int16_t fl[3];
    for(uint8_t b = 0; b < 3; b++) {
        fl[b] = rf_scan_band_floor(st, n, RF_SCAN_COARSE_STEP_HZ, b, scratch);
        CHECK(fl[b] >= floors[b] - 1 && fl[b] <= floors[b] + 1, "floor b%u=%d", b, fl[b]);
    }

    /* 1) narrow signal on one step (433.8 MHz) */
    uint32_t i433 = idx_of(433800000u);
    CHECK(i433 != UINT32_MAX, "433.8 not a plan point");
    st[i433].peak_dbm = -60;
    st[i433].mean_dbm = -70;
    /* 2) two-step signal around 868.4/869.0 */
    uint32_t i868 = idx_of(868400000u);
    CHECK(i868 != UINT32_MAX, "868.4 not a plan point");
    st[i868].peak_dbm = -75;
    st[i868 + 1].peak_dbm = -72;
    /* 3) wide occupant 921–925 MHz */
    uint32_t i921 = idx_of(921200000u);
    CHECK(i921 != UINT32_MAX, "921.2 not a plan point");
    for(uint32_t k = 0; k < 7; k++) st[i921 + k].peak_dbm = (int16_t)(-70 - (int)k);
    /* 4) below-threshold bump must NOT trigger: floor -100 + 10 = -90 */
    uint32_t i403 = idx_of(403200000u);
    st[i403].peak_dbm = -91;
    /* 5) last point of band 0 (348) and first of band 1 (387) both hot:
     *    must stay two clusters (different bands) */
    uint32_t i348 = idx_of(348000000u), i387 = idx_of(387000000u);
    CHECK(i387 == i348 + 1u, "band boundary indices");
    st[i348].peak_dbm = -70;
    st[i387].peak_dbm = -70;

    RfScanCluster cl[RF_SCAN_MAX_CLUSTERS];
    size_t k = rf_scan_find_clusters(st, n, RF_SCAN_COARSE_STEP_HZ, fl, cl, RF_SCAN_MAX_CLUSTERS);
    CHECK(k == 5u, "clusters=%u", (unsigned)k);
    for(size_t i = 1; i < k; i++) CHECK(cl[i].lo_hz > cl[i - 1].lo_hz, "not sorted");
    if(k == 5u) {
        CHECK(cl[0].lo_hz == 348000000u && cl[0].band == 0, "c0 %u", (unsigned)cl[0].lo_hz);
        CHECK(cl[1].lo_hz == 387000000u && cl[1].band == 1, "c1 %u", (unsigned)cl[1].lo_hz);
        CHECK(cl[2].peak_hz == 433800000u && cl[2].steps == 1 && cl[2].peak_dbm == -60, "c2");
        CHECK(cl[3].lo_hz == 868400000u && cl[3].hi_hz == 869000000u && cl[3].peak_hz == 869000000u,
              "c3 %u-%u pk %u", (unsigned)cl[3].lo_hz, (unsigned)cl[3].hi_hz,
              (unsigned)cl[3].peak_hz);
        CHECK(cl[4].steps == 7 && cl[4].peak_hz == 921200000u, "c4 steps %u", cl[4].steps);
        CHECK(cl[2].samples == 97u, "samples");

        RfScanWindow w;
        CHECK(rf_scan_fine_window(&cl[2], &w), "window c2");
        CHECK(w.start_hz == 433500000u && w.count == 31u, "c2 win %u x%u", (unsigned)w.start_hz,
              (unsigned)w.count);
        CHECK(rf_scan_fine_window(&cl[0], &w), "window c0");
        CHECK(w.start_hz + (w.count - 1u) * RF_SCAN_FINE_STEP_HZ <= rf_scan_bands[0].hi_hz,
              "c0 window exceeds band");
        CHECK(rf_scan_fine_window(&cl[1], &w), "window c1");
        CHECK(w.start_hz == rf_scan_bands[1].lo_hz, "c1 window not clipped: %u", (unsigned)w.start_hz);
        /* wide cluster (3.6 MHz) → ±span around the peak only */
        CHECK(rf_scan_fine_window(&cl[4], &w), "window c4");
        CHECK(w.start_hz == 920900000u && w.count == 31u, "c4 win %u x%u", (unsigned)w.start_hz,
              (unsigned)w.count);
    }

    /* capacity: 20 isolated hits, cap 16 → the 16 strongest survive */
    RfScanStat* st2 = make_sweep(&n, floors);
    for(uint32_t h = 0; h < 20; h++) {
        uint32_t i = 10u + h * 20u;
        st2[i].peak_dbm = (int16_t)(-85 + (int)h); // -85..-66
    }
    k = rf_scan_find_clusters(st2, n, RF_SCAN_COARSE_STEP_HZ, fl, cl, RF_SCAN_MAX_CLUSTERS);
    CHECK(k == RF_SCAN_MAX_CLUSTERS, "cap k=%u", (unsigned)k);
    int16_t weakest = 0;
    for(size_t i = 0; i < k; i++) if(i == 0 || cl[i].peak_dbm < weakest) weakest = cl[i].peak_dbm;
    CHECK(weakest == -81, "weakest kept %d", weakest);

    /* failed steps (samples==0) are neither floor input nor detections */
    RfScanStat* st3 = make_sweep(&n, floors);
    st3[i433].samples = 0;
    st3[i433].peak_dbm = -10;
    k = rf_scan_find_clusters(st3, n, RF_SCAN_COARSE_STEP_HZ, fl, cl, RF_SCAN_MAX_CLUSTERS);
    CHECK(k == 0u, "failed step detected k=%u", (unsigned)k);

    free(st);
    free(st2);
    free(st3);
    free(scratch);
}

static void test_fine_analyze(void) {
    /* symmetric lobe centred between idx 10 and 11 → centroid 10.5 */
    int16_t p[31];
    for(int i = 0; i < 31; i++) p[i] = -100;
    p[9] = -64; p[10] = -60; p[11] = -60; p[12] = -64;
    RfScanFine f;
    CHECK(rf_scan_fine_analyze(p, 31, 433500000u, 20000u, &f), "analyze");
    CHECK(f.center_hz == 433710000u, "center %u", (unsigned)f.center_hz);
    CHECK(f.width_hz == 80000u, "width %u", (unsigned)f.width_hz);
    CHECK(f.peak_dbm == -60 && f.peak_index == 10u, "peak");

    /* single bin */
    for(int i = 0; i < 31; i++) p[i] = -100;
    p[0] = -50;
    CHECK(rf_scan_fine_analyze(p, 31, 433500000u, 20000u, &f), "edge analyze");
    CHECK(f.center_hz == 433500000u && f.width_hz == 20000u, "edge %u %u", (unsigned)f.center_hz,
          (unsigned)f.width_hz);

    /* all failed */
    for(int i = 0; i < 31; i++) p[i] = RF_SCAN_NO_SIGNAL_DBM;
    CHECK(!rf_scan_fine_analyze(p, 31, 433500000u, 20000u, &f), "no signal must fail");
    CHECK(!rf_scan_fine_analyze(p, 0, 0, 20000u, &f), "n=0");
}

static void test_preset_clone(void) {
    /* layout identical to subghz_device_cc1101_preset_ook_650khz_async_regs */
    const uint8_t src[] = {
        0x02, 0x0D, 0x03, 0x07, 0x08, 0x32, 0x0B, 0x06, 0x14, 0x00, 0x13, 0x00, 0x12, 0x30,
        0x11, 0x32, 0x10, 0x17, 0x18, 0x18, 0x19, 0x18, 0x1D, 0x91, 0x1C, 0x00, 0x1B, 0x07,
        0x20, 0xFB, 0x22, 0x11, 0x21, 0xB6, 0x00, 0x00, 0x00, 0xC0, 0, 0, 0, 0, 0, 0};
    uint8_t dst[RF_SCAN_PRESET_MAX];
    size_t n = rf_scan_clone_preset(src, dst, sizeof(dst), RF_SCAN_REG_MDMCFG4, RF_SCAN_MDMCFG4_58KHZ);
    CHECK(n == sizeof(src), "clone size %u", (unsigned)n);
    CHECK(dst[16] == 0x10 && dst[17] == 0xF7, "MDMCFG4 not patched: %02X=%02X", dst[16], dst[17]);
    dst[17] = 0x17;
    CHECK(memcmp(src, dst, sizeof(src)) == 0, "other bytes changed");
    CHECK(rf_scan_clone_preset(src, dst, 20, RF_SCAN_REG_MDMCFG4, 0xF7) == 0u, "overflow not caught");
    CHECK(rf_scan_clone_preset(src, dst, sizeof(dst), 0x3F, 0x00) == 0u, "missing reg not caught");

    /* 58 kHz decode: BW = 26e6 / (8 * (4 + M) * 2^E) */
    uint8_t v = RF_SCAN_MDMCFG4_58KHZ;
    unsigned e = (v >> 6) & 3u, m = (v >> 4) & 3u;
    double bw = 26e6 / (8.0 * (4 + m) * (double)(1u << e));
    CHECK(bw > 58000.0 && bw < 58100.0, "bw %.1f", bw);
    v = 0x17;
    e = (v >> 6) & 3u, m = (v >> 4) & 3u;
    bw = 26e6 / (8.0 * (4 + m) * (double)(1u << e));
    CHECK(bw == 650000.0, "stock bw %.1f", bw);
}

int main(void) {
    test_plan();
    test_path_safety();
    test_dbm_and_flags();
    test_floor_and_clusters();
    test_fine_analyze();
    test_preset_clone();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
