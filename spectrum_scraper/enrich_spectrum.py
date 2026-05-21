#!/usr/bin/env python3
"""Spectrum allocation enricher.

Loads a baseline CSV of frequency allocations (USA federal/non-federal +
ITU Region 1/2/3 + typical devices) and enriches each band with per-country
data fetched from external spectrum databases.

Sources used
------------
  1. Local baseline CSV               (USA + ITU services + typical devices)
  2. EFIS / CEPT SOAP web service     (per-country EU allocations, 48 countries)
       https://efis.cept.org/sitecontent.jsp?sitecontent=webservices
  3. FCC mirror                       (USA 47 CFR 2.106 table, manual download)
  4. NTIA chart                       (USA federal allocations, manual)

Output: a long-form CSV — one row per (freq_low_mhz, freq_high_mhz, country,
service, application, source) — easy to join, filter, and load into SQL or
pandas downstream.

Usage
-----
    python enrich_spectrum.py \\
        --baseline data/baseline_USA_EU_ITU.csv \\
        --out      output/spectrum_enriched.csv

Useful flags
------------
    --skip-efis        Do not call the EFIS web service.
    --countries A,B,C  Limit EFIS calls to these ISO-3166-1 alpha-3 codes.
    --freq-min MHz     Only enrich bands whose low edge >= this value.
    --freq-max MHz     Only enrich bands whose high edge <= this value.
    --cache-dir DIR    Cache directory for SOAP responses (default: cache/).
    --efis-endpoint U  Override the EFIS SOAP endpoint.

The EFIS endpoint URL changes from time to time. If the call fails, look it
up on https://efis.cept.org/sitecontent.jsp?sitecontent=webservices and pass
the new endpoint with --efis-endpoint.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import logging
import re
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Iterator
import ssl
from urllib import error as urlerror
from urllib import request as urlrequest

# Build an SSL context that works on macOS where Python doesn't have access to
# the system keychain. Prefer the certifi bundle; fall back to system defaults.
def _make_ssl_context() -> ssl.SSLContext:
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()

_SSL_CONTEXT = _make_ssl_context()
from xml.etree import ElementTree as ET

LOG = logging.getLogger("enrich_spectrum")

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

CEPT_COUNTRIES: tuple[str, ...] = (
    "ALB", "AND", "ARM", "AUT", "AZE", "BEL", "BIH", "BGR", "BLR",
    "CHE", "CYP", "CZE", "DEU", "DNK", "ESP", "EST", "FIN", "FRA",
    "GBR", "GEO", "GRC", "HRV", "HUN", "IRL", "ISL", "ITA", "LIE",
    "LTU", "LUX", "LVA", "MCO", "MDA", "MKD", "MLT", "MNE", "NLD",
    "NOR", "POL", "PRT", "ROU", "RUS", "SMR", "SRB", "SVK", "SVN",
    "SWE", "TUR", "UKR", "VAT",
)

DEFAULT_EFIS_ENDPOINT = "https://efis.cept.org/efisapi/Public/services/Public"
DEFAULT_EFIS_NS = "http://search.efis.cept.org/"

SOAP_TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<soap:Envelope xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/"
               xmlns:efi="{ns}">
  <soap:Body>
    <efi:getApplications>
      <countryCode>{country}</countryCode>
      <startFreq>{start_khz}</startFreq>
      <stopFreq>{stop_khz}</stopFreq>
    </efi:getApplications>
  </soap:Body>
</soap:Envelope>"""

USER_AGENT = "Mhz-Localiser-SpectrumEnricher/1.0 (+github.com/tfd-42/mhz_localiser)"

# Maps the baseline's "service columns" to a clean source label.
BASELINE_SERVICE_COLUMNS: tuple[tuple[str, str, str], ...] = (
    # (csv_column,                     source_label,                 region)
    ("itu_intl_region1_2_services",    "ITU Radio Regulations",      "Region 1/2"),
    ("itu_region3_services",           "ITU Radio Regulations",      "Region 3"),
    ("usa_federal_services",           "FCC 47 CFR 2.106 (Federal)", "USA"),
    ("usa_nonfederal_services",        "FCC 47 CFR 2.106 (Non-Fed)", "USA"),
)


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Band:
    """One frequency range from the baseline CSV, normalised to MHz."""
    low_mhz: float
    high_mhz: float
    raw_range: str
    raw_unit: str
    typical_devices: str = ""
    note: str = ""

    @property
    def low_khz(self) -> int:
        return int(round(self.low_mhz * 1000))

    @property
    def high_khz(self) -> int:
        return int(round(self.high_mhz * 1000))


