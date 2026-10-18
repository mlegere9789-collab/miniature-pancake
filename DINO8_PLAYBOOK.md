# Dino 8 Recovery Playbook

**Scope:** `dino8-kernel/`, `dino8-app/` (branch `claude/pdf-audit-i2bvwm`, PR #17)
**Purpose:** Replace the dispatch model that built this project so far with one that reaches completion efficiently, with verifiable quality, instead of burning usage on self-reported motion.

This document is the required reading for anyone (human or agent) dispatching work on Dino 8 going forward. It replaces the old per-round dispatch template.

---

## 1. Why this exists

An audit of the project's first delivery phase found three concrete process failures — not failures of the underlying engineering, which is largely real and substantive (see Appendix B).

**1. The status document was an unbounded narrative log, not a status document.** `dino8-kernel/PARITY_MAP.md` grew to 3,032,062 bytes / 10,758 lines: 382 essay-length round reports (300–800+ words each), never pruned. Every dispatch prompt instructed the next session to "read PARITY_MAP.md fresh" — meaning hundreds of sessions each paid to read a growing wall of prose just to find the one current table buried in it. This was pure, avoidable token spend.

**2. A known, crashing test was normalized instead of fixed.** The kernel test suite has a test-suite-aborting "fragment-loop" crash traced to commit `878ecdb` (an Intersections & projections change). Across dozens of subsequent rounds — including 8+ rounds dedicated specifically to Boolean operations, the highest-weight kernel category — this was referred to as "pre-existing, already-disclosed" rather than treated as a blocker. Boolean operations ended at 66.0% complete: the most-invested, least-finished kernel category, gated the whole time by one unfixed bug nobody was tasked with actually fixing.

**3. "Done" was self-reported prose, not machine-verified.** CI exists and is real (Linux/Windows/Windows-ARM/macOS, `ctest` + a 645-assertion smoke harness), but nothing ties a `[present]` claim in PARITY_MAP to a specific passing test. A manual recount once caught a genuine double-count bug in the bookkeeping itself — meaning even the self-reported numbers weren't reliably self-consistent, let alone independently verified.

On top of this, the dispatch model itself (38+ standing "never sit idle" persistent sessions, refilled on an hourly timer regardless of session liveness) died silently when the account hit its weekly usage cap around Oct 13, and then kept firing uselessly into dead sessions for five more days because nothing checked liveness before redispatching.

Real progress did happen in the two weeks before the collapse (kernel 73.0% → 78.1%, app 82.2% → 84.3%, Oct 7–13). The fix here is not "start over" — it's "stop bleeding usage on process, and stop letting known bugs hide behind self-reported green."

---

## 2. The five rules

1. **Lean status doc.** Dispatch decisions read `dino8-kernel/PARITY_MAP_CURRENT.md` only — a machine-generated snapshot table, not prose. The historical narrative stays in `PARITY_MAP.md` as an archival log; nobody is required to read it to do their job.
2. **A known-broken test is a blocker, not a footnote.** If the suite can't run clean, the next unit of work in that category is fixing it — not working around it, not another round of "disclosed and moved on."
3. **"Done" is machine-verified, not self-reported.** A category row only flips to `[present]` when there is a named, passing automated test for that specific item and an independent adversarial-review pass has failed to refute it (see §5).
4. **Dispatch is budget-aware and liveness-checked.** Before refilling any session, check it's actually alive and check remaining usage budget. On low budget: pause and report, never silently keep firing.
5. **The north-star metric is cost-per-verified-item, not the self-reported percentage.** Report it every round. A metric that can plateau for 14 hours without anyone noticing is not a metric worth optimizing.

---

## 3. Definition of done (per item)

An item in PARITY_MAP_CURRENT.md may be marked `[present]` only when **all** of the following are true:

- [ ] A real implementation exists (no stub, no hardcoded return, no "not yet implemented" path on the happy path).
- [ ] A named automated test exercises it (kernel: a `dino8_kernel_tests` case; app: a `tests/*_script.txt` smoke assertion or equivalent).
- [ ] That test is currently passing in CI on at least one platform, ideally all four.
- [ ] An independent verifier (a second agent, or a human) has tried to refute "this is actually done" by reading the diff and the test, and failed to.
- [ ] The commit message or PR note names the test and the verification outcome — not prose like "verified end-to-end," but the actual test name/ID.

Anything short of this stays `[partial]` with a one-line gap note, exactly as the old log already did well for gaps — that convention is kept.

---

## 4. Dispatch model

- **Right-sized concurrency.** Dispatch sessions for categories that are genuinely ready to make independent progress, not one per PARITY_MAP row regardless of whether it's currently productive. If a category is blocked on a shared bug (like the Boolean crash), fix the bug first; don't run parallel sessions circling the same blocker.
- **Liveness check before every redispatch.** Call `get_session` (or equivalent) and confirm the target session is actually active before sending it more work. A session that's idle because it finished is fine to refill; a session that's dead because it hit a cap is not.
- **Budget check before every redispatch.** Track remaining usage. If it's low, stop dispatching and report status instead of continuing to fire hourly regardless.
- **Event-driven, not timer-driven, where possible.** The next unit of work for a slot starts when the previous unit's *verification* completes, not on a fixed hourly clock blind to real state.
- **Scope discipline per dispatch.** 2–3 closely related items per round, as before — but gated by §3, not self-reported.

---

## 5. Verification protocol

Before any item moves to `[present]`:

1. The implementing session writes (or points to) the specific test that exercises it, and pastes the actual passing test output — not "ran the suite clean."
2. A second, independent pass (a separate agent invocation, told to try to disprove the claim) reads the diff and the test and either: (a) finds a real gap, in which case the item stays `[partial]` with that gap noted, or (b) can't refute it, in which case it's `[present]`.
3. For bug fixes specifically (like the Boolean crash): the fix must come with a regression test that fails on the old code and passes on the new code — not just "the crash didn't happen this run."
4. CI must be green (ideally all four platforms) including the new/changed test before the PARITY_MAP_CURRENT.md edit is committed.

---

## 6. Metrics

Report every round:

- **Cost-per-verified-item** = (tokens spent this round) ÷ (items that passed the full §5 gate this round). Rounds that produce motion but no gated items report this as "∞ / undefined" rather than hiding it.
- **Headline %** (kernel, app) — same as before, but now it only moves when an item clears the gate, so it can't drift from reality the way the old self-reported number did.
- **Known blockers** — any known-broken test or shared dependency currently gating a category, carried forward explicitly every round until actually closed (not quietly re-labeled "disclosed").

---

## 7. Immediate actions taken

- `dino8-kernel/PARITY_MAP_CURRENT.md` created: a lean, table-only snapshot of the real current state (see Appendix A), replacing `PARITY_MAP.md` as required reading. The original file is untouched and remains as historical record.
- A dedicated fix pass was dispatched for the Boolean-operations fragment-loop crash (root cause: commit `878ecdb`), with a regression test required before it can be marked resolved. This is the single highest-leverage fix available: the highest-weight kernel category, most-invested in rounds, least-finished as a direct result of this one bug.
- This playbook is committed at the repo root as the required reading for all future dispatches, replacing the old per-round prompt template described in §4.

---

## Appendix A: Current status snapshot (as of commit `ab20e4ce`)

**Kernel — headline 78.1% (total weight 17.75, 17 categories)**

| Category | Weight | Items | Present | Partial | Missing | % |
|---|---|---|---|---|---|---|
| Topology | 1.0 | 27 | 13 | 14 | 0 | 74.1% |
| Geometry representation | 1.0 | 29 | 23 | 6 | 0 | 89.7% |
| Boolean operations | 1.5 | 25 | 9 | 15 | 1 | 66.0% |
| Blending & chamfering | 1.5 | 24 | 6 | 18 | 0 | 62.5% |
| Sweeping/lofting/extruding/revolving | 1.0 | 29 | 7 | 22 | 0 | 62.1% |
| Offsetting & shelling | 1.0 | 27 | 3 | 24 | 0 | 55.6% |
| Local/direct-edit | 1.0 | 28 | 8 | 20 | 0 | 64.3% |
| Intersections & projections | 1.5 | 29 | 29 | 0 | 0 | 100% |
| Healing | 1.0 | 30 | 23 | 7 | 0 | 88.3% |
| Mass properties & spatial queries | 1.0 | 30 | 28 | 2 | 0 | 96.7% |
| Tessellation | 1.0 | 25 | 20 | 5 | 0 | 90.0% |
| Transformations/patterns/splitting | 0.5 | 22 | 12 | 10 | 0 | 77.3% |
| Kernel data exchange | 1.0 | 27 | 14 | 9 | 4 | 68.5% |
| Feature operations | 1.0 | 24 | 6 | 18 | 0 | 62.5% |
| Surface/Curve ops (curves) | 1.0 | 28 | 28 | 0 | 0 | 100% |
| Surface/Curve ops (surfaces) | 1.0 | 29 | 23 | 6 | 0 | 89.7% |
| SubD kernel support | 0.75 | 22 | 16 | 6 | 0 | 86.4% |

**Known blocker:** fragment-loop test-abort crash (root cause commit `878ecdb`) affects the Boolean operations category and has been tolerated rather than fixed since its introduction. Fixing it is the top-priority dispatch per §7.

**App — headline 84.3% (total weight 7.75, 8 categories)**

| Category | Weight | Items | Present | Partial | Missing | % |
|---|---|---|---|---|---|---|
| Command system | 1.5 | 19 | 12 | 5 | 2 | 76.3% |
| 2D drafting & annotation | 1.0 | 18 | 18 | 0 | 0 | 100% |
| Viewport display/rendering | 1.0 | 18 | 16 | 2 | 0 | 94.4% |
| Scripting/automation | 1.0 | 15 | 12 | 3 | 0 | 90.0% |
| File I/O | 1.0 | 17 | 11 | 3 | 3 | 73.5% |
| SubD app-toolset | 0.75 | 24 | 19 | 4 | 1 | 87.5% |
| UI/UX | 1.0 | 19 | 14 | 4 | 1 | 84.2% |
| Ecosystem | 0.5 | 16 | 7 | 6 | 3 | 62.5% |

---

## Appendix B: Why the underlying code isn't being thrown out

A source spot-check of `boolean.cpp` found a genuine adaptive-tolerance retry path wrapping the Manifold mesh-boolean library, and an honest, mathematically-reasoned implementation of `SymmetricDifference` as a composition (rather than a naive single-shell approach that the code comments correctly identify as wrong for non-manifold contact edges). `AUDIT.md`'s command-coverage tracker cross-checks real registration macros against a live scan of the compiled binary's actual command table — more rigorous than source-grepping — and recent closures cite specific, independently checkable work: a from-scratch RFC-1951-conformant PNG encoder, the X11 ICCCM clipboard protocol, AutoCAD's real Arbitrary Axis Algorithm, each with a stated verification method (hand-derived values, pixel-level checks, byte-offset checks). The codebase is large in absolute terms (827 files; `dino8-app/` alone has 260 test files) and test-heavy. The problem this playbook fixes is process, not substance.
