# Ticket T-NNN: <title>

- **Class / estimate:** XS | S | M | L, estimated usage:
- **Spec reference:** docs/specs/SPEC-NNN (the only design context the implementer receives)
- **Acceptance tests:** names of the failing tests written first by a different agent. The implementer may not edit them.
- **Scope fence:** files and crates allowed to change. Everything else is out of bounds.
- **Non-goals:**
- **Stop-loss:** re-plan if usage reaches 2x the estimate or the change exceeds about 400 lines.

## Definition of done
1. All acceptance tests pass; no test weakened, removed or skipped; no threshold changed.
2. fmt, clippy (warnings are errors), tests, forbidden-terms scan and no-process-text lint pass.
3. Determinism golden hashes unchanged unless the spec changed behaviour (then goldens change in the same PR, with a reason).
4. Data records touched carry `source` and `confidence`; nothing became `verified` without a second source.
5. No Restricted material in code, data, strings or docs; provenance recorded.
6. No process text in shipped artefacts. Ledger row appended. ADR written if a decision was made.
