// Boolean and splitting commands (mesh-based, via Manifold).
#include "commands/cmd_common.h"
#include "dino8/kernel/boolean_general.h"

namespace dino8::app {

namespace {

// Tries a genuine B-rep-preserving boolean before RunBoolean below falls
// back to its own mesh/Manifold path - PARITY_MAP.md's own "kernel: Boolean
// operations" category names "B-rep-preserving booleans reachable from the
// application (polysurface in, polysurface out)" as the one item still
// missing outright: every app boolean command tessellates its operands via
// MeshOf and emits a mesh result, with zero references anywhere in
// dino8-app/src to BooleanCombinePlanar/BooleanCombineMixed/
// BooleanCombineGeneral. This closes that gap for the one case those kernel
// functions actually cover end-to-end: every operand on both sides is
// already a plain, single-lump ON_Brep (ObjectKind::Brep, not a surface,
// mesh or SubD that would need tessellating just to find out), combined via
// BooleanCombinePlanarNAry - the same convex-then-concave, "exact
// construction first, fail open to the approximate path" structure
// TryExactFillet/TryExactChamfer already use in cmd_fillet.cpp for the
// identical reason (the caller can't know in advance whether the exact
// engine's own PlanarFaces()-only, single-lump-per-op scope will accept
// this particular selection). SymmetricDifference has no N-ary form (its
// own result is a two-lump Brep::Compound that can't be folded further by
// Union), so it only takes the exact path for exactly one Brep per side,
// via a single direct BooleanCombinePlanar call. Returns nullopt - a
// silent, ordinary fallback to RunBoolean's own mesh path below, not a
// user-visible failure - whenever any operand isn't a plain Brep, the
// group is empty, SymmetricDifference has more than one Brep per side, or
// the exact engine itself throws (a curved face anywhere on any operand,
// a compound operand fed to Union/SymmetricDifference, or any of that
// engine's other disclosed scope limits). `second_ids` empty means a
// single-group Union (RunBoolean's own !two_sets case, where every operand
// - both `a` and `b` combined - folds into one result via Union); a
// non-empty `second_ids` combines `first_ids`'s own fold against
// `second_ids`'s own fold via `op`.
std::optional<kernel::Brep> TryExactBrepBoolean(CommandContext& ctx, const std::vector<ObjectId>& first_ids, const std::vector<ObjectId>& second_ids,
                                                 kernel::BooleanOp op) {
  // IsSolid() (closed, manifold, actually enclosing a volume) is required
  // here, not just ObjectKind::Brep - BooleanCombinePlanar's own PlanarFaces()
  // precondition happily accepts an OPEN planar-faced shell (e.g. a box with
  // one face deleted: still every remaining face is individually planar) and
  // folds it into the result anyway, silently producing a wrong, non-closed
  // answer instead of the "reject the bad operand, leave it untouched" the
  // mesh path below already gives via its own IsClosedManifold() check - see
  // boolean_adversarial_script.txt's own "Non-manifold input" case, which
  // this precondition exists specifically to keep exact and mesh path
  // agreeing on which operands are eligible in the first place.
  auto collect = [&](const std::vector<ObjectId>& ids, std::vector<kernel::Brep>& out) -> bool {
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind != ObjectKind::Brep || !o->brep || !o->brep->raw().IsSolid()) return false;
      out.push_back(*o->brep);
    }
    return !out.empty();
  };
  std::vector<kernel::Brep> g1, g2;
  if (!collect(first_ids, g1)) return std::nullopt;
  if (!second_ids.empty() && !collect(second_ids, g2)) return std::nullopt;
  try {
    if (second_ids.empty()) return kernel::BooleanCombinePlanarNAry(g1, {}, kernel::BooleanOp::Union);
    if (op == kernel::BooleanOp::SymmetricDifference) {
      if (g1.size() != 1 || g2.size() != 1) return std::nullopt;
      return kernel::BooleanCombinePlanar(g1[0], g2[0], op);
    }
    return kernel::BooleanCombinePlanarNAry(g1, g2, op);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// Shared by BooleanCommand and Boolean2ObjectsCommand: unions each side's
// own set (when there are several objects per side), then combines the two
// sides with `op`. `swap_sides` runs the op with the sides reversed, so
// e.g. Difference(A,B) can be flipped to Difference(B,A) without the caller
// re-collecting meshes. `try_exact_brep` (BooleanUnion/BooleanDifference/
// BooleanIntersection/Boolean2Objects; never the Mesh* aliases, which
// promise a mesh result) tries TryExactBrepBoolean above first, ahead of
// this function's own mesh path.
void RunBoolean(CommandContext& ctx, const std::vector<ObjectId>& a, const std::vector<ObjectId>& b, kernel::BooleanOp op,
                bool two_sets, const std::string& label, bool swap_sides = false, bool try_exact_brep = false) {
  std::vector<ObjectId> all = a;
  all.insert(all.end(), b.begin(), b.end());
  if (try_exact_brep && !all.empty()) {
    const std::vector<ObjectId>& ea = swap_sides ? b : a;
    const std::vector<ObjectId>& eb = swap_sides ? a : b;
    std::vector<ObjectId> first_ids = two_sets ? ea : all;
    std::vector<ObjectId> second_ids = two_sets ? eb : std::vector<ObjectId>{};
    if (std::optional<kernel::Brep> result = TryExactBrepBoolean(ctx, first_ids, second_ids, op)) {
      ctx.Doc().BeginChange(label);
      int layer = 0;
      if (const SceneObject* o = ctx.Doc().Find(all.front())) layer = o->layer_index;
      for (ObjectId id : all) ctx.Doc().Remove(id);
      if (result->raw().m_F.Count() > 0) {
        SceneObject n = SceneObject::MakeBrep(*result);
        n.layer_index = layer;
        ctx.Doc().Add(std::move(n));
        ctx.Print(label + ": exact B-rep boolean (no tessellation), " + std::to_string(result->raw().m_F.Count()) + " face(s)");
      } else {
        ctx.Print(label + ": result is empty");
      }
      return;
    }
  }
  std::vector<std::pair<ObjectId, kernel::Mesh>> ma, mb;
  auto collect = [&](const std::vector<ObjectId>& ids, std::vector<std::pair<ObjectId, kernel::Mesh>>& out) {
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!m || !m->IsClosedManifold()) { ctx.Warn("Object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      out.push_back({id, *m});
    }
  };
  if (two_sets) { collect(a, ma); collect(b, mb); }
  else { collect(all, ma); }
  if (ma.empty() || (two_sets && mb.empty())) { ctx.Warn("Nothing to combine"); return; }
  try {
    kernel::Mesh result = ma[0].second;
    int layer = ctx.Doc().Find(ma[0].first) ? ctx.Doc().Find(ma[0].first)->layer_index : 0;
    if (two_sets) {
      for (size_t i = 1; i < ma.size(); ++i) result = kernel::BooleanCombine(result, ma[i].second, kernel::BooleanOp::Union);
      kernel::Mesh other = mb[0].second;
      for (size_t i = 1; i < mb.size(); ++i) other = kernel::BooleanCombine(other, mb[i].second, kernel::BooleanOp::Union);
      result = swap_sides ? kernel::BooleanCombine(other, result, op) : kernel::BooleanCombine(result, other, op);
    } else {
      for (size_t i = 1; i < ma.size(); ++i) result = kernel::BooleanCombine(result, ma[i].second, op);
    }
    ctx.Doc().BeginChange(label);
    for (auto& [id, m] : ma) ctx.Doc().Remove(id);
    for (auto& [id, m] : mb) ctx.Doc().Remove(id);
    if (result.FaceCount() > 0) {
      SceneObject n = SceneObject::MakeMesh(result);
      n.layer_index = layer;
      ctx.Doc().Add(std::move(n));
      ctx.Print(label + ": " + std::to_string(result.FaceCount()) + " faces, volume " + FormatNumber(result.Volume()));
    } else {
      ctx.Print(label + ": result is empty");
    }
  } catch (const std::exception& ex) {
    ctx.Warn(std::string("Boolean failed: ") + ex.what());
  }
}

// Two-set boolean: first selection, then second selection. `try_exact_brep`
// is set for the plain BooleanUnion/BooleanDifference/BooleanIntersection
// registrations below, not their MeshBooleanX aliases, which promise a
// mesh result even when an exact B-rep one would be available.
class BooleanCommand : public Command {
 public:
  BooleanCommand(kernel::BooleanOp op, const char* label, bool two_sets, bool try_exact_brep = false)
      : op_(op), label_(label), two_sets_(two_sets), try_exact_brep_(try_exact_brep) {}
  void Begin(CommandContext&) override { WantObjects(two_sets_ ? std::string("Select first set of objects") : "Select objects to " + std::string(label_)); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (two_sets_ && a_.empty()) {
      a_ = ids;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      WantObjects("Select second set of objects");
      accept_preselection = false;
      return;
    }
    RunBoolean(ctx, a_, ids, op_, two_sets_, label_, /*swap_sides=*/false, try_exact_brep_);
    Finish();
  }
  kernel::BooleanOp op_;
  const char* label_;
  bool two_sets_;
  bool try_exact_brep_;
  std::vector<ObjectId> a_;
};

// Boolean2Objects: like BooleanDifference/Intersection/Union but for exactly
// two sets, with a "Result" option that cycles through every combination
// (Union / Intersection / A-B / B-A / SymmetricDifference) before
// committing on Enter - Rhino's own way of letting you preview and pick the
// result you want without a separate command per combination.
class Boolean2ObjectsCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select first set of objects"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (a_.empty()) {
      a_ = ids;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      WantObjects("Select second set of objects");
      accept_preselection = false;
      return;
    }
    b_ = ids;
    ready_ = true;
    options = {{"Result", result_, {"Union", "Intersection", "A-B", "B-A", "SymmetricDifference"}, false, false}};
    WantEnter("Press Enter to combine (Result=" + result_ + ")");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n != "Result") return;
    result_ = v;
    options[0].value = v;
    prompt = "Press Enter to combine (Result=" + result_ + ")";
  }
  void OnEnter(CommandContext& ctx) override { if (ready_) Run(ctx); }
  // A stray non-Enter token while picking the first/second set (`ready_`
  // still false) is not an accept-with-defaults - falling through to
  // Run() here would combine with whichever set is still empty and eat
  // the token besides. Only once both sets are in (the "Result=" confirm
  // prompt) does an unrecognized token commit with the current Result
  // rather than sit modal; it is then re-run as its own command.
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!ready_) return;
    Run(ctx);
    ctx.Engine().Execute(t);
  }
  void Run(CommandContext& ctx) {
    kernel::BooleanOp op = kernel::BooleanOp::Union;
    bool swap = false;
    if (result_ == "Intersection") op = kernel::BooleanOp::Intersection;
    else if (result_ == "A-B") op = kernel::BooleanOp::Difference;
    else if (result_ == "B-A") { op = kernel::BooleanOp::Difference; swap = true; }
    else if (result_ == "SymmetricDifference") op = kernel::BooleanOp::SymmetricDifference;
    RunBoolean(ctx, a_, b_, op, true, "Boolean2Objects", swap, /*try_exact_brep=*/true);
    Finish();
  }
  std::vector<ObjectId> a_, b_;
  std::string result_ = "Union";
  bool ready_ = false;
};

