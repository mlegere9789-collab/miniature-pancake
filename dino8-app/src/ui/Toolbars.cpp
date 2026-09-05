// Rhino-style tabbed toolbar groups (Standard, Curve Tools, Surface Tools,
// ...) drawn as vector icon buttons with optional captions, plus the
// vertical "sidebar" toolbar on the left edge. Every button runs a command
// by name; a right click runs the button's alternate command (Circle ->
// Circle3Pt, ExtrudeCrv -> ExtrudeSrf...) exactly like Rhino's buttons.
//
// The Standard tab is the user-customisable one (app.toolbar_commands,
// edited in Options > Toolbar or with Ctrl+right-click on a button).
#include "app/Application.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/Icons.h"
#include "ui/Panels.h"
#include "ui/Theme.h"

namespace dino8::app {

namespace {

struct ToolButton {
  const char* command;
  const char* label;      // short caption under the icon
  const char* alt;        // right-click command (nullptr = none)
  const char* tip;        // one-line fallback when the catalog has no text
};

// Every button that can appear on any tab or on the sidebar.
const ToolButton kButtons[] = {
    {"New", "New", nullptr, "New document"},
    {"Open", "Open", "Import", "Open a .3dm / OBJ / STL / DXF"},
    {"Save", "Save", "SaveAs", "Save (Ctrl+S)"},
    {"Import", "Import", nullptr, "Import geometry into this document"},
    {"Export", "Export", nullptr, "Export selected objects"},
    {"Print", "Print", nullptr, "Print / export a vector drawing"},
    {"Undo", "Undo", "UndoMultiple", "Undo (Ctrl+Z)"},
    {"Redo", "Redo", "RedoMultiple", "Redo (Ctrl+Y)"},
    {"Cut", "Cut", nullptr, "Cut (Ctrl+X)"},
    {"CopyToClipboard", "Copy", nullptr, "Copy to clipboard (Ctrl+C)"},
    {"Paste", "Paste", nullptr, "Paste (Ctrl+V)"},
    {"Point", "Point", "Points", "Point object"},
    {"Line", "Line", "Lines", "Line from two points"},
    {"Polyline", "Polyline", "Rectangle", "Polyline through points"},
    {"Curve", "Curve", "InterpCrv", "Control point curve"},
    {"InterpCrv", "Interp", "Sketch", "Curve through points"},
    {"Circle", "Circle", "Circle3Pt", "Circle: centre, radius"},
    {"Arc", "Arc", "Arc3Pt", "Arc: centre, start, end"},
    {"Rectangle", "Rect", "Rectangle3Pt", "Rectangle: two corners"},
    {"Polygon", "Polygon", "PolygonStar", "Polygon: centre, radius"},
    {"Ellipse", "Ellipse", "Ellipse3Pt", "Ellipse: centre, axes"},
    {"Helix", "Helix", "Spiral", "Helix"},
    {"Offset", "Offset", "OffsetSrf", "Offset curve"},
    {"Fillet", "Fillet", "FilletCorners", "Fillet curves"},
    {"Chamfer", "Chamfer", "ChamferEdge", "Chamfer curves"},
    {"Extend", "Extend", "ExtendSrf", "Extend curve"},
    {"Rebuild", "Rebuild", "RebuildUV", "Rebuild curve / surface"},
    {"Blend", "Blend", "BlendSrf", "Blend curves"},
    {"Project", "Project", "Pull", "Project curves onto a surface"},
    {"Divide", "Divide", "ClosestPt", "Divide a curve into points"},
    {"PointsOn", "Points On", "PointsOff", "Control points on (F10)"},
    {"Text", "Text", "TextObject", "Text"},
    {"Dim", "Dim", "DimAligned", "Linear dimension"},
    {"DimRadius", "Radius", "DimDiameter", "Radius dimension"},
    {"DimAngle", "Angle", nullptr, "Angle dimension"},
    {"Leader", "Leader", nullptr, "Leader"},
    {"Hatch", "Hatch", "HatchBase", "Hatch"},
    {"Make2D", "Make2D", "Silhouette", "2D drawing from 3D"},
    {"Box", "Box", "SubDBox", "Box: two corners, height"},
    {"Sphere", "Sphere", "SubDSphere", "Sphere: centre, radius"},
    {"Cylinder", "Cylinder", "Tube", "Cylinder"},
    {"Cone", "Cone", "Pyramid", "Cone"},
    {"Torus", "Torus", "Pipe", "Torus"},
    {"Pyramid", "Pyramid", "Ellipsoid", "Pyramid"},
    {"Plane", "Plane", "PlaneThroughPt", "Planar surface: corners"},
    {"PlanarSrf", "Planar", "EdgeSrf", "Surface from planar curves"},
    {"ExtrudeCrv", "Extrude", "ExtrudeSrf", "Extrude a curve"},
    {"Revolve", "Revolve", "RailRevolve", "Revolve a curve"},
    {"Loft", "Loft", "EdgeSrf", "Loft curves"},
    {"Sweep1", "Sweep 1", "Sweep2", "Sweep along one rail"},
    {"Sweep2", "Sweep 2", "Sweep1", "Sweep along two rails"},
    {"NetworkSrf", "Network", "EdgeSrf", "Surface from a curve network"},
    {"Pipe", "Pipe", "Tube", "Pipe around a curve"},
    {"OffsetSrf", "Offset", "Shell", "Offset surface"},
    {"FilletSrf", "Fillet", "BlendSrf", "Fillet surfaces"},
    {"BooleanUnion", "Union", "BooleanDifference", "Boolean union"},
    {"BooleanDifference", "Diff", "BooleanIntersection", "Boolean difference"},
    {"BooleanIntersection", "Inter", "BooleanSplit", "Boolean intersection"},
    {"Cap", "Cap", "Shell", "Cap planar holes"},
    {"Shell", "Shell", "OffsetSrf", "Hollow a solid"},
    {"FilletEdge", "Fillet", "ChamferEdge", "Fillet solid edges"},
    {"ChamferEdge", "Chamfer", "FilletEdge", "Chamfer solid edges"},
    {"ExtrudeSrf", "Ext Srf", "ExtrudeCrv", "Extrude a surface"},
    {"Move", "Move", nullptr, "Move"},
    {"Copy", "Copy", "CopyToClipboard", "Copy in place"},
    {"Rotate", "Rotate", "Rotate3D", "Rotate"},
    {"Scale", "Scale", "ScaleNU", "Scale"},
    {"Mirror", "Mirror", "Symmetry", "Mirror"},
    {"Array", "Array", "ArrayPolar", "Rectangular array"},
    {"ArrayPolar", "Polar", "ArrayCrv", "Polar array"},
    {"Orient", "Orient", "Orient3Pt", "Orient"},
    {"Align", "Align", "Distribute", "Align objects"},
    {"Twist", "Twist", "Bend", "Twist"},
    {"Bend", "Bend", "Taper", "Bend"},
    {"Gumball", "Gumball", nullptr, "Toggle the gumball"},
    {"Trim", "Trim", "Split", "Trim"},
    {"Split", "Split", "Trim", "Split"},
    {"Join", "Join", "Explode", "Join"},
    {"Explode", "Explode", "Join", "Explode"},
    {"Delete", "Delete", nullptr, "Delete (Del)"},
    {"Hide", "Hide", "Show", "Hide (Ctrl+H)"},
    {"Show", "Show", "ShowSelected", "Show all"},
    {"Lock", "Lock", "Unlock", "Lock"},
    {"Isolate", "Isolate", "Unisolate", "Isolate selection"},
    {"Group", "Group", "Ungroup", "Group (Ctrl+G)"},
    {"Layer", "Layers", nullptr, "Layers panel"},
    {"Distance", "Distance", "Angle", "Measure distance"},
    {"Length", "Length", "Radius", "Curve length"},
    {"Area", "Area", "AreaCentroid", "Area"},
    {"Volume", "Volume", "VolumeCentroid", "Volume"},
    {"What", "What", "List", "Describe objects"},
    {"BoundingBox", "BBox", nullptr, "Bounding box"},
    {"Curvature", "Curvature", "CurvatureGraph", "Curvature"},
    {"Zebra", "Zebra", "ZebraOff", "Zebra stripe analysis"},
    {"EMap", "EMap", "EMapOff", "Environment map analysis"},
    {"CurvatureAnalysis", "Curv Anl", "CurvatureAnalysisOff", "Curvature analysis"},
    {"DraftAngleAnalysis", "Draft", "DraftAngleAnalysisOff", "Draft angle analysis"},
    {"Check", "Check", "SelBadObjects", "Check objects"},
    {"Dir", "Dir", "Flip", "Direction"},
    {"Zoom", "Zoom", "ZoomWindow", "Zoom"},
    {"ZoomExtents", "Zoom Ext", "ZoomExtentsAll", "Zoom extents"},
    {"ZoomSelected", "Zoom Sel", "ZoomTarget", "Zoom selected"},
    {"Pan", "Pan", "RotateView", "Pan"},
    {"UndoView", "Undo View", "RedoView", "Undo view change"},
    {"4View", "4 Views", "3View", "Four viewports"},
    {"MaxViewport", "Maximize", nullptr, "Maximize viewport"},
    {"Perspective", "Persp", "Isometric", "Perspective view"},
    {"Top", "Top", "Bottom", "Top view"},
    {"Front", "Front", "Back", "Front view"},
    {"Right", "Right", "Left", "Right view"},
    {"Wireframe", "Wire", "XRayViewport", "Wireframe display"},
    {"Shade", "Shaded", "GhostedViewport", "Shaded display"},
    {"Rendered", "Rendered", "RenderedViewport", "Rendered display"},
    {"RenderedViewport", "Rendered", "ArcticViewport", "Rendered display"},
    {"Grid", "Grid", nullptr, "Toggle the grid (F7)"},
    {"Render", "Render", "RenderPreview", "Render"},
    {"RenderPreview", "Preview", nullptr, "Render preview"},
    {"Materials", "Materials", "MaterialEditor", "Materials"},
    {"Lights", "Lights", "PointLight", "Lights"},
    {"Spotlight", "Spot", "DirectionalLight", "Spotlight"},
    {"EnvironmentEditor", "Env", nullptr, "Environment editor"},
    {"Sun", "Sun", "GroundPlane", "Sun"},
    {"ViewCaptureToFile", "Capture", "ScreenCaptureToFile", "Capture the viewport to a file"},
    {"Mesh", "Mesh", "MeshFromPoints", "Mesh from NURBS"},
    {"MeshBox", "Box", "MeshSphere", "Mesh box"},
    {"MeshSphere", "Sphere", "MeshCylinder", "Mesh sphere"},
    {"MeshPlane", "Plane", "MeshCone", "Mesh plane"},
    {"MeshBooleanUnion", "Union", "MeshBooleanDifference", "Mesh boolean union"},
    {"MeshBooleanDifference", "Diff", "MeshBooleanIntersection", "Mesh boolean difference"},
    {"MeshSplit", "Split", "MeshTrim", "Split meshes"},
    {"Weld", "Weld", "Unweld", "Weld mesh vertices"},
    {"ReduceMesh", "Reduce", "QuadRemesh", "Reduce mesh"},
    {"MeshRepair", "Repair", "FillMeshHoles", "Mesh repair"},
    {"FillMeshHoles", "Fill", "MeshRepair", "Fill mesh holes"},
    {"MeshToNURB", "To NURBS", "MeshToSubD", "Mesh to NURBS"},
    {"OffsetMesh", "Offset", "ExtrudeMesh", "Offset mesh"},
    {"SubDBox", "Box", "SubDSphere", "SubD box"},
    {"SubDSphere", "Sphere", "SubDCylinder", "SubD sphere"},
    {"SubDCylinder", "Cylinder", "SubDCone", "SubD cylinder"},
    {"SubDPlane", "Plane", "SubDTorus", "SubD plane"},
    {"ToSubD", "To SubD", "ToNURBS", "Convert to SubD"},
    {"SubDLoft", "Loft", "SubDSweep1", "SubD loft"},
    {"OffsetSubD", "Offset", "SubDThicken", "Offset SubD"},
    {"Crease", "Crease", "RemoveCrease", "Crease SubD edges"},
    {"Bridge", "Bridge", "Stitch", "Bridge SubD edges"},
    {"SubDDisplayToggle", "Flat/Smooth", nullptr, "Toggle SubD display"},
    {"Reflect", "Reflect", "Symmetry", "Reflect SubD"},
    {"Block", "Block", "Insert", "Define a block"},
    {"Insert", "Insert", "BlockManager", "Insert a block"},
    {"Options", "Options", "DocumentProperties", "Options"},
    {"Help", "Help", "CommandList", "Help (F1)"},
    {"SelAll", "Sel All", "SelNone", "Select all (Ctrl+A)"},
    {"Ungroup", "Ungroup", "Group", "Ungroup"},
    {"Unlock", "Unlock", "Lock", "Unlock"},
};

const ToolButton* FindButton(const std::string& command) {
  for (const ToolButton& b : kButtons) if (command == b.command) return &b;
  return nullptr;
}

struct ToolbarTab {
  const char* name;
  std::vector<const char*> commands;  // "|" = separator
};

const char* const kSep = "|";

const ToolbarTab kTabs[] = {
    {"Standard", {}},  // filled from app.toolbar_commands
    {"Curve Tools", {"Point", "Line", "Polyline", "Curve", "InterpCrv", "Circle", "Arc", "Rectangle", "Polygon", "Ellipse", "Helix", kSep,
                     "Offset", "Fillet", "Chamfer", "Extend", "Blend", "Trim", "Split", "Join", "Rebuild", "Divide", "Project", "PointsOn"}},
    {"Surface Tools", {"Plane", "PlanarSrf", "ExtrudeCrv", "Revolve", "Loft", "Sweep1", "Sweep2", "NetworkSrf", "Pipe", kSep,
                       "OffsetSrf", "FilletSrf", "Blend", "Trim", "Split", "Join", "Rebuild", "Cap", "Dir"}},
    {"Solid Tools", {"Box", "Sphere", "Cylinder", "Cone", "Torus", "Pyramid", "ExtrudeCrv", "ExtrudeSrf", kSep,
                     "BooleanUnion", "BooleanDifference", "BooleanIntersection", "Cap", "Shell", "FilletEdge", "ChamferEdge", "OffsetSrf", "Explode"}},
    {"Mesh Tools", {"Mesh", "MeshBox", "MeshSphere", "MeshPlane", kSep, "MeshBooleanUnion", "MeshBooleanDifference", "MeshSplit", "Weld",
                    "ReduceMesh", "MeshRepair", "FillMeshHoles", "OffsetMesh", "MeshToNURB"}},
    {"SubD", {"SubDBox", "SubDSphere", "SubDCylinder", "SubDPlane", "ToSubD", "SubDLoft", kSep, "OffsetSubD", "Crease", "Bridge", "Reflect", "SubDDisplayToggle"}},
    {"Transform", {"Move", "Copy", "Rotate", "Scale", "Mirror", "Array", "ArrayPolar", "Orient", "Align", kSep, "Twist", "Bend", "Gumball", kSep,
                   "Group", "Ungroup", "Hide", "Show", "Lock", "Unlock", "Isolate", "Delete"}},
    {"Analyze", {"Distance", "Length", "Area", "Volume", "What", "BoundingBox", "Curvature", kSep, "Zebra", "EMap", "CurvatureAnalysis", "DraftAngleAnalysis", "Check", "Dir"}},
    {"Drafting", {"Text", "Dim", "DimRadius", "DimAngle", "Leader", "Hatch", "Make2D", kSep, "Line", "Polyline", "Circle", "Rectangle", "Block", "Insert", "Print"}},
    {"Render", {"Render", "RenderPreview", "Materials", "Lights", "Spotlight", "Sun", "EnvironmentEditor", kSep, "RenderedViewport", "ViewCaptureToFile"}},
    {"View", {"Zoom", "ZoomExtents", "ZoomSelected", "Pan", "UndoView", kSep, "4View", "MaxViewport", "Perspective", "Top", "Front", "Right", kSep,
              "Wireframe", "Shade", "RenderedViewport", "Grid"}},
};
constexpr int kTabCount = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));

