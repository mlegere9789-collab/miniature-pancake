// Real-browser verification for PARITY_MAP.md's "Cloud model viewer / app
// builder (ShapeDiver equivalent)" item (src/io/WebViewer.h) - genuinely
// opens the viewer HTML in a real, headless Chromium (via Playwright, this
// sandbox's pre-installed one - see PLAYWRIGHT_BROWSERS_PATH) and checks
// that it actually renders: no console errors, a WebGL context was
// created, and the page's own window.__dino8Viewer hook (set after the
// first successful draw - see WebViewer.cpp's own top comment) reports
// ready with the expected triangle count. This is NOT a ctest target (this
// repo's automated test toolchain is C++-only) - run it by hand after
// building dino8_test_web_viewer, which leaves the fixture this script
// opens:
//
//   cmake --build build --target dino8_test_web_viewer && ./build/dino8_test_web_viewer
//   node tests/web_viewer_browser_check.js build/web_viewer_browser_fixture.html
//
// Exits 0 and prints "all passed" on success, 1 otherwise - the same
// contract every C++ test binary in this repo already uses, so a human (or
// a future CI step) reads its result the same way.
const { chromium } = require('playwright');
const path = require('path');

async function main() {
  const fixture = process.argv[2];
  if (!fixture) {
    console.error('usage: node web_viewer_browser_check.js <path-to-fixture.html>');
    process.exit(1);
  }
  const fileUrl = 'file://' + path.resolve(fixture);

  let failures = 0;
  const check = (ok, what) => {
    console.log((ok ? 'ok  ' : 'FAIL') + ' ' + what);
    if (!ok) failures++;
  };

  const consoleErrors = [];
  const pageErrors = [];
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage();
    page.on('console', msg => { if (msg.type() === 'error') consoleErrors.push(msg.text()); });
    page.on('pageerror', err => pageErrors.push(String(err)));

    await page.goto(fileUrl);
    // window.__dino8Viewer is set synchronously during the inline <script>'s
    // own execution (see WebViewer.cpp) - no network/async wait needed, but
    // a short wait is harmless insurance against a slow headless renderer.
    await page.waitForFunction('window.__dino8Viewer !== undefined', { timeout: 5000 });
    const result = await page.evaluate('window.__dino8Viewer');

    check(consoleErrors.length === 0, 'no browser console errors while loading the viewer (' + JSON.stringify(consoleErrors) + ')');
    check(pageErrors.length === 0, 'no uncaught page exceptions while loading the viewer (' + JSON.stringify(pageErrors) + ')');
    check(result && result.error === null, 'the viewer reports no internal error: ' + (result && result.error));
    check(result && result.ready === true, 'the viewer reports ready:true (WebGL context created, first frame drawn)');
    check(result && result.triangleCount > 0, 'the viewer actually drew a nonzero number of triangles (' + (result && result.triangleCount) + ')');

    const hasCanvas = await page.evaluate("!!document.querySelector('canvas')");
    check(hasCanvas, 'a real <canvas> element exists in the rendered page');
    const webglWorks = await page.evaluate("!!document.querySelector('canvas').getContext('webgl')");
    check(webglWorks, "the canvas's WebGL context can still be acquired (not lost/unsupported)");

    // The "app builder" half's own real, if narrow, parametric control: a
    // #controls panel with one Scale <input type=range> per part, each
    // genuinely wired to window.__dino8Viewer.scales[i] (read by draw()
    // every frame, not a cosmetic label) - not just that the panel's own
    // markup exists, but that dragging a slider actually changes the
    // tracked value the renderer reads.
    const partCount = result && Array.isArray(result.scales) ? result.scales.length : -1;
    check(partCount > 0, 'window.__dino8Viewer.scales is a real array, one entry per part (' + partCount + ')');
    check(partCount > 0 && result.scales.every(s => s === 1), 'every part starts at scale 1 (no slider touched yet)');
    const sliderCount = await page.evaluate("document.querySelectorAll('#controls input[type=range]').length");
    check(sliderCount === partCount, '#controls has exactly one range slider per part (' + sliderCount + ' sliders, ' + partCount + ' parts)');
    // Drag the first slider to 2.0 and dispatch a real 'input' event, the
    // same event the browser fires on an actual mouse drag - not calling
    // the handler directly, so this exercises the real DOM wiring.
    await page.evaluate(() => {
      const el = document.querySelectorAll('#controls input[type=range]')[0];
      el.value = '2';
      el.dispatchEvent(new Event('input'));
    });
    const scalesAfterDrag = await page.evaluate('window.__dino8Viewer.scales');
    check(scalesAfterDrag[0] === 2, "dragging the first part's own slider to 2.0 updated window.__dino8Viewer.scales[0] to 2 (got " + scalesAfterDrag[0] + ')');
    check(scalesAfterDrag.slice(1).every(s => s === 1), 'every OTHER part stayed at scale 1 - the slider only rebuilds its own part, not every part at once');
  } finally {
    await browser.close();
  }

  if (failures) { console.log(failures + ' FAILED'); process.exit(1); }
  console.log('all passed');
}

main().catch(e => { console.error(e); process.exit(1); });
