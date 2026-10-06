// Builds the self-contained HTML + WebGL viewer ExportWebViewer writes -
// see io/WebViewer.h for the exact scope this closes (PARITY_MAP.md's
// "Cloud model viewer / app builder (ShapeDiver equivalent)" item) and what
// it deliberately does not (cloud hosting, exposed parametric inputs).
//
// Construction: every exportable object becomes one "part" - a flat color
// plus a triangle soup (positions/normals/indices), tessellated the same
// way ExportMeshFile's OBJ/STL path already tessellates a Brep/Surface/
// SubD (io/File3dm.cpp's ExportMeshFile - same per-kind tolerance choices,
// so a viewer and an OBJ export of the same document show the same shape).
// Geometry is already in world/document space (SceneObject's own fields
// are transformed in place - see SceneObject::Transform's own comment), so
// no per-part model matrix is needed, only one shared camera.
//
// The HTML itself is a single file: inline <style>/<script>, the part data
// baked in as plain JS array literals, and a hand-written WebGL1 renderer
// (one directional light + ambient term, flat per-part color via a
// uniform, per-vertex normals for the diffuse term) - no <script src=...>
// to any CDN or local file, so the exported file opens and renders
// identically from a local double-click or a web server, offline or on.
// Mouse-drag orbits, the wheel zooms; the initial camera frames the whole
// scene's bounding sphere so a document of any size opens nicely centered.
// After its first successful draw, the page's own script sets
// `window.__dino8Viewer = {ready: true, triangleCount: N, error: null}` (or
// `{ready: false, triangleCount: 0, error: "..."}` on a WebGL failure) -
// a deliberate, simple hook tests/test_web_viewer_browser_check.py drives
// through a real headless browser to confirm the page actually renders,
// not just that its embedded JSON looks right.
#include "io/WebViewer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

#include "geom/BrepMesher.h"

namespace dino8::app {

namespace {

struct Part {
  Color color;
  std::vector<float> positions;  // x,y,z per vertex
  std::vector<float> normals;    // x,y,z per vertex, parallel to positions
  std::vector<std::uint32_t> indices;  // 3 per triangle
};

// Appends `mesh`'s triangles (quads split into two triangles) to `part`,
// using ComputeVertexNormals() for lighting - the same geometry-derived
// normal convention every other exporter in this codebase already uses
// (see mesh.h's own comment on why normals are never stored, always
// derived).
void AppendMesh(const kernel::Mesh& mesh, Part& part) {
  const ON_Mesh& m = mesh.raw();
  const std::vector<kernel::Vector3d> normals = mesh.ComputeVertexNormals();
  const std::uint32_t base = static_cast<std::uint32_t>(part.positions.size() / 3);
  for (int i = 0; i < m.VertexCount(); ++i) {
    const ON_3dPoint v = m.Vertex(i);
    part.positions.push_back(static_cast<float>(v.x));
    part.positions.push_back(static_cast<float>(v.y));
    part.positions.push_back(static_cast<float>(v.z));
    const kernel::Vector3d n = (i < static_cast<int>(normals.size())) ? normals[static_cast<size_t>(i)] : kernel::Vector3d{0, 0, 1};
    part.normals.push_back(static_cast<float>(n.x));
    part.normals.push_back(static_cast<float>(n.y));
    part.normals.push_back(static_cast<float>(n.z));
  }
  for (int i = 0; i < m.FaceCount(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    part.indices.push_back(base + static_cast<std::uint32_t>(f.vi[0]));
    part.indices.push_back(base + static_cast<std::uint32_t>(f.vi[1]));
    part.indices.push_back(base + static_cast<std::uint32_t>(f.vi[2]));
    if (f.IsQuad()) {
      part.indices.push_back(base + static_cast<std::uint32_t>(f.vi[0]));
      part.indices.push_back(base + static_cast<std::uint32_t>(f.vi[2]));
      part.indices.push_back(base + static_cast<std::uint32_t>(f.vi[3]));
    }
  }
}

void WriteFloatArray(std::ostringstream& out, const std::vector<float>& v) {
  out << '[';
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) out << ',';
    out << v[i];
  }
  out << ']';
}

void WriteUintArray(std::ostringstream& out, const std::vector<std::uint32_t>& v) {
  out << '[';
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) out << ',';
    out << v[i];
  }
  out << ']';
}

}  // namespace

