# Archicad 29/30 Parity Register: coverage

924 rows, program `archicad`, all `tier_now` None and `status` open (no free credit). Target is Strong, or Best for 24 crown-jewel rows. Ids are `D8-ARCHICAD-<DOMAIN>-<nnnn>`. Each row has a one-sentence statement, a source (kind, ref, doc), an acceptance sketch (`acceptance[0].sketch`, test id `acc.archicad.<domain>.<nnnn>`) and a superiority note. The second pass (2026-11-14) checked the original 678 rows against the Archicad 30 Help and added 246; see `VERIFY.md` for counts, rejected rows and unreachable sources.

## Rows by domain
| Domain | Rows | Factory |
|---|---|---|
| analysis | 32 | analysis-site |
| api | 42 | scripting-api-ai |
| attrib | 53 | bim-arch |
| cad2d | 21 | drafting-docs |
| edit | 51 | bim-arch |
| gdl | 47 | scripting-api-ai |
| graphic | 45 | drafting-docs |
| hotlink | 28 | collab-cloud |
| ifc | 62 | interop-migration |
| mep | 34 | bim-struct-mep |
| nav | 54 | drafting-docs |
| new2930 | 26 | bim-arch |
| props | 46 | bim-arch |
| reno | 26 | drafting-docs |
| sched | 36 | drafting-docs |
| seo | 28 | bim-arch |
| struct | 22 | bim-struct-mep |
| team | 56 | collab-cloud |
| toolbox | 168 | bim-arch |
| ui | 23 | ux-access-i18n |
| viz | 24 | render-anim |

## Method and limits (read before trusting)
- First pass (earlier, superseded): written from general knowledge without access to vendor pages. Second pass: every row was checked against Archicad 30 Help pages on help.graphisoft.com (the only allowed and reachable documentation host); method and caveats are in `VERIFY.md`.
- `notes` start with `VERIFIED`, `CORRECTED`, `REJECTED:`, `UNVERIFIED` or `ADDED+VERIFIED`. Rejected rows are kept for traceability.
- JSON command API, GDL reference and C++ API docs were not reachable, so those rows stay UNVERIFIED unless PyPI or the devkit README confirmed them.
- Numeric figures in acceptance sketches are closed-form or definitional checks, not measurements from Archicad. A licensed-copy walk-through (plan 4.3) is still needed to confirm completeness and behaviours.
- Topics not found in the Help, so not in the register: reinforcement/rebar, timber/CLT, dormers, daylight analysis, LandXML terrain, cost workflow, Twinmotion/Enscape links.
