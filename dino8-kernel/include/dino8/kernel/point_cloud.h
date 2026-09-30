#pragma once

#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::kernel {

// One neighbor found by PointCloud::KNearest() or
// PointsWithinRadius(): the index of a point in this cloud (exactly what
// PointAt(index) would return) paired with its exact Euclidean distance
// to the query point that found it.
struct PointCloudNeighbor {
  int index = -1;
  double distance = 0;
};

// Wraps ON_PointCloud - OpenNURBS' own point-set geometry class, the same
// one its own .3dm reader/writer already round-trips (ON_BinaryArchive's
// object table dispatches on ON_Geometry::Cast the same way every other
// object kind here does). A real point-cloud representation, not a
// grouped set of separate Point objects: one array of positions plus,
// per point cloud (not per point - ON_PointCloud's own convention, see
// HasPointColors()/HasPointNormals() below), an optional parallel array
// of per-point colors and an optional parallel array of per-point
// normals.
//
// Deliberately scoped down from Rhino's own PointCloud object: this
// wrapper covers position + per-point color + per-point normal (what a
// viewport draw, a Save/Open round-trip, and a Move/Rotate/Scale
// transform all genuinely need) and leaves out ON_PointCloud's own
// per-point "hidden" flag and per-point value channel (m_H/m_V) - both
// exist to support interactively editing individual points within a
// cloud (grip-dragging/hiding one point at a time), and nothing in this
// app's UI or command set can select a sub-point of a point cloud (only
// pick/move/delete the whole object, the same granularity every other
// object kind here already has) - so there's nothing that would ever
// read or write those two fields. Honestly noted here, in
// PointCloud::PointCount()'s own doc comment on SceneObject.h, and in
// the PointCloud command's own registration text, rather than silently
// left unimplemented.
class PointCloud {
 public:
  int PointCount() const { return cloud_.PointCount(); }
  void AppendPoint(const Point3d& p) { cloud_.AppendPoint(p); }

  Point3d PointAt(int i) const { return cloud_.m_P[i]; }
  void SetPointAt(int i, const Point3d& p) { cloud_.m_P[i] = p; }

  // True only when there is exactly one color per point (ON_PointCloud's
  // own "all or nothing" convention - a partially-set m_C is read back as
  // "no colors at all", never silently truncated or index-out-of-range).
  bool HasColors() const { return cloud_.HasPointColors(); }
  // `colors.size()` must equal PointCount(); anything else clears the
  // per-point colors entirely rather than partially setting them (same
  // "all or nothing" convention ON_PointCloud itself enforces).
  void SetColors(const std::vector<ON_Color>& colors);
  ON_Color ColorAt(int i) const { return cloud_.m_C[i]; }

  bool HasNormals() const { return cloud_.HasPointNormals(); }
  void SetNormals(const std::vector<Vector3d>& normals);
  Vector3d NormalAt(int i) const { return cloud_.m_N[i]; }

  // Axis-aligned bounding box over every point. Throws
  // std::invalid_argument on an empty cloud, matching Mesh::GetBoundingBox's
  // own convention for "nothing to bound" rather than returning a
  // degenerate all-zero box that would look like a real point at the
  // origin.
  BoundingBox GetBoundingBox() const;

  // Applies `xform` to a copy of this cloud (points, and normals if
  // present - ON_PointCloud::Transform already handles both, including
  // re-normalizing transformed normals) and returns it, the same
  // "returns a transformed copy" convention Mesh::Transform() and
  // NurbsCurve/NurbsSurface's own in-place-vs-Brep::Transform split use
  // elsewhere in this kernel.
  PointCloud Transform(const ON_Xform& xform) const;

  // The `k` closest points to `query`, nearest first - the search a
  // "select nearby points" tool, a local normal-estimation step, or a
  // nearest-sample lookup needs, which nothing here could answer before
  // (only a per-point-BY-INDEX PointAt() existed; nothing could ask
  // "which points are near this one"). Exact Euclidean distance to every
  // point in the cloud, brute force - no spatial acceleration structure
  // (no kd-tree/R-tree, despite OpenNURBS shipping a real ON_RTree this
  // could have been layered on), the same "exact answer over every
  // candidate, no BVH" tradeoff Mesh::DistanceTo() documents for its own
  // point-to-triangle search; honest about being O(PointCount()) per
  // query rather than claiming a scalability this doesn't have. Ties
  // (exactly equal distance) are broken by ascending index, so the
  // result is fully deterministic and repeatable. If `k >= PointCount()`,
  // every point is returned, sorted by distance - not an error, the same
  // "clamp rather than reject" a request for more neighbors than exist
  // gets elsewhere. Throws std::invalid_argument if `k <= 0` or the cloud
  // has no points (there is no reasonable set of "nearest points" to a
  // query into an empty cloud, unlike PointsWithinRadius() below, where
  // zero matches is itself a legitimate answer).
  std::vector<PointCloudNeighbor> KNearest(Point3d query, int k) const;

