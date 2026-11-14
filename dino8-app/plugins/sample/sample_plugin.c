/* HelloDino: the Dino 8 sample plug-in. Proves the C ABI end to end:
 * registers a command (HelloDino) and a Dino Flow node (Hello Offset). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dino8_plugin.h"

static const Dino8PluginApi* g_api = NULL;

static int HelloDinoCommand(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)ctx;
  char line[256];
  if (argc > 1) {
    snprintf(line, sizeof line, "HelloDino: hello, %s! (from the sample plug-in)", argv[1]);
  } else {
    snprintf(line, sizeof line, "HelloDino: hello from the sample plug-in!");
  }
  g_api->print(line);
  /* Also prove the geometry-creation callbacks work: drop a point at the origin. */
  g_api->add_point(0.0, 0.0, 0.0);
  return 0;
}

/* A flow node: offsets a point along Z by `Height` - just enough to prove
 * register_flow_node()/the evaluator callback round-trips real numbers. */
static int HelloOffsetEval(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs, int output_count,
                           char* error, int error_size, void* user_data) {
  (void)user_data;
  (void)error;
  (void)error_size;
  if (input_count < 2 || output_count < 1) return 1;
  Dino8FlowValue out;
  memset(&out, 0, sizeof out);
  out.kind = DINO8_FLOW_POINT;
  out.xyz[0] = inputs[0].xyz[0];
  out.xyz[1] = inputs[0].xyz[1];
  out.xyz[2] = inputs[0].xyz[2] + inputs[1].number;
  outputs[0] = out;
  return 0;
}

DINO8_PLUGIN_EXPORT int dino8_plugin_init(const Dino8PluginApi* api) {
  g_api = api;
  if (!api || api->api_version != DINO8_PLUGIN_API_VERSION) return 1;
  api->register_command("HelloDino", "Prints a greeting and drops a point at the origin.", HelloDinoCommand, NULL);

  Dino8FlowPort inputs[2];
  memset(inputs, 0, sizeof inputs);
  inputs[0].name = "Point";
  inputs[0].kind = DINO8_FLOW_POINT;
  inputs[1].name = "Height";
  inputs[1].kind = DINO8_FLOW_NUMBER;
  inputs[1].default_number = 1.0;

  Dino8FlowPort outputs[1];
  memset(outputs, 0, sizeof outputs);
  outputs[0].name = "Point";
  outputs[0].kind = DINO8_FLOW_POINT;

  api->register_flow_node("Hello Offset", "Plug-ins", "Offsets a point up by Height (sample plug-in node).", inputs, 2,
                          outputs, 1, HelloOffsetEval, NULL);
  api->print("HelloDino sample plug-in loaded.");
  return 0;
}

DINO8_PLUGIN_EXPORT const char* dino8_plugin_name(void) { return "HelloDino"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_version(void) { return "1.0.0"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_description(void) { return "Sample plug-in: a command and a Dino Flow node."; }
