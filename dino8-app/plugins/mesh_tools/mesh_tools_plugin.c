/* MeshTools: a Dino 8 sample plug-in demonstrating real mesh generation
 * through the C ABI.
 *
 *   - Command "TerrainMesh cols rows size amplitude seed" builds a
 *     procedural heightfield (a grid of quads with a deterministic
 *     pseudo-random height per vertex) and adds it to the document via
 *     add_mesh() - the full vertex/face-array path, not a stub.
 *   - Dino Flow node "Terrain Height" evaluates the same height function
 *     at a single (X, Y), so a graph can preview/query the terrain's
 *     shape (e.g. to place objects on it) before or instead of baking the
 *     mesh with the command.
 *   - Dino Flow node "Terrain Mesh" builds the same heightfield as the
 *     TerrainMesh command, but as a DINO8_FLOW_MESH *value* (via
 *     make_mesh_value()) instead of a baked document object - so it can
 *     feed straight into other Dino Flow nodes (native ones like Mesh Face
 *     Count/Join Meshes, or another plug-in's) for further processing in
 *     the graph, which a command-only geometry generator cannot do.
 *   - Dino Flow node "Plugin Mesh Info" takes a DINO8_FLOW_MESH *input*
 *     (from Terrain Mesh, a native mesh node, or any other source in the
 *     graph) and reports its face/vertex counts via mesh_face_count()/
 *     mesh_vertex_count() - proving a plug-in node can consume geometry
 *     produced elsewhere in the graph, not just create it.
 *
 * All three mesh-shaped entry points (command, Terrain Mesh, Plugin Mesh
 * Info) share BuildTerrainGrid() so the geometry is identical regardless of
 * which path built it.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dino8_plugin.h"

static const Dino8PluginApi* g_api = NULL;

/* A small deterministic hash -> [0,1), used instead of libc rand() so the
 * height field only depends on (seed, i, j), not call order. */
static double Hash01(int seed, int i, int j) {
  unsigned int h = (unsigned int)seed * 374761393u + (unsigned int)i * 668265263u + (unsigned int)j * 2147483647u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (double)(h % 1000000u) / 1000000.0;
}

static double TerrainHeight(double x, double y, double amplitude, double frequency, int seed) {
  const double base = amplitude * sin(frequency * x) * cos(frequency * y);
  /* A little per-cell jitter so the surface is not a perfectly smooth
   * sinusoid - still fully deterministic for a given seed. */
  const double jitter = (Hash01(seed, (int)floor(x), (int)floor(y)) - 0.5) * amplitude * 0.15;
  return base + jitter;
}

static int ParseIntArg(int argc, const char** argv, int index, int fallback) {
  if (index >= argc) return fallback;
  return atoi(argv[index]);
}
static double ParseNumArg(int argc, const char** argv, int index, double fallback) {
  if (index >= argc) return fallback;
  return atof(argv[index]);
}

/* Builds the vertex/face arrays for a `cols` x `rows` heightfield grid.
 * Caller owns the returned `*out_xyz`/`*out_faces` (free() them) and gets
 * the vertex/face counts back in `*out_vertex_count`/`*out_face_count`.
 * Returns 0 on success, non-zero on a bad size or an allocation failure. */
static int BuildTerrainGrid(int cols, int rows, double size, double amplitude, int seed, double** out_xyz, int** out_faces,
                            int* out_vertex_count, int* out_face_count) {
  if (cols < 1 || rows < 1 || cols > 400 || rows > 400) return 1;
  const int vcols = cols + 1, vrows = rows + 1;
  const int vertex_count = vcols * vrows;
  const int face_count = cols * rows;
  double* xyz = (double*)malloc((size_t)vertex_count * 3 * sizeof(double));
  int* faces = (int*)malloc((size_t)face_count * 4 * sizeof(int));
  if (!xyz || !faces) { free(xyz); free(faces); return 2; }

  for (int j = 0; j < vrows; ++j) {
    for (int i = 0; i < vcols; ++i) {
      const int v = j * vcols + i;
      const double x = (i - cols * 0.5) * size;
      const double y = (j - rows * 0.5) * size;
      const double z = TerrainHeight(x, y, amplitude, 1.0 / (size * 2.0 + 1e-9), seed);
      xyz[v * 3 + 0] = x;
      xyz[v * 3 + 1] = y;
      xyz[v * 3 + 2] = z;
    }
  }
  int f = 0;
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < cols; ++i, ++f) {
      const int a = j * vcols + i, b = a + 1, c = a + vcols + 1, d = a + vcols;
      faces[f * 4 + 0] = a; faces[f * 4 + 1] = b; faces[f * 4 + 2] = c; faces[f * 4 + 3] = d;
    }
  }
  *out_xyz = xyz;
  *out_faces = faces;
  *out_vertex_count = vertex_count;
  *out_face_count = face_count;
  return 0;
}

