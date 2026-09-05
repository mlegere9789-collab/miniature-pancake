# Dino 8 vs Rhino 8 — Implementation Audit

*Read-only audit of `/home/user/miniature-pancake/dino8-app` (branch `claude/pdf-audit-i2bvwm`, HEAD 14bfad8, tree mid-cherry-pick of 3943b8b "clipping planes, layouts, named CPlanes, animation") plus the agent worktrees `agent-a0941ca88c6de51b1` (cmd_viewtools.cpp — identical to the staged main-tree copy) and `agent-aa263d74c70074354` (UI polish: `src/ui/Icons.cpp`, `Toolbars.cpp`, autocomplete, context menus — it adds no new command registrations). Sources: doc1.txt / doc2.txt (Rhino 8 Complete Feature Audit, Parts 1–7; doc1 is the PDF-extracted copy with clean tables), doc3.txt (rhnio8 — Rhino 8 Command Line Reference, 1055 entries), doc4.pdf (2-page "Dino 8" design blueprint; no poppler/pypdf available, so it was decoded by hand from its FlateDecode streams and ToUnicode CMaps — it is fully readable and is quoted below). Date: 2026-09-05.*

---

## 0. Headline numbers

| Metric | Value |
|---|---|
| Rhino 8 catalogue (data/commands.json) | **1055** commands |
| Registered names after all `Reg()` calls, last-registration-wins in Application.cpp order | 1058 (969 catalogue names + 89 extras/aliases such as `ZoomExtents`, `Circle3Pt`, `Top`, `Layers`) |
| **Implemented** (catalogue) | **592** (56.1 %) |
| **Partial** (catalogue) | **377** (35.7 %) |
| **Planned** (catalogue name never registered → help-only placeholder) | **86** (8.2 %) |
| Registered but *print-only* stubs (factory is `Say`/`Stub`/`Planned`/`SubObjectPlanned`/`HoleFeatureStub` or an `Immediate` whose body is one `ctx.Print`) | **125** (114 Partial + 11 "Implemented") |
| Commands that actually do geometric/UI work | 969 − 125 = **844** (80 % of the catalogue) |
| Catalogue commands exercised by a scripted test (`tests/*.txt`, `tests/smoke.sh`) | **249** (229 Implemented, 20 Partial) → 61 % of Implemented commands have **no** automated test |
| Latest headless run (`/tmp/d8/smoke_vt.log`, 20:51, same minute as `build/Dino8`) | 302 ok / **4 FAIL** (linetype + block round-trip checks in `annotate2_script.txt`, see §1.4) |
| Network / licence / telemetry code | **none** (see §4.3) |

Method: `scratchpad/cov.py` parses every `Reg(e, "Name", factory[, CommandStatus::X[, "note"]])` with balanced-parenthesis extraction across the 25 `cmd_*.cpp` files in the order `Application::RegisterCommands()` calls them (create, solids, transform, edit, boolean, analyze, select, view, layer, file, misc, annotate, drafting, annotate2, select2, state, curves2, srfedit, meshtools, subd, solidtools, viewtools, curveedit, surface, render). The eight loop/table registrations (`for (const char* n : {...}) Reg(e, n, ...)` in select2/state/solidtools/viewtools and the `Metric` table in meshtools) were expanded by hand. Keys are lower-cased because `CommandEngine::Register` keys on `ToLower(name)` and `RegisterCatalogPlaceholders()` fills in everything else as Planned. The parser's counts match the app's own start-up line ("1055 commands loaded … planned") in the smoke logs.

---

## 1. Command coverage

### 1.1 Status distribution by registration file (last registration wins)

The default status when omitted is `Implemented`, so several files register "Implemented" commands that are pure prints — those are counted as stubs above, not as working commands. Notable "Implemented" stubs: `Cancel` (legitimately a no-op), `CheckInLicense`, `CheckOutLicense`, `Login`, `Logout`, `Libraries` (all print the "free software" line), `GetIssueState`, `HistoryPurge`, `HistoryUpdate`, `Menus`, `SetCurrentRenderPlugIn`.

### 1.2 The 86 Planned commands, grouped by the reference's menu / toolbar

