# Rhino 8 reference-oracle kit (observation-only; see CLEANROOM.md first)

| File | Role |
|---|---|
| CLEANROOM.md | protocol, gates G0-G5, firewall between Observers and Implementers |
| REVIEW_LOG.md | G0 sign-off and intake log (nothing runs before G0 is signed) |
| gen_cases.py -> cases.jsonl | deterministic case corpus authored by us (200 cases: primitives, Booleans incl. tangent/near-tangent sweeps, fillet/chamfer, offset, extrude/revolve/loft, intersections, curves) |
| run_here.py | one-click launcher to run inside Rhino (edit REPO, LIMIT=5 smoke then 0 full) |
| run_rhino.py | Observer-side runner inside Rhino 8 (public RhinoCommon only). UNTESTED until the first smoke run |
| compare.py | golden schema check (G2) + Dino 8 vs golden comparison with FIXED tolerance classes |
| LOCAL_SESSION_PROMPT.md | the instruction to paste into the local session on the owner's computer |
| golden/ , specs/ | Observer output (numbers/flags and our-own-words behaviour statements) |

Not yet built (Implementer side): the Dino 8 case runner that executes cases.jsonl through the kernel/app and writes
dino_results.jsonl in the same record shape, plus the Register-row tests that point at golden-backed comparisons.
