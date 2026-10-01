# Promotion Report — Mhz_Localiser v2.2

_Generated 2026-10-01 · Audit via `repo_audit.py` · Evidence-based throughout_

---

## 0. Secrets scan

**Result: CLEAN.** 30 files scanned. No sensitive filenames, no credential patterns found.

---

## 1. Attribution

**Finding: Insufficient — no attribution section drafted.**

Checked: git remotes (single `origin`, no `upstream`), all 27 commit messages (no import/fork/vendor language), `LICENSE` copyright (`Copyright (c) 2026 Mhz_Localise contributors`), `github.fork = false`. No evidence of reused code from another project. The project is original work by TFD-42.

---

## 2. Production-readiness checklist

| Item | Status | Notes |
|------|--------|-------|
| README: value prop above fold | ✅ | Clear one-liner, limitations honestly placed after features |
| README: quickstart | ✅ | Copy-pasteable install + ADB commands |
| README: table of contents | ✅ | Inline · links at top |
| README: screenshots / UI images | ✅ | 3 app screenshots embedded |
| README: limitations section | ✅ | Detailed, with field validation table |
| Badge row | ✅ | 8 badges, all grounded in confirmed facts |
| LICENSE (MIT) | ✅ | Present and correct |
| CONTRIBUTING.md | ✅ | Present |
| CODE_OF_CONDUCT.md | ✅ | Present |
| SECURITY.md | ✅ | Present |
| CHANGELOG.md | ✅ | Keep-a-Changelog format, now up to v2.2 |
| .github/ISSUE_TEMPLATE | ✅ | 2 templates (bug + feature) |
| .github/PULL_REQUEST_TEMPLATE.md | ✅ | Present |
| CI workflow | ✅ | `ci.yml` present, badge displayed |
| Dependency pinning | ⚠️ | `npm install` uses semver ranges; no `package-lock.json` in repo. Low risk for a build-only dependency set, but `npm ci` + committed lockfile would make builds fully reproducible |
| Tests directory | ❌ | No `tests/` — README does not claim test coverage, which is correct. Not adding a false badge. |
| GitHub social preview image | ⚠️ | Default GitHub preview. Add a custom 1280×640 image under Settings → General → Social preview — it's what shows when this link is shared on Slack/Twitter/etc. |
| .github/FUNDING.yml | ❌ | Missing (optional — add only if you want a Sponsor button) |

**No files need to be drafted** — the only actionable items are the social preview (a Settings click) and optionally committing `package-lock.json`.

---

## 3. SEO, badges, backlinks

### GitHub description (updated)

> Flipper Zero + Android: rolling 300–928 MHz sweep, RSSI triangulation (Nelder-Mead), freq DB browser with CH/OFCOM entries, offline spectrum lookup (ITU/FCC/CEPT/OFCOM, 2967 rows).

_Note: `gh repo edit --description` requires your confirmation to run (external write). Run it yourself:_

```bash
gh repo edit TFD-42/Mhz_Localiser \
  --description "Flipper Zero + Android: rolling 300–928 MHz sweep, RSSI triangulation (Nelder-Mead), freq DB browser with CH/OFCOM entries, offline spectrum lookup (ITU/FCC/CEPT/OFCOM, 2967 rows)."
```

### Topics — suggested additions (currently at 20-topic limit)

Remove 3 lower-value topics to add:

| Remove | Add |
|--------|-----|
| `beta-release` | `rolling-scan` |
| `localization` | `cc1101` |
| `gps` | `swiss-frequencies` |

```bash
gh repo edit TFD-42/Mhz_Localiser \
  --remove-topic beta-release \
  --remove-topic localization \
  --remove-topic gps \
  --add-topic rolling-scan \
  --add-topic cc1101 \
  --add-topic swiss-frequencies
```

### Badges (already in README — grounded in confirmed facts)

All 8 current badges are fact-backed:
- `license-MIT` ✅ confirmed by LICENSE file
- CI badge ✅ ci.yml exists
- Flipper Zero platform ✅
- Android 8.0+ ✅ (AndroidManifest minSdkVersion)
- USB · BLE ✅ both transports implemented
- Spectrum DB 2,967 rows ✅ confirmed row count
- Rolling scan 300–928 MHz ✅ implemented in rf_logger.c
- Status: beta ✅ honest

### Backlink checklist (external actions — you submit these)

1. **[awesome-flipper-zero](https://github.com/djsime1/awesome-flipperzero)** — most-watched Flipper list. One-line pitch ready to paste:
   > `Mhz_Localiser` — live Sub-GHz RSSI logger with rolling 300–928 MHz sweep, Android triangulation app, and offline spectrum allocation lookup (2,967 entries, CH/OFCOM included).

2. **[awesome-sdr](https://github.com/adamlui/awesome-sdr)** — SDR tools list:
   > `Mhz_Localiser` — pocket RF triangulator: Flipper Zero CC1101 → Android GPS capture → Nelder-Mead RSSI trilateration. Free, no signup, works over USB or Bluetooth LE.

3. **AlternativeTo** — list as alternative to KrakenSDR for the hobbyist segment. Honest pitch: lower precision but $60 all-in vs. $500.

4. **r/flipperzero** and **r/amateursatellites / r/amateurradio** — only when the v2.2 release is tagged. These communities will verify the limitations claims; the honest README holds up to that scrutiny.

5. **Show HN** — the README is solid and the limitations are disclosed. Ready for Hacker News scrutiny if you want the exposure. Suggested title: _"Show HN: Flipper Zero + Android = $60 RF triangulator with rolling spectrum sweep"_

---

## 4. Doc changes applied

| File | What changed |
|------|-------------|
| `README.md` | Added `## What's new in v2.2` table; updated file tree (Roll Scan tab, 2,967 rows); updated Flipper app flow (DB list + rolling scan navigation); added `## Roll Scan` section with feature table; updated `## Allocation List` with correct row count + CH note; updated `## Data Protocol` with ROLL stream format; added rolling-scan badge; updated TOC |
| `CHANGELOG.md` | Promoted `[Unreleased]` → `[2.2] - 2026-10-01`; cleaned up duplicate entries; proper English throughout |

Nothing deleted. All limitation sections intact and unchanged in substance.

---

## Steps with nothing to report

- **Attribution**: no evidence found in either direction — correct to have no Acknowledgments section.
- **Dependency vulnerabilities**: not in scope of this pass; `dependabot[bot]` is already active (seen in contributors list).
- **License correctness**: MIT, single copyright block, no stale/conflicting headers.
