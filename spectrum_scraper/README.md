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

# Offline run: baseline only, no network calls
python enrich_spectrum.py --skip-efis \
    --baseline data/baseline_USA_EU_ITU.csv \
    --out      output/spectrum_baseline_longform.csv

# Full run: enrich every band with EFIS data for all 48 CEPT countries
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
