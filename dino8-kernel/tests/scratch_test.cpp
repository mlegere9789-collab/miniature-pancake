#include <cstdio>
#include <fstream>
#include <string>
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

// Mesh::LoadObj()'s new g/o/usemtl/mtllib (per-face group/material name +
// resolved .mtl diffuse color) support, exercised standalone: the full
// dino8_kernel_tests binary aborts earlier (in the pre-existing, disclosed
// BooleanCombineGeneral "3+ fragment loops" limitation some early boolean
// fixtures trip) before it ever reaches TestMeshLoadObjGroupsAndMaterials,
// so this checks the same logic that test exercises, directly, in isolation.
int main() {
  ON::Begin();
  using namespace dino8::kernel;

  const std::string mtl_path = "/tmp/scratch_materials.mtl";
  {
    std::ofstream mtl(mtl_path);
    mtl << "newmtl Red\nKd 1.0 0.0 0.0\n";
    mtl << "newmtl Blue\nKd 0.0 0.0 1.0\n";
    mtl << "newmtl Green\nKd 0.0 1.0 0.0\n";
  }

  const std::string obj_path = "/tmp/scratch_groups_materials.obj";
  {
    std::ofstream out(obj_path);
    out << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";
    out << "f 1 2 3 4\n";
    out << "v 2 0 0\nv 3 0 0\nv 3 1 0\nv 2 1 0\n";
    out << "mtllib " << mtl_path << '\n';
    out << "g GroupA\nusemtl Red\n";
    out << "f 5 6 7 8\n";
    out << "v 4 0 0\nv 5 0 0\nv 5 1 0\nv 4 1 0\n";
    out << "o GroupB\nusemtl Blue\n";
    out << "f 9 10 11 12\n";
    out << "v 6 0 0\nv 8 0 0\nv 9 1 0\nv 7 2 0\nv 5 1 0\n";
    out << "g GroupC\nusemtl Green\n";
    out << "f 13 14 15 16 17\n";
    out << "v 10 0 0\nv 11 0 0\nv 11 1 0\nv 10 1 0\n";
    out << "usemtl Unknown\n";
    out << "f 18 19 20 21\n";
  }

  Mesh mesh;
  std::printf("LoadObj == Ok: %d\n", Mesh::LoadObj(obj_path, mesh) == Result::Ok);
  std::printf("FaceCount == 7: %d (got %d)\n", mesh.FaceCount() == 7, mesh.FaceCount());
  std::printf("HasFaceGroupNames: %d\n", mesh.HasFaceGroupNames());
  std::printf("HasFaceMaterialNames: %d\n", mesh.HasFaceMaterialNames());
  std::printf("HasFaceMaterialColors: %d\n", mesh.HasFaceMaterialColors());

  std::printf("face0 group empty: %d\n", mesh.FaceGroupNameAt(0).empty());
  std::printf("face0 material empty: %d\n", mesh.FaceMaterialNameAt(0).empty());

  std::printf("face1 group == GroupA: %d\n", mesh.FaceGroupNameAt(1) == "GroupA");
  std::printf("face1 material == Red: %d\n", mesh.FaceMaterialNameAt(1) == "Red");
  const Color red = mesh.FaceMaterialColorAt(1);
  std::printf("face1 color == red: %d (%d,%d,%d)\n", red.r == 255 && red.g == 0 && red.b == 0, red.r, red.g, red.b);

  std::printf("face2 group == GroupB: %d\n", mesh.FaceGroupNameAt(2) == "GroupB");
  std::printf("face2 material == Blue: %d\n", mesh.FaceMaterialNameAt(2) == "Blue");
  const Color blue = mesh.FaceMaterialColorAt(2);
  std::printf("face2 color == blue: %d (%d,%d,%d)\n", blue.r == 0 && blue.g == 0 && blue.b == 255, blue.r, blue.g,
              blue.b);

  bool all_pentagon_tagged = true;
  for (int i = 3; i <= 5; ++i) {
    if (mesh.FaceGroupNameAt(i) != "GroupC" || mesh.FaceMaterialNameAt(i) != "Green") all_pentagon_tagged = false;
  }
  std::printf("pentagon's 3 fan triangles all tagged GroupC/Green: %d\n", all_pentagon_tagged);
  const Color green = mesh.FaceMaterialColorAt(3);
  std::printf("pentagon triangle color == green: %d\n", green.r == 0 && green.g == 255 && green.b == 0);

  std::printf("face6 material == Unknown: %d\n", mesh.FaceMaterialNameAt(6) == "Unknown");
  const Color unresolved = mesh.FaceMaterialColorAt(6);
  std::printf("face6 color == default black: %d\n", unresolved.r == 0 && unresolved.g == 0 && unresolved.b == 0);

  const int oob = mesh.FaceCount() + 100;
  std::printf("out-of-range group/material both empty: %d\n",
              mesh.FaceGroupNameAt(oob).empty() && mesh.FaceMaterialNameAt(oob).empty());

  std::remove(obj_path.c_str());
  std::remove(mtl_path.c_str());

  // Missing mtllib file.
  const std::string no_mtl_path = "/tmp/scratch_missing_mtllib.obj";
  {
    std::ofstream out(no_mtl_path);
    out << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";
    out << "mtllib does_not_exist.mtl\n";
    out << "g SomeGroup\nusemtl SomeMaterial\n";
    out << "f 1 2 3 4\n";
  }
  Mesh no_mtl_mesh;
  std::printf("LoadObj with missing mtllib == Ok: %d\n", Mesh::LoadObj(no_mtl_path, no_mtl_mesh) == Result::Ok);
  std::printf("group still recorded: %d\n", no_mtl_mesh.HasFaceGroupNames() && no_mtl_mesh.FaceGroupNameAt(0) == "SomeGroup");
  std::printf("material name still recorded: %d\n",
              no_mtl_mesh.HasFaceMaterialNames() && no_mtl_mesh.FaceMaterialNameAt(0) == "SomeMaterial");
  std::printf("HasFaceMaterialColors false (mtllib not found): %d (want 0)\n", no_mtl_mesh.HasFaceMaterialColors());
  std::remove(no_mtl_path.c_str());

  // Plain file, no g/o/usemtl at all.
  const std::string plain_path = "/tmp/scratch_no_groups.obj";
  {
    std::ofstream out(plain_path);
    out << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";
    out << "f 1 2 3 4\n";
  }
  Mesh plain_mesh;
  std::printf("LoadObj plain == Ok: %d\n", Mesh::LoadObj(plain_path, plain_mesh) == Result::Ok);
  std::printf("plain HasFaceGroupNames false: %d (want 0)\n", plain_mesh.HasFaceGroupNames());
  std::printf("plain HasFaceMaterialNames false: %d (want 0)\n", plain_mesh.HasFaceMaterialNames());
  std::remove(plain_path.c_str());

  ON::End();
  return 0;
}
