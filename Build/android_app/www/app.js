// RF Triangulator — simplified app
// Runs both in a browser (dev) and inside Capacitor on Android.

const Capacitor = window.Capacitor || null;
const Geolocation = Capacitor?.Plugins?.Geolocation || null;
const FlipperSerial = Capacitor?.Plugins?.FlipperSerial || null;

// ── State ────────────────────────────────────────────────────────────
const state = {
  captures: [],       // { id, lat, lon, rssi, freqHz, meta }
  nextId: 1,
  lastGps: null,      // { lat, lon, accuracy, ts }
  estimateMarker: null,
  estimateCircle: null,
};

const flipper = {
  connected: false,
  streaming: false,
  latestRssi: null,
  latestFreqHz: 433920000,
  latestLqi: 0,
  latestN: 0,
  autoCapture: false,
  autoCaptureInterval: 2000,
  autoCaptureTimer: null,
};

// ── DOM refs ─────────────────────────────────────────────────────────
const $ = (id) => document.getElementById(id);

// ── Map ──────────────────────────────────────────────────────────────
const map = L.map('map', { zoomControl: true, attributionControl: false })
  .setView([48.8566, 2.3522], 13);
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
  maxZoom: 19, crossOrigin: true,
}).addTo(map);

let userMarker = null;
let userAccCircle = null;

// ── Propagation (log-distance, n=3, 433.92 MHz default) ──────────────
function rssiToDistMeters(rssi, freqHz = 433920000) {
  const fMHz = freqHz / 1e6;
  const n = 3.0;
  const pt = 10;
  const pl0 = 32.44 + 20 * Math.log10(fMHz) - 60;
  const pl = pt - rssi;
  const d = Math.pow(10, (pl - pl0) / (10 * n));
  return Math.min(50000, Math.max(1, d));
}

// ── Lat/Lon <-> local XY ─────────────────────────────────────────────
function ll2xy(lat, lon, oLat, oLon) {
  const R = 6371000;
  return [
    (lon - oLon) * Math.PI / 180 * R * Math.cos(oLat * Math.PI / 180),
    (lat - oLat) * Math.PI / 180 * R,
  ];
}
function xy2ll(x, y, oLat, oLon) {
  const R = 6371000;
  return [
    oLat + (y / R) * 180 / Math.PI,
    oLon + (x / (R * Math.cos(oLat * Math.PI / 180))) * 180 / Math.PI,
  ];
}

// ── Nelder-Mead ──────────────────────────────────────────────────────
function nelderMead(f, x0, { step = 50, maxIter = 600, tol = 1e-6 } = {}) {
  const n = x0.length;
  let simplex = [x0.slice()];
  for (let i = 0; i < n; i++) { const v = x0.slice(); v[i] += step; simplex.push(v); }
  let vals = simplex.map(f);
  for (let iter = 0; iter < maxIter; iter++) {
    const ord = vals.map((v, i) => i).sort((a, b) => vals[a] - vals[b]);
    simplex = ord.map(i => simplex[i]); vals = ord.map(i => vals[i]);
    if (Math.abs(vals[n] - vals[0]) < tol) break;
    const cen = new Array(n).fill(0);
    for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) cen[j] += simplex[i][j] / n;
    const xr = cen.map((c, j) => c + (c - simplex[n][j]));
    const fr = f(xr);
    if (fr >= vals[0] && fr < vals[n - 1]) { simplex[n] = xr; vals[n] = fr; continue; }
    if (fr < vals[0]) {
      const xe = cen.map((c, j) => c + 2 * (xr[j] - c));
      const fe = f(xe);
      if (fe < fr) { simplex[n] = xe; vals[n] = fe; } else { simplex[n] = xr; vals[n] = fr; }
      continue;
    }
    const xc = cen.map((c, j) => c + 0.5 * (simplex[n][j] - c));
    if (f(xc) < vals[n]) { simplex[n] = xc; vals[n] = f(xc); continue; }
    for (let i = 1; i <= n; i++) {
      simplex[i] = simplex[0].map((s, j) => s + 0.5 * (simplex[i][j] - s));
      vals[i] = f(simplex[i]);
    }
  }
  return { x: simplex[0], fx: vals[0] };
}

