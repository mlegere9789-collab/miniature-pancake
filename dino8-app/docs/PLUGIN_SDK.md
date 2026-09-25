# Dino 8 Plug-in SDK

Dino 8 plug-ins are plain shared libraries (`.so` / `.dll` / `.dylib`) built
against one public C header, [`include/dino8_plugin.h`](../include/dino8_plugin.h).
No SDK download, no build-system lock-in, no account, no network call: any
compiler for any language with a C ABI (C, C++, Rust, Zig, ...) can produce
one. Dino 8 loads every plug-in it finds in `<config>/plugins` and in the
`plugins/` folder next to the executable at startup, and keeps each one
loaded for the session.

This document is the node/command registration API, illustrated with real
snippets copied from the four example plug-ins that ship in this
repository - read their full source for complete, buildable context:

| Plug-in | Source | Demonstrates |
|---|---|---|
| HelloDino | `plugins/sample/sample_plugin.c` | The minimum viable plug-in: one command, one Dino Flow node. |
| MeshTools | `plugins/mesh_tools/mesh_tools_plugin.c` | Procedural **mesh generation** via `add_mesh()` (a full vertex/face-array command), paired with a Dino Flow node that samples the same height function. |
| CurveTools | `plugins/curve_tools/curve_tools_plugin.c` | A parametric **curve generator**: a helix command via `add_polyline()`, paired with a Dino Flow node that returns a point on the same helix at parameter `T` (chain it after a `Range` node to build a point cloud). |
| AnalysisTools | `plugins/analysis_tools/analysis_tools_plugin.c` | **Multi-object, read-and-write command I/O**: reads the selection and each object's bounding box, prints a report, and writes results back as user text - plus a generative (non-passthrough) Fibonacci-sphere point-distribution node. |

