# ADR-005: Kernel thread-safety contract (which kernel calls may run concurrently)

| | |
|---|---|
| Status | Proposed. The audit is complete and bounded (section 4); the decision belongs to the owner. |
| Date | 2026-11-14 (UTC) |
| Plan | Master Plan Rev 2, 5.2 (threads bullet: "Spike C audits the kernel for global and static mutable state and runs it under the thread sanitizer") and 5.4 (fallback: serialized kernel calls) |
| Evidence | branch `w0-kthreads` @ `e5bc8421`, `dino8-core/KERNEL_THREADS.md`, probe `dino8-core/kernel_tsan_probe/`, openNURBS patch `dino8-core/kernel_tsan_probe/opennurbs-8.34-thread-safety.patch`; kernel at cand2 `1b690f52`, openNURBS `v8.34.26223.11001`, Manifold `v3.5.2` |
| Note | Spike C itself states that this audit was not part of its work (RESULTS.md section 9); it was done as the separate `w0-kthreads` job and is recorded here as the input to ADR-003. |

## 1. Context

The incremental engine (ADR-003) can run kernel calls on a work-stealing pool only if those calls are safe to run concurrently on independent inputs. Plan 5.4 says that if the kernel cannot
be called concurrently the engine runs kernel calls serialized and the speedup target is re-stated before Wave 1 closes. The app already runs kernel tessellation concurrently in production
(`ParallelFor` in `dino8-app/src/main.cpp:305` and `io/FileExchange.cpp:4178`) but keeps `HoleArray` booleans serial "until someone verifies thread-safety in those libraries"
(`commands/cmd_solidtools.cpp:949`); this audit is that verification.

## 2. Decision

1. Concurrent kernel calls are allowed (not serialized behind one lock, no process isolation) for: primitives and `NurbsCurve`/`NurbsSurface`/`Brep` construction and transforms; tessellation
   (`Tessellate*`, `TessellateToClosedMesh`, conforming, adaptive, `MergeAndWeld`); mass properties; mesh queries; mesh booleans, split, hull and simplify (Manifold, with `MANIFOLD_PAR=-1`; the Manifold boolean was run in the probe, split, hull and simplify are safe by audit only);
   B-rep mixed, planar and general booleans, N-ary and compound operands, `MakeHole(s)`; fillet, chamfer, revolve, extrude, pipe, convex hull; point clouds (with the fix below).
2. Serialize (register as `D8G_SERIALIZED`, one engine-wide kernel mutex, or one dedicated I/O thread): SubD (until the `ON_NextContentSerialNumber` patch is applied) and all 3dm, STEP and IGES
   file I/O (`ONX_Model`, `Model::Save`/`Load`, `Model::AddBrep`, IGES/STEP curve and surface), which hit 8 unpatched openNURBS and libuuid globals.
