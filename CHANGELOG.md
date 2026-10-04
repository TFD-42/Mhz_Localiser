# Changelog

All notable changes to Mhz_Localiser are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow semantic-ish versioning tied to the FAP/APK version.

## [3.0] - 2026-10-04

### Changed
- **FAP — rolling scan rewritten as a two-pass scan over the CC1101 bands only**
  (300–348, 387–464, 779–928 MHz). The v2 sweep stepped through the 348–387 and
  464–779 MHz holes: `subghz_retune()` rejected those frequencies, the return value was
  ignored, and the radio kept measuring the previous frequency while the stream labelled
  it with the hole frequency (352 wasted steps × 2 s, and wrong data).
  - Coarse: 600 kHz steps (< 650 kHz RX filter → contiguous coverage), 100 ms peak-hold.
  - Fine: 58 kHz filter, 20 kHz steps, ±300 kHz per cluster → centre + −6 dB width.
  - Chip reset + preset load once per pass instead of on every step.
  - Threshold `max(−93 dBm, per-band median floor + 10 dB)` on the peak, not the mean.
  - Coarse pass ≈ 48 s computed (461 points × ~103 ms) instead of > 20 min; plus
    ~1.3 s per detected cluster for the fine pass. Not yet timed on hardware.
- **Protocol v3** (backward compatible): `ROLL` gains `pass,floor_dbm,width_hz,cluster,seq`;
  `rssi_min` renamed `rssi_peak` (same value: strongest sample); new
  `# RF_LOGGER_RSCAN pos|floor|end|err|warn` event lines.
- **Android Roll Scan tab**: pass-aware progress, fine results supersede coarse ones,
  lost-line counter, Peak/Width columns, DOM built with `textContent`.

### Added
- FAP scan-results screen: OK starts continuous RSSI on the selected hit.
- `flipper/rf_scan.{c,h}`: hardware-free scan logic; host tests in `flipper/tests`
  (ASan/UBSan) and `android/tests/roll_ui.test.js` (jsdom), both wired into CI.

### Fixed
- Possible `furi_crash("SubGhz: Incorrect frequency during set.")` for requests on the
  firmware's lower window bounds: they pass `is_frequency_valid()` but quantize below
  the window. Every tune is checked against the CC1101 quantization first.
- USB CDC writes longer than 64 bytes (e.g. the roll header) overran the endpoint packet
  buffer; writes are now chunked with TX-complete flow control.
- Roll Scan **Tune** button threw `ReferenceError: state is not defined` (handler lived
  outside the main closure) — now goes through `window.rfSetTargetFreq`.
- SD logging flag stayed `on` after stopping; rolling-scan SD files used the running-mode
  header; Back during a rolling scan left the SD file open.
- Removed DB entries the CC1101 cannot tune (169.40 Sigfox, 380.5 Polycom UL,
  466.025 POCSAG); out-of-band manual values are flagged on screen and blink red on OK.

## [2.2] - 2026-10-01

### Added
- **FAP — Frequency DB browser** (`StateDbList`): press **→** on the last digit of the
  manual screen to open a scrollable list of 35 pre-loaded frequencies.
  - ISM/SRD, LoRa 868, TPMS/RKE 315/433, PMR446, aviation (VHF/UHF), radiosonde, Polycom CH, GSM-R SBB.
  - Dedicated **CH (Switzerland)** section: Polycom TETRAPOL 380/390 MHz, GSM-R SBB 877/922 MHz,
    BAKOM SRD 433/868/869 MHz.
  - Select entry → loaded directly into manual screen; top sentinel entry starts rolling scan.
- **FAP — Rolling spectrum scan** (`StateRolling`): sweeps 300–928 MHz in 1 MHz steps.
  - 2 s base dwell per step; extended to 3 s for MED signals (−100 to −80 dBm).
  - RSSI averaged over 10 samples; classifies as SKIP / MED / STRONG.
  - Streams `# RF_LOGGER_ROLL` header + `ROLL,ts_ms,freq_hz,rssi_avg,rssi_min,samples,flag` lines.