function triangulate(caps) {
  if (caps.length < 3) return null;
  const oLat = caps.reduce((s, c) => s + c.lat, 0) / caps.length;
  const oLon = caps.reduce((s, c) => s + c.lon, 0) / caps.length;
  const pts = caps.map(c => {
    const [x, y] = ll2xy(c.lat, c.lon, oLat, oLon);
    return { x, y, d: rssiToDistMeters(c.rssi, c.freqHz) };
  });
  const cost = ([x, y]) => pts.reduce((s, p) => {
    const e = Math.hypot(x - p.x, y - p.y) - p.d; return s + e * e;
  }, 0);
  let cx = 0, cy = 0, w = 0;
  for (const p of pts) { const wt = 1 / Math.max(1, p.d); cx += p.x * wt; cy += p.y * wt; w += wt; }
  const res = nelderMead(cost, [cx / w, cy / w], { step: 100, maxIter: 800 });
  const rms = Math.sqrt(res.fx / pts.length);
  const [lat, lon] = xy2ll(res.x[0], res.x[1], oLat, oLon);
  return { lat, lon, rmsErr: rms };
}

// ── Captures ─────────────────────────────────────────────────────────
function addCapture(lat, lon, rssi, freqHz, meta = {}) {
  const id = state.nextId++;
  const cap = { id, lat, lon, rssi, freqHz: freqHz || flipper.latestFreqHz || 433920000, meta };
  cap.marker = L.circleMarker([lat, lon], {
    radius: 8, color: '#58a6ff', fillColor: '#58a6ff', fillOpacity: 0.7, weight: 2,
  }).addTo(map).bindTooltip(`#${id}: ${rssi} dBm`, { permanent: false, direction: 'top' });
  const distM = rssiToDistMeters(cap.rssi, cap.freqHz);
  cap.circle = L.circle([lat, lon], {
    radius: distM, color: '#58a6ff', fillOpacity: 0.05, weight: 1,
  }).addTo(map);
  state.captures.push(cap);
  renderList();
  refreshEstimate();
  return cap;
}

function removeCapture(id) {
  const i = state.captures.findIndex(c => c.id === id);
  if (i < 0) return;
  const cap = state.captures[i];
  map.removeLayer(cap.marker);
  map.removeLayer(cap.circle);
  state.captures.splice(i, 1);
  renderList();
  refreshEstimate();
}

// ── List render ───────────────────────────────────────────────────────
function renderList() {
  const list = $('receivers-list');
  const count = $('receiver-count');
  count.textContent = state.captures.length;
  count.className = 'pill ' + (state.captures.length >= 3 ? 'good' : 'warn');
  list.innerHTML = '';
  for (const cap of state.captures) {
    const card = document.createElement('div');
    card.className = 'receiver-card';
    const fMHz = (cap.freqHz / 1e6).toFixed(2);
    card.innerHTML = `
      <div class="hdr">
        <span class="name">#${cap.id}</span>
        <button class="delete-btn" data-id="${cap.id}" aria-label="Remove">✕</button>
      </div>
      <div class="coord">${cap.lat.toFixed(5)}, ${cap.lon.toFixed(5)}</div>
      <div class="rssi-val">${cap.rssi} dBm · ${fMHz} MHz${cap.meta?.source ? ' · ' + cap.meta.source : ''}</div>
    `;
    list.appendChild(card);
  }
  list.querySelectorAll('.delete-btn').forEach(b => {
    b.onclick = () => removeCapture(+b.dataset.id);
  });
}

// ── Estimate ─────────────────────────────────────────────────────────
function refreshEstimate() {
  if (state.estimateMarker) { map.removeLayer(state.estimateMarker); state.estimateMarker = null; }
  if (state.estimateCircle) { map.removeLayer(state.estimateCircle); state.estimateCircle = null; }
  const estStatus = $('estimate-status');
  const estCoord = $('estimate-coord');
  const estError = $('estimate-error');
  const gotoEst = $('goto-estimate');
  if (state.captures.length < 3) {
    estStatus.textContent = `Need ≥ 3 captures (have ${state.captures.length}).`;
    estCoord.textContent = '';
    estError.textContent = '';
    gotoEst.style.display = 'none';
    return;
  }
  const r = triangulate(state.captures);
  if (!r) return;
  estStatus.textContent = '✓ Estimated transmitter:';
  estCoord.textContent = `${r.lat.toFixed(6)}, ${r.lon.toFixed(6)}`;
  estError.textContent = `RMS residual ${r.rmsErr.toFixed(1)} m`;
  gotoEst.style.display = 'block';
  gotoEst.dataset.lat = r.lat;
  gotoEst.dataset.lon = r.lon;
  const txIcon = L.divIcon({ className: '', html: '<div class="tx-icon-pulse"></div>', iconSize: [22, 22], iconAnchor: [11, 11] });
  state.estimateMarker = L.marker([r.lat, r.lon], { icon: txIcon, zIndexOffset: 1000 })
    .addTo(map)
    .bindTooltip(`Estimated Tx ±${r.rmsErr.toFixed(0)}m`, { permanent: true, direction: 'top', offset: [0, -14] });
  state.estimateCircle = L.circle([r.lat, r.lon], { radius: Math.max(10, r.rmsErr), color: '#f85149', fillOpacity: 0.15, weight: 1 }).addTo(map);
}

