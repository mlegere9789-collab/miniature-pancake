// Dino 8 Touch - companion app for Dino 8 (Rhino-for-iPad equivalent).
// Plain JS, no libraries, no CDN, no network access except the Dino 8
// desktop session the user pairs with (or none at all in standalone mode).
'use strict';
(function () {
  var $ = function (id) { return document.getElementById(id); };

  // ---------------------------------------------------------------- state
  var ORBIT_RAD_PER_PX = 0.008;      // one-finger orbit speed (documented, asserted by the gesture test)
  var FOV = 45 * Math.PI / 180;
  var TAP_MOVE_PX = 10, LONG_PRESS_MS = 500, DOUBLE_TAP_MS = 350, DOUBLE_TAP_PX = 40, TAP_MAX_MS = 300;
  var cam = { target: [0, 0, 0], yaw: -0.8, pitch: 0.5, dist: 40 };
  var S = {
    mode: 'none',            // none | remote | local
    host: '', token: '',
    objects: [],             // {id,type,name,color,selected,pos:Float32Array,nrm,edges,lines,points,bbox,buf}
    layers: [], canUndo: false, canRedo: false, active: '', prompt: '',
    tool: 'select', display: 'shaded',
    lastSceneText: '', lastOutput: [], lastCommandResult: null,
    measure: [], preview: null, ready: false, swState: 'none', sceneVersion: 0, lastError: ''
  };

  function store(k, v) { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) { return null; } return null; }

  // ----------------------------------------------------------- vec / math
  function sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
  function add(a, b) { return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]; }
  function mul(a, s) { return [a[0] * s, a[1] * s, a[2] * s]; }
  function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
  function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
  function norm(a) { var l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }

  function camBasis() {
    var cp = Math.cos(cam.pitch);
    var dir = [cp * Math.cos(cam.yaw), cp * Math.sin(cam.yaw), Math.sin(cam.pitch)]; // target -> eye
    var eye = add(cam.target, mul(dir, cam.dist));
    var fwd = mul(dir, -1);
    var right = norm(cross(fwd, [0, 0, 1]));
    var up = cross(right, fwd);
    return { eye: eye, fwd: fwd, right: right, up: up };
  }
  function viewport() { var c = $('gl'); return { w: c.clientWidth || 1, h: c.clientHeight || 1 }; }

  function viewProj(aspect) {
    var b = camBasis();
    var f = 1 / Math.tan(FOV / 2), n = Math.max(cam.dist * 0.01, 0.01), fa = cam.dist * 200 + 1000;
    var P = [f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (fa + n) / (n - fa), -1, 0, 0, 2 * fa * n / (n - fa), 0];
    var V = [b.right[0], b.up[0], -b.fwd[0], 0, b.right[1], b.up[1], -b.fwd[1], 0, b.right[2], b.up[2], -b.fwd[2], 0,
      -dot(b.right, b.eye), -dot(b.up, b.eye), dot(b.fwd, b.eye), 1];
    return m4mul(P, V);
  }
  function m4mul(a, b) {
    var o = new Array(16);
    for (var c = 0; c < 4; c++) for (var r = 0; r < 4; r++) {
      var s = 0; for (var k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s;
    }
    return o;
  }
  function m4id() { return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; }

  function rayAt(px, py) {
    var v = viewport(), b = camBasis(), t = Math.tan(FOV / 2), asp = v.w / v.h;
    var x = (2 * px / v.w - 1) * t * asp, y = (1 - 2 * py / v.h) * t;
    return { o: b.eye, d: norm(add(b.fwd, add(mul(b.right, x), mul(b.up, y)))) };
  }
  function project(p) {
    var v = viewport(), b = camBasis(), t = Math.tan(FOV / 2), asp = v.w / v.h, q = sub(p, b.eye);
    var z = dot(q, b.fwd); if (z < 1e-6) return null;
    return { x: (dot(q, b.right) / (z * t * asp) * 0.5 + 0.5) * v.w, y: (0.5 - dot(q, b.up) / (z * t) * 0.5) * v.h, z: z };
  }
  function worldPerPixel() { return 2 * cam.dist * Math.tan(FOV / 2) / viewport().h; }

  // -------------------------------------------------------------- toast
  var toastTimer = 0;
  function toast(msg, ms) {
    var t = $('toast'); t.textContent = msg; t.classList.add('show');
    clearTimeout(toastTimer); toastTimer = setTimeout(function () { t.classList.remove('show'); }, ms || 1800);
  }
  function hint(msg) { $('hint').textContent = msg || ''; }

  // ------------------------------------------------------------- network
  function baseUrl() {
    var h = S.host.trim();
    if (!h) return '';
    if (!/^https?:\/\//i.test(h)) h = 'http://' + h;
    h = h.replace(/\/+$/, '');
    try { if (new URL(h).host === location.host && location.protocol.indexOf('http') === 0) return ''; } catch (e) { /* ignore */ }
    return h;
  }
  function api(path, opts) {
    opts = opts || {};
    var headers = { 'Authorization': 'Bearer ' + S.token };
    if (opts.body !== undefined) headers['Content-Type'] = 'application/json';
    return fetch(baseUrl() + '/touch/api/' + path, { method: opts.method || 'GET', headers: headers, body: opts.body, cache: 'no-store' })
      .then(function (r) {
        return r.text().then(function (txt) {
          var j = null; try { j = JSON.parse(txt); } catch (e) { /* not json */ }
          if (r.status === 401) throw new Error('Wrong pairing token');
          if (r.status === 403 && j && j.error) throw new Error(j.error);
          if (!r.ok) throw new Error((j && j.error) || ('HTTP ' + r.status));
          return j;
        });
      });
  }
  function setConn(kind, label) {
    $('conn-dot').className = 'dot ' + (kind === 'remote' ? 'on' : kind === 'local' ? 'local' : 'off');
    $('conn-label').textContent = label;
  }
  function setMsg(text, cls) { var m = $('connect-msg'); m.textContent = text || ''; m.className = 'msg ' + (cls || ''); }

  function connect() {
    S.host = $('in-host').value.trim(); S.token = $('in-token').value.trim();
    if (!S.host || !S.token) { setMsg('Enter the desktop address and the pairing token.', 'err'); return Promise.resolve(false); }
    setMsg('Connecting...');
    return api('ping').then(function (j) {
      S.mode = 'remote'; S.lastError = '';
      store('d8.host', S.host); store('d8.token', S.token);
      setConn('remote', 'Dino 8 ' + (j.version || '') + ' @ ' + S.host);
      setMsg('Connected.', 'ok');
      closePanels(); hint('');
      return refresh(true).then(function () { zoomExtents(); return true; });
    }).catch(function (e) {
      S.lastError = String(e.message || e);
      var m = S.lastError;
      if (location.protocol === 'https:' && S.host.indexOf('https') !== 0) m += ' (an https page cannot reach an http desktop: open the desktop-served http://host:port/touch/ page instead)';
      setMsg(m, 'err'); setConn('none', 'Not connected'); return false;
    });
  }
  function disconnect() {
    S.mode = 'none'; S.objects = []; S.layers = []; S.lastSceneText = ''; S.sceneVersion++;
    setConn('none', 'Not connected'); setMsg('Disconnected.'); updateUi(); store('d8.token', '');
  }

  var refreshing = false;
  function refresh(force) {
    if (S.mode !== 'remote' || refreshing) return Promise.resolve();
    refreshing = true;
    return Promise.all([api('state'), api('scene')]).then(function (r) {
      var st = r[0], sceneText = JSON.stringify(r[1]);
      S.layers = st.layers || []; S.canUndo = !!st.can_undo; S.canRedo = !!st.can_redo; S.active = st.active || ''; S.prompt = st.prompt || '';
      if (force || sceneText !== S.lastSceneText) { S.lastSceneText = sceneText; loadScene(r[1].objects || []); }
      S.serverObjectCount = st.objects;
      setConn('remote', $('conn-label').textContent.replace(/^Offline - /, ''));
      updateUi();
    }).catch(function (e) {
      S.lastError = String(e.message || e);
      $('conn-dot').className = 'dot off'; $('conn-label').textContent = 'Offline - ' + $('conn-label').textContent.replace(/^Offline - /, '');
    }).then(function () { refreshing = false; });
  }

  function sendCommand(lines) {
    if (S.mode !== 'remote') { toast('Connect to a Dino 8 session to run commands'); return Promise.resolve(null); }
    return api('command', { method: 'POST', body: JSON.stringify({ lines: lines }) }).then(function (j) {
      S.lastCommandResult = j; S.lastOutput = j.output || [];
      logOut(lines, j);
      return refresh(true).then(function () { return j; });
    }).catch(function (e) { toast(String(e.message || e), 3000); logOut(lines, { output: ['! ' + e.message] }); return null; });
  }
  function logOut(lines, j) {
    var log = $('cmd-log');
    log.textContent += lines.map(function (l) { return '> ' + l; }).join('\n') + '\n' + (j.output || []).join('\n') + (j.output && j.output.length ? '\n' : '');
    log.scrollTop = log.scrollHeight;
    $('cmd-prompt').textContent = j.prompt ? j.prompt : 'Command';
  }

  // --------------------------------------------------------------- scene
  var gl = null, progMesh = null, progLine = null, gridBuf = null;

  function bboxOf(arr) {
    var mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (var i = 0; i + 2 < arr.length; i += 3) for (var k = 0; k < 3; k++) { if (arr[i + k] < mn[k]) mn[k] = arr[i + k]; if (arr[i + k] > mx[k]) mx[k] = arr[i + k]; }
    return arr.length >= 3 ? { min: mn, max: mx } : null;
  }
  function mergeBox(a, b) { if (!a) return b; if (!b) return a; return { min: [Math.min(a.min[0], b.min[0]), Math.min(a.min[1], b.min[1]), Math.min(a.min[2], b.min[2])], max: [Math.max(a.max[0], b.max[0]), Math.max(a.max[1], b.max[1]), Math.max(a.max[2], b.max[2])] }; }

  function flatNormals(pos) {
    var n = new Float32Array(pos.length);
    for (var i = 0; i + 8 < pos.length; i += 9) {
      var a = [pos[i], pos[i + 1], pos[i + 2]], b = [pos[i + 3], pos[i + 4], pos[i + 5]], c = [pos[i + 6], pos[i + 7], pos[i + 8]];
      var nn = norm(cross(sub(b, a), sub(c, a)));
      for (var k = 0; k < 3; k++) { n[i + k * 3] = nn[0]; n[i + k * 3 + 1] = nn[1]; n[i + k * 3 + 2] = nn[2]; }
    }
    return n;
  }
  function triOutline(pos) {
    var o = new Float32Array(pos.length * 2);
    for (var i = 0, j = 0; i + 8 < pos.length; i += 9) {
      for (var e = 0; e < 3; e++) { var a = i + e * 3, b = i + ((e + 1) % 3) * 3; for (var k = 0; k < 3; k++) o[j++] = pos[a + k]; for (k = 0; k < 3; k++) o[j++] = pos[b + k]; }
    }
    return o;
  }
  // The desktop's default object/layer colour is black (it draws on white); on
  // this dark viewport a near-black colour would vanish, so lift it to a light grey.
  function displayColor(c) {
    c = c || [0.8, 0.8, 0.8];
    return Math.max(c[0], c[1], c[2]) < 0.28 ? [0.78, 0.81, 0.86] : c;
  }
  function makeObj(o) {
    var pos = new Float32Array(o.tri || []), edges = new Float32Array(o.edges || []), lines = new Float32Array(o.lines || []), pts = new Float32Array(o.points || []);
    var box = mergeBox(mergeBox(bboxOf(pos), bboxOf(lines)), mergeBox(bboxOf(pts), bboxOf(edges)));
    return { id: o.id, type: o.type, name: o.name || '', color: displayColor(o.color), selected: !!o.selected, locked: !!o.locked,
      pos: pos, nrm: flatNormals(pos), edges: edges, wire: edges.length ? edges : triOutline(pos), lines: lines, points: pts, bbox: box, buf: null };
  }
  function loadScene(list) {
    if (gl) S.objects.forEach(freeBufs);
    S.objects = list.map(makeObj); S.sceneVersion++;
    S.objects.forEach(function (o) { if (o.selected) { /* kept */ } });
    updateUi();
  }
  function freeBufs(o) { if (!o.buf || !gl) return; for (var k in o.buf) gl.deleteBuffer(o.buf[k]); o.buf = null; }
  function sceneBox(onlySelected) {
    var b = null;
    S.objects.forEach(function (o) { if (!onlySelected || o.selected) b = mergeBox(b, o.bbox); });
    return b;
  }
  function zoomExtents() {
    var b = sceneBox(false);
    if (!b) { cam.target = [0, 0, 0]; cam.dist = 40; return; }
    var c = mul(add(b.min, b.max), 0.5), r = Math.max(Math.hypot(b.max[0] - b.min[0], b.max[1] - b.min[1], b.max[2] - b.min[2]) / 2, 0.5);
    var v = viewport(), fit = Math.min(FOV, 2 * Math.atan(Math.tan(FOV / 2) * Math.min(1, v.w / v.h)));
    cam.target = c; cam.dist = r / Math.sin(fit / 2) * 1.15;
  }

  // ------------------------------------------------------------- picking
  function rayTri(o, d, a, b, c) {
    var e1 = sub(b, a), e2 = sub(c, a), p = cross(d, e2), det = dot(e1, p);
    if (Math.abs(det) < 1e-12) return -1;
    var inv = 1 / det, t0 = sub(o, a), u = dot(t0, p) * inv; if (u < 0 || u > 1) return -1;
    var q = cross(t0, e1), v = dot(d, q) * inv; if (v < 0 || u + v > 1) return -1;
    var t = dot(e2, q) * inv; return t > 1e-6 ? t : -1;
  }
  function rayBox(o, d, bx) {
    var tmin = 0, tmax = Infinity;
    for (var k = 0; k < 3; k++) {
      if (Math.abs(d[k]) < 1e-12) { if (o[k] < bx.min[k] || o[k] > bx.max[k]) return false; continue; }
      var t1 = (bx.min[k] - o[k]) / d[k], t2 = (bx.max[k] - o[k]) / d[k];
      if (t1 > t2) { var t = t1; t1 = t2; t2 = t; }
      tmin = Math.max(tmin, t1); tmax = Math.min(tmax, t2); if (tmin > tmax) return false;
    }
    return true;
  }
  function distSeg2(px, py, ax, ay, bx, by) {
    var vx = bx - ax, vy = by - ay, l = vx * vx + vy * vy, t = l ? Math.max(0, Math.min(1, ((px - ax) * vx + (py - ay) * vy) / l)) : 0;
    return Math.hypot(px - (ax + t * vx), py - (ay + t * vy));
  }
  function pickAt(px, py) {
    var r = rayAt(px, py), best = null, i, j;
    S.objects.forEach(function (ob) {
      if (!ob.bbox) return;
      var pad = { min: sub(ob.bbox.min, [worldPerPixel() * 8, worldPerPixel() * 8, worldPerPixel() * 8]), max: add(ob.bbox.max, [worldPerPixel() * 8, worldPerPixel() * 8, worldPerPixel() * 8]) };
      if (!rayBox(r.o, r.d, pad)) return;
      var p = ob.pos;
      for (i = 0; i + 8 < p.length; i += 9) {
        var t = rayTri(r.o, r.d, [p[i], p[i + 1], p[i + 2]], [p[i + 3], p[i + 4], p[i + 5]], [p[i + 6], p[i + 7], p[i + 8]]);
        if (t > 0 && (!best || t < best.t)) best = { id: ob.id, t: t, point: add(r.o, mul(r.d, t)) };
      }
      var segs = [ob.lines, ob.edges.length && !p.length ? ob.edges : null];
      segs.forEach(function (arr) {
        if (!arr) return;
        for (j = 0; j + 5 < arr.length; j += 6) {
          var a = project([arr[j], arr[j + 1], arr[j + 2]]), b = project([arr[j + 3], arr[j + 4], arr[j + 5]]);
          if (a && b && distSeg2(px, py, a.x, a.y, b.x, b.y) < 16) {
            var tt = (a.z + b.z) / 2; if (!best || tt < best.t) best = { id: ob.id, t: tt, point: add(r.o, mul(r.d, tt)) };
          }
        }
      });
      for (j = 0; j + 2 < ob.points.length; j += 3) {
        var pp = project([ob.points[j], ob.points[j + 1], ob.points[j + 2]]);
        if (pp && Math.hypot(pp.x - px, pp.y - py) < 18 && (!best || pp.z < best.t)) best = { id: ob.id, t: pp.z, point: [ob.points[j], ob.points[j + 1], ob.points[j + 2]] };
      }
    });
    return best;
  }
  function groundHit(px, py, z) {
    var r = rayAt(px, py); if (Math.abs(r.d[2]) < 1e-6) return null;
    var t = (z - r.o[2]) / r.d[2]; return t > 0 ? add(r.o, mul(r.d, t)) : null;
  }

  // ----------------------------------------------------------- selection
  function setSelection(ids, mode) {
    if (S.mode === 'remote') {
      return api('select', { method: 'POST', body: JSON.stringify({ ids: ids, mode: mode }) }).then(function (j) {
        var set = {}; (j.selected || []).forEach(function (i) { set[i] = true; });
        S.objects.forEach(function (o) { o.selected = !!set[o.id]; });
        S.lastSceneText = ''; updateUi();
      }).catch(function (e) { toast(String(e.message || e)); });
    }
    var cur = {}; S.objects.forEach(function (o) { if (o.selected) cur[o.id] = true; });
    if (mode === 'set' || mode === 'clear') cur = {};
    if (mode !== 'clear') ids.forEach(function (i) { if (mode === 'toggle') { if (cur[i]) delete cur[i]; else cur[i] = true; } else cur[i] = true; });
    S.objects.forEach(function (o) { o.selected = !!cur[o.id]; });
    updateUi(); return Promise.resolve();
  }
  function selectedIds() { return S.objects.filter(function (o) { return o.selected; }).map(function (o) { return o.id; }); }
  function selCentroid() { var b = sceneBox(true); return b ? mul(add(b.min, b.max), 0.5) : null; }

  function tapSelect(x, y, additive) {
    var h = pickAt(x, y);
    if (!h) { if (!additive) { setSelection([], 'clear'); hint('Selection cleared'); } return; }
    var o = S.objects.filter(function (q) { return q.id === h.id; })[0];
    setSelection([h.id], additive ? 'toggle' : 'set');
    hint((o ? o.type + ' ' : '') + '#' + h.id + (o && o.name ? ' "' + o.name + '"' : ''));
  }

  // ------------------------------------------------------------- measure
  function measureTap(x, y) {
    var h = pickAt(x, y), p = h ? h.point : groundHit(x, y, 0);
    if (!p) { toast('Tap on the model or the ground'); return; }
    if (S.measure.length >= 2) S.measure = [];
    S.measure.push(p);
    if (S.measure.length === 2) {
      var d = sub(S.measure[1], S.measure[0]);
      hint('Distance ' + Math.hypot(d[0], d[1], d[2]).toFixed(3) + '   dx ' + d[0].toFixed(3) + '  dy ' + d[1].toFixed(3) + '  dz ' + d[2].toFixed(3));
      S.measureResult = Math.hypot(d[0], d[1], d[2]);
    } else hint('Tap the second point');
  }

  // --------------------------------------------------------- transforms
  function fmt(n) { return (Math.abs(n) < 1e-9 ? 0 : n).toFixed(5).replace(/0+$/, '').replace(/\.$/, ''); }
  function fmtPt(p) { return fmt(p[0]) + ',' + fmt(p[1]) + ',' + fmt(p[2]); }
  function previewMatrix() {
    var pv = S.preview; if (!pv) return null;
    var c = pv.center, M = m4id();
    if (pv.type === 'move') { M[12] = pv.delta[0]; M[13] = pv.delta[1]; M[14] = pv.delta[2]; }
    else if (pv.type === 'rotate') {
      var cs = Math.cos(pv.angle), sn = Math.sin(pv.angle);
      M = [cs, sn, 0, 0, -sn, cs, 0, 0, 0, 0, 1, 0, c[0] - (cs * c[0] - sn * c[1]), c[1] - (sn * c[0] + cs * c[1]), 0, 1];
    } else if (pv.type === 'scale') {
      var f = pv.factor; M = [f, 0, 0, 0, 0, f, 0, 0, 0, 0, f, 0, c[0] * (1 - f), c[1] * (1 - f), c[2] * (1 - f), 1];
    }
    return M;
  }
  function beginTransform(x, y) {
    var c = selCentroid(); if (!c) { hint('Long-press an object to select it first'); return null; }
    var tool = S.tool;
    var t = { type: tool, center: c, x0: x, y0: y, h0: groundHit(x, y, c[2]) };
    if (tool === 'move' && !t.h0) return null;
    if (tool === 'rotate') { var pc = project(c); t.pc = pc; t.a0 = pc ? Math.atan2(y - pc.y, x - pc.x) : 0; }
    S.preview = { type: tool, center: c, delta: [0, 0, 0], angle: 0, factor: 1 };
    return t;
  }
  function updateTransform(t, x, y) {
    var pv = S.preview; if (!pv) return;
    if (t.type === 'move') {
      var h = groundHit(x, y, t.center[2]); if (h) pv.delta = sub(h, t.h0);
      hint('Move ' + fmtPt(pv.delta));
    } else if (t.type === 'rotate') {
      // screen-space angle about the object's projected centre; Z-up world, viewed from above => screen CCW is world CCW
      var a = t.pc ? Math.atan2(y - t.pc.y, x - t.pc.x) : 0, d = -(a - t.a0);
      while (d > Math.PI) d -= 2 * Math.PI; while (d < -Math.PI) d += 2 * Math.PI;
      pv.angle = d; hint('Rotate ' + (d * 180 / Math.PI).toFixed(1) + ' deg');
    } else if (t.type === 'scale') {
      pv.factor = Math.max(0.05, Math.min(20, Math.exp(-(y - t.y0) / 200))); hint('Scale ' + pv.factor.toFixed(3) + 'x');
    }
  }
  function commitTransform(t) {
    var pv = S.preview; S.preview = null; if (!pv) return;
    var c = pv.center, lines = null;
    if (t.type === 'move' && Math.hypot(pv.delta[0], pv.delta[1], pv.delta[2]) > 1e-6) lines = ['Move', fmtPt(c), fmtPt(add(c, pv.delta))];
    else if (t.type === 'rotate' && Math.abs(pv.angle) > 1e-4) lines = ['Rotate', fmtPt(c), fmt(pv.angle * 180 / Math.PI)];
    else if (t.type === 'scale' && Math.abs(pv.factor - 1) > 1e-3) lines = ['Scale', fmtPt(c), fmt(pv.factor)];
    if (!lines) return;
    if (S.mode === 'remote') {
      sendCommand(lines).then(function (j) {
        if (j && j.active) sendCommand(['Cancel']);   // never leave a half-fed command waiting on the desktop
      });
    } else { applyLocal(t.type, pv); }
  }
  function applyLocal(type, pv) {   // standalone mode: transform the local copy
    var M = (function () { S.preview = pv; var m = previewMatrix(); S.preview = null; return m; })();
    S.objects.forEach(function (o) {
      if (!o.selected) return;
      ['pos', 'edges', 'lines', 'points'].forEach(function (k) {
        var a = o[k]; for (var i = 0; i + 2 < a.length; i += 3) {
          var x = a[i], y = a[i + 1], z = a[i + 2];
          a[i] = M[0] * x + M[4] * y + M[8] * z + M[12]; a[i + 1] = M[1] * x + M[5] * y + M[9] * z + M[13]; a[i + 2] = M[2] * x + M[6] * y + M[10] * z + M[14];
        }
      });
      o.nrm = flatNormals(o.pos); o.wire = o.edges.length ? o.edges : triOutline(o.pos);
      o.bbox = mergeBox(mergeBox(bboxOf(o.pos), bboxOf(o.lines)), mergeBox(bboxOf(o.points), bboxOf(o.edges)));
      freeBufs(o);
    });
  }

  // ----------------------------------------------------------- gestures
  var pointers = {}, seq = null, lastTap = null, xform = null;
  function count() { return Object.keys(pointers).length; }
  function metrics() {
    var ids = Object.keys(pointers), n = ids.length, cx = 0, cy = 0;
    ids.forEach(function (k) { cx += pointers[k].x; cy += pointers[k].y; });
    cx /= n; cy /= n;
    var d = 0; if (n >= 2) d = Math.hypot(pointers[ids[0]].x - pointers[ids[1]].x, pointers[ids[0]].y - pointers[ids[1]].y);
    return { cx: cx, cy: cy, d: d };
  }
  function evPos(e) { var r = $('gl').getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; }

  function onDown(e) {
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    var p = evPos(e);
    try { $('gl').setPointerCapture(e.pointerId); } catch (x) { /* ignore */ }
    pointers[e.pointerId] = { x: p.x, y: p.y, sx: p.x, sy: p.y };
    var n = count();
    if (n === 1) {
      seq = { t0: performance.now(), maxN: 1, moved: false, x: p.x, y: p.y, long: false, base: null };
      seq.timer = setTimeout(function () {
        if (seq && !seq.moved && seq.maxN === 1 && count() === 1) { seq.long = true; tapSelect(seq.x, seq.y, true); if (navigator.vibrate) navigator.vibrate(15); }
      }, LONG_PRESS_MS);
      if (S.tool === 'move' || S.tool === 'rotate' || S.tool === 'scale') xform = beginTransform(p.x, p.y);
    } else if (seq) {
      seq.maxN = Math.max(seq.maxN, n); clearTimeout(seq.timer);
      if (xform) { S.preview = null; xform = null; }
    }
    if (seq) seq.base = metrics();
    e.preventDefault();
  }
  function onMove(e) {
    var pt = pointers[e.pointerId]; if (!pt || !seq) return;
    var p = evPos(e); pt.x = p.x; pt.y = p.y;
    var n = count();
    if (Math.hypot(p.x - pt.sx, p.y - pt.sy) > TAP_MOVE_PX) { seq.moved = true; clearTimeout(seq.timer); }
    if (!seq.moved && n === 1) return;
    var m = metrics(), b = seq.base;
    if (n === 1) {
      if (xform) { updateTransform(xform, p.x, p.y); }
      else {
        // first move past the tap threshold applies the whole displacement since touch-down
        cam.yaw -= (m.cx - b.cx) * ORBIT_RAD_PER_PX;
        cam.pitch = Math.max(-1.5, Math.min(1.5, cam.pitch + (m.cy - b.cy) * ORBIT_RAD_PER_PX));
      }
    } else if (n === 2) {
      var bs = camBasis(), wpp = worldPerPixel();
      cam.target = add(cam.target, add(mul(bs.right, -(m.cx - b.cx) * wpp), mul(bs.up, (m.cy - b.cy) * wpp)));
      if (b.d > 1 && m.d > 1) cam.dist = Math.max(0.01, Math.min(1e7, cam.dist * (b.d / m.d)));
    }
    seq.base = m;
    e.preventDefault();
  }
  function onUp(e) {
    if (!pointers[e.pointerId]) return;
    var p = evPos(e);
    delete pointers[e.pointerId];
    var n = count();
    if (n > 0) { if (seq) seq.base = metrics(); return; }
    if (!seq) return;
    clearTimeout(seq.timer);
    var s = seq; seq = null;
    var dur = performance.now() - s.t0;
    if (xform) { var t = xform; xform = null; if (s.moved) commitTransform(t); else S.preview = null; return; }
    if (s.moved || e.type === 'pointercancel') return;
    if (s.maxN === 3 && dur < 600) { hint('Undo'); doUndo(); return; }
    if (s.maxN === 1 && !s.long && dur < TAP_MAX_MS + 200) {
      var now = performance.now();
      // double tap = the second finger lands within DOUBLE_TAP_MS of the first lifting
      if (lastTap && s.t0 - lastTap.t < DOUBLE_TAP_MS &&Math.hypot(p.x - lastTap.x, p.y - lastTap.y) < DOUBLE_TAP_PX) {
        lastTap = null; zoomExtents(); hint('Zoom extents'); return;
      }
      lastTap = { t: now, x: p.x, y: p.y };
      if (S.tool === 'select') tapSelect(p.x, p.y, false);
      else if (S.tool === 'measure') measureTap(p.x, p.y);
    }
  }
  function doUndo() {
    if (S.mode === 'remote') sendCommand(['Undo']); else toast('Undo needs a desktop session');
  }

  // ---------------------------------------------------------------- GL
  var VS_MESH = 'attribute vec3 aPos;attribute vec3 aNrm;uniform mat4 uVP;uniform mat4 uM;varying vec3 vN;varying vec3 vP;' +
    'void main(){vec4 w=uM*vec4(aPos,1.0);vP=w.xyz;vN=mat3(uM)*aNrm;gl_Position=uVP*w;}';
  var FS_MESH = 'precision mediump float;uniform vec3 uCol;uniform float uAlpha;uniform vec3 uEye;varying vec3 vN;varying vec3 vP;' +
    'void main(){vec3 n=normalize(vN);vec3 v=normalize(uEye-vP);float d=abs(dot(n,v));float l=0.30+0.55*d+0.15*abs(n.z);gl_FragColor=vec4(uCol*l,uAlpha);}';
  var VS_LINE = 'attribute vec3 aPos;uniform mat4 uVP;uniform mat4 uM;uniform float uSize;void main(){gl_Position=uVP*uM*vec4(aPos,1.0);gl_PointSize=uSize;}';
  var FS_LINE = 'precision mediump float;uniform vec4 uCol;void main(){gl_FragColor=uCol;}';

  function compile(type, src) { var s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; }
  function program(vs, fs) {
    var p = gl.createProgram(); gl.attachShader(p, compile(gl.VERTEX_SHADER, vs)); gl.attachShader(p, compile(gl.FRAGMENT_SHADER, fs));
    gl.linkProgram(p); if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p)); return p;
  }
  function initGL() {
    var c = $('gl');
    gl = c.getContext('webgl', { antialias: true, alpha: false }) || c.getContext('experimental-webgl');
    if (!gl) { hint('WebGL is not available on this device'); return false; }
    progMesh = program(VS_MESH, FS_MESH); progLine = program(VS_LINE, FS_LINE);
    gridBuf = gl.createBuffer();
    return true;
  }
  function bufOf(o, key, data) {
    if (!o.buf) o.buf = {};
    if (!o.buf[key]) { o.buf[key] = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, o.buf[key]); gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW); }
    return o.buf[key];
  }
  function niceStep(x) { var p = Math.pow(10, Math.floor(Math.log10(x))), m = x / p; return (m < 2 ? 1 : m < 5 ? 2 : 5) * p; }

  var frames = 0;
  function renderOnce() {
    var c = $('gl'), dpr = Math.min(window.devicePixelRatio || 1, 2);
    var W = Math.round(c.clientWidth * dpr), H = Math.round(c.clientHeight * dpr);
    if (c.width !== W || c.height !== H) { c.width = W; c.height = H; var ov = $('overlay'); ov.width = W; ov.height = H; }
    if (gl && W > 0) {
      gl.viewport(0, 0, W, H);
      gl.clearColor(0.067, 0.086, 0.114, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL);
      var VP = viewProj(W / H), b = camBasis(), id = m4id(), pm = previewMatrix();
      drawGrid(VP, id);
      var mode = S.display;
      S.objects.forEach(function (o) {
        var M = (o.selected && pm) ? pm : id;
        var col = o.selected ? [0.25, 0.75, 1.0] : o.color;
        if (mode !== 'wireframe' && o.pos.length) {
          gl.useProgram(progMesh);
          var ap = gl.getAttribLocation(progMesh, 'aPos'), an = gl.getAttribLocation(progMesh, 'aNrm');
          gl.uniformMatrix4fv(gl.getUniformLocation(progMesh, 'uVP'), false, VP);
          gl.uniformMatrix4fv(gl.getUniformLocation(progMesh, 'uM'), false, M);
          gl.uniform3fv(gl.getUniformLocation(progMesh, 'uCol'), col);
          gl.uniform3fv(gl.getUniformLocation(progMesh, 'uEye'), b.eye);
          var alpha = mode === 'ghosted' ? 0.28 : mode === 'xray' ? 0.5 : 1;
          gl.uniform1f(gl.getUniformLocation(progMesh, 'uAlpha'), alpha);
          if (alpha < 1) { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false); if (mode === 'xray') gl.disable(gl.DEPTH_TEST); }
          gl.enable(gl.POLYGON_OFFSET_FILL); gl.polygonOffset(1, 1);
          gl.bindBuffer(gl.ARRAY_BUFFER, bufOf(o, 'pos', o.pos)); gl.enableVertexAttribArray(ap); gl.vertexAttribPointer(ap, 3, gl.FLOAT, false, 0, 0);
          gl.bindBuffer(gl.ARRAY_BUFFER, bufOf(o, 'nrm', o.nrm)); gl.enableVertexAttribArray(an); gl.vertexAttribPointer(an, 3, gl.FLOAT, false, 0, 0);
          gl.drawArrays(gl.TRIANGLES, 0, o.pos.length / 3);
          gl.disableVertexAttribArray(an);
          gl.disable(gl.POLYGON_OFFSET_FILL); gl.disable(gl.BLEND); gl.depthMask(true); gl.enable(gl.DEPTH_TEST);
        }
        var lineCol = o.selected ? [1, 0.85, 0.2, 1] : (mode === 'wireframe' ? o.color.concat(1) : [0.05, 0.07, 0.1, 1]);
        if (mode === 'wireframe' || o.edges.length) drawLines(o, 'wire', mode === 'wireframe' ? o.wire : o.edges, VP, M, lineCol, gl.LINES);
        if (o.lines.length) drawLines(o, 'lines', o.lines, VP, M, o.selected ? [1, 0.85, 0.2, 1] : o.color.concat(1), gl.LINES);
        if (o.points.length) drawLines(o, 'pts', o.points, VP, M, o.selected ? [1, 0.85, 0.2, 1] : o.color.concat(1), gl.POINTS, 8 * dpr);
      });
    }
    drawOverlay(dpr);
    frames++;
  }
  function draw() { renderOnce(); requestAnimationFrame(draw); }
  // Test hook: renders a frame and counts sampled pixels that differ from the clear colour.
  function litPixels() {
    if (!gl) return -1;
    renderOnce();
    var w = gl.drawingBufferWidth, h = gl.drawingBufferHeight, px = new Uint8Array(w * h * 4), n = 0;
    gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
    for (var i = 0; i < px.length; i += 4 * 5) if (Math.abs(px[i] - 17) + Math.abs(px[i + 1] - 22) + Math.abs(px[i + 2] - 29) > 40) n++;
    return n;
  }
  function drawLines(o, key, data, VP, M, col, mode, size) {
    gl.useProgram(progLine);
    var ap = gl.getAttribLocation(progLine, 'aPos');
    gl.uniformMatrix4fv(gl.getUniformLocation(progLine, 'uVP'), false, VP);
    gl.uniformMatrix4fv(gl.getUniformLocation(progLine, 'uM'), false, M);
    gl.uniform4fv(gl.getUniformLocation(progLine, 'uCol'), col);
    gl.uniform1f(gl.getUniformLocation(progLine, 'uSize'), size || 1);
    gl.bindBuffer(gl.ARRAY_BUFFER, bufOf(o, key + (data === o.wire ? 'W' : ''), data)); gl.enableVertexAttribArray(ap); gl.vertexAttribPointer(ap, 3, gl.FLOAT, false, 0, 0);
    gl.drawArrays(mode, 0, data.length / 3);
  }
  var gridKey = '';
  function drawGrid(VP, M) {
    var box = sceneBox(false), r = box ? Math.max(box.max[0] - box.min[0], box.max[1] - box.min[1], 1) : 40;
    var step = niceStep(r / 8), n = 12, key = step + ':' + (box ? box.min[0] + box.min[1] : 0);
    if (key !== gridKey) {
      var cx = box ? Math.round((box.min[0] + box.max[0]) / 2 / step) * step : 0, cy = box ? Math.round((box.min[1] + box.max[1]) / 2 / step) * step : 0, v = [];
      for (var i = -n; i <= n; i++) { v.push(cx + i * step, cy - n * step, 0, cx + i * step, cy + n * step, 0, cx - n * step, cy + i * step, 0, cx + n * step, cy + i * step, 0); }
      gl.bindBuffer(gl.ARRAY_BUFFER, gridBuf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(v), gl.STATIC_DRAW); gridKey = key; gridBuf.count = v.length / 3;
    }
    gl.useProgram(progLine);
    var ap = gl.getAttribLocation(progLine, 'aPos');
    gl.uniformMatrix4fv(gl.getUniformLocation(progLine, 'uVP'), false, VP);
    gl.uniformMatrix4fv(gl.getUniformLocation(progLine, 'uM'), false, M);
    gl.uniform4fv(gl.getUniformLocation(progLine, 'uCol'), [0.22, 0.28, 0.36, 1]);
    gl.bindBuffer(gl.ARRAY_BUFFER, gridBuf); gl.enableVertexAttribArray(ap); gl.vertexAttribPointer(ap, 3, gl.FLOAT, false, 0, 0);
    gl.drawArrays(gl.LINES, 0, gridBuf.count);
  }
  function drawOverlay(dpr) {
    var ov = $('overlay'), g = ov.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, ov.width, ov.height);
    if (S.measure.length) {
      g.strokeStyle = '#ffd34d'; g.fillStyle = '#ffd34d'; g.lineWidth = 2; g.font = '15px system-ui,sans-serif';
      var pts = S.measure.map(project);
      pts.forEach(function (p) { if (p) { g.beginPath(); g.arc(p.x, p.y, 6, 0, 6.3); g.fill(); } });
      if (pts.length === 2 && pts[0] && pts[1]) {
        g.beginPath(); g.moveTo(pts[0].x, pts[0].y); g.lineTo(pts[1].x, pts[1].y); g.stroke();
        var d = sub(S.measure[1], S.measure[0]), t = Math.hypot(d[0], d[1], d[2]).toFixed(3);
        g.fillText(t, (pts[0].x + pts[1].x) / 2 + 8, (pts[0].y + pts[1].y) / 2 - 8);
      }
    }
  }

  // ---------------------------------------------------------------- UI
  function closePanels() { document.querySelectorAll('.panel.open').forEach(function (p) { p.classList.remove('open'); }); }
  function togglePanel(id) { var p = $(id), open = p.classList.contains('open'); closePanels(); if (!open) p.classList.add('open'); }
  function updateUi() {
    var sel = selectedIds().length, tris = 0; S.objects.forEach(function (o) { tris += o.pos.length / 9; });
    $('stats').textContent = S.objects.length + ' obj  ' + sel + ' sel  ' + Math.round(tris) + ' tri';
    $('btn-undo').disabled = S.mode !== 'remote' || !S.canUndo;
    $('btn-redo').disabled = S.mode !== 'remote' || !S.canRedo;
    var ul = $('layer-list'); ul.innerHTML = '';
    S.layers.forEach(function (L) {
      var li = document.createElement('li'), sw = document.createElement('span'), nm = document.createElement('span'), bt = document.createElement('button');
      sw.className = 'sw'; sw.style.background = 'rgb(' + L.color.map(function (v) { return Math.round(v * 255); }).join(',') + ')';
      nm.className = 'nm'; nm.textContent = L.name; bt.textContent = L.visible ? 'Hide' : 'Show'; bt.setAttribute('data-layer', L.index);
      bt.onclick = function () { api('layer', { method: 'POST', body: JSON.stringify({ index: L.index, visible: !L.visible }) }).then(function () { return refresh(true); }); };
      li.appendChild(sw); li.appendChild(nm); li.appendChild(bt); ul.appendChild(li);
    });
  }
  function setTool(t) {
    S.tool = t; S.preview = null; xform = null; if (t !== 'measure') S.measure = [];
    document.querySelectorAll('#toolbar .tool').forEach(function (b) { b.classList.toggle('active', b.getAttribute('data-tool') === t); });
    var msgs = { select: 'Tap or long-press to select. One finger orbits, two fingers pan/zoom.', move: 'Drag to move the selection', rotate: 'Drag around the selection to rotate', scale: 'Drag up/down to scale the selection', measure: 'Tap two points to measure' };
    hint(msgs[t] || '');
  }
  function setDisplay(m) {
    S.display = m;
    document.querySelectorAll('#display-modes .mode').forEach(function (b) { b.classList.toggle('active', b.getAttribute('data-mode') === m); });
  }

  // ----------------------------------------------------- standalone files
  function parseObj(text) {
    var v = [], tri = [], lines = text.split(/\r?\n/);
    lines.forEach(function (ln) {
      var p = ln.trim().split(/\s+/);
      if (p[0] === 'v') v.push([+p[1], +p[2], +p[3]]);
      else if (p[0] === 'f') {
        var idx = p.slice(1).map(function (s) { var i = parseInt(s.split('/')[0], 10); return i < 0 ? v.length + i : i - 1; });
        for (var k = 1; k + 1 < idx.length; k++) [idx[0], idx[k], idx[k + 1]].forEach(function (i) { var q = v[i]; if (q) tri.push(q[0], q[1], q[2]); });
      }
    });
    return tri;
  }
  function parseStl(buf) {
    var tri = [], dv = new DataView(buf), head = new TextDecoder().decode(buf.slice(0, 5));
    var n = buf.byteLength >= 84 ? dv.getUint32(80, true) : 0;
    if (head !== 'solid' || 84 + n * 50 === buf.byteLength) {
      for (var i = 0; i < n; i++) for (var k = 0; k < 3; k++) { var o = 84 + i * 50 + 12 + k * 12; tri.push(dv.getFloat32(o, true), dv.getFloat32(o + 4, true), dv.getFloat32(o + 8, true)); }
    } else {
      var re = /vertex\s+(\S+)\s+(\S+)\s+(\S+)/g, txt = new TextDecoder().decode(buf), m;
      while ((m = re.exec(txt))) tri.push(+m[1], +m[2], +m[3]);
    }
    return tri;
  }
  function openLocal(name, tri) {
    if (!tri.length) { setMsg('No triangles found in ' + name, 'err'); return; }
    S.mode = 'local'; S.layers = []; S.lastSceneText = '';
    loadScene([{ id: 1, type: 'mesh', name: name, color: [0.75, 0.78, 0.82], tri: tri }]);
    setConn('local', 'Standalone: ' + name); closePanels(); zoomExtents(); hint('Opened ' + name + ' (' + Math.round(tri.length / 9) + ' triangles)');
  }
  function demoTriangles() {
    var c = [[0, 0, 0], [10, 0, 0], [10, 10, 0], [0, 10, 0], [0, 0, 10], [10, 0, 10], [10, 10, 10], [0, 10, 10]];
    var f = [[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4], [2, 3, 7, 6], [1, 2, 6, 5], [3, 0, 4, 7]], t = [];
    f.forEach(function (q) { [q[0], q[1], q[2], q[0], q[2], q[3]].forEach(function (i) { t.push(c[i][0], c[i][1], c[i][2]); }); });
    return t;
  }

  // -------------------------------------------------------------- wiring
  function wire() {
    var cv = $('gl');
    cv.addEventListener('pointerdown', onDown); cv.addEventListener('pointermove', onMove);
    cv.addEventListener('pointerup', onUp); cv.addEventListener('pointercancel', onUp);
    cv.addEventListener('wheel', function (e) { e.preventDefault(); cam.dist = Math.max(0.01, cam.dist * Math.exp(e.deltaY * 0.001)); }, { passive: false });
    cv.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    ['gesturestart', 'gesturechange'].forEach(function (n) { document.addEventListener(n, function (e) { e.preventDefault(); }); });
    document.querySelectorAll('#toolbar .tool').forEach(function (b) { b.onclick = function () { setTool(b.getAttribute('data-tool')); }; });
    $('btn-layers').onclick = function () { togglePanel('panel-layers'); };
    $('btn-display').onclick = function () { togglePanel('panel-display'); };
    $('btn-cmd').onclick = function () { togglePanel('panel-cmd'); };
    $('btn-connect').onclick = function () { togglePanel('panel-connect'); };
    $('btn-close-connect').onclick = closePanels;
    document.querySelectorAll('[data-close]').forEach(function (b) { b.onclick = closePanels; });
    document.querySelectorAll('#display-modes .mode').forEach(function (b) { b.onclick = function () { setDisplay(b.getAttribute('data-mode')); }; });
    $('btn-undo').onclick = function () { sendCommand(['Undo']); };
    $('btn-redo').onclick = function () { sendCommand(['Redo']); };
    $('btn-extents').onclick = function () { zoomExtents(); };
    $('btn-do-connect').onclick = connect;
    $('btn-disconnect').onclick = disconnect;
    $('btn-demo').onclick = function () { openLocal('demo cube', demoTriangles()); };
    $('in-file').onchange = function (e) {
      var f = e.target.files[0]; if (!f) return;
      var r = new FileReader();
      r.onload = function () { try { openLocal(f.name, /\.stl$/i.test(f.name) ? parseStl(r.result) : parseObj(new TextDecoder().decode(r.result))); } catch (x) { setMsg('Could not read ' + f.name + ': ' + x.message, 'err'); } };
      r.readAsArrayBuffer(f);
    };
    $('cmd-form').onsubmit = function (e) {
      e.preventDefault(); var inp = $('in-cmd'), line = inp.value.trim(); inp.value = '';
      if (line) sendCommand([line]);
    };
    window.addEventListener('resize', function () { /* canvas resized in draw() */ });
    window.addEventListener('online', function () { refresh(true); });
    setInterval(function () { if (S.mode === 'remote' && !seq && document.visibilityState !== 'hidden') refresh(false); }, 2500);
  }

  function registerSW() {
    if (!('serviceWorker' in navigator)) { S.swState = 'unsupported'; return; }
    navigator.serviceWorker.register('sw.js', { scope: './' }).then(function (reg) {
      S.swState = 'registered';
      var w = reg.installing || reg.waiting || reg.active;
      if (w) { w.addEventListener('statechange', function () { S.swState = w.state; }); S.swState = w.state; }
      navigator.serviceWorker.ready.then(function () { S.swState = 'ready'; });
    }).catch(function (e) { S.swState = 'error: ' + e.message; });
  }

  function boot() {
    if (!initGL()) S.glFailed = true;
    wire(); setTool('select'); updateUi();
    // Pairing link: http://host:port/touch/#token=XXXX (the fragment never goes to any server).
    var m = /[#&]token=([0-9A-Za-z._~-]+)/.exec(location.hash || '');
    var onDesktop = location.protocol.indexOf('http') === 0 && /\/touch\/?$/.test(location.pathname) && location.hostname;
    $('in-host').value = store('d8.host') || (onDesktop ? location.host : '');
    $('in-token').value = m ? m[1] : (store('d8.token') || '');
    if (m) { try { history.replaceState(null, '', location.pathname); } catch (e) { /* ignore */ } }
    setConn('none', 'Not connected');
    if ($('in-host').value && $('in-token').value) connect().then(function (ok) { if (!ok) togglePanel('panel-connect'); });
    else togglePanel('panel-connect');
    registerSW();
    S.ready = true;
    requestAnimationFrame(draw);
  }

  // --------------------------------------------------------- test hook
  window.__dino8Touch = {
    get ready() { return S.ready; }, get frames() { return frames; }, get mode() { return S.mode; }, get tool() { return S.tool; },
    get display() { return S.display; }, get swState() { return S.swState; }, get glOk() { return !!gl; },
    get objectCount() { return S.objects.length; }, get serverObjectCount() { return S.serverObjectCount; },
    get selected() { return selectedIds(); }, get lastError() { return S.lastError; }, get lastOutput() { return S.lastOutput; },
    get lastCommandResult() { return S.lastCommandResult; }, get measureResult() { return S.measureResult; },
    get sceneVersion() { return S.sceneVersion; }, get measureCount() { return S.measure.length; },
    camera: function () { return { yaw: cam.yaw, pitch: cam.pitch, dist: cam.dist, target: cam.target.slice() }; },
    orbitRadPerPx: ORBIT_RAD_PER_PX, fov: FOV,
    worldPerPixel: worldPerPixel,
    projectObject: function (id) {
      var o = S.objects.filter(function (q) { return q.id === id; })[0]; if (!o || !o.bbox) return null;
      var p = project(mul(add(o.bbox.min, o.bbox.max), 0.5)); return p ? { x: p.x, y: p.y } : null;
    },
    litPixels: litPixels,
    projectPoint: function (p) { var q = project(p); return q ? { x: q.x, y: q.y } : null; },
    objectBox: function (id) { var o = S.objects.filter(function (q) { return q.id === id; })[0]; return o ? o.bbox : null; },
    pick: function (x, y) { var h = pickAt(x, y); return h ? h.id : null; },
    refresh: function () { return refresh(true); }, sendCommand: sendCommand, zoomExtents: zoomExtents
  };

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})();
