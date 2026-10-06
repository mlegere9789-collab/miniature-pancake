# Dino 8

### by LegeLabs (a Mike Legere company)

A free desktop NURBS / SubD / mesh modeler that speaks Rhino 8's command
vocabulary. Totally free: no subscription, no payment, no licence keys, no
accounts, no telemetry, no update nags.

Dino 8 is built on the `dino8-kernel` geometry library (OpenNURBS for
NURBS/B-rep/.3dm, Manifold for watertight mesh booleans) with a Dear ImGui
+ GLFW user interface. It runs on Windows, Linux and macOS from one code
base.

**[Read the docs](docs/site/index.html)** - a getting-started guide, a
searchable reference for all 1055 Rhino 8 commands (each marked
Implemented/Partial/Planned), ten verified step-by-step tutorials, and an
honest "coming from Rhino" migration guide. Open `docs/site/index.html`
directly in a browser, or serve the `docs/site/` folder with any static
file server - no build step required.

## What is in the test build

* **Every Rhino 8 command name** (1055 from the Rhino 8 Command Line
  Reference) is present on the command line, with the full reference help
  text in the Help panel. The Command List panel (F1) shows which are
  implemented, partial, or planned. Planned commands open their help
  instead of failing silently.
* **Rhino-style command line**: prompts, clickable options, autocomplete,
  aliases (`b` = Box, `co` = Copy...), `_`/`-`/`!` macro prefixes, Enter
  repeats, Esc cancels, relative `@x,y`, polar `d<angle`, and typed
  distances along the cursor direction.
* **Four dockable viewports** (Top / Front / Right / Perspective) with
  right-drag orbit, Shift/middle-drag pan, wheel zoom-to-cursor, window and
  crossing selection, object snaps (End/Mid/Cen/Quad/Near/Vertex), grid
  snap, ortho and planar modes, and ten display modes (Wireframe, Shaded,
  Rendered, Ghosted, X-Ray, Technical, Artistic, Pen, Arctic, Monochrome).
* **Geometry**: points, lines, polylines, control-point and interpolated
  curves, circles, arcs, ellipses, rectangles, polygons, helices, spirals;
  planes, boxes, spheres, cylinders, cones, truncated cones, tubes, tori,
  ellipsoids, pyramids; extrusions (capped solids from closed planar
  curves), revolves, lofts, planar surfaces; SubD primitives; mesh
  primitives; Boolean union / difference / intersection / split on any
  closed solid; move, copy, rotate, scale (1D/2D/3D), mirror, arrays,
  orient; join, explode, rebuild, flip, offset, extend; groups, hide/show,
  lock; layers with sub-layers, colours and notes.
* **Analysis**: distance, angle, length, area, volume, centroids,
  bounding box, curvature, deviation, What / List / Check / Audit.
* **Files**: native `.3dm` read and write (layers, names, colours, user
  text, notes, named views, units), OBJ and STL import/export, a
  file-system browser that works the same on every platform.
* **Undo that never stops working**: every change snapshots the document,
  so Undo/Redo (and Undo Multiple) cannot get out of sync with history.

## Building

```sh
cmake -S dino8-app -B dino8-app/build -DCMAKE_BUILD_TYPE=Release
cmake --build dino8-app/build --parallel
dino8-app/build/Dino8
```

Dependencies (GLFW, Dear ImGui, OpenNURBS, Manifold) are fetched by CMake.
On Linux you need the OpenGL/X11 development headers
(`libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`).

## Quality checks

* `ctest` in the build directory runs the brep mesher unit test (every
  primitive must mesh to a closed manifold at several tolerances).
* `dino8-app/tests/smoke.sh` runs the real application headless (under
  Xvfb when no display is present) through a command script: primitives,
  a Boolean difference, volume, extrusion, save / open of a .3dm,
  undo/redo, measurement and OBJ export, and checks the results.
* `Dino8 --smoke N --script file.txt --screenshot out.ppm` renders N frames
  of a scripted session and writes the final frame, for visual checks.

## Batch scripting

`Dino8 --script file.txt` (without `--smoke`) is a supported headless batch
mode for automation: it runs the file's commands once each with its window
hidden, then exits on its own with a real exit code - no `--smoke` frame
count and no human needed to close a window. See
[`docs/BATCH_SCRIPTING.md`](docs/BATCH_SCRIPTING.md).

## Compute server

`Dino8 --serve PORT` runs headless and starts a minimal HTTP server: POST a
Lua script to `/run` (or a Python script to `/run/python`), get back its
printed output (or a structured `{ok, output}` JSON response, with `Accept:
application/json`), against the same running document every request shares.
`GET /objects` lists the document's current objects (id/type/name/layer/
bounding box) as JSON, and `?geometry=1` adds each point/mesh/curve
object's own coordinates/vertices+faces/NURBS definition (every other kind
still comes back `null`); `POST /objects` takes one of those same three
shapes and adds it to the document, the first way to get geometry *into*
the server as structured data. One or more `--serve-token [NAME:]TOKEN` flags
require a matching `Authorization: Bearer TOKEN` header, letting distinct
callers each hold their own named token instead of one shared secret. A
first, deliberately small step toward a Rhino.Compute-style story - no
concurrency, no TLS, and still no wire
format for surface/polysurface/SubD/point-cloud geometry. See
[`docs/COMPUTE_SERVER.md`](docs/COMPUTE_SERVER.md).

## Packaging

The GitHub Actions workflow `dino8-app.yml` builds and tests on Linux,
Windows and macOS and uploads:

* `Dino8Setup-<version>.exe` (Inno Setup installer, Windows x64)
* `Dino8-<version>-win64.zip` (portable)
* `.deb` and `.tar.gz` (Linux x64)
* `.dmg` (macOS, Apple Silicon)

The Inno Setup script lives in `packaging/windows/Dino8.iss`.
