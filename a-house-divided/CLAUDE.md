# A House Divided: project rules for every session

An original American Civil War grand-strategy and real-time-battle game. Placeholder title; the words
used by the reference series must never appear in code, data, strings, names or assets. This is a
**clean-room** project: read `docs/gates/G0-charter.md` before anything else.

## Layout
- `crates/`  core-math, sim-state (deterministic core libraries; no I/O, clocks or engine code)
- `shells/`  sim-cli (headless harness `hd-sim`), later viewer-web and the engine host
- `data/`    `schemas/` (JSON Schema), `history/` (TOML records with provenance), `rules/`, `scenarios/`
- `docs/`    `specs/` numbered specifications, `adr/` decisions, `gates/`, `cycle-reports/`, the build plan PDF
- `qc/`      `golden-replays/`, `fixtures/` (known-bad-value regression fixtures), `calibration/`
- `tools/`   forbidden-terms scan, no-process-text lint, data validator, golden verifier
- `ledger/`  `usage.csv` (one row per accepted ticket)

## Rules that never bend
1. **Determinism.** Authoritative state uses fixed-point integers only. No wall-clock, no thread races, no unordered-map
   iteration, no floating point in game logic. Randomness only from named streams of the master seed.
2. **Specs are the only design input.** Implement what `docs/specs/SPEC-NNN` says. If the spec is wrong, amend the spec first,
   then the tests, then the code.
3. **Tests first, and tests are not edited to pass.** Never weaken, skip, disable or quarantine a test; never edit a threshold
   to turn a gate green.
4. **Provenance.** Every historical number carries `source` and `confidence`. Nothing is `verified` without a `second_source`.
5. **Clean room.** Never read `/audits/`, any reference-game material, or search for it. Never copy names, numbers, schemas,
   layouts or text from any other game. Use period terminology and original vocabulary.
6. **No process text** in shipped artefacts (no "first draft", "my earlier", "TODO" in data or specs). One ticket, one session.

## Commands (run from the repository's `a-house-divided/` directory)
```
cargo fmt --all
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace            # add --release before merging determinism-sensitive work
python3 tools/scan_forbidden.py && python3 tools/lint_process_text.py && python3 tools/validate_data.py
bash tools/verify_goldens.sh      # cross-platform determinism proof (also runs in CI)
```
Keep terminal output short: pipe long output through `tail -n 30`.

## Gates (see build plan PDF section 10 and 11)
Q0 local, Q1 pull request, Q2 merge, Q3 nightly, Q4 every cycle, Q5 human playtest, Q6 release (G7). Main is always green and always
runnable. A determinism mismatch on any platform is a P0 that stops merges.

## Working style
- One ticket per session, from `docs/ticket-template.md`, fewer than about 400 changed lines.
- Record decisions as short ADRs in `docs/adr/`. Append one row to `ledger/usage.csv` per accepted ticket.
- Commit small, with the ticket and spec ids in the message.