const char* kSidebar[] = {
    "Point", "Line", "Polyline", "Curve", "Circle", "Arc", "Rectangle", "Polygon",
    "Box", "Sphere", "Cylinder", "ExtrudeCrv", "Loft", "Revolve", "PlanarSrf", "Pipe",
    "BooleanUnion", "BooleanDifference", "Move", "Copy", "Rotate", "Scale", "Mirror", "Array",
    "Trim", "Split", "Join", "Explode", "Fillet", "Offset", "Hide", "Show",
};

float LabelFontSize() { return 11.5f * ImGui::GetStyle().FontScaleMain; }

// Fitted button size for the current settings: icon + caption.
ImVec2 ButtonSize(const Application& app, const char* label) {
  const float icon = static_cast<float>(app.toolbar_icon_size);
  float w = icon + 12.0f;
  float h = icon + 10.0f;
  if (app.toolbar_labels && label) {
    ImGui::PushFont(nullptr, LabelFontSize());
    const float tw = ImGui::CalcTextSize(label).x + 8.0f;
    h += ImGui::GetTextLineHeight() + 1.0f;
    ImGui::PopFont();
    w = std::max(w, tw);
  }
  return ImVec2(w, h);
}

void RichTooltip(Application& app, const ToolButton* b, const std::string& command, bool customizable) {
  const RegisteredCommand* rc = app.Engine().Find(command);
  const CommandInfo* info = rc ? rc->info : app.Catalog().Find(command);
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(360.0f);
  ImGui::TextColored(ThemeColors::Accent(), "%s", rc ? rc->name.c_str() : command.c_str());
  if (rc && rc->status != CommandStatus::Implemented) {
    ImGui::SameLine();
    const ImVec4 col = rc->status == CommandStatus::Partial ? ImVec4(ThemeColors::kWarn[0], ThemeColors::kWarn[1], ThemeColors::kWarn[2], 1)
                                                            : ImVec4(ThemeColors::kMuted[0], ThemeColors::kMuted[1], ThemeColors::kMuted[2], 1);
    StatusBadge(CommandStatusName(rc->status), col);
  }
  std::string desc;
  if (info && !info->description.empty()) desc = info->description;
  else if (b && b->tip) desc = b->tip;
  if (!desc.empty()) ImGui::TextUnformatted(desc.c_str());
  if (rc && !rc->note.empty()) ImGui::TextDisabled("%s", rc->note.c_str());
  ImGui::Separator();
  ImGui::TextDisabled("Left click:");
  ImGui::SameLine();
  ImGui::TextUnformatted(command.c_str());
  if (b && b->alt) {
    ImGui::TextDisabled("Right click:");
    ImGui::SameLine();
    ImGui::TextUnformatted(b->alt);
  }
  if (customizable) ImGui::TextDisabled("Ctrl+right click: customize");
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
}

}  // namespace

