# PARITY_MAP — current status (lean snapshot)

**This file is the required reading for dispatch decisions.** `PARITY_MAP.md` in this directory is the full historical round-by-round narrative log (382 rounds, ~10,750 lines) — it is an archival record only; nothing requires reading it to pick up the next unit of work. See `/DINO8_PLAYBOOK.md` at the repo root for the process this file is part of.

As of commit `ab20e4ce` (last full re-score).

## Kernel — headline 78.1% (total weight 17.75, 17 categories)

| Category | Weight | Items | Present | Partial | Missing | % |
|---|---|---|---|---|---|---|
| Topology | 1.0 | 27 | 13 | 14 | 0 | 74.1% |
| Geometry representation | 1.0 | 29 | 23 | 6 | 0 | 89.7% |
| Boolean operations | 1.5 | 25 | 9 | 15 | 1 | 66.0% |
| Blending & chamfering | 1.5 | 24 | 6 | 18 | 0 | 62.5% |
| Sweeping/lofting/extruding/revolving | 1.0 | 29 | 7 | 22 | 0 | 62.1% |
| Offsetting & shelling | 1.0 | 27 | 3 | 24 | 0 | 55.6% |
| Local/direct-edit | 1.0 | 28 | 8 | 20 | 0 | 64.3% |
| Intersections & projections | 1.5 | 29 | 29 | 0 | 0 | 100% |
| Healing | 1.0 | 30 | 23 | 7 | 0 | 88.3% |
| Mass properties & spatial queries | 1.0 | 30 | 28 | 2 | 0 | 96.7% |
| Tessellation | 1.0 | 25 | 20 | 5 | 0 | 90.0% |
| Transformations/patterns/splitting | 0.5 | 22 | 12 | 10 | 0 | 77.3% |
| Kernel data exchange | 1.0 | 27 | 14 | 9 | 4 | 68.5% |
| Feature operations | 1.0 | 24 | 6 | 18 | 0 | 62.5% |
| Surface/Curve ops (curves) | 1.0 | 28 | 28 | 0 | 0 | 100% |
| Surface/Curve ops (surfaces) | 1.0 | 29 | 23 | 6 | 0 | 89.7% |
| SubD kernel support | 0.75 | 22 | 16 | 6 | 0 | 86.4% |

**Open blocker:** fragment-loop test-suite-abort crash, root cause commit `878ecdb`, affecting the Boolean operations category. Historically tolerated as "disclosed" rather than fixed — per `/DINO8_PLAYBOOK.md` §2 rule 2, this is now a blocking bug: fix + regression test required before further Boolean-category work is dispatched.

## App — headline 84.3% (total weight 7.75, 8 categories)

| Category | Weight | Items | Present | Partial | Missing | % |
|---|---|---|---|---|---|---|
| Command system | 1.5 | 19 | 12 | 5 | 2 | 76.3% |
| 2D drafting & annotation | 1.0 | 18 | 18 | 0 | 0 | 100% |
| Viewport display/rendering | 1.0 | 18 | 16 | 2 | 0 | 94.4% |
| Scripting/automation | 1.0 | 15 | 12 | 3 | 0 | 90.0% |
| File I/O | 1.0 | 17 | 11 | 3 | 3 | 73.5% |
| SubD app-toolset | 0.75 | 24 | 19 | 4 | 1 | 87.5% |
| UI/UX | 1.0 | 19 | 14 | 4 | 1 | 84.2% |
| Ecosystem | 0.5 | 16 | 7 | 6 | 3 | 62.5% |

## Update protocol

Do not hand-edit the percentages above without clearing the gate in `/DINO8_PLAYBOOK.md` §3 and §5 for the specific item(s) moved. Note the commit(s) and test name(s) that justified any row change in the commit message, not in prose here — this file stays a table, not a log.
