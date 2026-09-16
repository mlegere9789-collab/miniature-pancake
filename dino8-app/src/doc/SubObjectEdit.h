// Geometry behind sub-object editing: where control points / vertices /
// edges / faces of a SceneObject are, how to move or delete them, and the
// mesh / SubD topology walks the Sel* commands need (edge loops, rings,
// face loops, connected parts, naked edges).
#pragma once

#include <string>
#include <vector>

#include "doc/SubObject.h"

namespace dino8::app {

// ---- control points / vertices ---------------------------------------------
// Number of editable points: curve CVs, surface CVs (i * CVCountV + j),
// mesh vertices, SubD control-net vertices. 0 for points and breps.
int ControlPointCount(const SceneObject& o);
bool ControlPointPosition(const SceneObject& o, int index, kernel::Point3d& out);
// Moves one point (invalidates the display). SubDs move the control net
// vertex and clear the evaluation cache.
bool SetControlPointPosition(SceneObject& o, int index, kernel::Point3d p);
// Surface grid dimensions (false for anything but a surface).
bool SurfaceGrid(const SceneObject& o, int& count_u, int& count_v);

// ---- sub-object geometry ---------------------------------------------------
// Vertex indices a mesh / SubD edge or face spans (a vertex ref gives
// itself). Empty for brep / surface faces and brep edges.
std::vector<int> SubObjectVertexIndices(const SceneObject& o, const SubObjectRef& r);
// Representative points (centroid, window tests): the vertices for mesh /
// SubD parts, sampled edge curves and face boundaries for breps.
std::vector<kernel::Point3d> SubObjectPoints(const SceneObject& o, const SubObjectRef& r);
// Display geometry for highlighting: points (x,y,z), line segments
// (x,y,z pairs) and triangles (x,y,z,nx,ny,nz per vertex).
void AppendSubObjectDisplay(const SceneObject& o, const SubObjectRef& r, std::vector<float>& points,
                            std::vector<float>& lines, std::vector<float>& triangles);
// Polyline along a brep edge (n samples).
std::vector<kernel::Point3d> BrepEdgePolyline(const ON_Brep& b, int edge_index, int samples = 24);

// ---- editing ---------------------------------------------------------------
// Transforms the selected sub-objects of `o` (all of them must belong to
// `o`). Vertices move directly; mesh / SubD edges and faces move their
// vertices (each once); brep faces and edges use MoveBrepFaces / MoveBrepEdges;
// a surface face moves every CV. Returns false when nothing moved.
bool TransformSubObjects(SceneObject& o, const std::vector<SubObjectRef>& refs, const ON_Xform& xf);
// Deletes sub-objects: curve CVs (while the curve keeps at least `order`
// CVs), brep faces, mesh faces / vertices, SubD faces. `remove_object`
// becomes true when nothing is left. Returns false with a message when the
// deletion is not possible.
bool DeleteSubObjects(SceneObject& o, const std::vector<SubObjectRef>& refs, bool& remove_object, std::string& message);

// Approximate MoveFace / MoveEdge on a brep: the listed faces move rigidly
// with their edges; neighbouring untrimmed faces follow by moving the row
// of surface control points along the shared edge (exact for iso edges,
// nearest CVs otherwise); the remaining edge curves are re-fitted between
// their moved vertices. Tolerances are recomputed afterwards.
bool MoveBrepFaces(ON_Brep& b, const std::vector<int>& faces, const ON_Xform& xf);
bool MoveBrepEdges(ON_Brep& b, const std::vector<int>& edges, const ON_Xform& xf);

// ---- topology (meshes and SubD control nets) --------------------------------
// The polygon mesh the topology walks run on: a mesh's own faces or the
// SubD control net (copied into `scratch`). Null for other kinds.
const ON_Mesh* TopologyMesh(const SceneObject& o, kernel::Mesh& scratch);
std::vector<SubObjectRef> EdgeLoop(const SceneObject& o, const SubObjectRef& edge);
std::vector<SubObjectRef> EdgeRing(const SceneObject& o, const SubObjectRef& edge);
std::vector<SubObjectRef> FaceLoop(const SceneObject& o, const SubObjectRef& edge);
std::vector<SubObjectRef> ConnectedFaces(const SceneObject& o, int seed_face);
// Faces reachable from the seeds without crossing a boundary edge.
std::vector<SubObjectRef> FacesToBoundary(const SceneObject& o, const std::vector<int>& seed_faces,
                                          const std::vector<SubObjectRef>& boundary_edges);
std::vector<SubObjectRef> AllEdges(const SceneObject& o);
std::vector<SubObjectRef> NakedEdges(const SceneObject& o);
std::vector<SubObjectRef> NakedEdgeVertices(const SceneObject& o);
// Mesh / SubD control-net edges shared by more than two faces (SelNonManifold).
std::vector<SubObjectRef> NonManifoldEdges(const SceneObject& o);
// SubD crease edges as control-net edges.
std::vector<SubObjectRef> SubDCreaseEdges(const SceneObject& o);
// Faces of the topology mesh adjacent to a vertex / edge.
std::vector<int> FacesOfEdge(const ON_Mesh& m, int a, int b);

}  // namespace dino8::app