// Shared by ImprintCommand/MutualImprintCommand: the first object in `ids`
// that is a plain Brep with at least one face - the exact precondition
// kernel::ImprintFaces()/MutualImprintFaces() themselves enforce (they throw
// std::invalid_argument on a faceless operand, TestImprintFacesRejects
// EmptyOperands, tests/test_basic.cpp), so this is a pre-check for a clear
// command-level warning, not a stand-in for that guard. No IsSolid()
// requirement (unlike TryExactBrepBoolean's operand collection above) -
// imprinting works on an open sheet as well as a closed solid, since it
// never ray-casts in/out of either operand, only splits faces along their
// mutual SSX curves.
const SceneObject* FirstImprintableBrep(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (o && o->kind == ObjectKind::Brep && o->brep && o->brep->raw().m_F.Count() > 0) return o;
  }
  return nullptr;
}

// Imprint: splits `target`'s own faces wherever they cross `tool`, without
// removing material from either - Parasolid PK_BODY_imprint / ACIS IMPRINT
// (PARITY_MAP.md's "kernel: Boolean operations" "Face-face imprint" gap).
// kernel::ImprintFaces() already existed, fully tested at the kernel layer;
// this is its first app command. `target` is replaced in the document by
// the imprinted result (its exact original shape/volume, with more faces
// wherever `tool` crosses it); `tool` is read-only per ImprintFaces()'s own
// contract and is left in the document untouched, exactly like a fillet or
// chamfer command leaves its reference geometry alone.
class ImprintCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select object to imprint (its own shape and volume are kept; only its faces split)"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_target_) {
      const SceneObject* o = FirstImprintableBrep(ctx, ids);
      if (!o) { ctx.Warn("Imprint: select a Brep with at least one face"); Finish(); return; }
      target_id_ = o->id;
      have_target_ = true;
      ctx.Doc().Select(target_id_, false);
      WantObjects("Select the imprinting tool object (kept unchanged)");
      accept_preselection = false;
      return;
    }
    Run(ctx, ids);
    Finish();
  }
  void Run(CommandContext& ctx, const std::vector<ObjectId>& ids) {
    const SceneObject* tool = FirstImprintableBrep(ctx, ids);
    const SceneObject* target = ctx.Doc().Find(target_id_);
    if (!tool || tool->id == target_id_ || !target || !target->brep) {
      ctx.Warn("Imprint: select a different Brep as the tool");
      return;
    }
    const int before = target->brep->raw().m_F.Count();
    const int layer = target->layer_index;
    try {
      kernel::Brep result = kernel::ImprintFaces(*target->brep, *tool->brep);
      ctx.Doc().BeginChange("Imprint");
      ctx.Doc().Remove(target_id_);
      SceneObject n = SceneObject::MakeBrep(result);
      n.layer_index = layer;
      ctx.Doc().Add(std::move(n));
      ctx.Print("Imprint: " + std::to_string(result.raw().m_F.Count()) + " face(s) (was " + std::to_string(before) + "), no material removed");
    } catch (const std::exception& ex) {
      ctx.Warn(std::string("Imprint failed: ") + ex.what());
    }
  }
  ObjectId target_id_ = kNoObject;
  bool have_target_ = false;
};

