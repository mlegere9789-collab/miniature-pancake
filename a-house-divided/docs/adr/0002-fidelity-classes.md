# ADR 0002: Fidelity classes fix the simulation tier per scenario

**Status:** accepted.

**Decision.** Three fidelity classes: F0 exact per-projectile simulation near engagement, F1 volley bundles with statistics drawn from the same weapon
tables and random streams, F2 regiment-level rules checked against F0/F1. The class is a property of the scenario and is recorded in the replay. It is
never chosen by the machine it runs on. Quick-resolve runs the scenario's class headless, so it matches played battles by construction.

**Why.** Resolves the conflict between "every shot simulated" and cost limits without letting hardware change outcomes in multiplayer, replays or forecasts.

**Test.** A calibration test keeps F1 and F2 within tolerance of F0 on shared scenes (added with the first rules specs).