Every one of these builds as its own CMake target, entirely independent of
the `Dino8` target (see each plug-in's `CMakeLists.txt`), and is copied next
to the executable's `plugins/` folder automatically - exactly what a
third-party plug-in's own build would do.

## The one required export

```c
extern "C" int dino8_plugin_init(const Dino8PluginApi* api);
```

Dino 8 calls this once after loading the library. Return `0` for success;
anything else fails the load and the plug-in stays unloaded. `api` is a
struct of function pointers valid for the whole session - store it (a
`static` file-scope pointer, as every example here does) since command
callbacks and node evaluators run long after `dino8_plugin_init` returns.

Optional exports let `GrasshopperPluginList` / the Plug-in Manager panel
describe the plug-in: `dino8_plugin_name`, `dino8_plugin_version`,
`dino8_plugin_description`, and a `dino8_plugin_shutdown` called before
unload.

## Registering a command

```c
int (*register_command)(const char* name, const char* help, Dino8CommandFn fn, void* user_data);
```

`Dino8CommandFn` is `int (*)(int argc, const char** argv, Dino8CommandContext* ctx)` -
`argv[0]` is the command name, the rest are whatever tokens the user (or a
script) typed after it. Return `0` on success. From HelloDino:

```c
static int HelloDinoCommand(int argc, const char** argv, Dino8CommandContext* ctx) {
  (void)ctx;
  char line[256];
  if (argc > 1) snprintf(line, sizeof line, "HelloDino: hello, %s! (from the sample plug-in)", argv[1]);
  else snprintf(line, sizeof line, "HelloDino: hello from the sample plug-in!");
  g_api->print(line);
  g_api->add_point(0.0, 0.0, 0.0);
  return 0;
}
...
api->register_command("HelloDino", "Prints a greeting and drops a point at the origin.", HelloDinoCommand, NULL);
```

A command with real argument parsing and multi-object I/O (AnalysisTools'
`BBoxReport` - reads the selection, queries each object's bounding box,
prints a report, and tags each object with its computed volume):

```c
static int BBoxReportCommand(int argc, const char** argv, Dino8CommandContext* ctx) {
  unsigned long long ids[256];
  const int n = g_api->get_selection(ids, 256);
  if (n <= 0) { g_api->print("BBoxReport: nothing selected"); return 0; }
  for (int i = 0; i < n; ++i) {
    double bmin[3], bmax[3];
    if (!g_api->get_object_bbox(ids[i], bmin, bmax)) continue;
    const double volume = (bmax[0]-bmin[0]) * (bmax[1]-bmin[1]) * (bmax[2]-bmin[2]);
    char vbuf[64];
    snprintf(vbuf, sizeof vbuf, "%.6g", volume);
    g_api->set_user_text(ids[i], "BBoxVolume", vbuf);
  }
  return 0;
}
```

### Geometry-creation callbacks

`add_point`, `add_line`, `add_polyline`, `add_mesh` each add a real object to
the document and return its id (`0` on failure). `add_mesh` takes a flat
`xyz` vertex array and a `faces` array of four indices per face (repeat the
third index for a triangle) - MeshTools' `TerrainMesh` command builds a full
heightfield grid this way:

```c
const unsigned long long id = g_api->add_mesh(xyz, vertex_count, faces, face_count);
```

`get_selection`, `get_object_bbox`, `delete_object`, `select_object`, and
`set_user_text` round out read/write access to the document from a command.

## Registering a Dino Flow node

```c
int (*register_flow_node)(const char* name, const char* category, const char* description,
                          const Dino8FlowPort* inputs, int input_count,
                          const Dino8FlowPort* outputs, int output_count,
                          Dino8FlowEvalFn evaluator, void* user_data);
```

Ports are described with `Dino8FlowPort { name, nickname, kind, default_number }`.
Value kinds are `DINO8_FLOW_NUMBER`, `_INTEGER`, `_BOOLEAN`, `_TEXT`,
`_POINT`, `_VECTOR` (there is no plug-in-side Curve/Mesh/Brep kind yet - see
Limitations below). The evaluator is called once per matched item
combination; Dino 8's own Grasshopper-style list matching (see
`docs/` and `src/flow/FlowGraph.cpp`'s `RunMatched`/`Graph::Evaluate`) does
the branch/item bookkeeping before your callback ever runs, so a plug-in
node written for single items automatically works when wired to lists -
exactly like a native node:

```c
typedef int (*Dino8FlowEvalFn)(const Dino8FlowValue* inputs, int input_count,
                               Dino8FlowValue* outputs, int output_count,
                               char* error, int error_size, void* user_data);
```

CurveTools' `Helix Point` node - a real parametric generator, not a
passthrough - and its registration:

```c
static void HelixPoint(double t, double turns, double radius, double pitch, double* out_xyz) {
  const double angle = t * turns * 2.0 * M_PI;
  out_xyz[0] = radius * cos(angle);
  out_xyz[1] = radius * sin(angle);
  out_xyz[2] = t * turns * pitch;
}

static int HelixPointEval(const Dino8FlowValue* inputs, int input_count, Dino8FlowValue* outputs,
                          int output_count, char* error, int error_size, void* user_data) {
  if (input_count < 4 || output_count < 1) return 1;
  Dino8FlowValue out; memset(&out, 0, sizeof out);
  out.kind = DINO8_FLOW_POINT;
  HelixPoint(inputs[0].number, inputs[1].number, inputs[2].number, inputs[3].number, out.xyz);
  outputs[0] = out;
  return 0;
}

Dino8FlowPort inputs[4] = { {"T", NULL, DINO8_FLOW_NUMBER, 0}, {"Turns", NULL, DINO8_FLOW_NUMBER, 3.0},
                            {"Radius", NULL, DINO8_FLOW_NUMBER, 5.0}, {"Pitch", NULL, DINO8_FLOW_NUMBER, 2.0} };
Dino8FlowPort outputs[1] = { {"Point", NULL, DINO8_FLOW_POINT, 0} };
api->register_flow_node("Helix Point", "Plug-ins", "Point on the same helix Spiral builds, at parameter T in [0, 1].",
                        inputs, 4, outputs, 1, HelixPointEval, NULL);
```

Wire a `Range` node's 0..1 output into `T` and you get a point cloud along
the helix, ready for Dino Flow's own `Interpolate Curve` or `Nurbs Curve
Through Points` nodes - a real interop pattern between plug-in nodes and
the built-in node library, not just a toy.

Return non-zero (optionally writing a message to `error`) to flag the node
with an error, surfaced in the editor exactly like a built-in node's error.

## Data trees and the plug-in ABI

Dino Flow's internal value is `flow::Value` inside a `flow::Tree` of
branches (see `src/flow/FlowData.h`) - the data-tree structure behind
`Graft`/`Flatten`/`Simplify Tree`/`List Item`/`List Length` (`src/flow/
FlowNodesCore.cpp`). A plug-in node's `Dino8FlowValue` inputs/outputs are
always **single items**: Dino 8's list-matching layer (`Graph::Evaluate`)
gathers the tree, walks it branch-by-branch and item-by-item, and calls your
evaluator once per item - so a plug-in node is automatically graft/flatten/
list-transparent without doing anything tree-aware itself. What a plug-in
cannot do (in API version 1) is emit more than one output per port per
call, or receive a raw list/tree handle; chain a native `Merge`/`Entwine`/
`Graft Tree` node around a plug-in node's ports if you need explicit tree
shaping.

## Limitations, honestly

- **No Curve/Surface/Brep/Mesh port kind.** `Dino8FlowValue` only carries
  Number/Integer/Boolean/Text/Point/Vector (`DINO8_FLOW_*`). A plug-in
  cannot return a curve or mesh *value* from a flow node - only build one
  directly into the document via `add_polyline`/`add_mesh` from a
  **command**. MeshTools and CurveTools work around this by pairing a
  document-writing command with a flow node that returns the *numbers*
  describing the same generated shape (a height, a point on the curve) -
  a real and useful pattern, but not the same as a native node that
  outputs geometry values other nodes can wire further.
- **One value per output port per evaluator call.** A plug-in node cannot
  itself emit a list; only Dino 8's own list-matching machinery can grow a
  tree, by calling the evaluator multiple times (once per item) and
  collecting the results.
- **No custom node preview or editor widget.** Plug-in nodes always render
  with the generic port-only body; sliders/panels/toggle-style custom UI
  are a native-node-only feature (`NodeDef::Special`).
- Everything above is `reserved[8]` future-proofed in `Dino8PluginApi` -
  the ABI is designed so a version 2 could add tree-aware ports and more
  value kinds without breaking version-1 plug-ins.

## Building an example plug-in standalone

Each example's `CMakeLists.txt` only needs the header:

```cmake
add_library(my_plugin SHARED my_plugin.c)
set_target_properties(my_plugin PROPERTIES PREFIX "" OUTPUT_NAME "my_plugin" C_STANDARD 99)
target_include_directories(my_plugin PRIVATE /path/to/dino8-app/include)
```

Copy the resulting `.so`/`.dll`/`.dylib` into `<config>/plugins` or the
folder next to the `Dino8` executable, or point Dino 8 at another folder
with `GrasshopperFolders add <path>`. `GrasshopperPluginList` reports what
loaded and how many commands/nodes each plug-in registered.