// MutualImprint: like Imprint above, but both operands imprint each other -
// Parasolid/ACIS's own two-way imprint. kernel::MutualImprintFaces() runs
// ImprintFaces() twice with the operands swapped; neither side ever loses
// material. Both objects are replaced in the document by their own
// imprinted result.
class MutualImprintCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select first object to imprint"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_first_) {
      const SceneObject* o = FirstImprintableBrep(ctx, ids);
      if (!o) { ctx.Warn("MutualImprint: select a Brep with at least one face"); Finish(); return; }
      first_id_ = o->id;
      have_first_ = true;
      ctx.Doc().Select(first_id_, false);
      WantObjects("Select the second object to imprint");
      accept_preselection = false;
      return;
    }
    Run(ctx, ids);
    Finish();
  }
  void Run(CommandContext& ctx, const std::vector<ObjectId>& ids) {
    const SceneObject* second = FirstImprintableBrep(ctx, ids);
    const SceneObject* first = ctx.Doc().Find(first_id_);
    if (!second || second->id == first_id_ || !first || !first->brep) {
      ctx.Warn("MutualImprint: select a different Brep as the second object");
      return;
    }
    const ObjectId second_id = second->id;
    const int layer_a = first->layer_index, layer_b = second->layer_index;
    try {
      auto [imprinted_a, imprinted_b] = kernel::MutualImprintFaces(*first->brep, *second->brep);
      ctx.Doc().BeginChange("MutualImprint");
      ctx.Doc().Remove(first_id_);
      ctx.Doc().Remove(second_id);
      SceneObject na = SceneObject::MakeBrep(imprinted_a);
      na.layer_index = layer_a;
      ctx.Doc().Add(std::move(na));
      SceneObject nb = SceneObject::MakeBrep(imprinted_b);
      nb.layer_index = layer_b;
      ctx.Doc().Add(std::move(nb));
      ctx.Print("MutualImprint: " + std::to_string(imprinted_a.raw().m_F.Count()) + " + " +
                std::to_string(imprinted_b.raw().m_F.Count()) + " face(s), no material removed");
    } catch (const std::exception& ex) {
      ctx.Warn(std::string("MutualImprint failed: ") + ex.what());
    }
  }
  ObjectId first_id_ = kNoObject;
  bool have_first_ = false;
};

// Shared by SplitBySheetCommand/TrimSheetBySolidCommand below: the first
// object in `ids` that is a plain, closed (ON_Brep::IsSolid()) Brep - the
// precondition kernel::SplitBySheet()/TrimSheetBySolid() themselves expect
// of their own `solid` operand (both classify by ray-casting in/out of it,
// which has no well-defined meaning for an open shell).
const SceneObject* FirstSolidBrep(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (o && o->kind == ObjectKind::Brep && o->brep && o->brep->raw().IsSolid()) return o;
  }
  return nullptr;
}

// Shared by SplitBySheetCommand/TrimSheetBySolidCommand below: the first
// object in `ids` usable as an open cutting/trimming "sheet" - either a
// Brep with at least one face (the same precondition FirstImprintableBrep
// above already checks, since kernel::SplitBySheet()/TrimSheetBySolid()
// impose no IsSolid() requirement on `sheet` either) or a bare Surface
// object, wrapped into a one-face Brep via Brep::FromSurface() the same way
// this kernel's own test fixtures (MakePlanarSheetZ, dino8-kernel/tests/
// test_basic.cpp) build a sheet from a NurbsSurface - a plain "Plane"/
// "Plane3Pt"/"SrfPt" result has never gone through PlanarSrf's own
// curve-to-Brep step, so without this a user would have to build a closed
// planar curve and run PlanarSrf first just to get a sheet either command
// could accept at all. Returns the Brep by value (not a document reference)
// since a Surface-kind pick has no Brep object of its own to point to; the
// picked object itself (whichever kind) is always left untouched in the
// document, exactly like FirstImprintableBrep's own `tool` contract.
struct SheetPick {
  ObjectId id;
  kernel::Brep brep;
};
std::optional<SheetPick> FirstSheetBrep(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Brep && o->brep && o->brep->raw().m_F.Count() > 0) return SheetPick{id, *o->brep};
    if (o->kind == ObjectKind::Surface && o->surface) return SheetPick{id, kernel::Brep::FromSurface(*o->surface)};
  }
  return std::nullopt;
}

