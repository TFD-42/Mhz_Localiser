#!/usr/bin/env python3
"""Interactive launcher for the spectrum allocation table.

Two modes:
    [1] List all allocations, paginated, with optional filters.
    [2] Look up a frequency (MHz, with unit/range parsing).

The launcher reads a long-form CSV produced by ``enrich_spectrum.py``. It
prefers ``output/spectrum_enriched.csv`` (full EFIS run) and falls back to
``output/spectrum_baseline_longform.csv`` (offline baseline). If neither
exists, it generates the offline baseline on first run.

Run:
    python launcher.py
"""

from __future__ import annotations

import csv
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENRICHED = HERE / "output" / "spectrum_enriched.csv"
BASELINE_LF = HERE / "output" / "spectrum_baseline_longform.csv"
BASELINE_CSV = HERE / "data" / "baseline_USA_EU_ITU.csv"
ENRICH_SCRIPT = HERE / "enrich_spectrum.py"

PAGE_SIZE = 20


# ---------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------

@dataclass
class Allocation:
    freq_low_mhz: float
    freq_high_mhz: float
    country: str
    region: str
    service: str
    status: str
    application: str
    footnotes: str
    typical_devices: str
    source: str
    note: str

    def contains(self, mhz: float) -> bool:
        return self.freq_low_mhz <= mhz <= self.freq_high_mhz

    def overlaps(self, lo: float, hi: float) -> bool:
        return not (self.freq_high_mhz < lo or self.freq_low_mhz > hi)


