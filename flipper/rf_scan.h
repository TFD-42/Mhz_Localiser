/*
 * rf_scan.h — pure (hardware-free) logic for the two-pass rolling scan.
 *
 * Nothing in here touches furi/HAL, so the whole module is unit-tested on the
 * host (see tests/test_rf_scan.c) and linked unchanged into the FAP.
 *
 * Hardware facts this module encodes (verified against flipperzero-firmware
 * 1.4.3 and dev, targets/f7/furi_hal/furi_hal_subghz.c):
 *   - furi_hal_subghz_is_frequency_valid() accepts only three windows:
 *       299 999 755 – 348 000 335 Hz
 *       386 999 938 – 464 000 000 Hz
 *       778 999 847 – 928 000 000 Hz
 *   - furi_hal_subghz_set_frequency_and_path() selects the RF path from the
 *     *quantized* frequency returned by the CC1101 and calls furi_crash() if it
 *     falls outside those windows. A request that passes is_frequency_valid()
 *     but quantizes just below a lower edge therefore crashes the Flipper.
 *     rf_scan_hz_path_safe() replicates the quantization to rule that out.
 *   - Quantization: FREQ = floor(hz * 2^16 / 26 MHz), real = floor(FREQ * 26 MHz / 2^16)
 *     (lib/drivers/cc1101.c, cc1101_set_frequency).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RF_SCAN_BAND_COUNT 3u

/* Coarse pass: stock ook_650khz preset (RX filter 650 kHz). A step below the
 * filter bandwidth leaves no blind gap between adjacent steps. */
#define RF_SCAN_COARSE_STEP_HZ  600000u
#define RF_SCAN_COARSE_BW_HZ    650000u
#define RF_SCAN_COARSE_DWELL_MS 100u

/* Fine pass: same preset with MDMCFG4 patched to a 58 kHz RX filter, 20 kHz
 * steps, +/-300 kHz around each coarse cluster — the same parameters the stock
 * Sub-GHz Frequency Analyzer uses for its fine stage. */
#define RF_SCAN_FINE_STEP_HZ    20000u
#define RF_SCAN_FINE_BW_HZ      58000u
#define RF_SCAN_FINE_SPAN_HZ    300000u
#define RF_SCAN_FINE_DWELL_MS   40u
#define RF_SCAN_FINE_MAX_STEPS  128u

/* Clusters wider than this are broadband/continuous occupants (GSM-R, TETRA…):
 * the fine pass only resolves +/-span around their strongest coarse step. */
#define RF_SCAN_WIDE_CLUSTER_HZ 1200000u
#define RF_SCAN_MAX_CLUSTERS    16u

/* Detection: peak-hold over the dwell must clear BOTH an absolute floor (the
 * stock Frequency Analyzer threshold, -93 dBm) and the band's measured noise
 * floor + margin. Peak-hold, not average: OOK bursts are short and an average
 * over the dwell dilutes them into the noise. */
#define RF_SCAN_ABS_MIN_DBM     (-93)
#define RF_SCAN_MARGIN_DB       10
#define RF_SCAN_STRONG_DBM      (-80)
#define RF_SCAN_NO_SIGNAL_DBM   (-200)

#define RF_SCAN_SETTLE_MS       2u

/* CC1101 MDMCFG4 (address 0x10): CHANBW_E=3, CHANBW_M=3, DRATE_E=7 → 58 kHz. */
#define RF_SCAN_REG_MDMCFG4     0x10u
#define RF_SCAN_MDMCFG4_58KHZ   0xF7u
#define RF_SCAN_PRESET_MAX      96u

typedef struct {
    uint32_t lo_hz;
    uint32_t hi_hz;
} RfScanBand;

extern const RfScanBand rf_scan_bands[RF_SCAN_BAND_COUNT];

typedef struct {
    int16_t mean_dbm;
    int16_t peak_dbm;
    uint16_t samples;
} RfScanStat;

typedef struct {
    /* coarse */
    uint32_t lo_hz;
    uint32_t hi_hz;
    uint32_t peak_hz;
    int16_t peak_dbm;
    int16_t mean_dbm;
    int16_t floor_dbm;
    uint16_t steps;
    uint32_t samples;
    uint8_t band;
    /* fine (valid when fine_done) */
    bool fine_done;
    uint32_t center_hz;
    uint32_t width_hz;
    int16_t fine_peak_dbm;
    int16_t fine_mean_dbm;
    uint32_t fine_samples;
} RfScanCluster;

typedef struct {
    uint32_t start_hz;
    uint32_t count;
} RfScanWindow;

typedef struct {
    uint32_t center_hz;
    uint32_t width_hz;
    uint32_t peak_index;
    int16_t peak_dbm;
} RfScanFine;

/* ---- band plan ---- */
uint32_t rf_scan_plan_len(uint32_t step_hz);
bool rf_scan_plan_at(uint32_t step_hz, uint32_t idx, uint32_t* hz, uint8_t* band);

/* ---- CC1101 frequency safety ---- */
uint32_t rf_scan_cc1101_quantize(uint32_t hz);
bool rf_scan_hz_path_safe(uint32_t hz);
int rf_scan_band_of(uint32_t hz);

/* ---- detection ---- */
int16_t rf_scan_dbm_round(float dbm);
int16_t rf_scan_threshold(int16_t floor_dbm);
const char* rf_scan_flag(int16_t peak_dbm);
int16_t rf_scan_band_floor(
    const RfScanStat* st,
    uint32_t n,
    uint32_t step_hz,
    uint8_t band,
    int16_t* scratch);
size_t rf_scan_find_clusters(
    const RfScanStat* st,
    uint32_t n,
    uint32_t step_hz,
    const int16_t floor_dbm[RF_SCAN_BAND_COUNT],
    RfScanCluster* out,
    size_t cap);

/* ---- fine pass ---- */
bool rf_scan_fine_window(const RfScanCluster* c, RfScanWindow* w);
bool rf_scan_fine_analyze(
    const int16_t* peak_dbm,
    uint32_t n,
    uint32_t start_hz,
    uint32_t step_hz,
    RfScanFine* out);

/* Copy a CC1101 preset ({reg,val}… 0,0 + 8-byte PA table) patching one
 * register. Returns bytes written, 0 if dst is too small or reg is absent. */
size_t rf_scan_clone_preset(
    const uint8_t* src,
    uint8_t* dst,
    size_t cap,
    uint8_t reg,
    uint8_t value);

#ifdef __cplusplus
}
#endif