// SplitBySheet: splits a closed solid into the two real B-rep pieces on
// either side of an open cutting sheet, each capped with the portion of the
// sheet inside the solid - PARITY_MAP.md's "kernel: Boolean operations"
// "Sheet/solid trim" bullet's first half ("open surface as cutter through a
// solid"). kernel::SplitBySheet() (boolean_general.h/.cpp) already existed,
// fully tested at the kernel layer (including a genuinely curved solid and,
// as of this pass, a genuinely curved sheet too - see this category's own
// trailing note in PARITY_MAP.md); this is its first app command, closing
// that bullet's own previously-named "wiring either into an app command"
// gap for this half. The sheet is read-only and stays in the document
// untouched, exactly like Imprint's own `tool` contract above; the solid is
// replaced by its own two pieces (0, 1, or 2 new objects - a cutter that
// misses the solid entirely leaves one empty, dropped silently, matching
// kernel::SplitBySheet's own "kept.empty()" convention for a disjoint
// sheet), never tessellated.
class SplitBySheetCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select the closed solid to split (kept as two pieces)"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_solid_) {
      const SceneObject* o = FirstSolidBrep(ctx, ids);
      if (!o) { ctx.Warn("SplitBySheet: select a closed solid Brep"); Finish(); return; }
      solid_id_ = o->id;
      have_solid_ = true;
      ctx.Doc().Select(solid_id_, false);
      WantObjects("Select the cutting sheet (an open surface or Brep; kept unchanged)");
      accept_preselection = false;
      return;
    }
    Run(ctx, ids);
    Finish();
  }
  void Run(CommandContext& ctx, const std::vector<ObjectId>& ids) {
    std::optional<SheetPick> sheet = FirstSheetBrep(ctx, ids);
    const SceneObject* solid = ctx.Doc().Find(solid_id_);
    if (!sheet || sheet->id == solid_id_ || !solid || !solid->brep) {
      ctx.Warn("SplitBySheet: select a different object as the cutting sheet");
      return;
    }
    const int layer = solid->layer_index;
    try {
      auto [positive_side, negative_side] = kernel::SplitBySheet(*solid->brep, sheet->brep);
      ctx.Doc().BeginChange("SplitBySheet");
      ctx.Doc().Remove(solid_id_);
      int made = 0;
      for (kernel::Brep* piece : {&positive_side, &negative_side}) {
        if (piece->raw().m_F.Count() == 0) continue;
        SceneObject n = SceneObject::MakeBrep(*piece);
        n.layer_index = layer;
        ctx.Doc().Add(std::move(n));
        ++made;
      }
      ctx.Print("SplitBySheet: " + std::to_string(made) + " piece(s), exact B-rep (no tessellation)");
    } catch (const std::exception& ex) {
      ctx.Warn(std::string("SplitBySheet failed: ") + ex.what());
    }
  }
  ObjectId solid_id_ = kNoObject;
  bool have_solid_ = false;
};

// TrimSheetBySolid: trims an open sheet's own surface down to the portion
// inside (or, with KeepInside=No, outside) a solid - PARITY_MAP.md's "Sheet/
// solid trim" bullet's OTHER half, distinct from SplitBySheetCommand above
// (which splits a solid BY a sheet; this trims a sheet BY a solid, never
// splitting, capping or returning the solid itself). kernel::
// TrimSheetBySolid() already existed, fully tested at the kernel layer; this
// is its first app command. The solid is read-only and stays in the
// document untouched (used purely as the ray-cast classification target,
// exactly like ImprintCommand's own `tool`); the sheet is replaced by its
// own trimmed result.
class TrimSheetBySolidCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select the sheet to trim"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_sheet_) {
      std::optional<SheetPick> sheet = FirstSheetBrep(ctx, ids);
      if (!sheet) { ctx.Warn("TrimSheetBySolid: select a Brep or Surface with at least one face"); Finish(); return; }
      sheet_id_ = sheet->id;
      have_sheet_ = true;
      ctx.Doc().Select(sheet_id_, false);
      options = {{"KeepInside", keep_inside_ ? "Yes" : "No", {"Yes", "No"}, false, true}};
      WantObjects("Select the trimming solid (kept unchanged)");
      accept_preselection = false;
      return;
    }
    Run(ctx, ids);
    Finish();
  }
  void OnOption(CommandContext&, const std::string& n, const std::string&) override {
    if (n == "KeepInside") { keep_inside_ = !keep_inside_; options[0].value = keep_inside_ ? "Yes" : "No"; }
  }
  void Run(CommandContext& ctx, const std::vector<ObjectId>& ids) {
    const SceneObject* solid = FirstSolidBrep(ctx, ids);
    const SceneObject* sheet_obj = ctx.Doc().Find(sheet_id_);
    if (!solid || solid->id == sheet_id_ || !sheet_obj) {
      ctx.Warn("TrimSheetBySolid: select a different, closed solid Brep");
      return;
    }
    std::optional<SheetPick> sheet = FirstSheetBrep(ctx, {sheet_id_});
    if (!sheet) { ctx.Warn("TrimSheetBySolid: the originally-selected sheet is no longer valid"); return; }
    const int layer = sheet_obj->layer_index;
    try {
      kernel::Brep result = kernel::TrimSheetBySolid(sheet->brep, *solid->brep, keep_inside_);
      ctx.Doc().BeginChange("TrimSheetBySolid");
      ctx.Doc().Remove(sheet_id_);
      SceneObject n = SceneObject::MakeBrep(result);
      n.layer_index = layer;
      ctx.Doc().Add(std::move(n));
      ctx.Print("TrimSheetBySolid: " + std::to_string(result.raw().m_F.Count()) + " face(s), KeepInside=" +
                std::string(keep_inside_ ? "Yes" : "No"));
    } catch (const std::exception& ex) {
      ctx.Warn(std::string("TrimSheetBySolid failed: ") + ex.what());
    }
  }
  ObjectId sheet_id_ = kNoObject;
  bool have_sheet_ = false;
  bool keep_inside_ = true;
};

