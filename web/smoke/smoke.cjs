// Smoke test for the browser build in headless Chromium (Playwright).
//
//   node web/smoke/smoke.cjs <url> <out-dir> [--phone]
//
// Checks: the page starts with no page errors; the wasm rules give the pinned
// determinism fingerprint (tests/test_determinism.cpp); the canvas draws a
// colony (not one flat colour); a mouse drag designates tiles (desktop) or a
// tap with the Hut tool plans a hut (phone profile, touch). Writes PNGs to
// <out-dir>.
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { chromium, devices } = require('playwright');

const FINGERPRINT = '011ed2b2f364b21b';  // seed 42, 64x64, 30 settlers, 1000 ticks
const [url, outDir] = process.argv.slice(2);
const phone = process.argv.includes('--phone');
if (!url || !outDir) {
  console.error('usage: smoke.cjs <url> <out-dir> [--phone]');
  process.exit(2);
}
fs.mkdirSync(outDir, { recursive: true });

// Minimal PNG decoder (8-bit RGB/RGBA, non-interlaced: what Chromium writes).
function decodePng(buf) {
  let pos = 8, width = 0, height = 0, channels = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('ascii', pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      channels = { 2: 3, 6: 4 }[data[9]];
      if (data[8] !== 8 || !channels || data[12] !== 0) throw new Error('unsupported PNG');
    } else if (type === 'IDAT') idat.push(data);
    pos += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(height * stride);
  for (let y = 0; y < height; y++) {
    const f = raw[y * (stride + 1)];
    for (let x = 0; x < stride; x++) {
      const v = raw[y * (stride + 1) + 1 + x];
      const a = x >= channels ? out[y * stride + x - channels] : 0;
      const b = y > 0 ? out[(y - 1) * stride + x] : 0;
      const c = x >= channels && y > 0 ? out[(y - 1) * stride + x - channels] : 0;
      let p;
      if (f === 0) p = 0;
      else if (f === 1) p = a;
      else if (f === 2) p = b;
      else if (f === 3) p = (a + b) >> 1;
      else {
        const pa = Math.abs(b - c), pb = Math.abs(a - c), pc = Math.abs(a + b - 2 * c);
        p = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      out[y * stride + x] = (v + p) & 255;
    }
  }
  return { width, height, channels, data: out };
}

// Distinct colours (quantised to 4 bits per channel) in a PNG.
function colourCount(png) {
  const img = decodePng(png);
  const seen = new Set();
  for (let i = 0; i < img.data.length; i += img.channels)
    seen.add(((img.data[i] >> 4) << 8) | ((img.data[i + 1] >> 4) << 4) | (img.data[i + 2] >> 4));
  return seen.size;
}

(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const base = phone ? devices['iPhone 13'] : { viewport: { width: 1280, height: 800 } };
  // "headless" in the UA keeps arcade from counting a smoke run as a play.
  const context = await browser.newContext({ ...base, userAgent: (base.userAgent || 'Mozilla/5.0') + ' steading-smoke headless' });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  const fail = async (why) => {
    console.error('FAIL: ' + why);
    await page.screenshot({ path: path.join(outDir, 'failure.png') }).catch(() => {});
    await browser.close();
    process.exit(1);
  };

  // The main loop keeps the load event from ever firing: wait for commit only.
  await page.goto(url + (url.includes('?') ? '&' : '?') + 'seed=42', { waitUntil: 'commit' });
  try {
    await page.waitForFunction(() => window.steading && window.steading.status, null, { timeout: 60000 });
  } catch (e) {
    await fail('the game never became ready: ' + errors.join(' | '));
  }

  const fp = await page.evaluate(() => window.steading.fingerprint(42, 1000));
  console.log('wasm fingerprint (seed 42, 1000 ticks): ' + fp);
  if (fp !== FINGERPRINT) await fail(`fingerprint ${fp}, expected ${FINGERPRINT}`);

  const status = async () => JSON.parse(await page.evaluate(() => window.steading.status()));
  // Input is handled on the next frame, and SwiftShader frames are slow: poll.
  const until = async (pred, ms = 15000) => {
    const end = Date.now() + ms;
    let st = await status();
    while (!pred(st) && Date.now() < end) {
      await page.waitForTimeout(200);
      st = await status();
    }
    return st;
  };
  let s = await status();
  if (s.seed !== 42) await fail('seed from the URL was not used: ' + s.seed);
  await page.waitForTimeout(1500);
  const t0 = (await status()).tick;
  if (t0 < 1) await fail('the colony does not tick in real time');

  await page.evaluate(() => window.steading.advance(300));
  await page.waitForTimeout(800);
  const shot = await page.screenshot({ path: path.join(outDir, 'steading.png'), timeout: 180000 });
  const colours = colourCount(shot);
  console.log(`screenshot: ${colours} distinct colours`);
  if (colours < 12) await fail('the canvas looks blank (' + colours + ' colours)');

  const box = await page.locator('#canvas').boundingBox();
  const cx = box.x + box.width / 2, cy = box.y + box.height / 2;
  if (phone) {
    // Touch: pick the Hut tool and tap the map.
    await page.locator('[data-tool="2"]').tap();
    await page.touchscreen.tap(cx + 30, cy + 30);
    s = await until((st) => !/new colony/.test(st.message));
    console.log('after a tap with the Hut tool: ' + s.message);
    if (!/hut|wood|water|occupied|tile|bounds|stand|place/i.test(s.message) || /new colony/.test(s.message))
      await fail('a tap did not reach the game: ' + s.message);
  } else {
    await page.mouse.move(cx - 120, cy - 80);
    await page.mouse.down();
    await page.mouse.move(cx + 120, cy + 80, { steps: 8 });
    await page.mouse.up();
    s = await until((st) => /^designated/.test(st.message));
    console.log('after a drag: ' + s.message);
    if (!/^designated [1-9]\d* tile/.test(s.message)) await fail('a drag did not designate tiles: ' + s.message);
    await page.keyboard.press('Tab');
    await page.keyboard.down('KeyW');
    await page.waitForTimeout(600);
    await page.keyboard.up('KeyW');
    s = await until((st) => st.flying);
    if (!s.flying) await fail('Tab did not switch to the free-fly camera');
    await page.screenshot({ path: path.join(outDir, 'steading-fly.png'), timeout: 180000 });
  }
  await page.locator('#pause').click();
  if (!(await until((st) => st.paused)).paused) await fail('the Pause button did not pause');

  // The loading shell finished; and without WebGL 2 it shows the trailer (or
  // its still) and says why, and the engine never downloads.
  {
    await page.waitForFunction(() => { const r = document.querySelector('.sk-loader'); return r && r.hidden; }, null, { timeout: 10000 }).catch(() => {});
    const shell = await page.evaluate(() => {
      const r = document.querySelector('.sk-loader');
      return { hidden: !!(r && r.hidden), state: window.StrataLoader && StrataLoader.state };
    });
    if (!shell.hidden || shell.state !== 'done') await fail('the loading shell did not finish: ' + JSON.stringify(shell));
    console.log('ok: the loading shell showed and is gone');
    const bare = await browser.newContext({ ...base, userAgent: (base.userAgent || 'Mozilla/5.0') + ' steading-smoke headless' });
    await bare.addInitScript(() => {
      const get = HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext = function (t, o) { return t === 'webgl2' ? null : get.call(this, t, o); };
    });
    const p2 = await bare.newPage();
    const engine = [];
    p2.on('request', (r) => { if (/steading\.(wasm|js)$/.test(r.url())) engine.push(r.url()); });
    await p2.goto(url, { waitUntil: 'commit' });
    await p2.waitForFunction(() => window.StrataLoader && StrataLoader.state === 'fallback', null, { timeout: 30000 });
    const fb = await p2.evaluate(() => {
      const r = document.querySelector('.sk-loader');
      return { text: r.innerText, media: !!r.querySelector('video, img'), wide: document.documentElement.scrollWidth > innerWidth };
    });
    await p2.screenshot({ path: path.join(outDir, 'no-webgl2.png') });
    if (!/WebGL 2/.test(fb.text) || fb.wide) await fail('without WebGL 2 the shell did not say why: ' + JSON.stringify(fb));
    if (engine.length) await fail('without WebGL 2 the engine still downloaded: ' + engine.join(', '));
    console.log('ok: without WebGL 2 the shell ' + (fb.media ? 'shows the trailer and ' : '') + 'says why, and nothing of the engine downloads');
    await bare.close();
  }

  if (errors.length) await fail('page errors: ' + errors.join(' | '));
  console.log(`OK (${phone ? 'phone' : 'desktop'}): tick ${s.tick}, ${s.settlers} settlers`);
  await browser.close();
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
