# Changelog

All notable changes to Mhz_Localiser are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow semantic-ish versioning tied to the FAP/APK version.

## [Unreleased] - 2026-09-29

### Added
- **spectrum.csv enriched (+31 rows)** — new entries from `rf_reference.db` covering:
  - Switzerland (OFCOM CH / BAKOM) allocations from Swiss NFAP 2026 (1 Jan 2026 edition):
    433 MHz ISM (3 sub-profiles: 10 mW <10% DC, 1 mW 100% DC, 500 mW ≤1% DC),
    868 MHz SRD bands (868.0–868.6 MHz 25 mW; 869.4–869.65 MHz 500 mW),
    Wi-Fi 2.4 / 5 / 6E (5945–6425 MHz via RIR1010-11 / ECC/DEC/(20)01).
  - 300–390 MHz full coverage for CH/EU:
    312–315 MHz mobile MIL, 315–322 MHz mobile MIL,
    322–328.6 MHz radio astronomy + MIL, **328.6–335.4 MHz ILS glide slope** (ICAO Annex 10),
    335.4–380 MHz MIL+UWB, **380–385 MHz TETRA uplink** (emergency services ERC/DEC/(01)19),
    385–387 MHz PPDR tuning range, 387–390 MHz MIL+UWB.
  - **7 RF anomaly/attack detection signatures** (tagged `[ANOMALY]`) with per-metric
    alert thresholds: Wi-Fi broadband jamming, spot jamming, BLE advertising flood,
    BLE/2.4 GHz jamming, rogue AP proximity indicator, deauth flood (RF+protocol),
    unregistered strong signal.
- **`rf_reference.db`** — full SQLite reference database added to
  `spectrum_scraper/data/` with tables `products`, `rf_profiles`, `sources`,
  `regulatory_notes`, `research_log`, `anomaly_signatures`, `anomaly_thresholds`.
  Sources: Great Scott Gadgets official docs, IEEE 802.11-2020, Bluetooth SIG Core Spec 5.4,
  ETSI EN 300 220 / 300 328 / 301 893, ECC/CEPT decisions, FCC CFR 47, Swiss NFAP 2026,
  ETSI TS 100 392-15 (TETRA), ICAO Annex 10.

### Changed
- spectrum.csv: 2449 → 2480 rows (31 new, zero duplicates removed).

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

[2.1]: https://github.com/TFD-42/Mhz_Localiser/releases
[2.0]: https://github.com/TFD-42/Mhz_Localiser/releases
[1.0]: https://github.com/TFD-42/Mhz_Localiser/releases
