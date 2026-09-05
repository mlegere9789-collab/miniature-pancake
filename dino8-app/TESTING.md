# Dino 8 beta: what to test

Install from the GitHub pre-release attached to the pull request's CI run
(`Dino8Setup-0.1.0.exe` on Windows; `.deb` / `.tar.gz` on Linux; `.dmg` on
macOS). Nothing phones home, nothing asks for a licence.

## First launch
* Four viewports (Top, Perspective, Front, Right), Layers and Properties on
  the right, Command History at the bottom, toolbar on top, command line
  under it, object snaps and status toggles at the bottom.
* Drag panel tabs to re-dock or float them; the layout is remembered
  between runs (config folder: `%APPDATA%\Dino8`, `~/.config/dino8`,
  `~/Library/Application Support/Dino8`).

## Command line (Rhino muscle memory)
* Type `Box`, Enter, click two corners in Top, type `10`, Enter.
* Type `b` (alias for Box), `c` (Circle), `l` (Line), `co` (Copy), `m` (Move).
* Press Enter on an empty command line to repeat the last command; Esc cancels.
* Options appear as blue buttons after the prompt (e.g. `NumSides=8` in
  Polygon, `Copy=Yes` in Rotate); click them or type the option name.
* Coordinates: `10,5,0`, relative `@5,0`, polar `20<45`, or a bare distance
  while pointing the mouse in a direction.
* F1 opens the Command List with all 1055 Rhino 8 commands and whether each
  is Implemented, Partial or Planned; clicking one shows its full help.

## Modelling to try
* Solids: Box, Sphere, Cylinder, Cone, TCone, Torus, Tube, Pyramid, Plane.
* Curves: Line, Polyline, Curve, InterpCrv, Circle, Arc, Arc3Pt, Ellipse,
  Rectangle, Polygon (NumSides), Helix, Spiral, Offset, Fillet, Chamfer,
  FilletCorners, Trim, Split, Intersect, Divide, Rebuild, Join, Explode.
* Surfaces/solids from curves: ExtrudeCrv (closed planar curves become
  capped solids), Revolve, Loft, PlanarSrf, Cap.
* Booleans: BooleanUnion / Difference / Intersection / Split between any
  closed solids (box, sphere, cylinder, torus, extrusions, revolves).
* Transform: Move, Copy, Rotate, Scale/Scale1D/Scale2D, Mirror, Array,
  ArrayPolar, Orient3Pt, or just drag the Gumball (arrows move, rings
  rotate, cubes scale; Shift = 15 degree steps / uniform scale).
* Analysis: Distance, Length, Area, Volume, BoundingBox, What, List, Check.
* SubD: SubDBox/Sphere/Cylinder/Cone/Torus, ToSubD, ToNURBS, SubDivide.
* Meshes: Mesh, MeshBox/Sphere/..., MeshBoolean*, ReduceMesh, Weld,
  CheckMesh, SplitDisjointMesh.

## Viewports
* Right-drag orbits (Perspective) or pans (parallel views); Shift+right or
  middle-drag pans; wheel zooms toward the cursor; PgUp/PgDn zoom; arrow
  keys orbit; Home resets the view.
* Left-click selects; drag left-to-right for window select, right-to-left
  for crossing; Ctrl toggles; Shift adds. Right-click = Enter.
* Viewport title menu: set view, display mode (Wireframe, Shaded, Rendered,
  Ghosted, X-Ray, Technical, Artistic, Pen, Arctic, Monochrome), maximize.
* F7 grid, F8 ortho, F9 grid snap, F10 control points on, Esc deselect.

## Files
* Save / Open `.3dm` (round-trips layers, names, colours, user text,
  notes, named views, units) - try opening the file in Rhino too.
* Import/Export OBJ and STL. ViewCaptureToFile saves the active viewport
  as a BMP. Unsaved changes prompt on New / Open / Exit / window close.

## Known limits in this beta
* Annotation (Dim*, Text, Hatch, Leader), Blocks, Grasshopper, rendering
  and the remaining "Planned" commands open their reference help instead
  of running.
* Fillet/Chamfer are exact for lines and approximate for other curves.
* Native file dialogs are Windows-only; Linux/macOS use the built-in browser.

Please report anything that installs badly, crashes, or feels wrong.
