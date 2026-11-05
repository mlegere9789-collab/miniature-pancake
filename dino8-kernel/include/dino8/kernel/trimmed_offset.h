// Offset of ONE TRIMMED (freeform or analytic) face as a B-rep, with the trim
// boundary following the offset surface - PARITY_MAP.md "kernel: Offsetting,
// shelling, thickening", "Trimmed-surface / polysurface offset" ("No trimmed
// curved-face B-rep offset"). Implemented in src/trimmed_offset.cpp.
#pragma once

#include <string>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// What OffsetTrimmedFace() measured on its own result.
struct TrimmedOffsetReport {
  // True when the offset SURFACE is the exact closed-form offset
  // (NurbsSurface::Offset()'s `OffsetInfo::exact`: plane, sphere, cylinder,
  // cone, torus); false for a tolerance-bounded freeform fit.
  bool surface_exact = false;
  // NurbsSurface::Offset()'s measured worst distance from the offset surface
  // to the exact parallel locus S(u,v) + d N(u,v) (<= tolerance_used).
  double surface_deviation = 0.0;
  // The tolerance applied to the surface fit AND to every rebuilt edge.
  double tolerance_used = 0.0;
  // Worst MEASURED distance between a rebuilt 3D edge curve and the offset
  // surface evaluated along that edge's own (unchanged) 2D trim, on 8 samples
  // per interpolation point (always <= tolerance_used, else the call refuses).
  double max_edge_deviation = 0.0;
  // Worst measured distance between a rebuilt 3D edge curve and the exact
  // locus S(c2(t)) + d N(c2(t)) of the ORIGINAL surface along the same trim:
  // the independent statement that the boundary follows the true offset
  // (<= surface_deviation + max_edge_deviation, up to evaluation noise).
  double max_edge_locus_deviation = 0.0;
  int edges_rebuilt = 0;
  int vertices_moved = 0;
  // Empty on success; on Result::Failed the reason (the output is untouched).
  std::string refusal_reason;
};

// Offsets the single face of `sheet` by `distance`, keeping its trim.
//
//  * `sheet` is a one-face body (a trimmed freeform patch such as
//    Brep::TrimmedPatchFace(), a Brep::FromSurface() sheet, or a
//    TrimmedPlanarFace()). Anything else throws std::invalid_argument.
//  * Sign: positive moves along the face's OUTWARD normal, i.e. the surface's
//    NormalAt() direction, reversed when the face itself is flipped
//    (ON_BrepFace::m_bRev).
//  * Surface: NurbsSurface::Offset() - the exact rational form for a
//    plane/sphere/cylinder/cone/torus, otherwise the tolerance-controlled
//    refit with its fold-through-curvature guard. The result keeps the
//    input's (u, v) domain and parametrization, so out(u,v) = S(u,v) +
//    distance * N(u,v) and every trim keeps its 2D curve unchanged: the
//    offset face covers exactly the image of the same (u, v) region.
//  * Boundary: each edge's 3D curve is rebuilt as a cubic through points of
//    the offset surface taken along that edge's own 2D trim (doubling the
//    point count until the measured deviation is within the tolerance); each
//    vertex moves to the offset surface at its trim end. Edge tolerances are
//    the measured deviations. A face with only polygon side-table trims (a
//    TrimmedPlanarFace()) has no edges: its polygons live in (u, v) and are
//    carried over verbatim.
//  * A one-face body with no loops (FromSurface()) is offset as a surface.
//  * distance == 0 returns an exact copy.
//
// Refused (Result::Failed, `out` untouched, `refusal_reason` set): the surface
// offset itself is refused (it would fold through a center of curvature, a
// radius reaches zero, ...); the offset changed the (u, v) domain; an edge is
// used by more than one trim of the face (a seam of a periodic surface); a
// trim curve has a tangent break strictly inside one edge (split such an edge
// at its corners first - a cubic cannot follow a kink); an edge cannot be
// fitted within the tolerance with 2048 points; the assembled body fails
// ON_Brep::IsValid(). Throws std::invalid_argument for a non-finite distance
// or tolerance and for a body that is not a single face.
//
// Not done (still open in PARITY_MAP): several faces meeting at shared edges
// (corner reconstruction between offset neighbours), and a global check that
// the offset region does not overlap itself between samples.
Result OffsetTrimmedFace(const Brep& sheet, double distance, Brep& out, double tolerance = -1.0,
                         TrimmedOffsetReport* report = nullptr);

}  // namespace dino8::kernel
