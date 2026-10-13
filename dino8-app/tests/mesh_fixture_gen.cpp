// mesh_fixture_gen - writes a small, real .3dm file containing a single
// mesh, built directly through OpenNURBS' own ONX_Model/ON_Mesh API (the
// same API src/io/File3dm.cpp's Save3dm uses) rather than through Dino 8's
// own Open/Save round trip.
//
// Regression coverage: OpenNURBS' ON_Mesh::Read() (ON_Mesh::ReadFaceArray()
// under it) copies each face's vertex indices straight off the disk with
// no check against the vertex count it read a moment earlier, and never
// calls ON_Mesh::IsValid(). File3dm.cpp's Load3dm() used to trust that: a
// mesh whose face indexes past its own vertex array loaded into the
// document verbatim, after which every mesh query (Area, tessellation,
// booleans...) read memory past m_V - silently, no diagnostic (see
// MeshFaceIndicesInRange's own comment in File3dm.cpp, and the kernel's
// matching fix in dino8-kernel/src/file_io.cpp, commit f6fee28). Dino 8
// itself has no command that can construct an out-of-range mesh face (the
// app's own mesh-building code never emits one), so this generator - an
// independent path, not Dino 8's own exporter - exists purely to give the
// Load3dm range check a real corrupt-on-disk mesh to prove itself against.
//
// Usage: mesh_fixture_gen <output.3dm> [good|bad]
//   bad  (default): 3 vertices, one in-range face, one face indexing
//        vertex 7 - which does not exist. Load3dm must skip this mesh
//        (and only this mesh) and report it as a corrupt mesh, not crash
//        or silently keep it.
//   good: the same 3 vertices, both faces in range (the second face is
//        degenerate - repeated indices - which is legitimate and must
//        still load; the check is a range check, not a full validity
//        check).
#include <opennurbs.h>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <output.3dm> [good|bad]\n", argv[0]);
    return 1;
  }
  const char* path = argv[1];
  const bool good = argc > 2 && std::strcmp(argv[2], "good") == 0;

  ON_Mesh mesh;
  mesh.m_V.Append(ON_3fPoint(0.0f, 0.0f, 0.0f));
  mesh.m_V.Append(ON_3fPoint(1.0f, 0.0f, 0.0f));
  mesh.m_V.Append(ON_3fPoint(0.0f, 1.0f, 0.0f));

  ON_MeshFace face_ok;
  face_ok.vi[0] = 0;
  face_ok.vi[1] = 1;
  face_ok.vi[2] = 2;
  face_ok.vi[3] = 2;  // triangle: last index repeats the third, as ON_Mesh expects
  mesh.m_F.Append(face_ok);

  ON_MeshFace face_second;
  if (good) {
    // Degenerate but in-range: legitimate output from some exporters, and
    // must still load (the range check is not the stricter
    // ON_MeshFace::IsValid() no-repeated-index rule).
    face_second.vi[0] = 0;
    face_second.vi[1] = 0;
    face_second.vi[2] = 1;
    face_second.vi[3] = 1;
  } else {
    // Out of range: vertex 7 does not exist (only 0..2 do).
    face_second.vi[0] = 0;
    face_second.vi[1] = 1;
    face_second.vi[2] = 7;
    face_second.vi[3] = 7;
  }
  mesh.m_F.Append(face_second);

  ONX_Model model;
  model.m_sStartSectionComments = "mesh_fixture_gen test fixture";
  ON_3dmObjectAttributes attr;
  model.AddModelGeometryComponent(new ON_Mesh(mesh), &attr);

  ON_TextLog log;
  if (!model.Write(path, 0, &log)) {
    std::fprintf(stderr, "mesh_fixture_gen: OpenNURBS could not write %s\n", path);
    return 1;
  }
  std::printf("wrote %s (%s mesh: %d vertices, %d faces)\n", path, good ? "good" : "bad",
              mesh.m_V.Count(), mesh.m_F.Count());
  return 0;
}
