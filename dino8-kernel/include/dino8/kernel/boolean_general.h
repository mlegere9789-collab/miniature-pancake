// General boundary-evaluation boolean engine.
//
// Unlike boolean.cpp's BooleanCombineMixed (which is hand-solved,
// closed-form geometry enumerated per surface-TYPE-pair: plane+plane,
// plane+cylinder, cylinder+cylinder, ...), BooleanCombineGeneral works on
// ANY pair of ON_Surface-based faces by actually running the general
// surface/surface intersector (dino8/kernel/surface_intersect.h) between
// every (bbox-overlapping) face pair, splitting each face's own trim loop
// along the resulting intersection curves, classifying each fragment
// in/out of the other solid by ray-casting (via the general curve/surface
// intersector, not hand-solved ray-vs-plane/ray-vs-cylinder formulas), and
// reassembling the kept fragments into a genuine ON_Brep (real
// ON_BrepVertex/ON_BrepEdge/ON_BrepLoop/ON_BrepTrim topology, coincident
// points welded into shared vertices/edges - the same identity mechanism
// Brep::FromMixedFaces() already uses, just generalized to an arbitrary
// ON_Surface instead of only a plane/cylinder/cone).
//
// Scope/limitations (see boolean_general.cpp's own top comment for the
// full disclosure): a face is expected to carry at most one "outer"
// intersection component per opposing face pair that either (a) closes
// entirely inside the face's own trim (a closed loop - becomes a hole in
// the untouched fragment plus a separate interior fragment), or (b) meets
// the face's own trim boundary at exactly two points (an open arc - splits
// that trim boundary into two fragments there). Faces are assumed genus-0
// (no pre-existing holes) two-shell solids. Multiple non-interacting
// chains on the same face are supported (each is spliced in turn); chains
// that cross EACH OTHER on the same face are not.
#pragma once

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

Brep BooleanCombineGeneral(const Brep& a, const Brep& b, BooleanOp op);

// A purely additive, opt-in sibling of Brep::TessellateToClosedMesh()/
// TessellateToClosedMeshConforming(), scoped ONLY to BooleanCombineGeneral's
// own results, that closes the mesh-watertightness gap this file's own
// top-of-file doc comment discloses: `result`'s own faces are tessellated
// via Brep::Tessellate(u_divisions, v_divisions) - the exact same shared,
// unmodified grid-clip tessellator BooleanCombineMixed also depends on, not
// touched by this function at all - then, before welding, every boundary
// edge of one face's own tessellation that another face's tessellation
// happens to have an extra, un-partnered vertex sitting exactly on (this
// engine's own dense straight-segment polyline edges - see this file's own
// top comment - get resampled at different densities by the two adjacent
// faces' independent (u, v) grids, since neither Tessellate() nor
// TessellateConforming() has ever matched a general trimmed face's own
// polyline boundary the way TessellateConforming()'s existing analytic-
// curve/plain-quad passes match theirs) is re-triangulated as a fan through
// that extra vertex, so the two sides' boundary vertex sets agree exactly
// before Mesh::MergeAndWeld() runs. Also drops any resulting zero-area
// (degenerate, repeated-vertex) triangle - a separate, pre-existing
// grid-clip artifact (near a surface's own singular point, e.g. a
// sphere's pole, but also - confirmed directly - at an ordinary planar
// trim corner where two cut boundaries meet) that otherwise leaves
// spurious zero-length "edges" behind. This drop runs BOTH before and
// after the T-junction stitching pass above, not only after: dropping a
// boundary-line sliver strands its two OTHER edges - which the sliver
// had "claimed" as internal, so the stitcher never offered them a
// cross-face partner - as fresh, unmatched boundary edges once the
// sliver disappears, unless it is gone before the stitcher ever sees it.
// tests/general_boolean_sweep.cpp's own 76-combination measurement
// (BooleanCombineGeneral over 19 primitive-pair cases x 4 ops, at
// u_divisions=32/v_divisions=128) went from 0/76 to 14/76 genuinely
// Mesh::IsClosedManifold() from this reordering alone - every
// axis-aligned-planar case (box+box, disjoint/touching/rotated box
// pairs) now closes at any division count tried (8x8 through 32x128).
// The other 62 all pair a curved face (cylinder/cone/sphere/torus)
// against another face along a curved or skew intersection: each side's
// own grid-clip tessellation approximates that shared curve with its own
// independently-sampled dense polyline (not shared sample points, unlike
// a straight box edge, where both sides' clip points genuinely coincide
// once the sliver above stops hiding them) - a materially larger, still
// explicitly open gap; see boolean_general.cpp's own top-of-file comment.
// See boolean_general.cpp's own implementation comments for the exact
// algorithm and TestBooleanCombineGeneralBoxBox/BoxCylinder/SphereBox
// (tests/test_basic.cpp) for the falsifiable Mesh::IsClosedManifold()
// claims this makes.
Mesh TessellateGeneralBooleanClosedMesh(const Brep& result, int u_divisions = 8, int v_divisions = 8);

}  // namespace dino8::kernel