- **Android — Roll Scan tab** (3rd tab): receives rolling scan stream, displays sortable table
  of detected frequencies with Avg RSSI, Min RSSI, samples, signal class.
  - Filter buttons: MED+ / All / STRONG only.
  - **Tune** button on each row loads that frequency into the Triangulator tab.
- **spectrum.csv +487 rows** (2,480 → 2,967 total):
  - **59 CH/OFCOM entries** (280–1100 MHz): Polycom TETRAPOL, GSM-R SBB, LoRa, SRD,
    Armée Suisse military bands, GNSS, ADS-B/SSR, DME Skyguide, TPMS.
  - 437 additional FR/DE/UK/IT/ES/CN/RU/US/ITU entries filling coverage gaps.

### Changed
- README updated for v2.2: new What's new section, updated file tree, app flow,
  Roll Scan section, and ROLL protocol documentation.
- spectrum.csv: 2,480 → 2,967 rows.

---

## [2.1] - 2026-08-13

### Added
- **Bluetooth LE transport.** The Flipper FAP now streams the same CSV telemetry over
  **both USB CDC and Bluetooth LE** simultaneously. The Android app can connect over either —
  fully wireless, no cable required.
- **Dual-transport picker** in the app drawer (USB / Bluetooth); the solver, map, and
  allocation lookup behave identically on either transport.
- **BLE device picker** that lists nearby Flippers (matched by serial-service UUID and by
  name, so custom-named units without the "Flipper" prefix are found). The chosen device
  address is remembered for one-tap reconnect.
- **On-device Bluetooth status** on the Flipper running screen: `BT:off` / `BT:adv` / `BT:ok`.
- **RSSI smoothing** — exponential moving-average filter on the live stream to damp
  single-sample multipath spikes before capture.
- **Selectable environment / path-loss exponent `n`** (open field / suburban / urban /
  dense / indoor, or a custom value) in the drawer.
- **Automatic outlier rejection** — leave-one-out residual check excludes likely-multipath
  captures before the final solve.
- **Live capture-geometry hint** warning when captures are too collinear for a good fix.
- **Session persistence** — captures and settings survive an app restart.
- Live allocation sync driven by the Flipper stream, auto-detected frequency, and a
  display-only allocation view.

### Fixed
- BLE reliability: force the LE transport during pairing, self-heal advertising after a
  stack restart, refresh Android's stale GATT service cache before discovery, and bind the
  correct data characteristic (TX / indicate rather than the flow-control notify char).
- Tab navigation: the live stream no longer forces the app back to the Allocation tab,
  so the Triangulator view stays put while data streams.

### Changed
- `application.fam` now requires the `bt` service; FAP version bumped to 2.1.
- README, protocol docs, and requirements updated for dual-transport operation.

## [2.0]

### Changed
- Flipper FAP is **manual-frequency only** — the preset menu (315 / 433 / 868 / 915 MHz)
  was removed and the app opens directly on the digit editor for faster startup.
- Smaller FAP binary (preset table and menu renderer eliminated).
- Android plugin auto-reconnects over USB if the link drops.
- Enriched README; tightened `.gitignore`.

## [1.0]

### Added
- Initial release: Sub-GHz RSSI logger FAP streaming CSV over USB CDC.
- Android triangulation app: live readout, GPS + RSSI capture, Nelder–Mead least-squares
  solver with RMS error, Leaflet map.
- Offline spectrum allocation lookup (~2,450 rows: ITU R1/R2/R3, USA federal + non-federal,
  per-country EU) bundled as `spectrum.csv`, with the Python `spectrum_scraper` tooling.

[2.2]: https://github.com/TFD-42/Mhz_Localiser/releases
[2.1]: https://github.com/TFD-42/Mhz_Localiser/releases
[2.0]: https://github.com/TFD-42/Mhz_Localiser/releases
[1.0]: https://github.com/TFD-42/Mhz_Localiser/releases
