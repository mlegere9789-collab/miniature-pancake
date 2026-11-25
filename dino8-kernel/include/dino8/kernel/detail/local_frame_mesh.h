#pragma once

// Implementation plumbing shared by brep.cpp (which defines it) and sweep.cpp - not part of the public dino8::kernel API.
//
// Brep::TessellateToClosedMesh() but of a copy translated to the body's own bounding-box centre when the body is far (more than 4
// half diagonals) from the origin. An ON_Mesh stores float32 vertices (relative precision ~6e-8 of the COORDINATE, not of the body),
// so a thin or small body far from the origin loses its shape in the mesh: a 3 x 4 x 0.5e-3 sheet 1e5 from the origin has no
// thickness left, its mesh volume reads 0. Every "tessellate, and flip the faces if the signed volume is negative" outward check must
// therefore read this mesh, not the one at the body's own coordinates (the flip was skipped and an INWARD solid, Brep::Volume() < 0,
// was returned). The shift is rounded to a power-of-two grid so the translated control points are exact.
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"

namespace dino8::kernel::detail {

Mesh LocalFrameClosedMesh(const Brep& b, int u_divisions, int v_divisions);

}  // namespace dino8::kernel::detail