// Split solids by a plane through two picked points (normal to the CPlane).
class SplitPlaneCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select solids to split"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantPoint("Start of cutting line"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!a_) { a_ = p; WantPoint("End of cutting line"); return; }
    Vector3d n = ON_CrossProduct(p - *a_, ActiveNormal(ctx));
    if (n.Length() <= 0) { ctx.Warn("Degenerate cutting line"); Finish(); return; }
    n.Unitize();
    const double offset = ON_DotProduct(n, *a_);
    ctx.Doc().BeginChange("Split");
    int made = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!m || !m->IsClosedManifold()) { ctx.Warn("Object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      try {
        auto [pos, neg] = kernel::SplitByPlane(*m, n, offset);
        const int layer = o->layer_index;
        ctx.Doc().Remove(id);
        for (kernel::Mesh* part : {&pos, &neg}) if (part->FaceCount() > 0) { SceneObject s = SceneObject::MakeMesh(*part); s.layer_index = layer; ctx.Doc().Add(std::move(s)); ++made; }
      } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
    }
    ctx.Print("Split into " + std::to_string(made) + " piece(s)");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> a_;
};

// WireCut: cuts by a plane through two picked points, like Split, but keeps
// only one side (the "Side" option) and discards the other - unlike
// BooleanSplit/MeshSplit/MeshBooleanSplit, which keep both pieces.
class WireCutCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Side", "Positive", {"Positive", "Negative"}, false, false}};
    WantObjects("Select solids to cut");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Side") { keep_positive_ = (v == "Positive"); options[0].value = v; } }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantPoint("Start of cutting line"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!a_) { a_ = p; WantPoint("End of cutting line"); return; }
    Vector3d n = ON_CrossProduct(p - *a_, ActiveNormal(ctx));
    if (n.Length() <= 0) { ctx.Warn("Degenerate cutting line"); Finish(); return; }
    n.Unitize();
    const double offset = ON_DotProduct(n, *a_);
    ctx.Doc().BeginChange("WireCut");
    int made = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!m || !m->IsClosedManifold()) { ctx.Warn("Object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      try {
        auto [pos, neg] = kernel::SplitByPlane(*m, n, offset);
        const int layer = o->layer_index;
        ctx.Doc().Remove(id);
        kernel::Mesh& keep = keep_positive_ ? pos : neg;
        if (keep.FaceCount() > 0) { SceneObject s = SceneObject::MakeMesh(keep); s.layer_index = layer; ctx.Doc().Add(std::move(s)); ++made; }
      } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
    }
    ctx.Print("WireCut: kept " + std::to_string(made) + " piece(s), discarded the other side");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> a_;
  bool keep_positive_ = true;
};

