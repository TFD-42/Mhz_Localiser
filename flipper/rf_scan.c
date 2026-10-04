/*
 * rf_scan.c — hardware-free logic for the two-pass rolling scan.
 * See rf_scan.h for the hardware facts encoded here and their sources.
 */
#include "rf_scan.h"

#include <math.h>
#include <string.h>

/* Scan plan: round-MHz edges inside the firmware windows. Every edge is the
 * exact image of a CC1101 grid point, so any request >= lo quantizes >= the
 * firmware's lower bound (checked exhaustively by the host tests). */
const RfScanBand rf_scan_bands[RF_SCAN_BAND_COUNT] = {
    {300000000u, 348000000u},
    {387000000u, 464000000u},
    {779000000u, 928000000u},
};

/* Firmware windows, verbatim from furi_hal_subghz_is_frequency_valid() and
 * furi_hal_subghz_set_frequency_and_path(). */
static const RfScanBand fw_windows[RF_SCAN_BAND_COUNT] = {
    {299999755u, 348000335u},
    {386999938u, 464000000u},
    {778999847u, 928000000u},
};

#define CC1101_QUARTZ_HZ 26000000ull
#define CC1101_FDIV      0x10000ull
#define CC1101_FMASK     0xFFFFFFull

/* ------------------------------------------------------------------ plan */

static uint32_t band_points(const RfScanBand* b, uint32_t step_hz) {
    if(step_hz == 0u || b->hi_hz < b->lo_hz) return 0u;
    uint32_t n = (b->hi_hz - b->lo_hz) / step_hz + 1u;
    uint32_t last = b->lo_hz + (n - 1u) * step_hz;
    if(last != b->hi_hz) n++; /* always measure the upper edge */
    return n;
}

uint32_t rf_scan_plan_len(uint32_t step_hz) {
    uint32_t total = 0u;
    for(uint32_t b = 0; b < RF_SCAN_BAND_COUNT; b++) total += band_points(&rf_scan_bands[b], step_hz);
    return total;
}

bool rf_scan_plan_at(uint32_t step_hz, uint32_t idx, uint32_t* hz, uint8_t* band) {
    for(uint32_t b = 0; b < RF_SCAN_BAND_COUNT; b++) {
        const RfScanBand* bd = &rf_scan_bands[b];
        uint32_t n = band_points(bd, step_hz);
        if(idx < n) {
            uint32_t f = bd->lo_hz + idx * step_hz;
            if(f > bd->hi_hz || idx == n - 1u) f = bd->hi_hz;
            if(hz) *hz = f;
            if(band) *band = (uint8_t)b;
            return true;
        }
        idx -= n;
    }
    return false;
}

/* -------------------------------------------------------- CC1101 safety */

uint32_t rf_scan_cc1101_quantize(uint32_t hz) {
    uint64_t reg = ((uint64_t)hz * CC1101_FDIV) / CC1101_QUARTZ_HZ;
    reg &= CC1101_FMASK;
    return (uint32_t)((reg * CC1101_QUARTZ_HZ) / CC1101_FDIV);
}

static int fw_window_of(uint32_t hz) {
    for(int b = 0; b < (int)RF_SCAN_BAND_COUNT; b++) {
        if(hz >= fw_windows[b].lo_hz && hz <= fw_windows[b].hi_hz) return b;
    }
    return -1;
}

bool rf_scan_hz_path_safe(uint32_t hz) {
    /* Gate 1: is_frequency_valid() on the requested value.
     * Gate 2: set_frequency_and_path() on the quantized value (else furi_crash). */
    int req = fw_window_of(hz);
    if(req < 0) return false;
    return fw_window_of(rf_scan_cc1101_quantize(hz)) == req;
}

int rf_scan_band_of(uint32_t hz) {
    for(int b = 0; b < (int)RF_SCAN_BAND_COUNT; b++) {
        if(hz >= rf_scan_bands[b].lo_hz && hz <= rf_scan_bands[b].hi_hz) return b;
    }
    return -1;
}

/* ------------------------------------------------------------ detection */

int16_t rf_scan_dbm_round(float dbm) {
    if(dbm != dbm) return RF_SCAN_NO_SIGNAL_DBM; /* NaN */
    if(dbm > 100.0f) dbm = 100.0f;
    if(dbm < -300.0f) dbm = -300.0f;
    int v = (dbm >= 0.0f) ? (int)(dbm + 0.5f) : -(int)(-dbm + 0.5f);
    return (int16_t)v;
}

