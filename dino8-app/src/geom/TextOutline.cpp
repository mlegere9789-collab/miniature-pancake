#include "geom/TextOutline.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <cstdlib>
#include <filesystem>
#include <memory>

namespace dino8::app {

std::vector<std::string> CandidateFontFiles() {
  std::vector<std::string> v;
#if defined(_WIN32)
  std::string windir = std::getenv("WINDIR") ? std::getenv("WINDIR") : "C:\\Windows";
  for (const char* f : {"arial.ttf", "segoeui.ttf", "calibri.ttf", "verdana.ttf", "tahoma.ttf"}) v.push_back(windir + "\\Fonts\\" + f);
#elif defined(__APPLE__)
  for (const char* f : {"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc",
                        "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Supplemental/Verdana.ttf"}) v.push_back(f);
#else
  for (const char* f : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                        "/usr/share/fonts/truetype/freefont/FreeSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                        "/usr/share/fonts/noto/NotoSans-Regular.ttf"}) v.push_back(f);
#endif
  return v;
}

namespace {

struct Decomposer {
  const ON_Plane* plane;
  double scale;
  double x_offset;
  ON_PolyCurve* current = nullptr;
  ON_3dPoint start, last;
  std::vector<kernel::NurbsCurve>* out;

  ON_3dPoint P(const FT_Vector* v) const { return plane->PointAt((v->x * scale) + x_offset, v->y * scale); }
  void Close() {
    if (!current) return;
    if (current->Count() > 0) {
      if (last.DistanceTo(start) > 1e-9) current->Append(new ON_LineCurve(last, start));
      ON_NurbsCurve nc;
      if (current->GetNurbForm(nc) > 0) { kernel::NurbsCurve k; k.raw() = nc; out->push_back(k); }
    }
    delete current;
    current = nullptr;
  }
  static int MoveTo(const FT_Vector* to, void* user) {
    Decomposer* d = static_cast<Decomposer*>(user);
    d->Close();
    d->current = new ON_PolyCurve();
    d->start = d->last = d->P(to);
    return 0;
  }
  static int LineTo(const FT_Vector* to, void* user) {
    Decomposer* d = static_cast<Decomposer*>(user);
    ON_3dPoint p = d->P(to);
    if (p.DistanceTo(d->last) > 1e-9) d->current->Append(new ON_LineCurve(d->last, p));
    d->last = p;
    return 0;
  }
  static int ConicTo(const FT_Vector* c, const FT_Vector* to, void* user) {
    Decomposer* d = static_cast<Decomposer*>(user);
    ON_BezierCurve bez(3, false, 3);
    bez.SetCV(0, d->last); bez.SetCV(1, d->P(c)); bez.SetCV(2, d->P(to));
    ON_NurbsCurve* nc = new ON_NurbsCurve();
    bez.GetNurbForm(*nc);
    d->current->Append(nc);
    d->last = d->P(to);
    return 0;
  }
  static int CubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) {
    Decomposer* d = static_cast<Decomposer*>(user);
    ON_BezierCurve bez(3, false, 4);
    bez.SetCV(0, d->last); bez.SetCV(1, d->P(c1)); bez.SetCV(2, d->P(c2)); bez.SetCV(3, d->P(to));
    ON_NurbsCurve* nc = new ON_NurbsCurve();
    bez.GetNurbForm(*nc);
    d->current->Append(nc);
    d->last = d->P(to);
    return 0;
  }
};

struct FtLibrary {
  FT_Library lib = nullptr;
  FT_Face face = nullptr;
  std::string file;
  ~FtLibrary() { if (face) FT_Done_Face(face); if (lib) FT_Done_FreeType(lib); }
};

FtLibrary& Library() {
  static FtLibrary ft;
  if (!ft.lib) {
    if (FT_Init_FreeType(&ft.lib)) { ft.lib = nullptr; return ft; }
    for (const std::string& f : CandidateFontFiles()) {
      std::error_code ec;
      if (!std::filesystem::exists(f, ec)) continue;
      if (FT_New_Face(ft.lib, f.c_str(), 0, &ft.face) == 0) { ft.file = f; break; }
      ft.face = nullptr;
    }
  }
  return ft;
}

}  // namespace

bool TextToCurves(const std::string& text, double height, const ON_Plane& plane,
                  std::vector<kernel::NurbsCurve>& out, std::string& font_used, double* advance_width) {
  FtLibrary& ft = Library();
  if (!ft.face) { font_used.clear(); return false; }
  font_used = ft.file;
  FT_Face face = ft.face;
  // Capital height in font units: use the 'H' glyph's bbox, else 0.7 em.
  double cap_units = face->units_per_EM * 0.7;
  if (FT_Load_Char(face, 'H', FT_LOAD_NO_BITMAP | FT_LOAD_NO_SCALE) == 0) {
    FT_BBox bb;
    FT_Outline_Get_CBox(&face->glyph->outline, &bb);
    if (bb.yMax > 0) cap_units = static_cast<double>(bb.yMax);
  }
  const double scale = height / cap_units;
  Decomposer d;
  d.plane = &plane;
  d.scale = scale;
  d.x_offset = 0;
  d.out = &out;
  FT_Outline_Funcs funcs = {Decomposer::MoveTo, Decomposer::LineTo, Decomposer::ConicTo, Decomposer::CubicTo, 0, 0};
  for (unsigned char ch : text) {
    if (ch == ' ') { d.x_offset += face->units_per_EM * 0.3 * scale; continue; }
    if (FT_Load_Char(face, ch, FT_LOAD_NO_BITMAP | FT_LOAD_NO_SCALE)) continue;
    FT_Outline_Decompose(&face->glyph->outline, &funcs, &d);
    d.Close();
    d.x_offset += face->glyph->advance.x * scale;
  }
  if (advance_width) *advance_width = d.x_offset;
  return !out.empty();
}

}  // namespace dino8::app