  // Every point within `radius` of `query` (inclusive: distance <=
  // radius), sorted by ascending distance - the search a "select points
  // near here" tool or a local-density/outlier check needs. Exact
  // Euclidean distance to every point, brute force (same
  // no-acceleration-structure tradeoff as KNearest() above, and the same
  // ascending-index tie-break for determinism). An empty result is not an
  // error - nothing lying within `radius` is a legitimate answer, not a
  // failed query, so an empty cloud or a radius smaller than every
  // distance both just return an empty vector. A negative `radius` IS
  // rejected: throws std::invalid_argument (there is no such thing as a
  // negative-radius neighborhood, so this can only be a caller bug, not
  // a sparse region).
  std::vector<PointCloudNeighbor> PointsWithinRadius(Point3d query, double radius) const;

  // Writes this cloud to a plain-text ASCII XYZ point-cloud file - the
  // de facto point-cloud interchange format (CloudCompare, PCL, MeshLab
  // all read/write it) that this kernel had no path to at all: every
  // point-cloud entry point here was .3dm-only (Model::AddPointCloud())
  // or an in-memory-only op (Transform(), KNearest(), ...), with no
  // Save/Load of its own the way Mesh has SaveObj/SaveStl. One point per
  // line: "x y z" if this cloud has no normals, or "x y z nx ny nz" if it
  // does - normals ride along because the format has one unambiguous
  // convention for them (three more columns, same order as position).
  // Per-point colors are deliberately NOT written: unlike position and
  // normal, ASCII XYZ has no single agreed-on column order, count, or
  // scale for color (RGB 0-255? 0-1? before or after the normal
  // columns?) across the tools that read it, so writing something would
  // be inventing a convention this format doesn't actually have, not a
  // real export - a silent, undocumented color loss would be worse than
  // this honest, documented one. Returns Result::Failed if the file
  // can't be opened for writing.
  Result SaveXyz(const std::string& path) const;

  // Reads a plain-text ASCII XYZ point-cloud file written by SaveXyz()
  // (or any compatible tool): every non-blank line must carry exactly 3
  // whitespace-separated numbers (position) or exactly 6 (position then
  // normal, SaveXyz()'s own convention), and every line in one file must
  // carry the same count - a file mixing 3- and 6-column lines would be
  // genuinely ambiguous (is column 4 a normal, or the next point's x?),
  // so it's rejected rather than guessed at. Returns Result::Failed -
  // leaving `out_cloud` untouched rather than half-filled - if the file
  // can't be opened, has zero points, or any line has a column count
  // other than 3/6, disagrees with an earlier line's column count, or a
  // column that fails to parse as a real number.
  static Result LoadXyz(const std::string& path, PointCloud& out_cloud);

  // Writes this cloud to a plain-text .pts point-cloud file - the common
  // laser-scan/point-cloud interchange format (Leica Cyclone, CloudCompare,
  // ...) PARITY_MAP.md's own "Point-cloud/scan formats" evidence names as
  // still entirely missing from this kernel (only SaveXyz/LoadXyz's own
  // ASCII XYZ existed before this). Unlike XYZ, a .pts file opens with a
  // single header line giving the exact point count, then one point per
  // line: "x y z" if this cloud has no colors, or "x y z r g b" (R/G/B as
  // 0-255 integers) if it does. Scoped down from the fuller Leica Cyclone
  // .pts convention some tools write, which also carries a 4th per-point
  // "intensity" column before R/G/B: this class has no per-point intensity
  // channel to source that column from (only position/color/normal - see
  // this class' own top-of-file scope note), so inventing one would be
  // fabricating data no caller ever supplied, the same reasoning SaveXyz()
  // already gives for leaving color out of ITS OWN format entirely.
  // Normals are also not written: unlike position and color, the real .pts
  // format has no normal column in any variant, so there is no convention
  // to follow here, honest or otherwise. Returns Result::Failed if the
  // file can't be opened for writing.
  Result SavePts(const std::string& path) const;