// Turns an open cutter mesh (a surface tessellation, an open mesh - one
// simply-connected patch with a single boundary loop) into a closed
// half-space-like solid BooleanCombine can use: extrudes it, starting
// exactly at its own real position, along its own area-weighted average
// face normal far enough to clear every target in `span` (the targets'
// combined bounding box) - so BooleanCombine(target, this solid,
// Intersection)/Difference split the target into the piece on the
// outward-normal side of the cutter and the piece on the other side,
// the same halves kernel::SplitByPlane's own exact plane cutter produces
// for a flat cutter, approximated here for a general (possibly curved)
// one. Extruding from the cap's OWN position (not shifted back first) is
// what makes this a half-space rather than a thickened slab centered on
// the cutter - a slab wide enough to clear the target from both sides
// would swallow it whole (Intersection = the entire target, Difference =
// empty) instead of splitting it in two. Only an approximation for a
// strongly curved cutter (the extrusion direction is one fixed vector,
// not a per-point normal offset) - the same honest tradeoff RegionSlab/
// SlabFromTrimmedPlane and OffsetSrf Solid=Yes already accept elsewhere
// in this codebase (cmd_solidtools.cpp, cmd_surface.cpp) for turning an
// open cap into a solid. Returns nullopt if `cap` has no faces, is
// degenerate (zero net normal, e.g. a folded or self-cancelling patch)
// or ExtrudeCappedSolid rejects it (no clean single boundary loop).
std::optional<kernel::Mesh> SolidifyOpenCutter(const kernel::Mesh& cap, const kernel::BoundingBox& span) {
  if (cap.FaceCount() == 0) return std::nullopt;
  const ON_Mesh& raw = cap.raw();
  ON_3dVector normal_sum(0, 0, 0);
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    const ON_3fPoint& a = raw.m_V[f.vi[0]];
    const ON_3fPoint& b = raw.m_V[f.vi[1]];
    const ON_3fPoint& c = raw.m_V[f.vi[2]];
    normal_sum += ON_CrossProduct(ON_3dVector(b - a), ON_3dVector(c - a));
    if (f.IsQuad()) {
      const ON_3fPoint& d = raw.m_V[f.vi[3]];
      normal_sum += ON_CrossProduct(ON_3dVector(c - a), ON_3dVector(d - a));
    }
  }
  if (normal_sum.Length() <= 0) return std::nullopt;
  normal_sum.Unitize();
  const kernel::BoundingBox cap_bb = cap.GetBoundingBox();
  kernel::BoundingBox combined = span;
  combined.min.x = std::min(combined.min.x, cap_bb.min.x); combined.min.y = std::min(combined.min.y, cap_bb.min.y); combined.min.z = std::min(combined.min.z, cap_bb.min.z);
  combined.max.x = std::max(combined.max.x, cap_bb.max.x); combined.max.y = std::max(combined.max.y, cap_bb.max.y); combined.max.z = std::max(combined.max.z, cap_bb.max.z);
  const double reach = std::max((combined.max - combined.min).Length(), 1.0) * 2.0;
  try {
    kernel::Mesh solid = kernel::Mesh::ExtrudeCappedSolid(cap, normal_sum * reach);
    if (solid.FaceCount() == 0 || !solid.IsClosedManifold()) return std::nullopt;
    if (solid.Volume() < 0) solid = solid.FlipNormals();
    return solid;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// SplitByObject: splits solids by one or more arbitrary cutting objects
// (other solids, or open surfaces/meshes), keeping both resulting pieces -
// the general cutting-object case that BooleanSplit/MeshSplit/
// MeshBooleanSplit above don't cover (those only cut by a plane through two
// picked points; see PARITY_MAP.md kernel:features "Split body with an
// arbitrary surface / solid cutter"). Every cutting object already a
// closed solid is used as-is; an open one is solidified first
// (SolidifyOpenCutter above), then every cutter is unioned into one, the
// same "merge all cutters into a single rigid tool" approach HoleArray
// (cmd_solidtools.cpp) uses for multiple hole centres. Each target then
// gets cut by that one merged tool: BooleanCombine(target, tool,
// Intersection) is the piece inside the cutter, BooleanCombine(target,
// tool, Difference) is the piece outside it. A target only actually got
// split if BOTH pieces come back non-empty (the cutter genuinely crosses
// its boundary). If the cutter misses the target entirely, Difference
// alone comes back non-empty (the whole, untouched target - subtracting
// nothing changes nothing) while Intersection is empty; symmetrically, a
// cutter that fully encloses the target leaves Intersection non-empty and
// Difference empty. Only requiring "not both empty" here would treat both
// of those as a successful split and needlessly re-mesh the target and
// consume the cutter for zero real effect, so this requires both halves
// to be non-empty, matching Rhino's own Split (reports "no intersection
// found" and leaves the target and the cutter alone).
class SplitByObjectCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select solids to split"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_targets_) {
      target_ids_ = ids;
      have_targets_ = true;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      WantObjects("Select cutting objects (solids or surfaces)");
      accept_preselection = false;
      return;
    }
    Run(ctx, ids);
    Finish();
  }
  void Run(CommandContext& ctx, const std::vector<ObjectId>& cutter_ids) {
    // Try a genuine B-rep solid-by-solid split first, ahead of the mesh
    // pipeline below - PARITY_MAP.md's own "kernel: Boolean operations"
    // category names this command specifically as "a general cutting-object
    // split with true KeepAll semantics, but it is app-level mesh-boolean,
    // not [the Keep/split options] item's B-rep solid-by-solid split", even
    // though the kernel-level engine it needs (kernel::SplitBrepBySolid/
    // SplitBrepByManySolids, boolean_general.h/.cpp) already exists and is
    // already tested - it simply had zero callers anywhere in dino8-app.
    // Only attempted when every target AND every cutter is already a
    // plain, single-lump, closed (ON_Brep::IsSolid()) ObjectKind::Brep -
    // the same eligibility TryExactBrepBoolean above uses for the same
    // reason (PlanarFaces()-shaped engines happily accept an open
    // planar-faced shell and would silently misclassify it). All-or-
    // nothing across every selected target, mirroring
    // TryExactBrepBoolean's own group semantics: if any target/cutter
    // isn't eligible, or the kernel itself throws (BooleanCombineGeneral's
    // own disclosed scope limits - genus-0 faces, one crossing component
    // per opposing face pair, no self-crossing chains), or either
    // resulting half of any target comes back with zero faces (the cutter
    // missed or fully enclosed that target - the same "both halves must be
    // non-empty" rule the mesh path below already enforces), this falls
    // through silently to the existing mesh pipeline for every target,
    // unchanged.
    auto collect_solid_breps = [&](const std::vector<ObjectId>& ids, std::vector<kernel::Brep>& out) -> bool {
      for (ObjectId id : ids) {
        const SceneObject* o = ctx.Doc().Find(id);
        if (!o || o->kind != ObjectKind::Brep || !o->brep || !o->brep->raw().IsSolid()) return false;
        out.push_back(*o->brep);
      }
      return !out.empty();
    };
    std::vector<kernel::Brep> exact_targets, exact_cutters;
    if (collect_solid_breps(target_ids_, exact_targets) && collect_solid_breps(cutter_ids, exact_cutters)) {
      struct BrepResult { ObjectId id; int layer; kernel::Brep outside, inside; };
      std::vector<BrepResult> brep_results;
      bool all_ok = true;
      for (size_t i = 0; i < target_ids_.size() && all_ok; ++i) {
        try {
          auto [outside, inside] = kernel::SplitBrepByManySolids(exact_targets[i], exact_cutters);
          if (outside.raw().m_F.Count() == 0 || inside.raw().m_F.Count() == 0) { all_ok = false; break; }
          const SceneObject* o = ctx.Doc().Find(target_ids_[i]);
          brep_results.push_back({target_ids_[i], o ? o->layer_index : 0, std::move(outside), std::move(inside)});
        } catch (const std::exception&) {
          all_ok = false;
        }
      }
      if (all_ok && !brep_results.empty()) {
        ctx.Doc().BeginChange("SplitByObject");
        for (ObjectId id : cutter_ids) ctx.Doc().Remove(id);
        int made = 0;
        for (BrepResult& res : brep_results) {
          ctx.Doc().Remove(res.id);
          for (kernel::Brep* piece : {&res.outside, &res.inside}) {
            SceneObject s = SceneObject::MakeBrep(*piece);
            s.layer_index = res.layer;
            ctx.Doc().Add(std::move(s));
            ++made;
          }
        }
        ctx.Print("SplitByObject: " + std::to_string(brep_results.size()) + " solid(s) split into " + std::to_string(made) +
                   " piece(s) (exact B-rep, no tessellation)");
        return;
      }
      // Not every target split cleanly via the exact engine (an ineligible
      // operand was never collected here in the first place) - fall
      // through to the mesh pipeline below for every target, same as if
      // the exact path had never been tried.
    }

    std::vector<std::pair<ObjectId, kernel::Mesh>> targets;
    kernel::BoundingBox span;
    bool have_span = false;
    for (ObjectId id : target_ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!m || !m->IsClosedManifold()) { ctx.Warn("SplitByObject: object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      const kernel::BoundingBox bb = m->GetBoundingBox();
      if (!have_span) { span = bb; have_span = true; }
      else {
        span.min.x = std::min(span.min.x, bb.min.x); span.min.y = std::min(span.min.y, bb.min.y); span.min.z = std::min(span.min.z, bb.min.z);
        span.max.x = std::max(span.max.x, bb.max.x); span.max.y = std::max(span.max.y, bb.max.y); span.max.z = std::max(span.max.z, bb.max.z);
      }
      targets.push_back({id, *m});
    }
    if (targets.empty()) { ctx.Warn("SplitByObject: no closed solids to split"); return; }

    std::optional<kernel::Mesh> tool;
    std::vector<ObjectId> used_cutter_ids;
    for (ObjectId id : cutter_ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> cm = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!cm || cm->FaceCount() == 0) continue;
      std::optional<kernel::Mesh> solidified = cm->IsClosedManifold() ? cm : SolidifyOpenCutter(*cm, span);
      if (!solidified) { ctx.Warn("SplitByObject: cutting object " + std::to_string(id) + " could not be turned into a cutting solid; skipped"); continue; }
      used_cutter_ids.push_back(id);
      if (!tool) { tool = solidified; continue; }
      try { tool = kernel::BooleanCombine(*tool, *solidified, kernel::BooleanOp::Union); }
      catch (const std::exception&) { /* keep the previous tool; one bad cutter shouldn't lose the rest */ }
    }
    if (!tool) { ctx.Warn("SplitByObject: no usable cutting objects"); return; }

    // Compute every target's split result BEFORE touching the document -
    // if the cutting object(s) don't actually split any target in two, this
    // command must leave the document exactly as it found it (matching
    // BooleanDifference/BooleanIntersection's own "no effect" contract on a
    // non-intersecting pair, and Rhino's own Split). Deleting the cutter
    // unconditionally up front, and treating "not both empty" as a genuine
    // split, used to needlessly re-mesh the target and consume the user's
    // cutting geometry even when the cutter merely missed the target (or
    // fully enclosed it) - only ONE of Intersection/Difference is ever
    // empty in those cases, not both, so that used to read as "1 solid(s)
    // split into 1 piece(s)" for a pair of objects that never touched.
    struct Result { ObjectId id; int layer; kernel::Mesh inside, outside; };
    std::vector<Result> results;
    for (auto& [id, mesh] : targets) {
      const SceneObject* o = ctx.Doc().Find(id);
      const int layer = o ? o->layer_index : 0;
      std::optional<kernel::Mesh> inside, outside;
      try { kernel::Mesh r = kernel::BooleanCombine(mesh, *tool, kernel::BooleanOp::Intersection); if (r.FaceCount() > 0) inside = r; }
      catch (const std::exception&) {}
      try { kernel::Mesh r = kernel::BooleanCombine(mesh, *tool, kernel::BooleanOp::Difference); if (r.FaceCount() > 0) outside = r; }
      catch (const std::exception&) {}
      // A real split needs BOTH halves to be non-empty - the cutter must
      // actually cross the target's boundary, not merely miss it (outside
      // == the whole target, inside empty) or fully enclose it (the
      // reverse) - see this command's own class-level doc comment above.
      if (!inside || !outside) { ctx.Warn("SplitByObject: object " + std::to_string(id) + " does not intersect the cutting object(s)"); continue; }
      results.push_back({id, layer, std::move(*inside), std::move(*outside)});
    }
    if (results.empty()) { ctx.Warn("SplitByObject: nothing intersects the cutting object(s); the document is unchanged"); return; }

    ctx.Doc().BeginChange("SplitByObject");
    // The cutting object(s) are consumed into the split, same as
    // BooleanDifference/BooleanIntersection consume both of their operands
    // (RunBoolean above) - not left behind as leftover geometry - but only
    // now that at least one target actually got split by them.
    for (ObjectId id : used_cutter_ids) ctx.Doc().Remove(id);
    int made = 0;
    for (Result& res : results) {
      ctx.Doc().Remove(res.id);
      for (kernel::Mesh* piece : {&res.inside, &res.outside}) {
        SceneObject s = SceneObject::MakeMesh(*piece);
        s.layer_index = res.layer;
        ctx.Doc().Add(std::move(s));
        ++made;
      }
    }
    ctx.Print("SplitByObject: " + std::to_string(results.size()) + " solid(s) split into " + std::to_string(made) + " piece(s) (mesh boolean; results are meshes)");
  }
  std::vector<ObjectId> target_ids_;
  bool have_targets_ = false;
};

