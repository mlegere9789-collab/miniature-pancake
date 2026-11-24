# Grasshopper plugin parity rows: coverage

Parity Register rows for third-party Grasshopper plugins (Grasshopper 1 core components are covered by another worker). One row is one normalized user outcome. Files: `plugins-<plugin>.jsonl`, validated against `parity/schema/row.schema.json` (JSON Schema draft-07). All rows are `tier_now: None`, `status: open`, `evidence: null`.

## Per-plugin summary

| Plugin file | ID range | Rows | Target Best | Rows with notes | Doc hosts cited (rows) |
|---|---|---|---|---|---|
| alpaca4d | ALPACA4D-6001 .. 6016 | 16 | 10 | 0 | github.com (15), alpaca4d.gitbook.io (1) |
| anemone | ANEMONE-6701 .. 6710 | 10 | 2 | 10 | discourse.mcneel.com (10) |
| biomorpher | BIOMORPH-6501 .. 6511 | 11 | 3 | 11 | github.com (8), discourse.mcneel.com (3) |
| butterfly-swift | BFLY-5601 .. 5615 | 15 | 4 | 0 | github.com (13), www.food4rhino.com (2) |
| climatestudio | CLIMATE-5401 .. 5420 | 20 | 11 | 0 | discourse.mcneel.com (20) |
| clipper | CLIPPER-7701 .. 7710 | 10 | 0 | 0 | github.com (9), discourse.mcneel.com (1) |
| cocoon | COCOON-7101 .. 7108 | 8 | 1 | 8 | parametrichouse.com (6), www.grasshopper3d.com (2) |
| dendro | DENDRO-6101 .. 6112 | 12 | 1 | 0 | github.com (6), discourse.mcneel.com (6) |
| dragonfly | DRAGONFL-5101 .. 5127 | 27 | 6 | 0 | docs.ladybug.tools (27) |
| eddy3d | EDDY3D-5501 .. 5520 | 20 | 10 | 0 | discourse.mcneel.com (20) |
| elefront | ELEFRONT-8101 .. 8112 | 12 | 0 | 0 | discourse.mcneel.com (12) |
| elk | ELK-7501 .. 7512 | 12 | 0 | 0 | github.com (11), discourse.mcneel.com (1) |
| firefly | FIREFLY-9501 .. 9512 | 12 | 1 | 0 | www.grasshopper3d.com (6), discourse.mcneel.com (4), github.com (2) |
| fologram | FOLOGRAM-9601 .. 9612 | 12 | 3 | 0 | ms-kb.msd.unimelb.edu.au (6), community.fologram.com (3), docs.fologram.com (3) |
| goat | GOAT-6601 .. 6606 | 6 | 5 | 6 | discourse.mcneel.com (6) |
| hal | HAL-9301 .. 9318 | 18 | 3 | 0 | muddyturnip.github.io (12), discourse.mcneel.com (6) |
| heron | HERON-7601 .. 7627 | 27 | 0 | 0 | github.com (27) |
| honeybee | HONEYBEE-5301 .. 5370 | 70 | 23 | 0 | docs.ladybug.tools (70) |
| hops-rhino-compute | HOPS-9701 .. 9719 | 19 | 6 | 0 | developer.rhino3d.com (19) |
| human-ui | HUMANUI-8401 .. 8411 | 11 | 0 | 1 | github.com (11) |
| ivy | IVY-8801 .. 8805 | 5 | 1 | 3 | discourse.mcneel.com (5) |
| kangaroo2 | KANGAROO-5801 .. 5829 | 29 | 15 | 0 | github.com (23), discourse.mcneel.com (6) |
| karamba3d | KARAMBA-5701 .. 5728 | 28 | 18 | 0 | manual.karamba3d.com (19), manual-1-3.karamba3d.com (3), manual-2.karamba3d.com (2) |
| kuka-prc | KUKAPRC-9201 .. 9214 | 14 | 3 | 0 | www.prc.robotsinarchitecture.org (7), prc.robotsinarchitecture.org (5), github.com (2) |
| ladybug | LADYBUG-5001 .. 5048 | 48 | 12 | 0 | docs.ladybug.tools (48) |
| lands-design | LANDS-8301 .. 8320 | 20 | 2 | 6 | discourse.mcneel.com (15), www.rhino3d.com (5) |
| lunchbox | LUNCHBOX-8601 .. 8616 | 16 | 2 | 2 | discourse.mcneel.com (16) |
| lyrebird | LYREBIRD-7901 .. 7912 | 12 | 0 | 12 | discourse.mcneel.com (11), graphisoft.com (1) |
| machina | MACHINA-9401 .. 9411 | 11 | 1 | 0 | github.com (11) |
| meshplus | MESHPLUS-7301 .. 7308 | 8 | 0 | 8 | www2.food4rhino.com (5), github.com (2), discourse.mcneel.com (1) |
| metahopper | METAHOP-8701 .. 8709 | 9 | 0 | 2 | discourse.mcneel.com (9) |
| millipede | MILLIPED-5901 .. 5912 | 12 | 7 | 0 | grasshopper3d.com (4), www.grasshopper3d.com (4), discourse.mcneel.com (4) |
| octopus | OCTOPUS-6201 .. 6212 | 12 | 9 | 11 | discourse.mcneel.com (11), www.food4rhino.com (1) |
| opossum | OPOSSUM-6401 .. 6411 | 11 | 7 | 10 | discourse.mcneel.com (9), www.food4rhino.com (2) |
| paneling-tools | PANELING-8501 .. 8512 | 12 | 0 | 3 | discourse.mcneel.com (12) |
| peacock | PEACOCK-9001 .. 9007 | 7 | 1 | 7 | discourse.mcneel.com (7) |
| plankton | PLANKTON-7401 .. 7410 | 10 | 0 | 9 | raw.githubusercontent.com (5), discourse.mcneel.com (5) |
| pollination | POLLINAT-5201 .. 5221 | 21 | 2 | 0 | github.com (21) |
| pufferfish | PUFFER-7001 .. 7014 | 14 | 0 | 14 | discourse.mcneel.com (14) |
| rhino-inside-revit | RIR-7801 .. 7831 | 31 | 0 | 0 | raw.githubusercontent.com (31) |
| robots | ROBOTS-9101 .. 9119 | 19 | 6 | 0 | github.com (19) |
| shapediver | SHAPEDIV-9801 .. 9818 | 18 | 2 | 0 | help.shapediver.com (11), github.com (7) |
| speckle | SPECKLE-8001 .. 8020 | 20 | 0 | 20 | docs.speckle.systems (14), github.com (6) |
| tree-sloth | TSLOTH-8901 .. 8905 | 5 | 0 | 3 | discourse.mcneel.com (5) |
| tt-toolbox | TTTOOLBX-6801 .. 6818 | 18 | 0 | 18 | www2.food4rhino.com (8), blog.rhino3d.com (4), www.grasshopper3d.com (4) |
| visualarq | VISARQ-8201 .. 8228 | 28 | 0 | 28 | discourse.mcneel.com (28) |
| volvox | VOLVOX-7201 .. 7208 | 8 | 2 | 8 | github.com (4), blog.rhino3d.com (3), www.grasshopper3d.com (1) |
| wallacei | WALLACEI-6301 .. 6316 | 16 | 9 | 15 | discourse.mcneel.com (16) |
| weaverbird | WEAVER-6901 .. 6909 | 9 | 0 | 9 | www.grasshopper3d.com (5), discourse.mcneel.com (4) |

