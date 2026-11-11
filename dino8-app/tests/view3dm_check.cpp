// view3dm_check - the named views of a .3dm file through OpenNURBS' own API, independent of Dino 8's Save3dm / Load3dm.
//
//   view3dm_check fixture OUT.3dm   writes a file the way Rhino would: a parallel named view "RhinoTop" (60 units tall, 80 wide, the
//                                   usual 4:3 screen port) and a perspective named view "RhinoPersp" with a 28 mm (35 mm-film)
//                                   lens. No Dino8.* strings - Dino 8 never wrote this file.
//   view3dm_check oldwriter OUT.3dm writes ONE parallel and ONE perspective named view by exactly the steps Save3dm used before it
//                                   wrote any frustum (projection, location, direction, up, target - nothing else), to show what
//                                   such a file holds. Evidence only; not used by the smoke section.
//   view3dm_check newwriter OUT.3dm  the same two views by the steps Save3dm takes now (screen port + frustum + 35 mm lens): a parallel
//                                   view of height 80 and a perspective view with a 35 mm lens. Evidence only.
//   view3dm_check dump FILE.3dm     prints, per named view, one line
//                                      view <i> name=<n> persp=<0|1> eye=(..) dir=(..) up=(..) target=(..) frustum_h=<t-b>
//                                      frustum_w=<r-l> lens=<35 mm-film lens length, 0 for a parallel view>
#include <opennurbs.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string Narrow(const ON_wString& s) {
  std::string out;
  const wchar_t* p = static_cast<const wchar_t*>(s);
  for (; p && *p; ++p) out.push_back(*p < 128 ? static_cast<char>(*p) : '?');
  return out;
}

// What Save3dm wrote before the fix: no frustum, no screen port, no lens.
void OldSteps(ON_3dmView& v, bool perspective, const ON_3dPoint& eye, const ON_3dPoint& target, const ON_3dVector& up) {
  v.m_vp.SetProjection(perspective ? ON::perspective_view : ON::parallel_view);
  v.m_vp.SetCameraLocation(eye);
  v.m_vp.SetCameraDirection(target - eye);
  v.m_vp.SetCameraUp(up);
  v.m_vp.SetTargetPoint(target);
}

int Write(ONX_Model& model, const char* path, const char* what) {
  ON_TextLog log;
  if (!model.Write(path, 0, &log)) {
    std::fprintf(stderr, "view3dm_check: OpenNURBS could not write %s\n", path);
    return 1;
  }
  std::printf("wrote %s (%s)\n", path, what);
  return 0;
}

int Fixture(const char* path) {
  ONX_Model model;
  model.m_sStartSectionComments = "view3dm_check fixture";
  {
    ON_3dmView v;
    v.m_name = L"RhinoTop";
    OldSteps(v, false, ON_3dPoint(5, 5, 100), ON_3dPoint(5, 5, 0), ON_3dVector(0, 1, 0));
    v.m_vp.SetScreenPort(0, 800, 600, 0, 0, 1);
    v.m_vp.SetFrustum(-40.0, 40.0, -30.0, 30.0, 0.1, 5000.0);
    model.m_settings.m_named_views.Append(v);
  }
  {
    ON_3dmView v;
    v.m_name = L"RhinoPersp";
    OldSteps(v, true, ON_3dPoint(50, -50, 40), ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1));
    v.m_vp.SetScreenPort(0, 800, 600, 0, 0, 1);
    v.m_vp.SetFrustum(-0.5, 0.5, -0.375, 0.375, 1.0, 5000.0);
    v.m_vp.SetCamera35mmLensLength(28.0);
    model.m_settings.m_named_views.Append(v);
  }
  return Write(model, path, "named views RhinoTop (parallel, 60 tall) and RhinoPersp (28 mm lens)");
}

