#include "dino8/kernel/boolean_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <numeric>
#include <tuple>

#include "dino8/kernel/boolean_general.h"

namespace dino8::kernel {

namespace {

bool Contains(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

const char* EngineEntryPoint(BooleanEngineKind e) {
  switch (e) {
    case BooleanEngineKind::Planar: return "BooleanCombinePlanar";
    case BooleanEngineKind::Mixed: return "BooleanCombineMixed";
    case BooleanEngineKind::General: return "BooleanCombineGeneral";
  }
  return "?";
}

void FillOperand(const Brep& b, BooleanOperandReport& out) {
  out.face_count = b.FaceCount();
  out.open_edges = ReportOpenEdges(b);
  for (const BooleanEdgeReport& e : out.open_edges) {
    if (e.trim_count >= 3) ++out.non_manifold_edges;
    else ++out.naked_edges;
  }
}

double Dist(const Point3d& a, const Point3d& b) { return (a - b).Length(); }

// Polygon area vector (Newell); length = 2 * area.
ON_3dVector NewellNormal(const std::vector<Point3d>& loop) {
  ON_3dVector n(0, 0, 0);
  for (size_t i = 0; i < loop.size(); ++i) {
    const Point3d& p = loop[i];
    const Point3d& q = loop[(i + 1) % loop.size()];
    n.x += (p.y - q.y) * (p.z + q.z);
    n.y += (p.z - q.z) * (p.x + q.x);
    n.z += (p.x - q.x) * (p.y + q.y);
  }
  return n;
}

struct HealAttempt {
  bool built = false;
  Brep brep;
  double max_move = 0.0;
  int merged = 0;
  std::string why;
};

HealAttempt TrySnap(const std::vector<Brep::PlanarFace>& faces, double bound) {
  HealAttempt out;
  // Cluster every loop vertex (union-find, single linkage) closer than `bound`.
  std::vector<Point3d> pts;
  std::vector<std::vector<int>> face_ids(faces.size());
  for (size_t f = 0; f < faces.size(); ++f) {
    for (const Point3d& p : faces[f].loop) {
      face_ids[f].push_back(static_cast<int>(pts.size()));
      pts.push_back(p);
    }
  }
  std::vector<int> parent(pts.size());
  std::iota(parent.begin(), parent.end(), 0);
  auto find = [&](int x) {
    while (parent[x] != x) x = parent[x] = parent[parent[x]];
    return x;
  };
  for (size_t i = 0; i < pts.size(); ++i)
    for (size_t j = i + 1; j < pts.size(); ++j)
      if (Dist(pts[i], pts[j]) <= bound) parent[find(static_cast<int>(i))] = find(static_cast<int>(j));
  std::map<int, std::pair<ON_3dVector, int>> sum;
  for (size_t i = 0; i < pts.size(); ++i) {
    auto& s = sum[find(static_cast<int>(i))];
    s.first += ON_3dVector(pts[i]);
    ++s.second;
  }
  std::vector<Point3d> snapped(pts.size());
  for (size_t i = 0; i < pts.size(); ++i) {
    const auto& s = sum[find(static_cast<int>(i))];
    snapped[i] = Point3d(s.first.x / s.second, s.first.y / s.second, s.first.z / s.second);
    out.max_move = std::max(out.max_move, Dist(pts[i], snapped[i]));
  }
  out.merged = static_cast<int>(pts.size() - sum.size());

  std::vector<Brep::PlanarFace> rebuilt;
  for (size_t f = 0; f < faces.size(); ++f) {
    std::vector<Point3d> loop;
    for (int id : face_ids[f]) {
      if (!loop.empty() && Dist(loop.back(), snapped[id]) < 1e-12) continue;
      loop.push_back(snapped[id]);
    }
    while (loop.size() > 1 && Dist(loop.front(), loop.back()) < 1e-12) loop.pop_back();
    // Drop spikes a->b->a created by merging (a vertex whose neighbours coincide).
    bool changed = true;
    while (changed && loop.size() >= 3) {
      changed = false;
      for (size_t i = 0; i < loop.size(); ++i) {
        const Point3d& prev = loop[(i + loop.size() - 1) % loop.size()];
        const Point3d& next = loop[(i + 1) % loop.size()];
        if (Dist(prev, next) < 1e-12) {
          loop.erase(loop.begin() + static_cast<long>(i));
          changed = true;
          break;
        }
      }
    }
    ON_3dVector n = NewellNormal(loop);
    if (loop.size() < 3 || n.Length() < 1e-12) {
      out.why = "a face collapses to a sliver or point at gap bound " + std::to_string(bound) +
                " (the model has a feature smaller than the bound)";
      return out;
    }
    n.Unitize();
    Brep::PlanarFace pf;
    pf.loop = loop;
    pf.plane = ON_Plane(loop[0], n);
    double worst = 0.0;
    for (const Point3d& p : loop) worst = std::max(worst, std::fabs(pf.plane.DistanceTo(p)));
    if (worst > bound) {
      out.why = "snapping leaves a face non-planar by " + std::to_string(worst) + " > bound " + std::to_string(bound);
      return out;
    }
    rebuilt.push_back(std::move(pf));
  }
  try {
    out.brep = Brep::FromPlanarFaces(rebuilt);
    out.built = true;
  } catch (const std::exception& e) {
    out.why = std::string("rebuilding the shell failed: ") + e.what();
  }
  return out;
}

bool IsClosedValidSolid(const Brep& b) {
  const Brep::CheckReport rep = b.Check();
  return rep.is_closed && b.raw().IsValid() && b.raw().IsSolid();
}

// Edge-count of a mesh welded on exact float coordinates.
void CountMeshOpenEdges(const Mesh& m, int& open_edges, int& non_manifold) {
  open_edges = 0;
  non_manifold = 0;
  const ON_Mesh& raw = m.raw();
  std::map<std::tuple<float, float, float>, int> weld;
  std::vector<int> id(static_cast<size_t>(raw.m_V.Count()));
  for (int i = 0; i < raw.m_V.Count(); ++i) {
    const ON_3fPoint& p = raw.m_V[i];
    id[static_cast<size_t>(i)] = weld.emplace(std::make_tuple(p.x, p.y, p.z), static_cast<int>(weld.size())).first->second;
  }
  std::map<std::pair<int, int>, int> uses;
  for (int f = 0; f < raw.m_F.Count(); ++f) {
    const ON_MeshFace& face = raw.m_F[f];
    const int n = face.IsQuad() ? 4 : 3;
    for (int k = 0; k < n; ++k) {
      const int a = id[static_cast<size_t>(face.vi[k])], b = id[static_cast<size_t>(face.vi[(k + 1) % n])];
      if (a == b) continue;
      ++uses[std::minmax(a, b)];
    }
  }
  for (const auto& kv : uses) {
    if (kv.second == 1) ++open_edges;
    else if (kv.second >= 3) ++non_manifold;
  }
}

}  // namespace

const char* BooleanFailureReasonName(BooleanFailureReason r) {
  switch (r) {
    case BooleanFailureReason::CompoundOperand: return "CompoundOperand";
    case BooleanFailureReason::UnsupportedOperation: return "UnsupportedOperation";
    case BooleanFailureReason::InvalidTolerance: return "InvalidTolerance";
    case BooleanFailureReason::EmptyOperand: return "EmptyOperand";
    case BooleanFailureReason::EmptyOperandGroup: return "EmptyOperandGroup";
    case BooleanFailureReason::UnsupportedGeometry: return "UnsupportedGeometry";
    case BooleanFailureReason::InvalidPolygon: return "InvalidPolygon";
    case BooleanFailureReason::InvalidDirection: return "InvalidDirection";
    case BooleanFailureReason::OpenOperand: return "OpenOperand";
    case BooleanFailureReason::NonManifoldResult: return "NonManifoldResult";
    case BooleanFailureReason::OpenResult: return "OpenResult";
    case BooleanFailureReason::EngineFailure: return "EngineFailure";
    case BooleanFailureReason::Unclassified: return "Unclassified";
  }
  return "?";
}

BooleanFailureReason ClassifyBooleanFailureMessage(const std::string& m) {
  if (Contains(m, "3 or more faces") || Contains(m, "3 or more fragment loops") ||
      Contains(m, "shared by 3 or more") || Contains(m, "claimed by 3 or more"))
    return BooleanFailureReason::NonManifoldResult;
  if (Contains(m, "Manifold::Status()") || Contains(m, "boolean operation failed"))
    return BooleanFailureReason::EngineFailure;
  if (Contains(m, "tolerance") && (Contains(m, "positive") || Contains(m, "finite")))
    return BooleanFailureReason::InvalidTolerance;
  if (Contains(m, "compound")) return BooleanFailureReason::CompoundOperand;
  if (Contains(m, "no faces")) return BooleanFailureReason::EmptyOperand;
  if (Contains(m, "must be non-zero") || Contains(m, "must be nonzero")) return BooleanFailureReason::InvalidDirection;
  if (Contains(m, "is not planar")) return BooleanFailureReason::UnsupportedGeometry;
  if (Contains(m, "not a closed") || Contains(m, "closed solid") ||
      Contains(m, "open shell") || Contains(m, "not closed"))
    return BooleanFailureReason::OpenOperand;
  if (Contains(m, "SymmetricDifference") || Contains(m, "not supported") || Contains(m, "unsupported") ||
      Contains(m, "out of scope") || Contains(m, "scope"))
    return BooleanFailureReason::UnsupportedGeometry;
  return BooleanFailureReason::Unclassified;
}

std::vector<BooleanEdgeReport> ReportOpenEdges(const Brep& brep) {
  std::vector<BooleanEdgeReport> out;
  Brep::CheckReport rep;
  try {
    rep = brep.Check();
  } catch (...) {
    return out;
  }
  for (const Brep::CheckIssue& issue : rep.issues) {
    if (issue.kind != Brep::CheckIssue::Kind::NakedEdge && issue.kind != Brep::CheckIssue::Kind::NonManifoldEdge) continue;
    BooleanEdgeReport e;
    e.edge_index = issue.index;
    e.trim_count = issue.other_index;
    e.location = issue.location;
    const ON_Brep& raw = brep.raw();
    if (issue.index >= 0 && issue.index < raw.m_E.Count()) {
      const ON_BrepEdge& edge = raw.m_E[issue.index];
      if (edge.m_vi[0] >= 0 && edge.m_vi[1] >= 0 && edge.m_vi[0] < raw.m_V.Count() && edge.m_vi[1] < raw.m_V.Count())
        e.length = Dist(raw.m_V[edge.m_vi[0]].Point(), raw.m_V[edge.m_vi[1]].Point());
    }
    out.push_back(e);
  }
  return out;
}

PlanarHealResult HealPlanarOperandGaps(const Brep& operand, double max_gap) {
  PlanarHealResult r;
  r.healed = operand;
  if (!(max_gap > 0.0) || !std::isfinite(max_gap)) {
    r.reason = BooleanFailureReason::InvalidTolerance;
    r.message = "dino8::kernel::HealPlanarOperandGaps: max_gap must be finite and strictly positive (tolerance not positive)";
    return r;
  }
  if (operand.FaceCount() == 0) {
    r.reason = BooleanFailureReason::EmptyOperand;
    r.message = "dino8::kernel::HealPlanarOperandGaps: operand has no faces";
    return r;
  }
  try {
    if (IsClosedValidSolid(operand)) {
      r.ok = true;
      r.already_closed = true;
      return r;
    }
    const ON_Brep& raw = operand.raw();
    for (int li = 0; li < raw.m_L.Count(); ++li) {
      if (raw.m_L[li].m_type == ON_BrepLoop::inner) {
        r.reason = BooleanFailureReason::UnsupportedGeometry;
        r.message = "dino8::kernel::HealPlanarOperandGaps: operand has faces with inner loops (holes); gap-healing handles simple planar polygons only";
        r.remaining_open_edges = ReportOpenEdges(operand);
        return r;
      }
    }
    std::vector<Brep::PlanarFace> faces;
    try {
      faces = operand.PlanarFaces();
    } catch (const std::exception& e) {
      r.reason = BooleanFailureReason::UnsupportedGeometry;
      r.message = std::string("dino8::kernel::HealPlanarOperandGaps: gap-healing needs an all-planar operand: ") + e.what();
      r.remaining_open_edges = ReportOpenEdges(operand);
      return r;
    }
    std::string last_why = "no candidate bound produced a closed shell";
    for (const double div : {64.0, 16.0, 4.0, 1.0}) {
      const double bound = max_gap / div;
      HealAttempt at = TrySnap(faces, bound);
      if (!at.built) {
        last_why = at.why;
        continue;
      }
      if (IsClosedValidSolid(at.brep)) {
        r.ok = true;
        r.healed = at.brep;
        r.gap_bound_used = bound;
        r.max_vertex_move = at.max_move;
        r.vertices_merged = at.merged;
        return r;
      }
      last_why = "snapped shell at bound " + std::to_string(bound) + " is still not a closed valid solid";
    }
    r.reason = BooleanFailureReason::OpenOperand;
    r.message = "dino8::kernel::HealPlanarOperandGaps: operand is not a closed shell and could not be healed within gap " +
                std::to_string(max_gap) + ": " + last_why;
    r.remaining_open_edges = ReportOpenEdges(operand);
  } catch (const std::exception& e) {
    r.reason = BooleanFailureReason::Unclassified;
    r.message = std::string("dino8::kernel::HealPlanarOperandGaps: ") + e.what();
  }
  return r;
}

BooleanReport DiagnoseBrepBoolean(const Brep& a_in, const Brep& b_in, BooleanOp op, BooleanEngineKind engine,
                                  const BooleanDiagnoseOptions& options) {
  BooleanReport rep;
  rep.engine = engine;
  rep.op = op;
  rep.function_name = EngineEntryPoint(engine);
  Brep a = a_in, b = b_in;
  FillOperand(a_in, rep.a);
  FillOperand(b_in, rep.b);

  if (options.heal_max_gap > 0.0) {
    struct Item {
      const Brep* src;
      Brep* dst;
      BooleanOperandReport* rpt;
      const char* name;
    };
    for (const Item& it : {Item{&a_in, &a, &rep.a, "a"}, Item{&b_in, &b, &rep.b, "b"}}) {
      if (it.rpt->naked_edges == 0 && it.rpt->non_manifold_edges == 0) continue;
      PlanarHealResult h = HealPlanarOperandGaps(*it.src, options.heal_max_gap);
      if (!h.ok) {
        rep.reason = h.reason == BooleanFailureReason::Unclassified ? BooleanFailureReason::OpenOperand : h.reason;
        rep.message = std::string("operand ") + it.name + ": " + h.message;
        rep.function_name = "HealPlanarOperandGaps";
        return rep;
      }
      *it.dst = h.healed;
      it.rpt->healed = !h.already_closed;
      it.rpt->healed_gap = h.gap_bound_used;
      it.rpt->max_vertex_move = h.max_vertex_move;
    }
  }

  // An operand with naked edges is reported but not pre-refused: some engines
  // accept shells their own preconditions allow. If the engine then fails or
  // returns an open body, the operand lists explain why.
  try {
    switch (engine) {
      case BooleanEngineKind::Planar:
        rep.result = BooleanCombinePlanar(a, b, op, options.tolerance, options.allow_non_manifold_result);
        break;
      case BooleanEngineKind::Mixed:
        rep.result = BooleanCombineMixed(a, b, op, options.tolerance, options.allow_non_manifold_result);
        break;
      case BooleanEngineKind::General:
        rep.result = BooleanCombineGeneral(a, b, op, options.tolerance > 0.0 ? options.tolerance : 0.001,
                                           options.allow_non_manifold_result);
        break;
    }
    rep.has_result = true;
  } catch (const BooleanOperationError& e) {
    rep.reason = e.reason();
    rep.function_name = e.function_name();
    rep.message = e.what();
    rep.typed_at_source = true;
    return rep;
  } catch (const std::exception& e) {
    rep.message = e.what();
    rep.reason = ClassifyBooleanFailureMessage(rep.message);
    // An untyped refusal on an operand that is not even a closed shell is
    // most usefully reported as that.
    if (rep.reason == BooleanFailureReason::Unclassified && (rep.a.naked_edges > 0 || rep.b.naked_edges > 0))
      rep.reason = BooleanFailureReason::OpenOperand;
    return rep;
  } catch (...) {
    rep.message = "unknown (non-std) exception";
    rep.reason = BooleanFailureReason::Unclassified;
    return rep;
  }

  rep.empty_result = rep.result.FaceCount() == 0;
  if (!rep.empty_result) {
    rep.result_open_edges = ReportOpenEdges(rep.result);
    int naked = 0, nonman = 0;
    for (const BooleanEdgeReport& e : rep.result_open_edges) (e.trim_count >= 3 ? nonman : naked)++;
    if (naked > 0) {
      rep.reason = BooleanFailureReason::OpenResult;
      rep.message = "the engine returned a body with " + std::to_string(naked) + " naked edge(s); it is not a closed shell";
      return rep;
    }
    if (nonman > 0 && !options.allow_non_manifold_result) {
      rep.reason = BooleanFailureReason::NonManifoldResult;
      rep.message = "the engine returned a body with " + std::to_string(nonman) + " non-manifold edge(s)";
      return rep;
    }
  }
  rep.ok = true;
  return rep;
}

MeshBooleanReport DiagnoseMeshBoolean(const Mesh& a, const Mesh& b, BooleanOp op) {
  MeshBooleanReport rep;
  rep.op = op;
  CountMeshOpenEdges(a, rep.open_edges_a, rep.non_manifold_edges_a);
  CountMeshOpenEdges(b, rep.open_edges_b, rep.non_manifold_edges_b);
  try {
    rep.result = BooleanCombine(a, b, op);
    rep.has_result = true;
    rep.manifold_status = "NoError";
  } catch (const std::exception& e) {
    rep.message = e.what();
    rep.reason = ClassifyBooleanFailureMessage(rep.message);
    const std::string key = "[result status: ";
    const size_t at = rep.message.find(key);
    if (at != std::string::npos) {
      const size_t from = at + key.size();
      const size_t to = rep.message.find(';', from);
      rep.manifold_status = rep.message.substr(from, to == std::string::npos ? std::string::npos : to - from);
    }
    if (rep.reason == BooleanFailureReason::EngineFailure && (rep.open_edges_a > 0 || rep.open_edges_b > 0))
      rep.reason = BooleanFailureReason::OpenOperand;
    return rep;
  }
  CountMeshOpenEdges(rep.result, rep.open_edges_result, rep.non_manifold_edges_result);
  if (rep.open_edges_result > 0) {
    rep.reason = BooleanFailureReason::OpenResult;
    rep.message = "the mesh engine returned a body with " + std::to_string(rep.open_edges_result) + " open edge(s)";
    return rep;
  }
  rep.ok = true;
  return rep;
}

}  // namespace dino8::kernel
