#include "io/FileSat.h"

#include <opennurbs.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include "dino8/kernel/acis_sat.h"

namespace dino8::app {

namespace {

bool Mentions(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

bool AnyMentions(const kernel::AcisSatReadResult& r, const char* needle) {
  if (Mentions(r.error, needle)) return true;
  for (const std::string& w : r.warnings) if (Mentions(w, needle)) return true;
  return false;
}

std::string Num(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.6g", v);
  return buf;
}

std::string BodyLine(const kernel::Brep& b, const std::string& name, const kernel::BoundingBox& box) {
  std::ostringstream os;
  os << "  " << name << ": " << b.LiveFaceCount() << " faces, " << b.LiveEdgeCount() << " edges, " << b.LiveVertexCount() << " vertices, ";
  if (b.raw().IsSolid()) os << "closed solid";
  else os << "no closed shell reported by the topology check (open sheet, or a whole-sphere/torus face without boundary edges)";
  os << ", bbox " << Num(box.max.x - box.min.x) << " x " << Num(box.max.y - box.min.y) << " x " << Num(box.max.z - box.min.z);
  // Brep::Area/Volume integrate NURBS faces exactly and refuse other surface types: say so instead of printing half a number.
  std::string area = "not computed here (use Area)";
  try { area = Num(b.Area()); } catch (...) {}
  os << ", area " << area;
  if (b.raw().IsSolid()) {
    std::string vol = "not computed here (use Volume)";
    try { vol = Num(b.Volume()); } catch (...) {}
    os << ", volume " << vol;
  }
  return os.str();
}

// A whole-sphere / whole-torus body has one face and no edges. The kernel builds it as a bare NURBS face, which the
// application's mesher does not display or measure (bounding box, Volume and Area all came back 0 in the app), so it is
// rebuilt as the same native ON_BrepSphere / ON_BrepTorus the Sphere and Torus commands create. Anything else is untouched.
bool RebuildWholePrimitive(const kernel::Brep& in, kernel::Brep& out, const char*& what) {
  const ON_Brep& b = in.raw();
  if (b.m_F.Count() != 1 || b.m_E.Count() != 0) return false;
  const ON_Surface* srf = b.m_F[0].SurfaceOf();
  if (!srf) return false;
  ON_Sphere sph;
  ON_Torus tor;
  ON_Brep* made = nullptr;
  if (srf->IsSphere(&sph, 1e-4)) {
    made = ON_BrepSphere(sph);
    what = "sphere";
  } else if (srf->IsTorus(&tor, 1e-4)) {
    made = ON_BrepTorus(tor);
    what = "torus";
  }
  if (!made) return false;
  out.raw() = *made;
  delete made;
  return true;
}

const char* const kLayoutNote =
    "Format status: only planes, straight curves and the topology records (body/lump/shell/face/loop/coedge/edge/vertex/point) are "
    "documented in the public text this reader was written from (ezdxf 'ACIS Tools'); the header, record framing and several tokens "
    "are from recollection, and no real ACIS-written file has been used to test it (see dino8-kernel/docs/ACIS_SAT_NOTES.md).";

const char* const kSabNote =
    "SAB status: the binary token tags, header layout and two-valued-word encoding are UNVERIFIED (no public description of the byte layout "
    "was reachable; they are recollection plus first-principles choices). The reader and writer have only been tested against each other, "
    "never against a file from a real ACIS-based product (see dino8-kernel/docs/ACIS_SAT_NOTES.md).";

// True when the file starts with the SAB signature.
bool FileIsSab(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  char head[16] = {};
  f.read(head, 15);
  return f.gcount() == 15 && kernel::LooksLikeAcisSab(std::string(head, 15));
}

}  // namespace

bool ImportSat(Document& doc, const std::string& path, std::string& summary) {
  summary.clear();
  const std::string file = std::filesystem::path(path).filename().string();
  const bool binary = FileIsSab(path);
  const char* const fmt = binary ? "SAB" : "SAT";
  kernel::AcisSatOptions strict;
  kernel::AcisSatReadResult res = kernel::ReadAcis(path, strict);
  const size_t strict_bodies = res.bodies.size();
  bool used_unverified = false;
  if (AnyMentions(res, "UNVERIFIED")) {
    kernel::AcisSatOptions relaxed;
    relaxed.allow_unverified_layouts = true;
    kernel::AcisSatReadResult second = kernel::ReadAcis(path, relaxed);
    if (second.ok && second.bodies.size() > strict_bodies) {
      res = std::move(second);
      used_unverified = true;
    } else if (second.ok) {
      // The relaxed pass found nothing more: its refusals are the real reasons (the strict pass can only say the opt-in flag was off).
      res.warnings = std::move(second.warnings);
      res.warnings.push_back("the unverified-layout entities were tried as well and still did not convert");
    }
  }
  if (!res.ok) {
    summary = std::string(fmt) + " import refused (" + file + "): " + (res.error.empty() ? std::string("no body could be converted") : res.error);
    for (const std::string& w : res.warnings) if (w != res.error) summary += "\n  " + w;
    return false;
  }
  const std::string stem = std::filesystem::path(path).stem().string();
  std::ostringstream os;
  std::string rebuilt_notes;
  os << fmt << " import " << file << ": " << res.bodies.size() << " Brep body(ies) read";
  for (size_t i = 0; i < res.bodies.size(); ++i) {
    const std::string name = res.bodies.size() == 1 ? stem : stem + "_" + std::to_string(i + 1);
    kernel::Brep body = res.bodies[i];
    kernel::Brep rebuilt;
    const char* prim = "";
    const bool file_reversed = body.raw().m_F.Count() == 1 && body.raw().m_F[0].m_bRev;
    if (RebuildWholePrimitive(body, rebuilt, prim)) {
      body = rebuilt;
      if (file_reversed) {
        body.raw().Flip();
        rebuilt_notes += "\nNote: " + name + " is a REVERSED " + std::string(prim) + " face in the file (material outside the surface); that orientation was kept.";
      }
      rebuilt_notes += std::string("\nNote: ") + name + " is a whole " + prim + " face with no edges; it was rebuilt as the application's native " + prim +
                       " Brep (outward orientation) so it can be displayed and measured.";
    }
    SceneObject o = SceneObject::MakeBrep(body);
    o.name = name;
    os << "\n" << BodyLine(body, name, o.BoundingBox());
    doc.Add(std::move(o));
  }
  if (used_unverified) {
    os << "\nUNVERIFIED LAYOUT: " << (res.bodies.size() - strict_bodies) << " of " << res.bodies.size()
       << " bodies use cone/sphere/torus/ellipse entities whose field layouts are NOT stated in any public document used (taken from "
          "recollection); they were read on that basis and rejected unless the geometry fit its own edges. Check them against the source "
          "model before relying on them.";
  } else {
    os << "\nEvery entity read uses a layout that public documentation states (planes, straight curves, topology).";
  }
  os << rebuilt_notes;
  for (const std::string& w : res.warnings) os << "\nRefused/skipped: " << w;
  os << "\nCoordinates were taken as-is (scaled by the header's units value) and read as document units; no unit conversion is applied.";
  os << "\n" << kLayoutNote;
  if (binary) os << "\n" << kSabNote;
  summary = os.str();
  return true;
}

namespace {

bool ExportAcis(const Document& doc, const std::string& path, bool selected_only, std::string& error, bool binary) {
  const char* const fmt = binary ? "SAB" : "SAT";
  struct Cand { const SceneObject* obj; kernel::Brep brep; bool unverified; };
  std::vector<Cand> ok;
  std::vector<std::string> refused;
  int total = 0;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;
    ++total;
    const std::string nm = o.name.empty() ? std::string(ObjectKindName(o.kind)) : o.name;
    std::optional<kernel::Brep> body;
    if (o.kind == ObjectKind::Brep && o.brep) {
      body.emplace(*o.brep);
    } else if (o.kind == ObjectKind::Surface && o.surface) {
      try {
        body.emplace(kernel::Brep::FromSurface(*o.surface));
      } catch (const std::exception& e) {
        refused.push_back(nm + " (surface): " + e.what());
        continue;
      }
    } else {
      refused.push_back(nm + " (" + ObjectKindName(o.kind) + "): " + fmt + " export writes Brep bodies only; this object kind has no body representation here");
      continue;
    }
    // Try the documented-layout subset first, then (clearly flagged) the unverified one.
    bool done = false;
    for (int pass = 0; pass < 2 && !done; ++pass) {
      kernel::AcisSatOptions opt;
      opt.allow_unverified_layouts = pass == 1;
      std::ostringstream scratch;
      kernel::AcisSatWriteResult w;
      try {
        w = binary ? kernel::WriteAcisSab(scratch, {*body}, opt) : kernel::WriteAcisSat(scratch, {*body}, opt);
      } catch (const std::exception& e) {
        w.ok = false;
        w.error = e.what();
      }
      if (w.ok) {
        ok.push_back({&o, *body, pass == 1});
        done = true;
      } else if (pass == 1 || !Mentions(w.error, "UNVERIFIED")) {
        refused.push_back(nm + ": " + w.error);
        done = true;
      }
    }
  }
  if (total == 0) { error = "Nothing to export"; return false; }
  if (ok.empty()) {
    error = std::string(fmt) + " export wrote nothing (" + path + "). Refused:";
    for (const std::string& r : refused) error += "\n  " + r;
    return false;
  }
  std::vector<kernel::Brep> bodies;
  bool any_unverified = false;
  for (const Cand& c : ok) { bodies.push_back(c.brep); any_unverified = any_unverified || c.unverified; }
  kernel::AcisSatOptions opt;
  opt.allow_unverified_layouts = any_unverified;
  kernel::AcisSatWriteResult w;
  try {
    w = binary ? kernel::WriteAcisSab(path, bodies, opt) : kernel::WriteAcisSat(path, bodies, opt);
  } catch (const std::exception& e) {
    w.ok = false;
    w.error = e.what();
  }
  if (!w.ok) { error = std::string(fmt) + " export failed (" + path + "): " + w.error; return false; }
  std::ostringstream os;
  os << fmt << " export " << std::filesystem::path(path).filename().string() << (binary ? " (ACIS 7.0 binary, version 700): " : " (ACIS 7.0 text, version 700): ") << ok.size() << " of " << total << " object(s) written";
  for (const Cand& c : ok) {
    os << "\n" << BodyLine(c.brep, c.obj->name.empty() ? std::string(ObjectKindName(c.obj->kind)) : c.obj->name, c.obj->BoundingBox())
       << (c.unverified ? " [UNVERIFIED cone/sphere/torus/ellipse layout]" : " [documented layouts only]");
  }
  if (any_unverified) {
    os << "\nUNVERIFIED LAYOUT: the bodies marked above carry cone/sphere/torus/ellipse entities written from recollection of the format, "
          "not from a public specification, and never compared with a file from a real ACIS-based product; another CAD system may reject "
          "or misread them. Re-import into Dino 8 is the only check that has been run.";
  }
  for (const std::string& r : refused) os << "\nNot written: " << r;
  os << "\nCoordinates are written as-is with header units 1; the document's unit system (" << doc.Settings().unit_system << ") is not recorded in the file.";
  os << "\n" << kLayoutNote;
  if (binary) os << "\n" << kSabNote;
  error = os.str();
  return true;
}

}  // namespace

bool ExportSat(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  return ExportAcis(doc, path, selected_only, error, false);
}

bool ExportSab(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  return ExportAcis(doc, path, selected_only, error, true);
}

}  // namespace dino8::app
