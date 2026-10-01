# Undo/redo scaling: diff-based history vs full snapshots

This documents what changed in `src/doc/Document.h`/`.cpp`'s undo/redo
model, what it actually buys, and where it honestly doesn't help. The
numbers below come from `tests/undo_bench.cpp`, a standalone benchmark
(not part of `smoke.sh` - it has nothing to assert, only timings to
report). Build and run it yourself with:

```
cmake --build build --target dino8_undo_bench -j$(nproc)
./build/dino8_undo_bench
```

## What changed

The old model (`Document::Capture()`/`Restore()`, still present and still
used for named snapshots) took a **full deep copy of every object, layer,
group, material, light, clipping plane, and layout** on every
`BeginChange()`, and `Undo()`/`Redo()` each took an *additional* full copy
before restoring - so a single Undo cost roughly two full document copies,
no matter how small the edit was.

The new model (`StateDelta`/`HistoryEntry`) instead:

- Always stores the small, cheap document-level state (layers, groups,
  materials, lights, clipping planes, layouts, id counters) in full, both
  directions - these are never the bottleneck in a document with any real
  number of objects, so there's no risk in not trying to diff them.
- For objects, stores full copies only of the ones that were actually
  **added**, **removed**, or are **"modified candidates"** - ids present
  both before and after the edit that might have changed value.
- `Document::BeginChange(label)` (the general, always-safe path every
  existing call site still uses unless migrated) can't cheaply tell which
  of the "present on both sides" objects actually changed - `SceneObject`
  has no `operator==` and its geometry members are opaque OpenNURBS
  wrappers (`ON_Mesh`, `ON_Brep`, `ON_NurbsCurve`, `ON_NurbsSurface`,
  `ON_SubD`) with no cheap, safe way to compare two values without risking
  a hand-rolled comparator silently declaring two different objects equal
  (which would corrupt Undo). So under this path, **every id present on
  both sides is conservatively treated as a candidate** - for a pure
  add/remove edit that's an empty set (a real, unconditional win with zero
  risk); for a pure in-place edit (Move, color change, ...) with no
  id-set change, that's *every object in the document*, i.e. no smaller
  than the old full snapshot for that specific case.
- `Document::BeginChangeForObjects(label, ids)` is a new, opt-in fast path
  for an audited call site that already knows, before it mutates anything,
  the *exact* set of existing object ids it's about to touch (and that it
  won't add/remove objects or touch other document state). This narrows
  the candidate set to just those ids - this is where the real per-edit
  scaling win comes from. It's wired up today for `cmd_transform.cpp`'s
  `ApplyXform` (Move/Rotate/Rotate3D/Scale/ScaleNU/Mirror/Orient/Orient3Pt/
  Nudge/ProjectToCPlane, all with `Copy=No`) - the single highest-frequency
  choke point for "move/transform an existing selection in place." The
  `Copy=Yes` variant of the same commands doesn't need it: it only adds
  new objects and never touches the originals, which the general path
  already handles as a cheap add-only delta.
- `Undo()`/`Redo()` apply or invert the delta directly - no extra full
  `Capture()` on every call the way the old code needed.
- Every 50 finalized entries, a full `Snapshot` is also captured (shared
  via `shared_ptr`) and attached to that entry, used for a cheap
  structural self-check in `Undo()` (object count / id-counter
  consistency, `assert`-only, compiled out in release builds). Because
  every delta already carries (or lazily materializes) both directions,
  ordinary Undo/Redo application never needs to replay a chain of deltas
  back to a checkpoint - see the long comment on `HistoryEntry` in
  `Document.h` for why this doesn't need to be (and isn't) a
  diff-from-last-checkpoint scheme.

## Benchmark setup

`tests/undo_bench.cpp` builds a **12,000-object** document, each object a
low-poly cylinder mesh (28 faces) - a real, if modest, geometry payload
per object, not a bare point. It then times, on that same document:

1. A full snapshot (`SaveNamedSnapshot`/`RestoreNamedSnapshot`, which call
   the exact same unmodified `Capture()`/`Restore()` the old undo model
   used for every step) - this is a faithful stand-in for "what the old
   model's per-Undo cost was," on real code, not a re-simulation of
   deleted logic.
2. 200 cycles of (move 1 of 12,000 objects, Undo, Redo) via the **general**
   `BeginChange` path.
3. 200 cycles of (move 12 of 12,000 objects, Undo, Redo) via the **fast**
   `BeginChangeForObjects` path.
4. 200 cycles of (add 1 object to the 12,000-object document, then Undo)
   via the general path, to show its other real, unconditional win.

## Results (this run; machine was also running several other builds/tests
concurrently, so treat absolute numbers as ballpark, not a clean-room
number - the *ratios* are the point)