std::vector<std::string> DefaultToolbarCommands() {
  return {"New", "Open", "Save", "|", "Undo", "Redo", "|", "Cut", "CopyToClipboard", "Paste", "Delete", "|",
          "Move", "Copy", "Rotate", "Scale", "Mirror", "|", "Join", "Explode", "Trim", "Split", "|",
          "Hide", "Show", "Lock", "Group", "|", "ZoomExtents", "4View", "Wireframe", "Shade", "RenderedViewport", "|", "Options", "Help"};
}

const char* ToolbarTabName(int index) {
  if (index < 0 || index >= kTabCount) return "";
  return kTabs[index].name;
}
int ToolbarTabCount() { return kTabCount; }

const char* ToolbarButtonLabel(const std::string& command) {
  const ToolButton* b = FindButton(command);
  return b ? b->label : nullptr;
}

float ToolbarHeight(const Application& app) {
  if (!app.Panels().toolbars) return 0.0f;
  const float tabs = ImGui::GetTextLineHeight() + 8.0f;
  return 2.0f + tabs + 4.0f + ButtonSize(app, app.toolbar_labels ? "Xg" : nullptr).y + 6.0f;
}

float LeftSidebarWidth(const Application& app) {
  if (!app.show_left_sidebar || !app.Panels().toolbars) return 0.0f;
  return 2.0f * (static_cast<float>(app.toolbar_icon_size) + 12.0f) + 3.0f * 3.0f + 2.0f;
}

