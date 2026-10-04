// Usage: npm install --no-save jsdom@24 && node android/tests/roll_ui.test.js android/www
// Drives the real index.html + app.js in jsdom with a stubbed Leaflet and a
// fake FlipperSerial bridge, then feeds a v3 rolling-scan stream and a legacy one.
const fs = require('fs'), path = require('path');
const { JSDOM } = require('jsdom');
const WWW = process.argv[2];
let html = fs.readFileSync(path.join(WWW, 'index.html'), 'utf8')
  .replace(/<script src="https:[^"]+"><\/script>/g, '')
  .replace('<script src="app.js"></script>', '');
const dom = new JSDOM(html, { runScripts: 'outside-only', pretendToBeVisual: true, url: 'http://localhost/' });
const w = dom.window;
const errors = [];
w.addEventListener('error', e => errors.push(String(e.error || e.message)));
// chainable no-op Leaflet
const chain = new Proxy(function () {}, { get: (t, k) => (k === 'then' ? undefined : chain), apply: () => chain, construct: () => chain });
w.L = chain;
Object.defineProperty(w.navigator, 'geolocation', { value: { watchPosition: () => 1, getCurrentPosition: () => {}, clearWatch: () => {} } });
let dataCb = null;
w.Capacitor = { Plugins: {
  FlipperSerial: { addListener: (ev, cb) => { if (ev === 'data') dataCb = cb; return Promise.resolve({ remove(){} }); } },
  FlipperBle:    { addListener: () => Promise.resolve({ remove(){} }) },
} };
w.fetch = () => Promise.resolve({ ok: true, text: () => Promise.resolve(fs.readFileSync(path.join(WWW, 'spectrum.csv'), 'utf8')) });
w.eval(fs.readFileSync(path.join(WWW, 'app.js'), 'utf8'));
const feed = l => dataCb({ line: l });
const $ = id => w.document.getElementById(id);
let fail = 0; const ok = (c, m) => { if (!c) { fail++; console.error('FAIL', m); } else console.log('ok  ', m); };

ok(typeof dataCb === 'function', 'bridge data listener registered');
// ---- v3 stream ----
feed('# RF_LOGGER_ROLL v=3 bands=300000000-348000000,387000000-464000000,779000000-928000000 step=600000 bw=650000 dwell_ms=100 fine_step=20000 fine_bw=58000 fine_span=300000 fine_dwell_ms=40 points=461');
feed('ROLL,ts_ms,freq_hz,rssi_avg,rssi_peak,samples,flag,pass,floor_dbm,width_hz,cluster,seq');
ok($('rollStatus').textContent.includes('Pass 1/2'), 'v3 header → pass 1 status');
feed('# RF_LOGGER_RSCAN pos pass=C idx=230 total=461 hz=433800000');
ok(Math.abs(parseFloat($('rollProgressBar').style.width) - 40) <= 1, 'coarse progress ~40% got ' + $('rollProgressBar').style.width);
feed('# RF_LOGGER_RSCAN floor b0=-104 b1=-100 b2=-98 thr0=-93 thr1=-90 thr2=-88');
feed('ROLL,50000,433800000,-70,-60,97,STRONG,C,-100,600000,0,0');
feed('ROLL,50001,868800000,-85,-82,194,MED,C,-98,1200000,1,1');
ok($('rollTbody').querySelectorAll('tr').length === 2, 'two coarse hits shown');
feed('# RF_LOGGER_RSCAN pos pass=F idx=0 total=2 hz=433800000');
ok($('rollStatus').textContent.includes('Pass 2/2'), 'fine status');
feed('ROLL,52000,433921000,-66,-58,1200,STRONG,F,-100,40000,0,2');
// seq 3 dropped on purpose
feed('ROLL,53000,868951000,-84,-80,1200,STRONG,F,-98,60000,1,4');
const rows = Array.from($('rollTbody').querySelectorAll('tr'));
ok(rows.length === 2, 'fine replaces coarse (still 2 rows), got ' + rows.length);
ok(rows[0].children[0].textContent === '433.921 ·F', 'strongest first, fine tag: ' + rows[0].children[0].textContent);
ok(rows[0].children[2].textContent === '-58 dBm' && rows[0].children[3].textContent === '40 kHz', 'peak + width cells');
feed('ROLL,53500,433800000,-70,-60,97,STRONG,C,-100,600000,0,5');
ok($('rollTbody').querySelector('tr').children[0].textContent === '433.921 ·F', 'late coarse line does not override fine');
feed('# RF_LOGGER_RSCAN end status=ok clusters=2 coarse_ms=48000 total_ms=51000 tune_err=0 usb_drops=0');
const st = $('rollStatus').textContent;
ok(st.includes('Scan complete') && st.includes('1 line(s) lost') && st.includes('51 s') && st.includes('floor -104/-100/-98'), 'end status: ' + st);
ok($('rollProgressBar').style.width === '100%', 'progress 100%');
// Tune button bridge
rows[0].querySelector('.roll-tune-btn').click();
ok(errors.length === 0, 'no page errors after Tune click: ' + errors.join(' | '));
// XSS-ish flag sanitising
feed('# RF_LOGGER_ROLL v=3');
feed('ROLL,1,433000000,-70,-60,1,<img src=x onerror=alert(1)>,C,-100,600000,0,0');
ok(!$('rollTbody').innerHTML.includes('<img'), 'flag sanitised');
// ---- legacy v2 stream ----
$('rollClear').click();
feed('# RF_LOGGER_ROLL start=300000000 step=1000000 max=928000000');
feed('ROLL,12345,433920000,-87,-84,10,MED');
feed('ROLL,18345,927000000,-76,-72,10,STRONG');
ok($('rollTbody').querySelectorAll('tr').length === 2, 'legacy rows');
ok($('rollStatus').textContent.startsWith('Scan complete'), 'legacy completion heuristic');
// Running-mode CSV still parsed (not swallowed by the new branches)
feed('# RF_LOGGER_DBG req=433920000 act=433919830');
feed('ts_ms,req_hz,act_hz,rssi_dbm,rssi_raw,lqi,n');
feed('1000,433920000,433919830,-71,0x39,12,1');
ok(errors.length === 0, 'no page errors overall: ' + errors.join(' | '));
console.log(fail ? `${fail} FAILED` : 'ALL OK');
process.exit(fail ? 1 : 0);