// ── GPS ───────────────────────────────────────────────────────────────
async function startGpsWatch() {
  if (Geolocation) {
    try {
      const st = await Geolocation.checkPermissions();
      if (st.location !== 'granted') await Geolocation.requestPermissions();
    } catch (_) {}
    try {
      await Geolocation.watchPosition({ enableHighAccuracy: true, timeout: 15000 }, (pos, err) => {
        if (err) { setGpsBadge('stale', 'GPS: error'); return; }
        if (pos) onGpsFix(pos.coords);
      });
      return;
    } catch (_) {}
  }
  if (navigator.geolocation) {
    navigator.geolocation.watchPosition(
      (p) => onGpsFix(p.coords),
      (e) => setGpsBadge('stale', 'GPS: ' + e.message),
      { enableHighAccuracy: true, maximumAge: 1000, timeout: 15000 }
    );
  } else {
    setGpsBadge('stale', 'GPS: unavailable');
  }
}

function onGpsFix(c) {
  state.lastGps = { lat: c.latitude, lon: c.longitude, accuracy: c.accuracy, ts: Date.now() };
  const cls = c.accuracy < 15 ? 'ok' : c.accuracy < 50 ? 'weak' : 'stale';
  setGpsBadge(cls, `GPS: ${c.latitude.toFixed(5)}, ${c.longitude.toFixed(5)} ±${c.accuracy.toFixed(0)}m`);
  if (!userMarker) {
    userMarker = L.circleMarker([c.latitude, c.longitude], {
      radius: 7, color: '#58a6ff', fillColor: '#58a6ff', fillOpacity: 0.9, weight: 2,
    }).addTo(map);
    userAccCircle = L.circle([c.latitude, c.longitude], { radius: c.accuracy, color: '#58a6ff', weight: 1, fillOpacity: 0.05 }).addTo(map);
    map.setView([c.latitude, c.longitude], 17);
  } else {
    userMarker.setLatLng([c.latitude, c.longitude]);
    userAccCircle.setLatLng([c.latitude, c.longitude]);
    userAccCircle.setRadius(c.accuracy);
  }
}

function setGpsBadge(cls, text) {
  $('gps-badge').className = 'gps-badge ' + cls;
  $('gps-text').textContent = text;
}

// ── Capture ───────────────────────────────────────────────────────────
async function captureHere() {
  let fix = state.lastGps;
  if (!fix || Date.now() - fix.ts > 5000) {
    try {
      if (Geolocation) {
        const p = await Geolocation.getCurrentPosition({ enableHighAccuracy: true, timeout: 15000 });
        fix = { lat: p.coords.latitude, lon: p.coords.longitude, accuracy: p.coords.accuracy, ts: Date.now() };
        onGpsFix(p.coords);
      } else if (navigator.geolocation) {
        fix = await new Promise((res, rej) =>
          navigator.geolocation.getCurrentPosition(
            (p) => { onGpsFix(p.coords); res({ lat: p.coords.latitude, lon: p.coords.longitude, accuracy: p.coords.accuracy, ts: Date.now() }); },
            rej, { enableHighAccuracy: true, timeout: 15000 }
          )
        );
      }
    } catch (e) {
      return toast('No GPS fix: ' + (e.message || 'failed'), 'error');
    }
  }
  if (!fix) return toast('No GPS fix yet', 'error');
  const rssi = flipper.latestRssi !== null ? flipper.latestRssi : -70;
  const freqHz = flipper.latestFreqHz || 433920000;
  const src = flipper.connected ? 'Flipper' : 'manual';
  const cap = addCapture(fix.lat, fix.lon, rssi, freqHz, { source: `GPS ±${fix.accuracy.toFixed(0)}m · ${src}` });
  toast(`#${cap.id}: ${rssi} dBm @ ${(freqHz / 1e6).toFixed(2)} MHz`, 'ok');
}

