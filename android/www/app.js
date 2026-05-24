/*
 * RF Triangulator — front-end logic
 * ---------------------------------
 * Listens to the FlipperSerial Capacitor plugin for live CSV rows,
 * mirrors freq + RSSI in the top bar, lets the user capture {lat,lon,rssi,freq}
 * tuples via GPS, draws them on a Leaflet map, and runs a Nelder-Mead
 * least-squares solver to estimate the transmitter location.
 *
 * The path-loss distance model is:
 *     d = 10^( (Tx_dBm - RSSI_dBm) / (10 * n) )
 * with n configurable in the drawer (default 3.0, urban).
 */

(() => {
'use strict';

/* ----------------------------- state ----------------------------- */
const state = {
  connected: false,
  freqHz: null,
  rssi: null,
  captures: [],     // {id, lat, lon, rssi, freq, source}
  estimate: null,   // {lat, lon, rms}
  autoTimer: null,
  autoInterval: 2000,
  n: 3.0,
  txDbm: 10,
  nextId: 1,
};

/* ----------------------------- map ----------------------------- */
const map = L.map('map', { zoomControl: true }).setView([48.8566, 2.3522], 14);
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
  maxZoom: 19,
  attribution: '© OpenStreetMap',
}).addTo(map);

const layerCaptures = L.layerGroup().addTo(map);
const layerCircles  = L.layerGroup().addTo(map);
let estimateMarker = null;
let userMarker = null;

navigator.geolocation.watchPosition(pos => {
  const { latitude, longitude } = pos.coords;
  if (!userMarker) {
    userMarker = L.circleMarker([latitude, longitude], {
      radius: 6, color: '#1976d2', fillColor: '#1976d2', fillOpacity: 0.8,
    }).addTo(map);
    map.setView([latitude, longitude], 17);
  } else {
    userMarker.setLatLng([latitude, longitude]);
  }
}, err => console.warn('geo', err), { enableHighAccuracy: true, maximumAge: 1000 });

/* ----------------------------- bridge ----------------------------- */
const Bridge = window.Capacitor?.Plugins?.FlipperSerial;

function setConnDot(on) {
  state.connected = on;
  const dot = document.getElementById('connDot');
  dot.classList.toggle('on', on);
  dot.classList.toggle('off', !on);
  dot.title = on ? 'connected' : 'disconnected';
}

if (Bridge) {
  Bridge.addListener('status', ev => {
    setConnDot(ev.state === 'connected');
  });
  Bridge.addListener('data', ev => {
    handleSerialLine(ev.line);
  });
}

function handleSerialLine(line) {
  if (!line || line.startsWith('#') || line.startsWith('ts_ms')) return;
  // ts_ms,req_hz,act_hz,rssi_dbm,rssi_raw,lqi,n
  const parts = line.split(',');
  if (parts.length < 4) return;
  const reqHz   = parseInt(parts[1], 10);
  const rssi    = parseInt(parts[3], 10);
  if (!isFinite(rssi)) return;
  state.rssi   = rssi;
  if (isFinite(reqHz)) state.freqHz = reqHz;
  renderReadout();
  // Auto-sync allocation list with live frequency
  if (isFinite(reqHz)) syncAllocByFreq(reqHz);
}

function renderReadout() {
  const fEl = document.getElementById('freqVal');
  const rEl = document.getElementById('rssiVal');
  fEl.textContent = state.freqHz
    ? (state.freqHz / 1e6).toFixed(2) + ' MHz'
    : '— MHz';
  rEl.textContent = state.rssi !== null ? state.rssi + ' dBm' : '— dBm';
  const fill = Math.max(0, Math.min(100, ((state.rssi ?? -120) + 120) / 90 * 100));
  document.getElementById('barFill').style.width = fill + '%';
}

