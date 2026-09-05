// Editing commands: Delete, Undo/Redo, Group, Hide/Show/Lock, Join,
// Explode, Rebuild, Reverse, Offset...
#include "commands/cmd_common.h"
#include "io/File3dm.h"

namespace dino8::app {

namespace {

void HideShow(CommandContext& ctx, const std::vector<ObjectId>& ids, bool visible, const char* label) {
  ctx.Doc().BeginChange(label);
  for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->visible = visible; if (!visible) o->selected = false; }
}

}  // namespace

void RegisterEditCommands(CommandEngine& e) {
  Reg(e, "Delete", OnSelection("Select objects to delete", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Delete");
        for (ObjectId id : ids) ctx.Doc().Remove(id);
        ctx.Print("Deleted " + std::to_string(ids.size()) + " object(s)");
      }));
  Reg(e, "Undo", Immediate([](CommandContext& ctx) { if (!ctx.Doc().Undo()) ctx.Print("Nothing to undo"); }));
  Reg(e, "Redo", Immediate([](CommandContext& ctx) { if (!ctx.Doc().Redo()) ctx.Print("Nothing to redo"); }));
  Reg(e, "UndoMultiple", Immediate([](CommandContext& ctx) { ctx.App().Panels().undo_multiple = true; }));
  Reg(e, "RedoMultiple", Immediate([](CommandContext& ctx) { ctx.App().Panels().redo_multiple = true; }));
  Reg(e, "ClearUndo", Immediate([](CommandContext& ctx) { ctx.Doc().ClearUndo(); ctx.Print("Undo history cleared"); }));
  Reg(e, "Group", OnSelection("Select objects to group", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Group");
        int g = ctx.Doc().CreateGroup(ids);
        ctx.Print("Group " + std::to_string(g) + " created with " + std::to_string(ids.size()) + " object(s)");
      }, 1));
  Reg(e, "Ungroup", OnSelection("Select groups to ungroup", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ctx.Doc().BeginChange("Ungroup"); ctx.Doc().Ungroup(ids); }));
  Reg(e, "AddToGroup", OnSelection("Select objects to add to the selected group", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        int g = -1;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->group_id >= 0) { g = o->group_id; break; }
        if (g < 0) { ctx.Warn("Selection contains no group"); return; }
        ctx.Doc().BeginChange("AddToGroup");
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) o->group_id = g;
      }));
  Reg(e, "RemoveFromGroup", OnSelection("Select objects to remove from their group", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ctx.Doc().BeginChange("RemoveFromGroup"); ctx.Doc().Ungroup(ids); }));
  Reg(e, "Hide", OnSelection("Select objects to hide", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { HideShow(ctx, ids, false, "Hide"); }));
  Reg(e, "Show", Immediate([](CommandContext& ctx) {
        ctx.Doc().BeginChange("Show");
        int n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) if (!o.visible) { o.visible = true; ++n; }
        ctx.Print("Showed " + std::to_string(n) + " object(s)");
      }));
  Reg(e, "ShowSelected", Immediate([](CommandContext& ctx) {
        ctx.Doc().BeginChange("ShowSelected");
        for (SceneObject& o : ctx.Doc().Objects()) if (!o.visible) { o.visible = true; o.selected = true; }
      }), CommandStatus::Partial, "Shows and selects all hidden objects.");
  Reg(e, "HideSwap", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("HideSwap"); for (SceneObject& o : ctx.Doc().Objects()) { o.visible = !o.visible; o.selected = false; } }));
  Reg(e, "Isolate", OnSelection("Select objects to isolate", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Isolate");
        for (SceneObject& o : ctx.Doc().Objects()) o.visible = std::find(ids.begin(), ids.end(), o.id) != ids.end();
      }));
  Reg(e, "Unisolate", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("Unisolate"); for (SceneObject& o : ctx.Doc().Objects()) o.visible = true; }));
  Reg(e, "Lock", OnSelection("Select objects to lock", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ctx.Doc().BeginChange("Lock"); for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->locked = true; o->selected = false; } }));
  Reg(e, "Unlock", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("Unlock"); for (SceneObject& o : ctx.Doc().Objects()) o.locked = false; }));
  Reg(e, "UnlockSelected", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("UnlockSelected"); for (SceneObject& o : ctx.Doc().Objects()) if (o.locked) { o.locked = false; o.selected = true; } }));
  Reg(e, "LockSwap", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("LockSwap"); for (SceneObject& o : ctx.Doc().Objects()) { o.locked = !o.locked; o.selected = false; } }));
  Reg(e, "Join", OnSelection("Select objects to join", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        // Curves: chain end-to-end into one polycurve (NURBS form). Surfaces/breps: append into one polysurface. Meshes: merge.
        std::vector<const SceneObject*> curves, breps, meshes;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          if (o->kind == ObjectKind::Curve) curves.push_back(o);
          else if (o->kind == ObjectKind::Brep || o->kind == ObjectKind::Surface) breps.push_back(o);
          else if (o->kind == ObjectKind::Mesh) meshes.push_back(o);
        }
        ctx.Doc().BeginChange("Join");
        if (curves.size() >= 2) {
          ON_PolyCurve pc;
          std::vector<const SceneObject*> remaining = curves;
          pc.Append(new ON_NurbsCurve(remaining[0]->curve->raw()));
          remaining.erase(remaining.begin());
          const double tol = ctx.Settings().absolute_tolerance * 10;
          bool progress = true;
          while (progress && !remaining.empty()) {
            progress = false;
            for (size_t i = 0; i < remaining.size(); ++i) {
              ON_NurbsCurve c = remaining[i]->curve->raw();
              if (c.PointAtStart().DistanceTo(pc.PointAtEnd()) <= tol) { pc.Append(new ON_NurbsCurve(c)); }
              else if (c.PointAtEnd().DistanceTo(pc.PointAtEnd()) <= tol) { c.Reverse(); pc.Append(new ON_NurbsCurve(c)); }
              else if (c.PointAtEnd().DistanceTo(pc.PointAtStart()) <= tol) { pc.Prepend(new ON_NurbsCurve(c)); }
              else if (c.PointAtStart().DistanceTo(pc.PointAtStart()) <= tol) { c.Reverse(); pc.Prepend(new ON_NurbsCurve(c)); }
              else continue;
              remaining.erase(remaining.begin() + static_cast<long>(i));
              progress = true;
              break;
            }
          }
          if (pc.Count() >= 2) {
            kernel::NurbsCurve k;
            if (CurveFromON(pc, k)) {
              SceneObject n = SceneObject::MakeCurve(k);
              n.layer_index = curves[0]->layer_index;
              for (const SceneObject* o : curves) if (std::find(remaining.begin(), remaining.end(), o) == remaining.end()) ctx.Doc().Remove(o->id);
              ctx.Doc().Add(std::move(n));
              ctx.Print("Joined " + std::to_string(pc.Count()) + " curves into one");
            }
          } else ctx.Warn("Curve ends do not meet");
        }
        if (breps.size() >= 2) {
          ON_Brep* b = new ON_Brep();
          for (const SceneObject* o : breps) {
            if (o->kind == ObjectKind::Brep) b->Append(o->brep->raw());
            else { ON_Brep tmp; ON_NurbsSurface* srf = new ON_NurbsSurface(o->surface->raw()); tmp.Create(srf); b->Append(tmp); }
          }
          JoinNakedEdges(*b, ctx.Settings().absolute_tolerance * 10);
          kernel::Brep k; k.raw() = *b; delete b;
          SceneObject n = SceneObject::MakeBrep(k);
          n.layer_index = breps[0]->layer_index;
          for (const SceneObject* o : breps) ctx.Doc().Remove(o->id);
          ctx.Doc().Add(std::move(n));
          ctx.Print("Joined " + std::to_string(breps.size()) + " surfaces into one polysurface");
        }
        if (meshes.size() >= 2) {
          std::vector<kernel::Mesh> ms;
          for (const SceneObject* o : meshes) ms.push_back(*o->mesh);
          SceneObject n = SceneObject::MakeMesh(kernel::Mesh::MergeAndWeld(ms, ctx.Settings().absolute_tolerance));
          n.layer_index = meshes[0]->layer_index;
          for (const SceneObject* o : meshes) ctx.Doc().Remove(o->id);
          ctx.Doc().Add(std::move(n));
          ctx.Print("Joined " + std::to_string(meshes.size()) + " meshes");
        }
      }, 2));
  Reg(e, "Explode", OnSelection("Select objects to explode", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Explode");
        int made = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          const int layer = o->layer_index;
          if (o->kind == ObjectKind::Brep) {
            const ON_Brep& b = o->brep->raw();
            for (int f = 0; f < b.m_F.Count(); ++f) {
              ON_Brep* face = b.DuplicateFace(f, true);
              if (!face) continue;
              kernel::Brep k; k.raw() = *face; delete face;
              SceneObject n = SceneObject::MakeBrep(k); n.layer_index = layer; ctx.Doc().Add(std::move(n)); ++made;
            }
            ctx.Doc().Remove(id);
          } else if (o->kind == ObjectKind::Curve) {
            // Split at kinks / polyline vertices.
            const ON_NurbsCurve& c = o->curve->raw();
            ON_SimpleArray<double> kinks;
            const int span = c.SpanCount();
            ON_SimpleArray<double> knots(span + 1);
            knots.SetCount(span + 1);
            c.GetSpanVector(knots.Array());
            std::vector<double> splits;
            for (int i = 1; i < span; ++i) {
              ON_3dVector t0 = c.TangentAt(knots[i] - 1e-6), t1 = c.TangentAt(knots[i] + 1e-6);
              if (ON_DotProduct(t0, t1) < std::cos(1.0 * ON_PI / 180.0)) splits.push_back(knots[i]);
            }
            if (splits.empty()) continue;
            double t0 = c.Domain().Min();
            splits.push_back(c.Domain().Max());
            for (double t1 : splits) {
              ON_NurbsCurve seg = c;
              if (seg.Trim(ON_Interval(t0, t1))) { kernel::NurbsCurve k; k.raw() = seg; SceneObject n = SceneObject::MakeCurve(k); n.layer_index = layer; ctx.Doc().Add(std::move(n)); ++made; }
              t0 = t1;
            }
            ctx.Doc().Remove(id);
          } else if (o->kind == ObjectKind::Mesh) {
            for (const kernel::Mesh& part : kernel::Decompose(*o->mesh)) { SceneObject n = SceneObject::MakeMesh(part); n.layer_index = layer; ctx.Doc().Add(std::move(n)); ++made; }
            ctx.Doc().Remove(id);
          }
        }
        ctx.Print("Exploded into " + std::to_string(made) + " object(s)");
      }));
  Reg(e, "Rebuild", OnSelection("Select curves or surfaces to rebuild", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Rebuild");
        for (ObjectId id : ids) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          if (o->kind == ObjectKind::Curve) {
            std::vector<Point3d> pts;
            const int n = 16;
            kernel::Interval d = o->curve->Domain();
            for (int i = 0; i < n; ++i) pts.push_back(o->curve->PointAt(d.min + (d.max - d.min) * i / (n - 1.0)));
            *o->curve = kernel::NurbsCurve::FromControlPoints(pts, 3);
            o->InvalidateDisplay();
          } else if (o->kind == ObjectKind::Surface) {
            std::vector<Point3d> grid;
            const int n = 12;
            kernel::Interval du = o->surface->Domain(0), dv = o->surface->Domain(1);
            for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) grid.push_back(o->surface->PointAt(du.min + (du.max - du.min) * i / (n - 1.0), dv.min + (dv.max - dv.min) * j / (n - 1.0)));
            *o->surface = kernel::NurbsSurface::FromControlGrid(grid, n, n, 3, 3);
            o->InvalidateDisplay();
          }
        }
      }), CommandStatus::Partial, "Rebuilds to 16 points (curves) / 12x12 (surfaces) degree 3; count options are planned.");
  Reg(e, "ChangeDegree", OnSelection("Select curves or surfaces", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("ChangeDegree");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (!o) continue; if (o->kind == ObjectKind::Curve) { o->curve->ElevateDegree(o->curve->Degree() + 1); o->InvalidateDisplay(); } else if (o->kind == ObjectKind::Surface) { o->surface->ElevateDegree(0, o->surface->DegreeU() + 1); o->surface->ElevateDegree(1, o->surface->DegreeV() + 1); o->InvalidateDisplay(); } }
      }), CommandStatus::Partial, "Raises degree by one; typed target degree is planned.");
  Reg(e, "Flip", OnSelection("Select curves, surfaces or meshes to flip", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Flip");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (!o) continue; if (o->kind == ObjectKind::Curve) o->curve->Reverse(); else if (o->kind == ObjectKind::Surface) o->surface->Reverse(0); else if (o->kind == ObjectKind::Mesh) *o->mesh = o->mesh->FlipNormals(); else if (o->kind == ObjectKind::Brep) o->brep->raw().Flip(); o->InvalidateDisplay(); }
      }));
  Reg(e, "Dir", OnSelection("Select objects to show direction", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o) continue; if (o->kind == ObjectKind::Curve) { kernel::Interval d = o->curve->Domain(); ctx.Print("Curve " + std::to_string(id) + ": start " + FormatPoint(o->curve->PointAt(d.min)) + " tangent " + FormatPoint(Point3d(o->curve->TangentAt(d.min)))); } else if (o->kind == ObjectKind::Surface) { ctx.Print("Surface " + std::to_string(id) + ": normal at centre " + FormatPoint(Point3d(o->surface->NormalAt((o->surface->Domain(0).min + o->surface->Domain(0).max) / 2, (o->surface->Domain(1).min + o->surface->Domain(1).max) / 2)))); } }
        ctx.Print("Use Flip to reverse direction.");
      }), CommandStatus::Partial, "Reports direction; interactive flip arrows are planned.");
  Reg(e, "Offset", OnSelection("Select curves to offset", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Offset");
        const double d = ctx.Settings().grid_spacing;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Curve) continue;
          std::vector<Point3d> pts;
          Vector3d n = ActiveNormal(ctx);
          for (double t : o->curve->SuggestedParameterValues(0.01)) { Vector3d tan = o->curve->TangentAt(t); Vector3d side = ON_CrossProduct(tan, n); side.Unitize(); pts.push_back(o->curve->PointAt(t) + side * d); }
          if (pts.size() >= 2) ctx.Doc().Add(SceneObject::MakeCurve(o->curve->IsLinear() ? PolylineCurve({pts.front(), pts.back()}) : kernel::NurbsCurve::FromControlPoints(pts, std::min(3, static_cast<int>(pts.size()) - 1))));
        }
      }), CommandStatus::Partial, "Offsets by one grid unit toward the CPlane right side; distance/side picking is planned.");
  Reg(e, "Extend", OnSelection("Select curves to extend", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Extend");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) { kernel::Interval d = o->curve->Domain(); double len = d.max - d.min; o->curve->Extend(d.min - len * 0.1, d.max + len * 0.1); o->InvalidateDisplay(); } }
      }), CommandStatus::Partial, "Extends both ends by 10% of the domain; picking the extension is planned.");
  Reg(e, "MakePeriodic", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("MakePeriodic");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) { o->curve->raw().MakePeriodicUniformKnotVector(); o->InvalidateDisplay(); } }
      }), CommandStatus::Partial);
  Reg(e, "Weight", OnSelection("Select curves or surfaces to make rational", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Weight");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) o->curve->MakeRational(); else if (o && o->kind == ObjectKind::Surface) o->surface->MakeRational(); }
      }), CommandStatus::Partial, "Makes objects rational; per-point weight editing is planned.");
  Reg(e, "PointsOn", OnSelection("Select objects to turn on control points", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->show_control_points = true; o->InvalidateDisplay(); } }));
  Reg(e, "PointsOff", Immediate([](CommandContext& ctx) { for (SceneObject& o : ctx.Doc().Objects()) if (o.show_control_points) { o.show_control_points = false; o.InvalidateDisplay(); } }));
  Reg(e, "SolidPtOn", OnSelection("Select polysurfaces", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->show_control_points = true; o->InvalidateDisplay(); } }), CommandStatus::Partial);
  Reg(e, "InsertKnot", OnSelection("Select curves or surfaces", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("InsertKnot");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (!o) continue; if (o->kind == ObjectKind::Curve) { kernel::Interval d = o->curve->Domain(); o->curve->InsertKnotAt((d.min + d.max) / 2); } else if (o->kind == ObjectKind::Surface) { kernel::Interval d = o->surface->Domain(0); o->surface->InsertKnotAt(0, (d.min + d.max) / 2); } o->InvalidateDisplay(); }
      }), CommandStatus::Partial, "Inserts a knot at the domain midpoint; picking is planned.");
  Reg(e, "SetObjectName", OnSelection("Select objects to name", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("SetObjectName");
        int i = 1;
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) o->name = "Object " + std::to_string(i++);
        ctx.Print("Named " + std::to_string(ids.size()) + " object(s). Edit names in the Properties panel.");
      }), CommandStatus::Partial, "Assigns sequential names; edit them in Properties.");
  Reg(e, "SetUserText", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { for (ObjectId id : ids) ctx.Doc().Select(id, true); ctx.App().Panels().properties = true; ctx.Print("Set user text in the Properties panel > Attribute User Text."); }), CommandStatus::Partial);
  Reg(e, "GetUserText", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) for (const auto& [k, v] : o->user_text) ctx.Print(std::to_string(id) + ": " + k + " = " + v); }));
  Reg(e, "DocumentUserText", Immediate([](CommandContext& ctx) { ctx.App().Panels().document_user_text = true; }));
  Reg(e, "GetDocumentUserText", Immediate([](CommandContext& ctx) { for (const auto& [k, v] : ctx.Doc().UserText()) ctx.Print(k + " = " + v); }));
  Reg(e, "BoxEdit", Immediate([](CommandContext& ctx) { ctx.App().Panels().box_edit = true; }));
  Reg(e, "Properties", Immediate([](CommandContext& ctx) { ctx.App().Panels().properties = true; }));
  Reg(e, "ObjectProperties", Immediate([](CommandContext& ctx) { ctx.App().Panels().properties = true; }));
  Reg(e, "CopyToClipboard", OnSelection("Select objects to copy", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::string path = ctx.App().ExeDir() + "/dino8_clipboard.3dm";
        Document sub;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) sub.Add(*o);
        sub.Layers() = ctx.Doc().Layers();
        std::string err;
        if (Save3dm(sub, path, err)) ctx.Print("Copied " + std::to_string(ids.size()) + " object(s) to the clipboard"); else ctx.Warn(err);
      }));
  Reg(e, "Cut", OnSelection("Select objects to cut", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::string path = ctx.App().ExeDir() + "/dino8_clipboard.3dm";
        Document sub;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) sub.Add(*o);
        sub.Layers() = ctx.Doc().Layers();
        std::string err;
        if (!Save3dm(sub, path, err)) { ctx.Warn(err); return; }
        ctx.Doc().BeginChange("Cut");
        for (ObjectId id : ids) ctx.Doc().Remove(id);
      }));
  Reg(e, "Paste", Immediate([](CommandContext& ctx) {
        std::string path = ctx.App().ExeDir() + "/dino8_clipboard.3dm";
        std::string err;
        if (!ctx.App().ImportFile(path, err)) ctx.Warn("Clipboard is empty");
      }));
}

}  // namespace dino8::app
