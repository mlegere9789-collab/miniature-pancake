# Grasshopper plugin parity rows: coverage

Parity Register rows for third-party Grasshopper plugins (Grasshopper 1 core components are covered by another worker). One row is one normalized user outcome. Files: `plugins-<plugin>.jsonl`, validated against `parity/schema/row.schema.json` (JSON Schema draft-07). All rows are `tier_now: None`, `status: open`, `evidence: null`.

## Per-plugin summary

| Plugin file | ID range | Rows | Target Best | Cites unconfirmed URL | Rows with notes | Doc hosts cited (rows) |
|---|---|---|---|---|---|---|
| alpaca4d | ALPACA4D-6001 .. 6016 | 16 | 10 | 1 | 1 | github.com (15), alpaca4d.gitbook.io (1) |
| anemone | ANEMONE-6701 .. 6710 | 10 | 2 | 10 | 10 | discourse.mcneel.com (10) |
| biomorpher | BIOMORPH-6501 .. 6511 | 11 | 3 | 11 | 11 | github.com (8), discourse.mcneel.com (3) |
| butterfly-swift | BFLY-5601 .. 5615 | 15 | 4 | 2 | 2 | github.com (13), www.food4rhino.com (2) |
| climatestudio | CLIMATE-5401 .. 5438 | 38 | 15 | 0 | 4 | climatestudiodocs.com (38) |
| clipper | CLIPPER-7701 .. 7710 | 10 | 0 | 0 | 0 | github.com (9), discourse.mcneel.com (1) |
| cocoon | COCOON-7101 .. 7108 | 8 | 1 | 8 | 8 | parametrichouse.com (6), www.grasshopper3d.com (2) |
| dendro | DENDRO-6101 .. 6112 | 12 | 1 | 0 | 0 | github.com (6), discourse.mcneel.com (6) |
| dragonfly | DRAGONFL-5101 .. 5130 | 30 | 6 | 4 | 25 | docs.ladybug.tools (30) |
| eddy3d | EDDY3D-5501 .. 5584 | 84 | 23 | 4 | 4 | docs.eddy3d.com (84) |
| elefront | ELEFRONT-8101 .. 8112 | 12 | 0 | 0 | 0 | discourse.mcneel.com (12) |
| elk | ELK-7501 .. 7512 | 12 | 0 | 0 | 0 | github.com (11), discourse.mcneel.com (1) |
| firefly | FIREFLY-9501 .. 9512 | 12 | 1 | 10 | 10 | www.grasshopper3d.com (6), discourse.mcneel.com (4), github.com (2) |
| fologram | FOLOGRAM-9601 .. 9612 | 12 | 3 | 12 | 12 | ms-kb.msd.unimelb.edu.au (6), community.fologram.com (3), docs.fologram.com (3) |
| goat | GOAT-6601 .. 6606 | 6 | 5 | 6 | 6 | discourse.mcneel.com (6) |
| hal | HAL-9301 .. 9328 | 27 | 2 | 0 | 3 | docs.hal-robotics.com (27) |
| heron | HERON-7601 .. 7627 | 27 | 0 | 0 | 0 | github.com (27) |
| honeybee | HONEYBEE-5301 .. 5400 | 100 | 36 | 2 | 44 | docs.ladybug.tools (100) |
| hops-rhino-compute | HOPS-9701 .. 9719 | 19 | 6 | 0 | 0 | developer.rhino3d.com (19) |
| human-ui | HUMANUI-8401 .. 8411 | 11 | 0 | 0 | 1 | github.com (11) |
| ivy | IVY-8801 .. 8805 | 5 | 1 | 0 | 3 | discourse.mcneel.com (5) |
| kangaroo2 | KANGAROO-5801 .. 5829 | 29 | 15 | 1 | 1 | github.com (23), discourse.mcneel.com (6) |
| karamba3d | KARAMBA-5701 .. 5763 | 63 | 24 | 1 | 6 | manual.karamba3d.com (62), scripting.karamba3d.com (1) |
| kuka-prc | KUKAPRC-9201 .. 9214 | 14 | 3 | 12 | 12 | www.prc.robotsinarchitecture.org (7), prc.robotsinarchitecture.org (5), github.com (2) |
| ladybug | LADYBUG-5001 .. 5051 | 51 | 13 | 6 | 34 | docs.ladybug.tools (51) |
| lands-design | LANDS-8301 .. 8320 | 20 | 2 | 1 | 20 | discourse.mcneel.com (15), www.rhino3d.com (5) |
| lunchbox | LUNCHBOX-8601 .. 8616 | 16 | 2 | 2 | 16 | discourse.mcneel.com (16) |
| lyrebird | LYREBIRD-7901 .. 7912 | 12 | 0 | 1 | 12 | discourse.mcneel.com (11), graphisoft.com (1) |
| machina | MACHINA-9401 .. 9411 | 11 | 1 | 0 | 0 | github.com (11) |
| meshplus | MESHPLUS-7301 .. 7308 | 8 | 0 | 5 | 8 | www2.food4rhino.com (5), github.com (2), discourse.mcneel.com (1) |
| metahopper | METAHOP-8701 .. 8709 | 9 | 0 | 0 | 2 | discourse.mcneel.com (9) |
| millipede | MILLIPED-5901 .. 5912 | 12 | 7 | 8 | 8 | grasshopper3d.com (4), www.grasshopper3d.com (4), discourse.mcneel.com (4) |
| octopus | OCTOPUS-6201 .. 6212 | 12 | 9 | 12 | 12 | discourse.mcneel.com (11), www.food4rhino.com (1) |
| opossum | OPOSSUM-6401 .. 6411 | 11 | 7 | 11 | 11 | discourse.mcneel.com (9), www.food4rhino.com (2) |
| paneling-tools | PANELING-8501 .. 8512 | 12 | 0 | 0 | 3 | discourse.mcneel.com (12) |
| peacock | PEACOCK-9001 .. 9007 | 7 | 1 | 0 | 7 | discourse.mcneel.com (7) |
| plankton | PLANKTON-7401 .. 7410 | 10 | 0 | 0 | 9 | raw.githubusercontent.com (5), discourse.mcneel.com (5) |
| pollination | POLLINAT-5201 .. 5227 | 27 | 3 | 2 | 11 | docs.pollination.solutions (27) |
| pufferfish | PUFFER-7001 .. 7014 | 14 | 0 | 0 | 14 | discourse.mcneel.com (14) |
| rhino-inside-revit | RIR-7801 .. 7831 | 31 | 0 | 0 | 0 | raw.githubusercontent.com (31) |
| robots | ROBOTS-9101 .. 9119 | 19 | 6 | 0 | 0 | github.com (19) |
| shapediver | SHAPEDIV-9801 .. 9827 | 27 | 2 | 0 | 4 | help.shapediver.com (23), github.com (4) |
| speckle | SPECKLE-8001 .. 8025 | 23 | 0 | 2 | 5 | docs.speckle.systems (23) |
| tree-sloth | TSLOTH-8901 .. 8905 | 5 | 0 | 2 | 3 | discourse.mcneel.com (5) |
| tt-toolbox | TTTOOLBX-6801 .. 6818 | 18 | 0 | 18 | 18 | www2.food4rhino.com (8), blog.rhino3d.com (4), www.grasshopper3d.com (4) |
| visualarq | VISARQ-8201 .. 8228 | 28 | 0 | 0 | 28 | discourse.mcneel.com (28) |
| volvox | VOLVOX-7201 .. 7208 | 8 | 2 | 4 | 8 | github.com (4), blog.rhino3d.com (3), www.grasshopper3d.com (1) |
| wallacei | WALLACEI-6301 .. 6316 | 16 | 9 | 16 | 16 | discourse.mcneel.com (16) |
| weaverbird | WEAVER-6901 .. 6909 | 9 | 0 | 5 | 9 | www.grasshopper3d.com (5), discourse.mcneel.com (4) |