  // Reads a plain-text .pts point-cloud file written by SavePts() (or a
  // compatible tool writing the same 3-or-6-column, no-intensity
  // convention documented on SavePts() above): the first non-blank line
  // must be a single positive integer point count; every following
  // non-blank line must carry exactly 3 (position) or exactly 6 (position
  // then R/G/B, each an integer in [0, 255]) whitespace-separated values,
  // matching LoadXyz()'s own "one shared column count for the whole file,
  // never mixed" rule. Unlike LoadXyz(), the header count is itself
  // checked: a file whose actual point-line count doesn't match what its
  // own header declared - truncated by a failed write, or hand-edited - is
  // Result::Failed rather than silently loaded with however many lines
  // happened to be there. Returns Result::Failed - leaving `out_cloud`
  // untouched - on a missing/non-positive/non-integer header count, a
  // point-line count mismatch, a column count other than 3/6, a column
  // count that disagrees with an earlier line's, an out-of-range or
  // non-integer R/G/B value, or a column that fails to parse as a number.
  static Result LoadPts(const std::string& path, PointCloud& out_cloud);

  // Writes this cloud to a plain-ASCII PCL Point Cloud Data (.pcd, v0.7)
  // file - the third point-cloud interchange format this kernel gets a
  // path to (after XYZ and .pts above), and the one PCL/ROS's own
  // ecosystem of tools actually reads and writes natively. A real
  // `.pcd`'s header can declare an arbitrary ordered FIELDS list; this
  // writer only ever emits one of four fixed combinations, chosen by
  // which optional data this cloud actually has - `x y z`, `x y z rgb`,
  // `x y z normal_x normal_y normal_z`, or
  // `x y z rgb normal_x normal_y normal_z` - the same "no fabricating a
  // column this cloud has no data for" discipline SaveXyz()/SavePts()
  // already apply to their own formats, but unlike either of those two
  // (each limited to just one optional extra), this format can carry
  // color AND normals in the same file, since real `.pcd` genuinely
  // supports both at once. Color is packed into a single `rgb` field the
  // way real PCL files do: `(r << 16) | (g << 8) | b` reinterpreted as an
  // IEEE-754 float bit pattern, not three separate columns - so a color
  // round trip through this writer and LoadPcd() below is exact (the
  // packed float's bits are preserved, not just its decimal value).
  // `WIDTH`/`POINTS` are both written as `PointCount()` and `HEIGHT` as
  // `1` - this only ever writes an unorganized cloud, the same
  // "positions are a flat list, not a 2D grid" shape this class has
  // throughout. `DATA` is always `ascii` - the binary/binary_compressed
  // DATA variants real `.pcd` files can also use are out of scope here,
  // the same "plain text only" scope every other point-cloud/mesh format
  // in this kernel already has. Returns Result::Failed if the file can't
  // be opened for writing.
  Result SavePcd(const std::string& path) const;

  // Reads a plain-ASCII `.pcd` file written by SavePcd() (or a compatible
  // v0.7-header PCL file using the same field set and packed-`rgb`
  // convention documented on SavePcd() above) into `out_cloud`. This is a
  // deliberately narrow scan for exactly that structure, not a general
  // PCD reader: the header must contain a `VERSION` line (any value - the
  // same "require the format's own marker, don't silently misread a
  // different file" stance LoadVrml()'s `#VRML` check and LoadUsda()'s
  // `#usda` check already take), a `FIELDS` line whose value is exactly
  // one of the four whitespace-separated combinations SavePcd() can write
  // (`x y z`, `x y z rgb`, `x y z normal_x normal_y normal_z`, or
  // `x y z rgb normal_x normal_y normal_z`) - any other field list, order,
  // or subset is rejected outright rather than guessed at - a `POINTS`
  // line giving the exact point count, and a `DATA` line whose value is
  // exactly `ascii` (`binary`/`binary_compressed` are rejected, out of
  // scope per SavePcd()'s own doc comment). A `HEIGHT` line, if present,
  // must be `1` (an organized/structured cloud - `HEIGHT > 1` - is out of
  // scope, the same "flat list of positions" shape this class has
  // throughout); `SIZE`/`TYPE`/`COUNT`/`WIDTH`/`VIEWPOINT` lines are
  // tolerated but not otherwise validated, since everything this reader
  // actually needs to parse the data section correctly already comes from
  // `FIELDS` and `POINTS`. Exactly `POINTS` data lines follow, each with
  // the column count `FIELDS` implies (3/4/6/7); an `rgb` column is
  // parsed back to its exact original R/G/B by reversing SavePcd()'s own
  // float-bit-pack (the token is read as a double - exactly representing
  // the written float, since a double losslessly holds any float value -
  // then narrowed to `float` and its bits reinterpreted as the packed
  // `uint32_t`), so a color round trip through SavePcd()/LoadPcd() is
  // exact even though the file itself never shows a plain integer R/G/B.
  // Returns Result::Failed - leaving `out_cloud` untouched - if the file
  // can't be opened, `VERSION`/`FIELDS`/`POINTS`/`DATA` isn't found,
  // `FIELDS` isn't one of the four known combinations, `DATA` isn't
  // `ascii`, `HEIGHT` is present and isn't `1`, the actual data-line count
  // doesn't match `POINTS`, a data line's column count doesn't match what
  // `FIELDS` implies, or any column fails to parse as a number.
  static Result LoadPcd(const std::string& path, PointCloud& out_cloud);

