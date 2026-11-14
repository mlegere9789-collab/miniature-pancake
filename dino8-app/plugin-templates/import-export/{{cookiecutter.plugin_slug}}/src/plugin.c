/* {{cookiecutter.plugin_name}}: a Dino 8 import/export plug-in.
 * {{cookiecutter.command_name}}Import <file>  reads "x y z" lines (# comments allowed) and adds a point for each.
 * {{cookiecutter.command_name}}Export <file>  writes the bounding-box centre of every selected object, one "x y z" line each.
 * Plain C against dino8_plugin.h only. */
#include <stdio.h>
#include <string.h>

#include "dino8_plugin.h"

static const Dino8PluginApi* g_api = NULL;

/* Command tokens arrive split on spaces; rejoin them so a path with spaces works. */
static void JoinPath(int argc, const char** argv, char* out, size_t cap) {
  out[0] = 0;
  for (int i = 1; i < argc; ++i) {
    if (i > 1 && strlen(out) + 1 < cap) strcat(out, " ");
    if (strlen(out) + strlen(argv[i]) + 1 < cap) strcat(out, argv[i]);
  }
}

static int RunImport(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)ctx;
  char path[1024];
  JoinPath(argc, argv, path, sizeof path);
  if (!path[0]) { g_api->print("{{cookiecutter.command_name}}Import: usage: {{cookiecutter.command_name}}Import <file>"); return 1; }
  FILE* f = fopen(path, "r");
  if (!f) { g_api->print("{{cookiecutter.command_name}}Import: cannot open the file"); return 1; }
  char line[512];
  int count = 0;
  while (fgets(line, sizeof line, f)) {
    double x, y, z;
    if (line[0] == '#') continue;
    if (sscanf(line, "%lf %lf %lf", &x, &y, &z) != 3) continue;
    if (g_api->add_point(x, y, z)) ++count;
  }
  fclose(f);
  char msg[1200];
  snprintf(msg, sizeof msg, "{{cookiecutter.plugin_slug}}: imported %d point(s) from %s", count, path);
  g_api->print(msg);
  return 0;
}

static int RunExport(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)ctx;
  char path[1024];
  JoinPath(argc, argv, path, sizeof path);
  if (!path[0]) { g_api->print("{{cookiecutter.command_name}}Export: usage: {{cookiecutter.command_name}}Export <file>"); return 1; }
  unsigned long long ids[4096];
  const int n = g_api->get_selection(ids, 4096);
  FILE* f = fopen(path, "w");
  if (!f) { g_api->print("{{cookiecutter.command_name}}Export: cannot write the file"); return 1; }
  fprintf(f, "# x y z  ({{cookiecutter.plugin_name}} export)\n");
  int written = 0;
  for (int i = 0; i < n && i < 4096; ++i) {
    double lo[3], hi[3];
    if (!g_api->get_object_bbox(ids[i], lo, hi)) continue;
    fprintf(f, "%.9g %.9g %.9g\n", (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5);
    ++written;
  }
  fclose(f);
  char msg[1200];
  snprintf(msg, sizeof msg, "{{cookiecutter.plugin_slug}}: exported %d object(s) to %s", written, path);
  g_api->print(msg);
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
  if (api->register_command("{{cookiecutter.command_name}}Import", "Imports x y z lines from a text file as points.", RunImport, NULL)) return 3;
  if (api->register_command("{{cookiecutter.command_name}}Export", "Exports the centre of each selected object to a text file.", RunExport, NULL)) return 3;
  return 0;
}

DINO8_PLUGIN_EXPORT const char* dino8_plugin_name(void) { return "{{cookiecutter.plugin_name}}"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_version(void) { return "{{cookiecutter.version}}"; }
DINO8_PLUGIN_EXPORT const char* dino8_plugin_description(void) { return "{{cookiecutter.description}}"; }