// MeshSmooth: smooths and refines with typed Strength (0-1, softens creases)
// and MinSharpAngle options and a computed default target edge length,
// unlike the previous fixed-parameter pass.
class MeshSmoothCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Strength", "0", {}, true, false}, {"MinSharpAngle", "52.5", {}, true, false}};
    WantObjects("Select meshes to smooth");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    char* e = nullptr;
    double d = std::strtod(v.c_str(), &e);
    if (!e || *e) return;
    if (n == "Strength") { strength_ = std::clamp(d, 0.0, 1.0); for (OptionSpec& o : options) if (o.name == "Strength") o.value = FormatNumber(strength_); }
    else if (n == "MinSharpAngle") { min_sharp_angle_ = d; for (OptionSpec& o : options) if (o.name == "MinSharpAngle") o.value = FormatNumber(min_sharp_angle_); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ctx.Doc().BeginChange("MeshSmooth");
    int done = 0;
    for (ObjectId id : ids) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind != ObjectKind::Mesh) continue;
      try {
        kernel::BoundingBox bb = o->mesh->GetBoundingBox();
        double len = (bb.max - bb.min).Length() / 30;
        *o->mesh = kernel::SmoothAndRefine(*o->mesh, len, min_sharp_angle_, strength_);
        o->InvalidateDisplay();
        ++done;
      } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
    }
    ctx.Print("MeshSmooth: " + std::to_string(done) + " mesh(es) (Strength=" + FormatNumber(strength_) + " MinSharpAngle=" + FormatNumber(min_sharp_angle_) + ")");
    Finish();
  }
  double strength_ = 0.0;
  double min_sharp_angle_ = 52.5;
};

}  // namespace

