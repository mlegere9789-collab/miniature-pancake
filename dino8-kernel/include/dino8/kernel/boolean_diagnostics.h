#pragma once

#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

// ---------------------------------------------------------------------------
// Boolean failure diagnostics and operand gap-healing
// ---------------------------------------------------------------------------
//
// Two things the Boolean engines did not have:
//
//  1. A structured failure REPORT. The engines themselves still refuse with
//     free text at ~300 throw sites (and BooleanOperationError at a few);
//     DiagnoseBrepBoolean()/DiagnoseMeshBoolean() run an engine, never throw,
//     and return a BooleanReport: ok/failed, a BooleanFailureReason for EVERY
//     failure (a typed one verbatim from BooleanOperationError, otherwise
//     classified from the message by ClassifyBooleanFailureMessage(), with
//     BooleanFailureReason::Unclassified plus the verbatim message when no
//     pattern matches), the refusing function's name, and real NAKED-EDGE
//     reporting: every operand's and the result's open / over-used edges with
//     the edge index, 3D location and length (Brep::Check()'s own
//     NakedEdge/NonManifoldEdge issues). A result that an engine returned but
//     that is not a closed shell is reported as OpenResult, never as ok.
//
//  2. Automatic GAP-HEALING of imprecise planar-faced operands
//     (HealPlanarOperandGaps(), and DiagnoseBrepBoolean() with
//     options.heal_max_gap > 0). An operand whose faces do not quite meet
//     (naked edges with sub-tolerance gaps, the "box whose top face floats
//     1e-4 high" shape) is repaired BEFORE the Boolean by clustering vertices
//     closer than the smallest bound that closes the shell and snapping them
//     to the cluster centroid - the caller supplies only an UPPER BOUND on
//     the gap it is willing to bridge, not the gap itself. The smallest bound
//     that yields a closed, valid solid is used and reported. A gap larger
//     than the bound, or a feature smaller than the bound (a face that would
//     collapse), is refused with the reason, not guessed.
//
// Honest scope: healing handles operands whose faces are ALL planar
// (Brep::PlanarFaces() succeeds); anything else is refused with
// BooleanFailureReason::UnsupportedGeometry. The classification of free-text
// refusals is pattern based and only as complete as the patterns: see
// ClassifyBooleanFailureMessage().

enum class BooleanEngineKind { Planar, Mixed, General };

// One open (or over-used) edge of a body.
struct BooleanEdgeReport {
  int edge_index = -1;
  int trim_count = 0;                    // 0 or 1 = naked; >= 3 = non-manifold
  Point3d location = Point3d(0, 0, 0);  // a point on / near the edge
  double length = 0.0;                   // chord length between the edge's end vertices
};

// Naked and non-manifold edges of `brep` (Brep::Check()'s NakedEdge and
// NonManifoldEdge issues, enriched with each edge's length).
std::vector<BooleanEdgeReport> ReportOpenEdges(const Brep& brep);

struct BooleanOperandReport {
  int face_count = 0;
  int naked_edges = 0;
  int non_manifold_edges = 0;
  std::vector<BooleanEdgeReport> open_edges;
  bool healed = false;           // gap-healing changed this operand
  double healed_gap = 0.0;       // the (smallest) bound that closed it
  double max_vertex_move = 0.0;  // largest distance any vertex was moved by healing
};

struct BooleanDiagnoseOptions {
  double tolerance = -1.0;                 // forwarded to the engine (negative = engine default)
  double heal_max_gap = 0.0;               // > 0: heal non-closed planar operands, bridging gaps up to this
  bool allow_non_manifold_result = false;  // Planar/Mixed engines only (forwarded)
};

struct BooleanReport {
  bool ok = false;
  BooleanEngineKind engine = BooleanEngineKind::Planar;
  BooleanOp op = BooleanOp::Union;
  // Meaningful only when !ok.
  BooleanFailureReason reason = BooleanFailureReason::Unclassified;
  std::string function_name;     // refusing function (typed source) or the engine entry point
  std::string message;           // the engine's own message, verbatim
  bool typed_at_source = false;  // true when the engine threw a BooleanOperationError itself
  bool has_result = false;       // the engine returned a body (even when !ok, e.g. OpenResult)
  bool empty_result = false;     // returned body has no faces (a legitimate empty Intersection)
  Brep result;
  BooleanOperandReport a, b;
  std::vector<BooleanEdgeReport> result_open_edges;
};

// Runs the chosen engine on (a, b) and reports. Never throws.
BooleanReport DiagnoseBrepBoolean(const Brep& a, const Brep& b, BooleanOp op, BooleanEngineKind engine,
                                  const BooleanDiagnoseOptions& options = BooleanDiagnoseOptions());

struct MeshBooleanReport {
  bool ok = false;
  BooleanOp op = BooleanOp::Union;
  BooleanFailureReason reason = BooleanFailureReason::Unclassified;
  std::string message;
  std::string manifold_status;  // "NoError", or the Manifold result-status name parsed from the message
  Mesh result;
  bool has_result = false;
  int open_edges_a = 0, open_edges_b = 0, open_edges_result = 0;  // edges used by 1 face
  int non_manifold_edges_a = 0, non_manifold_edges_b = 0, non_manifold_edges_result = 0;  // used by 3+
};

// Mesh-engine counterpart. Never throws. Open/non-manifold edges are counted
// on the exact-coordinate-welded mesh.
MeshBooleanReport DiagnoseMeshBoolean(const Mesh& a, const Mesh& b, BooleanOp op);

// Maps an engine's free-text refusal to a reason (Unclassified if no pattern
// matches). Pure function of the text.
BooleanFailureReason ClassifyBooleanFailureMessage(const std::string& message);

// Human-readable enum name ("OpenOperand", ...), for logs and tests.
const char* BooleanFailureReasonName(BooleanFailureReason reason);

struct PlanarHealResult {
  bool ok = false;              // `healed` is a closed, valid solid
  bool already_closed = false;  // no healing was needed; `healed` is the input
  Brep healed;
  double gap_bound_used = 0.0;  // smallest candidate bound that closed the shell
  double max_vertex_move = 0.0;
  int vertices_merged = 0;      // number of vertices removed by clustering
  BooleanFailureReason reason = BooleanFailureReason::Unclassified;  // when !ok
  std::string message;                                               // when !ok
  std::vector<BooleanEdgeReport> remaining_open_edges;               // when !ok
};

// Heals a planar-faced operand whose faces do not quite meet. Tries bounds
// max_gap/64, /16, /4, /1 in turn and keeps the first that gives
// IsSolid() && IsValid() && Check().is_closed. Never throws.
PlanarHealResult HealPlanarOperandGaps(const Brep& operand, double max_gap);

}  // namespace dino8::kernel