@dataclass
class Row:
    """One enriched output row: a band × jurisdiction × service combination."""
    freq_low_mhz: float
    freq_high_mhz: float
    country: str            # ISO-3 code, "USA", "ITU", or specific CEPT code
    region: str             # "Region 1/2", "Region 3", "USA", "CEPT-XXX"
    service: str            # primary radio service (cleaned)
    status: str             # PRIMARY / Secondary / Permitted
    application: str = ""   # specific application (EFIS) or device class
    footnotes: str = ""
    typical_devices: str = ""
    source: str = ""
    note: str = ""


# ---------------------------------------------------------------------------
# Baseline loading
# ---------------------------------------------------------------------------

_RANGE_RE = re.compile(r"^\s*([\d.]+)\s*-\s*([\d.]+)\s*$")


def _to_mhz(value: float, unit: str) -> float:
    """Normalise a frequency value to MHz given its unit."""
    unit = unit.strip().lower()
    if unit == "khz":
        return value / 1_000.0
    if unit == "mhz" or unit == "":
        return value
    if unit == "ghz":
        return value * 1_000.0
    raise ValueError(f"Unknown frequency unit: {unit!r}")


def _parse_range(text: str, unit: str) -> tuple[float, float]:
    m = _RANGE_RE.match(text)
    if not m:
        raise ValueError(f"Cannot parse frequency range: {text!r}")
    lo, hi = float(m.group(1)), float(m.group(2))
    return _to_mhz(lo, unit), _to_mhz(hi, unit)