IconButtonResult IconButton(Application& app, const char* command, const char* label, bool show_label, bool customizable) {
  IconButtonResult r;
  const ToolButton* b = FindButton(command);
  const RegisteredCommand* rc = app.Engine().Find(command);
  const bool implemented = rc && rc->status != CommandStatus::Planned;
  const bool running = app.Engine().IsRunning() && rc && rc->name == app.Engine().ActiveName();
  const ImVec2 size = ButtonSize(app, show_label ? label : nullptr);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::PushID(command);
  ImGui::InvisibleButton("##btn", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImGuiStyle& st = ImGui::GetStyle();
  // Background: nothing at rest, a soft plate on hover, accent while pressed or running.
  if (running) dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ThemeColors::AccentU32(0.35f), 4.0f);
  if (held) dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_ButtonActive), 4.0f);
  else if (hovered) dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_ButtonHovered), 4.0f);
  if (running) dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ThemeColors::AccentU32(0.9f), 4.0f);
  const ImVec4 text_col = implemented ? st.Colors[ImGuiCol_Text] : st.Colors[ImGuiCol_TextDisabled];
  const float icon = static_cast<float>(app.toolbar_icon_size);
  const ImVec2 ipos(pos.x + (size.x - icon) * 0.5f, pos.y + 5.0f);
  DrawIcon(dl, command, ipos, icon, ImGui::GetColorU32(text_col), implemented ? ThemeColors::AccentU32() : ImGui::GetColorU32(ImVec4(text_col.x, text_col.y, text_col.z, 0.7f)));
  if (show_label && label) {
    ImGui::PushFont(nullptr, LabelFontSize());
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + icon + 6.0f), ImGui::GetColorU32(ImVec4(text_col.x, text_col.y, text_col.z, 0.85f)), label);
    ImGui::PopFont();
  }
  if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && held) r.left = true;
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) r.left = true;
  if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
    if (ImGui::GetIO().KeyCtrl && customizable) r.context = true;
    else if (b && b->alt) r.right = true;
    else if (customizable) r.context = true;
  }
  if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay)) RichTooltip(app, b, command, customizable);
  ImGui::PopID();
  return r;
}

