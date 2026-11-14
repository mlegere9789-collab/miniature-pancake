// Dino 8 reference-output harness for Autodesk Forma (embedded-view extension).
// STATUS: UNTESTED against the real service. Method names and shapes come from the type declarations of the
// public Apache-2.0 package forma-embedded-view-sdk 0.96.0. Every scenario is wrapped, so errors are recorded.
// SAFETY: reads only, through the SDK in the owner's own signed-in session. Nothing is stored or sent elsewhere.
// Scenarios that trigger analyses (they add analysis records to the project) run only when the box is ticked AND
// the open project's name starts with "D8-REF" AND the user can edit. Nothing in the proposal is modified.
import { Forma } from "forma-embedded-view-sdk/auto";

const HARNESS_VERSION = "0.1.0";
const $ = (id) => document.getElementById(id);
const logEl = $("log");
const log = (m) => { logEl.textContent += "\n" + m; };
let dirHandle = null;
const results = { manifest: {}, measurements: {}, commands: {}, files: {} };

function stats(arr) {
  if (!arr || arr.length === 0) return { count: 0 };
  let min = Infinity, max = -Infinity, sum = 0, n = 0;
  for (let i = 0; i < arr.length; i++) { const v = arr[i]; if (Number.isNaN(v)) continue; if (v < min) min = v; if (v > max) max = v; sum += v; n++; }
  return { count: arr.length, finite: n, min, max, mean: n ? sum / n : null };
}
function gridSummary(g) {
  if (!g) return null;
  return { width: g.width, height: g.height, x0: g.x0, y0: g.y0, scale: g.scale, resolution: g.resolution, values: stats(g.grid), masked_cells: g.mask ? g.mask.reduce((a, b) => a + (b ? 1 : 0), 0) : null };
}
function meshStats(tri) {
  // tri: flat Float32Array x,y,z per vertex, 3 vertices per triangle
  const n = Math.floor(tri.length / 9); let area = 0, vol = 0;
  const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
  for (let t = 0; t < n; t++) {
    const o = t * 9; const a = [tri[o], tri[o + 1], tri[o + 2]], b = [tri[o + 3], tri[o + 4], tri[o + 5]], c = [tri[o + 6], tri[o + 7], tri[o + 8]];
    for (const p of [a, b, c]) for (let k = 0; k < 3; k++) { if (p[k] < mn[k]) mn[k] = p[k]; if (p[k] > mx[k]) mx[k] = p[k]; }
    const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    const cr = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
    area += 0.5 * Math.hypot(cr[0], cr[1], cr[2]);
    vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6;
  }
  return { triangles: n, area_m2: area, signed_volume_m3: vol, bbox_min: mn, bbox_max: mx };
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function waitForAnalysis(types, before, timeoutMs = 600000) {
  const t0 = Date.now(); const known = new Set(before.map((a) => a.analysisId));
  while (Date.now() - t0 < timeoutMs) {
    const list = await Forma.analysis.list({ analysisTypes: types });
    const fresh = list.filter((a) => !known.has(a.analysisId));
    const done = fresh.find((a) => a.status === "SUCCEEDED");
    if (done) return done;
    const failed = fresh.find((a) => a.status && a.status !== "SUCCEEDED" && a.status !== "RUNNING" && a.status !== "PENDING" && a.status !== "QUEUED");
    if (failed) return failed;
    await sleep(5000);
  }
  throw new Error("analysis did not finish within the time limit");
}
async function sandboxAllowed() {
  if (!$("sandbox").checked) return "skipped: sandbox box not ticked";
  const p = await Forma.project.get();
  if (!String(p.name || "").startsWith("D8-REF")) return "skipped: project name does not start with D8-REF";
  if (!(await Forma.getCanEdit())) return "skipped: no edit access";
  return null;
}

// Synthetic fixed test site for predictive wind: flat ground, one 30 m x 30 m block, 30 m tall, centred (500x500 px, 1.5 m px).
function syntheticHeightMaps() {
  const N = 500, size = 20, lo = 250 - size / 2, hi = 250 + size / 2;
  const terrain = new Array(N * N).fill(0), both = new Array(N * N).fill(0);
  for (let y = lo; y < hi; y++) for (let x = lo; x < hi; x++) both[y * N + x] = 255;
  return { terrainHeightArray: terrain, buildingAndTerrainHeightArray: both, minHeight: 0, maxHeight: 30 };
}
const ROSE = { height: 10, data: [0, 45, 90, 135, 180, 225, 270, 315].map((d) => ({ direction: d, probability: 0.125, weibull_parameters: { scale: 6.0, shape: 2.0 } })) };
async function wind(scale) {
  const grid = await Forma.predictiveAnalysis.predictWind({ heightMaps: syntheticHeightMaps(), windRose: ROSE, type: "comfort", roughness: 0.3, comfortScale: scale });
  return { input: "synthetic: flat ground, 30x30x30 m block, uniform 8-direction rose (probability 0.125, Weibull scale 6 shape 2 at 10 m), roughness 0.3",
           grid: { width: grid.width, height: grid.height, scale: grid.scale, values: stats(grid.grid) }, center_value: grid.grid[Math.floor(grid.height / 2) * grid.width + Math.floor(grid.width / 2)] };
}
async function analysisScenario(type, month, date) {
  const gate = await sandboxAllowed(); if (gate) return { skipped: gate };
  const paths = await Forma.geometry.getPathsByCategory({ category: "building" });
  const before = await Forma.analysis.list({ analysisTypes: [type] });
  const trig = type === "noise" ? await Forma.analysis.triggerNoise({ selectedElementPaths: paths }) : await Forma.analysis.triggerSun({ selectedElementPaths: paths, month, date });
  const done = await waitForAnalysis([type], before);
  const detail = type === "noise" ? await Forma.analysis.getNoiseAnalysis({ analysisId: done.analysisId }) : await Forma.analysis.getSunAnalysis({ analysisId: done.analysisId });
  const grid = await Forma.analysis.getGroundGrid({ analysis: done });
  return { building_paths: paths.length, triggered: trig.analysisId, status: done.status, parameters: detail.parameters, grid: gridSummary(grid) };
}

async function treeSummary(path, depth, maxDepth, acc) {
  const r = await Forma.elements.getByPath({ path });
  const el = r.element; acc.nodes++;
  const cat = el && el.properties && el.properties.category ? el.properties.category : "(none)"; acc.by_category[cat] = (acc.by_category[cat] || 0) + 1;
  if (depth < maxDepth && el && el.children) for (const ch of el.children.slice(0, 200)) await treeSummary(path === "root" ? "root/" + ch.key : path + "/" + ch.key, depth + 1, maxDepth, acc);
  return acc;
}

const SCENARIOS = [
  ["s01_project_info", async () => await Forma.project.get()],
  ["s02_session_and_settings", async () => ({ region: Forma.getRegion(), canEdit: await Forma.getCanEdit(), units: await Forma.getPresentationUnitSystem(), settings: await Forma.settings.get() })],
  ["s03_proposals", async () => ({ rootUrn: await Forma.proposal.getRootUrn(), id: await Forma.proposal.getId(), all: (await Forma.proposal.getAll()).map((p) => ({ name: p.properties && p.properties.name })) })],
  ["s04_element_tree_summary", async () => await treeSummary("root", 0, 3, { nodes: 0, by_category: {} })],
  ["s05_terrain_bbox_and_elevation_grid", async () => {
    const bb = await Forma.terrain.getBbox(); const out = [];
    for (let i = 0; i < 5; i++) for (let j = 0; j < 5; j++) { const x = bb.min.x + (bb.max.x - bb.min.x) * i / 4, y = bb.min.y + (bb.max.y - bb.min.y) * j / 4; out.push({ x, y, z: await Forma.terrain.getElevationAt({ x, y }) }); }
    return { bbox: bb, samples: out };
  }],
  ["s06_area_metrics_whole_proposal", async () => await Forma.areaMetrics.calculate({})],
  ["s07_building_footprints", async () => {
    const paths = await Forma.geometry.getPathsByCategory({ category: "building" }); const fp = [];
    for (const p of paths.slice(0, 100)) fp.push({ path: p, footprint: await Forma.geometry.getFootprint({ path: p }) });
    return { building_count: paths.length, footprints: fp };
  }],
  ["s08_scene_mesh_statistics", async () => meshStats(await Forma.geometry.getTriangles({}))],
  ["s09_sun_date_and_camera", async () => ({ sun_date: String(await Forma.sun.getDate()), camera: await Forma.camera.getCurrent() })],
  ["s10_existing_analyses", async () => (await Forma.analysis.list({})).map((a) => ({ type: a.analysisType, status: a.status, createdAt: a.createdAt }))],
  ["s11_wind_parameters_for_location", async () => await Forma.predictiveAnalysis.getWindParameters()],
  ["s12_wind_comfort_lawson_lddc", async () => await wind("lawson_lddc")],
  ["s13_wind_comfort_davenport", async () => await wind("davenport")],
  ["s14_wind_comfort_lawson", async () => await wind("lawson")],
  ["s15_wind_comfort_lawson_2001", async () => await wind("lawson_2001")],
  ["s16_wind_comfort_nen8100", async () => await wind("nen8100")],
  ["s17_wind_comfort_cstb", async () => await wind("cstb")],
  ["s18_sun_analysis_june21", async () => await analysisScenario("sun", 6, 21)],
  ["s19_sun_analysis_december21", async () => await analysisScenario("sun", 12, 21)],
  ["s20_noise_analysis", async () => await analysisScenario("noise")],
];

async function writeOut(name, text) {
  results.files[name] = text.length;
  if (dirHandle) { const fh = await dirHandle.getFileHandle(name, { create: true }); const w = await fh.createWritable(); await w.write(text); await w.close(); return true; }
  return false;
}
function downloadAll() {
  const blob = new Blob([JSON.stringify(results, null, 2)], { type: "application/json" });
  const a = document.createElement("a"); a.href = URL.createObjectURL(blob); a.download = "d8_forma_reference.json"; a.click();
}
function commandsList() {
  // The SDK object enumerates its own API sets; the product's menus are not enumerable (see README).
  const sets = Object.keys(Forma).filter((k) => typeof Forma[k] === "object" && Forma[k] !== null).sort();
  const methods = {};
  for (const k of sets) { try { methods[k] = Object.getOwnPropertyNames(Object.getPrototypeOf(Forma[k])).filter((m) => m !== "constructor").sort(); } catch (e) { methods[k] = "error: " + e.message; } }
  return { sdk_api_sets: sets, sdk_methods: methods, note: "Forma menu commands cannot be listed from the SDK; walk the UI manually." };
}

$("pick").onclick = async () => { try { dirHandle = await window.showDirectoryPicker({ mode: "readwrite" }); log("Output folder chosen."); } catch (e) { log("Folder not chosen (" + e.message + "). Use the download button after the run."); } };
$("save").onclick = downloadAll;
$("run").onclick = async () => {
  const t0 = Date.now(); log("Running...");
  for (const [id, fn] of SCENARIOS) {
    const t = Date.now();
    try { results.measurements[id] = { status: "ok", values: await fn(), seconds: (Date.now() - t) / 1000 }; }
    catch (e) { results.measurements[id] = { status: "error", error: String(e && e.message || e), stack: String(e && e.stack || ""), seconds: (Date.now() - t) / 1000 }; }
    log(id + ": " + results.measurements[id].status);
  }
  results.commands = commandsList();
  results.manifest = { harness: "dino8-reference-forma", harness_version: HARNESS_VERSION, tested_against_real_application: false, utc: new Date().toISOString(),
    program: "Autodesk Forma (embedded view)", sdk: "forma-embedded-view-sdk 0.96.0", user_agent: navigator.userAgent, platform: navigator.platform,
    project_id: Forma.getProjectId(), region: Forma.getRegion(), sandbox_enabled: $("sandbox").checked,
    scenario_count: SCENARIOS.length, scenarios_ok: Object.values(results.measurements).filter((m) => m.status === "ok").length,
    seconds: (Date.now() - t0) / 1000, credentials_stored: false };
  const wrote = await writeOut("measurements.json", JSON.stringify(results.measurements, null, 2))
    && await writeOut("commands.json", JSON.stringify(results.commands, null, 2))
    && await writeOut("manifest.json", JSON.stringify(results.manifest, null, 2));
  log(wrote ? "Results written to the chosen folder." : "Use 'Download results' to save one JSON file, then put it in your output folder.");
};