Total: 999 rows, 225 with target Best, 179 citing a URL that a transcript audit could not confirm as fetched.

## Source reliability (read before relying on rows)

Research ran from a restricted network. Many official documentation hosts did not resolve or returned 403/405 on the first pass; a second pass (2026-11-24) re-read plugins whose hosts were then allowed. Two audits back the status below:

- The `Cites unconfirmed URL` column counts rows whose `source.doc` was not fetched successfully by any worker, checked against the workers' transcripts. Those rows carry a `notes` field saying so. The audit is a heuristic: a fetched page that mentioned an error word is counted as unconfirmed, so the column over-reports rather than under-reports.
- Rows not flagged cite a page a worker opened, but a page can be a forum thread or repository README rather than a component reference. Treat those as feature-level outcomes.

Known weak areas:

- No official component reference was reachable for: VisualARQ, Lands Design, LunchBox (hosts `www.visualarq.com`, `www.asuni.com`, `apps.provingground.io` are still blocked), Octopus, Wallacei, Opossum, Biomorpher, Anemone, Goat, TT Toolbox, Fologram, KUKA|prc, Firefly, Millipede, Weaverbird, Mesh+, Cocoon and Volvox.
- Kangaroo 2 has no official online component reference; rows rest on community example repositories and forum threads.
- Swift is a separate OpenFOAM plugin (ODS Studio), not a successor to Butterfly; only feature-level rows exist.

## Plugin identity corrections found during research

- Cocoon is an iso-surface (marching cubes) mesher, not a point-cloud reconstructor. Volvox is a point-cloud plugin, not a voxel plugin.
- Plankton rows for Conway operators, subdivision and per-element properties describe capabilities the half-edge kernel enables; its README lists them as planned.

## Gaps and follow-ups

- Several plugins are below requested row counts (Weaverbird, Cocoon, Mesh+, Volvox, Ivy, Tree Sloth, Goat, Peacock) because documentation was thin; rows were not padded.
- Not yet covered: Crane, Starling, Bifocals, and Lyrebird beyond component families. Honeybee-Radiance and Honeybee-Energy are folded into the Honeybee file.
- Next pass: allow the blocked hosts above, then re-run component-level reads for the plugins in the weak-areas list.