/* ----------------------------- capture ----------------------------- */
function capture(source = 'manual') {
  if (state.rssi === null) { toast('No live RSSI yet'); return; }
  navigator.geolocation.getCurrentPosition(pos => {
    const c = {
      id: state.nextId++,
      lat: pos.coords.latitude,
      lon: pos.coords.longitude,
      rssi: state.rssi,
      freq: state.freqHz,
      source,
    };
    state.captures.push(c);
    drawCapture(c);
    renderCaptureList();
    solve();
  }, err => toast('GPS error: ' + err.message), { enableHighAccuracy: true, maximumAge: 0, timeout: 8000 });
}

function drawCapture(c) {
  const m = L.marker([c.lat, c.lon]).addTo(layerCaptures)
    .bindPopup(`#${c.id} ${c.rssi} dBm`);
  const d = rssiToDistance(c.rssi);
  L.circle([c.lat, c.lon], { radius: d, color: '#888', weight: 1, fillOpacity: 0.05 })
    .addTo(layerCircles);
}

function renderCaptureList() {
  const ol = document.getElementById('captureList');
  ol.innerHTML = '';
  for (const c of state.captures) {
    const li = document.createElement('li');
    li.textContent = `${c.rssi} dBm — ${c.lat.toFixed(5)}, ${c.lon.toFixed(5)} (${rssiToDistance(c.rssi).toFixed(0)} m)`;
    ol.appendChild(li);
  }
  document.getElementById('estN').textContent = state.captures.length;
}

/* ----------------------------- path loss + solver ----------------------------- */
function rssiToDistance(rssi) {
  // d = 10^((Tx - RSSI) / (10*n)), reference d0 = 1 m, PL(d0) folded into txDbm
  const exp = (state.txDbm - rssi) / (10 * state.n);
  return Math.pow(10, exp);
}

/* haversine */
const R_EARTH = 6371000;
function toRad(d) { return d * Math.PI / 180; }
function haversine(lat1, lon1, lat2, lon2) {
  const dLat = toRad(lat2 - lat1);
  const dLon = toRad(lon2 - lon1);
  const a = Math.sin(dLat/2)**2 + Math.cos(toRad(lat1))*Math.cos(toRad(lat2))*Math.sin(dLon/2)**2;
  return 2 * R_EARTH * Math.asin(Math.sqrt(a));
}

function residualSum([lat, lon]) {
  let s = 0;
  for (const c of state.captures) {
    const d_obs = rssiToDistance(c.rssi);
    const d_est = haversine(lat, lon, c.lat, c.lon);
    const r = d_est - d_obs;
    s += r * r;
  }
  return s;
}

/* Nelder–Mead in 2D */
function nelderMead(f, x0, opts = {}) {
  const alpha = 1, gamma = 2, rho = 0.5, sigma = 0.5;
  const maxIter = opts.maxIter ?? 500;
  const tol     = opts.tol     ?? 1e-7;
  const step    = opts.step    ?? 1e-3;

  let simplex = [
    x0.slice(),
    [x0[0] + step, x0[1]],
    [x0[0],        x0[1] + step],
  ].map(p => ({ p, v: f(p) }));

  for (let it = 0; it < maxIter; it++) {
    simplex.sort((a, b) => a.v - b.v);
    const best = simplex[0], worst = simplex[2], second = simplex[1];
    if (Math.abs(worst.v - best.v) < tol) break;

    const centroid = [(best.p[0] + second.p[0]) / 2, (best.p[1] + second.p[1]) / 2];
    const reflect = [centroid[0] + alpha * (centroid[0] - worst.p[0]),
                     centroid[1] + alpha * (centroid[1] - worst.p[1])];
    const vr = f(reflect);

    if (vr < second.v && vr >= best.v) { simplex[2] = { p: reflect, v: vr }; continue; }
    if (vr < best.v) {
      const expand = [centroid[0] + gamma * (reflect[0] - centroid[0]),
                      centroid[1] + gamma * (reflect[1] - centroid[1])];
      const ve = f(expand);
      simplex[2] = ve < vr ? { p: expand, v: ve } : { p: reflect, v: vr };
      continue;
    }
    const contract = [centroid[0] + rho * (worst.p[0] - centroid[0]),
                      centroid[1] + rho * (worst.p[1] - centroid[1])];
    const vc = f(contract);
    if (vc < worst.v) { simplex[2] = { p: contract, v: vc }; continue; }
    /* shrink */
    simplex[1] = { p: [best.p[0] + sigma * (second.p[0] - best.p[0]),
                       best.p[1] + sigma * (second.p[1] - best.p[1])], v: 0 };
    simplex[2] = { p: [best.p[0] + sigma * (worst.p[0]  - best.p[0]),
                       best.p[1] + sigma * (worst.p[1]  - best.p[1])], v: 0 };
    simplex[1].v = f(simplex[1].p);
    simplex[2].v = f(simplex[2].p);
  }
  simplex.sort((a, b) => a.v - b.v);
  return simplex[0];
}

