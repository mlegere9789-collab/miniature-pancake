/* CurveTools: a Dino 8 sample plug-in demonstrating a real curve generator
 * through the C ABI.
 *
 *   - Command "Spiral turns radius pitch segments" builds a 3D helix as a
 *     polyline (via add_polyline()) - real, densely sampled curve
 *     geometry, not a placeholder.
 *   - Dino Flow node "Helix Point" evaluates a point on the same helix at
 *     a parameter T in [0, 1], so a graph can build its own point list
 *     (e.g. wire a Range node's 0..1 output into T) and interpolate a
 *     curve from it with Dino Flow's own curve nodes, or place objects
 *     along the helix.
 *   - Dino Flow node "Spiral Curve" builds the same helix as the Spiral
 *     command, but as a DINO8_FLOW_CURVE *value* (via
 *     make_curve_value_polyline()) instead of a baked document object, so
 *     it can feed straight into other Dino Flow nodes for further
 *     processing in the graph.
 *   - Dino Flow node "Plugin Curve Length" takes a DINO8_FLOW_CURVE
 *     *input* (from Spiral Curve, a native curve node, or elsewhere in the
 *     graph) and reports its arc length via curve_length() - proving a
 *     plug-in node can receive real geometry, not just create it.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dino8_plugin.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const Dino8PluginApi* g_api = NULL;

/* Point on a helix of `turns` full turns, `radius`, rising `pitch` units
 * per turn, at parameter t in [0, 1] (t=0 is the base, t=1 the top). */
static void HelixPoint(double t, double turns, double radius, double pitch, double* out_xyz) {
  const double angle = t * turns * 2.0 * M_PI;
  out_xyz[0] = radius * cos(angle);
  out_xyz[1] = radius * sin(angle);
  out_xyz[2] = t * turns * pitch;
}

static int ParseIntArg(int argc, const char** argv, int index, int fallback) {
  if (index >= argc) return fallback;
  return atoi(argv[index]);
}
static double ParseNumArg(int argc, const char** argv, int index, double fallback) {
  if (index >= argc) return fallback;
  return atof(argv[index]);
}

static int SpiralCommand(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)ctx;
  const double turns = ParseNumArg(argc, argv, 1, 3.0);
  const double radius = ParseNumArg(argc, argv, 2, 5.0);
  const double pitch = ParseNumArg(argc, argv, 3, 2.0);
  const int segments = ParseIntArg(argc, argv, 4, 96);
  if (segments < 2 || segments > 5000) {
    g_api->print("Spiral: segments must be between 2 and 5000");
    return 1;
  }

  const int point_count = segments + 1;
  double* xyz = (double*)malloc((size_t)point_count * 3 * sizeof(double));
  if (!xyz) { g_api->print("Spiral: out of memory"); return 1; }
  for (int i = 0; i <= segments; ++i) {
    const double t = (double)i / (double)segments;
    HelixPoint(t, turns, radius, pitch, &xyz[i * 3]);
  }
  const unsigned long long id = g_api->add_polyline(xyz, point_count, 0);
  free(xyz);
  if (id == 0) { g_api->print("Spiral: add_polyline failed"); return 1; }

  char line[256];
  snprintf(line, sizeof line, "Spiral: built a %.3g-turn helix (radius %.3g, pitch %.3g, %d segments), object #%llu", turns,
           radius, pitch, segments, id);
  g_api->print(line);
  return 0;
}

static int HelixPointEval(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs, int output_count,
                          char* error, int error_size, void* user_data) {
  (void)user_data;
  (void)error;
  (void)error_size;
  if (input_count < 4 || output_count < 1) return 1;
  const double t = inputs[0].number;
  const double turns = inputs[1].number;
  const double radius = inputs[2].number;
  const double pitch = inputs[3].number;
  Dino8FlowValue out;
  memset(&out, 0, sizeof out);
  out.kind = DINO8_FLOW_POINT;
  HelixPoint(t, turns, radius, pitch, out.xyz);
  outputs[0] = out;
  return 0;
}

/* Dino Flow node: same helix as Spiral, but returned as a DINO8_FLOW_CURVE
 * *value* (make_curve_value_polyline) rather than baked into the document. */
static int SpiralCurveEval(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs, int output_count,
                           char* error, int error_size, void* user_data) {
  (void)user_data;
  if (input_count < 4 || output_count < 1) return 1;
  const double turns = inputs[0].number;
  const double radius = inputs[1].number;
  const double pitch = inputs[2].number;
  const int segments = (int)inputs[3].number;
  if (segments < 2 || segments > 5000) {
    snprintf(error, (size_t)error_size, "Segments must be between 2 and 5000");
    return 1;
  }

  const int point_count = segments + 1;
  double* xyz = (double*)malloc((size_t)point_count * 3 * sizeof(double));
  if (!xyz) { snprintf(error, (size_t)error_size, "out of memory"); return 1; }
  for (int i = 0; i <= segments; ++i) {
    const double t = (double)i / (double)segments;
    HelixPoint(t, turns, radius, pitch, &xyz[i * 3]);
  }
  Dino8FlowValue out;
  memset(&out, 0, sizeof out);
  out.kind = DINO8_FLOW_CURVE;
  out.geom = g_api->make_curve_value_polyline(xyz, point_count, 0);
  free(xyz);
  if (out.geom == 0) { snprintf(error, (size_t)error_size, "make_curve_value_polyline failed"); return 1; }
  outputs[0] = out;
  return 0;
}