def load_baseline(path: Path) -> Iterator[tuple[Band, dict[str, str]]]:
    """Yield (band, raw_row) pairs from the baseline CSV."""
    with path.open(newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        for raw in reader:
            unit = raw.get("freq_unit", "MHz") or "MHz"
            # Prefer the pre-normalised MHz range when present; fall back to
            # the original-unit range. The baseline ships both.
            mhz_text = (raw.get("freq_range_mhz") or "").strip()
            try:
                if mhz_text:
                    lo, hi = _parse_range(mhz_text, "MHz")
                else:
                    lo, hi = _parse_range(raw["freq_range"], unit)
            except (KeyError, ValueError) as exc:
                LOG.warning("skipping unparseable row: %s (%s)", raw, exc)
                continue
            band = Band(
                low_mhz=lo,
                high_mhz=hi,
                raw_range=raw.get("freq_range", ""),
                raw_unit=unit,
                typical_devices=raw.get("typical_devices_applications", ""),
                note=raw.get("note", ""),
            )
            yield band, raw


# ---------------------------------------------------------------------------
# Service-string parsing
# ---------------------------------------------------------------------------

_FOOTNOTE_RE = re.compile(r"\b(?:US|G|NG|5\.)\w+\b")


def split_services(blob: str) -> list[tuple[str, str, str]]:
    """Split a "service column" cell into individual services.

    Returns a list of (service, status, footnotes). ITU/FCC convention is that
    UPPERCASE services are PRIMARY allocations and Mixed-case ones are
    Secondary. Footnote codes (e.g. "5.115", "US340", "G19") are extracted
    out of the service text.
    """
    if not blob or not blob.strip():
        return []
    parts = [p.strip() for p in blob.split("|")]
    out: list[tuple[str, str, str]] = []
    for part in parts:
        if not part:
            continue
        footnotes = " ".join(_FOOTNOTE_RE.findall(part))
        cleaned = _FOOTNOTE_RE.sub("", part).strip(" ,;")
        if not cleaned:
            continue
        # Heuristic: a service is "PRIMARY" when every letter token is upper-
        # case (allowing parenthesised qualifiers like "(R)" or "(active)").
        letters = re.sub(r"[^A-Za-z]", "", cleaned)
        status = "PRIMARY" if letters and letters.isupper() else "Secondary"
        out.append((cleaned, status, footnotes))
    return out


def expand_baseline_row(band: Band, raw: dict[str, str]) -> Iterator[Row]:
    """Expand one baseline row into one Row per (column × service)."""
    for col, source, region in BASELINE_SERVICE_COLUMNS:
        services = split_services(raw.get(col, ""))
        footnote_col = col.replace("_services", "_footnotes")
        extra_footnotes = (raw.get(footnote_col) or "").strip()
        country = "USA" if region == "USA" else "ITU"
        for service, status, fn in services:
            combined_fn = " ".join(f for f in (fn, extra_footnotes) if f)
            yield Row(
                freq_low_mhz=band.low_mhz,
                freq_high_mhz=band.high_mhz,
                country=country,
                region=region,
                service=service,
                status=status,
                footnotes=combined_fn,
                typical_devices=band.typical_devices,
                source=source,
                note=band.note,
            )


# ---------------------------------------------------------------------------
# EFIS SOAP client
# ---------------------------------------------------------------------------

class EFISClient:
    """Minimal SOAP client for the EFIS getApplications endpoint.

    The EFIS web services are documented at
    https://efis.cept.org/sitecontent.jsp?sitecontent=webservices

    The endpoint URL has moved a few times. If the call returns HTTP 404 or
    a SOAP fault, override it with --efis-endpoint.
    """

    def __init__(
        self,
        endpoint: str = DEFAULT_EFIS_ENDPOINT,
        namespace: str = DEFAULT_EFIS_NS,
        cache_dir: Path | None = None,
        timeout: int = 30,
        max_retries: int = 4,
    ) -> None:
        self.endpoint = endpoint
        self.namespace = namespace
        self.cache_dir = cache_dir
        self.timeout = timeout
        self.max_retries = max_retries
        if cache_dir is not None:
            cache_dir.mkdir(parents=True, exist_ok=True)

    def _cache_path(self, country: str, lo_khz: int, hi_khz: int) -> Path | None:
        if self.cache_dir is None:
            return None
        key = f"{self.endpoint}|{country}|{lo_khz}|{hi_khz}".encode()
        digest = hashlib.sha1(key).hexdigest()[:16]
        return self.cache_dir / f"efis-{country}-{lo_khz}-{hi_khz}-{digest}.xml"

    def fetch(self, country: str, lo_khz: int, hi_khz: int) -> str | None:
        cache_path = self._cache_path(country, lo_khz, hi_khz)
        if cache_path is not None and cache_path.exists():
            return cache_path.read_text(encoding="utf-8")

        body = SOAP_TEMPLATE.format(
            ns=self.namespace,
            country=country,
            start_khz=lo_khz,
            stop_khz=hi_khz,
        ).encode("utf-8")
        req = urlrequest.Request(
            self.endpoint,
            data=body,
            headers={
                "Content-Type": "text/xml; charset=utf-8",
                "SOAPAction": '"getApplications"',
                "User-Agent": USER_AGENT,
                "Accept": "text/xml, application/xml",
            },
        )

        backoff = 2.0
        for attempt in range(1, self.max_retries + 1):
            try:
                with urlrequest.urlopen(req, timeout=self.timeout, context=_SSL_CONTEXT) as resp:
                    raw = resp.read().decode("utf-8", errors="replace")
            except urlerror.HTTPError as exc:
                # 4xx is final — endpoint or country probably wrong.
                if 400 <= exc.code < 500:
                    LOG.error("EFIS HTTP %s for %s [%d-%d kHz]",
                              exc.code, country, lo_khz, hi_khz)
                    return None
                LOG.warning("EFIS HTTP %s (attempt %d/%d) for %s",
                            exc.code, attempt, self.max_retries, country)
            except (urlerror.URLError, TimeoutError) as exc:
                LOG.warning("EFIS network error (attempt %d/%d) for %s: %s",
                            attempt, self.max_retries, country, exc)
            else:
                if cache_path is not None:
                    cache_path.write_text(raw, encoding="utf-8")
                return raw
            time.sleep(backoff)
            backoff *= 2
        LOG.error("EFIS failed permanently for %s [%d-%d kHz]",
                  country, lo_khz, hi_khz)
        return None

    @staticmethod
    def parse(raw_xml: str) -> list[dict[str, str]]:
        """Extract application entries from an EFIS SOAP response."""
        try:
            root = ET.fromstring(raw_xml)
        except ET.ParseError as exc:
            LOG.warning("EFIS XML parse error: %s", exc)
            return []
        # The EFIS schema wraps each entry in <application>…</application> with
        # leaf children whose tag names map to friendly fields. We strip the
        # namespace and look for the canonical leaf names.
        out: list[dict[str, str]] = []
        for app in root.iter():
            tag = app.tag.split("}")[-1].lower()
            if tag != "application":
                continue
            entry: dict[str, str] = {}
            for child in app.iter():
                ctag = child.tag.split("}")[-1].lower()
                if ctag == "application" or child.text is None:
                    continue
                entry[ctag] = child.text.strip()
            if entry:
                out.append(entry)
        return out


def efis_rows_for_band(
    client: EFISClient, band: Band, countries: Iterable[str]
) -> Iterator[Row]:
    for country in countries:
        raw = client.fetch(country, band.low_khz, band.high_khz)
        if raw is None:
            continue
        for entry in EFISClient.parse(raw):
            service = (entry.get("servicename") or entry.get("service")
                       or entry.get("radioservice") or "")
            application = (entry.get("applicationname") or entry.get("application")
                           or entry.get("applicationdescription") or "")
            status = entry.get("status") or ""
            regulation = (entry.get("regulation") or entry.get("eccdecision")
                          or entry.get("note") or "")
            if not service and not application:
                continue
            yield Row(
                freq_low_mhz=band.low_mhz,
                freq_high_mhz=band.high_mhz,
                country=country,
                region=f"CEPT-{country}",
                service=service,
                status=status,
                application=application,
                footnotes=regulation,
                typical_devices=band.typical_devices,
                source="EFIS (CEPT)",
                note=band.note,
            )


# ---------------------------------------------------------------------------
# Pipeline
# ---------------------------------------------------------------------------

FIELDNAMES = (
    "freq_low_mhz",
    "freq_high_mhz",
    "country",
    "region",
    "service",
    "status",
    "application",
    "footnotes",
    "typical_devices",
    "source",
    "note",
)


def write_rows(path: Path, rows: Iterable[Row]) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDNAMES)
        writer.writeheader()
        for row in rows:
            writer.writerow({
                "freq_low_mhz":     f"{row.freq_low_mhz:.6f}",
                "freq_high_mhz":    f"{row.freq_high_mhz:.6f}",
                "country":          row.country,
                "region":           row.region,
                "service":          row.service,
                "status":           row.status,
                "application":      row.application,
                "footnotes":        row.footnotes,
                "typical_devices":  row.typical_devices,
                "source":           row.source,
                "note":             row.note,
            })
            count += 1
    return count