bool ExportWebViewer(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  std::vector<Part> parts;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;

    Part part;
    part.color = doc.EffectiveColor(o);
    if (o.kind == ObjectKind::Mesh && o.mesh) {
      AppendMesh(*o.mesh, part);
    } else if (o.kind == ObjectKind::Brep && o.brep) {
      BrepMeshOptions opt;
      opt.chord_tolerance = 0.01;
      AppendMesh(MeshBrepClosed(o.brep->raw(), opt), part);
    } else if (o.kind == ObjectKind::Surface && o.surface) {
      AppendMesh(o.surface->TessellateGridAdaptive(0.01), part);
    } else if (o.kind == ObjectKind::SubD && o.subd) {
      AppendMesh(o.subd->ToApproximateMesh(), part);
    } else {
      continue;  // Point/Curve/PointCloud: nothing a triangle-mesh viewer can show
    }
    if (!part.indices.empty()) parts.push_back(std::move(part));
  }

  if (parts.empty()) {
    error = "Nothing to export: " + std::string(selected_only ? "select" : "the document needs") +
            " at least one mesh, surface, polysurface or SubD";
    return false;
  }

  // Scene bounding sphere (every part's every vertex), to frame the
  // default camera regardless of the document's own scale.
  kernel::Point3d min{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
  kernel::Point3d max{-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
  for (const Part& p : parts) {
    for (size_t i = 0; i + 2 < p.positions.size(); i += 3) {
      min.x = std::min(min.x, static_cast<double>(p.positions[i]));
      min.y = std::min(min.y, static_cast<double>(p.positions[i + 1]));
      min.z = std::min(min.z, static_cast<double>(p.positions[i + 2]));
      max.x = std::max(max.x, static_cast<double>(p.positions[i]));
      max.y = std::max(max.y, static_cast<double>(p.positions[i + 1]));
      max.z = std::max(max.z, static_cast<double>(p.positions[i + 2]));
    }
  }
  const double cx = (min.x + max.x) / 2, cy = (min.y + max.y) / 2, cz = (min.z + max.z) / 2;
  const double dx = max.x - min.x, dy = max.y - min.y, dz = max.z - min.z;
  double radius = 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!(radius > 1e-9)) radius = 1.0;  // a single point / degenerate box: avoid a zero-distance camera

  std::ostringstream out;
  out << R"(<!doctype html>
<html><head><meta charset="utf-8"><title>Dino 8 model viewer</title>
<style>
  html,body{margin:0;height:100%;background:#2b2e33;overflow:hidden}
  canvas{display:block;width:100%;height:100%}
  #hint{position:fixed;left:8px;bottom:8px;color:#aaa;font:12px sans-serif;user-select:none}
</style></head>
<body>
<canvas id="c"></canvas>
<div id="hint">drag to orbit &middot; wheel to zoom</div>
<script>
const PARTS = [
)";
  for (size_t pi = 0; pi < parts.size(); ++pi) {
    const Part& p = parts[pi];
    out << (pi ? ",{" : "{");
    out << "color:[" << p.color.r << ',' << p.color.g << ',' << p.color.b << "],positions:";
    WriteFloatArray(out, p.positions);
    out << ",normals:";
    WriteFloatArray(out, p.normals);
    out << ",indices:";
    WriteUintArray(out, p.indices);
    out << "}\n";
  }
  out << "];\n";
  out << "const CENTER=[" << cx << ',' << cy << ',' << cz << "];\nconst RADIUS=" << radius << ";\n";

  out << R"(
const canvas = document.getElementById('c');
const gl = canvas.getContext('webgl');
window.__dino8Viewer = {ready:false, triangleCount:0, error:null};
if (!gl) {
  window.__dino8Viewer.error = 'WebGL not available';
} else {
try {
  // WebGL1 only allows 16-bit (UNSIGNED_SHORT) element indices unless this
  // extension is enabled - near-universal in practice (every evergreen
  // browser, including headless Chromium), but checked explicitly rather
  // than assumed, since a document with over 65535 vertices in one part
  // would otherwise silently draw garbage instead of failing loudly.
  if (!gl.getExtension('OES_element_index_uint')) throw new Error('OES_element_index_uint not supported');
  function compile(type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    return s;
  }
  const vsSrc = 'attribute vec3 aPos; attribute vec3 aNorm; uniform mat4 uMVP; uniform mat4 uModel; varying vec3 vNorm;' +
                'void main(){ vNorm = mat3(uModel) * aNorm; gl_Position = uMVP * vec4(aPos, 1.0); }';
  const fsSrc = 'precision mediump float; varying vec3 vNorm; uniform vec3 uColor;' +
                'void main(){ vec3 n = normalize(vNorm); vec3 light = normalize(vec3(0.5, 0.7, 1.0));' +
                'float diff = max(dot(n, light), 0.0); vec3 lit = uColor * (0.35 + 0.65 * diff); gl_FragColor = vec4(lit, 1.0); }';
  const prog = gl.createProgram();
  gl.attachShader(prog, compile(gl.VERTEX_SHADER, vsSrc));
  gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, fsSrc));
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
  const aPos = gl.getAttribLocation(prog, 'aPos');
  const aNorm = gl.getAttribLocation(prog, 'aNorm');
  const uMVP = gl.getUniformLocation(prog, 'uMVP');
  const uModel = gl.getUniformLocation(prog, 'uModel');
  const uColor = gl.getUniformLocation(prog, 'uColor');

  const buffers = PARTS.map(p => {
    const posBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(p.positions), gl.STATIC_DRAW);
    const normBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, normBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(p.normals), gl.STATIC_DRAW);
    const idxBuf = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, idxBuf);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array(p.indices), gl.STATIC_DRAW);
    return {posBuf, normBuf, idxBuf, count: p.indices.length, color: p.color};
  });

  // Minimal 4x4 matrix helpers - this file deliberately has no matrix
  // library dependency, only the handful of operations a static orbit
  // camera actually needs.
  function perspective(fovy, aspect, near, far) {
    const f = 1 / Math.tan(fovy / 2);
    return [f/aspect,0,0,0, 0,f,0,0, 0,0,(far+near)/(near-far),-1, 0,0,(2*far*near)/(near-far),0];
  }
  function lookAt(eye, center, up) {
    function sub(a,b){return [a[0]-b[0],a[1]-b[1],a[2]-b[2]];}
    function norm(a){const l=Math.hypot(a[0],a[1],a[2])||1;return [a[0]/l,a[1]/l,a[2]/l];}
    function cross(a,b){return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]];}
    const z = norm(sub(eye,center)); const x = norm(cross(up,z)); const y = cross(z,x);
    return [x[0],y[0],z[0],0, x[1],y[1],z[1],0, x[2],y[2],z[2],0,
            -(x[0]*eye[0]+x[1]*eye[1]+x[2]*eye[2]), -(y[0]*eye[0]+y[1]*eye[1]+y[2]*eye[2]), -(z[0]*eye[0]+z[1]*eye[1]+z[2]*eye[2]), 1];
  }
  function mul(a,b){
    const r = new Array(16).fill(0);
    for (let c=0;c<4;c++) for (let row=0;row<4;row++) for (let k=0;k<4;k++) r[c*4+row]+=a[k*4+row]*b[c*4+k];
    return r;
  }

  let yaw = 0.6, pitch = 0.5, dist = RADIUS * 3.0;
  let dragging = false, lastX = 0, lastY = 0;
  canvas.addEventListener('mousedown', e => { dragging = true; lastX = e.clientX; lastY = e.clientY; });
  window.addEventListener('mouseup', () => { dragging = false; });
  window.addEventListener('mousemove', e => {
    if (!dragging) return;
    yaw += (e.clientX - lastX) * 0.01;
    pitch = Math.max(-1.5, Math.min(1.5, pitch + (e.clientY - lastY) * 0.01));
    lastX = e.clientX; lastY = e.clientY;
  });
  canvas.addEventListener('wheel', e => {
    dist = Math.max(RADIUS * 0.05, Math.min(RADIUS * 50, dist * (e.deltaY > 0 ? 1.1 : 0.9)));
    e.preventDefault();
  }, {passive:false});

  function resize() {
    canvas.width = canvas.clientWidth || 800;
    canvas.height = canvas.clientHeight || 600;
  }
  function draw() {
    resize();
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.enable(gl.DEPTH_TEST);
    gl.clearColor(0.169, 0.180, 0.200, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    const eye = [CENTER[0] + dist*Math.cos(pitch)*Math.sin(yaw), CENTER[1] + dist*Math.sin(pitch), CENTER[2] + dist*Math.cos(pitch)*Math.cos(yaw)];
    const view = lookAt(eye, CENTER, [0,1,0]);
    const proj = perspective(Math.PI/4, canvas.width/Math.max(1,canvas.height), Math.max(0.01, dist*0.001), dist*100 + RADIUS*10);
    const mvp = mul(proj, view);
    const model = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];

    gl.useProgram(prog);
    gl.uniformMatrix4fv(uMVP, false, mvp);
    gl.uniformMatrix4fv(uModel, false, model);
    for (const b of buffers) {
      gl.uniform3f(uColor, b.color[0], b.color[1], b.color[2]);
      gl.bindBuffer(gl.ARRAY_BUFFER, b.posBuf);
      gl.enableVertexAttribArray(aPos);
      gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, b.normBuf);
      gl.enableVertexAttribArray(aNorm);
      gl.vertexAttribPointer(aNorm, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, b.idxBuf);
      gl.drawElements(gl.TRIANGLES, b.count, gl.UNSIGNED_INT, 0);
    }
    requestAnimationFrame(draw);
  }
  draw();
  window.__dino8Viewer.ready = true;
  window.__dino8Viewer.triangleCount = PARTS.reduce((n,p)=>n+p.indices.length/3, 0);
} catch (e) {
  window.__dino8Viewer.error = String(e && e.message ? e.message : e);
}
}
</script>
</body></html>
)";

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "Could not write " + path;
    return false;
  }
  file << out.str();
  if (!file) {
    error = "Could not write " + path;
    return false;
  }
  return true;
}

}  // namespace dino8::app