static int TerrainMeshCommand(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)ctx;
  const int cols = ParseIntArg(argc, argv, 1, 10);
  const int rows = ParseIntArg(argc, argv, 2, 10);
  const double size = ParseNumArg(argc, argv, 3, 2.0);
  const double amplitude = ParseNumArg(argc, argv, 4, 3.0);
  const int seed = ParseIntArg(argc, argv, 5, 1);

  double* xyz = NULL;
  int* faces = NULL;
  int vertex_count = 0, face_count = 0;
  const int rc = BuildTerrainGrid(cols, rows, size, amplitude, seed, &xyz, &faces, &vertex_count, &face_count);
  if (rc == 1) { g_api->print("TerrainMesh: cols/rows must be between 1 and 400"); return 1; }
  if (rc != 0) { g_api->print("TerrainMesh: out of memory"); return 1; }

  const unsigned long long id = g_api->add_mesh(xyz, vertex_count, faces, face_count);
  free(xyz);
  free(faces);
  if (id == 0) { g_api->print("TerrainMesh: add_mesh failed"); return 1; }

  char line[256];
  snprintf(line, sizeof line, "TerrainMesh: built a %dx%d heightfield (%d vertices, %d faces), object #%llu", cols, rows,
           vertex_count, face_count, id);
  g_api->print(line);
  return 0;
}

static int TerrainHeightEval(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs, int output_count,
                             char* error, int error_size, void* user_data) {
  (void)user_data;
  (void)error;
  (void)error_size;
  if (input_count < 5 || output_count < 1) return 1;
  const double x = inputs[0].number;
  const double y = inputs[1].number;
  const double amplitude = inputs[2].number;
  const double frequency = inputs[3].number;
  const int seed = (int)inputs[4].number;
  Dino8FlowValue out;
  memset(&out, 0, sizeof out);
  out.kind = DINO8_FLOW_NUMBER;
  out.number = TerrainHeight(x, y, amplitude, frequency, seed);
  outputs[0] = out;
  return 0;
}

/* Dino Flow node: same heightfield as TerrainMesh, but returned as a
 * DINO8_FLOW_MESH *value* (make_mesh_value) rather than baked into the
 * document - so it can be wired straight into other Dino Flow nodes. */
static int TerrainMeshEval(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs, int output_count,
                           char* error, int error_size, void* user_data) {
  (void)user_data;
  if (input_count < 5 || output_count < 1) return 1;
  const int cols = (int)inputs[0].number;
  const int rows = (int)inputs[1].number;
  const double size = inputs[2].number;
  const double amplitude = inputs[3].number;
  const int seed = (int)inputs[4].number;

  double* xyz = NULL;
  int* faces = NULL;
  int vertex_count = 0, face_count = 0;
  const int rc = BuildTerrainGrid(cols, rows, size, amplitude, seed, &xyz, &faces, &vertex_count, &face_count);
  if (rc != 0) {
    snprintf(error, (size_t)error_size, rc == 1 ? "Cols/Rows must be between 1 and 400" : "out of memory");
    return 1;
  }

  Dino8FlowValue out;
  memset(&out, 0, sizeof out);
  out.kind = DINO8_FLOW_MESH;
  out.geom = g_api->make_mesh_value(xyz, vertex_count, faces, face_count);
  free(xyz);
  free(faces);
  if (out.geom == 0) { snprintf(error, (size_t)error_size, "make_mesh_value failed"); return 1; }
  outputs[0] = out;
  return 0;
}

/* Dino Flow node: consumes a DINO8_FLOW_MESH *input* (from Terrain Mesh, a
 * native mesh node, or elsewhere in the graph) and reports its face/vertex
 * counts - proof that a plug-in node can receive real geometry, not just
 * produce it. */
static int PluginMeshInfoEval(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs, int output_count,
                              char* error, int error_size, void* user_data) {
  (void)user_data;
  if (input_count < 1 || output_count < 2) return 1;
  if (inputs[0].kind != DINO8_FLOW_MESH || inputs[0].geom == 0) {
    snprintf(error, (size_t)error_size, "Mesh input is empty");
    return 1;
  }
  Dino8FlowValue faces, verts;
  memset(&faces, 0, sizeof faces);
  memset(&verts, 0, sizeof verts);
  faces.kind = DINO8_FLOW_INTEGER;
  faces.number = g_api->mesh_face_count(inputs[0].geom);
  verts.kind = DINO8_FLOW_INTEGER;
  verts.number = g_api->mesh_vertex_count(inputs[0].geom);
  outputs[0] = faces;
  outputs[1] = verts;
  return 0;
}

/* Regression test for the plugin ABI's face-index bounds check: deliberately
 * sends a face that references a vertex past the end of a tiny 3-vertex
 * mesh. add_mesh() must reject it (return 0) rather than let a buggy plugin
 * read/write past the vertex array. */