/* Dino Flow node: consumes a DINO8_FLOW_CURVE *input* and reports its arc
 * length via curve_length() - proof a plug-in node can receive real
 * geometry from an upstream node, not just produce it. */
static int PluginCurveLengthEval(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs, int output_count,
                                 char* error, int error_size, void* user_data) {
  (void)user_data;
  if (input_count < 1 || output_count < 1) return 1;
  if (inputs[0].kind != DINO8_FLOW_CURVE || inputs[0].geom == 0) {
    snprintf(error, (size_t)error_size, "Curve input is empty");
    return 1;
  }
  Dino8FlowValue out;
  memset(&out, 0, sizeof out);
  out.kind = DINO8_FLOW_NUMBER;
  out.number = g_api->curve_length(inputs[0].geom);
  outputs[0] = out;
  return 0;
}

DINO8_PLUGIN_EXPORT int dino8_plugin_init(const Dino8PluginApi* api) {
  g_api = api;
  if (!api || api->api_version != DINO8_PLUGIN_API_VERSION) return 1;

  api->register_command("Spiral", "Spiral turns radius pitch segments - builds a helix polyline.", SpiralCommand, NULL);

  Dino8FlowPort inputs[4];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "T"; inputs[0].kind = DINO8_FLOW_NUMBER;
  inputs[1].name = "Turns"; inputs[1].kind = DINO8_FLOW_NUMBER; inputs[1].default_number = 3.0;
  inputs[2].name = "Radius"; inputs[2].kind = DINO8_FLOW_NUMBER; inputs[2].default_number = 5.0;
  inputs[3].name = "Pitch"; inputs[3].kind = DINO8_FLOW_NUMBER; inputs[3].default_number = 2.0;

  Dino8FlowPort outputs[1];
  memset(outputs, 0, sizeof outputs);
  outputs[0].name = "Point"; outputs[0].kind = DINO8_FLOW_POINT;

  api->register_flow_node("Helix Point", "Plug-ins", "Point on the same helix Spiral builds, at parameter T in [0, 1].", inputs, 4,
                          outputs, 1, HelixPointEval, NULL);

  Dino8FlowPort curve_inputs[4];
  memset(curve_inputs, 0, sizeof curve_inputs);
  curve_inputs[0].name = "Turns"; curve_inputs[0].kind = DINO8_FLOW_NUMBER; curve_inputs[0].default_number = 3.0;
  curve_inputs[1].name = "Radius"; curve_inputs[1].kind = DINO8_FLOW_NUMBER; curve_inputs[1].default_number = 5.0;
  curve_inputs[2].name = "Pitch"; curve_inputs[2].kind = DINO8_FLOW_NUMBER; curve_inputs[2].default_number = 2.0;
  curve_inputs[3].name = "Segments"; curve_inputs[3].kind = DINO8_FLOW_INTEGER; curve_inputs[3].default_number = 96;
  Dino8FlowPort curve_outputs[1];
  memset(curve_outputs, 0, sizeof curve_outputs);
  curve_outputs[0].name = "Curve"; curve_outputs[0].kind = DINO8_FLOW_CURVE;
  api->register_flow_node("Spiral Curve", "Plug-ins",
                          "Builds the same helix as Spiral, as a real Curve value other Dino Flow nodes can consume (not a "
                          "baked document object).",
                          curve_inputs, 4, curve_outputs, 1, SpiralCurveEval, NULL);

  Dino8FlowPort len_inputs[1];
  memset(len_inputs, 0, sizeof len_inputs);
  len_inputs[0].name = "Curve"; len_inputs[0].kind = DINO8_FLOW_CURVE;
  Dino8FlowPort len_outputs[1];
  memset(len_outputs, 0, sizeof len_outputs);
  len_outputs[0].name = "Length"; len_outputs[0].kind = DINO8_FLOW_NUMBER;
  api->register_flow_node("Plugin Curve Length", "Plug-ins", "Reports the arc length of any Curve value wired into it.",
                          len_inputs, 1, len_outputs, 1, PluginCurveLengthEval, NULL);

  api->print("CurveTools sample plug-in loaded (Spiral command, Helix Point/Spiral Curve/Plugin Curve Length nodes).");
  return 0;
}

DINO8_PLUGIN_EXPORT const char* dino8_plugin_name(void) { return "CurveTools"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_version(void) { return "1.0.0"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_description(void) { return "Sample plug-in: parametric helix curve generation."; }
