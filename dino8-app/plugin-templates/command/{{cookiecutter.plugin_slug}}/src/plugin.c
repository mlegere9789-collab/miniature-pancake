/* {{cookiecutter.plugin_name}}: a Dino 8 command plug-in.
 * Registers one command, {{cookiecutter.command_name}} [x y z], that drops a point (default: the origin)
 * and reports it in the command history. Plain C against dino8_plugin.h only. */
#include <stdio.h>
#include <stdlib.h>

#include "dino8_plugin.h"

static const Dino8PluginApi* g_api = NULL;

static int Run{{cookiecutter.command_name}}(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)ctx;
  double p[3] = {0.0, 0.0, 0.0};
  if (argc == 2) {
    /* "1,2,3" typed as one token */
    sscanf(argv[1], "%lf,%lf,%lf", &p[0], &p[1], &p[2]);
  } else {
    for (int i = 1; i < argc && i <= 3; ++i) p[i - 1] = atof(argv[i]);
  }
  const unsigned long long id = g_api->add_point(p[0], p[1], p[2]);
  if (!id) {
    g_api->print("{{cookiecutter.plugin_slug}}: could not add the point");
    return 1;
  }
  char line[200];
  snprintf(line, sizeof line, "{{cookiecutter.plugin_slug}}: {{cookiecutter.command_name}} placed a point at (%g, %g, %g)", p[0], p[1], p[2]);
  g_api->print(line);
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
  return api->register_command("{{cookiecutter.command_name}}", "{{cookiecutter.description}}", Run{{cookiecutter.command_name}}, NULL);
}

DINO8_PLUGIN_EXPORT const char* dino8_plugin_name(void) { return "{{cookiecutter.plugin_name}}"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_version(void) { return "{{cookiecutter.version}}"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_description(void) { return "{{cookiecutter.description}}"; }
