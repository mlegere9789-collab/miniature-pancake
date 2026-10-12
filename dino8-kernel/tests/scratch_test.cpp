#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

// Mesh::SetVertexAlpha/HasVertexAlpha/VertexAlphaAt (+ SavePly/LoadPly's
// own new alpha property, SaveOff/LoadOff's own COFF `a` column), exercised
// standalone: the full dino8_kernel_tests binary aborts earlier (in the
// pre-existing, disclosed BooleanCombineGeneral "3+ fragment loops"
// limitation some early boolean fixtures trip) before it ever reaches
// TestMeshVertexAlphaRoundTrips, so this checks the same logic that test
// exercises, directly, in isolation.
int main() {
  ON::Begin();
  using namespace dino8::kernel;

  auto make_box = []() {
    // A unit box's worth of vertices is enough to exercise this; exact
    // positions don't matter for an alpha/color test.
    std::vector<Point3d> v = {
        Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(1, 1, 0), Point3d(0, 1, 0),
        Point3d(0, 0, 1), Point3d(1, 0, 1), Point3d(1, 1, 1), Point3d(0, 1, 1),
    };
    Mesh m;
    ON_Mesh& raw = m.raw();
    for (const Point3d& p : v) raw.m_V.Append(ON_3fPoint(p.x, p.y, p.z));
    ON_MeshFace f0, f1;
    f0.vi[0] = 0; f0.vi[1] = 1; f0.vi[2] = 2; f0.vi[3] = 3;
    f1.vi[0] = 4; f1.vi[1] = 5; f1.vi[2] = 6; f1.vi[3] = 7;
    raw.m_F.Append(f0);
    raw.m_F.Append(f1);
    return m;
  };

  Mesh no_colors = make_box();
  std::vector<unsigned char> alpha_try(static_cast<size_t>(no_colors.VertexCount()), 128);
  std::printf("SetVertexAlpha with no colors == Failed: %d\n",
              no_colors.SetVertexAlpha(alpha_try) == Result::Failed);
  std::printf("HasVertexAlpha after rejected call: %d (want 0)\n", no_colors.HasVertexAlpha());

  std::vector<Color> colors;
  std::vector<unsigned char> alpha;
  for (int i = 0; i < no_colors.VertexCount(); ++i) {
    colors.push_back(Color{static_cast<unsigned char>((i * 37) % 256), static_cast<unsigned char>((i * 91) % 256),
                            static_cast<unsigned char>((i * 173) % 256)});
    alpha.push_back(static_cast<unsigned char>((i * 29 + 10) % 256));
  }

  Mesh with_colors = make_box();
  std::printf("SetVertexColors == Ok: %d\n", with_colors.SetVertexColors(colors) == Result::Ok);

  std::vector<unsigned char> wrong_size(alpha.size() - 1, 200);
  std::printf("SetVertexAlpha wrong size == Failed: %d\n", with_colors.SetVertexAlpha(wrong_size) == Result::Failed);
  std::printf("HasVertexAlpha after wrong-size rejected call: %d (want 0)\n", with_colors.HasVertexAlpha());
  std::printf("VertexAlphaAt default before SetVertexAlpha: %d (want 255)\n",
              static_cast<int>(with_colors.VertexAlphaAt(0)));

  std::printf("SetVertexAlpha correct size == Ok: %d\n", with_colors.SetVertexAlpha(alpha) == Result::Ok);
  std::printf("HasVertexAlpha after real SetVertexAlpha: %d (want 1)\n", with_colors.HasVertexAlpha());
  bool alpha_matches = true;
  for (int i = 0; i < with_colors.VertexCount(); ++i) {
    if (with_colors.VertexAlphaAt(i) != alpha[static_cast<size_t>(i)]) alpha_matches = false;
  }
  std::printf("every VertexAlphaAt() matches what was set: %d\n", alpha_matches);

  // PLY ASCII round trip.
  {
    const std::string path = "/tmp/scratch_alpha_ascii.ply";
    std::printf("SavePly(ascii) == Ok: %d\n", with_colors.SavePly(path) == Result::Ok);
    std::ifstream in(path);
    std::string line;
    bool saw_alpha = false;
    while (std::getline(in, line)) {
      if (line == "end_header") break;
      if (line == "property uchar alpha") saw_alpha = true;
    }
    std::printf("header declares 'property uchar alpha': %d\n", saw_alpha);
    Mesh reloaded;
    std::printf("LoadPly(ascii) == Ok: %d\n", Mesh::LoadPly(path, reloaded) == Result::Ok);
    std::printf("reloaded HasVertexAlpha: %d\n", reloaded.HasVertexAlpha());
    bool reloaded_matches = true;
    for (int i = 0; i < reloaded.VertexCount(); ++i) {
      if (reloaded.VertexAlphaAt(i) != alpha[static_cast<size_t>(i)]) reloaded_matches = false;
    }
    std::printf("reloaded ascii alpha matches exactly: %d\n", reloaded_matches);
    std::remove(path.c_str());
  }

  // PLY binary round trip.
  {
    const std::string path = "/tmp/scratch_alpha_binary.ply";
    std::printf("SavePly(binary) == Ok: %d\n", with_colors.SavePly(path, /*binary=*/true) == Result::Ok);
    Mesh reloaded;
    std::printf("LoadPly(binary) == Ok: %d\n", Mesh::LoadPly(path, reloaded) == Result::Ok);
    std::printf("reloaded binary HasVertexAlpha: %d\n", reloaded.HasVertexAlpha());
    bool reloaded_matches = true;
    for (int i = 0; i < reloaded.VertexCount(); ++i) {
      if (reloaded.VertexAlphaAt(i) != alpha[static_cast<size_t>(i)]) reloaded_matches = false;
    }
    std::printf("reloaded binary alpha matches exactly: %d\n", reloaded_matches);
    std::remove(path.c_str());
  }

  // Colors-but-no-alpha mesh still writes the plain 3-channel form.
  {
    Mesh colors_only = make_box();
    colors_only.SetVertexColors(colors);
    const std::string path = "/tmp/scratch_no_alpha.ply";
    std::printf("SavePly(colors only) == Ok: %d\n", colors_only.SavePly(path) == Result::Ok);
    std::ifstream in(path);
    std::string line;
    bool saw_alpha = false;
    while (std::getline(in, line)) {
      if (line == "end_header") break;
      if (line == "property uchar alpha") saw_alpha = true;
    }
    std::printf("colors-only header declares NO alpha property: %d (want 0)\n", saw_alpha);
    std::remove(path.c_str());
  }

  // .off/.coff round trip.
  {
    const std::string path = "/tmp/scratch_alpha.off";
    std::printf("SaveOff == Ok: %d\n", with_colors.SaveOff(path) == Result::Ok);
    std::ifstream in(path);
    std::string header_line;
    std::getline(in, header_line);
    std::getline(in, header_line);  // counts line
    std::string first_vertex_line;
    std::getline(in, first_vertex_line);
    std::istringstream vline(first_vertex_line);
    double x = 0, y = 0, z = 0;
    int r = -1, g = -1, b = -1, a = -1;
    vline >> x >> y >> z >> r >> g >> b >> a;
    std::printf("first COFF vertex alpha column == real value: %d (got %d, want %d)\n",
                a == static_cast<int>(alpha[0]), a, static_cast<int>(alpha[0]));
    in.close();

    Mesh reloaded;
    std::printf("LoadOff == Ok: %d\n", Mesh::LoadOff(path, reloaded) == Result::Ok);
    std::printf("reloaded COFF HasVertexAlpha: %d\n", reloaded.HasVertexAlpha());
    bool coff_matches = true;
    for (int i = 0; i < reloaded.VertexCount(); ++i) {
      if (reloaded.VertexAlphaAt(i) != alpha[static_cast<size_t>(i)]) coff_matches = false;
    }
    std::printf("reloaded COFF alpha matches exactly: %d\n", coff_matches);
    std::remove(path.c_str());
  }

  ON::End();
  return 0;
}
