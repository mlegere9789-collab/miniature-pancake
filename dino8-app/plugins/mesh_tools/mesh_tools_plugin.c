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
 *
 * Both use the same TerrainHeight() function so the flow node's numbers
 * are a faithful description of what the command will build.
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

static int TerrainMeshCommand(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)ctx;
  const int cols = ParseIntArg(argc, argv, 1, 10);
  const int rows = ParseIntArg(argc, argv, 2, 10);
  const double size = ParseNumArg(argc, argv, 3, 2.0);
  const double amplitude = ParseNumArg(argc, argv, 4, 3.0);
  const int seed = ParseIntArg(argc, argv, 5, 1);
  if (cols < 1 || rows < 1 || cols > 400 || rows > 400) {
    g_api->print("TerrainMesh: cols/rows must be between 1 and 400");
    return 1;
  }

  const int vcols = cols + 1, vrows = rows + 1;
  const int vertex_count = vcols * vrows;
  const int face_count = cols * rows;
  double* xyz = (double*)malloc((size_t)vertex_count * 3 * sizeof(double));
  int* faces = (int*)malloc((size_t)face_count * 4 * sizeof(int));
  if (!xyz || !faces) { free(xyz); free(faces); g_api->print("TerrainMesh: out of memory"); return 1; }

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

DINO8_PLUGIN_EXPORT int dino8_plugin_init(const Dino8PluginApi* api) {
  g_api = api;
  if (!api || api->api_version != DINO8_PLUGIN_API_VERSION) return 1;

  api->register_command("TerrainMesh", "TerrainMesh cols rows size amplitude seed - builds a procedural heightfield mesh.",
                        TerrainMeshCommand, NULL);

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
  api->print("MeshTools sample plug-in loaded (TerrainMesh command, Terrain Height node).");
  return 0;
}

DINO8_PLUGIN_EXPORT const char* dino8_plugin_name(void) { return "MeshTools"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_version(void) { return "1.0.0"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_description(void) { return "Sample plug-in: procedural heightfield mesh generation."; }