(Anything in this list runs `RegisterCatalogPlaceholders()`'s placeholder: it prints "Not implemented in this build yet" and opens the Help panel — it never fails silently.)

| Rhino menu / toolbar | Planned commands |
|---|---|
| Curve | Convert, MatchCrvDir, OffsetNormal, PointCloudContour |
| Curve Tools (toolbar) | RebuildCrvNonUniform, RibbonOffset |
| Surface | BlendSrf, TweenSurfaces |
| Surface Creation / Analysis (toolbar) | DevLoft, ThicknessAnalysisOff |
| Solid | ExtrudeSrfAlongCrv, ExtrudeSrfTapered, ExtrudeSrfToPoint |
| Mesh / SubD | AlignVertices, MeshPolyline, MultiPipe, CollapseMeshFacesByArea, CollapseMeshFacesByAspectRatio, CollapseMeshFacesByEdgeLength, ExtractDuplicateMeshFaces, WeldEdge, SelConnectedMeshFaces, SplitMeshWithCurve, ExtractUVMesh, CreaseSplitting |
| Analyze | EdgeContinuity, EvaluateUVPt, GCon, PointsFromUV, CheckNewObjects, ClearAnalysisMeshes, CurvatureGraphOff, ExtractCurvatureGraph, ThicknessAnalysis, DraftAnglePoint |
| Point / Point Edit / Select Points | AddNextU, AddNextV, AddPrevU, AddPrevV, NextU, NextV, PrevU, PrevV, HBar, PtOffSelected, DrapePt, CullControlPolygon |
| Edit / Layer / Visibility | DupLayer, CopyToLayer, HighlightObjectLayers, LayerBook, IsolateLock, UnisolateLock, JoinCopy, MatchProperties |
| View / Viewport title / Window | MoveTargetToObjects, Synchronize Views, DisplayProperties, ClearAllObjectDisplayModes, SaveWindowLayout, WindowLayout, PopupToolbar |
| Drafting | ChangeSpace, DecimalPoint |
| Tools / scripting / files | AttachGHSData, Rescue3dmFile, EditPythonScript, EditScript, LoadScript, ExportRuiFile, ExportBitmaps, AcadSchemes, IgesImportOptions, IGESStudy, ReadEveryIGESEntity, SetIgesLayerLevelMap, STEPTree, StepUnitsAndTolerance |
| Texture mapping / render | Unwrap, UVEditor, ApplyOcsMapping, ExtractCustomMappingObject |
| Misc | AlignProfiles, ShortPath, ReducePointCloud, PointCloudSection |

Observation: 27 of the 86 are point-editing / IGES-STEP / script-editor commands that depend on features Dino 8 does not have at all (sub-object selection, IGES/STEP readers, a script editor). `BlendSrf`, `MatchSrf`-class surfacing, `ThicknessAnalysis`, `EdgeContinuity`/`GCon` are the ones a Rhino user will miss first.

Also note: **`PushPull` is not in the 1055-command catalogue at all** (the reference docs omit it), so it is neither registered nor "Planned" — it is simply absent, although doc4's parity contract promises it.

### 1.3 The 377 Partial commands

The full list with the note each one prints is in Appendix A. The 114 print-only Partial stubs cluster into these families:

| Family (count) | What happens when you run it | Where |
|---|---|---|
| Surface/edge editing (52): FilletEdge, ChamferEdge, BlendEdge, FilletSrf, ChamferSrf, VariableFilletSrf, MatchSrf, MergeFaces/MergeAllCoplanarFaces, MoveFace/MoveEdge, SplitEdge/SplitFace, ApplyCrv/ApplyMesh, SrfSeam, UnjoinEdge, Boss, Rib, SetPlanar, SoftEditSrf, ExtractBadSrf, ConvertExtrusion, FitSrf … | Prints "… planned" (`Planned(...)` factory) | `cmd_srfedit.cpp:960-1000` |
| Sub-object selection (13): SelControlPoint, SelU/V/UV, SelEdgeLoop/Ring, SelFaceLoop, SelMeshEdges, SelMeshPart, InvertPt … | Prints "Sub-object selection is coming; whole objects for now." | `cmd_select2.cpp:439-442` |
| Digitizers (11), Grasshopper/plug-ins (9), Worksession, Snapshots, NamedPosition, ContentFilter, LimitReferenceModel | Prints guidance | `cmd_state.cpp:618-632` |
| Clipping drawings (5), PerspectiveMatch | "use ClippingSections + Print" | `cmd_viewtools.cpp:1038` |
| Holes (4: Copy/Mirror/Move/RotateHole) | "Holes are not feature objects in this build" | `cmd_solidtools.cpp:1644` |
| History, PackageManager, Pan, RotateView, RotateCamera, AddNgonsToMesh, FlatShade, EndBulge, InsertLineIntoCrv, RemoveSymmetry, RemoveGuide, Bake, MappingWidgetOff, RemovePerFaceColors, DownloadLibraryTextures | Print only | misc/view/curves2/subd/render |

The other 263 Partial commands do real work with a documented simplification. Representative honest notes (verbatim from the source): `OffsetSrf` "Polysurfaces and meshes are offset as meshes along vertex normals"; `Shell` "Hollows a closed solid as a mesh"; `Loft` "Normal loft; Loose/Tight/Straight styles are planned"; `Make2D` "Projects visible wire geometry; hidden-line removal is planned"; `Hatch` "Solid fills become planar surfaces; line patterns are curve groups"; `Block` "Instances are grouped copies tagged with the block name (not linked)"; `Dim*/Text/Leader` "Creates grouped curve geometry (text outlines from the system font) rather than a live annotation"; `Gumball` "Move handles; rotate/scale handles are planned" — **this note is stale**: `src/ui/Gumball.cpp:121-174` implements `RotX/Y/Z` and `ScaleX/Y/Z` handles.

Also stale/misleading: `cmd_misc.cpp:113` registers `Text` as a Notes-panel stub, but `cmd_annotate.cpp:254` re-registers it later with a real `TextCommand`, so the working version wins (the parser follows this order).

### 1.4 What the tests prove

- `tests/smoke.sh` is the only harness; CI (`.github/workflows/dino8-app.yml`) runs `ctest` (brep-mesher unit test, 7 assertions; kernel `test_basic.cpp`, ~577 checks) and `smoke.sh` on Linux/Windows/macOS. `smoke.sh` contains 250 `check "…"` assertions plus `@expect_objects/@expect_selected` lines over 13 scripts: base heredoc, `ui_script` (synthetic mouse/keyboard replay), `curveedit`, `curves2`, `exchange`, `surface`, `srfedit`, `meshtools`, `subd`, `render`, `annotate2`, `state`, `viewtools`, `solidtools`.
- Most assertions are string matches on the command-history line (e.g. `"Volume = 31.06 cubic"`, `"Sweep1: 1 section(s) along 2 rail stations"`, `"Bridge: joined 2 faces with a tube of 4 quads"`), plus object counts and a `gl_error=0` check. They prove the command ran and produced numerically plausible output; they do not check surface quality or Rhino-equivalence.
- Latest log on this machine (`/tmp/d8/smoke_vt.log`): 302 ok, 4 FAIL — "linetype table and object linetype survived the 3dm round-trip", "ReplaceBlock swapped both A instances for B", "SelBlockInstanceNamed found the replaced instances", "CreateUniqueBlock copied the definition". These are all in `annotate2_script.txt` and coincide with `src/io/File3dm.cpp` and `Document.cpp` being in an unresolved-conflict state, so they are most likely merge fallout rather than pre-existing regressions — but they were red at the time of this audit. Earlier per-branch logs (`smoke_a2.log` 217 ok, `smoke_r.log` 199 ok, `smoke_st.log` 241 ok, `smoke_subd.log` 171 ok) were all green.
- 363 of the 592 Implemented commands appear in no script at all. Untested-but-Implemented areas include most of `cmd_edit.cpp` (Join/Explode/Rebuild/Offset/Extend), `cmd_transform.cpp` beyond Move/Scale, `cmd_layer.cpp`, `cmd_view.cpp`, `cmd_select.cpp`, `cmd_analyze.cpp` (Zebra/EMap/Curvature/DraftAngle shading), `cmd_curves2.cpp` conics beyond the four checked, and nearly all of `cmd_state.cpp`'s option toggles.

---

## 2. Feature-area coverage against the Feature Audit / Command Reference

Status scale: **Complete** (Rhino-equivalent for everyday use), **Mostly**, **Partial**, **Missing**.

| Area | Status | Evidence | What is missing |
|---|---|---|---|
| **Curves** (creation) | Mostly | `cmd_create.cpp` (Line, Polyline, Curve, InterpCrv, Circle, Arc, Ellipse, Rectangle, Polygon, Helix, Spiral), `cmd_curves2.cpp` (Conic, Parabola, Hyperbola, Catenary, BlendCrv, TweenCurves, ArrayCrv, Contour, Section, fits — tested in `curves2_script.txt`), kernel `NurbsCurve` (Trim/Split/Extend/InsertKnot/ElevateDegree). | Sketch is click-only; Match is a blend not a true end-match; Extend picks no extension; OffsetCrvOnSrf is planar. |
| **Curve editing** | Mostly | `cmd_curveedit.cpp` Intersect/Split/Trim/Fillet/Chamfer/FilletCorners (tested, `curveedit_script.txt`); `cmd_edit.cpp` Join/Explode/Rebuild/Offset/Extend/InsertKnot/PointsOn. | TESTING.md: "Fillet/Chamfer are exact for lines and approximate for other curves". Control points can be shown (`show_control_points`) but there is **no control-point dragging** in `Viewport.cpp` (only drawing at line 701) — point editing, SelU/V, HBar, Weight are stubs/planned. |
| **Surfaces** | Partial | `cmd_surface.cpp` Sweep1/2, NetworkSrf (Coons/ruled), Patch (plane fit), Pipe, OffsetSrf, Shell, ExtrudeCrvAlongCrv/Tapered, Project, Pull (tested, `surface_script.txt`); `cmd_srfedit.cpp` ExtractSrf, DeleteFaces, DupBorder/Edge, Untrim, isocurves, ExtendSrf, UnrollSrf, Silhouette, RailRevolve, Ribbon (tested). Kernel has no surface/surface intersector: `brep.h` only offers `FromSurface`, `Box`, `Sphere`, `TrimmedPlanarFace`, tessellation. | The whole Class-A / surface-editing set is print-only: **FilletSrf, BlendSrf (Planned), MatchSrf, ChamferSrf, VariableFilletSrf, MergeSrf (refit), FitSrf, SrfSeam, SplitFace/Edge, MoveFace/Edge, Boss, Rib**. Loft has no styles; NetworkSrf only 2–4 curves; Patch is a least-squares plane. No untrim-by-edge, no trim-with-surface. |
| **Solids** | Partial | `cmd_solids.cpp` primitives + ExtrudeCrv/ExtrudeSrf/Revolve/Cap; `cmd_boolean.cpp` union/difference/intersection **on meshes via Manifold** (`kernel/boolean.cpp`, `ToManifold`) — the result is a mesh object, not a B-rep; BooleanSplit/MeshSplit/WireCut are plane splits only. `cmd_solidtools.cpp` RoundHole, CurveBoolean, Clash, Cage/CageEdit, Flow, ScaleByPlane (tested, `solidtools_script.txt`). | **FilletEdge / ChamferEdge / BlendEdge are print-only stubs** (`cmd_srfedit.cpp:978-980`). No PushPull (not even in the catalogue). Booleans destroy NURBS-ness (output is a mesh, so later Fillet/Untrim/etc. cannot apply). Holes are not features. `Shell`/`OffsetSrf Solid=Yes` produce meshes. |
| **SubD** | Mostly | `cmd_subd.cpp` (1541 lines): Crease/RemoveCrease, ExtrudeSubD, Inset, Bridge, OffsetSubD, RepairSubD, InsertEdge, InsertPoint, DivideAlongCreases, Fill, AutomaticSubDFromMesh, SubD primitives, SubDDisplayToggle, ShrinkWrap (convex hull) — tested `subd_script.txt`; kernel `SubD::FromControlMesh/Subdivide/ToApproximateMesh`. | Sub-object selection is by picked point only; `ShrinkWrap` is a **convex hull** (concavities not followed — nothing like Rhino's); `MakeSubDFriendly`/`ToNURBS` are approximate rebuilds, not exact Catmull-Clark→NURBS (doc4 promised "exact NURBS conversion at any subdivision level"); MultiPipe planned; no Symmetry live editing; SubDExpandEdges/SpinEdge simplified. |
| **Meshes** | Mostly | `cmd_meshtools.cpp` (2015 lines): deformations (Twist/Bend/Taper/Stretch/Shear/Maelstrom/SoftMove/Smooth), ExtrudeMesh, OffsetMesh, FillMeshHoles, Weld/Unweld, MeshRepair, primitives, MeshIntersect, MeshPatch, Drape, extract-by-metric family (tested `meshtools_script.txt`); `cmd_boolean.cpp` MeshBoolean*, ReduceMesh, CheckMesh. | Edge/face/vertex picking is "nearest to pick" only (CollapseMeshEdge collapses the *shortest* edge, not a picked one); ReduceMesh has no target count; QuadRemesh is a UV-grid sample/triangle pairing, not a real quad remesher; MeshOutline is a convex hull; AlignVertices, WeldEdge, SplitMeshWithCurve, MeshPolyline planned. |
| **Transforms** | Mostly | `cmd_transform.cpp` Move/Copy/Rotate/Scale1D/2D/3D/Mirror/Array/ArrayPolar/Orient/Orient3Pt; Gumball with move/rotate/scale handles (`Gumball.cpp`); Align/Distribute (`curves2`); Flow/CageEdit (`solidtools`). | Rotate3D only about CPlane normal; ScaleNU one axis; SoftTransform move only; ArrayCrvOnSrf no normal orientation; ArrayLinear = Array. |
| **Analysis** | Mostly | `cmd_analyze.cpp` Distance/Angle/Length/Area/Volume/BoundingBox/What/List/Check/Audit; Zebra/EMap/CurvatureAnalysis/DraftAngleAnalysis are real GL shaders (`GlRenderer::DrawTrianglesZebra/EMap`), ShowEdges. | Curvature prints 11 samples; CurvatureGraph is a curve object; AreaMoments/VolumeMoments lack second moments; EdgeContinuity, GCon, ThicknessAnalysis, EvaluateUVPt planned; no interactive "point on surface" evaluation. Not covered by any test script. |
| **Annotation / drafting** | Partial | `cmd_annotate.cpp` + `cmd_annotate2.cpp` (1369 lines): Text, TextObject, Dim, DimLinear/Aligned/Angle/Radius/Diameter/Ordinate/Area/CurveLength, Leader, Centermark, RevCloud, Dot, Hatch (Solid/Hatch1/Grid/Hatch2), linetypes incl. custom, AnnotationStyles (options-driven), Make2D (projection) — tested `annotate2_script.txt`. `geom/TextOutline.cpp` renders glyph outlines. | **Annotations are not annotation objects**: dimensions and text are grouped curves built from font outlines, so they do not re-associate, re-scale with detail scale, or export to Rhino/DXF as DIMENSION/TEXT entities; no dimension-style dialog; 4 hatch patterns, no .pat import; Make2D without hidden-line removal; no GD&T, tables, multi-leaders. |
| **Layouts / printing** | Partial | `cmd_viewtools.cpp` Layout/Detail/Layouts/LayoutProperties/CopyLayout/Import Layout, per-detail `display_mode`, `scale`, `hidden_layers` (`Document.h:190-207`), `DrawLayoutsPanel`; `Print` → vector PDF/SVG via `FileExchange.cpp` `ExportPdf/ExportSvg` with `Scale=` and `PrintDisplay` (tested `exchange_script.txt`, `viewtools_script.txt`, `qpdf --check`). | Print writes the *active view* — there is no multi-sheet / sheet-set print, no raster print, no print dialog (line-weight/colour table); line widths only in PrintDisplay preview ("print colours are planned"); no page-setup UI beyond `LayoutProperties` options. |
| **Rendering / materials / lights** | Partial | `cmd_render.cpp` (924 lines) + `RenderPanels.cpp`: Materials panel (presets, colour/gloss/reflectivity/transparency/emission/texture/mapping, assign to object/layer), Lights (Point/Spot/Directional/Sun/GroundPlane), Environments (colour/gradient/sky), Textures, Render Window; `Application::RenderView` renders the Rendered display mode into an FBO (`GlRenderer.cpp:283`) with supersampling; SaveRenderWindowAs BMP/PPM; materials & lights round-trip in .3dm (tested `render_script.txt`). | **The "renderer" is the OpenGL rasteriser, not a path tracer** (doc4 promised "GPU path tracer, default on… denoise + AOVs"); no shadows/GI/reflections beyond shader tint, no procedural textures, no image environments, no PNG/JPEG output (BMP/PPM only), no render queue/resolution dialog beyond command options. |
| **Display modes** | Mostly | `Viewport.h:17` ten modes (Wireframe, Shaded, Rendered, Ghosted, XRay, Technical, Artistic, Pen, Arctic, Monochrome), viewport-title menu, per-viewport; RenderArctic tested. | Per-object display modes not supported (`SetObjectDisplayMode` note); no display-mode editor (custom modes, edge colours, line widths); Raytraced mode absent; FlatShade is a flag only. |
| **Object snaps / modelling aids** | Mostly | `Viewport.h:35 SnapSettings` End/Mid/Cen/Point/Near/Vertex/Int/Perp/Tan/Quad, grid snap, ortho, planar; status-bar toggles; F-keys (`Application.cpp:1243`). | **No Knot or Project osnap; SmartTrack is a stored flag with no tracking-line logic** (only `Application.cpp:1565` and the Options checkbox reference it); SnapToLocked/Occluded are flags; no one-shot osnaps beyond typed hidden options. |
| **Selection** | Partial | `cmd_select.cpp` + `cmd_select2.cpp` (~110 Sel* filters, SelDup, SelShortCrv, SelKeyValue, SelVolumeSphere, NamedSelections, block/annotation filters — tested `state_script.txt`); window/crossing/Ctrl/Shift in viewport; Selection Filter panel. | **No sub-object selection at all** (edges, faces, vertices, control points) — the single largest interaction gap; SelBrush is one circle; SelChildren/Parents are group members. |
| **Blocks** | Partial | `cmd_drafting.cpp` Block/Insert/`InstantiateBlock`, `cmd_annotate2.cpp` ReplaceBlock, CreateUniqueBlock, RescueBlockOrphans, block attribute keys, ExportLinkedBlocks. | Instances are **grouped copies tagged with the block name** — editing the definition does not update instances except through `BlockEdit`'s redefine step; no linked/embedded reference files; BlockManager is a printout, not a panel; failing round-trip checks in the current tree. |
| **History** | Missing | `cmd_misc.cpp:110-111` History/RecordHistory print "not recorded"; `HistoryPurge/Update` stubs. Undo is whole-document snapshots (`Document.h:5, 371`). | No construction history / associativity anywhere (doc4 explicitly chose "history-free direct editing"). |
| **Scripting / Grasshopper / plug-ins** | Missing | `RunScript`/`RunPythonScript` run command macros (`MacroRunCommand`); `Macro`, `Alias`, `ReadCommandFile`, Macro Editor panel exist. `Grasshopper` and the nine `Grasshopper*` commands, `PlugInManager`, `PackageManager`, `ScriptEditor`, `EditPythonScript` print "not yet available". | No Python/C#/RhinoScript, no SDK ("DinoCommon" from doc4 does not exist), no node editor ("Dino Flow" from doc4 does not exist), no plug-in loading. |
| **File formats** | Partial | `File3dm.cpp` native .3dm via OpenNURBS (layers, names, colours, user text, notes, named views, units, materials, lights, layouts round-trip — tested); OBJ/STL import+export; `FileExchange.cpp` DXF import/export (lines, arcs, circles as rational NURBS, LWPOLYLINE, POINT, 3DFACE meshes, layers — tested), PLY import/export, SVG + PDF export, images BMP/PPM/PNG read for Picture/textures. | Rhino's 50+ formats vs Dino's 7: **no DWG, IGES, STEP, SAT, Parasolid, SKP, FBX, glTF/GLB, 3DS, AI/EPS, SolidWorks, point-cloud formats, E57, VRML, OFF, DAE, X, LWO**; .3dm write does not emit real annotation/hatch/block-instance entities (they are curves/groups); DXF has no splines-as-SPLINE write path for dimensions. |
| **UI: command line** | Complete (for the parts that exist) | `Application.cpp` prompts, clickable option chips, aliases, `_`/`-`/`!` prefixes, Enter-repeat, Esc, relative/polar input, autocomplete table with Up/Down/Tab (`Application.cpp:956-997`), F1 Command List with status filter, Help panel with the full reference text for all 1055 commands. | Autocomplete is prefix-only (`catalog_.WithPrefix`) not the fuzzy palette doc4 describes. |
| **UI: toolbars / icons** | Mostly (with aa263 merged) | aa263 worktree `Icons.cpp` (~95 procedural vector glyphs + ~180 aliases, lettered fallback), `Toolbars.cpp` tabs Standard/Curve/Surface/Solid/Mesh/SubD/Transform/Analyze/Drafting/Render/View with tooltips + right-click alternates, customisable Standard tab, left sidebar, persisted icon size. Main tree has the older text-button toolbar. | ~95 icons for 969 commands; no user-drawn icons, no .rui import/export (`ExportRuiFile` planned), no floating toolbars, no toolbar editor with search. |
| **UI: panels** | Mostly | `Panels.h`: Layers (sublayers, colour, lock, on/off, notes/description, layer states), Properties (object/layer/colour/linetype/material/user text), Command History, Command List, Help, Notifications, Named Views, Notes, Document User Text, Materials, Lights, Rendering, Environments, Textures, Render Window, Display, Calculator, Linetypes, BoxEdit, Undo Multiple, Layer State Manager, Selection Filter, Macro Editor, Clipping Planes, Layouts, Named CPlanes, Options, Document Properties, About. Dockable via ImGui docking; layout persisted. | Missing Rhino panels: Block Manager, Sun (separate), Ground Plane, Snapshots, Named Positions, Web Browser, Libraries, File Explorer (a text browser only), Render Content, Package Manager, Object Snaps (status bar only), Grasshopper. |
| **UI: viewports / gumball / navigation** | Mostly | Four dockable viewports, orbit/pan/zoom, window/crossing select, viewport-title menu, F7/F8/F9/F10, SplitViewportHorizontal/CloseViewport/NewViewport (grid restore), named views, camera commands, animations (turntable/path, RecordAnimation to BMP frames — tested). Gumball move/rotate/scale with Shift snapping. | No ad-hoc floating viewports ("ad-hoc viewports are planned"); no per-object display modes; no 3Dconnexion; no on-gumball numeric entry (doc4 "Beat" claim). |
| **Customisation / options** | Mostly | Options tabs General/Modeling Aids/View/Aliases/Toolbar/Keyboard (`Panels.cpp:678-770`), OptionsExport/Import, theme accent (aa263), light/dark theme, settings in `%APPDATA%\Dino8` / `~/.config/dino8`. | No Appearance/Colours page, no display-mode editor, no Advanced settings grid, no Alerter, no 3Dconnexion page. |
| **Documentation / help** | Complete | Help panel with the full official reference text per command (`data/commands.json` `help` field, 1055 entries), CommandHelp, LearnRhino/Tutorials/WhatsNew open URLs; TESTING.md. | No offline user guide beyond the command pages; no tutorials of Dino's own. |
| **Licensing** | Complete (by design) | `cmd_state.cpp:40 kFree`, `cmd_misc.cpp:101 Licenses`, `Panels.cpp:534,662,679`; About page lists OpenNURBS (MIT) + Manifold (Apache 2.0). | Nothing missing: there is no licence, account, subscription, activation, update check or telemetry code (§4.3). |

---

## 3. UX pain points from the Feature Audit (doc1/doc2 Part 4–6) — does Dino 8 address them?

The audit's theme tally (forum-topic counts): wishlist 740, materials/rendering UI 429, undo/selection/osnap 408, printing/PDF/layouts 386, gumball/PushPull/SubD/ShrinkWrap 385, toolbar/icon cosmetics 363, Mac parity 269, slowness 252, crashes 216, Layers panel 207, fillet/boolean reliability 152, blocks/worksessions 127, constraints/history 102, SpaceMouse 80, MDI/tabs 35.

| # | Pain point (audit) | Dino 8 status | Evidence |
|---|---|---|---|
| 1 | Toolbar system: XML scheme, lost custom toolbars, no .rui export, fuzzy SVG icons at 125 %, **dark-mode icons invisible**, thick separators, 4-dot grips, arrangement resetting on restart | **Partly addressed** | aa263: procedural icons drawn with ImDrawList at 24/32/40 px "crisp on both themes", accent derived from theme, uniform 4 px rounding, hairline separators, tab/size/sidebar persisted. Not addressed: no .rui import/export (`ExportRuiFile` Planned), no toolbar-editor search, no user-drawn icons; only ~95 icons — most buttons fall back to lettered boxes. |
| 2 | Panel docking/resizing quirks, duplicate panels, Linetypes panel shrinking | **Addressed by construction** | ImGui docking with one persisted layout (`Application.cpp` settings); each panel is a single `bool` toggle in `Panels` so duplicates cannot exist. Not verified against the audit's specific regressions — no docking test exists. |
| 3 | Mac/Windows UI unification backlash (right-click display-mode switching removed) | **Addressed** | One codebase; viewport-title menu and right-click context menu offer display modes (aa263 commit text; `MenuBar.cpp` "Display Mode" submenu). |
| 4 | Rhino 8 slower than 7 (layer delete, isolate, copy/paste, navigation); Layers panel lag with 500+ layers; Block Manager freezes; worksession slowness | **Unknown / not addressed** | No performance tests or benchmarks anywhere; `DrawLayersPanel` rebuilds a tree every frame (ImGui immediate mode); Undo snapshots the *whole* document on every change (`Document.h:5` "pushes a full snapshot") — doc4 promised O(change) persistent structures, the code does O(document). Large-model behaviour is untested. |
| 5 | Materials panel UX: scrolling, slow delete, no delete-all, no default material, no text-only list, Notes tab, stacked properties, texture button confusion | **Mostly addressed** | `RenderPanels.cpp:69-196` is a flat list with New/Duplicate/Delete/Rename, presets (Brass, Chrome, Glass…), inline Colour/Gloss/Reflectivity/Transparency/Emission/Texture/Mapping, "Assign to selection / layer"; there is no thumbnail grid, no Notes tab. No "delete all" button, and the material model is far simpler than Rhino's (no PBR channels, no procedural textures). |
| 6 | Crashes / stability | **Unknown** | Headless runs report `gl_error=0`; no crash-reporter, no fuzz/stress tests; the current tree has 4 failing checks. |
| 7 | Printing "nightmare": repeated pages, vector falling back to raster, wrong page size, line weights not scaling with detail scale, hatches not printing, no progress bar, sheet selection not remembered | **Partly addressed / mostly N/A** | Print is always vector (`ExportPdf`/`ExportSvg`), page grows rather than clips at forced scale (`FileExchange.cpp:1110-1112`), `qpdf --check` passes in the test. But there is no multi-page/sheet printing at all, hatches are surfaces/curves so they print as geometry, line weights exist only in `PrintDisplay` preview, no print dialog. |
| 8 | SVG export: closed paths split; missing closing `Z` | **Addressed** | `FileExchange.cpp:1201` writes `" Z"` for closed curves; each curve is one `<path>`. |
| 9 | Layouts: no per-detail memory of display mode; inconsistent scale labels | **Addressed** | `LayoutDetail.display_mode` and `scale` are stored per detail (`Document.h:190-201`) and round-trip through .3dm (`viewtools_script.txt` re-opens the file and lists Layouts). |
| 10 | 2D drafting "20 years late": no live hatched sections, GD&T, tables, multi-leaders, revision tables, BoM | **Not addressed** | Dimensions/text are static curve groups (no associativity), 4 hatch patterns, `ClippingSections` gives static curves, no tables/GD&T/BoM. |
| 11 | Gumball quirks on SubD; PushPull lacks History | **Partly** | Gumball works on whole objects (`Gumball.cpp`); it cannot act on SubD faces/edges because there is no sub-object selection; PushPull does not exist. |
| 12 | ShrinkWrap limits (settings persistence, ballooning face counts, no boolean mode) | **Not addressed — worse** | `ShrinkWrap` note: "Builds the 3D convex hull of the selection (concavities are not followed)"; it is not a shrink-wrap. |
| 13 | Cycles "update lag", no AMD parity, weak native renderer | **Not addressed** | The renderer is the GL rasteriser rendered to an FBO; nothing GPU-vendor-specific; no path tracing. |
| 14 | Fillet / boolean reliability (152 threads; "certain canonical fillet situations Rhino doesn't know how to handle") | **Sidestepped, not solved** | Booleans use Manifold on meshes — robust for closed manifold meshes (skips open ones with a warning, `cmd_boolean.cpp:33`) but the result is a mesh, so the NURBS model is lost; edge fillets do not exist (`FilletEdge` stub). doc4's "exact B-rep intersection with tolerance-based fallback to mesh boolean" is only the fallback half. |
| 15 | OSnap regressions (snaps not catching unselected objects, Project in parallel views) | **Partly** | Snap set is small but deterministic (`SnapSettings`); no Project osnap exists to regress; SmartTrack is a no-op flag. |
| 16 | Undo "randomly" stops with "nothing to undo" after block/material/sun edits | **Addressed** | Full-document snapshot undo (`Document::BeginChange`, `UndoMultiple` panel) cannot desynchronise — at the cost of memory/perf on large files (see #4). |
| 17 | SpaceMouse / 3Dconnexion | **Not addressed** | No 3Dconnexion code or options page. |
| 18 | No document tabs / MDI | **Same as Rhino** | Single document per process (`Application::NewDocument`). |
| 19 | Declined feature: parametric constraints | **Not addressed** | None (and none promised by doc4). |
| 20 | Declined feature: Class-A tools (G2/G3 continuity, multi-edge corner fillets, CV-count control) | **Not addressed** | BlendSrf Planned, MatchSrf stub, Blend is G1 only. |
| 21 | Declined feature: GD&T symbol set, tables, BoM | **Not addressed** | — |
| 22 | Declined feature: live multi-user collaboration | **Not addressed** | No networking at all (by design). |
| 23 | Declined feature: Windows-on-ARM build | **Not addressed** | CI builds x64 Windows/Linux and Apple-Silicon macOS only. |
| 24 | Declined feature: **layer notes / document metadata** | **Addressed** | `Document.h:29 std::string description; // the "layer notes" Rhino 8 users asked for`; Layers panel "Notes"; `DocumentUserText`, `SetDocumentUserText`, Notes panel, document properties. |
| 25 | Declined feature: full hatch library with colour/bitmap fills and draw order | **Not addressed** | 4 patterns; draw order is a user-text tag (`BringToFront` note "Draw order is stored in user text") with no rendering effect. |
| 26 | Declined feature: parametric architectural components (VisualARQ) | **Not addressed** | — |
| 27 | Declined feature: reworked Materials panel | **Addressed (simplified)** | see #5 |
| 28 | Review-synthesis cons: learning curve, weak native render, boolean/fillet failures, price | **Price: solved** (free, no licence); the rest unchanged or worse (no fillets). |
| 29 | Praised: command line + autocomplete + mouse | **Preserved** | §2 "UI: command line". |
| 30 | doc4 blueprint promises not delivered: GPU path tracer, material library, "Dino Flow" node editor, DinoCommon .NET SDK, exact SubD→NURBS, O(change) undo, fuzzy command palette, layout state in physical units, EV code-signed installer, e-mail/Drive update flow | **Not delivered** | None of these exist in the source; the Inno Setup script exists (`packaging/windows/Dino8.iss`) but no signing; README says "no update nags" and there is no updater. |

---

## 4. "Before you install" verdict

### 4.1 What works well (and is tested)
- **Rhino muscle memory**: every one of the 1055 command names is accepted; prompts, option chips, aliases, Enter-repeat, relative/polar coordinates, F1 command list with Implemented/Partial/Planned badges, and the full official help text for each command.
- **Primitives, curves and the everyday toolkit**: lines/polylines/NURBS/interpolated curves, circles/arcs/ellipses/conics/catenaries, boxes/spheres/cylinders/cones/tori/tubes, ExtrudeCrv (capped solids), Revolve/RailRevolve, Loft, PlanarSrf, Sweep1/2, NetworkSrf (small networks), Pipe, OffsetSrf, Trim/Split/Fillet/Chamfer on curves, Join/Explode/Rebuild/Offset/Extend, all transforms and arrays, Gumball, groups, layers with sub-layers/notes/states, hide/lock/isolate, snapshot undo/redo.
- **Mesh and SubD work**: mesh booleans (Manifold), mesh repair/weld/offset/extrude/deform, SubD primitives with crease/extrude/inset/bridge/offset/insert-edge, AutomaticSubDFromMesh.
- **Files**: .3dm read/write through OpenNURBS that round-trips layers, materials, lights, layouts, named views, user text; OBJ/STL/PLY/DXF in and out; vector PDF/SVG "printing" of the active view.
- **Panels & display**: ten display modes, Zebra/EMap/curvature/draft-angle shading, Materials/Lights/Environments/Textures panels, clipping planes with section extraction, layouts with details, named CPlanes, turntable/path animation to frames, light/dark theme, persisted layout.
- **Free, offline, honest**: the app tells you in the command line when something is Partial and exactly how it deviates.

### 4.2 What is approximate (read the note before relying on it)
- **Booleans are mesh booleans.** Union/Difference/Intersection of NURBS solids convert to meshes and return a mesh; you cannot fillet, untrim, or extract exact surfaces afterwards. BooleanSplit/WireCut are plane cuts.
- **No edge fillets or chamfers on solids** (FilletEdge/ChamferEdge/BlendEdge print a message). Curve Fillet/Chamfer are exact for lines, approximate for other curves.
- **Surface editing is thin**: MatchSrf/FilletSrf/BlendSrf/MergeSrf/SrfSeam/MoveFace etc. are stubs or planned; Patch is a plane fit; Loft has one style; Shell/OffsetSrf on polysurfaces return meshes.
- **Annotation is geometry**: Dim/Text/Leader/Hatch produce grouped curves/surfaces built from font outlines — they don't update, don't scale with detail scale, and do not export as real DIMENSION/TEXT/HATCH entities.
- **Blocks are tagged groups**, not instances; History does not exist; ShrinkWrap is a convex hull; QuadRemesh, ReduceMesh, MeshOutline, Make2D (no hidden-line removal), Drape, Heightfield are simplified.
- **Selection is whole-object only**: no control-point dragging, no edge/face/vertex picking, so all the "Sel sub-object" commands and point-editing commands are stubs.
- **Rendering is the OpenGL viewport rendered offscreen** (BMP/PPM output), not a path tracer.
- **Printing is one view → one PDF/SVG**; no multi-sheet print, no print dialog, no line-weight table.
- **Osnaps** lack Knot and Project; SmartTrack is a checkbox with no behaviour.
- **Formats**: seven (3dm, obj, stl, ply, dxf, svg, pdf). No DWG/IGES/STEP/SKP/FBX/glTF/AI.

### 4.3 What is absent
Grasshopper or any node editor, Python/C#/RhinoScript, plug-ins/SDK, PushPull, construction history, IGES/STEP/DWG, sub-object selection, exact edge fillets, Class-A surfacing (G2+ blends, MatchSrf), per-object display modes, display-mode editor, real annotation objects/dimension styles, hatch library, Block Manager panel, digitizers, 3Dconnexion, worksessions, Snapshots, NamedPositions, Web Browser/Libraries/Render Content panels, .rui toolbars, native file dialogs outside Windows, any updater.

### 4.4 Hard-constraint check: no licensing, subscription, payment, telemetry or network code
`grep -rniE "licen[cs]e|subscri|payment|telemetr|analytics|curl|http://|https://|socket|WinHttp|WinINet|boost::asio|getaddrinfo|connect\(|send\(|recv\(|URLDownload|ShellExecute|xdg-open"` over `src/`, `CMakeLists.txt` and `packaging/` finds only:
- Free-software statements: `src/commands/cmd_state.cpp:40` (`kFree`), `:619` (CheckInLicense/CheckOutLicense/Login/Logout/Libraries/DownloadLibraryTextures → print `kFree`), `src/commands/cmd_misc.cpp:101` (`Licenses` → About panel), `src/commands/cmd_analyze.cpp:309` (version line), `src/ui/Panels.cpp:534,662,679` (Options/About text).
- `src/commands/cmd_state.cpp:328-341` `OpenUrl` for `OpenURL`/`WebBrowser`: launches the **system browser** (`xdg-open` / platform equivalent) on an http(s) URL the *user types*; the app itself makes no request. `TechSupport` prints a GitHub issues URL.
- `src/io/File3dm.cpp:597` writes the project URL into the .3dm application-URL metadata string (data, not a request).
- `CMakeLists.txt` FetchContent of GLFW/ImGui/OpenNURBS/Manifold at **build** time only; `packaging/windows/Dino8.iss` publisher/support/updates URLs are Inno Setup metadata strings.
- No sockets, no HTTP client, no analytics library, no update check, no activation, no account UI. `CheckForUpdates`/`WhatsNew`/`LearnRhino` open web pages in the user's browser. Verdict: **constraint satisfied.**

### 4.5 Build/test state at audit time
The main checkout is mid-cherry-pick with 11 conflicted files (Application.cpp/.h, Document.cpp/.h, File3dm.cpp, GlRenderer.cpp/.h, Camera.cpp, Viewport.cpp/.h, smoke.sh). The binary built at 20:50 passed 302 checks and failed 4 (linetype/block round-trip). Do not ship this tree until the conflicts are resolved and `smoke.sh` is green again.

---

## 5. Top 25 gaps to close next (prioritised)

1. **Resolve the merge and get `smoke.sh` green** — 4 failing block/linetype round-trip checks; conflicts in Document/File3dm/Viewport.
2. **Sub-object selection** (edges/faces/vertices/control points) with control-point dragging — unblocks ~40 stubbed Sel*/point-edit commands, Gumball on SubD faces, PushPull, MoveFace.
3. **Exact edge fillet/chamfer/blend on B-reps** (FilletEdge/ChamferEdge/BlendEdge) — the most-cited Rhino feature and currently print-only; needs a surface/surface intersector in `dino8-kernel/brep.h`.
4. **B-rep booleans** (keep NURBS output; fall back to Manifold meshes only on failure) — the architecture doc4 actually promised.
5. **Real annotation objects** (dimension/text/leader/hatch entities with styles, associativity, detail-scale awareness) and their .3dm/DXF export as DIMENSION/TEXT/HATCH.
6. **PushPull** (absent from catalogue and code) and MoveFace/MoveEdge/SplitFace/MergeFaces — the "SketchUp-simple" workflow reviewers praised in Rhino 8.
7. **Surface matching/blending**: MatchSrf, BlendSrf (Planned), FilletSrf, MergeSrf, SrfSeam, ExtendSrf-by-edge; Blend/BlendCrv with G2 option.
8. **Add test scripts for the 363 untested Implemented commands** (edit, transform, layer, view, select, analyze) and a small large-model performance benchmark.
9. **Knot/Project osnaps, SmartTrack tracking lines, one-shot osnaps** — closes the audit's 408-thread snap category.
10. **IGES/STEP import/export** (STEPTree, IgesImportOptions are Planned) and DWG — Rhino's interoperability is a top-3 review "pro".
11. **Multi-sheet printing with a print dialog** (page setup, line-weight/colour table, all layouts → one PDF, raster option).
12. **Block instances as real references** (linked/embedded definitions, instance transforms, Block Manager panel) — instances are currently groups.
13. **Python scripting** (RunPythonScript/EditPythonScript/ScriptEditor) with a documented object model; without it there is no automation beyond command macros.
14. **Grasshopper-class node editor** ("Dino Flow" in doc4) — the differentiating Rhino feature the audit's reviewers name most; even a minimal graph over existing commands would matter.
15. **Rendering**: shadows/reflections/GI (at minimum a CPU path tracer for `Render`), PNG/JPEG output, image environments, procedural textures, a render-settings dialog.
16. **Loft styles, NetworkSrf for >4 curves, true Patch (surface fit), Sweep with multiple sections/Roadlike** — the surface commands exist but only in their simplest form.
17. **Mesh editing with picked sub-elements** (collapse/split/swap by pick, ReduceMesh by target count, real QuadRemesh, real ShrinkWrap).
18. **Per-object display modes and a display-mode editor** (SetObjectDisplayMode, DisplayProperties is Planned).
19. **Toolbar completeness**: icons for the remaining ~870 commands, .rui import/export, floating/custom toolbars, toolbar editor with search (audit item #1 and doc4's "one canonical registry").
20. **Analysis gaps**: EdgeContinuity, GCon, ThicknessAnalysis, EvaluateUVPt, on-screen CurvatureGraph, second moments in AreaMoments/VolumeMoments.
21. **Undo memory**: replace full-document snapshots with per-change deltas before large models (`Document.h:5`) — doc4's O(change) promise and the audit's performance category.
22. **Layout/detail fidelity**: detail-scale-aware dimensions, detail viewport editing (double-click), scale labels, layout state in physical units.
23. **Hatch library** (.pat import, more than 4 patterns, draw order that actually affects display).
24. **Missing panels**: Block Manager, Sun, Ground Plane, Snapshots, Named Positions, Object Snaps panel, File Explorer with thumbnails; native file dialogs on Linux/macOS.
25. **Stale notes and status hygiene**: Gumball note says rotate/scale "planned" though implemented; `Text` double-registration; 11 "Implemented" print-only stubs should be Partial; add a `PushPull` catalogue entry.

---

## Appendix A — All 377 Partial commands with their notes

| Command | Note (verbatim) | Print-only stub? | Source |
|---|---|---|---|
| AddGuide | Guide curves are not stored. | yes | cmd_subd.cpp |
| AddMissingBlockAttributeKeys | Adds empty user-text keys (Keys=a,b) to instances and their definition. | yes | cmd_annotate2.cpp |
| AddNgonsToMesh | Not yet available in this build. | yes | cmd_meshtools.cpp |
| ApplyCrv | (no note) | yes | cmd_srfedit.cpp |
| ApplyCustomMapping | Planar projection in the object's box; custom source objects are planned. | yes | cmd_render.cpp |
| ApplyMesh | (no note) | yes | cmd_srfedit.cpp |
| ApplyMeshUVN | (no note) | yes | cmd_srfedit.cpp |
| ArcBlend | Builds a tangent cubic blend instead of a two-arc blend. | yes | cmd_curves2.cpp |
| AreaCentroid | Bounding-box centroid estimate. | yes | cmd_analyze.cpp |
| AreaMoments | Area and centroid; second moments are planned. | yes | cmd_srfedit.cpp |
| ArrayCrvOnSrf | Behaves like ArrayCrv; surface-normal orientation is planned. | yes | cmd_curves2.cpp |
| ArrayHole | Rectangular grid of round holes along the CPlane axes; profile holes are planned. | yes | cmd_solidtools.cpp |
| ArrayHolePolar | Polar array of round holes about the CPlane normal; profile holes are planned. | yes | cmd_solidtools.cpp |
| ArrayLinear | Uses the rectangular array with Y and Z counts of 1. | yes | cmd_transform.cpp |
| BackgroundBitmap | (no note) | yes | cmd_render.cpp |
| Bake | (no note) | yes | cmd_render.cpp |
| BakeMapping | (no note) | yes | cmd_render.cpp |
| Blend | Tangent (G1) blend; curvature continuity option is planned. | yes | cmd_curves2.cpp |
| BlendEdge | (no note) | yes | cmd_srfedit.cpp |
| Block | Instances are grouped copies tagged with the block name (not linked). | yes | cmd_drafting.cpp |
| BlockEdit | Places an editable copy of the definition at the instance; running BlockEdit again redefines the block from it. | yes | cmd_annotate2.cpp |
| BlockManager | Lists definitions in the command history; a panel is planned. | yes | cmd_drafting.cpp |
| BlockResetScale | Re-inserts the instance at its insertion point, dropping any scaling or rotation. | yes | cmd_annotate2.cpp |
| Boolean2Objects | Produces the symmetric difference; cycling through results is planned. | yes | cmd_boolean.cpp |
| BooleanSplit | Plane split. | yes | cmd_boolean.cpp |
| Boss | (no note) | yes | cmd_srfedit.cpp |
| Bounce | Bounces a ray off the visible meshes and surfaces (as meshes). | yes | cmd_solidtools.cpp |
| BringForward | Draw order is stored in user text. | yes | cmd_state.cpp |
| BringToFront | Draw order is stored in user text. | yes | cmd_state.cpp |
| CalcRPN | Uses infix notation. | yes | cmd_misc.cpp |
| Cap | Caps a single planar opening per polysurface. | yes | cmd_solids.cpp |
| ChamferEdge | (no note) | yes | cmd_srfedit.cpp |
| ChamferSrf | (no note) | yes | cmd_srfedit.cpp |
| ChangeDegree | Raises degree by one; typed target degree is planned. | yes | cmd_edit.cpp |
| ClearDrawOrder | Draw order is stored in user text. | yes | cmd_state.cpp |
| ClippingDrawings | Use ClippingSections + Print. | yes | cmd_viewtools.cpp |
| CollapseMeshEdge | Collapses the shortest edge of each mesh; edge picking is planned. | yes | cmd_meshtools.cpp |
| CollapseMeshFace | Collapses the smallest face of each mesh; face picking is planned. | yes | cmd_meshtools.cpp |
| CollapseMeshVertex | Collapses the shortest edge of each mesh; vertex picking is planned. | yes | cmd_meshtools.cpp |
| CommandPrompt | Stored; the command line stays visible so you can always type. | yes | cmd_state.cpp |
| ComputeVertexColors | Colours vertices by normal; the display does not show vertex colours yet. | yes | cmd_meshtools.cpp |
| ConnectSrf | (no note) | yes | cmd_srfedit.cpp |
| ContentFilter | (no note) | yes | cmd_state.cpp |
| ContinueCurve | Draws a new control-point curve; Join it to the original. | yes | cmd_curves2.cpp |
| ContinueInterpCrv | Draws a new interpolated curve; Join it to the original. | yes | cmd_curves2.cpp |
| ConvertExtrusion | (no note) | yes | cmd_srfedit.cpp |
| ConvertTextToBlockAttribute | Tags the text's objects with Key=text user text. | yes | cmd_annotate2.cpp |
| CopyHole | Holes are not feature objects in this build; prints guidance. | yes | cmd_solidtools.cpp |
| CopyRenderWindowToClipboard | Writes the image to a file next to the settings instead of the clipboard. | yes | cmd_render.cpp |
| CreateRegions | Every region of up to 6 overlapping closed curves; open-curve networks are planned. | yes | cmd_solidtools.cpp |
| CreateSolid | Joins and welds the surface meshes into a closed mesh solid; overlapping surfaces are not trimmed. | yes | cmd_solidtools.cpp |
| CSec | Runs Section once per call. | yes | cmd_curves2.cpp |
| Curvature | Prints curvature at 11 samples; the on-screen graph is planned. | yes | cmd_analyze.cpp |
| CurvatureGraph | Draws the graph as a curve object; delete it when done. | yes | cmd_analyze.cpp |
| CurveThroughPt | Picks points instead of selecting point objects. | yes | cmd_create.cpp |
| CurveThroughSrfControlPt | Extracts surface control points; run CurveThroughPt on them. | yes | cmd_curves2.cpp |
| CutVolume | Intersects the curves' extrusion with the solids and reports the volume (mesh result). | yes | cmd_solidtools.cpp |
| Diameter | (no note) | yes | cmd_analyze.cpp |
| DigBeep | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| DigCalibrate | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| DigCamera | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| DigClick | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| DigDisconnect | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| Digitize | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| DigLine | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| DigPause | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| DigScale | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| DigSection | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| DigSketch | No digitizer is connected; 3D digitizer support is planned. | yes | cmd_state.cpp |
| Dim | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate.cpp |
| DimAligned | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate.cpp |
| DimAngle | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate.cpp |
| DimArea | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate2.cpp |
| DimCreaseAngle | Angle between two lines or the first planar faces of two objects; no face picking on polysurfaces yet. | yes | cmd_annotate2.cpp |
| DimCurveLength | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate2.cpp |
| DimDiameter | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate.cpp |
| DimOrdinate | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate2.cpp |
| DimRadius | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate.cpp |
| DimRecenterText | Rebuilds the text at the position it was created with. | yes | cmd_annotate2.cpp |
| DimRotated | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate.cpp |
| DimVolume | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate2.cpp |
| Dir | Reports direction; interactive flip arrows are planned. | yes | cmd_edit.cpp |
| DisplayCommandPrompt | The command line is always shown. | yes | cmd_state.cpp |
| DollyZoom | Type the lens length; interactive dragging is planned. | yes | cmd_state.cpp |
| Domain | Reports the domain; use Reparameterize to change it. | yes | cmd_curves2.cpp |
| DownloadLibraryTextures | (no note) | yes | cmd_render.cpp |
| DragCopy | Stored; Alt-drag copies. | yes | cmd_state.cpp |
| DragMode | Stored; dragging follows the CPlane. | yes | cmd_state.cpp |
| DragStrength | Stored flag. | yes | cmd_state.cpp |
| Drape | Drapes a grid mesh over the visible objects along the CPlane normal. | yes | cmd_meshtools.cpp |
| DupMeshEdge | Duplicates the whole naked-edge chain nearest the pick. | yes | cmd_meshtools.cpp |
| EarthAnchorPoint | Stores the model point; latitude/longitude are planned. | yes | cmd_state.cpp |
| EdgeSrf | Uses NetworkSrf for 2, 3 or 4 edge curves. | yes | cmd_srfedit.cpp |
| EditClippingDrawings | Use ClippingSections + Print. | yes | cmd_viewtools.cpp |
| EditLightByHighlight | Uses the Lights panel; interactive highlight editing is planned. | yes | cmd_render.cpp |
| EditLightByLooking | Uses SetSpotlightToView / the Lights panel. | yes | cmd_render.cpp |
| Ellipsoid | Axis ratios 1 : 0.7 : 0.5; axis picking is planned. | yes | cmd_solids.cpp |
| EndBulge | (no note) | yes | cmd_curves2.cpp |
| Environments | Colour, gradient and sky backgrounds; image environments are planned. | yes | cmd_render.cpp |
| ExportClippingDrawings | Use ClippingSections + Print. | yes | cmd_viewtools.cpp |
| ExportLinkedBlocks | Saves the definition's objects to a .3dm (Name=, Path=); the block stays embedded. | yes | cmd_annotate2.cpp |
| ExportWithOrigin | Exports without re-basing the origin. | yes | cmd_file.cpp |
| Extend | Extends both ends by 10% of the domain; picking the extension is planned. | yes | cmd_edit.cpp |
| ExtendCrvOnSrf | Extends in space, not along the surface. | yes | cmd_curves2.cpp |
| ExtractAnalysisMesh | Extracts the display mesh via Mesh. | yes | cmd_srfedit.cpp |
| ExtractBadSrf | (no note) | yes | cmd_srfedit.cpp |
| ExtractClippingSlices | Extracts the section curves; planar slice surfaces are planned. | yes | cmd_viewtools.cpp |
| ExtractMeshFaces | Extracts the face nearest a picked point; multi-face selection is planned. | yes | cmd_meshtools.cpp |
| ExtractOriginalCaptives | Originals are kept for the session only, not in the file. | yes | cmd_solidtools.cpp |
| ExtractPipedCurve | (no note) | yes | cmd_srfedit.cpp |
| FileExplorer | Prints the folder; use Open for the file browser. | yes | cmd_state.cpp |
| FilletEdge | (no note) | yes | cmd_srfedit.cpp |
| FilletSrf | (no note) | yes | cmd_srfedit.cpp |
| FilletSrfCrv | (no note) | yes | cmd_srfedit.cpp |
| FilletSrfToRail | (no note) | yes | cmd_srfedit.cpp |
| FitCurveToSurface | (no note) | yes | cmd_srfedit.cpp |
| FitSrf | (no note) | yes | cmd_srfedit.cpp |
| FixedLengthCrvEdit | (no note) | yes | cmd_curves2.cpp |
| FlatShade | Display toggle not yet available; Unweld a mesh for faceted shading. | yes | cmd_meshtools.cpp |
| FlattenSrf | (no note) | yes | cmd_srfedit.cpp |
| FoldFace | (no note) | yes | cmd_srfedit.cpp |
| GrasshopperDeveloperSettings | Plug-ins and Grasshopper are not yet available in Dino 8. | yes | cmd_state.cpp |
| GrasshopperFolders | Plug-ins and Grasshopper are not yet available in Dino 8. | yes | cmd_state.cpp |
| GrasshopperGetSDKDocumentation | Plug-ins and Grasshopper are not yet available in Dino 8. | yes | cmd_state.cpp |
| GrasshopperIgnorePlugin | Plug-ins and Grasshopper are not yet available in Dino 8. | yes | cmd_state.cpp |
| GrasshopperLoadOneByOne | Plug-ins and Grasshopper are not yet available in Dino 8. | yes | cmd_state.cpp |
| GrasshopperPlayer | Plug-ins and Grasshopper are not yet available in Dino 8. | yes | cmd_state.cpp |
| GrasshopperPluginList | Plug-ins and Grasshopper are not yet available in Dino 8. | yes | cmd_state.cpp |
| Gumball | Move handles; rotate/scale handles are planned. | yes | cmd_misc.cpp |
| GumballAlignment | Stored; the widget uses world axes. | yes | cmd_state.cpp |
| GumballDynamicRelocate | Stored flag. | yes | cmd_state.cpp |
| GumballRelocate | Stored; the widget draws at the selection centre. | yes | cmd_state.cpp |
| GumballScaleMode | Stored; handles scale independently. | yes | cmd_state.cpp |
| HandleCurve | (no note) | yes | cmd_curves2.cpp |
| Hatch | Solid fills become planar surfaces; line patterns are curve groups. | yes | cmd_drafting.cpp |
| Heightfield | Grid mesh from a sine-wave function; image input is planned. | yes | cmd_meshtools.cpp |
| HidePt | Hides the control points of the selected objects. | yes | cmd_select2.cpp |
| HideRenderMesh | (no note) | yes | cmd_render.cpp |
| History | (no note) | yes | cmd_misc.cpp |
| Hydrostatics | Reports volume and centroid; waterline-specific values are planned. | yes | cmd_srfedit.cpp |
| ImportLayout | Imports the page and detail cameras; per-detail hidden objects are not mapped. | yes | cmd_viewtools.cpp |
| InfinitePlane | Creates a very large plane surface. | yes | cmd_state.cpp |
| Insert | Inserts a block definition, or imports a file when none exist. | yes | cmd_drafting.cpp |
| InsertKnot | Inserts a knot at the domain midpoint; picking is planned. | yes | cmd_edit.cpp |
| InsertLineIntoCrv | (no note) | yes | cmd_curves2.cpp |
| InsertPoint | Splits the nearest edge at the pick; the adjacent faces gain a corner (no connecting edges). | yes | cmd_subd.cpp |
| InterpCrvOnSrf | Interpolates in space; snap to the surface with Osnap. | yes | cmd_curves2.cpp |
| IntersectSelf | Uses Intersect on the selection. | yes | cmd_curves2.cpp |
| IntersectTwoSets | Uses Intersect on the selection. | yes | cmd_curves2.cpp |
| InvertPt | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| JoinEdge | (no note) | yes | cmd_srfedit.cpp |
| Leader | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate.cpp |
| LimitReferenceModel | (no note) | yes | cmd_state.cpp |
| LockViewport | Records the lock; mouse navigation still works. | yes | cmd_state.cpp |
| Loft | Normal loft; Loose/Tight/Straight styles are planned. | yes | cmd_solids.cpp |
| Maelstrom | Rotates about the CPlane normal through the centre. | yes | cmd_meshtools.cpp |
| Make2D | Projects visible wire geometry; hidden-line removal is planned. | yes | cmd_drafting.cpp |
| MakePeriodic | (no note) | yes | cmd_edit.cpp |
| MakeSubDFriendly | Rebuilds as degree-3 uniform curves/surfaces through sampled points (approximate). | yes | cmd_subd.cpp |
| MappingWidget | (no note) | yes | cmd_render.cpp |
| MappingWidgetOff | (no note) | yes | cmd_render.cpp |
| MarkFoci | Marks foci of ellipses and centers of arcs; parabola/hyperbola foci are planned. | yes | cmd_curves2.cpp |
| Match | Creates a tangent blend between the curve ends instead of moving the end of one curve. | yes | cmd_curves2.cpp |
| MatchMeshEdge | Moves naked-edge vertices of the meshes together when within the distance; run Weld afterwards to join. | yes | cmd_meshtools.cpp |
| MatchSrf | (no note) | yes | cmd_srfedit.cpp |
| Merge2MeshFaces | Merges the triangle nearest the pick with an adjacent triangle. | yes | cmd_meshtools.cpp |
| MergeAllCoplanarFaces | (no note) | yes | cmd_srfedit.cpp |
| MergeAllEdges | (no note) | yes | cmd_srfedit.cpp |
| MergeCoplanarFace | (no note) | yes | cmd_srfedit.cpp |
| MergeCrv | Joins the selected curves; single-span merging of tangent segments is planned. | yes | cmd_curves2.cpp |
| MergeEdge | (no note) | yes | cmd_srfedit.cpp |
| MergeFaces | (no note) | yes | cmd_srfedit.cpp |
| MergeSrf | Refits one surface through samples of both. | yes | cmd_srfedit.cpp |
| MeshBooleanSplit | Plane split. | yes | cmd_boolean.cpp |
| MeshFromLines | Builds faces from closed 3- and 4-line loops. | yes | cmd_meshtools.cpp |
| MeshOutline | Convex hull of the mesh projected on the CPlane. | yes | cmd_meshtools.cpp |
| MeshSplit | Plane split. | yes | cmd_boolean.cpp |
| MeshTrim | Trims with a plane through two points (normal to the CPlane); open meshes lose whole faces. | yes | cmd_meshtools.cpp |
| MigratePlugins | Plug-ins and Grasshopper are not yet available in Dino 8. | yes | cmd_state.cpp |
| MirrorHole | Holes are not feature objects in this build; prints guidance. | yes | cmd_solidtools.cpp |
| ModifyRadius | Interactive radius editing is planned; use Scale about the center. | yes | cmd_curves2.cpp |
| MoveCrv | (no note) | yes | cmd_curves2.cpp |
| MoveEdge | (no note) | yes | cmd_srfedit.cpp |
| MoveExtractedIsocurve | (no note) | yes | cmd_srfedit.cpp |
| MoveFace | (no note) | yes | cmd_srfedit.cpp |
| MoveHole | Holes are not feature objects in this build; prints guidance. | yes | cmd_solidtools.cpp |
| MoveUntrimmedEdge | (no note) | yes | cmd_srfedit.cpp |
| MoveUntrimmedFace | (no note) | yes | cmd_srfedit.cpp |
| MoveUVN | (no note) | yes | cmd_srfedit.cpp |
| MPlane | Follows the object's bounding-box centre; orientation tracking is planned. | yes | cmd_viewtools.cpp |
| NamedPosition | (no note) | yes | cmd_state.cpp |
| NestedClippingDrawing | Use ClippingSections + Print. | yes | cmd_viewtools.cpp |
| NewViewport | Restores the 4-viewport layout; ad-hoc viewports are planned. | yes | cmd_view.cpp |
| NonmanifoldMerge | Runs Join. | yes | cmd_solidtools.cpp |
| OffsetCrvOnSrf | Planar offset; offsetting along the surface is planned. | yes | cmd_curves2.cpp |
| OffsetMultiple | Runs Offset once; repeat for multiple offsets. | yes | cmd_curves2.cpp |
| OffsetSrf | Surfaces: control points offset along Greville normals (exact for planes). Polysurfaces and meshes are offset as meshes along vertex normals; Solid=Yes closes the shell as a mesh. | yes | cmd_surface.cpp |
| Orient | Uses three reference / target points. | yes | cmd_transform.cpp |
| OrientCameraToSrf | Uses the surface centre; picking a point on the surface is planned. | yes | cmd_viewtools.cpp |
| OrientCrvToEdge | Prints guidance. | yes | cmd_solidtools.cpp |
| OrthoAngle | Stored; ortho constrains to 90 degree steps. | yes | cmd_state.cpp |
| OrthoSnapToCPlaneZ | Stored flag. | yes | cmd_state.cpp |
| PackageManager | (no note) | yes | cmd_misc.cpp |
| PackSubDFaces | Reports the face count; texture packs are not stored. | yes | cmd_subd.cpp |
| PackTextures | (no note) | yes | cmd_render.cpp |
| Pan | Interactive pan is always available with the mouse. | yes | cmd_view.cpp |
| Paraboloid | Mesh output; a NURBS paraboloid is planned. | yes | cmd_meshtools.cpp |
| PatchSingleFace | (no note) | yes | cmd_srfedit.cpp |
| PerspectiveMatch | (no note) | yes | cmd_viewtools.cpp |
| Picture | Places a BMP/PPM/PNG image on a plane; PictureFrame options are planned. | yes | cmd_render.cpp |
| PlanarIntersection | Same as Section (intersects objects with a plane through two points). | yes | cmd_curves2.cpp |
| PlugInManager | Plug-ins and Grasshopper are not yet available in Dino 8. | yes | cmd_state.cpp |
| PointCloud | Groups the points; a dedicated point-cloud object is planned. | yes | cmd_state.cpp |
| PointGrid | 5 x 5 grid; count options are planned. | yes | cmd_create.cpp |
| PolylineOnMesh | Straight segments between points pulled to the mesh. | yes | cmd_meshtools.cpp |
| PopupMenu | Opens the command list; middle-click a viewport for the popup toolbar. | yes | cmd_state.cpp |
| PopupPopular | Lists recent commands and opens the command list. | yes | cmd_state.cpp |
| PrintDisplay | Previews line widths; print colours are planned. | yes | cmd_viewtools.cpp |
| Pyramid | Four-sided mesh pyramid. | yes | cmd_solids.cpp |
| QuadRemesh | Surfaces are sampled on a UV grid; other objects have their triangles paired into quads. | yes | cmd_subd.cpp |
| Radiate | Prints guidance. | yes | cmd_solidtools.cpp |
| RadiateFind | Prints guidance. | yes | cmd_solidtools.cpp |
| Radius | Reports the radius at the curve midpoint; picking a point is planned. | yes | cmd_analyze.cpp |
| ReadViewportsFromFile | Applies cameras to same-named viewports; window positions are not restored. | yes | cmd_viewtools.cpp |
| RebuildEdges | Recomputes edge tolerances; edge curve refitting is planned. | yes | cmd_srfedit.cpp |
| RebuildUV | Uniformizes knots; use Rebuild for point counts. | yes | cmd_srfedit.cpp |
| RedoView | (no note) | yes | cmd_view.cpp |
| ReduceMesh | Simplifies with a fixed tolerance; target count is planned. | yes | cmd_boolean.cpp |
| RefitTrim | (no note) | yes | cmd_srfedit.cpp |
| Reflect | Prints guidance. | yes | cmd_solidtools.cpp |
| RememberCopyOptions | Stored flag. | yes | cmd_state.cpp |
| RemoveAllNakedMicroEdges | (no note) | yes | cmd_srfedit.cpp |
| RemoveGuide | Guide curves are not stored. | yes | cmd_subd.cpp |
| RemovePerFaceColors | (no note) | yes | cmd_render.cpp |
| RemoveSymmetry | (no note) | yes | cmd_curves2.cpp |
| RenderBlowup | Crops a full-view render to the picked region. | yes | cmd_render.cpp |
| RenderOpenLastRendering | Shows the last in-session rendering; opening image files is planned. | yes | cmd_render.cpp |
| ReplaceEdge | (no note) | yes | cmd_srfedit.cpp |
| Reset | Keeps recent files, theme and window layout. | yes | cmd_state.cpp |
| Rib | (no note) | yes | cmd_srfedit.cpp |
| RLeaderEdit | Edits the leader text; leader points stay. | yes | cmd_annotate2.cpp |
| Rotate3D | Rotates about the CPlane normal; arbitrary axis picking is planned. | yes | cmd_transform.cpp |
| RotateCamera | (no note) | yes | cmd_view.cpp |
| RotateHole | Holes are not feature objects in this build; prints guidance. | yes | cmd_solidtools.cpp |
| RotateView | (no note) | yes | cmd_view.cpp |
| RTextEdit | Same as TextProperties. | yes | cmd_annotate2.cpp |
| Run | External programs are never run; use your shell. | yes | cmd_state.cpp |
| RunPythonScript | Runs command macros; Python scripting is planned. | yes | cmd_misc.cpp |
| RunScript | Runs command macros; Python scripting is planned. | yes | cmd_misc.cpp |
| SaveAsTemplate | Saves a normal .3dm you can open as a starting point. | yes | cmd_file.cpp |
| SaveSmall | Dino 8 never stores render meshes, so every save is already small. | yes | cmd_file.cpp |
| ScaleNU | Scales along the CPlane X axis; per-axis factors are planned. | yes | cmd_transform.cpp |
| ScreenCaptureToClipboard | Captures the active viewport to a file next to the settings. | yes | cmd_state.cpp |
| ScreenCaptureToFile | Captures the active viewport. | yes | cmd_view.cpp |
| ScriptEditor | (no note) | yes | cmd_misc.cpp |
| SelAnnotationStyle | Selects all annotation (one style is used). | yes | cmd_select2.cpp |
| SelBoundary | Selects closed curves. | yes | cmd_select.cpp |
| SelBox | Window selection in the active view. | yes | cmd_select.cpp |
| SelBrush | Selects inside one circle; painting a brush stroke is planned. | yes | cmd_select2.cpp |
| SelBrushPoints | Selects whole objects inside one circle. | yes | cmd_select2.cpp |
| SelChildren | Selects the other members of the selected objects' groups. | yes | cmd_select2.cpp |
| SelControlPoint | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelControlPointRegion | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelDimOverride | (no note) | yes | cmd_select2.cpp |
| SelDimTextOverride | (no note) | yes | cmd_select2.cpp |
| SelEdgeLoop | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelEdgeRing | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelExtrusion | Dino 8 stores extrusions as polysurfaces; selects every polysurface. | yes | cmd_select2.cpp |
| SelFaceLoop | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelFacesToBoundary | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelFontUse | Selects all annotation (one font is used). | yes | cmd_select2.cpp |
| SelMeshEdges | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelMeshFacesByArea | Extracts (and selects) the matching faces as a new mesh instead of highlighting them. | yes | cmd_meshtools.cpp |
| SelMeshFacesByAspectRatio | Extracts (and selects) the matching faces as a new mesh instead of highlighting them. | yes | cmd_meshtools.cpp |
| SelMeshFacesByDraftAngle | Extracts (and selects) the matching faces as a new mesh instead of highlighting them. | yes | cmd_meshtools.cpp |
| SelMeshFacesByEdgeLength | Extracts (and selects) the matching faces as a new mesh instead of highlighting them. | yes | cmd_meshtools.cpp |
| SelMeshPart | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelMirroredBlocks | Block instances are not tracked as mirrored yet; selects instances tagged Mirrored. | yes | cmd_select2.cpp |
| SelNakedMeshEdgePt | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelName | Selects objects sharing the first selected object's name. | yes | cmd_select.cpp |
| SelNonManifold | Selects whole meshes that have non-manifold edges. | yes | cmd_select2.cpp |
| SelParents | Selects the other members of the selected objects' groups. | yes | cmd_select2.cpp |
| SelPrev | (no note) | yes | cmd_select.cpp |
| SelSelfIntersectingCrv | Tests a polyline sampling of each curve. | yes | cmd_select2.cpp |
| SelSmall | Threshold is one grid unit. | yes | cmd_select.cpp |
| SelSubDEdges | Selects the SubDs; edges cannot be selected as sub-objects. | yes | cmd_subd.cpp |
| SelU | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelUV | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SelV | Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now. | yes | cmd_select2.cpp |
| SendBackward | Draw order is stored in user text. | yes | cmd_state.cpp |
| SendToBack | Draw order is stored in user text. | yes | cmd_state.cpp |
| SetMeshSurfaceParameters | Uses the global display tolerance. | yes | cmd_render.cpp |
| SetObjectDisplayMode | Per-viewport modes; per-object modes are planned. | yes | cmd_misc.cpp |
| SetObjectName | Assigns sequential names; edit them in Properties. | yes | cmd_edit.cpp |
| SetOneDaySunAnimation | (no note) | yes | cmd_viewtools.cpp |
| SetPerFaceColorByFacePack | (no note) | yes | cmd_render.cpp |
| SetPlanar | (no note) | yes | cmd_srfedit.cpp |
| SetPt | Sets Z of the selection's base to 0; X/Y/Z choice dialog is planned. | yes | cmd_transform.cpp |
| SetSeasonalSunAnimation | (no note) | yes | cmd_viewtools.cpp |
| SetSurfaceTangent | (no note) | yes | cmd_srfedit.cpp |
| SetUserText | (no note) | yes | cmd_edit.cpp |
| SetView | Use Top/Front/Right/Perspective or the viewport title menu. | yes | cmd_view.cpp |
| ShadeSelected | Shades the whole viewport. | yes | cmd_render.cpp |
| Shell | Hollows a closed solid as a mesh (outer minus inward vertex-normal offset); face removal to open the shell is planned. | yes | cmd_surface.cpp |
| ShowDir | (no note) | yes | cmd_curves2.cpp |
| ShowEnds | Highlights ends as preview points. | yes | cmd_curves2.cpp |
| ShowPt | Shows every control point of the selected objects. | yes | cmd_select2.cpp |
| ShowRenderMesh | (no note) | yes | cmd_render.cpp |
| ShowSelected | Shows and selects all hidden objects. | yes | cmd_edit.cpp |
| ShowZBuffer | Toggles the flag only. | yes | cmd_viewtools.cpp |
| ShrinkTrimmedSrfToEdge | Shrinks to the trim bounding box like ShrinkTrimmedSrf. | yes | cmd_srfedit.cpp |
| ShrinkWrap | Builds the 3D convex hull of the selection (concavities are not followed). | yes | cmd_subd.cpp |
| Sketch | Click points; freehand drag sketching is planned. | yes | cmd_create.cpp |
| Slide | Slides one control vertex along its best-aligned edge. | yes | cmd_subd.cpp |
| Smash | Edge-length preserving flattening (reports the area change). | yes | cmd_srfedit.cpp |
| Snapshots | (no note) | yes | cmd_state.cpp |
| SnapToLocked | Stored flag; snaps consider every visible object. | yes | cmd_state.cpp |
| SnapToMeshes | Stored flag. | yes | cmd_state.cpp |
| SnapToMeshObject | Stored flag. | yes | cmd_state.cpp |
| SnapToOccluded | Stored flag; snaps consider every visible object. | yes | cmd_state.cpp |
| SnapToSubDObject | Stored flag. | yes | cmd_state.cpp |
| SoftEditCrv | (no note) | yes | cmd_curves2.cpp |
| SoftEditSrf | (no note) | yes | cmd_srfedit.cpp |
| SoftTransform | Soft move only; rotation and scaling with falloff are planned. | yes | cmd_meshtools.cpp |
| SolidPtOn | (no note) | yes | cmd_edit.cpp |
| SphereTangentToThreeSurfaces | (no note) | yes | cmd_srfedit.cpp |
| SplitEdge | (no note) | yes | cmd_srfedit.cpp |
| SplitFace | (no note) | yes | cmd_srfedit.cpp |
| SplitMeshEdge | Splits the nearest edge at its midpoint. | yes | cmd_meshtools.cpp |
| SplitRefitSurface | (no note) | yes | cmd_srfedit.cpp |
| Splop | Places oriented copies at the picked surface points; the spherical mapping is planned. | yes | cmd_solidtools.cpp |
| Squish | Edge-length preserving flattening. | yes | cmd_srfedit.cpp |
| SquishBack | (no note) | yes | cmd_srfedit.cpp |
| SquishInfo | (no note) | yes | cmd_srfedit.cpp |
| SrfSeam | (no note) | yes | cmd_srfedit.cpp |
| SubDEllipsoid | Axis ratios 1 : 0.7 : 0.5. | yes | cmd_solids.cpp |
| SubDExpandEdges | Splits the faces next to each picked edge into a strip of the given width (no edge-loop continuation). | yes | cmd_subd.cpp |
| SubDFaceEdgeVertexToggle | Cycles a selection filter flag; sub-objects are picked by point. | yes | cmd_subd.cpp |
| SubDSpinEdge | Spins the edge one corner around the two faces sharing it; no direction option. | yes | cmd_subd.cpp |
| SubDUnfriend | Re-spaces the knots so the object is no longer uniform. | yes | cmd_subd.cpp |
| SwapMeshEdge | Swaps the interior edge nearest the pick. | yes | cmd_meshtools.cpp |
| SwapView | Swaps the active viewport's view with the next one. | yes | cmd_state.cpp |
| Symmetry | Mirrors the object; live symmetry editing is planned. | yes | cmd_curves2.cpp |
| Text | Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object. | yes | cmd_annotate.cpp |
| TextProperties | Options-driven (Text=, Height=); rebuilds the text outlines of the selected annotations. | yes | cmd_annotate2.cpp |
| Textures | Lists material textures; procedural textures are planned. | yes | cmd_render.cpp |
| TiltView | Tilts by 15 degrees per call. | yes | cmd_view.cpp |
| ToggleFloatingViewport | Re-docks the other viewports in the default grid. | yes | cmd_viewtools.cpp |
| ToggleRenderMesh | Toggles the viewport between Wireframe and Shaded. | yes | cmd_render.cpp |
| ToolbarLock | Stored flag; the toolbar is docked. | yes | cmd_state.cpp |
| TriangulateRenderMeshes | Triangulates the selected mesh objects (render meshes are not separate objects here). | yes | cmd_meshtools.cpp |
| Turntable | (no note) | yes | cmd_view.cpp |
| UndoSelected | Undoes the last change to the whole document. | yes | cmd_select2.cpp |
| UndoView | Restores the viewport's standard view. | yes | cmd_view.cpp |
| UnjoinEdge | (no note) | yes | cmd_srfedit.cpp |
| UnpackTextures | (no note) | yes | cmd_render.cpp |
| UnrollSrf | Exact for developable surfaces; doubly-curved surfaces are approximated. | yes | cmd_srfedit.cpp |
| UnrollSrfUV | Same as UnrollSrf. | yes | cmd_srfedit.cpp |
| UntrimBorder | Removes all trims of the face (holes included). | yes | cmd_srfedit.cpp |
| UnweldEdge | Unwelds every edge (each face gets its own vertices); edge picking is planned. | yes | cmd_meshtools.cpp |
| UpdateClippingDrawings | Use ClippingSections + Print. | yes | cmd_viewtools.cpp |
| UseExtrusions | (no note) | yes | cmd_srfedit.cpp |
| VariableBlendSrf | (no note) | yes | cmd_srfedit.cpp |
| VariableChamferSrf | (no note) | yes | cmd_srfedit.cpp |
| VariableFilletSrf | (no note) | yes | cmd_srfedit.cpp |
| VariableOffsetSrf | (no note) | yes | cmd_srfedit.cpp |
| ViewCaptureToClipboard | Writes the capture to a file next to the settings. | yes | cmd_state.cpp |
| VolumeMoments | Volume and centroid; inertia moments are planned. | yes | cmd_srfedit.cpp |
| WalkAbout | Keyboard stepping by command (WalkAbout Forward 5); continuous walk mode is planned. | yes |  |
| Weight | Makes objects rational; per-point weight editing is planned. | yes | cmd_edit.cpp |
| WeldVertices | Welds every coincident vertex of the selected meshes; vertex picking is planned. | yes | cmd_meshtools.cpp |
| WireCut | Plane cut through two points. | yes | cmd_boolean.cpp |
| Worksession | (no note) | yes | cmd_state.cpp |
| Zoom1To1Calibrate | Takes the value from the command line (pixels per mm or dpi); the on-screen ruler is planned. | yes | cmd_viewtools.cpp |
| ZoomEnds | Zooms to the selected curves. | yes | cmd_state.cpp |
| ZoomNaked | Zooms to the selection; see ShowEdges for naked edges. | yes | cmd_state.cpp |
| ZoomNonManifold | Zooms to the selection; SelNonManifold finds the meshes. | yes | cmd_state.cpp |

## Appendix B — All 86 Planned (help-only placeholder) commands, alphabetical

AcadSchemes, AddNextU, AddNextV, AddPrevU, AddPrevV, AlignProfiles, AlignVertices, ApplyOcsMapping, AttachGHSData, BlendSrf, ChangeSpace, CheckNewObjects, ClearAllObjectDisplayModes, ClearAnalysisMeshes, CollapseMeshFacesByArea, CollapseMeshFacesByAspectRatio, CollapseMeshFacesByEdgeLength, Convert, CopyToLayer, CreaseSplitting, CullControlPolygon, CurvatureGraphOff, DecimalPoint, DevLoft, DisplayProperties, DraftAnglePoint, DrapePt, DupLayer, EdgeContinuity, EditPythonScript, EditScript, EvaluateUVPt, ExportBitmaps, ExportRuiFile, ExtractCurvatureGraph, ExtractCustomMappingObject, ExtractDuplicateMeshFaces, ExtractUVMesh, ExtrudeSrfAlongCrv, ExtrudeSrfTapered, ExtrudeSrfToPoint, GCon, HBar, HighlightObjectLayers, IgesImportOptions, IGESStudy, IsolateLock, JoinCopy, LayerBook, LoadScript, MatchCrvDir, MatchProperties, MeshPolyline, MoveTargetToObjects, MultiPipe, NextU, NextV, OffsetNormal, PointCloudContour, PointCloudSection, PointsFromUV, PopupToolbar, PrevU, PrevV, PtOffSelected, ReadEveryIGESEntity, RebuildCrvNonUniform, ReducePointCloud, Rescue3dmFile, RibbonOffset, SaveWindowLayout, SelConnectedMeshFaces, SetIgesLayerLevelMap, ShortPath, SplitMeshWithCurve, STEPTree, StepUnitsAndTolerance, Synchronize Views, ThicknessAnalysis, ThicknessAnalysisOff, TweenSurfaces, UnisolateLock, Unwrap, UVEditor, WeldEdge, WindowLayout

## Appendix C — Registered names outside the catalogue (aliases/extras, 89)

Alias, AllLayersOn, AnnotationStyles, Arc3Pt, ArcticViewport, ArtisticViewport, Audit3dmFile, Back, Bottom, CheckForUpdates, CheckMesh, Circle3Pt, CircleD, CommandPaste, ConvexHull, CPlaneNext, CPlanePrevious, CPlaneThroughPoint, CPlaneToObject, CPlaneToView, CPlaneToWorld, DimLinear, DocumentUserText, ExportSelected, Extrude, Front, GhostedViewport, Grasshopper, GridOptions, IncrementalSave, LayerLock, LayerOff, LayerOn, Layers, LayerUnlock, LearnRhino, Left, Licenses, Linetype, Macro, MaterialEditor, Merge, MeshSmooth, MeshToSubD, MonochromeViewport, NewLayer, Nudge, ObjectProperties, Patch, PenViewport, Perspective, Plane3Pt, PolygonStar, RayTracedViewport, RecordHistory, Rectangle3Pt, RenderedViewport, RenderSettings, Right, SelArc, SelCircle, SelHidden, SelLocked, SetActiveRenderer, SetLayer, SetRenderColor, ShadedViewport, Skylight, SmartTrack, Spin, TCone, TechnicalViewport, TechSupport, Toolbar, ToolbarReset, Top, TwoPointPerspective, Units, ViewportProperties, WhatsNew, Wireframe, XRayViewport, Zoom1To1, ZoomExtents, ZoomExtentsAll, ZoomSelected, ZoomSelectedAll, ZoomTarget, ZoomWindow

## Appendix D — Catalogue commands exercised by a test script (249)

Align, ApplyCylindricalMapping, Arc, Area, ArrayCrv, AssignBlankTexture, AutomaticSubDFromMesh, Bend, Block, BooleanDifference, BoundingBox, Box, Bridge, CPlane, Cage, CageEdit, Camera, Cancel, Cap, Catenary, Centermark, Chamfer, ChangeToCurrentLayer, Check, CheckOutLicense, Circle, Clash, ClearClippingSections, ClippingPlane, ClippingSections, CloseCrv, CloseViewport, Conic, Contour, ConvertDots, CopyCPlaneToAll, CopyDetailToViewport, CopyLayout, CopyViewportToDetail, Crease, CreateUniqueBlock, CurveBoolean, Cylinder, DeleteFaces, Detail, DimArea, DimCurveLength, DirectionalLight, DisableClippingPlane, Distance, Distribute, DivideAlongCreases, Dot, Drape, DupBorder, DupEdge, DupFaceBorder, Echo, EnableClippingPlane, Enter, Environments, Exit, Export, ExportClippingSectionInfo, ExtendSrf, ExtractIsocurve, ExtractLineTypeSegments, ExtractMeshPart, ExtractRenderMesh, ExtractSrf, ExtractWireframe, ExtrudeCrv, ExtrudeCrvAlongCrv, ExtrudeCrvTapered, ExtrudeMesh, ExtrudeSubD, Fill, FillMeshHoles, Fillet, FilletCorners, FindText, Flow, GradientView, GroundPlane, Gumball, GumballSettings, Hatch, HatchScale, Heightfield, Hide, HideLayersInDetail, Hyperbola, Hyperlink, Import, Insert, InsertEdge, Inset, Intersect, Join, Layout, LayoutProperties, Layouts, Length, Lights, Line, LineThroughPt, Linetypes, List, Login, Maelstrom, Materials, MeshBox, MeshCylinder, MeshEllipsoid, MeshIntersect, MeshOutline, MeshPatch, MeshPlane, MeshRepair, MeshSphere, MeshTruncatedCone, Move, NamedCPlane, NamedSelections, NetworkSrf, New, OffsetMesh, OffsetSrf, OffsetSubD, Open, Parabola, Paraboloid, PerspectiveAngle, Pipe, PlanarMesh, PlanarSrf, Plane, PlaneThroughPt, PlayAnimation, Point, PointLight, PolygonCount, Polyline, Print, Project, Pull, Radius, RailRevolve, RecordAnimation, Rectangle, Redo, ReducePolyline, Render, RenderArctic, RenderAssignMaterialToObjects, RenderMergeIdenticalMaterials, RenderPreview, RenderReportImageFiles, RenderReportMissingImageFiles, Rendering, RepairSubD, ReplaceBlock, RescueBlockOrphans, RevCloud, Ribbon, RoundHole, Save, SaveClippingSectionCPlanes, SaveClippingSectionViews, SaveRenderWindowAs, Scale, ScaleByPlane, Section, SelAll, SelBlockInstanceNamed, SelCaptives, SelClippingPlane, SelClosedSubD, SelControls, SelCrv, SelDetail, SelDot, SelDupAll, SelHatch, SelID, SelKeyValue, SelLast, SelLight, SelLinetype, SelMesh, SelNone, SelPolysrf, SelPt, SelShortCrv, SelSrf, SelText, SelVolumeSphere, SelectionFilterEdges, SetActiveViewport, SetCurrentRenderPlugIn, SetCustomLinetype, SetDocumentUserText, SetLayerLinetype, SetLinetype, SetSpotlightToView, SetTurntableAnimation, Shear, Shell, Show, ShowLayersInDetail, ShrinkWrap, Silhouette, Slab, Smooth, SoftMove, Sphere, Split, SplitViewportHorizontal, Spotlight, SrfControlPtGrid, Stretch, SubCrv, SubDBox, SubDDisplayToggle, SubDTruncatedCone, Sun, Sweep1, Sweep2, SynchronizeRenderColors, Taper, Text, TextProperties, Trim, TruncatedCone, TruncatedPyramid, TweenCurves, Twist, Undo, UnrollSrf, Untrim, Unweld, ViewFirstFrame, ViewFrameNumber, ViewLastFrame, ViewNextFrame, Volume, Weld, What, ZoomLens