// ── Auto capture ──────────────────────────────────────────────────────
function startAutoCapture() {
  if (!flipper.connected) return toast('Connect Flipper first', 'error');
  flipper.autoCapture = true;
  updateFlipperDrawer();
  toast('Auto-capture ON', 'ok');
  const tick = async () => {
    if (!flipper.autoCapture) return;
    if (flipper.latestRssi !== null) await captureHere();
    flipper.autoCaptureTimer = setTimeout(tick, flipper.autoCaptureInterval);
  };
  flipper.autoCaptureTimer = setTimeout(tick, flipper.autoCaptureInterval);
}

function stopAutoCapture() {
  flipper.autoCapture = false;
  clearTimeout(flipper.autoCaptureTimer);
  flipper.autoCaptureTimer = null;
  updateFlipperDrawer();
  toast('Auto-capture OFF', '');
}

// ── Flipper mirror panel ──────────────────────────────────────────────
function updateMirror(rssi, freqHz, lqi, n) {
  const mirror = $('flipper-mirror');
  if (mirror) mirror.classList.remove('hidden');

  const freqEl = $('fm-freq');
  if (freqEl) freqEl.textContent = (freqHz / 1e6).toFixed(2) + ' MHz';

  const rssiEl = $('fm-rssi');
  if (rssiEl) {
    rssiEl.textContent = rssi.toFixed(0) + ' dBm';
    rssiEl.className = 'fm-rssi ' + (rssi > -70 ? 'strong' : rssi > -90 ? 'medium' : 'weak');
  }

  const bar = $('fm-bar');
  if (bar) {
    const pct = Math.max(0, Math.min(100, ((rssi + 120) / 120) * 100));
    bar.style.width = pct + '%';
    bar.style.background = rssi > -70 ? '#3fb950' : rssi > -90 ? '#d29922' : '#f85149';
  }

  const meta = $('fm-meta');
  if (meta) meta.textContent = `LQI:${lqi} · #${n}`;

  const dot = $('fm-dot');
  if (dot) dot.className = 'fm-dot live';
  const st = $('fm-status-text');
  if (st) st.textContent = 'Live';
}

function setMirrorDisconnected() {
  const mirror = $('flipper-mirror');
  if (mirror) mirror.classList.add('hidden');
  const dot = $('fm-dot');
  if (dot) { dot.className = 'fm-dot'; }
  const st = $('fm-status-text');
  if (st) st.textContent = 'Offline';
}

function updateFlipperDrawer() {
  const panel = $('flipper-panel');
  if (panel) panel.dataset.connected = flipper.connected ? '1' : '0';
  const badge = $('flipper-rssi-badge');
  if (badge) {
    badge.textContent = flipper.connected
      ? `${(flipper.latestFreqHz / 1e6).toFixed(2)} MHz · ${flipper.latestRssi?.toFixed(1) ?? '—'} dBm`
      : 'Not connected';
  }
  const connectBtn = $('flipper-connect-btn');
  if (connectBtn) connectBtn.textContent = flipper.connected ? 'Disconnect' : 'Connect Flipper';
  const autoBtn = $('flipper-auto-btn');
  if (autoBtn) {
    autoBtn.textContent = flipper.autoCapture ? 'Stop Auto' : 'Auto-capture';
    autoBtn.disabled = !flipper.connected;
  }
}

// ── Flipper connect/stream ────────────────────────────────────────────
async function flipperConnect() {
  if (!FlipperSerial) return toast('FlipperSerial plugin unavailable', 'error');
  try {
    const r = await FlipperSerial.connect();
    if (r.success) {
      flipper.connected = true;
      updateFlipperDrawer();
      toast('Flipper connected', 'ok');
      flipperStartStream();
    } else {
      toast(r.message || 'Connect failed', 'error');
    }
  } catch (e) {
    toast('USB error: ' + e.message, 'error');
  }
}

