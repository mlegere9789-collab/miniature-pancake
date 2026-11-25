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

Dino 8 side (Implementer): the case runner is the kernel test `extra::OracleRhino` (`dino8-kernel/tests/extra/oracle_rhino.cpp`, run with
`DINO8_TEST_FILTER=OracleRhino`). It prints one `D8RES {record}` line per case; `fetch_dino_results.py` turns the CI job log into `dino_results.jsonl`,
`compare.py ... --dino dino_results.jsonl --report divergence.jsonl` compares, and `divergence_report.py` writes `DIVERGENCE.md` (classes ranked by rows
blocked; golden records marked `provisional` or `hazard` are listed separately). Not yet built: the Register-row tests that point at golden-backed comparisons.
