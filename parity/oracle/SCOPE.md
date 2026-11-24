# Dino 8 (LegeLabs) scope and roles: read this first (owner order, 2026-11-24)

**Mission.** Dino 8 must reach 100% parity with all EIGHT baseline programs AND with Enscape, which is built into Dino 8
under the product name **Outscape**. Nine baselines in total:

| # | Program | Register rows |
|---|---|---|
| 1 | Rhino 8 | 1,356 |
| 2 | Grasshopper (plus its plug-in ecosystem) | 773 |
| 3 | Blender | 2,987 |
| 4 | SketchUp | 635 |
| 5 | Autodesk Revit | 1,034 |
| 6 | Archicad | 924 |
| 7 | Autodesk Forma | 186 |
| 8 | AutoCAD | 2,765 |
| 9 | Enscape (ships in Dino 8 as **Outscape**) | not yet built: the coordinator builds the Enscape rows from public documentation |

Register total today: 10,660 rows (programs 1-8). Dino 8 licence (owner's words): free, open source, never sold for gain.

**Chain of command.** Mike (owner) -> the cloud coordinator session ("second in command": plan, Register, CI, verification,
implementer agents) -> the Observer (the local session "Rhino 8 clean-room oracle protocol").

**The Observer's job is evidence, for every program.** The Observer produces behavioural evidence from the owner's licensed
copies: scripted cases run through each program's PUBLIC scripting/API surface, recording numbers and flags only, plus
behaviour statements in our own words. The Observer does NOT write Dino 8 implementation code (the firewall in
rhino/CLEANROOM.md applies to all nine). Implementation stays with the cloud implementers, who read only the repository.

**Rules extended to all nine programs** (same as rhino/CLEANROOM.md):
* No decompiling, disassembling, IL/method-body inspection, memory dumps, reading installers/resources/strings/help files,
  copying vendor text, assets, icons, materials, shaders, UI strings, or touching licence checks.
* A separate G0 entry (owner decision, own-risk or counsel's answer) is required for EACH program before any harness runs
  against it, written in `parity/oracle/<program>/REVIEW_LOG.md`.
* Blender: its source is public but GPL. Do not copy or port Blender code into Dino 8 (GPL permits commercial sale, which
  conflicts with the owner's licence intent: counsel question). Behaviour evidence only.
* Enscape: do not copy its assets, materials, shader code, UI text or icons. Outscape is our own design validated only by
  measured outputs (render statistics such as luminance histograms, resolution, timing; never its internal data).

**What the Observer does now.**
1. Finish the Rhino smoke run and full corpus per rhino/LOCAL_SESSION_PROMPT.md (or rhino/run_here.py).
2. For each of the other eight programs, write `parity/oracle/<program>/PLAN.md` (DESIGN ONLY, no runs): which Register
   factories can be measured through that program's public API (Blender bpy, AutoCAD AutoLISP/.NET core console, SketchUp
   Ruby API, Revit API, Archicad API, Grasshopper component scripting, Forma web API, Enscape: rendered-output statistics
   and documented settings), case families, tolerance classes, and what cannot be measured headlessly. Order by row count:
   Blender, AutoCAD, Revit, Archicad, Grasshopper, SketchUp, Forma, Enscape.
3. Report what the owner must sign (G0 per program) and what installs or add-ins each harness needs.
