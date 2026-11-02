# A House Divided (working title)

An original American Civil War strategy-and-tactics game: a weekly-turn campaign (supply, rivers and rail, politics, intelligence,
named commanders) joined to real-time battles of thousands of individually simulated soldiers, decided by one deterministic
simulation that also powers quick-resolve and AI forecasting.

Status: **Phase 0 foundation and Phase 1 prototype in progress.** The full plan is `docs/A_House_Divided_Build_Plan.pdf`.

| Area | State |
|---|---|
| Deterministic math and random streams (`crates/core-math`) | SPEC-001 |
| Agent store, spatial grid, state hash, replay format (`crates/sim-state`) | SPEC-002 |
| Headless harness and cross-platform determinism proof (`shells/sim-cli`) | SPEC-003 |
| Historical data (`data/history`) with provenance and validation | schemas v0 |

Everything here is original or properly licensed; see `docs/gates/G0-charter.md`. Not legal advice.
