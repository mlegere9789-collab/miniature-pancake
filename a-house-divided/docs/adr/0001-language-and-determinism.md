# ADR 0001: Rust core, fixed-point authority, cross-platform hash proof

**Status:** accepted (revisit at gate M1 only for the renderer host, never for the core rules).

**Decision.** The simulation core is Rust libraries with no I/O or engine dependency. Authoritative state is fixed-point (Q32.32) and integer.
Randomness comes from named streams derived from one master seed. State is hashed (FNV-1a 64 over a canonical encoding) at checkpoints, and a replay is
the initial parameters, the seed, the input stream and the checkpoint hashes.

**Why.** Compiler feedback catches defect classes early; a pure library can be hosted by any renderer; hashes make desync a measurable failure rather than
a feeling. Floating point differs across compilers and optimisation levels, so it is excluded from authoritative state.

**Proof obligation.** Golden replays are generated once on Linux and verified on Linux, Windows and macOS in CI. A mismatch anywhere is a P0.

**Consequences.** The renderer host (candidate: a commercial engine with a custom crowd module; fallback: custom renderer) is decided at M1 from measurements.