int16_t rf_scan_threshold(int16_t floor_dbm) {
    int t = (int)floor_dbm + RF_SCAN_MARGIN_DB;
    if(t < RF_SCAN_ABS_MIN_DBM) t = RF_SCAN_ABS_MIN_DBM;
    return (int16_t)t;
}

const char* rf_scan_flag(int16_t peak_dbm) {
    return (peak_dbm >= RF_SCAN_STRONG_DBM) ? "STRONG" : "MED";
}

int16_t rf_scan_band_floor(
    const RfScanStat* st,
    uint32_t n,
    uint32_t step_hz,
    uint8_t band,
    int16_t* scratch) {
    if(!st || !scratch) return RF_SCAN_NO_SIGNAL_DBM;
    uint32_t m = 0u;
    for(uint32_t i = 0; i < n; i++) {
        uint8_t b;
        if(!rf_scan_plan_at(step_hz, i, NULL, &b) || b != band) continue;
        if(st[i].samples == 0u) continue; /* never measured (tune failure) */
        /* insertion sort as we go */
        int16_t v = st[i].mean_dbm;
        uint32_t j = m;
        while(j > 0u && scratch[j - 1u] > v) {
            scratch[j] = scratch[j - 1u];
            j--;
        }
        scratch[j] = v;
        m++;
    }
    if(m == 0u) return RF_SCAN_NO_SIGNAL_DBM;
    if(m & 1u) return scratch[m / 2u];
    return (int16_t)(((int32_t)scratch[m / 2u - 1u] + (int32_t)scratch[m / 2u]) / 2);
}

static void cluster_insert(RfScanCluster* out, size_t* count, size_t cap, const RfScanCluster* c) {
    if(cap == 0u) return;
    if(*count < cap) {
        out[(*count)++] = *c;
        return;
    }
    /* full: evict the weakest if the newcomer is stronger */
    size_t weakest = 0u;
    for(size_t i = 1; i < *count; i++) {
        if(out[i].peak_dbm < out[weakest].peak_dbm) weakest = i;
    }
    if(c->peak_dbm > out[weakest].peak_dbm) out[weakest] = *c;
}

size_t rf_scan_find_clusters(
    const RfScanStat* st,
    uint32_t n,
    uint32_t step_hz,
    const int16_t floor_dbm[RF_SCAN_BAND_COUNT],
    RfScanCluster* out,
    size_t cap) {
    if(!st || !floor_dbm || !out) return 0u;
    size_t count = 0u;
    bool open = false;
    RfScanCluster cur;
    int32_t mean_sum = 0;
    memset(&cur, 0, sizeof(cur));

    for(uint32_t i = 0; i <= n; i++) {
        uint32_t hz = 0u;
        uint8_t band = 0u;
        bool hit = false;
        if(i < n && rf_scan_plan_at(step_hz, i, &hz, &band) && st[i].samples > 0u) {
            hit = st[i].peak_dbm >= rf_scan_threshold(floor_dbm[band]);
        }
        if(open && (!hit || band != cur.band)) {
            cur.mean_dbm = (int16_t)(mean_sum / (int32_t)cur.steps);
            cluster_insert(out, &count, cap, &cur);
            open = false;
        }
        if(!hit) continue;
        if(!open) {
            memset(&cur, 0, sizeof(cur));
            cur.lo_hz = hz;
            cur.peak_hz = hz;
            cur.peak_dbm = st[i].peak_dbm;
            cur.band = band;
            cur.floor_dbm = floor_dbm[band];
            mean_sum = 0;
            open = true;
        }
        cur.hi_hz = hz;
        cur.steps++;
        cur.samples += st[i].samples;
        mean_sum += st[i].mean_dbm;
        if(st[i].peak_dbm > cur.peak_dbm) {
            cur.peak_dbm = st[i].peak_dbm;
            cur.peak_hz = hz;
        }
    }

    /* ascending frequency: fine pass then walks the bands in order */
    for(size_t i = 1; i < count; i++) {
        RfScanCluster key = out[i];
        size_t j = i;
        while(j > 0u && out[j - 1u].lo_hz > key.lo_hz) {
            out[j] = out[j - 1u];
            j--;
        }
        out[j] = key;
    }
    return count;
}

/* ------------------------------------------------------------ fine pass */

