# parity/tools/catalog

Python-only (stdlib) inventory tools: what Dino 8 already ships, so Register
rows (`parity/register/<program>/*.jsonl`, schema `schema/row.schema.json`)
can be mapped to existing commands and kernel functions. Nothing is built or
run; everything is a static read of the sources.

**Hints, not evidence.** `map_rows.py` output is a fuzzy name match. A row
moves to `verified` only through a named test (`acceptance[].test_id`) with
recorded evidence, never through this mapping. Likewise `status` in
`catalog.jsonl` means "a `Reg()` call registers this command" (what the app
reports as Implemented/Partial/Planned), not "tested to Rhino parity".

## Scripts

| script | reads | writes |
|---|---|---|
| `extract_catalog.py` | `dino8-app/data/commands.json`, every `Reg(e, "Name", ...)` in `dino8-app/src`, `Application::RegisterCommands()` order, `InstallDefaultAliases()` | `catalog.jsonl` |
| `extract_kernel_api.py` | `dino8-kernel/include/**/*.h`, `dino8-kernel/src/**/*.h` | `kernel_api.jsonl` |
| `map_rows.py REGISTER_DIR` | the two jsonl files above + register rows | `candidates.jsonl` (not committed; regenerate) |

Run from anywhere: `python3 parity/tools/catalog/extract_catalog.py` (same for the others).

### catalog.jsonl
`CommandCatalog.cpp` is only the loader; the catalog data is
`data/commands.json` (the 1,070-command Rhino 8 list). A command is
`implemented`/`partial` when a `Reg()` call registers it (last registration in
`RegisterCommands()` order wins, as at run time); otherwise `planned` (the
engine's catalog placeholder). Registered names absent from the catalog are
emitted with `in_rhino_catalog=false` (Dino extensions). Fields: `name,
aliases[], category, options[], source_file, source_line, help_text_if_any`
(the `Reg()` note), plus `status, impl_file, impl_line, impl_group,
in_rhino_catalog, description, toolbars, menu, registrations[]`.
Limits: names created at run time (Lisp `defun`, plug-ins, the user alias
table) are invisible to a static scan; `Reg()` names held in a variable are
resolved only from a literal list or table in the same file (14 sites, all
resolved).

### kernel_api.jsonl
`{symbol, header, line, kind, name, scope, internal}`; kinds: `function,
method, static_method, constructor, destructor, operator, type`. Public
members only; `detail`/anonymous namespaces and `src/` headers have
`internal=true`. Overloads share a symbol and differ by `line`.
It is a lexical scan (comments, strings, preprocessor blanked; brace/scope
tracking), not a C++ parser: macro-generated or deeply templated
declarations may be missed.

### candidates.jsonl
`{row_id, register_file, label, candidates:[{kind, name, score, matched_on,
via, ...}]}`; `kind` is `command` or `kernel`; score 1.0 = identical
normalised name, lower = trigram/token blend (default floor 0.6, top 5 per
kind). Queries come from the quoted titles in `statement` and the last
segments of `source.ref`. Common words ("Select", "Copy") match by name only,
so read the hint, then find or write the test that proves the behaviour.

Note: a word the project forbids in committed files appears in a few source
notes; `extract_catalog.py` writes it as "callback" in `catalog.jsonl`.