3. Conditions for the concurrent lane:
   1. a kernel call runs to completion on one thread and is never migrated (the `thread_local` knobs are scoped per call);
   2. warm each published B-rep once with `brep.raw().BoundingBox()` before it becomes visible to other threads (the engine's snapshot-publish step); a shared `PointCloud` needs the fix in section 6;
   3. apply the openNURBS patch (the `IsSphere` bound and the atomic content serial number) when convenient; until the `IsSphere` patch is applied, general booleans on NURBS spheres read 8 bytes past a heap block (no crash or wrong result observed);
   4. never call the test switches (`detail::Set*Enabled`, `SetBrepNakedEdgeIndexMinimum`), `setenv`, or write Manifold's `ManifoldParams()` or quality settings while workers run; keep `MANIFOLD_PAR=-1`
      (Manifold then starts no threads and links no TBB, so the engine's pool supplies all parallelism with no nested pool);
   5. give workers at least 256 KiB of stack (measured peak 15.5 to 27.8 KiB over 13 operations at `-O1`) and catch `std::exception` per task (the kernel reports failure by throwing);
   6. expect no cooperative cancellation: a running B-rep boolean cannot be aborted, so the engine discards stale results (Manifold 3.5.2 has `ExecutionContext` cancellation for mesh booleans, unused).
4. The registration contract in RESULTS.md section 8 of Spike C (no global or static mutable state, deterministic, no re-entrancy, polls cancellation at least every millisecond, never unwinds across the boundary) is the
   definition of `D8G_THREAD_SAFE`.
5. The plan 5.4 fallback "run kernel calls serialized" is NOT forced by thread-safety: only SubD and file I/O sit in a serial lane.

## 3. Measured evidence

Method: (1) static inventory of every global, `static`, `thread_local`, function-local static and singleton in 26 kernel sources plus `nm -C --defined-only` over the 26 objects of cand2's kernel base, `readelf -sW` for TLS symbols, and the same `nm` pass over
171 of 193 openNURBS archive members and 16 of 18 Manifold members that the probe links; (2) a ThreadSanitizer probe with all kernel, openNURBS, Manifold, zlib and UUID sources rebuilt with `-fsanitize=thread -O1` (about 8 minutes wall on 2 jobs). The first
run with uninstrumented libraries missed the openNURBS findings. The probe runs each operation serially (twice, to show determinism) and then on 4 threads, aligned and rotated on alternate iterations, comparing every concurrent result bit for bit with the serial result.
Machine: g++ 13.3, libtsan 2, 4-core shared machine (load average 1.8 for the scaling run, 5 to 19 for the others).

Inventory: exactly 18 non-library static-storage symbols in the kernel objects, each accounted for (atomic counters, `thread_local` knobs scoped by RAII, per-object mutex caches, const tables); 4 `thread_local` variables; no singletons, registries or global caches; no
`rand`/`srand`/`strtok`/`setlocale`/`setenv`; tolerances are `constexpr`; openNURBS has no mutable tolerance global and no memory pool (`onmalloc` wraps libc). 13 distinct race root causes (one root cause is one shared variable or one faulty statement, however many reports TSan printed):
2 in kernel code (both fixed on the branch), 11 in openNURBS and its bundled libuuid (3 on code paths the engine would use, 8 in 3dm/STEP/IGES file I/O).

TSan results, 4 threads (the plan's 200 iterations were not reachable under TSan on the shared machine for the first two rows):

| run | scope | result |
|---|---|---|
| `indep`: 34 iterations x 4 threads x 8 operations = 1,088 calls (136 per operation), 609 s wall (the 600 s budget); patch applied, no suppressions | per-thread inputs: box minus cylinder (mixed and general engines), sphere union box, two-cylinder compound cutter `MakeHoles`, compound planar difference, tessellation, mass properties, Manifold mesh boolean | 0 reports, 0 divergences |
| `shared`: 66 iterations x 4 threads x 5 operations = 1,320 calls, 421 s | one const Brep, Mesh and PointCloud read by all threads, welded solids, booleans on the same operands | 0 reports, 0 divergences |
| `features`: fillet, chamfer, revolve, extrude, pipe, convex hull, 200 iterations x 4 threads = 800 calls each, 140 s | per-thread inputs, patch applied | 0 reports, 0 divergences |
| `features` SubD: 100 iterations unpatched; 200 iterations (800 calls) patched | per-thread inputs | unpatched 2 reports, one location (`ON_NextContentSerialNumber`); patched 0 |
| `features` 3dm + IGES + STEP round trip: 200 iterations (800 calls), patched | per-thread files | 236 reports, every one inside the file I/O operation |
| `--cold`, `indep`, 6 processes | no warm-up | 0 reports |
| `--cold`, `shared`, 8 processes | no warm-up | 2 of 8 processes report 13 reports at one location (`ON_Brep` bounding box cache); with `--prewarm-bbox` 0 of 8 |
| plain build (no sanitizer, `-O1`), all phases, 60 iterations x 4 threads (4,320 calls) | all | 0 divergences, 0 exceptions (the 5,456 openNURBS error events are the file I/O operation's own) |

Every concurrent result equalled its serial result bit for bit (0 divergences in all runs); the serial reference was reproduced exactly on a second call for every operation.

Scaling (plain `-O1` build, `indep` phase, 12 iterations per thread; load average 1.8; a pure-CPU control with no kernel code gave wall/CPU 1.01 at 1 and 2 threads and 1.40 at 4 threads, the noise floor of the box). CPU ms per call at 1 / 2 / 4 threads:

| operation | CPU ms per call 1 / 2 / 4 threads | wall/CPU at 4 threads |
|---|---|---|
| mixed boolean (box, cylinder) | 17.0 / 17.7 / 17.7 | 1.12 |
| general boolean (box, cylinder) | 197.7 / 203.9 / 203.6 | 1.13 |
| sphere union box | 150.8 / 149.3 / 149.0 | 1.09 |
| compound cutter | 139.5 / 142.2 / 142.9 | 1.06 |
| compound planar | 7.6 / 7.6 / 7.4 | 1.04 |
| mass properties | 4.2 / 4.3 / 4.2 | 1.10 |
| mesh boolean (Manifold) | 12.4 / 12.2 / 12.2 | 1.11 |
| tessellate | 13.3 / 31.5 / 90.5 | 1.04 |

Seven of eight operations cost the same CPU at 4 threads as at 1 (within 4 percent) and wall time exceeds CPU time by at most 13 percent, less than the 40 percent the control loses: no operation waits on a lock or the allocator. `tessellate` grows because the probe's input differs per thread
(thread `tid` builds a sphere centred at `(tid, 0, 0)`, and the adaptive tessellation costs about 7 ms for `tid` 0 but about 43, 47 and 220 ms for 1, 2 and 3; see the defect in section 6). Whole phase: 96 calls in 6.6 s on 1 thread, 192 in 7.1 s on 2, 384 in 9.8 s on 4; total thread CPU
divided by wall was 0.99 cores busy at 1 thread and 3.07 of 4 cores at 4 threads. An earlier run at load average 7 to 20 showed the same CPU per call and 4 to 5 times worse wall time (other agents' load).

## 4. Gates

| Gate | Verdict | Detail |
|---|---|---|
| Plan 5.2: audit the kernel for global and static mutable state | **Met** | inventory in KERNEL_THREADS.md section 2; every static-storage symbol of the 26 kernel objects, the 171 openNURBS members and 16 Manifold members is in exactly one row. The N/A rows are a judgement from the source and from absence from any TSan report; the probe's call coverage is not exhaustive, so N/A is not proof. |
| Plan 5.2: run it under the thread sanitizer | **Met in bounded form** | 34 and 66 iterations instead of 200 for the `indep` and `shared` phases (time budget); `-O1`, production is `-O3 -DNDEBUG` (optimisation changes how often a race shows, not whether the variable exists); probe operations only |
| Plan 5.4 fallback (serialize kernel calls) forced? | **No** | only SubD and file I/O need the serial lane |
| Determinism of concurrent results | Met for the probe operations | bit-identical in all runs |
| Engine compute-bound parallel speedup | **Not measured here** | kernel adds no serialization (3.07 of 4 cores busy), but the engine on a real kernel is ADR-003 open item 1 |
| Production-flag (`-O3 -DNDEBUG`) TSan run | **Not measured** | |
| Features outside the probe: `features.cpp`, `rib.cpp`, `boss.cpp`, `sheet_metal.cpp`, `offset_solid.cpp`, `trimmed_offset.cpp`, `mesh_unwrap.cpp`, loft and sweep | **Not exercised** (safe by audit of the source: no mutable state found with `nm`) | coverage gap |
| Shared SubD; Manifold operations other than `Boolean`; deep-recursion stack use (adaptive subdivision at depth 10, importers); mesh format readers and writers | **Not measured** | coverage gap |
| Windows and macOS | **Not measured** (probe is POSIX only) | |

## 5. Findings (13 root causes)

Kernel code (fixed on `w0-kthreads`): 1. `PointCloud::SpatialIndex() const` built and stored the R-tree without a lock (shared input only; 8 TSan reports in 3 iterations); 2. `std::localtime` at `iges.cpp:192`, `step_curve.cpp:174` and `:723` (15 reports in 12 iterations).
openNURBS on paths the engine would use: 3. `ON_Surface::IsSphere` out-of-bounds read (`opennurbs_revsurface.cpp:1775`, `j <= sc1` should be `j < sc1`), which TSan reports as a race or heap-use-after-free (5 reports in 6 iterations; 8 to 34 hits per two-iteration process); 4. `ON_NextContentSerialNumber`
(`opennurbs_statics.cpp:96`), every SubD operation; 5. `ON_Brep::m_bbox` lazily written by const `BoundingBox()` (first use on a shared const Brep; `ON_RevSurface::m_bbox` has the same pattern and is covered by the same warm-up, not reported separately).
openNURBS and libuuid file I/O: 6. `ON_Hash32Table` serial number; 7. `ON_ComponentManifestTableIndex` serial number; 8. `ON_ComponentManifestImpl` serial number; 9. `ON_ERROR_COUNT` through `ON_IncrementErrorCount` (22 events per 3dm/STEP/IGES round trip); 10. the global UUID written by
`ON_ClassId::Create`; 11. `ON_3dmRevisionHistory::NewRevision`; 12. `localtime`/`tzset` reached through `ON_SunEngine` from every `ONX_Model` constructor; 13. first-call state of the bundled libuuid.

## 6. Fixes made, and what is not fixed

Made on `w0-kthreads` (NOT on `cand2`; they must be merged by the Wave 1 kernel job):

| commit | change | verification quoted by the audit |
|---|---|---|
| `9555319d` | `detail::PointCloudSpatialCache` with `std::mutex`, same pattern as `MeshSpatialCache` | TSan 8 reports before, 0 after (50 iterations, 4 threads); kernel CI run 43533020521: `PointCloud` 195 ok, `ModelAddPointCloud` 18 ok, 0 failed |
| `7f75a3bd` | `localtime_r` / `localtime_s` at three call sites | TSan 15 reports before, 0 after; CI: `SaveIges` 13, `LoadIges` 6, `SaveStep` 91, `LoadStep` 54, `IgesWrites` 46, `IgesConverts` 354, `StepWrites` 309, `StepReads` 50 ok, 0 failed |

Not fixed in the repository: openNURBS is fetched by CMake (`FetchContent`, tag `v8.34.26223.11001`), not vendored. The two verified hunks (IsSphere bound; atomic `ON_NextContentSerialNumber`, 5 and 2 TSan reports before, 0 after, identical volumes and face counts) are in
`dino8-core/kernel_tsan_probe/opennurbs-8.34-thread-safety.patch` and are not wired into the build (no configure and full rebuild could run on the shared machine). The seven remaining file I/O items are one-line changes that were not verified; the cheaper path is to keep all 3dm/STEP/IGES I/O behind one lock or one I/O thread.

**Kernel defect found while measuring (not thread related, not fixed):** `Brep::TessellateToClosedMeshAdaptive` on a sphere that is not centred at the origin. Serial, one thread: `Brep::Sphere(Point3d(cx, 0, 0), 1.5).TessellateToClosedMeshAdaptive(0.5)` returns 8 faces for `cx` = 0 but 2,683,676 faces for `cx` = 1 and 741,462
for `cx` = 2; with tolerance 0.01 it returns 702 faces for `cx` = 0, `std::bad_alloc` for `cx` = 1 (under a 4 GB address-space limit), 9,639,006 for `cx` = 2 and 702 for `cx` = 3. The root cause is in the adaptive refinement or the conforming fallback (`brep.cpp:8569`). It killed one 4-thread scaling run by exhausting memory. Because
tessellation is the first adopter of the engine (ADR-003), the engine must not feed it off-origin spheres until it is fixed. Its severity class under plan 9.2 is a QC decision; as measured it is unbounded memory and wrong-scale output on a common primitive.

## 7. Consequences

- The engine can use the pool for the whole geometry path (tessellation, mass properties, booleans, features), the single largest dependency of the 20 times speedup gate (ADR-003); the serial lane is small.
- Engine-published B-reps must be warmed once; this is a rule enforced by the publish step, not by the type system.
- The kernel build gains a patch step for openNURBS (FetchContent `PATCH_COMMAND`), owned by whoever owns the build, and a TSan job built from the probe (plan 9.2 "Sanitizers and soak").
- Kernel cancellation is absent: long booleans run to completion; the engine's visible-first behaviour at the kernel boundary is limited to not starting work (ADR-003 section 6).

## 8. Risks

- Coverage: the probe exercises 18 operations; N/A and "safe by audit" rows are judgements. A single unexercised feature may hold an unlocked cache.
- TSan at `-O1` with fewer iterations than planned may miss rare races; an `-O3` run and longer runs are not done.
- The `IsSphere` out-of-bounds read exists in every cand2 build until the patch is wired in.
- Lazy const caches are an idiom in openNURBS and the kernel (`m_bbox`, `MeshSpatialCache`, `PointCloudSpatialCache`); a new one added without a lock breaks the contract silently. A CI job (the probe) and a review rule are needed.
- Behavioural global switches (test oracles, Manifold params) are safe only while nobody writes them.

## 9. Open items

1. Merge `9555319d` and `7f75a3bd` into the kernel base through the normal CI gate; wire the openNURBS patch with `FetchContent_Declare(... PATCH_COMMAND ...)`.
2. Fix or contain the off-origin adaptive tessellation defect; add a regression test.
3. Extend the probe to the unexercised features and to shared SubD; run 200 iterations and an `-O3` build.
4. Decide on extending the openNURBS patch to the seven file I/O items, or keep the I/O lock.
5. Windows and macOS runs.
6. Turn the probe into the nightly TSan job (plan 9.2); it needs the `_deps` of a configured kernel build and about 150 MB.

## 10. What Wave 1 must build first

1. Kernel factory job: merge the two kernel fixes, wire the openNURBS patch, fix the off-origin tessellation defect with a failing test first.
2. `d8-ffi` kernel registration table: each kernel function declared `D8G_THREAD_SAFE` or `D8G_SERIALIZED` per section 2, with the C++ catch-all wrapper and the publish-time `BoundingBox()` warm-up.
3. The serial lane: one mutex around SubD and all file I/O kernel calls.
4. The nightly TSan job from `run_probe.sh` (reproduction: `bash dino8-core/kernel_tsan_probe/run_probe.sh --threads 4 --iters 200 --phase both`, with `KT_PATCH=1` for the patched run).
