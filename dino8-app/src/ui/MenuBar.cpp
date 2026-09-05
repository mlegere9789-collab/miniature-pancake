// The main menu. Every item runs the matching command by name, so menus,
// toolbars, and the command line are always the same feature set.
#include "app/Application.h"
#include "imgui.h"
#include "ui/Panels.h"

namespace dino8::app {

namespace {

void Item(Application& app, const char* label, const char* command, const char* shortcut = nullptr) {
  bool enabled = true;
  if (const RegisteredCommand* rc = app.Engine().Find(command)) {
    enabled = rc->status != CommandStatus::Planned;
  }
  if (ImGui::MenuItem(label, shortcut, false, true)) app.Engine().Execute(command);
  if (!enabled && ImGui::IsItemHovered()) ImGui::SetTooltip("Planned: opens the reference help for %s", command);
}

void Items(Application& app, std::initializer_list<const char*> commands) {
  for (const char* c : commands) Item(app, c, c);
}

}  // namespace

void DrawMenuBar(Application& app) {
  if (!ImGui::BeginMainMenuBar()) return;
  Document& doc = app.Doc();
  PanelState& p = app.Panels();

  if (ImGui::BeginMenu("File")) {
    Item(app, "New", "New", "Ctrl+N");
    Item(app, "Open...", "Open", "Ctrl+O");
    if (ImGui::BeginMenu("Open Recent")) {
      if (app.RecentFiles().empty()) ImGui::TextDisabled("(none)");
      for (const std::string& f : app.RecentFiles()) {
        if (ImGui::MenuItem(f.c_str())) {
          std::string e;
          if (!app.OpenDocument(f, e)) app.Notify(e);
        }
      }
      ImGui::EndMenu();
    }
    Item(app, "Revert", "Revert");
    ImGui::Separator();
    Item(app, "Save", "Save", "Ctrl+S");
    Item(app, "Save As...", "SaveAs", "Ctrl+Shift+S");
    Item(app, "Save Small", "SaveSmall");
    Item(app, "Incremental Save", "IncrementalSave");
    Item(app, "Save As Template", "SaveAsTemplate");
    ImGui::Separator();
    Item(app, "Insert...", "Insert");
    Item(app, "Import...", "Import");
    Item(app, "Export Selected...", "Export");
    Item(app, "Export With Origin", "ExportWithOrigin");
    Item(app, "Worksession", "Worksession");
    ImGui::Separator();
    Item(app, "Notes", "Notes");
    Item(app, "Properties...", "DocumentProperties");
    Item(app, "Print...", "Print");
    ImGui::Separator();
    Item(app, "Exit", "Exit", "Alt+F4");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    Item(app, "Undo", "Undo", "Ctrl+Z");
    Item(app, "Redo", "Redo", "Ctrl+Y");
    Item(app, "Undo Multiple", "UndoMultiple");
    Item(app, "Redo Multiple", "RedoMultiple");
    ImGui::Separator();
    Item(app, "Cut", "Cut", "Ctrl+X");
    Item(app, "Copy", "CopyToClipboard", "Ctrl+C");
    Item(app, "Paste", "Paste", "Ctrl+V");
    Item(app, "Delete", "Delete", "Del");
    ImGui::Separator();
    if (ImGui::BeginMenu("Select Objects")) {
      Items(app, {"SelAll", "SelNone", "Invert", "SelPrev", "SelLast", "SelDup", "SelBoundary", "SelChain",
                  "SelColor", "SelLayer", "SelName", "SelGroup", "SelCrv", "SelSrf", "SelPolysrf", "SelMesh",
                  "SelSubD", "SelPt", "SelClosedCrv", "SelOpenCrv", "SelClosedSrf", "SelOpenSrf", "SelClosedMesh",
                  "SelOpenMesh", "SelLight", "SelDim", "SelBlockInstance", "SelHatch", "SelText", "SelSmall",
                  "SelBadObjects", "SelVisible", "SelWindow", "SelCrossing", "SelBrush", "SelLasso", "SelCircular",
                  "SelBox", "SelVolumeObject", "SelectionFilter"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Visibility")) {
      Items(app, {"Hide", "Show", "ShowSelected", "Isolate", "Unisolate", "HideSwap", "Lock", "Unlock", "UnlockSelected", "LockSwap"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Groups")) {
      Items(app, {"Group", "Ungroup", "AddToGroup", "RemoveFromGroup", "SetGroupName"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Blocks")) {
      Items(app, {"Block", "Insert", "BlockEdit", "BlockManager", "ExplodeBlock", "ReplaceBlock"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Control Points")) {
      Items(app, {"PointsOn", "PointsOff", "SolidPtOn", "InsertKnot", "RemoveKnot", "InsertControlPoint", "RemoveControlPoint",
                  "InsertKink", "InsertEditPoint", "EditPtOn", "Weight", "MoveUVN", "HBar", "SetPt"});
      ImGui::EndMenu();
    }
    ImGui::Separator();
    Items(app, {"Join", "Explode", "Trim", "Split", "Untrim", "Extend", "Fillet", "Chamfer", "Rebuild", "ChangeDegree",
                "Smooth", "Fair", "Match", "MergeAllEdges", "Offset", "Layer", "Properties", "ObjectProperties"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    Item(app, "Undo View Change", "UndoView", "Home");
    Item(app, "Redo View Change", "RedoView", "End");
    ImGui::Separator();
    Items(app, {"Pan", "RotateView", "RotateCamera", "Zoom", "ZoomExtents", "ZoomExtentsAll", "ZoomSelected", "ZoomTarget", "ZoomWindow", "Zoom1To1", "ZoomLens", "TiltView", "Walkabout", "Spin", "Turntable"});
    ImGui::Separator();
    if (ImGui::BeginMenu("Set View")) {
      Items(app, {"Top", "Bottom", "Front", "Back", "Right", "Left", "Perspective", "TwoPointPerspective", "Isometric", "Plan", "NamedView", "ViewCaptureToFile", "ViewCaptureToClipboard"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Set CPlane")) {
      Items(app, {"CPlane", "NamedCPlane", "CPlaneToObject", "CPlaneToView", "CPlaneThroughPoint", "CPlaneToWorld", "CPlaneNext", "CPlanePrevious", "OrientCPlaneToSrf", "UniversalCPlane"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Viewport Layout")) {
      if (ImGui::MenuItem("Single viewport")) app.SetViewportLayout(1);
      if (ImGui::MenuItem("3 viewports")) app.SetViewportLayout(3);
      if (ImGui::MenuItem("4 viewports")) app.SetViewportLayout(4);
      ImGui::Separator();
      Items(app, {"4View", "3View", "MaxViewport", "NewViewport", "NewFloatingViewport", "CloseViewport", "ViewportProperties", "SplitViewportHorizontal", "SplitViewportVertical", "NextViewport", "PrevViewport"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Display Mode")) {
      Items(app, {"SetDisplayMode", "Wireframe", "Shade", "ShadedViewport", "RenderedViewport", "GhostedViewport", "XRayViewport", "TechnicalViewport", "ArtisticViewport", "PenViewport", "ArcticViewport", "MonochromeViewport", "RayTracedViewport", "SetObjectDisplayMode"});
      ImGui::EndMenu();
    }
    ImGui::Separator();
    Item(app, "Grid", "Grid", "F7");
    Item(app, "Grid Options", "GridOptions");
    Item(app, "Background Bitmap", "BackgroundBitmap");
    Item(app, "Clipping Plane", "ClippingPlane");
    Item(app, "Named Views", "NamedView");
    Item(app, "Refresh Shade", "RefreshShade");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Curve")) {
    if (ImGui::BeginMenu("Point Object")) {
      Items(app, {"Point", "Points", "PointGrid", "Divide", "MarkFoci", "ClosestPt", "PointCloud", "ExtractPt", "PointDeviation", "PointsFromUV"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Line")) {
      Items(app, {"Line", "Lines", "Polyline", "PolylineOnMesh", "LineThroughPt", "ExtendCrvOnSrf", "PolygonMesh"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Circle")) {
      Items(app, {"Circle", "Circle3Pt", "CircleTTT", "CircleTTR", "CircleD", "CircleFitPoints"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Arc")) {
      Items(app, {"Arc", "Arc3Pt", "ArcSED", "ArcTTR", "ArcDir", "ArcBlend"});
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ellipse")) {
      Items(app, {"Ellipse", "Ellipse3Pt", "EllipseD", "EllipseFitPoints"});
      ImGui::EndMenu();
    }
    Items(app, {"Rectangle", "Rectangle3Pt", "RectangleRounded", "Polygon", "PolygonStar", "Parabola", "Hyperbola", "Conic", "Spiral", "Helix", "InterpCrv", "Curve", "Sketch", "CurveThroughPt", "CurveThroughPolyline", "Handlebar", "Blend", "BlendCrv", "CurveBoolean", "Offset", "OffsetCrvOnSrf", "FilletCorners", "Fillet", "Chamfer", "Extend", "ExtendByArc", "ExtendByLine", "ExtendOnSrf", "ConnectCrv", "Project", "Pull", "Isocurve", "Section", "Contour", "Silhouette", "DupBorder", "DupEdge", "DupFaceBorder", "DupMeshEdge", "ExtractIsocurve", "ExtractWireframe", "Intersect", "CurveFromUV", "Tween", "Fit", "Convert", "SimplifyCrv", "MakePeriodic", "Rebuild", "Fair", "Smooth", "RemoveMultiKnot", "MakeUniform", "Symmetry", "CrvSeam", "CrvStart", "CrvEnd", "Dir", "Flip"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Surface")) {
    Items(app, {"Plane", "Plane3Pt", "PlaneV", "PlaneThroughPt", "CutPlane", "PictureFrame", "SrfPt", "EdgeSrf", "PlanarSrf", "ExtrudeCrv", "ExtrudeCrvAlongCrv", "ExtrudeCrvTapered", "ExtrudeCrvToPoint", "ExtrudeSrf", "Loft", "Revolve", "RailRevolve", "Sweep1", "Sweep2", "NetworkSrf", "Patch", "Drape", "Heightfield", "FilletSrf", "ChamferSrf", "BlendSrf", "VariableFilletSrf", "OffsetSrf", "MatchSrf", "MergeSrf", "ExtendSrf", "ShrinkTrimmedSrf", "Untrim", "SplitEdge", "MergeEdge", "JoinEdge", "ShowEdges", "SrfSeam", "SetSurfaceTangent", "Rebuild", "RebuildUV", "ChangeDegree", "Smooth", "MakePeriodic", "SrfControlPtGrid", "UnrollSrf", "Squish", "Smash", "TweenSurfaces", "FitSrf", "ConvertToBeziers", "MakeUniformUV", "RemoveMultiKnot", "InsertKnot", "Dir", "SolidPtOn", "DivideAlongCreases"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("SubD")) {
    Items(app, {"SubDBox", "SubDSphere", "SubDCylinder", "SubDCone", "SubDTruncatedCone", "SubDEllipsoid", "SubDTorus", "SubDPlane", "SubDLoft", "SubDSweep1", "SubDSweep2", "SubDRevolve", "SubDMultiPipe", "SubDThicken", "ToSubD", "ToNURBS", "SubDDisplayToggle", "Bridge", "Bevel", "Crease", "RemoveCrease", "Fill", "InsertEdge", "InsertPoint", "SubDExpandEdges", "MergeFaces", "OffsetSubD", "Reflect", "Slide", "Stitch", "SubDivide", "Unweld", "QuadRemesh", "AddCorner", "RemoveCorner", "Append", "ExtrudeSubD", "MoveSubDVertex", "SmoothSubD", "SubDSymmetryToggle", "Symmetry", "SubDWireframe"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Solid")) {
    Items(app, {"Box", "Sphere", "Cylinder", "Cone", "TCone", "Torus", "Tube", "Pipe", "Ellipsoid", "Paraboloid", "Pyramid", "Slab", "ExtrudeCrv", "ExtrudeSrf", "Cap", "BooleanUnion", "BooleanDifference", "BooleanIntersection", "Boolean2Objects", "BooleanSplit", "Shell", "OffsetSrf", "FilletEdge", "ChamferEdge", "BlendEdge", "MergeAllCoplanarFaces", "MergeAllEdges", "Untrim", "SolidPtOn", "MoveFace", "MoveEdge", "ExtractSrf", "ExtrudeFace", "WireCut", "Text", "TextObject", "AutoCPlane", "DeleteHole", "MoveHole", "ArrayHole", "CopyHole", "RotateHole", "RoundHole", "PlaceHole", "MakeHole", "ShrinkTrimmedSrfToEdge", "CreateSolid", "SplitFace", "ClosePolysrf", "Cap"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Mesh")) {
    Items(app, {"Mesh", "MeshBox", "MeshSphere", "MeshCylinder", "MeshCone", "MeshTCone", "MeshTorus", "MeshPlane", "MeshEllipsoid", "MeshPolyline", "MeshPatch", "MeshFromPoints", "MeshFromLines", "3DFace", "MeshBooleanUnion", "MeshBooleanDifference", "MeshBooleanIntersection", "MeshBooleanSplit", "MeshSplit", "MeshTrim", "MeshIntersect", "Weld", "Unweld", "WeldVertices", "WeldEdge", "UnifyMeshNormals", "RebuildMesh", "RebuildMeshNormals", "ReduceMesh", "QuadRemesh", "TriangulateMesh", "QuadrangulateMesh", "MeshRepair", "MatchMeshEdge", "FillMeshHole", "FillMeshHoles", "SplitDisjointMesh", "SplitMeshEdge", "SplitMeshWithCurve", "ExtractMeshFaces", "ExtractMeshEdges", "ExtractMeshPart", "ExtractConnectedMeshFaces", "ExtractNonManifoldMeshEdges", "ExtractDuplicateMeshFaces", "DeleteMeshFaces", "ExtrudeMesh", "ExtrudeMeshEdge", "OffsetMesh", "MeshToNURB", "MeshOutline", "MeshWireframe", "ExtractPt", "CullDegenerateMeshFaces", "CheckMesh", "SwapMeshEdge", "CollapseMeshEdge", "CollapseMeshFace", "CollapseMeshVertex", "CollapseMeshFacesByArea", "CollapseMeshFacesByAspectRatio", "CollapseMeshFacesByEdgeLength", "MeshSmooth", "ShrinkWrap", "ApplyMesh", "ApplyCurvePiping", "ApplyDisplacement", "ApplyEdgeSoftening", "ApplyShutLining", "AlignMeshVertices", "PolygonMesh", "MeshSelfIntersect", "MeshToSubD", "MeshFromSubD", "ExportSelected"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Dimension")) {
    Items(app, {"Dim", "DimAligned", "DimRotated", "DimAngle", "DimRadius", "DimDiameter", "DimOrdinate", "DimArea", "DimCurveLength", "DimCreaseAngle", "DimRecenterText", "Leader", "Text", "TextObject", "Hatch", "HatchBase", "HatchScale", "Annotate", "AnnotationHistory", "DimStyles", "ConvertDots", "DotFormat", "MatchDimStyle", "MakeCurve", "Make2D", "Section", "SectionTools", "Length", "Area", "AreaCentroid", "AreaMoments", "Volume", "VolumeCentroid", "VolumeMoments", "Distance", "Angle", "Radius", "Diameter", "EvaluatePt", "EvaluateUVPt", "ExtractPt", "Analyze"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Transform")) {
    Items(app, {"Move", "Copy", "Rotate", "Rotate3D", "Scale", "Scale1D", "Scale2D", "ScaleNU", "ScaleByPlane", "Mirror", "Orient", "Orient3Pt", "OrientOnCrv", "OrientOnSrf", "OrientCameraToSrf", "Array", "ArrayPolar", "ArrayCrv", "ArrayCrvOnSrf", "ArraySrf", "ArrayLinear", "ArrayHole", "Gumball", "BoxEdit", "SetPt", "Shear", "Twist", "Bend", "Taper", "Flow", "FlowAlongSrf", "Maelstrom", "Splop", "Stretch", "Smooth", "Fair", "CageEdit", "Cage", "ReleaseFromCage", "SoftMove", "SoftEditCrv", "SoftEditSrf", "MoveUVN", "Project", "ProjectToCPlane", "RemapCPlane", "Align", "Distribute", "Group", "Ungroup", "Explode", "History", "RecordHistory", "HistoryPurge", "Dragmode", "Nudge", "Symmetry", "Reflect"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Tools")) {
    Items(app, {"Options", "DocumentProperties", "Toolbar", "ToolbarReset", "Alias", "Macro", "MacroEditor", "ReadCommandFile", "CommandHistory", "CommandList", "CommandPaste", "Calc", "CalcRPN", "Units", "Snap", "Osnap", "Ortho", "Planar", "SmartTrack", "ProjectOsnap", "PersistentOnCrv", "PersistentOnSrf", "PersistentOnMesh", "PersistentOnPolysrf", "DisableOsnap", "Gumball", "PointsOn", "PointsOff", "Layer", "LayerStateManager", "Repeat", "ScriptEditor", "RunScript", "RunPythonScript", "EditPythonScript", "Grasshopper", "GrasshopperPlayer", "PackageManager", "PluginManager", "Audit", "Audit3dmFile", "SystemInfo", "Notes", "Check", "SelBadObjects", "Purge", "ClearUndo", "ClearAllMeshes", "Lock", "Unlock", "Hide", "Show", "Isolate", "Unisolate", "Zoo", "Zoom"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Analyze")) {
    Items(app, {"Distance", "Length", "Angle", "Radius", "Diameter", "Area", "AreaCentroid", "AreaMoments", "Volume", "VolumeCentroid", "VolumeMoments", "BoundingBox", "Dir", "What", "List", "Check", "CheckMesh", "SelBadObjects", "EvaluatePt", "EvaluateUVPt", "Curvature", "CurvatureGraph", "CurvatureGraphOff", "CurvatureAnalysis", "CurvatureAnalysisOff", "Zebra", "ZebraOff", "EMap", "EMapOff", "DraftAngleAnalysis", "DraftAngleAnalysisOff", "ThicknessAnalysis", "ThicknessAnalysisOff", "EdgeContinuity", "ShowEdges", "ShowEdgesOff", "SelDup", "SelSmall", "CrvDeviation", "PointDeviation", "GetDocumentUserText", "GetUserText", "SetObjectName", "SetUserText", "DocumentUserText", "Report", "Intersect", "GeometryTree", "Hydrostatics", "Moments"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Render")) {
    Items(app, {"Render", "RenderPreview", "RenderWindow", "RenderOpenLastRendering", "RenderPreviewInWindow", "RenderPreviewSelected", "RenderSettings", "RenderPresets", "RenderReportBug", "Environment", "Environments", "EnvironmentEditor", "Materials", "MaterialEditor", "Textures", "TextureMapping", "Lights", "LightManager", "Spotlight", "PointLight", "DirectionalLight", "RectangularLight", "LinearLight", "Skylight", "Sun", "GroundPlane", "RenderMesh", "RenderMeshSettings", "SetRenderColor", "SetObjectDisplayMode", "SetDisplayMode", "TurnTable", "ViewCaptureToFile", "ViewCaptureToClipboard", "ScreenCaptureToFile", "ScreenCaptureToClipboard", "Animate", "SetActiveRenderer", "Snapshots", "CycleShadows", "Denoise"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Panels")) {
    ImGui::MenuItem("Layers", "F4", &p.layers);
    ImGui::MenuItem("Properties", "F3", &p.properties);
    ImGui::MenuItem("Command History", "F2", &p.command_history);
    ImGui::MenuItem("Command List (all 1055)", "Ctrl+F1", &p.command_list);
    ImGui::MenuItem("Help", "", &p.help);
    ImGui::MenuItem("Notifications", "", &p.notifications);
    ImGui::MenuItem("Named Views", "", &p.named_views);
    ImGui::MenuItem("Notes", "", &p.notes);
    ImGui::MenuItem("Document User Text", "", &p.document_user_text);
    ImGui::MenuItem("Linetypes", "", &p.linetypes);
    ImGui::MenuItem("Materials", "", &p.materials);
    ImGui::MenuItem("Display", "", &p.display);
    ImGui::MenuItem("Object Snaps", "", &p.object_snaps);
    ImGui::MenuItem("Toolbars", "", &p.toolbars);
    ImGui::MenuItem("Calculator", "", &p.calculator);
    ImGui::MenuItem("BoxEdit", "", &p.box_edit);
    ImGui::MenuItem("Layer State Manager", "", &p.layer_state_manager);
    ImGui::MenuItem("Selection Filter", "", &p.selection_filter);
    ImGui::MenuItem("Macro Editor", "", &p.macro_editor);
    ImGui::MenuItem("Clipping Planes", "", &p.clipping_planes);
    ImGui::MenuItem("Layouts", "", &p.layouts);
    ImGui::MenuItem("Named CPlanes", "", &p.named_cplanes);
    ImGui::MenuItem("Viewport Tabs", "", &app.show_viewport_tabs);
    ImGui::Separator();
    if (ImGui::MenuItem("Reset Window Layout")) app.SetViewportLayout(4);
    ImGui::MenuItem("ImGui Demo (developer)", "", &p.imgui_demo);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    Item(app, "Command Help", "Help", "F1");
    if (ImGui::MenuItem("Command List / Feature Coverage", "Ctrl+F1")) p.command_list = true;
    Items(app, {"CommandHelp", "LearnRhino", "Tutorials", "WhatsNew", "CheckForUpdates", "SystemInfo", "TechSupport", "Licenses"});
    ImGui::Separator();
    if (ImGui::MenuItem("About Dino 8")) p.about = true;
    ImGui::EndMenu();
  }

  // Right-aligned document status.
  const std::string title = (doc.Path().empty() ? std::string("Untitled") : doc.Path()) + (doc.Modified() ? " *" : "");
  const float w = ImGui::CalcTextSize(title.c_str()).x + 20.0f;
  ImGui::SameLine(ImGui::GetWindowWidth() - w);
  ImGui::TextDisabled("%s", title.c_str());
  ImGui::EndMainMenuBar();
}

}  // namespace dino8::app
