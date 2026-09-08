# Fuzz QA notes (`dino8_test_fuzz_geometry`)

## What it covers

`tests/test_fuzz_geometry.cpp` generates random-but-valid geometry with a
fixed-seed `std::mt19937_64` PRNG and checks invariants that must hold no
matter what the specific random input was:

1. **Random NURBS curves** (`dino8::kernel::NurbsCurve`) - random degree
   (1-5), random control point count, random coordinates, sometimes
   promoted to genuinely rational with random (including extreme, near-zero
   or large) weights, sometimes with a duplicated/coincident control point
   pair. Exercises `PointAt`, `TangentAt`, `CurvatureAt`, `Length`,
   `ClosestPoint`, `InsertKnotAt`, `ElevateDegree`,
   `MakeRational`/`MakeNonRational` round-trip, `Reverse`, `Trim`, `Split`.
2. **Random NURBS surfaces** (`dino8::kernel::NurbsSurface`) - random
   degree/CV-grid/weights, exercising `PointAt`, `NormalAt`,
   `ClosestPoint`.
3. **Random primitive-mesh boolean pairs** - random
   sphere/cylinder/cone/torus/box (via `ON_Brep*` factories + `MeshBrepClosed`,
   the same construction `test_brep_mesher.cpp` uses for known-good shapes),
   each randomly translated/rotated, occasionally an exact coincident pair,
   occasionally an extreme (very small or very large) radius, combined with
   a random `dino8::kernel::BooleanOp` (Union/Intersection/Difference/
   SymmetricDifference).
4. **Random surface/surface intersection** (`dino8::app::IntersectSurfaces`)
   - the core geometric primitive `cmd_fillet.cpp`'s `BuildFillet` is built
   on (offset-then-intersect). Full `BuildFillet` itself is not exercised
   here because it lives in `cmd_fillet.cpp`'s anonymous namespace and pulls
   in `app/Application.h` (the full GUI/document/command layer) through
   `commands/cmd_common.h`, which this standalone kernel-level test
   deliberately does not link against (same reasoning `dino8_test_brep_mesher`
   already applies to `BrepMesher.cpp`: link only the file under test plus
   `dino8_kernel`, not the whole app). `IntersectSurfaces` is the one real,
   independently-linkable piece of that pipeline, so it is what gets fuzzed.

Invariants checked on every case:

- No crash (`SIGSEGV`/`SIGABRT`/`SIGFPE`/`SIGILL`) and no hang (a
  `SIGALRM`-based per-case timeout, 5-10s depending on the category).
- No NaN/Inf in any point, vector, uv, bounding box, length, or volume the
  operation produces.
- A mesh a boolean operation returns with `FaceCount() > 0` must be
  `IsClosedManifold()` - `BooleanCombine`'s own documented contract
  (`dino8-kernel/include/dino8/kernel/boolean.h`) is "throw, or return a
  valid closed/watertight mesh," never a non-manifold "solid."
- A correctly-oriented, closed primitive mesh's own volume must be
  positive (this kernel's own documented outward-normal convention).

A `std::exception` (`std::invalid_argument`/`std::runtime_error`/etc.) from
any of these calls is treated as an *expected* rejection of degenerate
random input (this kernel's own normal "no" answer for bad input, per its
own doc comments), not a fuzz finding - only crashes, hangs, and the
invariant violations above are.

## Reproducing a failure

The PRNG seed defaults to a fixed constant for CI determinism, and is
overridable:

```sh
DINO8_FUZZ_SEED=<seed> ./build/dino8_test_fuzz_geometry
# or
./build/dino8_test_fuzz_geometry <seed>
```

`DINO8_FUZZ_ITERS=<n>` scales the iteration counts (split across the four
categories) for a deeper local run than CI's default budget.

On any invariant violation the test prints the failing case's category,
index, and the seed to reproduce it, then aborts (`std::_Exit(1)`) so the
process state at the moment of failure is preserved for a debugger
(no further random state is consumed after a failure, so the same seed
reaches the exact same failing case every time).

## Results as of this writing

No bugs found. Across the default seed plus a dozen hand-picked seeds
(1, 2, 3, 11, 22, 33, 42, 999, 123456, 7777777, and two runs at 3x the
default iteration count), including the adversarial variants added after
the first clean pass (duplicated/coincident control points, near-zero and
large rational weights, exactly-coincident boolean operand pairs, and
extreme primitive radii), this fuzzer accumulated **7M+ individual
invariant checks with zero failures**.

This is evidence of robustness in the specific surface fuzzed here (kernel
NURBS curve/surface manipulation, primitive-mesh booleans, and
surface/surface intersection), not a guarantee of correctness everywhere -
in particular:

- It does not reach `cmd_fillet.cpp`'s actual `BuildFillet`/trimming code
  (see above - that needs the app/Document layer, not just the kernel).
- It does not fuzz SubD, file I/O (`.3dm` round-tripping), or the command
  layer's own input validation/undo-redo interaction - those would need
  their own fuzz harnesses with different linkage (SubD is already
  kernel-only and would be a natural next extension; file I/O and command
  layer both need `dino8_app`'s heavier dependencies).
- Random control points/degrees are drawn from bounded, if wide, ranges
  (coordinates typically within +/-25 units, radii typically 0.5-6 with an
  occasional 0.01-0.2 or 30-200 extreme); it does not probe truly
  pathological scales (e.g. 1e12 vs 1e-12 in the same scene) the way a
  dedicated numerical-robustness fuzzer would.

If a future run of this fuzzer (a different seed, more iterations, or
after a kernel change) does find a real failure, add it here with its
exact reproducing seed and case description rather than weakening the
invariant check to make it pass.