async function flipperDisconnect() {
  stopAutoCapture();
  flipper.autoCapture = false;
  if (FlipperSerial) {
    await FlipperSerial.removeAllListeners().catch(() => {});
    await FlipperSerial.stopStream().catch(() => {});
    await FlipperSerial.disconnect().catch(() => {});
  }
  flipper.connected = false;
  flipper.streaming = false;
  setMirrorDisconnected();
  updateFlipperDrawer();
  toast('Flipper disconnected', '');
}

function onFlipperData(data) {
  if (data?.error) {
    flipper.connected = false;
    flipper.streaming = false;
    setMirrorDisconnected();
    updateFlipperDrawer();
    toast('Flipper disconnected', 'error');
    return;
  }
  if (data?.rssi !== undefined) {
    flipper.latestRssi = typeof data.rssi === 'number' ? data.rssi : parseFloat(data.rssi);
    if (data.freq) flipper.latestFreqHz = parseInt(data.freq, 10) || flipper.latestFreqHz;
    flipper.latestLqi = data.lqi || 0;
    flipper.latestN = data.n || 0;
    updateMirror(flipper.latestRssi, flipper.latestFreqHz, flipper.latestLqi, flipper.latestN);
    updateFlipperDrawer();
  }
}

async function flipperStartStream() {
  if (!FlipperSerial || !flipper.connected) return;
  flipper.streaming = true;
  // Register listener before starting stream
  await FlipperSerial.addListener('rssiData', onFlipperData);
  try {
    await FlipperSerial.startStream({});
  } catch (e) {
    flipper.streaming = false;
    toast('Stream error: ' + e.message, 'error');
  }
}

// ── .sub file import ──────────────────────────────────────────────────
async function importFiles(fileList) {
  const files = Array.from(fileList);
  let imported = 0, skipped = 0;
  for (const f of files) {
    try {
      const text = await f.text();
      const records = parseSubText(text, f.name);
      for (const rec of records) {
        let lat = rec.lat, lon = rec.lon;
        if (lat == null || lon == null) {
          if (state.lastGps) { lat = state.lastGps.lat; lon = state.lastGps.lon; }
          else { const c = map.getCenter(); lat = c.lat; lon = c.lng; }
        }
        addCapture(lat, lon, rec.rssi, rec.freq || 433920000, { source: f.name });
        imported++;
      }
      if (!records.length) skipped++;
    } catch (e) {
      console.error('parse failed', f.name, e);
      skipped++;
    }
  }
  $('import-status').textContent = `Imported ${imported} reading(s)${skipped ? ', skipped ' + skipped : ''}.`;
  toast(imported ? `Imported ${imported} capture(s)` : 'No RSSI found in files', imported ? 'ok' : 'error');
}

function parseSubText(text, filename) {
  const records = [];
  const lines = text.split(/\r?\n/);
  let pendingLat = null, pendingLon = null, pendingFreq = null;
  for (const raw of lines) {
    const ln = raw.trim();
    if (!ln) continue;
    const latM = ln.match(/lat(?:itude)?\s*[:=]\s*(-?\d+(?:\.\d+)?)/i);
    if (latM) pendingLat = parseFloat(latM[1]);
    const lonM = ln.match(/lon(?:g(?:itude)?)?\s*[:=]\s*(-?\d+(?:\.\d+)?)/i);
    if (lonM) pendingLon = parseFloat(lonM[1]);
    const freqM = ln.match(/^Frequency\s*[:=]\s*(\d+)/i);
    if (freqM) pendingFreq = parseInt(freqM[1], 10);
    const rssiM = ln.match(/RSSI\s*[:=]?\s*(-?\d+(?:\.\d+)?)\s*(?:dBm)?/i);
    if (rssiM) {
      const rssi = parseFloat(rssiM[1]);
      if (rssi >= -130 && rssi <= 20)
        records.push({ rssi, lat: pendingLat, lon: pendingLon, freq: pendingFreq, file: filename });
    }
  }
  if (records.length === 0) {
    for (const raw of lines) {
      const ln = raw.trim();
      if (!ln || ln.startsWith('#') || ln.startsWith('//')) continue;
      const cols = ln.split(/[,;\t]/).map(s => s.trim()).filter(Boolean);
      if (cols.length >= 3) {
        const a = parseFloat(cols[0]), b = parseFloat(cols[1]), c = parseFloat(cols[2]);
        if (Number.isFinite(a) && Number.isFinite(b) && Number.isFinite(c)
            && a >= -90 && a <= 90 && b >= -180 && b <= 180 && c >= -130 && c <= 20)
          records.push({ lat: a, lon: b, rssi: c, file: filename });
      }
    }
  }
  return records;
}

