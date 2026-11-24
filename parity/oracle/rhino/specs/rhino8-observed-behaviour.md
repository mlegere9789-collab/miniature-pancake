# Rhino 8 observed behaviour (Phase B; our own words; build 8.36.26296.17001; document tolerance 0.001)

Source: `golden/rhino8-8.36.26296.17001-2026-11-23.jsonl`, 200 cases, two full passes identical. All statements are about the public RhinoCommon
calls the runner makes, on geometry we authored. "Refused" means the call returned no result or raised; the exception type name is stored.

## Booleans (public Brep Boolean calls, tolerance 0.001)
* **Enclosed tool, inner solid fully inside outer solid:** both difference and intersection are refused, for sphere inside box
  (BOOL-BXSP-0010, -0011 difference; -0019, -0020 intersection) and for cylinder inside cylinder (BOOL-CYCY-0006, -0007, -0011, -0012).
  The call does not return the inner solid as the intersection, nor an outer solid with a void.
* **Identical solids:** union and intersection of two coincident boxes are refused (BOOL-BXBX-0001, -0013); difference is refused (BOOL-BXBX-0007).
* **Disjoint or merely touching tools:** difference of two spheres whose surfaces touch at a point or are apart is refused (BOOL-SPSP-0008, -0009, -0010);
  a box difference with a tool 0.01 beyond the face is refused (BOOL-BXBX-0012); intersection with a non-overlapping tool is refused (BOOL-BXBX-0018, BOOL-BXSP-0027).
* **Tangent contact:** sphere touching a box face at one point (centre 3.5, radius 1.5, box face at x = 2) is refused for difference and intersection (BOOL-BXSP-0017, -0026).
* **Overlapping tools succeed**, including a box difference whose tool shares a face-aligned edge (BOOL-BXBX-0008 .. -0011): volumes 22, 40, 63.9198, 64.
  The near-coincident case 3.99 keeps a thin sliver of volume 63.9198 with 8 faces; the exactly coincident case 4.0 has 6 faces (BOOL-BXBX-0010, -0011).
* Total refusals: 21 of 70 Boolean cases. Dino 8 should reproduce the refusal classes, not invent a result where Rhino returns none.

## Fillet and chamfer of all edges of a 2 x 2 x 2 box
* Fillet radius 0.99 succeeds (26 faces, volume 3.92082) (BLND-BOX-0004). Radius 1.0 and 1.01 are refused (BLND-BOX-0005, -0006).
* Chamfer distance 0.05 succeeds (26 faces) (BLND-BOX-0007). Distance 1.0 returns a brep that is flagged valid and not solid, with 32 faces, volume 282.76 and a bounding box
  reaching 22.2: a degenerate result, not a usable solid (BLND-BOX-0011). Distance 1.01 is refused (BLND-BOX-0012).
  The golden numbers for BLND-BOX-0011 should be treated as a hazard (do not match them); see AUDIT notes.

## Offset of closed solids
* The public offset call with the solid flag set returned an empty array for all 18 offset cases (closed boxes, spheres and the other closed primitives in OFFS-SOL-0001 .. -0018);
  with the flag clear it returns the single closed offset brep.
  The runner uses the flag-clear form. Sphere radius 3 offset by 0.1 gives area 120.7628, volume 124.7883, a single face (OFFS-SOL-0003); the closed forms agree.
* The bounding box reported for the offset sphere is far larger than the sphere (x reaches 6 pi, y 1.5 pi in OFFS-SOL-0003) although area and volume are correct.
  Dino 8 should not copy this; use the geometric extent and mark these bbox fields non-comparable.
* Box offsets are exact in size and volume (OFFS-SOL-0010: volume 60, area 94).

## Plane and sphere
* A plane that misses the sphere returns "no intersection" (INTS-PLSP-0001, -0008); a plane touching at z = r returns a point (INTS-PLSP-0002, -0006);
  a plane just inside returns a circle of circumference 2 pi sqrt(r^2 - z^2) (INTS-PLSP-0003 .. -0005). At z = 1.001 for r = 1 the answer is still "no intersection" (INTS-PLSP-0007).

## Tolerance facts
* Doc tolerance set to 0.001 is read back as 0.001. Coordinates of box and fillet results carry noise down to about 1e-9 around the exact values (BLND-BOX-0001 .. -0004),
  so bounding-box comparison must use the document tolerance, not equality.
