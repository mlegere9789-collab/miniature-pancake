// Real-browser check that the exported web viewer (src/io/WebViewer.cpp) is
// usable with fingers - the "touch-first companion" slice that fits in a
// static file. Like web_viewer_browser_check.js this needs Playwright and a
// Chromium (PLAYWRIGHT_BROWSERS_PATH) and is run by hand / by tests/web_viewer_touch_test.sh:
//
//   node tests/web_viewer_touch_check.js <exported viewer .html>
//
// It emulates a touch device, then drives REAL touch input through the DevTools
// protocol (Input.dispatchTouchEvent - the same events a phone screen produces),
// and reads the camera back from window.__dino8Viewer.camera():
//   one-finger drag -> orbit (yaw/pitch change), two-finger pinch apart -> zoom in
//   (dist shrinks), pinch together -> zoom out, mouse drag and wheel still work,
//   the page has a viewport meta, the canvas has touch-action:none, and a part's
//   Scale slider still drives window.__dino8Viewer.scales.
const { chromium } = require('playwright');
const path = require('path');

async function main() {
  const fixture = process.argv[2];
  if (!fixture) { console.error('usage: node web_viewer_touch_check.js <viewer.html>'); process.exit(1); }
  let failures = 0;
  const check = (ok, what) => { console.log((ok ? 'ok  ' : 'FAIL') + ' ' + what); if (!ok) failures++; };

  const browser = await chromium.launch();
  try {
    const ctx = await browser.newContext({ hasTouch: true, isMobile: true, viewport: { width: 400, height: 700 } });
    const page = await ctx.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(String(e)));
    page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
    await page.goto('file://' + path.resolve(fixture));
    await page.waitForFunction('window.__dino8Viewer && window.__dino8Viewer.ready === true', { timeout: 8000 });
    check(errors.length === 0, 'no console/page errors (' + JSON.stringify(errors) + ')');
    check(await page.evaluate("!!document.querySelector('meta[name=viewport]')"), 'page has a viewport meta tag (renders at device width on a phone)');
    check(await page.evaluate("getComputedStyle(document.querySelector('canvas')).touchAction") === 'none', 'canvas has touch-action:none (gestures do not scroll or browser-zoom the page)');
    check(await page.evaluate("typeof window.__dino8Viewer.camera === 'function'"), 'window.__dino8Viewer.camera() is exposed for tests');

    const cam = () => page.evaluate('window.__dino8Viewer.camera()');
    const cdp = await ctx.newCDPSession(page);
    const touch = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts.map((p, i) => ({ x: p[0], y: p[1], id: i })) });

    // one finger drag: orbit
    const c0 = await cam();
    await touch('touchStart', [[200, 300]]);
    for (let i = 1; i <= 8; i++) await touch('touchMove', [[200 + i * 10, 300 + i * 6]]);
    await touch('touchEnd', []);
    const c1 = await cam();
    check(c1.yaw > c0.yaw + 0.5, 'one-finger drag right orbits (yaw ' + c0.yaw.toFixed(2) + ' -> ' + c1.yaw.toFixed(2) + ')');
    check(c1.pitch > c0.pitch + 0.2, 'one-finger drag down tilts (pitch ' + c0.pitch.toFixed(2) + ' -> ' + c1.pitch.toFixed(2) + ')');
    check(Math.abs(c1.dist - c0.dist) < 1e-9, 'a one-finger drag does not zoom');
    check(c1.pointers === 0, 'all pointers released after touchEnd (' + c1.pointers + ' left)');

    // two fingers apart: zoom in
    await touch('touchStart', [[150, 350], [250, 350]]);
    for (let i = 1; i <= 6; i++) await touch('touchMove', [[150 - i * 10, 350], [250 + i * 10, 350]]);
    const c2 = await cam();
    check(c2.pointers === 2, 'two simultaneous touches are tracked (' + c2.pointers + ')');
    check(c2.dist < c1.dist * 0.7, 'pinching apart zooms in (dist ' + c1.dist.toFixed(2) + ' -> ' + c2.dist.toFixed(2) + ')');
    check(Math.abs(c2.yaw - c1.yaw) < 1e-9, 'a pinch does not orbit');
    // two fingers together: zoom out
    for (let i = 1; i <= 6; i++) await touch('touchMove', [[90 + i * 10, 350], [310 - i * 10, 350]]);
    await touch('touchEnd', []);
    const c3 = await cam();
    check(c3.dist > c2.dist * 1.3, 'pinching together zooms out (dist ' + c2.dist.toFixed(2) + ' -> ' + c3.dist.toFixed(2) + ')');

    // the existing mouse behaviour still works
    await page.mouse.move(200, 300); await page.mouse.down(); await page.mouse.move(260, 300, { steps: 5 }); await page.mouse.up();
    const c4 = await cam();
    check(c4.yaw > c3.yaw + 0.3, 'mouse drag still orbits (yaw ' + c3.yaw.toFixed(2) + ' -> ' + c4.yaw.toFixed(2) + ')');
    await page.mouse.wheel(0, 200);
    const c5 = await cam();
    check(c5.dist > c4.dist, 'mouse wheel still zooms (dist ' + c4.dist.toFixed(2) + ' -> ' + c5.dist.toFixed(2) + ')');

    // part Scale sliders (the "app builder" controls) still work
    await page.evaluate(() => { const el = document.querySelectorAll('#controls input[type=range]')[0]; el.value = '2'; el.dispatchEvent(new Event('input')); });
    check((await page.evaluate('window.__dino8Viewer.scales'))[0] === 2, 'Scale slider still drives window.__dino8Viewer.scales');
    check(errors.length === 0, 'still no errors after all gestures (' + JSON.stringify(errors) + ')');
  } finally {
    await browser.close();
  }
  if (failures) { console.log(failures + ' FAILED'); process.exit(1); }
  console.log('all passed');
}
main().catch(e => { console.error(e); process.exit(1); });
