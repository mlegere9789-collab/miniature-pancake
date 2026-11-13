#include "dino8/kernel/tessellation_refine.h"

namespace dino8::kernel {

MeshQuality MeasureMeshQuality(const Mesh&) { return MeshQuality{}; }

Mesh TessellateSurfaceRegionAdaptive(const NurbsSurface&, const RegionRefineOptions&, RegionRefineReport* report) {
  if (report != nullptr) *report = RegionRefineReport{};
  return Mesh{};
}

}  // namespace dino8::kernel