namespace {

// Draws one row of buttons for `commands`; returns the index of a button the
// user asked to remove (Standard tab only), or -1.
int DrawButtonRow(Application& app, const std::vector<std::string>& commands, bool customizable, bool show_labels) {
  int remove_index = -1;
  const float row_h = ButtonSize(app, show_labels ? "Xg" : nullptr).y;
  for (size_t i = 0; i < commands.size(); ++i) {
    const std::string& command = commands[i];
    ImGui::PushID(static_cast<int>(i));
    if (command == "|") {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + 3, p.y + 6), ImVec2(p.x + 3, p.y + row_h - 6), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
      ImGui::Dummy(ImVec2(7, row_h));
      ImGui::SameLine(0, 2);
      ImGui::PopID();
      continue;
    }
    const ToolButton* b = FindButton(command);
    const std::string label = b ? b->label : command;
    IconButtonResult r = IconButton(app, command.c_str(), label.c_str(), show_labels, customizable);
    if (r.left) app.Engine().Execute(command);
    if (r.right && b && b->alt) app.Engine().Execute(b->alt);
    if (r.context) ImGui::OpenPopup("tbctx");
    if (ImGui::BeginPopup("tbctx")) {
      ImGui::TextDisabled("%s", command.c_str());
      ImGui::Separator();
      if (ImGui::MenuItem("Remove from toolbar")) remove_index = static_cast<int>(i);
      if (ImGui::MenuItem("Customize toolbar...")) app.Panels().options = true;
      ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::SameLine(0, 2);
  }
  return remove_index;
}

}  // namespace

