#include "flow/FlowPreview.h"

#include "commands/cmd_common.h"
#include "geom/BrepMesher.h"

namespace dino8::flow {

namespace {

void PushPoint(std::vector<float>& buf, kernel::Point3d p) {
  buf.push_back(static_cast<float>(p.x));
  buf.push_back(static_cast<float>(p.y));
  buf.push_back(static_cast<float>(p.z));
}

void AppendMeshEdges(const kernel::Mesh& m, std::vector<float>& lines) {
  const ON_Mesh& raw = m.raw();
  for (int f = 0; f < raw.FaceCount(); ++f) {
    const ON_MeshFace& face = raw.m_F[f];
    const int idx[5] = {face.vi[0], face.vi[1], face.vi[2], face.IsQuad() ? face.vi[3] : face.vi[0], face.vi[0]};
    const int n = face.IsQuad() ? 4 : 3;
    for (int i = 0; i < n; ++i) {
      PushPoint(lines, raw.Vertex(idx[i]));
      PushPoint(lines, raw.Vertex(idx[i + 1]));
    }
  }
}

}  // namespace

void AppendPreviewGeometry(const Value& v, std::vector<float>& lines, std::vector<float>& points) {
  switch (v.kind) {
    case Kind::Point: PushPoint(points, v.point); break;
    case Kind::Vector: {
      PushPoint(lines, Point3d(0, 0, 0));
      PushPoint(lines, v.point);
      break;
    }
    case Kind::Plane: {
      const double s = 2.0;
      PushPoint(lines, v.plane.origin); PushPoint(lines, v.plane.origin + v.plane.x * s);
      PushPoint(lines, v.plane.origin); PushPoint(lines, v.plane.origin + v.plane.y * s);
      break;
    }
    case Kind::Curve: {
      if (!v.curve) break;
      const kernel::Interval d = v.curve->Domain();
      const int n = std::max(8, v.curve->SuggestedSamples(0.02));
      Point3d prev = v.curve->PointAt(d.min);
      for (int i = 1; i <= n; ++i) {
        const Point3d cur = v.curve->PointAt(d.min + (d.max - d.min) * i / n);
        PushPoint(lines, prev); PushPoint(lines, cur);
        prev = cur;
      }
      break;
    }
    case Kind::Surface: {
      if (!v.surface) break;
      const kernel::Mesh m = v.surface->TessellateGridAdaptive(0.1);
      AppendMeshEdges(m, lines);
      break;
    }
    case Kind::Brep: {
      if (!v.brep) break;
      try {
        app::BrepMeshOptions opt;
        opt.chord_tolerance = 0.2;
        const kernel::Mesh m = app::MeshBrepClosed(v.brep->raw(), opt);
        AppendMeshEdges(m, lines);
      } catch (...) {}
      break;
    }
    case Kind::Mesh: {
      if (v.mesh) AppendMeshEdges(*v.mesh, lines);
      break;
    }
    default: break;
  }
}

void CollectGraphPreview(Graph& g, std::vector<float>& lines, std::vector<float>& points,
                         std::vector<std::pair<kernel::Point3d, std::string>>& tags) {
  for (auto& n : g.Nodes()) {
    if (!n->enabled) continue;
    if (n->def && n->def->special == NodeDef::Special::TextTag) {
      Point3d p;
      if (n->inputs.size() >= 2) {
        const Value* first = nullptr;
        // Gathered input trees live in the wired source node's output; for
        // a literal (unwired) value use the typed default.
        if (n->inputs[0].has_user_value) first = &n->inputs[0].user_value;
        if (first && first->AsPoint(p)) tags.emplace_back(p, n->inputs[1].has_user_value ? n->inputs[1].user_value.AsText() : n->text);
      }
      continue;
    }
    if (!n->preview) continue;
    for (Port& out : n->outputs) {
      for (const Branch& b : out.data.branches) {
        for (const Value& v : b.items) AppendPreviewGeometry(v, lines, points);
      }
    }
  }
}

app::ObjectId BakeValue(app::Document& doc, const Value& v, int layer_index, const std::string& name,
                        const std::string& graph_source, NodeId source_node) {
  app::SceneObject obj;
  switch (v.kind) {
    case Kind::Point: obj = app::SceneObject::MakePoint(v.point); break;
    case Kind::Curve: if (v.curve) obj = app::SceneObject::MakeCurve(*v.curve); else return app::kNoObject; break;
    case Kind::Surface: if (v.surface) obj = app::SceneObject::MakeSurface(*v.surface); else return app::kNoObject; break;
    case Kind::Brep: if (v.brep) obj = app::SceneObject::MakeBrep(*v.brep); else return app::kNoObject; break;
    case Kind::Mesh: if (v.mesh) obj = app::SceneObject::MakeMesh(*v.mesh); else return app::kNoObject; break;
    default: return app::kNoObject;
  }
  if (layer_index >= 0) obj.layer_index = layer_index;
  if (!name.empty()) obj.name = name;
  if (!graph_source.empty()) {
    obj.user_text["FlowGraph"] = graph_source;
    obj.user_text["FlowNode"] = std::to_string(source_node);
  }
  return doc.Add(std::move(obj));
}

int BakeGraph(Graph& g, app::Document& doc) {
  int baked = 0;
  bool began = false;
  for (auto& n : g.Nodes()) {
    if (!n->def || n->def->special != NodeDef::Special::Bake || !n->enabled) continue;
    const std::string layer_name = n->inputs.size() > 1 && n->inputs[1].has_user_value ? n->inputs[1].user_value.AsText() : "";
    const std::string base_name = n->inputs.size() > 2 && n->inputs[2].has_user_value ? n->inputs[2].user_value.AsText() : "";
    int layer_index = -1;
    if (!layer_name.empty()) {
      layer_index = doc.FindLayer(layer_name);
      if (layer_index < 0) {
        if (!began) { doc.BeginChange("GrasshopperPlayer Bake"); began = true; }
        layer_index = doc.AddLayer(layer_name);
      }
    }
    // The Bake node's own input port holds the gathered upstream tree once
    // the graph has been solved (GatherInput populates it via the wire).
    Tree gathered;
    for (const flow::Wire& w : g.Wires()) {
      if (w.to == n->id && w.to_port == 0) {
        if (Node* src = g.Find(w.from)) gathered = src->outputs[static_cast<size_t>(w.from_port)].data;
      }
    }
    if (gathered.Empty() && n->inputs[0].has_user_value) gathered = Tree::Single(n->inputs[0].user_value);
    int i = 0;
    for (const Value& v : gathered.AllItems()) {
      if (!began) { doc.BeginChange("GrasshopperPlayer Bake"); began = true; }
      const std::string nm = base_name.empty() ? "" : (base_name + (gathered.ItemCount() > 1 ? "_" + std::to_string(i) : ""));
      if (BakeValue(doc, v, layer_index, nm, g.path, n->id) != app::kNoObject) ++baked;
      ++i;
    }
  }
  return baked;
}

int RebakeGraph(Graph& g, app::Document& doc, const std::string& graph_source) {
  if (!graph_source.empty()) {
    std::vector<app::ObjectId> stale;
    for (const app::SceneObject& o : doc.Objects()) {
      auto it = o.user_text.find("FlowGraph");
      if (it != o.user_text.end() && it->second == graph_source) stale.push_back(o.id);
    }
    for (app::ObjectId id : stale) doc.Remove(id);
  }
  return BakeGraph(g, doc);
}

}  // namespace dino8::flow