function solve() {
  if (state.captures.length < 3) {
    state.estimate = null;
    document.getElementById('estLat').textContent = '—';
    document.getElementById('estLon').textContent = '—';
    document.getElementById('estRms').textContent = '—';
    if (estimateMarker) { map.removeLayer(estimateMarker); estimateMarker = null; }
    return;
  }
  /* weighted centroid as starting guess */
  let wsum = 0, lat0 = 0, lon0 = 0;
  for (const c of state.captures) {
    const w = Math.pow(10, c.rssi / 10); /* stronger = higher weight */
    wsum += w; lat0 += w * c.lat; lon0 += w * c.lon;
  }
  lat0 /= wsum; lon0 /= wsum;

  const sol = nelderMead(residualSum, [lat0, lon0], { step: 1e-3 });
  const rms = Math.sqrt(sol.v / state.captures.length);
  state.estimate = { lat: sol.p[0], lon: sol.p[1], rms };

  document.getElementById('estLat').textContent = sol.p[0].toFixed(6);
  document.getElementById('estLon').textContent = sol.p[1].toFixed(6);
  document.getElementById('estRms').textContent = rms.toFixed(1);

  if (estimateMarker) map.removeLayer(estimateMarker);
  estimateMarker = L.marker([sol.p[0], sol.p[1]], {
    title: 'Estimated Tx',
  }).addTo(map).bindPopup(`Estimated Tx<br>RMS: ${rms.toFixed(1)} m`).openPopup();
}

/* ----------------------------- file import ----------------------------- */
function parseImported(text) {
  const out = [];
  // Try Flipper .sub style first (RSSI: / Latitude: / Longitude: blocks)
  const blocks = text.split(/Filetype:|^---$/m);
  for (const blk of blocks) {
    const rssi = /RSSI:\s*(-?\d+)/i.exec(blk);
    const lat  = /Latitude:\s*(-?\d+\.\d+)/i.exec(blk);
    const lon  = /Longitude:\s*(-?\d+\.\d+)/i.exec(blk);
    const freq = /Frequency:\s*(\d+)/i.exec(blk);
    if (rssi && lat && lon) {
      out.push({
        id: state.nextId++,
        rssi: parseInt(rssi[1], 10),
        lat:  parseFloat(lat[1]),
        lon:  parseFloat(lon[1]),
        freq: freq ? parseInt(freq[1], 10) : null,
        source: 'import',
      });
    }
  }
  // Fallback: CSV with id,lat,lon,rssi_dbm,freq_hz,source
  if (out.length === 0) {
    const lines = text.split(/\r?\n/).filter(l => l && !l.startsWith('id'));
    for (const line of lines) {
      const p = line.split(',');
      if (p.length >= 4) {
        const lat = parseFloat(p[1]), lon = parseFloat(p[2]), rssi = parseInt(p[3], 10);
        if (isFinite(lat) && isFinite(lon) && isFinite(rssi)) {
          out.push({ id: state.nextId++, lat, lon, rssi,
                     freq: parseInt(p[4], 10) || null, source: 'import' });
        }
      }
    }
  }
  return out;
}

