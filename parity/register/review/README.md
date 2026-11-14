# Register review (Rhino and Archicad)

- `REVIEW_rhino.md`, `REVIEW_archicad.md`: sample results, defect rates, systematic problems, coverage gaps, verdicts.
- `fixes_rhino.jsonl`, `fixes_archicad.jsonl`: proposed edits as `{id, field, old, new, reason}`. `field` is a row field (`statement`, `source.doc`, `target`, `factory`, `domain`, `alias_of`), or `row` with `new: "remove"` for rows the register's own notes reject. These are proposals; no register row was edited.
- `sample_ids_*.txt`, `sample_method.py`: the sampled ids and the script that regenerates them.

Note for tooling: `validate_register.py` scans every `parity/register/**/*.jsonl` as register rows, so the two `fixes_*.jsonl` files make a strict run report duplicate ids. Exclude `parity/register/review/` in the validator (or move the fixes files) before running it on this branch.
