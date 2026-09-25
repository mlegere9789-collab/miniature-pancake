// Thin re-export of dino8::kernel's general surface/curve intersector
// (dino8-kernel/include/dino8/kernel/surface_intersect.h) into the
// dino8::app namespace, so existing app callers (cmd_fillet.cpp,
// cmd_srfedit.cpp, BlendSurface.cpp, tests/test_fuzz_geometry.cpp) keep
// compiling unchanged. The real implementation now lives in the kernel
// (dino8-kernel/src/surface_intersect.cpp) - it was never app-specific
// to begin with, just app-only *code*, and the kernel's own general
// boolean work needs it too. Do not add logic here; add it to the
// kernel version instead.
#pragma once

#include "dino8/kernel/surface_intersect.h"

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

using kernel::IntersectOptions;
using kernel::SurfaceMesh;
using kernel::TessellateWithUV;

using kernel::IntersectionCurve;
using kernel::IntersectSurfaces;
using kernel::IntersectFaces;

using kernel::CurveSurfaceHit;
using kernel::IntersectCurveSurface;

using kernel::Residual;
using kernel::NewtonSolve;
using kernel::RefineSurfaceSurfacePoint;
using kernel::SurfaceClosestPoint;
using kernel::SurfaceClosestPointGlobal;
using kernel::CurveClosestParam;
using kernel::CurveClosestParamGlobal;
using kernel::InterpolateCubic;
using kernel::ChordParams;
using kernel::FaceContainsUV;
using kernel::LoopPolygon;
using kernel::PointInPolygon;

}  // namespace dino8::app