bool rf_scan_fine_window(const RfScanCluster* c, RfScanWindow* w) {
    if(!c || !w || c->band >= RF_SCAN_BAND_COUNT) return false;
    const RfScanBand* b = &rf_scan_bands[c->band];
    bool wide = (c->hi_hz - c->lo_hz) > RF_SCAN_WIDE_CLUSTER_HZ;
    uint32_t lo_anchor = wide ? c->peak_hz : c->lo_hz;
    uint32_t hi_anchor = wide ? c->peak_hz : c->hi_hz;

    uint32_t lo = (lo_anchor > b->lo_hz + RF_SCAN_FINE_SPAN_HZ) ? lo_anchor - RF_SCAN_FINE_SPAN_HZ :
                                                                  b->lo_hz;
    uint32_t hi = (hi_anchor + RF_SCAN_FINE_SPAN_HZ < b->hi_hz) ? hi_anchor + RF_SCAN_FINE_SPAN_HZ :
                                                                  b->hi_hz;
    if(hi < lo) return false;

    uint32_t count = (hi - lo) / RF_SCAN_FINE_STEP_HZ + 1u;
    if(count > RF_SCAN_FINE_MAX_STEPS) {
        /* centre the capped window on the strongest coarse step */
        uint32_t half = (RF_SCAN_FINE_MAX_STEPS / 2u) * RF_SCAN_FINE_STEP_HZ;
        uint32_t span = (RF_SCAN_FINE_MAX_STEPS - 1u) * RF_SCAN_FINE_STEP_HZ;
        lo = (c->peak_hz > b->lo_hz + half) ? c->peak_hz - half : b->lo_hz;
        if(lo + span > b->hi_hz) lo = b->hi_hz - span;
        count = RF_SCAN_FINE_MAX_STEPS;
    }
    w->start_hz = lo;
    w->count = count;
    return true;
}

bool rf_scan_fine_analyze(
    const int16_t* peak_dbm,
    uint32_t n,
    uint32_t start_hz,
    uint32_t step_hz,
    RfScanFine* out) {
    if(!peak_dbm || !out || n == 0u) return false;
    uint32_t imax = 0u;
    for(uint32_t i = 1; i < n; i++) {
        if(peak_dbm[i] > peak_dbm[imax]) imax = i;
    }
    int16_t pmax = peak_dbm[imax];
    if(pmax <= RF_SCAN_NO_SIGNAL_DBM) return false;

    /* -3 dB contiguous lobe → power-weighted centroid */
    uint32_t l3 = imax, r3 = imax;
    while(l3 > 0u && peak_dbm[l3 - 1u] >= pmax - 3) l3--;
    while(r3 + 1u < n && peak_dbm[r3 + 1u] >= pmax - 3) r3++;
    /* float only: the FAP API exports powf but not pow, and the M4 FPU is
     * single precision. Indices are < RF_SCAN_FINE_MAX_STEPS, so no precision
     * concern. */
    float wsum = 0.0f, isum = 0.0f;
    for(uint32_t i = l3; i <= r3; i++) {
        float w = powf(10.0f, (float)(peak_dbm[i] - pmax) / 10.0f);
        wsum += w;
        isum += w * (float)i;
    }
    float ic = (wsum > 0.0f) ? isum / wsum : (float)imax;

    /* -6 dB contiguous lobe → occupied width estimate */
    uint32_t l6 = imax, r6 = imax;
    while(l6 > 0u && peak_dbm[l6 - 1u] >= pmax - 6) l6--;
    while(r6 + 1u < n && peak_dbm[r6 + 1u] >= pmax - 6) r6++;

    /* offset = ic*step < 128*20 kHz, exact enough in float */
    out->center_hz = start_hz + (uint32_t)(ic * (float)step_hz + 0.5f);
    out->width_hz = (r6 - l6 + 1u) * step_hz;
    out->peak_index = imax;
    out->peak_dbm = pmax;
    return true;
}

/* --------------------------------------------------------------- preset */

size_t rf_scan_clone_preset(
    const uint8_t* src,
    uint8_t* dst,
    size_t cap,
    uint8_t reg,
    uint8_t value) {
    if(!src || !dst || cap < 10u) return 0u;
    size_t i = 0u;
    bool patched = false;
    while(src[i] != 0u) {
        if(i + 2u + 10u > cap) return 0u; /* pair + terminator + PA table */
        dst[i] = src[i];
        dst[i + 1u] = (src[i] == reg) ? value : src[i + 1u];
        if(src[i] == reg) patched = true;
        i += 2u;
    }
    if(!patched) return 0u;
    dst[i] = 0u;
    dst[i + 1u] = 0u;
    memcpy(&dst[i + 2u], &src[i + 2u], 8u);
    return i + 10u;
}