def run(args: argparse.Namespace) -> int:
    baseline = Path(args.baseline)
    if not baseline.exists():
        LOG.error("baseline file not found: %s", baseline)
        return 1

    countries: list[str]
    if args.countries:
        countries = [c.strip().upper() for c in args.countries.split(",") if c.strip()]
    else:
        countries = list(CEPT_COUNTRIES)

    client: EFISClient | None = None
    if not args.skip_efis:
        client = EFISClient(
            endpoint=args.efis_endpoint,
            cache_dir=Path(args.cache_dir),
        )

    def all_rows() -> Iterator[Row]:
        for band, raw in load_baseline(baseline):
            if args.freq_min is not None and band.high_mhz < args.freq_min:
                continue
            if args.freq_max is not None and band.low_mhz > args.freq_max:
                continue
            yield from expand_baseline_row(band, raw)
            if client is not None:
                yield from efis_rows_for_band(client, band, countries)

    out = Path(args.out)
    n = write_rows(out, all_rows())
    LOG.info("wrote %d rows to %s", n, out)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--baseline", default="data/baseline_USA_EU_ITU.csv",
                        help="Path to the baseline CSV.")
    parser.add_argument("--out", default="output/spectrum_enriched.csv",
                        help="Output CSV path.")
    parser.add_argument("--skip-efis", action="store_true",
                        help="Skip the EFIS SOAP calls (offline mode).")
    parser.add_argument("--countries", default="",
                        help="Comma-separated ISO-3 country codes to query.")
    parser.add_argument("--freq-min", type=float, default=None,
                        help="Only enrich bands above this MHz value.")
    parser.add_argument("--freq-max", type=float, default=None,
                        help="Only enrich bands below this MHz value.")
    parser.add_argument("--cache-dir", default="cache",
                        help="Directory for cached SOAP responses.")
    parser.add_argument("--efis-endpoint", default=DEFAULT_EFIS_ENDPOINT,
                        help="EFIS SOAP endpoint URL.")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Enable debug logging.")
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