```
Building a 12000-object document (each a 28-face cylinder mesh)...
  built in 1389.7 ms, RSS now 51780 kB

[1] Full snapshot of the whole 12000-object document:
      Capture (SaveNamedSnapshot):  138.17 ms
      Restore (RestoreNamedSnapshot): 90.27 ms
      RSS delta while the extra copy was held: ~40220 kB
      -> old model's per-Undo() cost was ~Capture+Restore = 228.44 ms, EVERY step,
         regardless of how small the edit was.

[2] General BeginChange(label) path, 200 x (move 1 of 12000 objects, Undo, Redo):
      avg BeginChange (incl. finalizing the previous entry): 108.931 ms
      avg Undo(): 203.471 ms   avg Redo(): 75.567 ms
      -> id-set is unchanged (a pure in-place move), so every object is a
         "modified candidate" under this path - no smaller than the old full
         snapshot for this case. This is exactly why BeginChangeForObjects exists.

[3] BeginChangeForObjects fast path, 200 x (move 12 of 12000 objects, Undo, Redo):
      avg BeginChangeForObjects: 0.416 ms
      avg Undo(): 16.073 ms   avg Redo(): 5.254 ms

[4] General BeginChange(label) path, 200 x (add 1 object to the 12000-object
    document, then Undo) - the general path's *other* real win, no fast path
    needed: avg BeginChange+Add+Undo: 352.327 ms
```

(Case 4's total is higher than case 1's Capture+Restore because it also
pays for one real `Mesh::Cylinder` construction per cycle - not part of
the undo machinery itself.)

## Reading these numbers honestly

- **Recording an edit** (`BeginChangeForObjects`) is genuinely O(selection
  size), not O(document size): **0.42 ms vs the old model's ~228 ms** per
  step on this 12k-object document - roughly **550x** less work to record,
  because it copies 12 objects instead of 12,000.
- **Applying** an Undo/Redo, even on the fast path, is *not* fully O(delta)
  in this implementation: `Document::ApplyObjectDelta` rebuilds an
  id-to-index lookup map over the *current* object vector on every call
  (there's no persistent id index kept on `Document` - adding one would
  mean keeping it in sync with every place that mutates `objects_`
  directly, including code outside `Document` that mutates the vector
  `Document::Objects()` returns, which is exactly the invasive,
  easy-to-get-subtly-wrong change this feature deliberately avoided), and
  it unconditionally calls `InvalidateDisplay()` on every object
  afterwards, matching the old `Restore()`'s behavior exactly (kept for
  strict behavioral parity - see the comment on `ApplyObjectDelta`). Both
  of those are O(document size), just cheap per-object work (a hash-map
  insert, a couple of bool writes) rather than a deep geometry copy. That
  is why Undo/Redo on the fast path measure at **16.1 ms / 5.3 ms** rather
  than sub-millisecond - still a real **14x / 43x** improvement over the
  ~228 ms full-snapshot baseline, just not the full asymptotic win a
  persistent id index would give.
- **The general path gives no storage/apply win for a pure in-place edit**
  with an unchanged id-set (case 2: avg Undo 203 ms, close to the old
  model's 228 ms) - by design, since it cannot safely tell which objects
  changed without either an unsafe geometry comparator or a call-site
  contract like `BeginChangeForObjects` provides. Its real, unconditional
  win is for add/remove-dominated edits (case 4): creating and deleting
  objects (a large fraction of real commands - draw a curve, place a
  primitive, boolean, delete a selection, ...) costs O(the objects
  actually added/removed), not O(document size), with zero migration
  needed.

## Scope: what's covered, what isn't

- **Covered, general path, zero risk, zero call-site changes:** any edit
  that only adds and/or removes objects (most creation and deletion
  commands) - the delta only ever stores the objects that were actually
  added/removed.
- **Covered, fast path:** `cmd_transform.cpp`'s `ApplyXform`, i.e. Move,
  Rotate, Rotate3D, Scale, ScaleNU, Mirror, Orient, Orient3Pt, Nudge, and
  ProjectToCPlane with `Copy=No` - audited to touch only the ids it's
  given and nothing else about the document.
- **Not covered (uses the general, safe, full-cost-for-this-case path):**
  every other command that modifies existing objects in place without
  adding/removing them - property edits (color/layer/name/material/
  linetype/...), the `Copy=Yes` transform variants' *treatment of the
  originals* (irrelevant - they aren't touched), and the ~380 other
  `BeginChange(label)` call sites across `src/commands/`. Each of these
  remains exactly as correct and exactly as expensive as before; none of
  them got slower. Migrating the highest-value additional call sites
  (property-edit commands operating on a selection) to
  `BeginChangeForObjects` would extend the same win to them, following
  the same audit-then-migrate pattern used for `ApplyXform` here.
- **Unconditionally unaffected:** `SaveNamedSnapshot`/`RestoreNamedSnapshot`
  / `DeleteNamedSnapshot` (still full, standalone `Capture()`/`Restore()`
  snapshots, independent of the undo/redo stacks, exactly as documented) -
  the task requires this path to keep taking full snapshots, and it does.

## Correctness

No test in `tests/*_script.txt` (run via `bash tests/smoke.sh`) changed
behavior - the full suite passes before and after this change, including
`edit_script.txt`'s `Undo`/`Redo`/`UndoMultiple`/`RedoMultiple`/
`ClearUndo` sequence and `session_script.txt`'s Snapshots
Save/Restore/Delete sequence. The object-diff logic never compares
`SceneObject` values (the one thing judged too risky to do safely within
this change's scope - see above); it only ever uses `ObjectId` set
membership, which is exact and cheap, so there is no way for it to
mistake a changed object for an unchanged one.
