// End-to-end test of the Dino 8 Touch PWA (dino8-app/touch/web) against a real
// headless Dino 8 session.
//
//   node touch_gestures.test.js /path/to/Dino8
//
// Starts   xvfb-run -a -s "-screen 0 1600x900x24" <Dino8> --serve 0 --serve-touch --touch-dir <web>
// loads the PWA in headless Chromium with touch emulation, drives it with
// synthetic touch events through the CDP Input.dispatchTouchEvent API and
// asserts MEASURED outcomes: camera orbit angle, pan distance, pinch zoom
// factor, double-tap zoom-extents, long-press selection (read back from the
// server), three-finger undo (object count read back from the server), a
// command typed into the PWA's command panel (object count and a Volume
// result read back through the server), the Move tool (bounding box read back
// from the server), measure, that unauthenticated requests are refused, and
// that the service worker lets the app load with the network blocked.
//
// Needs node + the `playwright` package with a Chromium already installed;
// exits 0 and prints "all passed" on success, 1 otherwise.
const { spawn } = require('child_process');
const path = require('path');
const os = require('os');
const fs = require('fs');

let chromium;
try { ({ chromium } = require('playwright')); } catch (e) { console.log('skip: playwright is not installed'); process.exit(0); }

const BIN = process.argv[2] || process.env.DINO8_BIN;
const WEB = path.resolve(__dirname, '..', 'web');
if (!BIN || !fs.existsSync(BIN)) { console.error('usage: node touch_gestures.test.js <path-to-Dino8>'); process.exit(1); }

let failures = 0;
const check = (ok, what) => { console.log((ok ? 'ok  ' : 'FAIL') + ' ' + what); if (!ok) failures++; };
const near = (a, b, tol) => Math.abs(a - b) <= tol;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function waitFor(fn, what, ms) {
  const end = Date.now() + (ms || 15000);
  for (;;) {
    let v; try { v = await fn(); } catch (e) { v = null; }
    if (v) return v;
    if (Date.now() > end) throw new Error('timed out waiting for ' + what);
    await sleep(100);
  }
}