Total: 819 rows, 188 with target Best.

## Source reliability (read before relying on rows)

Research ran from a restricted network. Many official documentation hosts did not resolve or returned 403/405 (Food4Rhino, grasshopper3d.com, several vendor sites). The `source.doc` URL is therefore not always a page that was opened in full:

- **Component pages read directly:** Honeybee, Ladybug and Dragonfly (docs.ladybug.tools component listings; per-component pages only for a few), Hops/Rhino.Compute (developer.rhino3d.com), Rhino.Inside.Revit and Plankton (README text), Heron and Pollination (public repositories).
- **Repository or forum evidence only:** Kangaroo 2 (community example repositories; there is no official component reference online), Alpaca4d and Dendro (repository file names and forum notes), Butterfly (repository wiki), Volvox, Cocoon, Weaverbird, Pufferfish, TT Toolbox, Mesh+ and most optimisation, utility and BIM plugins (forum category pages and release notes).
- **Search-summary based:** ClimateStudio, Eddy3D, Swift, Millipede, Karamba3D (the manual host was unreachable; component names came from search summaries and general FEM practice), part of TT Toolbox and VisualARQ. Rows that depend on this carry a `notes` field where the author flagged it, but not every row is flagged: treat rows citing only a forum category or a repository root as feature-level outcomes to be re-checked against official documentation.
- Hosts added to the environment allowlist on this run (manual.karamba3d.com, docs.speckle.systems, help.shapediver.com, solemma.com, docs.eddy3d.com, climatestudiodocs.com, visualarq.com, docs.hal-robotics.com, docs.pollination.solutions) have not yet been used for a second verification pass. That pass is the first follow-up.

## Plugin identity corrections found during research

- Swift is a separate OpenFOAM plugin (ODS Studio), not a successor to Butterfly; only feature-level rows were written.
- Cocoon is an iso-surface (marching cubes) mesher, not a point-cloud reconstructor. Volvox is a point-cloud plugin, not a voxel plugin.
- Plankton rows for Conway operators, subdivision and per-element properties describe capabilities the half-edge kernel enables; its README lists them as planned.

## Gaps and follow-ups

- Several plugins fall below the requested row counts (Weaverbird, Cocoon, Mesh+, Volvox, Ivy, Tree Sloth, Goat, Peacock) because documentation was thin; rows were not padded.
- Not yet covered: Ladybug Tools sub-plugins beyond Ladybug, Honeybee and Dragonfly (Honeybee-Radiance and Honeybee-Energy are folded into Honeybee), Lyrebird detail beyond component families, and plugins such as Crane, Starling and Bifocals.
- Next pass: re-run component-level reads for Karamba3D, Speckle, ShapeDiver, ClimateStudio and Eddy3D now that their hosts are allowed, and replace feature-level rows with component-level ones.
