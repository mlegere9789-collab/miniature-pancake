// The implementation moved to dino8-kernel/src/surface_intersect.cpp
// (dino8::kernel::IntersectSurfaces/IntersectFaces/IntersectCurveSurface
// and friends). geom/SurfaceIntersect.h re-exports those into
// dino8::app so this translation unit has nothing left to define - it
// stays around (rather than being deleted) only so existing CMakeLists
// source lists that name it don't need editing everywhere it is listed.
#include "geom/SurfaceIntersect.h"