static int TestBadMeshIndexCommand(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)argc; (void)argv; (void)ctx;
  const double xyz[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  const int bad_faces[4] = {0, 1, 2, 99};
  const unsigned long long id = g_api->add_mesh(xyz, 3, bad_faces, 1);
  g_api->print(id == 0 ? "TestBadMeshIndex: add_mesh correctly rejected an out-of-range face index"
                       : "TestBadMeshIndex: FAILED - add_mesh accepted an out-of-range face index");
  return id == 0 ? 0 : 1;
}

DINO8_PLUGIN_EXPORT int dino8_plugin_init(const Dino8PluginApi* api) {
  g_api = api;
  if (!api || api->api_version != DINO8_PLUGIN_API_VERSION) return 1;

  api->register_command("TerrainMesh", "TerrainMesh cols rows size amplitude seed - builds a procedural heightfield mesh.",
                        TerrainMeshCommand, NULL);
  api->register_command("TestBadMeshIndex", "TestBadMeshIndex - regression test: add_mesh must reject an out-of-range face index.",
                        TestBadMeshIndexCommand, NULL);

  Dino8FlowPort inputs[5];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "X"; inputs[0].kind = DINO8_FLOW_NUMBER;
  inputs[1].name = "Y"; inputs[1].kind = DINO8_FLOW_NUMBER;
  inputs[2].name = "Amplitude"; inputs[2].kind = DINO8_FLOW_NUMBER; inputs[2].default_number = 3.0;
  inputs[3].name = "Frequency"; inputs[3].kind = DINO8_FLOW_NUMBER; inputs[3].default_number = 0.3;
  inputs[4].name = "Seed"; inputs[4].kind = DINO8_FLOW_INTEGER; inputs[4].default_number = 1;

  Dino8FlowPort outputs[1];
  memset(outputs, 0, sizeof outputs);
  outputs[0].name = "Height"; outputs[0].kind = DINO8_FLOW_NUMBER;

  api->register_flow_node("Terrain Height", "Plug-ins", "Samples the same procedural heightfield TerrainMesh builds, at one (X, Y).",
                          inputs, 5, outputs, 1, TerrainHeightEval, NULL);

  Dino8FlowPort mesh_inputs[5];
  memset(mesh_inputs, 0, sizeof mesh_inputs);
  mesh_inputs[0].name = "Cols"; mesh_inputs[0].kind = DINO8_FLOW_INTEGER; mesh_inputs[0].default_number = 10;
  mesh_inputs[1].name = "Rows"; mesh_inputs[1].kind = DINO8_FLOW_INTEGER; mesh_inputs[1].default_number = 10;
  mesh_inputs[2].name = "Size"; mesh_inputs[2].kind = DINO8_FLOW_NUMBER; mesh_inputs[2].default_number = 2.0;
  mesh_inputs[3].name = "Amplitude"; mesh_inputs[3].kind = DINO8_FLOW_NUMBER; mesh_inputs[3].default_number = 3.0;
  mesh_inputs[4].name = "Seed"; mesh_inputs[4].kind = DINO8_FLOW_INTEGER; mesh_inputs[4].default_number = 1;
  Dino8FlowPort mesh_outputs[1];
  memset(mesh_outputs, 0, sizeof mesh_outputs);
  mesh_outputs[0].name = "Mesh"; mesh_outputs[0].kind = DINO8_FLOW_MESH;
  api->register_flow_node("Terrain Mesh", "Plug-ins",
                          "Builds the same heightfield as TerrainMesh, as a real Mesh value other Dino Flow nodes can consume "
                          "(not a baked document object).",
                          mesh_inputs, 5, mesh_outputs, 1, TerrainMeshEval, NULL);

  Dino8FlowPort info_inputs[1];
  memset(info_inputs, 0, sizeof info_inputs);
  info_inputs[0].name = "Mesh"; info_inputs[0].kind = DINO8_FLOW_MESH;
  Dino8FlowPort info_outputs[2];
  memset(info_outputs, 0, sizeof info_outputs);
  info_outputs[0].name = "Faces"; info_outputs[0].kind = DINO8_FLOW_INTEGER;
  info_outputs[1].name = "Vertices"; info_outputs[1].kind = DINO8_FLOW_INTEGER;
  api->register_flow_node("Plugin Mesh Info", "Plug-ins", "Reports the face/vertex count of any Mesh value wired into it.",
                          info_inputs, 1, info_outputs, 2, PluginMeshInfoEval, NULL);

  api->print("MeshTools sample plug-in loaded (TerrainMesh command, Terrain Height/Terrain Mesh/Plugin Mesh Info nodes).");
  return 0;
}

DINO8_PLUGIN_EXPORT const char* dino8_plugin_name(void) { return "MeshTools"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_version(void) { return "1.0.0"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_description(void) { return "Sample plug-in: procedural heightfield mesh generation."; }