/* ----------------------------- export ----------------------------- */
function download(name, mime, data) {
  const blob = new Blob([data], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = name;
  document.body.appendChild(a); a.click();
  setTimeout(() => { URL.revokeObjectURL(url); a.remove(); }, 100);
}

function exportCsv() {
  const rows = ['id,lat,lon,rssi_dbm,freq_hz,source'];
  for (const c of state.captures)
    rows.push(`${c.id},${c.lat},${c.lon},${c.rssi},${c.freq ?? ''},${c.source}`);
  download('captures.csv', 'text/csv', rows.join('\n'));
}

function exportJson() {
  download('captures.json', 'application/json',
    JSON.stringify({ captures: state.captures, estimate: state.estimate,
                     n: state.n, txDbm: state.txDbm }, null, 2));
}

/* ----------------------------- UI wiring ----------------------------- */
function toast(msg) {
  const t = document.createElement('div');
  t.className = 'toast'; t.textContent = msg;
  document.body.appendChild(t);
  setTimeout(() => t.remove(), 2500);
}

document.getElementById('menuBtn').onclick = () => {
  document.getElementById('drawer').classList.remove('hidden');
};
document.getElementById('closeDrawer').onclick = () => {
  document.getElementById('drawer').classList.add('hidden');
};
document.getElementById('captureBtn').onclick = () => capture('manual');
document.getElementById('solveBtn').onclick = solve;
document.getElementById('autoBtn').onclick = () => {
  const btn = document.getElementById('autoBtn');
  if (state.autoTimer) {
    clearInterval(state.autoTimer); state.autoTimer = null;
    btn.textContent = 'Auto-capture'; btn.classList.remove('active');
  } else {
    state.autoTimer = setInterval(() => capture('auto'), state.autoInterval);
    btn.textContent = 'Stop Auto'; btn.classList.add('active');
  }
};
document.getElementById('connectBtn').onclick = async () => {
  if (!Bridge) { toast('Native bridge unavailable'); return; }
  try { await Bridge.connect(); } catch (e) { toast('Connect failed: ' + e); }
};
document.getElementById('disconnectBtn').onclick = async () => {
  if (Bridge) await Bridge.disconnect();
};
document.getElementById('loadBtn').onclick = () => document.getElementById('fileInput').click();
document.getElementById('fileInput').onchange = e => {
  const file = e.target.files[0]; if (!file) return;
  const r = new FileReader();
  r.onload = () => {
    const imported = parseImported(r.result);
    if (!imported.length) { toast('Nothing parseable in file'); return; }
    for (const c of imported) { state.captures.push(c); drawCapture(c); }
    renderCaptureList(); solve();
    toast(`Imported ${imported.length} captures`);
  };
  r.readAsText(file);
};
document.getElementById('exportCsvBtn').onclick  = exportCsv;
document.getElementById('exportJsonBtn').onclick = exportJson;
document.getElementById('clearBtn').onclick = () => {
  state.captures = []; state.estimate = null;
  layerCaptures.clearLayers(); layerCircles.clearLayers();
  if (estimateMarker) { map.removeLayer(estimateMarker); estimateMarker = null; }
  renderCaptureList(); solve();
};
renderReadout();
renderCaptureList();
})();

