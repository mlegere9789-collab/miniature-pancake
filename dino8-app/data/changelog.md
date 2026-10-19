# Dino 8 changelog

Entries are grouped by the milestone a feature actually landed in, newest
first. Each heading is the version string shown in About / WhatsNew; the
current build's version is `DINO8_VERSION` (see CMakeLists.txt). This file
is edited by hand alongside real feature work - see `git log` for the exact
commits behind any entry.

## 0.1.0

- Electrical schematic symbol toolset: Resistor, Capacitor, Switch, Ground,
  Lamp, WireRun, ElecTag and PanelSchedule, with real circuit-row table
  generation.
- Mechanical/MEP components: fasteners and MEP run objects with their own
  regression coverage.
- Sheet Set Manager: batch-plot layouts across multiple .3dm files to PDF.
- CAD Standards Checker (Standards/CheckStandards).
- DWG/Xref Compare: a local two-file geometric diff (DwgCompare, XrefCompare,
  CompareClear).
- Live parametric constraints with auto-resolve, and visibility-state
  dynamic blocks.
- Real DWG import/export via GNU LibreDWG, plus DXF/DWG DIMENSION, MTEXT,
  SPLINE, HATCH and TEXT import.
- Kernel boolean hardening: cylinder/cylinder pairs (parallel, Steinmetz,
  unequal-radius, skew-axis full-pierce) and plane/cylinder booleans
  (BooleanCombineMixed, BooleanCombinePlanar), with boolean results treated
  as first-class operands so chained ops and SymmetricDifference are exact.
- GPU raytraced viewport: a real per-frame GPU BVH-traversal raytracer
  (GpuRaytracer) with material textures, transparency and multi-bounce,
  wired into the RayTraced display mode.
- CPU path tracer, SubD editing (creases, control-net editing, repair,
  conversions), and an embedded Python 3 (pybind11) scripting engine
  alongside the existing Lua engine.
- 2D/3D geometry kernel (dino8-kernel): mesh, B-rep, SubD, NURBS curve and
  surface support built from an OpenNURBS wrapper plus the Manifold boolean
  engine - the foundation everything above is built on.
