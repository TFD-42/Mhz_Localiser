// Usage: npm install --no-save jsdom@24 && node android/tests/offline_boot.test.js android/www
//
// Regression test for offline start-up. The app must boot with no network:
//   1. index.html may not pull scripts or stylesheets from a remote host
//      (a CDN outage or a phone in the field without data used to leave `L`
//      undefined, so app.js threw at map init and nothing after it ran —
//      USB/BLE bridge, Allocation List and Roll Scan included);
//   2. the real vendored Leaflet + app.js evaluate without a top-level error;
//   3. the Allocation List loads the bundled spectrum.csv and renders rows.
// Map tiles (OpenStreetMap) are the only expected network use; jsdom does not
// fetch images, so they are out of scope here by construction.
'use strict';
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const WWW = process.argv[2];
if (!WWW || !fs.existsSync(path.join(WWW, 'index.html'))) {
  console.error('usage: node offline_boot.test.js <path-to-android/www>');
  process.exit(2);
}

let fail = 0;
const ok = (cond, msg) => {
  if (cond) console.log('ok  ', msg);
  else { fail++; console.error('FAIL', msg); }
};

const rawHtml = fs.readFileSync(path.join(WWW, 'index.html'), 'utf8');

// ---- 1. no remote code or styles --------------------------------------
const remoteRefs = [...rawHtml.matchAll(/<(script|link)\b[^>]*\b(?:src|href)=["']((?:https?:)?\/\/[^"']+)["']/gi)]
  .map((m) => `${m[1]} ${m[2]}`);
ok(remoteRefs.length === 0, 'index.html loads no remote script/stylesheet' +
  (remoteRefs.length ? ` (found: ${remoteRefs.join(', ')})` : ''));

// Local scripts, in document order, resolved against www/.
const localScripts = [...rawHtml.matchAll(/<script\b[^>]*\bsrc=["']([^"']+)["'][^>]*><\/script>/gi)]
  .map((m) => m[1])
  .filter((src) => !/^(?:https?:)?\/\//i.test(src));
ok(localScripts.length >= 2, `local scripts found: ${localScripts.join(', ')}`);
for (const src of localScripts) {
  ok(fs.existsSync(path.join(WWW, src)), `script exists on disk: ${src}`);
}

// ---- 2. boot with the network cut --------------------------------------
const html = rawHtml.replace(/<script\b[^>]*\bsrc=["'][^"']+["'][^>]*><\/script>/gi, '');
const jsdomErrors = [];
const vc = new VirtualConsole();
vc.on('jsdomError', (e) => jsdomErrors.push(String((e && (e.detail || e.message)) || e)));
const dom = new JSDOM(html, {
  runScripts: 'outside-only',
  pretendToBeVisual: true,
  url: 'http://localhost/',
  virtualConsole: vc,
});
const w = dom.window;
const runtimeErrors = [];
w.addEventListener('error', (e) => runtimeErrors.push(String(e.error || e.message)));
w.addEventListener('unhandledrejection', (e) => runtimeErrors.push('unhandledrejection: ' + String(e.reason)));

Object.defineProperty(w.navigator, 'geolocation', {
  value: { watchPosition: () => 1, getCurrentPosition: () => {}, clearWatch: () => {} },
});
w.Capacitor = { Plugins: {
  FlipperSerial: { addListener: () => Promise.resolve({ remove() {} }) },
  FlipperBle:    { addListener: () => Promise.resolve({ remove() {} }) },
} };

// Offline fetch: only relative paths inside www/ resolve; anything absolute fails like a dead network.
const remoteFetches = [];
w.fetch = (url) => {
  const u = String(url);
  if (/^(?:https?:)?\/\//i.test(u)) {
    remoteFetches.push(u);
    return Promise.reject(new TypeError('Failed to fetch (offline): ' + u));
  }
  const root = path.resolve(WWW);
  const file = path.resolve(root, u.replace(/^\.?\//, '').split(/[?#]/)[0]);
  if (!file.startsWith(root + path.sep) || !fs.existsSync(file)) {
    return Promise.resolve({ ok: false, status: 404, text: () => Promise.resolve('') });
  }
  return Promise.resolve({ ok: true, status: 200, text: () => Promise.resolve(fs.readFileSync(file, 'utf8')) });
};

let bootError = null;
for (const src of localScripts) {
  try {
    w.eval(fs.readFileSync(path.join(WWW, src), 'utf8') + `\n//# sourceURL=${src}`);
  } catch (e) {
    bootError = `${src}: ${e && e.name}: ${e && e.message}`;
    break;
  }
}
ok(bootError === null, 'vendored Leaflet + app.js evaluate offline' + (bootError ? ` (${bootError})` : ''));
ok(typeof w.L === 'object' && typeof w.L.map === 'function', 'Leaflet global available from local copy');

// ---- 3. Allocation List works offline ---------------------------------
(async () => {
  const allocTab = w.document.querySelector('#tabs .tab[data-tab="alloc"]');
  ok(!!allocTab, 'Allocation tab button present');
  if (allocTab) allocTab.dispatchEvent(new w.MouseEvent('click', { bubbles: true }));

  // loadSpectrum() is async: poll until rows render or 5 s elapse.
  const deadline = Date.now() + 5000;
  let rows = 0;
  while (Date.now() < deadline) {
    rows = w.document.querySelectorAll('#allocPanel tbody tr').length;
    if (rows > 0) break;
    await new Promise((r) => setTimeout(r, 25));
  }
  ok(rows > 0, `Allocation List rendered ${rows} row(s) from bundled spectrum.csv`);
  ok(remoteFetches.length === 0, 'no fetch() to a remote host' +
    (remoteFetches.length ? ` (${remoteFetches.join(', ')})` : ''));
  ok(runtimeErrors.length === 0, 'no runtime error events' +
    (runtimeErrors.length ? ` (${runtimeErrors.slice(0, 3).join(' | ')})` : ''));
  // jsdom reports "Not implemented" for some browser APIs Leaflet probes; those are not app failures.
  const realJsdomErrors = jsdomErrors.filter((m) => !/Not implemented/i.test(m));
  ok(realJsdomErrors.length === 0, 'no jsdom script errors' +
    (realJsdomErrors.length ? ` (${realJsdomErrors.slice(0, 3).join(' | ')})` : ''));

  dom.window.close();
  console.log(fail ? `\n${fail} check(s) failed` : '\nall offline boot checks passed');
  process.exit(fail ? 1 : 0);
})().catch((e) => {
  console.error('FAIL harness error:', e);
  process.exit(1);
});