/* ===================================================================== */
/* Allocation List tab — independent module, reads www/spectrum.csv      */
/* ===================================================================== */
(function () {
  const $  = (id) => document.getElementById(id);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

  /* ---- Tab switching ------------------------------------------------- */
  const tabs       = $$('#tabs .tab');
  const tabPanels  = { map: $('map'), alloc: $('allocPanel') };
  let allocLoaded  = false;

  tabs.forEach((btn) => {
    btn.addEventListener('click', () => {
      const target = btn.dataset.tab;
      tabs.forEach((b) => b.classList.toggle('active', b === btn));
      Object.entries(tabPanels).forEach(([k, el]) => {
        if (!el) return;
        el.classList.toggle('active', k === target);
      });
      if (target === 'alloc' && !allocLoaded) {
        allocLoaded = true;
        loadSpectrum();
      }
      if (target === 'map' && typeof map !== 'undefined' && map.invalidateSize) {
        // Leaflet needs a redraw after being hidden
        setTimeout(() => map.invalidateSize(), 50);
      }
    });
  });

  /* ---- CSV parsing (handles quoted fields with commas) --------------- */
  function parseCsvLine(line) {
    const out = [];
    let cur = '', inQ = false;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (inQ) {
        if (c === '"' && line[i + 1] === '"') { cur += '"'; i++; }
        else if (c === '"') inQ = false;
        else cur += c;
      } else {
        if (c === ',') { out.push(cur); cur = ''; }
        else if (c === '"') inQ = true;
        else cur += c;
      }
    }
    out.push(cur);
    return out;
  }
  function parseCsv(text) {
    const lines = text.split(/\r?\n/).filter((l) => l.length);
    if (!lines.length) return [];
    const headers = parseCsvLine(lines[0]);
    return lines.slice(1).map((line) => {
      const cells = parseCsvLine(line);
      const row = {};
      headers.forEach((h, i) => { row[h] = cells[i] || ''; });
      return row;
    });
  }

  /* ---- State --------------------------------------------------------- */
  let allocations = [];

  async function loadSpectrum() {
    setEmpty('Loading spectrum data…');
    try {
      const resp = await fetch('spectrum.csv');
      if (!resp.ok) throw new Error('HTTP ' + resp.status);
      const text = await resp.text();
      const raw = parseCsv(text);
      allocations = raw
        .map((r) => ({
          freq_low_mhz:    parseFloat(r.freq_low_mhz),
          freq_high_mhz:   parseFloat(r.freq_high_mhz),
          country:         r.country || '',
          region:          r.region || '',
          service:         r.service || '',
          status:          r.status || '',
          application:     r.application || '',
          footnotes:       r.footnotes || '',
          typical_devices: r.typical_devices || '',
          source:          r.source || '',
          note:            r.note || '',
        }))
        .filter((a) => !isNaN(a.freq_low_mhz) && !isNaN(a.freq_high_mhz))
        .sort((a, b) =>
          a.freq_low_mhz - b.freq_low_mhz
          || a.country.localeCompare(b.country)
          || a.service.localeCompare(b.service)
        );
      populateFilters();
      renderTable(allocations);
    } catch (e) {
      setEmpty('Failed to load spectrum.csv: ' + e.message);
    }
  }

  function populateFilters() {
    const countries = Array.from(new Set(allocations.map((a) => a.country).filter(Boolean))).sort();
    const regions   = Array.from(new Set(allocations.map((a) => a.region).filter(Boolean))).sort();
    fillSelect($('allocCountry'),    countries);
    fillSelect($('allocMhzCountry'), countries);
    fillSelect($('allocRegion'),     regions);
  }
  function fillSelect(sel, values) {
    if (!sel) return;
    // keep the "(all)" first option
    const first = sel.querySelector('option');
    sel.innerHTML = '';
    if (first) sel.appendChild(first);
    values.forEach((v) => {
      const opt = document.createElement('option');
      opt.value = v; opt.textContent = v;
      sel.appendChild(opt);
    });
  }

  /* ---- Mode switching ------------------------------------------------ */
  const modeSel = $('allocMode');
  modeSel.addEventListener('change', () => {
    const byRegion = modeSel.value === 'region';
    $('allocByRegion').classList.toggle('hidden', !byRegion);
    $('allocByMhz').classList.toggle('hidden', byRegion);
    if (byRegion) applyRegionFilter();
    else renderTable([]);
  });

  /* ---- Filter logic -------------------------------------------------- */
  function applyRegionFilter() {
    const country = $('allocCountry').value;
    const region  = $('allocRegion').value;
    let rows = allocations;
    if (country) rows = rows.filter((a) => a.country === country);
    if (region)  rows = rows.filter((a) => a.region === region);
    renderTable(rows);
  }
  $('allocCountry').addEventListener('change', applyRegionFilter);
  $('allocRegion').addEventListener('change',  applyRegionFilter);

  function parseFreqInput(s) {
    s = s.trim().toLowerCase();
    if (!s) throw new Error('empty input');
    let unit = '';
    const m = /\s*(k|m|g)?hz\s*$/.exec(s);
    if (m) { unit = m[1] || 'm'; s = s.slice(0, m.index).trim(); }
    const toMhz = (v) => {
      if (unit === '' || unit === 'm') return v;
      if (unit === 'k') return v / 1000;
      if (unit === 'g') return v * 1000;
      throw new Error('unknown unit: ' + unit);
    };
    if (s.includes('-')) {
      const [lo, hi] = s.split('-', 2).map(parseFloat);
      if (isNaN(lo) || isNaN(hi)) throw new Error('invalid range');
      const a = toMhz(lo), b = toMhz(hi);
      return a <= b ? [a, b] : [b, a];
    }
    const v = parseFloat(s);
    if (isNaN(v)) throw new Error('not a number');
    const mhz = toMhz(v);
    return [mhz, mhz];
  }

  function applyMhzFilter() {
    const text = $('allocFreq').value;
    const country = $('allocMhzCountry').value;
    let lo, hi;
    try {
      [lo, hi] = parseFreqInput(text);
    } catch (e) {
      setEmpty('Could not parse: ' + e.message);
      $('allocCount').textContent = '0 results';
      $('allocTbody').innerHTML = '';
      return;
    }
    let pool = country ? allocations.filter((a) => a.country === country) : allocations;
    const matches = pool.filter((a) =>
      lo === hi
        ? a.freq_low_mhz <= lo && lo <= a.freq_high_mhz
        : !(a.freq_high_mhz < lo || a.freq_low_mhz > hi)
    );
    if (matches.length === 0) {
      // Suggest nearest bands
      const below = pool.filter((a) => a.freq_high_mhz < lo);
      const above = pool.filter((a) => a.freq_low_mhz > hi);
      const nearestBelow = below.length
        ? below.reduce((b, a) => (a.freq_high_mhz > b.freq_high_mhz ? a : b))
        : null;
      const nearestAbove = above.length
        ? above.reduce((b, a) => (a.freq_low_mhz < b.freq_low_mhz ? a : b))
        : null;
      let html = `No allocation covers ${lo === hi ? lo.toFixed(4) + ' MHz' : lo.toFixed(4) + '–' + hi.toFixed(4) + ' MHz'}`;
      if (country) html += ` in ${country}`;
      html += '.';
      if (nearestBelow || nearestAbove) {
        html += '<div class="nearby">Closest bands:<br>';
        if (nearestBelow) {
          const gap = (lo - nearestBelow.freq_high_mhz).toFixed(3);
          html += `↓ <b>${fmtRange(nearestBelow)}</b> (${nearestBelow.country} ${escapeHtml(nearestBelow.service)}) — ${gap} MHz below<br>`;
        }
        if (nearestAbove) {
          const gap = (nearestAbove.freq_low_mhz - hi).toFixed(3);
          html += `↑ <b>${fmtRange(nearestAbove)}</b> (${nearestAbove.country} ${escapeHtml(nearestAbove.service)}) — ${gap} MHz above<br>`;
        }
        html += '<small>Gaps usually mean military/government use or unallocated spectrum.</small></div>';
      }
      setEmpty(html);
      $('allocCount').textContent = '0 results';
      $('allocTbody').innerHTML = '';
      return;
    }
    renderTable(matches);
  }
  $('allocFreqGo').addEventListener('click', applyMhzFilter);
  $('allocFreq').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); applyMhzFilter(); }
  });
  $('allocMhzCountry').addEventListener('change', () => {
    if ($('allocFreq').value.trim()) applyMhzFilter();
  });

  /* ---- Reset --------------------------------------------------------- */
  $('allocClear').addEventListener('click', () => {
    $('allocCountry').value = '';
    $('allocRegion').value  = '';
    $('allocMhzCountry').value = '';
    $('allocFreq').value = '';
    if (modeSel.value === 'region') applyRegionFilter();
    else renderTable(allocations);
  });

  /* ---- Render -------------------------------------------------------- */
  function fmtRange(a) {
    return `${a.freq_low_mhz.toFixed(4)}–${a.freq_high_mhz.toFixed(4)} MHz`;
  }
  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[c]));
  }
  function renderTable(rows) {
    const tbody = $('allocTbody');
    tbody.innerHTML = '';
    $('allocCount').textContent = `${rows.length} result${rows.length === 1 ? '' : 's'}`;
    if (!rows.length) {
      setEmpty('No results. Adjust filters or pick a different mode.');
      return;
    }
    hideEmpty();
    const MAX = 500;
    const shown = rows.slice(0, MAX);
    const frag = document.createDocumentFragment();
    for (const a of shown) {
      const tr = document.createElement('tr');
      const app = a.application || (a.typical_devices.split(';')[0] || '').trim();
      tr.innerHTML =
        `<td class="freq-cell">${fmtRange(a)}</td>` +
        `<td>${escapeHtml(a.country)}</td>` +
        `<td>${escapeHtml(a.region)}</td>` +
        `<td>${escapeHtml(a.service)}</td>` +
        `<td class="status-${escapeHtml(a.status.replace(/[^A-Za-z]/g, ''))}">${escapeHtml(a.status)}</td>` +
        `<td>${escapeHtml(app)}</td>` +
        `<td>${escapeHtml(a.source)}</td>`;
      tr.title = a.typical_devices + (a.note ? '\n\n' + a.note : '');
      frag.appendChild(tr);
    }
    tbody.appendChild(frag);
    if (rows.length > MAX) {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td colspan="7" style="text-align:center; padding:12px; color:#7d8590;">… ${rows.length - MAX} more rows hidden. Refine filters to narrow.</td>`;
      tbody.appendChild(tr);
    }
  }
  function setEmpty(htmlMsg) {
    const el = $('allocEmpty');
    el.innerHTML = htmlMsg;
    el.classList.remove('hidden');
  }
  function hideEmpty() { $('allocEmpty').classList.add('hidden'); }

  /* ---- Live sync from Flipper stream --------------------------------- */
  window.syncAllocByFreq = function(freqHz) {
    if (allocations.length === 0) return; // Not loaded yet
    const freqMhz = freqHz / 1e6;
    const matches = allocations.filter((a) => a.freq_low_mhz <= freqMhz && freqMhz <= a.freq_high_mhz);
    // Auto-switch to allocation tab and display matches
    const allocTabBtn = $$('#tabs .tab').find(btn => btn.dataset.tab === 'alloc');
    const mapTabBtn = $$('#tabs .tab').find(btn => btn.dataset.tab === 'map');
    if (allocTabBtn && mapTabBtn && !allocTabBtn.classList.contains('active')) {
      // Only auto-switch if currently on map tab
      allocTabBtn.classList.add('active');
      mapTabBtn.classList.remove('active');
      tabPanels.alloc.classList.add('active');
      tabPanels.map.classList.remove('active');
      if (typeof map !== 'undefined' && map.invalidateSize) {
        setTimeout(() => map.invalidateSize(), 50);
      }
    }
    renderTable(matches);
  };
})();
