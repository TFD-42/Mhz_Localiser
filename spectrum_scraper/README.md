# Spectrum scraper

Enriches a baseline CSV of frequency allocations (USA federal/non-federal,
ITU Region 1/2/3, typical devices) with per-country data fetched from public
spectrum databases.

## Sources

| Source | Coverage | Access | Used by |
|--------|----------|--------|---------|
| Baseline CSV (`data/baseline_USA_EU_ITU.csv`) | USA + ITU + typical devices | Local file shipped with the repo | Always |
| [EFIS — CEPT](https://efis.cept.org) | 48 European countries | Public SOAP web service | `enrich_spectrum.py` (default) |
| [FCC Online Table](https://www.fcc.gov/oet/spectrum/table/fcctable.pdf) | USA | Public PDF / CSV | Manual import |
| [NTIA Allocation Chart](https://www.ntia.gov/page/united-states-frequency-allocation-chart) | USA federal | Public PDF | Manual import |
| [PerCon DataLinks](https://datalinks.perconcorp.com) | Worldwide | Commercial | Out of scope |

## Quick start

```bash
cd spectrum_scraper

# Interactive launcher: [1] list, [2] MHz lookup
python launcher.py

# Offline build of the long-form CSV (baseline only, no network)
python enrich_spectrum.py --skip-efis \
    --baseline data/baseline_USA_EU_ITU.csv \
    --out      output/spectrum_baseline_longform.csv

# Full enrichment: every band x 48 CEPT countries via EFIS
python enrich_spectrum.py \
    --baseline data/baseline_USA_EU_ITU.csv \
    --out      output/spectrum_enriched.csv

# Restrict to a few countries and the Sub-GHz hobby band
python enrich_spectrum.py \
    --countries FRA,DEU,GBR,ESP,ITA \
    --freq-min 300 --freq-max 1000 \
    --out output/spectrum_subghz.csv
```

No third-party dependencies — pure `stdlib` (`urllib`, `csv`, `xml.etree`).

## Interactive launcher

`launcher.py` reads the long-form CSV and offers two modes:

- **[1] List all allocations** — paginated table, filterable by country,
  region, and source substring.
- **[2] Look up by MHz** — accepts bare numbers (`433.92`), explicit units
  (`433.92 MHz`, `2.4 GHz`, `868 kHz`), and ranges (`88-108 MHz`). Optional
  country filter narrows the result.

On first run the launcher bootstraps `output/spectrum_baseline_longform.csv`
from the baseline (offline). Once you've run `enrich_spectrum.py` to produce
`output/spectrum_enriched.csv`, the launcher prefers that file automatically.

> **Note on baseline coverage** — the shipped baseline CSV is sparse: it
> covers VLF/LF (kHz units) and SHF (>3 GHz) but has gaps in MF/HF/VHF/UHF.
> For dense lookups in the 0.5-3000 MHz range, run the full EFIS enrichment.

## Output schema

Long-form CSV, one row per (band × jurisdiction × service):

| Column | Description |
|--------|-------------|
| `freq_low_mhz`, `freq_high_mhz` | Band edges in MHz (always populated) |
| `country` | `USA`, `ITU`, or ISO-3 CEPT country code (e.g. `FRA`) |
| `region` | `USA`, `Region 1/2`, `Region 3`, or `CEPT-XXX` |
| `service` | Primary radio service name |
| `status` | `PRIMARY` or `Secondary` (ITU/FCC convention) |
| `application` | Specific application (EFIS only) |
| `footnotes` | Footnote codes (e.g. `5.115 US340`) |
| `typical_devices` | Typical user devices for this band (from baseline) |
| `source` | Origin of the row (`FCC 47 CFR 2.106`, `EFIS (CEPT)`, etc.) |
| `note` | Free-text annotation |

## Notes

- **EFIS endpoint** — The CEPT SOAP endpoint URL changes occasionally. If you
  see HTTP 404 errors, look it up on the
  [EFIS web services page](https://efis.cept.org/sitecontent.jsp?sitecontent=webservices)
  and pass the new URL with `--efis-endpoint`.
- **Caching** — All SOAP responses are cached in `cache/` keyed by
  `(country, freq range)`. Re-runs are free; delete the directory to force a
  refetch.
- **Rate limiting** — The script issues one request per (country × band).
  For the default ~500 bands × 48 countries that is ~24 k requests; spread
  the run across sessions or restrict with `--countries` / `--freq-min/max`.

## Integration with Mhz_Localiser

The enriched CSV is the lookup table used by the RF Triangulator UI: once a
band is identified on the Flipper, the app can show "what is this frequency
allocated to, in this country?" with one CSV lookup.