void RegisterBooleanCommands(CommandEngine& e) {
  Reg(e, "BooleanUnion", Make<BooleanCommand>(kernel::BooleanOp::Union, "BooleanUnion", false, /*try_exact_brep=*/true));
  Reg(e, "BooleanDifference", Make<BooleanCommand>(kernel::BooleanOp::Difference, "BooleanDifference", true, /*try_exact_brep=*/true));
  Reg(e, "BooleanIntersection", Make<BooleanCommand>(kernel::BooleanOp::Intersection, "BooleanIntersection", true, /*try_exact_brep=*/true));
  Reg(e, "Boolean2Objects", Make<Boolean2ObjectsCommand>());
  Reg(e, "Imprint", Make<ImprintCommand>(), CommandStatus::Implemented,
      "Splits the target object's faces wherever they cross the tool object, removing no material from either (Parasolid/ACIS IMPRINT) - the tool is left unchanged.");
  Reg(e, "MutualImprint", Make<MutualImprintCommand>(), CommandStatus::Implemented,
      "Like Imprint, but both objects imprint each other and both are replaced by their own split result.");
  Reg(e, "SplitBySheet", Make<SplitBySheetCommand>(), CommandStatus::Implemented,
      "Splits a closed solid into the two real B-rep pieces on either side of an open cutting sheet (Brep or Surface) - the sheet is left unchanged.");
  Reg(e, "TrimSheetBySolid", Make<TrimSheetBySolidCommand>(), CommandStatus::Implemented,
      "Trims an open sheet (Brep or Surface) down to the portion inside, or with KeepInside=No outside, a closed solid - the solid is left unchanged.");
  Reg(e, "MeshBooleanUnion", Make<BooleanCommand>(kernel::BooleanOp::Union, "MeshBooleanUnion", false));
  Reg(e, "MeshBooleanDifference", Make<BooleanCommand>(kernel::BooleanOp::Difference, "MeshBooleanDifference", true));
  Reg(e, "MeshBooleanIntersection", Make<BooleanCommand>(kernel::BooleanOp::Intersection, "MeshBooleanIntersection", true));
  // "Split" itself lives in cmd_curveedit.cpp (curves by curves); it delegates solids to BooleanSplit.
  Reg(e, "BooleanSplit", Make<SplitPlaneCommand>());
  Reg(e, "MeshSplit", Make<SplitPlaneCommand>());
  Reg(e, "MeshBooleanSplit", Make<SplitPlaneCommand>());
  Reg(e, "SplitByObject", Make<SplitByObjectCommand>(), CommandStatus::Implemented,
      "Splits solids by one or more cutting solids or open surfaces (extended into a solid), keeping both resulting pieces - the general cutting-object BooleanSplit that BooleanSplit itself (a plane through two points only) does not cover.");
  Reg(e, "WireCut", Make<WireCutCommand>());
  // ReduceMesh: superseded, dead code (RegisterRemeshCommands registers the
  // real target-count-driven ReduceMesh afterwards; see
  // Application::RegisterCommands).
  Reg(e, "MeshSmooth", Make<MeshSmoothCommand>());
  Reg(e, "SplitDisjointMesh", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("SplitDisjointMesh");
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o || o->kind != ObjectKind::Mesh) continue; std::vector<kernel::Mesh> parts = kernel::Decompose(*o->mesh); if (parts.size() < 2) continue; int layer = o->layer_index; ctx.Doc().Remove(id); for (const kernel::Mesh& p : parts) { SceneObject s = SceneObject::MakeMesh(p); s.layer_index = layer; ctx.Doc().Add(std::move(s)); } }
      }));
  Reg(e, "Weld", OnSelection("Select meshes to weld", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Weld");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { *o->mesh = kernel::Mesh::MergeAndWeld({*o->mesh}, ctx.Settings().absolute_tolerance); o->InvalidateDisplay(); } }
      }));
  Reg(e, "UnifyMeshNormals", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("UnifyMeshNormals");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { o->mesh->raw().ComputeFaceNormals(); o->mesh->raw().ComputeVertexNormals(); o->InvalidateDisplay(); } }
      }));
  Reg(e, "RebuildMeshNormals", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { o->mesh->raw().ComputeFaceNormals(); o->mesh->raw().ComputeVertexNormals(); o->InvalidateDisplay(); } }
      }));
  Reg(e, "TriangulateMesh", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("TriangulateMesh");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { o->mesh->raw().ConvertQuadsToTriangles(); o->InvalidateDisplay(); } }
      }));
  Reg(e, "QuadrangulateMesh", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("QuadrangulateMesh");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { o->mesh->raw().ConvertTrianglesToQuads(ON_PI / 90.0, 0.0); o->InvalidateDisplay(); } }
      }));
  Reg(e, "CullDegenerateMeshFaces", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("CullDegenerateMeshFaces");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { int n = o->mesh->raw().CullDegenerateFaces(); ctx.Print("Removed " + std::to_string(n) + " degenerate face(s)"); o->InvalidateDisplay(); } }
      }));
  Reg(e, "CheckMesh", OnSelection("Select meshes to check", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Mesh) continue;
          const bool closed = o->mesh->IsClosedManifold();
          ctx.Print("Mesh " + std::to_string(id) + ": " + std::to_string(o->mesh->VertexCount()) + " vertices, " + std::to_string(o->mesh->FaceCount()) + " faces, " + (closed ? "closed manifold" : "open or non-manifold"));
          if (closed) {
            try { ctx.Print("  degenerate triangles: " + std::to_string(kernel::CountDegenerateTriangles(*o->mesh))); }
            catch (const std::exception& ex) { ctx.Warn(ex.what()); }
          }
        }
      }));
  Reg(e, "ConvexHull", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<Point3d> pts;
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o) continue; if (o->kind == ObjectKind::Point) pts.push_back(o->point); else { o->EnsureDisplay(0.05, 0.1); const DisplayCache& d = o->Display(); for (size_t i = 0; i + 2 < d.lines.size(); i += 3) pts.emplace_back(d.lines[i], d.lines[i + 1], d.lines[i + 2]); for (size_t i = 0; i + 5 < d.triangles.size(); i += 6) pts.emplace_back(d.triangles[i], d.triangles[i + 1], d.triangles[i + 2]); } }
        if (pts.size() < 4) { ctx.Warn("Need at least four points"); return; }
        try { AddObject(ctx, SceneObject::MakeMesh(kernel::ConvexHull(pts)), "ConvexHull"); } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
      }));
}

}  // namespace dino8::app