int OldWriter(const char* path) {
  ONX_Model model;
  {
    ON_3dmView v;
    v.m_name = L"OldTop";
    OldSteps(v, false, ON_3dPoint(0, 0, 100), ON_3dPoint(0, 0, 0), ON_3dVector(0, 1, 0));
    model.m_settings.m_named_views.Append(v);
  }
  {
    ON_3dmView v;
    v.m_name = L"OldPersp";
    OldSteps(v, true, ON_3dPoint(50, -50, 40), ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1));
    model.m_settings.m_named_views.Append(v);
  }
  return Write(model, path, "named views by the pre-fix Save3dm steps");
}

// The steps Save3dm takes now, for one parallel view of height 80 and one perspective view with a 35 mm lens.
int NewWriter(const char* path) {
  ONX_Model model;
  auto add = [&](const wchar_t* name, bool perspective, double ortho_height, double lens_mm) {
    ON_3dmView v;
    v.m_name = name;
    const ON_3dPoint eye = perspective ? ON_3dPoint(50, -50, 40) : ON_3dPoint(0, 0, 100);
    OldSteps(v, perspective, eye, ON_3dPoint(0, 0, 0), perspective ? ON_3dVector(0, 0, 1) : ON_3dVector(0, 1, 0));
    const double far_dist = std::max(1.0e4, 10.0 * (eye - ON_3dPoint(0, 0, 0)).Length());
    v.m_vp.SetScreenPort(0, 800, 600, 0, 0, 1);
    if (!perspective) {
      const double w = ortho_height * 4.0 / 3.0;
      v.m_vp.SetFrustum(-0.5 * w, 0.5 * w, -0.5 * ortho_height, 0.5 * ortho_height, 0.1, far_dist);
    } else {
      v.m_vp.SetFrustum(-0.5, 0.5, -0.375, 0.375, 1.0, far_dist);
      v.m_vp.SetCamera35mmLensLength(lens_mm);
    }
    model.m_settings.m_named_views.Append(v);
  };
  add(L"NewTop", false, 80.0, 50.0);
  add(L"NewPersp", true, 100.0, 35.0);
  return Write(model, path, "named views by the fixed Save3dm steps: parallel height 80, perspective lens 35");
}

int Dump(const char* path) {
  ONX_Model model;
  ON_TextLog log;
  if (!model.Read(path, &log)) {
    std::fprintf(stderr, "view3dm_check: OpenNURBS could not read %s\n", path);
    return 1;
  }
  std::printf("views: %d\n", model.m_settings.m_named_views.Count());
  for (int i = 0; i < model.m_settings.m_named_views.Count(); ++i) {
    const ON_3dmView& v = model.m_settings.m_named_views[i];
    const ON_3dPoint e = v.m_vp.CameraLocation(), t = v.m_vp.TargetPoint();
    const ON_3dVector d = v.m_vp.CameraDirection(), u = v.m_vp.CameraUp();
    double l = 0, r = 0, b = 0, tp = 0;
    v.m_vp.GetFrustum(&l, &r, &b, &tp);
    double lens = 0.0;
    if (v.m_vp.IsPerspectiveProjection()) v.m_vp.GetCamera35mmLensLength(&lens);
    std::printf("view %d name=%s persp=%d eye=(%.6g,%.6g,%.6g) dir=(%.6g,%.6g,%.6g) up=(%.6g,%.6g,%.6g) target=(%.6g,%.6g,%.6g) frustum_h=%.6g frustum_w=%.6g lens=%.6g\n",
                i, Narrow(v.m_name).c_str(), v.m_vp.IsPerspectiveProjection() ? 1 : 0, e.x, e.y, e.z, d.x, d.y, d.z, u.x, u.y, u.z, t.x, t.y, t.z, tp - b, r - l, lens);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s fixture|oldwriter|dump <file.3dm>\n", argv[0]);
    return 1;
  }
  if (!std::strcmp(argv[1], "fixture")) return Fixture(argv[2]);
  if (!std::strcmp(argv[1], "oldwriter")) return OldWriter(argv[2]);
  if (!std::strcmp(argv[1], "newwriter")) return NewWriter(argv[2]);
  if (!std::strcmp(argv[1], "dump")) return Dump(argv[2]);
  std::fprintf(stderr, "view3dm_check: unknown mode %s\n", argv[1]);
  return 1;
}