function startApp(extra) {
  return new Promise((resolve, reject) => {
    const args = ['-a', '-s', '-screen 0 1600x900x24', BIN, '--serve', '0', '--serve-touch', '--touch-dir', WEB].concat(extra || []);
    const child = spawn('timeout', ['900', 'xvfb-run', ...args], { detached: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '', port = 0, token = '';
    const onData = (d) => {
      out += d.toString();
      const mp = /serve: listening on port (\d+)/.exec(out), mt = /#token=([0-9a-f]+)/.exec(out);
      if (mp) port = +mp[1];
      if (mt) token = mt[1];
      if (port && token) resolve({ child, port, token, log: () => out });
    };
    child.stdout.on('data', onData); child.stderr.on('data', onData);
    child.on('exit', (c) => { if (!(port && token)) reject(new Error('Dino 8 exited early (' + c + '):\n' + out)); });
    setTimeout(() => { if (!(port && token)) reject(new Error('Dino 8 did not start serving in 60s:\n' + out)); }, 60000);
  });
}

function runToExit(args) {
  return new Promise((resolve) => {
    const child = spawn('timeout', ['120', 'xvfb-run', '-a', '-s', '-screen 0 1600x900x24', BIN].concat(args), { detached: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let out = ''; child.stdout.on('data', (d) => out += d); child.stderr.on('data', (d) => out += d);
    child.on('exit', (code) => resolve({ code, out }));
  });
}

async function main() {
  const app = await startApp();
  const base = 'http://127.0.0.1:' + app.port;
  const auth = { Authorization: 'Bearer ' + app.token };
  const get = async (p, h) => fetch(base + p, { headers: h || auth });
  const post = async (p, body, h) => fetch(base + p, { method: 'POST', headers: Object.assign({ 'Content-Type': 'application/json' }, h || auth), body: JSON.stringify(body) });
  const state = async () => (await get('/touch/api/state')).json();
  let browser;
  try {
    // ---------------------------------------------------------- auth / server
    console.log('--- server routes and authentication');
    let r = await get('/touch/api/state', {});
    check(r.status === 401, 'GET /touch/api/state with no token is refused (401, got ' + r.status + ')');
    r = await get('/touch/api/state', { Authorization: 'Bearer not-the-token' });
    check(r.status === 401, 'GET /touch/api/state with a wrong token is refused (401, got ' + r.status + ')');
    r = await post('/touch/api/command', { line: 'Box 0,0,0 1,1,0 1' }, {});
    check(r.status === 401, 'POST /touch/api/command with no token is refused (401, got ' + r.status + ')');
    r = await get('/touch/api/scene', {});
    check(r.status === 401, 'GET /touch/api/scene with no token is refused (401, got ' + r.status + ')');
    r = await fetch(base + '/run', { method: 'POST', body: 'print("x")' });
    check(r.status === 401, 'the pre-existing POST /run is also gated by the same token once one is configured (got ' + r.status + ')');
    r = await get('/touch/api/ping');
    let j = await r.json();
    check(r.status === 200 && j.ok === true && j.objects === 0, 'GET /touch/api/ping with the token works on an empty document (objects=' + j.objects + ')');
    check((await state()).objects === 0, 'refused unauthenticated command left the document empty (objects=0)');
    r = await fetch(base + '/touch/', {});
    const html = await r.text();
    check(r.status === 200 && /<title>Dino 8 Touch<\/title>/.test(html), 'GET /touch/ serves the app shell without a token (static code only)');
    check(/default-src 'self'/.test(r.headers.get('content-security-policy') || '') && !/https?:\/\/(?!127)/.test(html.replace(/xmlns="[^"]*"/g, '')), 'the app shell carries a same-origin Content-Security-Policy and references no external http(s) URL (no CDN, fonts or trackers)');
    for (const f of ['app.js', 'app.css', 'sw.js', 'manifest.webmanifest', 'icon-192.png']) {
      r = await fetch(base + '/touch/' + f);
      check(r.status === 200, 'GET /touch/' + f + ' is served (' + r.status + ', ' + r.headers.get('content-type') + ')');
    }
    r = await fetch(base + '/touch/..%2f..%2fetc%2fpasswd');
    check(r.status === 404, 'a path-traversal request under /touch/ is refused (404, got ' + r.status + ')');
    r = await fetch(base + '/touch/api/state', { method: 'OPTIONS' });
    check(r.status === 204 && r.headers.get('access-control-allow-headers') && /Authorization/.test(r.headers.get('access-control-allow-headers')), 'OPTIONS /touch/api/state answers the CORS preflight without a token');
    r = await post('/touch/api/command', { line: 'RunScript /etc/hostname' });
    check(r.status === 403, 'script-running commands are blocked on the touch channel (403, got ' + r.status + ')');
    r = await post('/touch/api/command', { lines: ['Box 0,0,0 10,10,0 10'] });
    j = await r.json();
    check(r.status === 200 && j.ok && j.objects === 1 && j.objects_before === 0, 'POST /touch/api/command Box built an object (objects ' + j.objects_before + ' -> ' + j.objects + ')');
    r = await post('/touch/api/command', { lines: ['Box 30,0,0 40,10,0 10'] });
    j = await r.json();
    check(j.objects === 2, 'second Box -> 2 objects (undo target for the three-finger test)');
    r = await get('/touch/api/scene');
    j = await r.json();
    check(j.objects.length === 2 && j.objects[0].tri.length >= 36 * 3 && j.objects[0].tri.length % 9 === 0, 'GET /touch/api/scene returns tessellated triangles for both boxes (' + (j.objects[0] ? j.objects[0].tri.length / 9 : 0) + ' triangles in the first)');

    // ------------------------------------------------------- network exposure
    console.log('--- LAN exposure');
    const lanIp = [].concat(...Object.values(os.networkInterfaces())).filter((i) => i.family === 'IPv4' && !i.internal).map((i) => i.address)[0];
    const noTok = await runToExit(['--serve', '0', '--serve-lan']);
    check(noTok.code === 1 && /--serve-lan needs/.test(noTok.out), '--serve-lan without any token refuses to start (exit ' + noTok.code + ')');
    if (lanIp) {
      const viaLan = await fetch('http://' + lanIp + ':' + app.port + '/touch/api/ping', { headers: auth }).then((x) => x.status).catch(() => 'refused');
      check(viaLan === 'refused', 'the default server is bound to loopback only: ' + lanIp + ':' + app.port + ' -> ' + viaLan);
      const lanApp = await startApp(['--serve-lan']);
      try {
        const priv = /^(10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.|169\.254\.|127\.)/.test(lanIp);
        const ok = await fetch('http://' + lanIp + ':' + lanApp.port + '/touch/api/ping', { headers: { Authorization: 'Bearer ' + lanApp.token } }).then((x) => x.status).catch(() => 'dropped');
        const bad = await fetch('http://' + lanIp + ':' + lanApp.port + '/touch/api/ping').then((x) => x.status).catch(() => 'dropped');
        const loop = await fetch('http://127.0.0.1:' + lanApp.port + '/touch/api/ping', { headers: { Authorization: 'Bearer ' + lanApp.token } }).then((x) => x.status).catch(() => 'dropped');
        check(loop === 200, '--serve-lan: still reachable over loopback with the token (' + loop + ')');
        if (priv) check(ok === 200 && bad === 401, '--serve-lan: reachable at private address ' + lanIp + ' with the token (' + ok + ') and 401 without it (' + bad + ')');
        else check(ok === 'dropped' && bad === 'dropped', '--serve-lan: this host\'s only address ' + lanIp + ' is NOT a private range, so the peer filter drops the connection even with the token (' + ok + ', ' + bad + ')');
      } finally { try { process.kill(-lanApp.child.pid, 'SIGKILL'); } catch (e) { /* gone */ } }
    } else console.log('     info: no non-loopback IPv4 interface here; LAN reachability checks skipped');

    // ---------------------------------------------------------------- browser
    console.log('--- PWA in touch-emulating Chromium');
    browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--enable-webgl'] });
    const ctx = await browser.newContext({ viewport: { width: 1024, height: 768 }, hasTouch: true, isMobile: true, deviceScaleFactor: 1, serviceWorkers: 'allow' });
    const page = await ctx.newPage();
    const consoleErrors = [];
    page.on('console', (m) => { if (m.type() === 'error') consoleErrors.push(m.text()); });
    page.on('pageerror', (e) => consoleErrors.push('pageerror: ' + e.message));
    await page.goto(base + '/touch/#token=' + app.token);
    await waitFor(() => page.evaluate(() => window.__dino8Touch && window.__dino8Touch.ready), 'PWA boot');
    await waitFor(() => page.evaluate(() => window.__dino8Touch.mode === 'remote' && window.__dino8Touch.objectCount === 2), 'pairing link auto-connect + 2 objects');
    check(true, 'the #token= pairing link connected and loaded 2 objects from the desktop');
    check(await page.evaluate(() => window.__dino8Touch.glOk), 'a WebGL context was created');
    await waitFor(() => page.evaluate(() => window.__dino8Touch.frames > 5), 'rendering frames');
    check(!(await page.evaluate(() => location.hash)), 'the token was removed from the address bar after pairing');
    // The viewport must actually have drawn the geometry: read the framebuffer back.
    const lit = await page.evaluate(() => window.__dino8Touch.litPixels());
    check(lit > 500, 'the WebGL viewport drew the two boxes: ' + lit + ' sampled framebuffer pixels differ from the background');

    const cdp = await ctx.newCDPSession(page);
    const touch = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts.map((p, i) => ({ x: p[0], y: p[1], id: p[2] !== undefined ? p[2] : i })) });
    const cam = () => page.evaluate(() => window.__dino8Touch.camera());
    async function drag(starts, deltas, steps, holdMs) {
      await touch('touchStart', starts.map((s, i) => [s[0], s[1], i]));
      for (let k = 1; k <= steps; k++) {
        await touch('touchMove', starts.map((s, i) => [s[0] + deltas[i][0] * k / steps, s[1] + deltas[i][1] * k / steps, i]));
        await sleep(16);
      }
      if (holdMs) await sleep(holdMs);
      await touch('touchEnd', []);
    }

    // one-finger orbit
    let c0 = await cam(), perPx = await page.evaluate(() => window.__dino8Touch.orbitRadPerPx);
    await drag([[500, 600]], [[120, 40]], 8);
    let c1 = await cam();
    const dyaw = c1.yaw - c0.yaw, dpitch = c1.pitch - c0.pitch;
    check(near(dyaw, -120 * perPx, 0.03), 'one-finger orbit: dragging 120 px right turned the yaw by ' + dyaw.toFixed(3) + ' rad (expected ' + (-120 * perPx).toFixed(3) + ')');
    check(near(dpitch, 40 * perPx, 0.03), 'one-finger orbit: dragging 40 px down changed the pitch by ' + dpitch.toFixed(3) + ' rad (expected ' + (40 * perPx).toFixed(3) + ')');
    check(near(c1.dist, c0.dist, c0.dist * 0.001), 'one-finger orbit leaves the zoom distance alone (' + c0.dist.toFixed(3) + ' -> ' + c1.dist.toFixed(3) + ')');
    check(JSON.stringify(c1.target) === JSON.stringify(c0.target), 'one-finger orbit leaves the camera target alone');

    // two-finger pan (same spacing, so no zoom)
    c0 = await cam();
    const wpp = await page.evaluate(() => window.__dino8Touch.worldPerPixel());
    await drag([[400, 600], [600, 600]], [[90, -50], [90, -50]], 8);
    c1 = await cam();
    const moved = Math.hypot(c1.target[0] - c0.target[0], c1.target[1] - c0.target[1], c1.target[2] - c0.target[2]);
    const expectPan = Math.hypot(90, 50) * wpp;
    check(near(moved, expectPan, expectPan * 0.05), 'two-finger pan: target moved ' + moved.toFixed(3) + ' world units for a 90,-50 px drag (expected ' + expectPan.toFixed(3) + ' = ' + Math.hypot(90, 50).toFixed(1) + ' px x ' + wpp.toFixed(4) + ')');
    check(near(c1.dist, c0.dist, c0.dist * 0.01), 'two-finger pan keeps the zoom distance (' + c0.dist.toFixed(3) + ' -> ' + c1.dist.toFixed(3) + ')');
    check(near(c1.yaw, c0.yaw, 1e-6) && near(c1.pitch, c0.pitch, 1e-6), 'two-finger pan does not rotate the camera');

    // pinch zoom: fingers 200 px apart -> 400 px apart is a 2x zoom in
    c0 = await cam();
    await drag([[412, 600], [612, 600]], [[-100, 0], [100, 0]], 8);
    c1 = await cam();
    check(near(c1.dist / c0.dist, 0.5, 0.025), 'pinch zoom: fingers spread 200 -> 400 px, distance ratio ' + (c1.dist / c0.dist).toFixed(3) + ' (expected 0.5)');
    c0 = await cam();
    await drag([[312, 600], [712, 600]], [[100, 0], [-100, 0]], 8);
    c1 = await cam();
    check(near(c1.dist / c0.dist, 2.0, 0.1), 'pinch zoom: fingers closed 400 -> 200 px, distance ratio ' + (c1.dist / c0.dist).toFixed(3) + ' (expected 2.0)');

    // double-tap = zoom extents
    c0 = await cam();
    const box = await page.evaluate(() => { const a = window.__dino8Touch.objectBox(1), b = window.__dino8Touch.objectBox(2); return { min: a.min.map((v, i) => Math.min(v, b.min[i])), max: a.max.map((v, i) => Math.max(v, b.max[i])) }; });
    const center = box.min.map((v, i) => (v + box.max[i]) / 2);
    await touch('touchStart', [[900, 120, 0]]); await sleep(40); await touch('touchEnd', []);
    await sleep(90);
    await touch('touchStart', [[902, 121, 0]]); await sleep(40); await touch('touchEnd', []);
    await sleep(200);
    c1 = await cam();
    check(near(c1.target[0], center[0], 0.01) && near(c1.target[1], center[1], 0.01) && near(c1.target[2], center[2], 0.01), 'double-tap: camera target is the scene bounding-box centre ' + center.map((v) => v.toFixed(2)) + ' (got ' + c1.target.map((v) => v.toFixed(2)) + ')');
    const radius = Math.hypot(box.max[0] - box.min[0], box.max[1] - box.min[1], box.max[2] - box.min[2]) / 2;
    check(c1.dist > radius && c1.dist < radius * 6, 'double-tap: distance ' + c1.dist.toFixed(2) + ' frames the model (bounding radius ' + radius.toFixed(2) + ')');
    const shift = Math.hypot(c1.target[0] - c0.target[0], c1.target[1] - c0.target[1], c1.target[2] - c0.target[2]);
    check(shift > 1, 'double-tap: camera actually changed (target moved ' + shift.toFixed(2) + ' units back to the model centre)');

    // long-press select
    await sleep(150);
    const p1 = await page.evaluate(() => window.__dino8Touch.projectObject(1));
    const hit = await page.evaluate(([x, y]) => window.__dino8Touch.pick(x, y), [p1.x, p1.y]);
    check(hit === 1, 'picking at the screen centre of object 1 hits object 1 (ray-triangle test), got ' + hit);
    check((await page.evaluate(() => window.__dino8Touch.selected)).length === 0, 'nothing selected before the long-press');
    await touch('touchStart', [[p1.x, p1.y, 0]]); await sleep(700); await touch('touchEnd', []);
    await waitFor(() => page.evaluate(() => window.__dino8Touch.selected.length === 1), 'long-press selection');
    check(JSON.stringify(await page.evaluate(() => window.__dino8Touch.selected)) === '[1]', 'long-press selected object 1 in the app');
    check(JSON.stringify((await state()).selected) === '[1]', 'long-press selection was written to the desktop document (server selected = ' + JSON.stringify((await state()).selected) + ')');

    // three-finger tap = undo
    check((await state()).objects === 2, 'before the three-finger tap the desktop has 2 objects');
    await touch('touchStart', [[300, 300, 0], [400, 300, 1], [500, 300, 2]]); await sleep(120); await touch('touchEnd', []);
    await waitFor(async () => (await state()).objects === 1, 'undo from three-finger tap', 8000);
    check((await state()).objects === 1, 'three-finger tap sent Undo: the desktop document went from 2 to 1 objects');
    await waitFor(() => page.evaluate(() => window.__dino8Touch.objectCount === 1), 'PWA scene refresh after undo');
    check(true, 'the PWA scene refreshed to 1 object after the undo');

    // toolbar + command panel: type a command in the PWA
    console.log('--- toolbar, command panel, tools');
    const toolButtons = await page.$$eval('#toolbar button', (bs) => bs.map((b) => { const r = b.getBoundingClientRect(); return [b.textContent.trim(), Math.round(r.width), Math.round(r.height)]; }));
    check(toolButtons.length >= 10 && toolButtons.every((t) => t[1] >= 44 && t[2] >= 44), 'toolbar has ' + toolButtons.length + ' buttons, every one at least 44x44 CSS px: ' + toolButtons.map((t) => t[0].replace(/\s+/g, ' ') + ' ' + t[1] + 'x' + t[2]).join(', '));
    await page.tap('#btn-cmd');
    await waitFor(() => page.evaluate(() => document.getElementById('panel-cmd').classList.contains('open')), 'command panel open');
    await page.tap('#in-cmd');
    await page.fill('#in-cmd', 'Box 60,0,0 70,10,0 10');
    await page.keyboard.press('Enter');
    await waitFor(async () => (await state()).objects === 2, 'typed Box command reached the desktop', 8000);
    check((await state()).objects === 2, 'a Box typed in the PWA command panel changed the desktop document (objects 1 -> 2)');
    await waitFor(() => page.evaluate(() => window.__dino8Touch.objectCount === 2), 'PWA shows the new box');
    check(await page.evaluate(() => /Box/i.test(document.getElementById('cmd-log').textContent)), 'the command panel shows the command and its history output');
    await page.fill('#in-cmd', 'SelAll'); await page.keyboard.press('Enter');
    await waitFor(async () => (await state()).selected.length === 2, 'SelAll', 8000);
    await page.fill('#in-cmd', 'Volume'); await page.keyboard.press('Enter');
    await waitFor(() => page.evaluate(() => window.__dino8Touch.lastOutput.some((l) => /olume/.test(l))), 'Volume output');
    const volOut = await page.evaluate(() => window.__dino8Touch.lastOutput.join(' | '));
    const vm = /([0-9]+(?:\.[0-9]+)?)/.exec(volOut.replace(/^.*olume[^0-9]*/, ''));
    check(vm && near(parseFloat(vm[1]), 2000, 1), 'Volume of the two 10x10x10 boxes read back through the server: "' + volOut + '" (expected 2000)');
    await page.fill('#in-cmd', 'SelNone'); await page.keyboard.press('Enter');
    await sleep(300);
    await page.tap('#btn-cmd');   // close panel

    // Move tool: select object 1 (tap in select tool), switch to Move, drag
    const q1 = await page.evaluate(() => window.__dino8Touch.projectObject(1));
    await touch('touchStart', [[q1.x, q1.y, 0]]); await sleep(60); await touch('touchEnd', []);
    await waitFor(async () => JSON.stringify((await state()).selected) === '[1]', 'tap select', 5000);
    check(true, 'a tap in the Select tool selected object 1 on the desktop');
    const bboxOf = async (id) => { const o = await (await get('/objects')).json(); return o.find((x) => x.id === id).bbox; };
    const bb0 = await bboxOf(1);
    await page.tap('button[data-tool="move"]');
    check(await page.evaluate(() => window.__dino8Touch.tool === 'move'), 'the Move toolbar button activated the Move tool');
    const c2 = await cam();
    await drag([[q1.x, q1.y]], [[60, 0]], 8);
    await waitFor(async () => { const b = await bboxOf(1); return Math.abs(b.min[0] - bb0.min[0]) > 0.1 || Math.abs(b.min[1] - bb0.min[1]) > 0.1; }, 'Move to reach the desktop', 8000);
    const bb1 = await bboxOf(1);
    const mv = [bb1.min[0] - bb0.min[0], bb1.min[1] - bb0.min[1], bb1.min[2] - bb0.min[2]];
    const expMove = 60 * (await page.evaluate(() => window.__dino8Touch.worldPerPixel())) * 1;
    check(Math.hypot(mv[0], mv[1]) > 0.5 && near(mv[2], 0, 1e-6), 'Move tool: a 60 px drag moved the object by (' + mv.map((v) => v.toFixed(3)) + ') on the desktop (horizontal, z unchanged; ~' + expMove.toFixed(2) + ' units at screen scale)');
    check(!(await state()).active, 'no command is left waiting on the desktop after the Move');
    check(JSON.stringify(c2.target) === JSON.stringify((await cam()).target), 'dragging with the Move tool did not move the camera');

    // layers / display / measure
    await page.tap('button[data-tool="select"]');
    await page.tap('#btn-layers');
    const nLayers = await page.$$eval('#layer-list li', (l) => l.length);
    check(nLayers >= 1, 'Layers panel lists the desktop layers (' + nLayers + ')');
    await page.tap('#layer-list li button');
    await waitFor(async () => (await state()).layers[0].visible === false, 'layer hide', 5000);
    check((await state()).layers[0].visible === false, 'tapping Hide in the Layers panel hid the layer on the desktop');
    await waitFor(() => page.evaluate(() => window.__dino8Touch.objectCount === 0), 'hidden layer objects leave the scene', 8000);
    check(true, 'objects on the hidden layer disappeared from the PWA scene');
    await page.tap('#layer-list li button');
    await waitFor(() => page.evaluate(() => window.__dino8Touch.objectCount === 2), 'layer shown again', 8000);
    check(true, 'tapping Show brought them back (2 objects)');
    await page.tap('#btn-layers');
    await page.tap('#btn-display');
    for (const m of ['wireframe', 'ghosted', 'xray', 'shaded']) {
      await page.tap('#display-modes button[data-mode="' + m + '"]');
      check((await page.evaluate(() => window.__dino8Touch.display)) === m, 'display mode ' + m + ' selected from the Display panel');
      const litM = await page.evaluate(() => window.__dino8Touch.litPixels());
      check(litM > 200, 'display mode ' + m + ' renders geometry (' + litM + ' lit pixels)');
    }
    await page.tap('#btn-display');

    await page.tap('button[data-tool="measure"]');
    await page.evaluate(() => window.__dino8Touch.zoomExtents());
    await sleep(100);
    const P = await page.evaluate(() => [window.__dino8Touch.projectPoint([-6, -6, 0]), window.__dino8Touch.projectPoint([-6, 6, 0])]);
    await touch('touchStart', [[P[0].x, P[0].y, 0]]); await sleep(40); await touch('touchEnd', []);
    await sleep(500);
    await touch('touchStart', [[P[1].x, P[1].y, 0]]); await sleep(40); await touch('touchEnd', []);
    await waitFor(() => page.evaluate(() => window.__dino8Touch.measureResult > 0), 'measure result', 5000);
    const md = await page.evaluate(() => window.__dino8Touch.measureResult);
    check(near(md, 12, 0.5), 'Measure tool: two taps on the ground 12 units apart measured ' + md.toFixed(3));

    // Rotate tool: a 45 degree screen-space arc about the selection's centre
    const centreOf = (bb) => bb.min.map((v, i) => (v + bb.max[i]) / 2);
    const sizeOf = (bb) => bb.max.map((v, i) => v - bb.min[i]);
    await page.evaluate(() => window.__dino8Touch.zoomExtents());
    await sleep(100);
    let bbR0 = await bboxOf(1);
    check(near(sizeOf(bbR0)[0], 10, 0.05), 'before Rotate the box is 10 wide (' + sizeOf(bbR0)[0].toFixed(3) + ')');
    const cR = await page.evaluate((c) => window.__dino8Touch.projectPoint(c), centreOf(bbR0));
    await page.tap('button[data-tool="rotate"]');
    await drag([[cR.x + 100, cR.y]], [[100 * Math.cos(Math.PI / 4) - 100, 100 * Math.sin(Math.PI / 4)]], 10);
    await waitFor(async () => Math.abs(sizeOf(await bboxOf(1))[0] - 10) > 0.5, 'Rotate to reach the desktop', 8000);
    const bbR1 = await bboxOf(1);
    check(near(sizeOf(bbR1)[0], 10 * Math.SQRT2, 0.1) && near(sizeOf(bbR1)[1], 10 * Math.SQRT2, 0.1) && near(sizeOf(bbR1)[2], 10, 0.05),
      'Rotate tool: a 45 degree drag turned the box on the desktop (bbox ' + sizeOf(bbR1).map((v) => v.toFixed(3)) + ', expected 14.142 x 14.142 x 10)');
    check(centreOf(bbR1).every((v, i) => near(v, centreOf(bbR0)[i], 0.05)), 'Rotate keeps the box centred on its own centre');
    check(!(await state()).active, 'no command is left waiting on the desktop after the Rotate');

    // Scale tool: dragging up 138.63 px is a factor of exactly 2
    const bbS0 = await bboxOf(1);
    const cS = await page.evaluate((c) => window.__dino8Touch.projectPoint(c), centreOf(bbS0));
    await page.tap('button[data-tool="scale"]');
    await drag([[cS.x, cS.y + 160]], [[0, -200 * Math.LN2]], 10);
    await waitFor(async () => sizeOf(await bboxOf(1))[2] > 15, 'Scale to reach the desktop', 8000);
    const bbS1 = await bboxOf(1);
    check(near(sizeOf(bbS1)[2], 20, 0.1) && near(sizeOf(bbS1)[0] / sizeOf(bbS0)[0], 2, 0.01), 'Scale tool: dragging up 138.6 px doubled the box on the desktop (height 10 -> ' + sizeOf(bbS1)[2].toFixed(3) + ', width x' + (sizeOf(bbS1)[0] / sizeOf(bbS0)[0]).toFixed(3) + ')');
    check(!(await state()).active, 'no command is left waiting on the desktop after the Scale');

    // ------------------------------------------------------------ service worker
    console.log('--- offline cache');
    await waitFor(() => page.evaluate(() => navigator.serviceWorker && navigator.serviceWorker.controller !== null || window.__dino8Touch.swState === 'ready'), 'service worker active', 10000);
    const swInfo = await page.evaluate(async () => { const r = await navigator.serviceWorker.ready; const keys = await caches.keys(); const c = await caches.open(keys[0]); const reqs = await c.keys(); return { scope: r.scope, caches: keys, urls: reqs.map((q) => new URL(q.url).pathname) }; });
    check(swInfo.urls.some((u) => /app\.js$/.test(u)) && swInfo.urls.some((u) => /index\.html$|\/touch\/$/.test(u)) && swInfo.urls.some((u) => /app\.css$/.test(u)), 'service worker cached the app shell: ' + swInfo.urls.join(' '));
    check(!swInfo.urls.some((u) => /\/touch\/api\//.test(u)), 'the service worker did not cache any /touch/api/ model data');
    await page.reload();     // now controlled by the worker
    await waitFor(() => page.evaluate(() => window.__dino8Touch && window.__dino8Touch.ready), 'reload');
    await ctx.setOffline(true);
    const offlineReq = await fetch(base + '/touch/').then((x) => x.status).catch(() => -1);   // sanity: node itself can still reach the server
    const page2 = await ctx.newPage();
    const err2 = [];
    page2.on('pageerror', (e) => err2.push(e.message));
    let navOk = true;
    await page2.goto(base + '/touch/').catch(() => { navOk = false; });
    await waitFor(() => page2.evaluate(() => window.__dino8Touch && window.__dino8Touch.ready), 'offline load', 15000).catch(() => { navOk = false; });
    check(navOk, 'with the network blocked (context offline) the app still loads from the service worker cache (node-side server reachable=' + (offlineReq === 200) + ')');
    check(await page2.evaluate(() => navigator.serviceWorker.controller !== null), 'the offline page is controlled by the service worker');
    check(await page2.evaluate(() => document.title === 'Dino 8 Touch' && window.__dino8Touch.glOk), 'offline: app shell rendered with a WebGL context');
    await page2.tap('#btn-demo').catch(async () => { await page2.evaluate(() => document.getElementById('btn-demo').click()); });
    await waitFor(() => page2.evaluate(() => window.__dino8Touch.mode === 'local' && window.__dino8Touch.objectCount === 1), 'standalone demo', 5000);
    check(true, 'offline standalone mode: the demo model opened without any network');
    // the offline page's pairing attempt must fail gracefully, not crash
    await page2.evaluate(() => { document.getElementById('in-host').value = '127.0.0.1:' + location.port; document.getElementById('in-token').value = 'x'; });
    check(err2.length === 0, 'no uncaught page errors while offline (' + err2.join('; ') + ')');
    await page2.close();
    await ctx.setOffline(false);

    // wrong token through the UI shows an error
    const page3 = await ctx.newPage();
    await page3.goto(base + '/touch/#token=' + 'f'.repeat(32));
    await waitFor(() => page3.evaluate(() => window.__dino8Touch && window.__dino8Touch.ready), 'page3 boot');
    await waitFor(() => page3.evaluate(() => /token/i.test(document.getElementById('connect-msg').textContent)), 'wrong token message', 8000);
    check(await page3.evaluate(() => window.__dino8Touch.mode !== 'remote'), 'a wrong pairing token does not connect (message: "' + (await page3.evaluate(() => document.getElementById('connect-msg').textContent)) + '")');
    await page3.close();

    const real = consoleErrors.filter((e) => !/Failed to load resource|net::ERR|favicon/i.test(e));
    check(real.length === 0, 'no console/page errors during the online session' + (real.length ? ': ' + real.join(' | ') : ''));
  } finally {
    if (browser) await browser.close().catch(() => {});
    try { process.kill(-app.child.pid, 'SIGKILL'); } catch (e) { /* already gone */ }
  }
  console.log(failures ? failures + ' check(s) FAILED' : 'all passed');
  process.exit(failures ? 1 : 0);
}

main().catch((e) => { console.error('FAIL ' + (e && e.stack || e)); process.exit(1); });
