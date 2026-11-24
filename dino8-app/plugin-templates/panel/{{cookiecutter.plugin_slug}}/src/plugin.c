/* {{cookiecutter.plugin_name}}: a Dino 8 report ("panel") plug-in.
 * {{cookiecutter.command_name}} prints a read-only table about the current selection - id, bounding-box
 * size - plus totals, in the command history.
 *
 * Honest scope note: plug-in API v2 has no hook for ImGui/dockable widgets;
 * a plug-in's visible surface is the command history (api->print), the
 * document (add_*), user text on objects and Dino Flow nodes. This template
 * is the supported way to present a panel-like report today, and a Dino Flow
 * node mirrors it so the same numbers can feed a graph. */
#include <stdio.h>

#include "dino8_plugin.h"

static const Dino8PluginApi* g_api = NULL;

static int Run{{cookiecutter.command_name}}(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)argc; (void)argv; (void)ctx;
  unsigned long long ids[2048];
  const int n = g_api->get_selection(ids, 2048);
  g_api->print("== {{cookiecutter.plugin_name}} ==");
  if (n <= 0) { g_api->print("{{cookiecutter.plugin_slug}}: nothing selected"); return 0; }
  double total = 0.0;
  int rows = 0;
  for (int i = 0; i < n && i < 2048; ++i) {
    double lo[3], hi[3];
    if (!g_api->get_object_bbox(ids[i], lo, hi)) continue;
    const double sx = hi[0] - lo[0], sy = hi[1] - lo[1], sz = hi[2] - lo[2];
    char line[200];
    snprintf(line, sizeof line, "  id %llu  size %g x %g x %g  box volume %g", ids[i], sx, sy, sz, sx * sy * sz);
    g_api->print(line);
    total += sx * sy * sz;
    ++rows;
  }
  char sum[200];
  snprintf(sum, sizeof sum, "{{cookiecutter.plugin_slug}}: %d object(s), total box volume %g", rows, total);
  g_api->print(sum);
  return 0;
}

/* Dino Flow node: Box Volume (X, Y, Z sizes in, volume out). */
static int BoxVolumeEval(const Dino8FlowValue* in, int in_n, Dino8FlowValue* out, int out_n, char* err, int err_n, void* ud) {
  (void)err; (void)err_n; (void)ud;
  if (in_n < 3 || out_n < 1) return 1;
  out[0].kind = DINO8_FLOW_NUMBER;
  out[0].number = in[0].number * in[1].number * in[2].number;
  return 0;
}

DINO8_PLUGIN_EXPORT int dino8_plugin_init(const Dino8PluginApi* api) {
  if (!api) return 1;
  /* ABI rule: Dino 8 only ever ADDS to Dino8PluginApi, so a plug-in built
   * against API version N runs on any host that offers version >= N. Refuse
   * (politely, with a reason in the command history) on an older host. */
  if (api->api_version < DINO8_PLUGIN_API_VERSION) {
    char msg[160];
    snprintf(msg, sizeof msg, "{{cookiecutter.plugin_slug}}: needs plug-in API v%d, this Dino 8 offers v%d",
             DINO8_PLUGIN_API_VERSION, api->api_version);
    if (api->print) api->print(msg);
    return 2;
  }
  g_api = api;
  if (api->register_command("{{cookiecutter.command_name}}", "Prints a report about the selection.", Run{{cookiecutter.command_name}}, NULL)) return 3;
  Dino8FlowPort ins[3] = { {"X", NULL, DINO8_FLOW_NUMBER, 1.0}, {"Y", NULL, DINO8_FLOW_NUMBER, 1.0}, {"Z", NULL, DINO8_FLOW_NUMBER, 1.0} };
  Dino8FlowPort outs[1] = { {"Volume", NULL, DINO8_FLOW_NUMBER, 0.0} };
  api->register_flow_node("{{cookiecutter.plugin_name}} Box Volume", "Plug-ins", "Volume of an X by Y by Z box.", ins, 3, outs, 1, BoxVolumeEval, NULL);
  return 0;
}

DINO8_PLUGIN_EXPORT const char* dino8_plugin_name(void) { return "{{cookiecutter.plugin_name}}"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_version(void) { return "{{cookiecutter.version}}"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_description(void) { return "{{cookiecutter.description}}"; }