  // Writes this cloud to a binary ASPRS LAS 1.2 (.las) file - the fourth
  // point-cloud interchange format this kernel gets a path to, and the one
  // real LiDAR/survey tooling (PDAL, LAStools, most GIS/point-cloud
  // software) actually reads and writes, unlike XYZ/.pts/.pcd's plain-text
  // conventions above. Writes Point Data Record Format 0 (position only)
  // if this cloud has no colors, or Format 2 (position + RGB) if
  // `HasColors()` is true - the same "no fabricating a column this cloud
  // has no data for" discipline SaveXyz()/SavePts()/SavePcd() already apply
  // to their own formats. Normals are NOT written: no LAS point data format
  // has a normal field at all (LAS is a LiDAR *scan* format - normals are
  // something a later processing step derives, never something a scanner
  // itself records), so there is no convention to follow here, the same
  // honest-omission reasoning SavePts() already gives for its own missing
  // normal column.
  //
  // Positions are quantized, a fundamental property of the LAS format
  // itself (every coordinate is stored as a scaled int32, not a native
  // double) rather than a shortcut this writer takes: each axis gets its
  // own offset (that axis' own minimum value over the whole cloud, the
  // ordinary LAS convention for keeping the stored integers small) and a
  // fixed scale factor of 0.001 (LAS' own common millimeter-precision
  // convention), so `stored_int = round((coordinate - axis_offset) / 0.001)`
  // and decoding is exact given that int - a round trip through
  // SaveLas()/LoadLas() reproduces the original position only to within
  // half the scale factor (0.0005), not bit-for-bit. Colors, in contrast,
  // round-trip exactly: each 8-bit channel is scaled to LAS' own 16-bit
  // Red/Green/Blue fields by the exact factor 257 (255 * 257 == 65535, so
  // `channel * 257` never exceeds a uint16, and integer-dividing back by
  // 257 recovers the original 0-255 value with no remainder for every
  // possible input). Returns Result::Failed if the file can't be opened
  // for writing, or if this cloud is empty (there is no meaningful
  // per-axis offset/bounding box to derive from zero points).
  Result SaveLas(const std::string& path) const;

  // Reads a binary LAS file written by SaveLas() (or a compatible LAS 1.2
  // file using Point Data Record Format 0 or 2) into `out_cloud`. A
  // deliberately narrow reader, not a general LAS 1.0-1.4 parser: the
  // 227-byte LAS 1.2 public header block is required exactly - file
  // signature `LASF`, version major/minor `1`/`2`, header size `227`, and
  // offset-to-point-data `227` (i.e. no Variable Length Records, which this
  // reader does not understand at all) - and the Point Data Format ID must
  // be `0` (20-byte records: position only) or `2` (26-byte records:
  // position + RGB), with the header's own declared point-data record
  // length matching that format exactly. Every other LAS version, any file
  // carrying VLRs, and every other point data format (1/3/4/5/... -
  // GPS time, waveform data, extra bytes, and every other richer LAS
  // feature) are rejected outright rather than silently misread, the same
  // "require the format's own exact structure, don't guess" stance
  // LoadGlb()'s own magic/version/chunk checks already take. Colors are
  // read back from Format 2's Red/Green/Blue fields (integer-divided by
  // 257, the exact inverse of SaveLas()'s own scaling - see SaveLas()'s
  // own doc comment); a Format-0 file leaves the loaded cloud with no
  // colors at all. Returns Result::Failed - leaving `out_cloud` untouched -
  // if the file can't be opened, the header doesn't match the exact
  // structure above, the point data format isn't 0 or 2, the record length
  // in the header doesn't match the format, or the file is truncated
  // before all of the header's own declared point records can be read.
  static Result LoadLas(const std::string& path, PointCloud& out_cloud);

  const ON_PointCloud& raw() const { return cloud_; }
  ON_PointCloud& raw() { return cloud_; }

 private:
  ON_PointCloud cloud_;
};

}  // namespace dino8::kernel
