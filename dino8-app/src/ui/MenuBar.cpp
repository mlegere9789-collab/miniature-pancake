// The main menu. Every item runs the matching command by name, so menus,
// toolbars, and the command line are always the same feature set.
#include "app/Application.h"
#include "i18n/I18n.h"
#include "imgui.h"
#include "ui/Panels.h"

namespace dino8::app {

using dino8::i18n::Tr;

namespace {

void Item(Application& app, const char* label, const char* command, const char* shortcut = nullptr) {
  bool enabled = true;
  if (const RegisteredCommand* rc = app.Engine().Find(command)) {
    enabled = rc->status != CommandStatus::Planned;
  }
  if (ImGui::MenuItem(label, shortcut, false, true)) app.Engine().Execute(command);
  if (!enabled && ImGui::IsItemHovered()) ImGui::SetTooltip(Tr("tooltip.planned_command").c_str(), command);
}

void Items(Application& app, std::initializer_list<const char*> commands) {
  for (const char* c : commands) Item(app, c, c);
}

}  // namespace

void DrawMenuBar(Application& app) {
  if (!ImGui::BeginMainMenuBar()) return;
  Document& doc = app.Doc();
  PanelState& p = app.Panels();

  if (ImGui::BeginMenu(Tr("menu.file").c_str())) {
    Item(app, Tr("file.new").c_str(), "New", "Ctrl+N");
    Item(app, Tr("file.open").c_str(), "Open", "Ctrl+O");
    if (ImGui::BeginMenu(Tr("submenu.open_recent").c_str())) {
      if (app.RecentFiles().empty()) ImGui::TextDisabled("%s", Tr("submenu.none").c_str());
      for (const std::string& f : app.RecentFiles()) {
        if (ImGui::MenuItem(f.c_str())) {
          std::string e;
          if (!app.OpenDocument(f, e)) app.Notify(e);
        }
      }
      ImGui::EndMenu();
    }
    Item(app, Tr("file.revert").c_str(), "Revert");
    ImGui::Separator();
    Item(app, Tr("file.save").c_str(), "Save", "Ctrl+S");
    Item(app, Tr("file.save_as").c_str(), "SaveAs", "Ctrl+Shift+S");
    Item(app, Tr("file.save_small").c_str(), "SaveSmall");
    Item(app, Tr("file.incremental_save").c_str(), "IncrementalSave");
    Item(app, Tr("file.save_as_template").c_str(), "SaveAsTemplate");
    ImGui::Separator();
    Item(app, Tr("file.insert").c_str(), "Insert");
    Item(app, Tr("file.import").c_str(), "Import");
    Item(app, Tr("file.export_selected").c_str(), "Export");
    Item(app, Tr("file.export_with_origin").c_str(), "ExportWithOrigin");
    Item(app, Tr("file.worksession").c_str(), "Worksession");
    ImGui::Separator();
    Item(app, Tr("file.notes").c_str(), "Notes");
    Item(app, Tr("file.properties").c_str(), "DocumentProperties");
    Item(app, Tr("file.print").c_str(), "Print");
    ImGui::Separator();
    Item(app, Tr("file.exit").c_str(), "Exit", "Alt+F4");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.edit").c_str())) {
    Item(app, Tr("edit.undo").c_str(), "Undo", "Ctrl+Z");
    Item(app, Tr("edit.redo").c_str(), "Redo", "Ctrl+Y");
    Item(app, Tr("edit.undo_multiple").c_str(), "UndoMultiple");
    Item(app, Tr("edit.redo_multiple").c_str(), "RedoMultiple");
    ImGui::Separator();
    Item(app, Tr("edit.cut").c_str(), "Cut", "Ctrl+X");
    Item(app, Tr("edit.copy").c_str(), "CopyToClipboard", "Ctrl+C");
    Item(app, Tr("edit.paste").c_str(), "Paste", "Ctrl+V");
    Item(app, Tr("edit.delete").c_str(), "Delete", "Del");
    ImGui::Separator();
    if (ImGui::BeginMenu(Tr("submenu.select_objects").c_str())) {
      Items(app, {"SelAll", "SelNone", "Invert", "SelPrev", "SelLast", "SelDup", "SelBoundary", "SelChain",
                  "SelColor", "SelLayer", "SelName", "SelGroup", "SelCrv", "SelSrf", "SelPolysrf", "SelMesh",
                  "SelSubD", "SelPt", "SelClosedCrv", "SelOpenCrv", "SelClosedSrf", "SelOpenSrf", "SelClosedMesh",
                  "SelOpenMesh", "SelLight", "SelDim", "SelBlockInstance", "SelHatch", "SelText", "SelSmall",
                  "SelBadObjects", "SelVisible", "SelWindow", "SelCrossing", "SelBrush", "SelLasso", "SelCircular",
                  "SelBox", "SelVolumeObject", "SelectionFilter"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.visibility").c_str())) {
      Items(app, {"Hide", "Show", "ShowSelected", "Isolate", "Unisolate", "HideSwap", "Lock", "Unlock", "UnlockSelected", "LockSwap"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.groups").c_str())) {
      Items(app, {"Group", "Ungroup", "AddToGroup", "RemoveFromGroup", "SetGroupName"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.blocks").c_str())) {
      Items(app, {"Block", "Insert", "BlockEdit", "BlockManager", "ExplodeBlock", "ReplaceBlock"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.control_points").c_str())) {
      Items(app, {"PointsOn", "PointsOff", "SolidPtOn", "InsertKnot", "RemoveKnot", "InsertControlPoint", "RemoveControlPoint",
                  "InsertKink", "InsertEditPoint", "EditPtOn", "Weight", "MoveUVN", "HBar", "SetPt"});
      ImGui::EndMenu();
    }
    ImGui::Separator();
    Items(app, {"Join", "Explode", "Trim", "Split", "Untrim", "Extend", "Fillet", "Chamfer", "Rebuild", "ChangeDegree",
                "Smooth", "Fair", "Match", "MergeAllEdges", "Offset", "Layer", "Properties", "ObjectProperties"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.view").c_str())) {
    Item(app, Tr("view.undo_view").c_str(), "UndoView", "Home");
    Item(app, Tr("view.redo_view").c_str(), "RedoView", "End");
    ImGui::Separator();
    Items(app, {"Pan", "RotateView", "RotateCamera", "Zoom", "ZoomExtents", "ZoomExtentsAll", "ZoomSelected", "ZoomTarget", "ZoomWindow", "Zoom1To1", "ZoomLens", "TiltView", "Walkabout", "Spin", "Turntable"});
    ImGui::Separator();
    if (ImGui::BeginMenu(Tr("submenu.set_view").c_str())) {
      Items(app, {"Top", "Bottom", "Front", "Back", "Right", "Left", "Perspective", "TwoPointPerspective", "Isometric", "Plan", "NamedView", "ViewCaptureToFile", "ViewCaptureToClipboard"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.set_cplane").c_str())) {
      Items(app, {"CPlane", "NamedCPlane", "CPlaneToObject", "CPlaneToView", "CPlaneThroughPoint", "CPlaneToWorld", "CPlaneNext", "CPlanePrevious", "OrientCPlaneToSrf", "UniversalCPlane"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.viewport_layout").c_str())) {
      if (ImGui::MenuItem(Tr("viewport_layout.single").c_str())) app.SetViewportLayout(1);
      if (ImGui::MenuItem(Tr("viewport_layout.three").c_str())) app.SetViewportLayout(3);
      if (ImGui::MenuItem(Tr("viewport_layout.four").c_str())) app.SetViewportLayout(4);
      ImGui::Separator();
      Items(app, {"4View", "3View", "MaxViewport", "NewViewport", "NewFloatingViewport", "CloseViewport", "ViewportProperties", "SplitViewportHorizontal", "SplitViewportVertical", "NextViewport", "PrevViewport"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.display_mode").c_str())) {
      Items(app, {"SetDisplayMode", "Wireframe", "Shade", "ShadedViewport", "RenderedViewport", "GhostedViewport", "XRayViewport", "TechnicalViewport", "ArtisticViewport", "PenViewport", "ArcticViewport", "MonochromeViewport", "RayTracedViewport", "SetObjectDisplayMode"});
      ImGui::EndMenu();
    }
    ImGui::Separator();
    Item(app, Tr("view.grid").c_str(), "Grid", "F7");
    Item(app, Tr("view.grid_options").c_str(), "GridOptions");
    Item(app, Tr("view.background_bitmap").c_str(), "BackgroundBitmap");
    Item(app, Tr("view.clipping_plane").c_str(), "ClippingPlane");
    Item(app, Tr("view.named_views").c_str(), "NamedView");
    Item(app, Tr("view.refresh_shade").c_str(), "RefreshShade");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.curve").c_str())) {
    if (ImGui::BeginMenu(Tr("submenu.point_object").c_str())) {
      Items(app, {"Point", "Points", "PointGrid", "Divide", "MarkFoci", "ClosestPt", "PointCloud", "ExtractPt", "PointDeviation", "PointsFromUV"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.line").c_str())) {
      Items(app, {"Line", "Lines", "Polyline", "PolylineOnMesh", "LineThroughPt", "ExtendCrvOnSrf", "PolygonMesh"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.circle").c_str())) {
      Items(app, {"Circle", "Circle3Pt", "CircleTTT", "CircleTTR", "CircleD", "CircleFitPoints"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.arc").c_str())) {
      Items(app, {"Arc", "Arc3Pt", "ArcSED", "ArcTTR", "ArcDir", "ArcBlend"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(Tr("submenu.ellipse").c_str())) {
      Items(app, {"Ellipse", "Ellipse3Pt", "EllipseD", "EllipseFitPoints"});
      ImGui::EndMenu();
    }
    Items(app, {"Rectangle", "Rectangle3Pt", "RectangleRounded", "Polygon", "PolygonStar", "Parabola", "Hyperbola", "Conic", "Spiral", "Helix", "InterpCrv", "Curve", "Sketch", "CurveThroughPt", "CurveThroughPolyline", "Handlebar", "Blend", "BlendCrv", "CurveBoolean", "Offset", "OffsetCrvOnSrf", "FilletCorners", "Fillet", "Chamfer", "Extend", "ExtendByArc", "ExtendByLine", "ExtendOnSrf", "ConnectCrv", "Project", "Pull", "Isocurve", "Section", "Contour", "Silhouette", "DupBorder", "DupEdge", "DupFaceBorder", "DupMeshEdge", "ExtractIsocurve", "ExtractWireframe", "Intersect", "CurveFromUV", "Tween", "Fit", "Convert", "SimplifyCrv", "MakePeriodic", "Rebuild", "Fair", "Smooth", "RemoveMultiKnot", "MakeUniform", "Symmetry", "CrvSeam", "CrvStart", "CrvEnd", "Dir", "Flip"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.surface").c_str())) {
    Items(app, {"Plane", "Plane3Pt", "PlaneV", "PlaneThroughPt", "CutPlane", "PictureFrame", "SrfPt", "EdgeSrf", "PlanarSrf", "ExtrudeCrv", "ExtrudeCrvAlongCrv", "ExtrudeCrvTapered", "ExtrudeCrvToPoint", "ExtrudeSrf", "Loft", "Revolve", "RailRevolve", "Sweep1", "Sweep2", "NetworkSrf", "Patch", "Drape", "Heightfield", "FilletSrf", "ChamferSrf", "BlendSrf", "VariableFilletSrf", "OffsetSrf", "MatchSrf", "MergeSrf", "ExtendSrf", "ShrinkTrimmedSrf", "Untrim", "SplitEdge", "MergeEdge", "JoinEdge", "ShowEdges", "SrfSeam", "SetSurfaceTangent", "Rebuild", "RebuildUV", "ChangeDegree", "Smooth", "MakePeriodic", "SrfControlPtGrid", "UnrollSrf", "Squish", "Smash", "TweenSurfaces", "FitSrf", "ConvertToBeziers", "MakeUniformUV", "RemoveMultiKnot", "InsertKnot", "Dir", "SolidPtOn", "DivideAlongCreases"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.subd").c_str())) {
    Items(app, {"SubDBox", "SubDSphere", "SubDCylinder", "SubDCone", "SubDTruncatedCone", "SubDEllipsoid", "SubDTorus", "SubDPlane", "SubDLoft", "SubDSweep1", "SubDSweep2", "SubDRevolve", "SubDMultiPipe", "SubDThicken", "ToSubD", "ToNURBS", "SubDDisplayToggle", "Bridge", "Bevel", "Crease", "RemoveCrease", "Fill", "InsertEdge", "InsertPoint", "SubDExpandEdges", "MergeFaces", "OffsetSubD", "Reflect", "Slide", "Stitch", "SubDivide", "Unweld", "QuadRemesh", "AddCorner", "RemoveCorner", "Append", "ExtrudeSubD", "MoveSubDVertex", "SmoothSubD", "SubDSymmetryToggle", "Symmetry", "SubDWireframe"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.solid").c_str())) {
    Items(app, {"Box", "Sphere", "Cylinder", "Cone", "TCone", "Torus", "Tube", "Pipe", "Ellipsoid", "Paraboloid", "Pyramid", "Slab", "ExtrudeCrv", "ExtrudeSrf", "Cap", "BooleanUnion", "BooleanDifference", "BooleanIntersection", "Boolean2Objects", "BooleanSplit", "Shell", "OffsetSrf", "FilletEdge", "ChamferEdge", "BlendEdge", "MergeAllCoplanarFaces", "MergeAllEdges", "Untrim", "SolidPtOn", "MoveFace", "MoveEdge", "ExtractSrf", "ExtrudeFace", "WireCut", "Text", "TextObject", "AutoCPlane", "DeleteHole", "MoveHole", "ArrayHole", "CopyHole", "RotateHole", "RoundHole", "PlaceHole", "MakeHole", "ShrinkTrimmedSrfToEdge", "CreateSolid", "SplitFace", "ClosePolysrf", "Cap"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.mesh").c_str())) {
    Items(app, {"Mesh", "MeshBox", "MeshSphere", "MeshCylinder", "MeshCone", "MeshTCone", "MeshTorus", "MeshPlane", "MeshEllipsoid", "MeshPolyline", "MeshPatch", "MeshFromPoints", "MeshFromLines", "3DFace", "MeshBooleanUnion", "MeshBooleanDifference", "MeshBooleanIntersection", "MeshBooleanSplit", "MeshSplit", "MeshTrim", "MeshIntersect", "Weld", "Unweld", "WeldVertices", "WeldEdge", "UnifyMeshNormals", "RebuildMesh", "RebuildMeshNormals", "ReduceMesh", "QuadRemesh", "TriangulateMesh", "QuadrangulateMesh", "MeshRepair", "MatchMeshEdge", "FillMeshHole", "FillMeshHoles", "SplitDisjointMesh", "SplitMeshEdge", "SplitMeshWithCurve", "ExtractMeshFaces", "ExtractMeshEdges", "ExtractMeshPart", "ExtractConnectedMeshFaces", "ExtractNonManifoldMeshEdges", "ExtractDuplicateMeshFaces", "DeleteMeshFaces", "ExtrudeMesh", "ExtrudeMeshEdge", "OffsetMesh", "MeshToNURB", "MeshOutline", "MeshWireframe", "ExtractPt", "CullDegenerateMeshFaces", "CheckMesh", "SwapMeshEdge", "CollapseMeshEdge", "CollapseMeshFace", "CollapseMeshVertex", "CollapseMeshFacesByArea", "CollapseMeshFacesByAspectRatio", "CollapseMeshFacesByEdgeLength", "MeshSmooth", "ShrinkWrap", "ApplyMesh", "ApplyCurvePiping", "ApplyDisplacement", "ApplyEdgeSoftening", "ApplyShutLining", "AlignMeshVertices", "PolygonMesh", "MeshSelfIntersect", "MeshToSubD", "MeshFromSubD", "ExportSelected"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.dimension").c_str())) {
    Items(app, {"Dim", "DimAligned", "DimRotated", "DimAngle", "DimRadius", "DimDiameter", "DimOrdinate", "DimArea", "DimCurveLength", "DimCreaseAngle", "DimRecenterText", "Leader", "Text", "TextObject", "Hatch", "HatchBase", "HatchScale", "Annotate", "AnnotationHistory", "DimStyles", "ConvertDots", "DotFormat", "MatchDimStyle", "MakeCurve", "Make2D", "Section", "SectionTools", "Length", "Area", "AreaCentroid", "AreaMoments", "Volume", "VolumeCentroid", "VolumeMoments", "Distance", "Angle", "Radius", "Diameter", "EvaluatePt", "EvaluateUVPt", "ExtractPt", "Analyze"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.transform").c_str())) {
    Items(app, {"Move", "Copy", "Rotate", "Rotate3D", "Scale", "Scale1D", "Scale2D", "ScaleNU", "ScaleByPlane", "Mirror", "Orient", "Orient3Pt", "OrientOnCrv", "OrientOnSrf", "OrientCameraToSrf", "Array", "ArrayPolar", "ArrayCrv", "ArrayCrvOnSrf", "ArraySrf", "ArrayLinear", "ArrayHole", "Gumball", "BoxEdit", "SetPt", "Shear", "Twist", "Bend", "Taper", "Flow", "FlowAlongSrf", "Maelstrom", "Splop", "Stretch", "Smooth", "Fair", "CageEdit", "Cage", "ReleaseFromCage", "SoftMove", "SoftEditCrv", "SoftEditSrf", "MoveUVN", "Project", "ProjectToCPlane", "RemapCPlane", "Align", "Distribute", "Group", "Ungroup", "Explode", "History", "RecordHistory", "HistoryPurge", "Dragmode", "Nudge", "Symmetry", "Reflect"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.tools").c_str())) {
    Item(app, Tr("tools.script_editor").c_str(), "ScriptEditor");
    Item(app, Tr("tools.run_script").c_str(), "RunScript");
    Item(app, Tr("tools.load_script").c_str(), "LoadScript");
    if (ImGui::MenuItem(Tr("tools.scripting_reference").c_str())) p.scripting_reference = true;
    ImGui::Separator();
    Items(app, {"Options", "DocumentProperties", "Toolbar", "ToolbarReset", "Alias", "Macro", "MacroEditor", "ReadCommandFile", "CommandHistory", "CommandList", "CommandPaste", "Calc", "CalcRPN", "Units", "Snap", "Osnap", "Ortho", "Planar", "SmartTrack", "ProjectOsnap", "PersistentOnCrv", "PersistentOnSrf", "PersistentOnMesh", "PersistentOnPolysrf", "DisableOsnap", "Gumball", "PointsOn", "PointsOff", "Layer", "LayerStateManager", "Repeat", "ScriptEditor", "RunScript", "RunPythonScript", "EditPythonScript", "Grasshopper", "GrasshopperPlayer", "PackageManager", "PluginManager", "Audit", "Audit3dmFile", "SystemInfo", "Notes", "Check", "SelBadObjects", "Purge", "ClearUndo", "ClearAllMeshes", "Lock", "Unlock", "Hide", "Show", "Isolate", "Unisolate", "Zoo", "Zoom"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.analyze").c_str())) {
    Items(app, {"Distance", "Length", "Angle", "Radius", "Diameter", "Area", "AreaCentroid", "AreaMoments", "Volume", "VolumeCentroid", "VolumeMoments", "BoundingBox", "Dir", "What", "List", "Check", "CheckMesh", "SelBadObjects", "EvaluatePt", "EvaluateUVPt", "Curvature", "CurvatureGraph", "CurvatureGraphOff", "CurvatureAnalysis", "CurvatureAnalysisOff", "Zebra", "ZebraOff", "EMap", "EMapOff", "DraftAngleAnalysis", "DraftAngleAnalysisOff", "ThicknessAnalysis", "ThicknessAnalysisOff", "EdgeContinuity", "ShowEdges", "ShowEdgesOff", "SelDup", "SelSmall", "CrvDeviation", "PointDeviation", "GetDocumentUserText", "GetUserText", "SetObjectName", "SetUserText", "DocumentUserText", "Report", "Intersect", "GeometryTree", "Hydrostatics", "Moments"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.render").c_str())) {
    Items(app, {"Render", "RenderPreview", "RenderWindow", "RenderOpenLastRendering", "RenderPreviewInWindow", "RenderPreviewSelected", "RenderSettings", "RenderPresets", "RenderReportBug", "Environment", "Environments", "EnvironmentEditor", "Materials", "MaterialEditor", "Textures", "TextureMapping", "Lights", "LightManager", "Spotlight", "PointLight", "DirectionalLight", "RectangularLight", "LinearLight", "Skylight", "Sun", "GroundPlane", "RenderMesh", "RenderMeshSettings", "SetRenderColor", "SetObjectDisplayMode", "SetDisplayMode", "TurnTable", "ViewCaptureToFile", "ViewCaptureToClipboard", "ScreenCaptureToFile", "ScreenCaptureToClipboard", "Animate", "SetActiveRenderer", "Snapshots", "CycleShadows", "Denoise"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.panels").c_str())) {
    ImGui::MenuItem(Tr("panel.layers").c_str(), "F4", &p.layers);
    ImGui::MenuItem(Tr("panel.properties").c_str(), "F3", &p.properties);
    ImGui::MenuItem(Tr("panel.command_history").c_str(), "F2", &p.command_history);
    ImGui::MenuItem(Tr("panel.command_list_menu").c_str(), "Ctrl+F1", &p.command_list);
    ImGui::MenuItem(Tr("panel.help").c_str(), "", &p.help);
    ImGui::MenuItem(Tr("panel.notifications").c_str(), "", &p.notifications);
    ImGui::MenuItem(Tr("panel.named_views").c_str(), "", &p.named_views);
    ImGui::MenuItem(Tr("panel.notes").c_str(), "", &p.notes);
    ImGui::MenuItem(Tr("panel.document_user_text").c_str(), "", &p.document_user_text);
    ImGui::MenuItem(Tr("panel.linetypes").c_str(), "", &p.linetypes);
    ImGui::MenuItem(Tr("panel.materials").c_str(), "", &p.materials);
    ImGui::MenuItem(Tr("panel.display").c_str(), "", &p.display);
    ImGui::MenuItem(Tr("panel.object_snaps").c_str(), "", &p.object_snaps);
    ImGui::MenuItem(Tr("panel.toolbars").c_str(), "", &p.toolbars);
    ImGui::MenuItem(Tr("panel.calculator").c_str(), "", &p.calculator);
    ImGui::MenuItem(Tr("panel.box_edit").c_str(), "", &p.box_edit);
    ImGui::MenuItem(Tr("panel.layer_state_manager").c_str(), "", &p.layer_state_manager);
    ImGui::MenuItem(Tr("panel.selection_filter").c_str(), "", &p.selection_filter);
    ImGui::MenuItem(Tr("panel.macro_editor").c_str(), "", &p.macro_editor);
    ImGui::MenuItem(Tr("panel.script_editor").c_str(), "", &p.script_editor);
    ImGui::MenuItem(Tr("panel.scripting_reference").c_str(), "", &p.scripting_reference);
    ImGui::MenuItem(Tr("panel.clipping_planes").c_str(), "", &p.clipping_planes);
    ImGui::MenuItem(Tr("panel.layouts").c_str(), "", &p.layouts);
    ImGui::MenuItem(Tr("panel.named_cplanes").c_str(), "", &p.named_cplanes);
    ImGui::MenuItem(Tr("panel.dino_flow").c_str(), "", &p.dino_flow);
    ImGui::MenuItem(Tr("panel.plugin_manager").c_str(), "", &p.plugin_manager);
    ImGui::MenuItem(Tr("panel.package_manager").c_str(), "", &p.package_manager);
    ImGui::MenuItem(Tr("panel.hatch_patterns").c_str(), "", &p.hatch_patterns);
    ImGui::MenuItem(Tr("panel.table_editor").c_str(), "", &p.table_editor);
    ImGui::MenuItem(Tr("panel.viewport_tabs").c_str(), "", &app.show_viewport_tabs);
    ImGui::Separator();
    if (ImGui::MenuItem(Tr("panel.reset_layout").c_str())) app.SetViewportLayout(4);
    ImGui::MenuItem(Tr("panel.imgui_demo").c_str(), "", &p.imgui_demo);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(Tr("menu.help").c_str())) {
    Item(app, Tr("help.command_help").c_str(), "Help", "F1");
    if (ImGui::MenuItem(Tr("help.command_list_menu").c_str(), "Ctrl+F1")) p.command_list = true;
    if (ImGui::MenuItem(Tr("tools.scripting_reference").c_str())) p.scripting_reference = true;
    Items(app, {"CommandHelp", "LearnRhino", "Tutorials", "WhatsNew", "CheckForUpdates", "SystemInfo", "TechSupport", "Licenses"});
    ImGui::Separator();
    if (ImGui::MenuItem(Tr("help.about").c_str())) p.about = true;
    ImGui::EndMenu();
  }

  // Right-aligned document status.
  const std::string title = (doc.Path().empty() ? Tr("status.untitled") : doc.Path()) + (doc.Modified() ? " *" : "");
  const float w = ImGui::CalcTextSize(title.c_str()).x + 20.0f;
  ImGui::SameLine(ImGui::GetWindowWidth() - w);
  ImGui::TextDisabled("%s", title.c_str());
  ImGui::EndMainMenuBar();
}

}  // namespace dino8::app