def load_allocations(path: Path) -> list[Allocation]:
    rows: list[Allocation] = []
    with path.open(newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        for r in reader:
            try:
                rows.append(Allocation(
                    freq_low_mhz=float(r["freq_low_mhz"]),
                    freq_high_mhz=float(r["freq_high_mhz"]),
                    country=r.get("country", ""),
                    region=r.get("region", ""),
                    service=r.get("service", ""),
                    status=r.get("status", ""),
                    application=r.get("application", ""),
                    footnotes=r.get("footnotes", ""),
                    typical_devices=r.get("typical_devices", ""),
                    source=r.get("source", ""),
                    note=r.get("note", ""),
                ))
            except (KeyError, ValueError):
                continue
    rows.sort(key=lambda a: (a.freq_low_mhz, a.country, a.service))
    return rows


def ensure_data() -> Path:
    """Pick the best available long-form CSV; bootstrap one if missing."""
    if ENRICHED.exists():
        return ENRICHED
    if BASELINE_LF.exists():
        return BASELINE_LF
    print("[launcher] No long-form CSV found — generating offline baseline...")
    if not BASELINE_CSV.exists():
        sys.exit(f"[launcher] Missing baseline data: {BASELINE_CSV}")
    result = subprocess.run(
        [sys.executable, str(ENRICH_SCRIPT),
         "--skip-efis",
         "--baseline", str(BASELINE_CSV),
         "--out", str(BASELINE_LF)],
        cwd=HERE,
    )
    if result.returncode != 0:
        sys.exit("[launcher] Failed to generate baseline long-form CSV.")
    return BASELINE_LF


# ---------------------------------------------------------------------------
# MHz input parsing
# ---------------------------------------------------------------------------

_UNIT_RE = re.compile(r"\s*(k|m|g)?hz\s*$", re.IGNORECASE)


def _to_mhz(value: float, unit: str) -> float:
    unit = unit.lower()
    if unit in ("", "m"):
        return value
    if unit == "k":
        return value / 1_000.0
    if unit == "g":
        return value * 1_000.0
    raise ValueError(f"unknown unit: {unit}")


def parse_freq(text: str) -> tuple[float, float]:
    """Parse a frequency input. Returns (lo_mhz, hi_mhz).

    Accepted:
        "433.92"            -> (433.92, 433.92)
        "433.92 MHz"        -> (433.92, 433.92)
        "2.4GHz"            -> (2400.0, 2400.0)
        "433.92-434.79 MHz" -> (433.92, 434.79)
        "868 kHz"           -> (0.868, 0.868)
    """
    s = text.strip().lower()
    if not s:
        raise ValueError("empty input")
    m = _UNIT_RE.search(s)
    unit = ""
    if m:
        unit = m.group(1) or "m"
        s = s[: m.start()].strip()
    if "-" in s:
        lo_s, hi_s = s.split("-", 1)
        lo = _to_mhz(float(lo_s), unit)
        hi = _to_mhz(float(hi_s), unit)
        if hi < lo:
            lo, hi = hi, lo
        return lo, hi
    v = _to_mhz(float(s), unit)
    return v, v


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

COLS = (
    ("freq",        18),  # "low-high MHz"
    ("country",      8),
    ("region",      12),
    ("service",     32),
    ("status",      10),
    ("application", 24),
    ("source",      28),
)


def _truncate(s: str, n: int) -> str:
    s = (s or "").replace("\n", " ").strip()
    return s if len(s) <= n else s[: n - 1] + "…"


def _format_freq(a: Allocation) -> str:
    return f"{a.freq_low_mhz:.4f}-{a.freq_high_mhz:.4f}"


def render_header() -> str:
    headers = {
        "freq": "MHz range",
        "country": "country",
        "region": "region",
        "service": "service",
        "status": "status",
        "application": "application",
        "source": "source",
    }
    line = "  ".join(_truncate(headers[c], w).ljust(w) for c, w in COLS)
    sep = "  ".join("-" * w for _, w in COLS)
    return line + "\n" + sep


def render_row(a: Allocation) -> str:
    cells = {
        "freq": _format_freq(a),
        "country": a.country,
        "region": a.region,
        "service": a.service,
        "status": a.status,
        "application": a.application or a.typical_devices.split(";")[0].strip(),
        "source": a.source,
    }
    return "  ".join(_truncate(cells[c], w).ljust(w) for c, w in COLS)


def render_detail(a: Allocation) -> str:
    out = []
    out.append(f"  Band       : {_format_freq(a)} MHz")
    out.append(f"  Country    : {a.country}  ({a.region})")
    out.append(f"  Service    : {a.service}  [{a.status}]")
    if a.application:
        out.append(f"  Application: {a.application}")
    if a.footnotes:
        out.append(f"  Footnotes  : {a.footnotes}")
    if a.typical_devices:
        out.append(f"  Devices    : {a.typical_devices}")
    if a.source:
        out.append(f"  Source     : {a.source}")
    if a.note:
        out.append(f"  Note       : {a.note}")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Mode 1 — listing
# ---------------------------------------------------------------------------

def pick_from_list(label: str, options: list[str]) -> str:
    """Show a numbered list of options and let the user pick one (or 0 = all).

    Returns the chosen value, or empty string if the user picked 0/Enter/q.
    """
    if not options:
        return ""
    print(f"\n  Filter {label}:")
    print("    [0] (all)")
    cols = 3
    width = max(len(o) for o in options) + 4
    for i, opt in enumerate(options, 1):
        end = "\n" if i % cols == 0 else ""
        print(f"    [{i:>2}] {opt.ljust(width)}", end=end)
    if len(options) % cols != 0:
        print()
    while True:
        raw = input(f"  Pick {label} [0-{len(options)}] (Enter = all) : ").strip().lower()
        if raw in ("", "0", "q"):
            return ""
        if raw.isdigit():
            idx = int(raw)
            if 1 <= idx <= len(options):
                return options[idx - 1]
        # also accept typed value (case-insensitive prefix match)
        matches = [o for o in options if o.lower().startswith(raw)]
        if len(matches) == 1:
            return matches[0]
        print(f"  ! Invalid choice. Type a number 0-{len(options)} or a unique prefix.")


def mode_list(rows: list[Allocation]) -> None:
    print("\n=== List all allocations ===")
    countries = sorted({a.country for a in rows if a.country})
    regions = sorted({a.region for a in rows if a.region})
    country = pick_from_list("country", countries)
    region = pick_from_list("region", regions)
    source = input("  Filter source (blank = all, substring match) : ").strip().lower()

    filtered = [
        a for a in rows
        if (not country or a.country == country)
        and (not region or a.region == region)
        and (not source or source in a.source.lower())
    ]
    print(f"\n  {len(filtered)} rows match.\n")
    if not filtered:
        return

    print(render_header())
    for i in range(0, len(filtered), PAGE_SIZE):
        for a in filtered[i: i + PAGE_SIZE]:
            print(render_row(a))
        if i + PAGE_SIZE >= len(filtered):
            break
        cmd = input(f"\n  -- {i + PAGE_SIZE}/{len(filtered)} -- "
                    "[Enter] more, [q] quit : ").strip().lower()
        if cmd == "q":
            return
        print(render_header())


# ---------------------------------------------------------------------------
# Mode 2 — MHz lookup
# ---------------------------------------------------------------------------

def mode_lookup(rows: list[Allocation]) -> None:
    print("\n=== Look up by frequency ===")
    print("  Examples: 433.92   |   2.4 GHz   |   88-108 MHz   |   868 kHz")
    text = input("  MHz value (or range)                : ").strip()
    if not text:
        return
    try:
        lo, hi = parse_freq(text)
    except ValueError as e:
        print(f"  ! Could not parse: {e}")
        return
    countries = sorted({a.country for a in rows if a.country})
    country = pick_from_list("country", countries)

    if lo == hi:
        matching = [a for a in rows if a.contains(lo)]
        title = f"{lo:.4f} MHz"
    else:
        matching = [a for a in rows if a.overlaps(lo, hi)]
        title = f"{lo:.4f}-{hi:.4f} MHz"
    if country:
        matching = [a for a in matching if a.country == country]

    print(f"\n  {len(matching)} allocations matching {title}"
          + (f" in {country}" if country else "") + ".\n")
    if not matching:
        # Suggest the nearest covered bands so users see they're in a gap.
        center = (lo + hi) / 2.0
        pool = [a for a in rows if not country or a.country == country]
        below = [a for a in pool if a.freq_high_mhz < lo]
        above = [a for a in pool if a.freq_low_mhz > hi]
        nearest_below = max(below, key=lambda a: a.freq_high_mhz) if below else None
        nearest_above = min(above, key=lambda a: a.freq_low_mhz) if above else None
        if nearest_below or nearest_above:
            print("  No allocation covers this frequency — closest bands:")
            if nearest_below:
                gap = lo - nearest_below.freq_high_mhz
                print(f"    ↓ {nearest_below.freq_low_mhz:.4f}-{nearest_below.freq_high_mhz:.4f} MHz  "
                      f"({nearest_below.country} {nearest_below.service})  "
                      f"— {gap:.4f} MHz below")
            if nearest_above:
                gap = nearest_above.freq_low_mhz - hi
                print(f"    ↑ {nearest_above.freq_low_mhz:.4f}-{nearest_above.freq_high_mhz:.4f} MHz  "
                      f"({nearest_above.country} {nearest_above.service})  "
                      f"— {gap:.4f} MHz above")
            print("  (Gaps usually mean military/government use or unallocated spectrum.)")
        return

    # Group by country for readability.
    by_country: dict[str, list[Allocation]] = {}
    for a in matching:
        by_country.setdefault(a.country, []).append(a)

    for c, group in sorted(by_country.items()):
        print(f"--- {c} ({len(group)}) " + "-" * (60 - len(c)))
        for a in group:
            print(render_detail(a))
            print()


# ---------------------------------------------------------------------------
# Main menu
# ---------------------------------------------------------------------------

def main() -> int:
    data_path = ensure_data()
    rows = load_allocations(data_path)
    print(f"\n=== Mhz_Localiser — Spectrum Lookup ===")
    print(f"  loaded {len(rows)} allocations from {data_path.relative_to(HERE)}")

    while True:
        print("\n  [1] List all allocations")
        print("  [2] Look up by MHz")
        print("  [q] Quit")
        try:
            choice = input("  > ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if choice == "1":
            mode_list(rows)
        elif choice == "2":
            mode_lookup(rows)
        elif choice in ("q", "quit", "exit"):
            return 0
        else:
            print("  ! Unknown choice.")


if __name__ == "__main__":
    sys.exit(main())
