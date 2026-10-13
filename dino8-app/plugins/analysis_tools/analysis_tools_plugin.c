/* AnalysisTools: a Dino 8 sample plug-in demonstrating multi-object,
 * read-and-write command I/O through the C ABI (distinct from the
 * geometry-creation examples in MeshTools/CurveTools).
 *
 *   - Command "BBoxReport" reads the current selection (get_selection()),
 *     reads each object's world bounding box (get_object_bbox()), prints
 *     a per-object report, and writes the computed box volume back onto
 *     each object as user text (set_user_text()) - selection in, geometry
 *     query, document write, all in one command.
 *   - Dino Flow node "Fibonacci Sphere Point" is a small generative-
 *     geometry algorithm (not a passthrough): given an index and a count,
 *     it places point `index` of an even Fibonacci-sphere distribution of
 *     `count` points on a sphere of the given radius - the same
 *     algorithm used for even point sampling in lighting/ambient-
 *     occlusion and antenna-placement problems.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dino8_plugin.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const Dino8PluginApi* g_api = NULL;

static int BBoxReportCommand(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)argc;
  (void)argv;
  (void)ctx;
  unsigned long long ids[256];
  const int n = g_api->get_selection(ids, 256);
  if (n <= 0) {
    g_api->print("BBoxReport: nothing selected");
    return 0;
  }
  char line[256];
  snprintf(line, sizeof line, "BBoxReport: %d object(s)", n);
  g_api->print(line);
  double total_volume = 0.0;
  for (int i = 0; i < n; ++i) {
    double bmin[3], bmax[3];
    if (!g_api->get_object_bbox(ids[i], bmin, bmax)) {
      snprintf(line, sizeof line, "  #%llu: bounding box unavailable", ids[i]);
      g_api->print(line);
      continue;
    }
    const double sx = bmax[0] - bmin[0], sy = bmax[1] - bmin[1], sz = bmax[2] - bmin[2];
    const double volume = sx * sy * sz;
    total_volume += volume;
    snprintf(line, sizeof line, "  #%llu: size %.4g x %.4g x %.4g, box volume %.4g", ids[i], sx, sy, sz, volume);
    g_api->print(line);
    char vbuf[64];
    snprintf(vbuf, sizeof vbuf, "%.6g", volume);
    g_api->set_user_text(ids[i], "BBoxVolume", vbuf);
  }
  snprintf(line, sizeof line, "BBoxReport: total bounding-box volume %.4g (tagged each object's BBoxVolume user text)", total_volume);
  g_api->print(line);
  return 0;
}

/* Even point distribution on a sphere via the golden-angle ("Fibonacci
 * sphere") method: for point i of count n, z runs linearly from 1 to -1
 * and the azimuth advances by the golden angle each step, which spreads
 * points with no clustering at the poles - unlike a naive lat/long grid. */
static void FibonacciSpherePoint(int index, int count, double radius, double* out_xyz) {
  if (count < 1) count = 1;
  if (index < 0) index = 0;
  if (index >= count) index = index % count;
  const double golden_angle = M_PI * (3.0 - sqrt(5.0));
  const double z = count > 1 ? (1.0 - 2.0 * (double)index / (double)(count - 1)) : 1.0;
  const double r_xy = sqrt(fmax(0.0, 1.0 - z * z));
  const double theta = golden_angle * (double)index;
  out_xyz[0] = radius * r_xy * cos(theta);
  out_xyz[1] = radius * r_xy * sin(theta);
  out_xyz[2] = radius * z;
}

static int FibonacciSpherePointEval(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs, int output_count,
                                    char* error, int error_size, void* user_data) {
  (void)user_data;
  (void)error;
  (void)error_size;
  if (input_count < 3 || output_count < 1) return 1;
  const int index = (int)inputs[0].number;
  const int count = (int)inputs[1].number;
  const double radius = inputs[2].number;
  Dino8FlowValue out;
  memset(&out, 0, sizeof out);
  out.kind = DINO8_FLOW_POINT;
  FibonacciSpherePoint(index, count, radius, out.xyz);
  outputs[0] = out;
  return 0;
}

DINO8_PLUGIN_EXPORT int dino8_plugin_init(const Dino8PluginApi* api) {
  g_api = api;
  if (!api || api->api_version != DINO8_PLUGIN_API_VERSION) return 1;

  api->register_command("BBoxReport", "BBoxReport - reports and tags the bounding-box volume of the selected objects.",
                        BBoxReportCommand, NULL);

  Dino8FlowPort inputs[3];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "Index"; inputs[0].kind = DINO8_FLOW_INTEGER;
  inputs[1].name = "Count"; inputs[1].kind = DINO8_FLOW_INTEGER; inputs[1].default_number = 100;
  inputs[2].name = "Radius"; inputs[2].kind = DINO8_FLOW_NUMBER; inputs[2].default_number = 1.0;

  Dino8FlowPort outputs[1];
  memset(outputs, 0, sizeof outputs);
  outputs[0].name = "Point"; outputs[0].kind = DINO8_FLOW_POINT;

  api->register_flow_node("Fibonacci Sphere Point", "Plug-ins",
                          "Point `Index` of an even Fibonacci-sphere distribution of `Count` points on a sphere of `Radius`.",
                          inputs, 3, outputs, 1, FibonacciSpherePointEval, NULL);
  api->print("AnalysisTools sample plug-in loaded (BBoxReport command, Fibonacci Sphere Point node).");
  return 0;
}

DINO8_PLUGIN_EXPORT const char* dino8_plugin_name(void) { return "AnalysisTools"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_version(void) { return "1.0.0"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_description(void) { return "Sample plug-in: selection/bbox analysis and Fibonacci-sphere point distribution."; }
