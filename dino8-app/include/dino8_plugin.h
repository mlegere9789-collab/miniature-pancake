/*
 * Dino 8 plug-in API (C ABI).
 *
 * A plug-in is a shared library (.so / .dll / .dylib) that exports
 *
 *     extern "C" int dino8_plugin_init(const Dino8PluginApi* api);
 *
 * Dino 8 loads every plug-in found in <config>/plugins and in the plugins/
 * folder next to the executable at start-up (and on demand through the
 * PlugInManager panel), calls dino8_plugin_init once, and keeps the library
 * loaded for the lifetime of the session. The plug-in registers commands
 * and Dino Flow nodes through the function pointers in the api struct.
 *
 * Everything is plain C so plug-ins can be written in C, C++, Rust, Zig...
 * and built with any compiler. No licence, no accounts, no network.
 *
 * Optional exports:
 *     const char* dino8_plugin_name(void);         human readable name
 *     const char* dino8_plugin_version(void);      e.g. "1.2.0"
 *     const char* dino8_plugin_description(void);
 *     void        dino8_plugin_shutdown(void);     called before unload
 */
#ifndef DINO8_PLUGIN_H
#define DINO8_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define DINO8_PLUGIN_API_VERSION 1

#if defined(_WIN32)
#define DINO8_PLUGIN_EXPORT __declspec(dllexport)
#else
#define DINO8_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* Opaque handle passed to command callbacks; valid only during the call. */
typedef struct Dino8CommandContext Dino8CommandContext;

/* A command callback receives the tokens typed after the command name
 * (argv[0] is the command name itself) and returns 0 on success. */
typedef int (*Dino8CommandFn)(int argc, const char** argv, Dino8CommandContext* ctx);

/* ---- Dino Flow node values ------------------------------------------- */

typedef enum Dino8FlowKind {
  DINO8_FLOW_NUMBER = 0,
  DINO8_FLOW_INTEGER = 1,
  DINO8_FLOW_BOOLEAN = 2,
  DINO8_FLOW_TEXT = 3,
  DINO8_FLOW_POINT = 4,
  DINO8_FLOW_VECTOR = 5,
  DINO8_FLOW_ANY = 100
} Dino8FlowKind;

/* One input or output value of a plug-in node. Only the members matching
 * `kind` are meaningful. Text points into memory owned by Dino 8 for
 * inputs; for outputs the plug-in writes into `text` (a buffer of
 * DINO8_FLOW_TEXT_MAX bytes) - it is copied after the evaluator returns. */
#define DINO8_FLOW_TEXT_MAX 512
typedef struct Dino8FlowValue {
  int kind;            /* Dino8FlowKind */
  double number;       /* NUMBER / INTEGER / BOOLEAN (0 or 1) */
  double xyz[3];       /* POINT / VECTOR */
  char text[DINO8_FLOW_TEXT_MAX];
} Dino8FlowValue;

typedef struct Dino8FlowPort {
  const char* name;     /* e.g. "Radius" */
  const char* nickname; /* e.g. "R" (may be NULL) */
  int kind;             /* Dino8FlowKind */
  double default_number;
} Dino8FlowPort;

/* Called once per matched item combination (Grasshopper-style list
 * matching is done by Dino 8). Fill `outputs[i]`; return 0 on success or
 * non-zero (with an optional message in `error`) to flag the node. */
typedef int (*Dino8FlowEvalFn)(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs,
                               int output_count, char* error, int error_size, void* user_data);

typedef struct Dino8PluginApi {
  int api_version;                 /* DINO8_PLUGIN_API_VERSION */
  const char* app_version;         /* "0.1.0" */
  const char* config_dir;          /* the user's Dino 8 configuration folder */
  void* app;                       /* reserved; do not dereference */

  /* Prints a line to the command history. */
  void (*print)(const char* text);

  /* Registers a command line command. `help` may be NULL. */
  int (*register_command)(const char* name, const char* help, Dino8CommandFn fn, void* user_data);

  /* Runs a command line as if typed ("Box 0,0,0 10,10,0 5"). */
  int (*run_command)(const char* command_line);

  /* Geometry creation: each returns the new object's id (0 on failure). */
  unsigned long long (*add_point)(double x, double y, double z);
  unsigned long long (*add_line)(double x0, double y0, double z0, double x1, double y1, double z1);
  unsigned long long (*add_polyline)(const double* xyz, int point_count, int closed);
  /* vertices: xyz triples; faces: `face_count` quads/tris as 4 indices each
   * (repeat the third index for a triangle). */
  unsigned long long (*add_mesh)(const double* xyz, int vertex_count, const int* faces, int face_count);

  /* Copies the ids of the selected objects into `out` (up to `max`),
   * returns the total number selected. */
  int (*get_selection)(unsigned long long* out, int max);
  /* Fills the world bounding box of an object; returns 0 when unknown. */
  int (*get_object_bbox)(unsigned long long id, double* min_xyz, double* max_xyz);
  int (*delete_object)(unsigned long long id);
  int (*select_object)(unsigned long long id, int selected);
  int (*set_user_text)(unsigned long long id, const char* key, const char* value);

  /* Registers a Dino Flow node in the "Plug-ins" tab. */
  int (*register_flow_node)(const char* name, const char* category, const char* description,
                            const Dino8FlowPort* inputs, int input_count, const Dino8FlowPort* outputs,
                            int output_count, Dino8FlowEvalFn evaluator, void* user_data);

  /* Reserved for future versions; always NULL in version 1. */
  void* reserved[8];
} Dino8PluginApi;

typedef int (*Dino8PluginInitFn)(const Dino8PluginApi* api);

#ifdef __cplusplus
}
#endif

#endif /* DINO8_PLUGIN_H */