void DrawToolbars(Application& app) {
  if (app.toolbar_commands.empty()) app.toolbar_commands = DefaultToolbarCommands();
  app.toolbar_tab = std::clamp(app.toolbar_tab, 0, kTabCount - 1);
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float menu_h = ImGui::GetFrameHeight();
  const float h = ToolbarHeight(app);
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + menu_h));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                           ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 2));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  if (ImGui::Begin("##toolbar", nullptr, flags)) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Tab strip: flat text tabs with an accent underline on the active one.
    {
      const float tab_h = ImGui::GetTextLineHeight() + 8.0f;
      ImGui::PushFont(nullptr, 13.0f * ImGui::GetStyle().FontScaleMain);
      for (int t = 0; t < kTabCount; ++t) {
        const bool active = t == app.toolbar_tab;
        const ImVec2 ts = ImGui::CalcTextSize(kTabs[t].name);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 sz(ts.x + 16.0f, tab_h);
        ImGui::PushID(t);
        if (ImGui::InvisibleButton("##tab", sz)) app.toolbar_tab = t;
        const bool hov = ImGui::IsItemHovered();
        if (active) dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(ImGuiCol_WindowBg), 4.0f, ImDrawFlags_RoundCornersTop);
        else if (hov) dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(ImGuiCol_TabHovered), 4.0f, ImDrawFlags_RoundCornersTop);
        dl->AddText(ImVec2(p.x + 8.0f, p.y + 4.0f), ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled), kTabs[t].name);
        if (active) dl->AddRectFilled(ImVec2(p.x + 6.0f, p.y + sz.y - 2.0f), ImVec2(p.x + sz.x - 6.0f, p.y + sz.y), ThemeColors::AccentU32(), 1.0f);
        ImGui::PopID();
        ImGui::SameLine(0, 0);
      }
      ImGui::PopFont();
      // Right side of the strip: label / size quick toggles.
      const float right_w = 150.0f;
      ImGui::SameLine(ImGui::GetWindowWidth() - right_w);
      ImGui::PushFont(nullptr, 12.0f * ImGui::GetStyle().FontScaleMain);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 2));
      if (ImGui::SmallButton(app.toolbar_labels ? "Labels: on" : "Labels: off")) app.toolbar_labels = !app.toolbar_labels;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show captions under the toolbar icons (Options > Toolbar)");
      ImGui::SameLine();
      char sz_label[16];
      std::snprintf(sz_label, sizeof(sz_label), "%d px", app.toolbar_icon_size);
      if (ImGui::SmallButton(sz_label)) app.toolbar_icon_size = app.toolbar_icon_size == 24 ? 32 : app.toolbar_icon_size == 32 ? 40 : 24;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Icon size: 24 / 32 / 40 px (Options > Toolbar)");
      ImGui::PopStyleVar();
      ImGui::PopFont();
      ImGui::NewLine();
      // Divider under the strip.
      const ImVec2 wp = ImGui::GetWindowPos();
      dl->AddLine(ImVec2(wp.x, wp.y + tab_h + 2.0f), ImVec2(wp.x + ImGui::GetWindowWidth(), wp.y + tab_h + 2.0f), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    }
    ImGui::SetCursorPos(ImVec2(6.0f, 2.0f + ImGui::GetTextLineHeight() + 8.0f + 4.0f));
    // The button row, horizontally scrollable with the wheel when it overflows.
    ImGui::BeginChild("##tbrow", ImVec2(0, ButtonSize(app, app.toolbar_labels ? "Xg" : nullptr).y + 2.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().MouseWheel != 0.0f)
      ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 60.0f);
    if (app.toolbar_tab == 0) {
      const int remove_index = DrawButtonRow(app, app.toolbar_commands, true, app.toolbar_labels);
      if (remove_index >= 0) app.toolbar_commands.erase(app.toolbar_commands.begin() + remove_index);
    } else {
      std::vector<std::string> cmds;
      for (const char* c : kTabs[app.toolbar_tab].commands) cmds.push_back(c);
      DrawButtonRow(app, cmds, false, app.toolbar_labels);
    }
    ImGui::NewLine();
    ImGui::EndChild();
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(4);
}

void DrawLeftSidebar(Application& app) {
  const float w = LeftSidebarWidth(app);
  if (w <= 0.0f) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float top = vp->WorkPos.y + ToolbarHeight(app) + app.CommandLineHeight();
  const float bottom = vp->WorkPos.y + vp->WorkSize.y - app.StatusBarHeight();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, top));
  ImGui::SetNextWindowSize(ImVec2(w, std::max(10.0f, bottom - top)));
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3, 4));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  if (ImGui::Begin("##sidebar", nullptr, flags)) {
    const bool saved_labels = app.toolbar_labels;
    app.toolbar_labels = false;  // sidebar is icon-only
    int col = 0;
    for (const char* c : kSidebar) {
      const ToolButton* b = FindButton(c);
      IconButtonResult r = IconButton(app, c, b ? b->label : c, false, false);
      if (r.left) app.Engine().Execute(c);
      if (r.right && b && b->alt) app.Engine().Execute(b->alt);
      if (++col % 2) ImGui::SameLine();
    }
    app.toolbar_labels = saved_labels;
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(4);
}

}  // namespace dino8::app