// ── Export ────────────────────────────────────────────────────────────
function exportCSV() {
  const rows = ['id,lat,lon,rssi_dbm,freq_hz,source'];
  for (const c of state.captures)
    rows.push(`${c.id},${c.lat},${c.lon},${c.rssi},${c.freqHz},${c.meta?.source || ''}`);
  download(rows.join('\n'), `rf-captures-${Date.now()}.csv`, 'text/csv');
}

function exportJSON() {
  const est = state.captures.length >= 3 ? triangulate(state.captures) : null;
  const data = {
    exportedAt: new Date().toISOString(),
    captures: state.captures.map(c => ({ id: c.id, lat: c.lat, lon: c.lon, rssi: c.rssi, freqHz: c.freqHz, source: c.meta?.source })),
    estimate: est,
  };
  download(JSON.stringify(data, null, 2), `rf-captures-${Date.now()}.json`, 'application/json');
}

function download(content, filename, mime) {
  const blob = new Blob([content], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = filename;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// ── Toast ─────────────────────────────────────────────────────────────
let toastTimer = null;
function toast(msg, kind = '') {
  const el = $('toast');
  if (!el) return;
  el.textContent = msg;
  el.className = 'toast show' + (kind ? ' ' + kind : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove('show'), 2500);
}

// ── Drag & drop ───────────────────────────────────────────────────────
let dragDepth = 0;
window.addEventListener('dragenter', (e) => {
  if (!e.dataTransfer?.types?.includes('Files')) return;
  e.preventDefault(); dragDepth++;
  $('drop-overlay').classList.remove('hidden');
});
window.addEventListener('dragover', (e) => { if (!e.dataTransfer?.types?.includes('Files')) return; e.preventDefault(); });
window.addEventListener('dragleave', (e) => {
  if (!e.dataTransfer?.types?.includes('Files')) return;
  dragDepth = Math.max(0, dragDepth - 1);
  if (!dragDepth) $('drop-overlay').classList.add('hidden');
});
window.addEventListener('drop', (e) => {
  if (!e.dataTransfer?.files?.length) return;
  e.preventDefault(); dragDepth = 0;
  $('drop-overlay').classList.add('hidden');
  importFiles(e.dataTransfer.files);
});

// ── Wire up DOM ───────────────────────────────────────────────────────
window.addEventListener('DOMContentLoaded', () => {
  $('menu-btn').onclick = () => $('drawer').classList.toggle('hidden');
  $('drawer-close').onclick = () => $('drawer').classList.add('hidden');

  $('capture-btn').onclick = captureHere;

  $('flipper-connect-btn').onclick = () =>
    flipper.connected ? flipperDisconnect() : flipperConnect();

  const autoBtn = $('flipper-auto-btn');
  if (autoBtn) autoBtn.onclick = () =>
    flipper.autoCapture ? stopAutoCapture() : startAutoCapture();

  $('clear-btn').onclick = () => {
    if (!state.captures.length) return;
    if (!confirm('Remove all captures?')) return;
    for (const c of state.captures) { map.removeLayer(c.marker); map.removeLayer(c.circle); }
    state.captures = []; state.nextId = 1;
    renderList(); refreshEstimate();
  };

  $('export-csv-btn').onclick = exportCSV;
  $('export-json-btn').onclick = exportJSON;

  $('goto-estimate').onclick = () => {
    const lat = parseFloat($('goto-estimate').dataset.lat);
    const lon = parseFloat($('goto-estimate').dataset.lon);
    if (Number.isFinite(lat) && Number.isFinite(lon)) map.setView([lat, lon], 18);
    $('drawer').classList.add('hidden');
  };

  const intervalSel = $('flipper-interval');
  if (intervalSel) intervalSel.onchange = (e) => { flipper.autoCaptureInterval = parseInt(e.target.value, 10); };

  $('sub-file').onchange = (e) => { if (e.target.files?.length) importFiles(e.target.files); e.target.value = ''; };
  $('sub-file-main').onchange = (e) => { if (e.target.files?.length) importFiles(e.target.files); e.target.value = ''; };

  updateFlipperDrawer();
  renderList();
  setTimeout(startGpsWatch, 300);
});
