# dino8-kernel thread-safety audit (Spike C input)

Question: can the incremental engine (plan 5.2 threads bullet, 5.4) call the kernel from several worker threads on
independent inputs? Answer, measured: **yes for tessellation, mass properties and the mesh and B-rep booleans; not for
SubD and file I/O (3dm, STEP, IGES), which stay behind one lock.** Conditions and the one-time warm-up for shared inputs
are in section 8.

| | |
|---|---|
| Audited | `dino8-kernel` at cand2 `1b690f52` (26 sources under `src/`, headers under `include/`), openNURBS `v8.34.26223.11001`, Manifold `v3.5.2` |
| Date and tools | 2026-11-14; g++ 13.3, ThreadSanitizer (libtsan 2), 4-core shared machine (load average 1.8 for the scaling run, 5 to 19 for the others) |
| Evidence | static audit (grep, `nm`, `readelf`) plus a probe that runs the kernel on 4 threads under ThreadSanitizer: `dino8-core/kernel_tsan_probe/probe.cpp` |
| Races found | **13 distinct root causes**: 2 in kernel code (both fixed on this branch), 11 in openNURBS and its bundled libuuid (3 on code paths the engine would use, 8 in 3dm/STEP/IGES file I/O). On tessellation, mass properties, the Manifold booleans and the B-rep mixed, planar and general boolean engines: 0 races in kernel code, plus one openNURBS out-of-bounds read (7.1). Counting rule in 4.3 |

## 1. Method

1. **Inventory** of every global, `static`, `thread_local`, function-local static and singleton: grep over `src/` and
   `include/`, then confirmed from the compiler's own output (`nm -C --defined-only` over the 26 objects of cand2's kernel
   base lists every data and bss symbol; `readelf -sW` lists the TLS symbols). Same `nm` pass over each openNURBS and
   Manifold archive member that the probe links (171 of 193 openNURBS members, 16 of 18 Manifold members).
2. **ThreadSanitizer probe.** All 26 kernel sources, the 171 openNURBS members, all 18 Manifold sources, the bundled
   zlib and UUID C files were rebuilt with `-fsanitize=thread -O1` (about 8 minutes wall on 2 jobs), so races inside the
   libraries are visible, not only races in kernel code. The first run of the kernel alone (libraries uninstrumented)
   missed the openNURBS findings below; instrument the libraries.
3. The probe starts a serial reference for every operation and thread (twice, to prove the kernel is deterministic), then
   runs the operations on 4 threads, 200 iterations, aligned (all threads in the same operation) and rotated (different
   engines overlap) on alternate iterations. Every concurrent result is compared bit for bit with the serial result, so
   interference shows up as a divergence even without the sanitizer.
   Phases: `indep` (per-thread inputs: box minus cylinder in the mixed and the general engine, sphere union box, a
   two-cylinder compound-cutter difference `MakeHoles`, a compound planar difference, tessellation, mass properties, a
   Manifold mesh boolean), `features` (fillet and chamfer, revolve, extrude, pipe, convex hull, SubD, 3dm/STEP/IGES round
   trip), `shared` (one const Brep, Mesh and PointCloud read by all threads; welded solids; the same operands fed to
   concurrent booleans), `--cold` (no serial warm-up, so first-call races are visible), `--stack-report`.

## 2. Inventory of mutable state

### 2.1 dino8-kernel (`src/`, `include/`)

`nm` over the 26 cand2 objects finds exactly 18 non-library static-storage symbols; each is accounted for. There are no
singletons, registries or global caches, no `rand`/`srand`/`strtok`/`setlocale`/`setenv`, no header-defined `inline`
variables, no `static` data in `include/dino8/kernel/detail/`. Tolerances (`include/dino8/kernel/tolerance.h`) are all
`constexpr`.

| file:line | what it holds | read-only after init? | protected? |
|---|---|---|---|
| `src/boolean.cpp:208` `g_body_broad_phase_tree_queries` | diagnostic counter (R-tree broad phase use), incremented at `:262`, read by `BodyBroadPhaseTreeQueryCount()` `:362` | no | `std::atomic<long>` |
| `src/surface_intersect.cpp:2991` `g_brep_spatial_acceleration_enabled` | test-only oracle switch (indexed versus brute-force scans, results bit-identical by contract) | no, flipped only by tests | `std::atomic<bool>`; **behavioural global**: do not flip while calls are in flight |
| `src/surface_intersect.cpp:2998` `g_brep_face_box_index_builds` | counter | no | `std::atomic<long>` |
| `src/surface_intersect.cpp:3003`, `:3004` `g_brep_naked_edge_index_minimum`, `_queries` | test knob (16) and counter | knob: tests only | `std::atomic<int>`, `std::atomic<long>` |
| `src/mesh.cpp:504` `g_mesh_spatial_acceleration_enabled` | test-only oracle switch | tests only | `std::atomic<bool>` |
| `src/brep.cpp:1153`, `:1154`, `:1158` `g_trimmed_face_rel_tol`, `_max_depth`, `_follow_neighbor_curves` | integration knobs of `VolumeOfSealedSolid()` | per call | `thread_local`, set and restored by the RAII `Knobs` at `:1740-1751` |
| `src/surface_intersect.cpp:856` `g_keyhole_tolerant_trim_test` | flag for one body-section call | per call | `thread_local`, RAII `KeyholeTolerantScope` `:2251` |
| `include/dino8/kernel/mesh.h:2869` `Mesh::spatial_cache_` (type `detail::MeshSpatialCache`, `:90-115`) | lazily built closest-point index, built inside const queries (`mesh.cpp:515-533`) | built once per object | per-object `std::mutex`; copies start empty |
| `include/dino8/kernel/point_cloud.h:404` `PointCloud::spatial_index_` (cand2) | lazily built R-tree, built inside const `KNearest`/`PointsWithinRadius` (`point_cloud.cpp:54-65`) | built once per object | **none in cand2: race on a shared const cloud (found, fixed on this branch with a mutex)** |
| `src/brep.cpp:8321` `kNoMatches`, `src/surface.cpp:2879` `kNoForced` | function-local `static const std::vector` (empty) | yes | C++11 guarded initialisation |
| `src/mesh.cpp:476` `kContainsPointDirections`, `src/surface_intersect.cpp:3317` `kBrepContainsPointDirections` | ray direction tables needing dynamic initialisation | yes, set before `main` | not needed |
| `src/sweep.cpp:834-835`, `src/fillet.cpp:5242`, `src/surface_intersect.cpp:4628`, `src/mesh.cpp:4037`, `src/acis_sat.cpp:2119-2120` | `static const` lookup tables | yes (`.rodata`) | not needed |
| `src/iges.cpp:192`, `src/step_curve.cpp:174`, `:723` | `std::localtime`: returns a pointer to one process-wide static buffer | n/a | **none in cand2 (found, fixed on this branch with `localtime_r`/`localtime_s`)** |
| `std::getenv` of `DINO8_BOOL_DEBUG` (13 sites), `DINO8_BOOL_DEBUG_VERBOSE` (10), `DINO8_SEAM_REPAIR_DEBUG` (3), `DINO8_RECONCILE_DEBUG` (2), `DINO8_SEAM_DEBUG`, `DINO8_NO_SEAM_REPAIR`, `DINO8_MESH_DEBUG`, `DINO8_FACE_ORIGIN_DEBUG` (1 each), `src/boolean_general.cpp`, `brep.cpp`, `surface.cpp`, `mesh.cpp` | debug switches (`DINO8_NO_SEAM_REPAIR` changes geometry) | yes | reads are safe unless another thread calls `setenv`; neither kernel nor app do |

All 4 `thread_local` variables are the ones in the table (`readelf` finds exactly those). The `Set*Enabled` switches have
no caller outside the tests. The app already runs kernel tessellation concurrently in production (`ParallelFor` in
`dino8-app/src/main.cpp:305`, `io/FileExchange.cpp:4178`) but keeps `HoleArray` booleans serial "until someone verifies
thread-safety in those libraries" (`commands/cmd_solidtools.cpp:949`); this audit is that verification.

### 2.2 Manifold 3.5.2 and `-DMANIFOLD_PAR=-1`

`MANIFOLD_PAR` selects Manifold's execution policy and must be `1` (parallel, oneTBB) or `-1` (series)
(`manifold-src/src/utils.h:24-31`). The kernel's CMake sets the option `OFF`, Manifold's own CMake then adds the public
definition `MANIFOLD_PAR=-1` (`src/CMakeLists.txt:106-109`), and `kb_objs.sh` repeats it for single-file builds. With `-1`
every `manifold::for_each`, `transform`, `reduce`, `sort` and scan in `parallel.h` runs sequentially on the calling thread:
**Manifold starts no threads and links no TBB.** For the engine that is the right setting: each boolean is one
single-threaded task and the engine's pool supplies the parallelism, with no nested pool and no oversubscription. With
`1`, every boolean would fan out into TBB's global pool.

Global state in Manifold: `Manifold::Impl::meshIDCounter_` (`impl.cpp:83`, `std::atomic<uint32_t>`, mesh ID allocator, atomic);
`manifoldParams` (`manifold.cpp:27`, `ExecutionParams`, plain struct read by the boolean paths; **nobody writes it** here,
so it is effectively constant: do not set `ManifoldParams()` while workers run); `circularAngle_`, `circularSegments_`,
`circularEdgeLength_` (`manifold.cpp`, only used by primitive constructors the kernel does not call);
`Partition::cache` with `cacheLock` (`subdivision.cpp`, mutex); `static std::atomic<int> counter` (`execution_impl.cpp:62`);
function-local `static const std::regex` in the OBJ reader (unused). Cooperative cancellation exists in this version
(`Manifold::WithContext(ExecutionContext)`, `Error::Cancelled`) but the kernel does not use it.

### 2.3 openNURBS 8.34

| where | what | status |
|---|---|---|
| `opennurbs_defines.h:277-320, 575, 587` | `ON_EPSILON`, `ON_SQRT_EPSILON`, `ON_ZERO_TOLERANCE` (2^-32), `ON_RELATIVE_TOLERANCE`, `ON_UNSET_VALUE` | preprocessor constants: **no mutable tolerance global** |
| `opennurbs_public_memory.cpp:28-60` | `onmalloc`/`onfree`/`onrealloc` | thin wrappers of libc `malloc`/`free`: **no memory pool**, as thread-safe as libc. `ON_Workspace` is per instance |
| `opennurbs_error.cpp:42-49` | `ON_ERROR_COUNT`, `ON_WARNING_COUNT`, `ON_MATH_ERROR_COUNT` (plain `static int`, `++` on every `ON_ERROR`/`ON_WARNING`), `ON_ACTIVE_ERROR_LOG`, `ON_DEBUG_ERROR_MESSAGE_OPTION` | **unprotected**. Measured: 0 events during every boolean, tessellation, mass-property, fillet, sweep and hull run; 22 per call of the 3dm/STEP/IGES round trip (4 of them from `Model::Load` alone), where TSan reports the race |
| `opennurbs_statics.cpp:96` `ON_NextContentSerialNumber` | process-wide serial counter used by SubD content stamps (`opennurbs_subd_copy.cpp:761,777`, `opennurbs_subd_fragment.cpp:3398`) and component status | **unprotected** (source comment: "can be made atomic"). Race on independent inputs in SubD |
| `opennurbs_object.cpp:74-75, 155-156` `ON__isnand`/`ON__isnanf` statics, `:422` `bSizedValidated` | lazily initialised constants (same value written) | formally racy on first call; no report in 6 cold-start runs |
| `opennurbs_object.cpp:835,847` `s_most_recent_class_id_create_uuid` | global written by every `ON_ClassId::Create()` (3dm reading) | **unprotected**; source: "The C++ opennurbs toolkit never uses this value" |
| `opennurbs_object.cpp` `ON_ClassId::m_p0`, `m_mark0` | class id registry | built by static initialisers, never mutated afterwards (the kernel never calls `PurgeAfter`/`IncrementMark`) |
| `opennurbs_hash_table.cpp:115`, `opennurbs_archive_manifest.cpp:3066, 3536` | serial number counters of every hash table and component manifest (constructed with every `ONX_Model`) | **unprotected**, 3dm I/O only |
| `opennurbs_brep.cpp:6722` `ON_Brep::InternalBrepBoundingBox`, `opennurbs_revsurface.cpp:1561` | bounding box cached into `m_bbox` through `const_cast` inside const `BoundingBox()`/`GetBBox()` | **unprotected lazy cache**: race on a shared const Brep at first use; cheap warm-up (7.4) |
| `opennurbs_revsurface.cpp:1775` `ON_Surface::IsSphere` | loop `j <= sc1` reads `t[sc1+1]`, one `double` past a `malloc` block | **memory-safety bug** (siblings `IsCylinder`/`IsCone`/`IsTorus` use `j < sc1`); TSan sees it as a race because the neighbouring heap block belongs to another thread |
| `opennurbs_rand.cpp:199` `static_randcontext` | legacy `on_rand()`/`on_srand()` generator, documented "not thread safe" | no caller in the linked members (only per-object `ON_RandomNumberGenerator`) |
| `android_uuid/gen_uuid.c:141` `static int fd = -2` and `srand` | first-call initialisation of the UUID generator (`ON_CreateUuid`, used by `file_io.cpp:64, 579, 938` and by 3dm writing) | **unprotected**; 3dm I/O only |
| `opennurbs_3dm_properties` `ON_3dmRevisionHistory::NewRevision`, `ON_SunEngine::GetDefaultLocalDateTime` -> `localtime` (reached from every `ONX_Model` constructor) | clock and time zone through libc static state | **unprotected**, 3dm I/O only |
| `opennurbs_planesurface.cpp g_data_list` (+lock), `opennurbs_unicode.cpp` Big5 tables (+lock), `opennurbs_units.cpp` unit-name caches, `opennurbs_bezier.cpp` `Tcache` | clipping planes, code pages, unit parsing, Bezier grid matrices | not reachable from the kernel |

### 2.4 Verdict for every mutable symbol that remains

Every static-storage symbol of the 26 kernel objects, of the 171 openNURBS members and the 16 Manifold members the probe
links (`nm -C --defined-only`, data, bss and TLS) is in exactly one row. Verdicts: **OK** = safe as it is; **WARM** = safe
once warmed or published (a rule, section 8); **FIXED** = unsafe, fixed on this branch; **PATCH** = unsafe, fixed by the
openNURBS patch, verified; **LOCK** = unsafe, not fixed, keep behind the engine's kernel lock; **N/A** = not reachable
from the kernel.

| symbol(s) | count | verdict |
|---|---|---|
| kernel `g_body_broad_phase_tree_queries`, `g_brep_face_box_index_builds`, `g_brep_naked_edge_index_queries` | 3 | OK (`std::atomic<long>`) |
| kernel `g_brep_spatial_acceleration_enabled`, `g_mesh_spatial_acceleration_enabled`, `g_brep_naked_edge_index_minimum` | 3 | OK (`std::atomic`); behavioural test switches: never flip while workers run |
| kernel `g_trimmed_face_rel_tol`, `_max_depth`, `_follow_neighbor_curves`, `g_keyhole_tolerant_trim_test` | 4 | OK (`thread_local`, RAII-scoped to one call) |
| kernel function-local `static const` (`kNoMatches`, `kNoForced`, direction tables, lookup tables, SAT `kTrue`/`kFalse`) | 11 | OK (const, guarded or `.rodata`) |
| kernel `Mesh::spatial_cache_` | per object | OK (per-object mutex) |
| kernel `PointCloud::spatial_index_` | per object | FIXED (mutex, `9555319d`) |
| kernel `std::localtime` at 3 call sites | 3 | FIXED (`localtime_r`, `7f75a3bd`) |
| Manifold `meshIDCounter_`, `execution_impl.cpp` counter | 2 | OK (`std::atomic`) |
| Manifold `Partition::cache` + `cacheLock` | 1 | OK (mutex) |
| Manifold `manifoldParams`, `circularAngle_`, `circularSegments_`, `circularEdgeLength_` | 4 | OK while nobody writes them (nobody does); never write while workers run |
| Manifold OBJ reader `static const std::regex` | 1 | N/A (reader unused) |
| openNURBS class-id registration: `ON_*::m_s_*_ptr`, `*_class_rtti`, `ON_ClassId::m_p0`, `m_p1`, `m_mark0` | 255 + 3 | OK (written by static initialisers before `main`, never mutated; the kernel never calls `PurgeAfter`/`IncrementMark`) |
| openNURBS named constants (`ON_3dPoint::Origin`, `ON_Interval::ZeroToOne`, `ON_UnitSystem::*`, `ON_Linetype::*`, `ON_HatchPattern::*`, `ON_SubD*::Empty`, `ON_PlaneEquation::*`, `ON_DBL_QNAN`, ..., the `UnitVector(int)` locals, `Unset`/`Empty`/`Default*` objects) | about 430 | OK (initialised before `main`, read only; C++11 guards for function-local ones) |
| openNURBS `ON_RenderContentType_*`, `ON_UniversalRenderEngineId` | 57 | OK (const ids) |
| openNURBS `ON_UnitSystem::UnitSystemName()::s_name` | 27 | OK (function-local, guarded) |
| openNURBS `ON_ERROR_COUNT`, `ON_WARNING_COUNT`, `ON_MATH_ERROR_COUNT` (plain `int`), `ON_ACTIVE_ERROR_LOG`, `ON_DEBUG_ERROR_MESSAGE_OPTION` | 5 | LOCK for file I/O (races there); 0 events on every other path |
| openNURBS `ON_NextContentSerialNumber` | 1 | PATCH |
| openNURBS `ON_Surface::IsSphere` out-of-bounds read (not a symbol; code defect) | 1 | PATCH |
| openNURBS `ON_Brep::m_bbox`, `ON_RevSurface::m_bbox` (lazy cache in const `BoundingBox()`) | per object | WARM |
| openNURBS `ON_Hash32Table_NewSerialNumber()::sn`, `ON_ComponentManifestTableIndex_sn_generator`, `ON_ComponentManifestImpl_SerialNumberGenerator()::manifest_impl_sn` | 3 | LOCK (plain integers, 3dm/STEP/IGES I/O) |
| openNURBS `s_most_recent_class_id_create_uuid` | 1 | LOCK (written by `ON_ClassId::Create`, 3dm reading) |
| openNURBS `ON_3dmRevisionHistory::NewRevision`, `ON_SunEngine` default time (libc `localtime`/`tzset`) | 2 | LOCK |
| openNURBS bundled libuuid `get_random_fd` `static int fd`, `srand` seed | 2 | LOCK |
| openNURBS `ON_ModelComponent::Internal_RuntimeSerialNumberGenerator`, `ON_SubDimple::Internal_RuntimeSerialNumberGenerator` | 2 | OK (`std::atomic<ON__UINT64>`, `opennurbs_statics.cpp:377,379`) |
| openNURBS `ON__isnand::b6/b7`, `ON__isnanf::b2/b3`, `ValidateSizesHelper()::bSizedValidated` | 5 | OK in practice: lazily computed constants, every writer stores the same value (formal race on first call only; 0 reports in 6 cold runs) |
| openNURBS `g_mutex` (`opennurbs_3dm_attributes`), `g_data_list` + `g_data_list_lock` + `AppendNew()::serial_number` (clipping planes), `ON_TestClass::internal_*` | 6 | N/A (not reachable; the clipping-plane list has its lock) |
| openNURBS `g_pContentHashCache` (`std::weak_ptr`, `opennurbs_file_utilities.cpp:2944`) | 1 | N/A (only `ON_FileSystem` content hashing, which the kernel does not call) |
| openNURBS `static_randcontext` (`on_rand`) | 1 | N/A (no caller in the linked members; documented "not thread safe") |
| openNURBS `g__bLeakUserData` (`ON__EnableLeakUserData`, never called), `g_bDisableDemotion` (set inside `ON_ClassId` construction, i.e. static initialisation) | 2 | OK |
| openNURBS `Internal_func_ModelLengthUnitSystemCallback` (`ON::RegisterModelLengthUnitSystemCallback`), `ON_NumberFormatter::bFormatIsAccurate` | 2 | OK (set once by the host application, never by the kernel) |
| openNURBS SubD globals: `ON_SubD_FixedSizeHeap::m__sn_factory` (plain `unsigned`, `opennurbs_subd_heap.cpp:76`), `ON_SubDHeap::m_offset_*_id`, `g_sizeof_fragment`, `LimitPointPool` (`limit_point_fsp`, `initialize_lock`), `QuadGridFromDisplayDensity` (`grid_cache`, `lock`), `ON_SubDMatrix::FromCache` (`hash_table`, `lock`), `Internal_TransientIdHelper` (`src`, `global_resource_lock`), `ON_SubDLevel::Read()::format` | 14 | LOCK: SubD stays behind the engine lock. Most are guarded by their own `ON_SleepLock`; `m__sn_factory` is an unprotected counter, and TSan reported only `ON_NextContentSerialNumber` for the SubD operations probed |
| openNURBS `ON_TextRunPool::thePool`, `RtfComposer::m_*`, `empty_astring`, `ON_TextStyle::binary_archive_opennurbs_version`, `ON_Big5*` tables (+lock), `ON_MapUnicodeToMSSBCP::utocp10000`, `sglb_asets`, `UNSET_4D_POINT`, `ON_PointGrid::m_no_point` | about 20 | N/A (annotation text, code pages and obsolete V5 code, or const) |
| openNURBS font name maps, `ON_Font::*` lazy tables, `ON_ManagedFonts::List`, `ON_CRT_LOCALE::m_validation_*`, `ON_ParseSettings::*`, `GetFunction()::f`, dimension-style and arrowhead tables, `ON_Bezier` `Tcache`, `ON_MD5::padding`, `Default*Private` objects | about 45 | N/A (font, parsing, dimension and rendering code; no kernel entry point calls it) |

The two largest groups (class-id objects, named constants, unit names and render-content ids, about 770 of the 948 openNURBS symbols) are written only by
static initialisers. The `N/A` verdicts are a judgement from the source and from the fact that none of those symbols shows
up in any TSan report over all 18 probe operations; the probe's call coverage is not exhaustive, so an `N/A` row is not a
proof (section 8, coverage gaps). Counts are of `nm` lines, rounded where several lines belong to one logical table.

## 3. ThreadSanitizer results (4 threads unless noted)

| run | build | scope | result |
|---|---|---|---|
| `indep`, 34 iterations x 4 threads x 8 operations = 1088 calls (136 per operation), 609 s wall (the 600 s budget; 200 iterations were not reachable under ThreadSanitizer on the shared machine) | kernel + openNURBS + Manifold instrumented, openNURBS patch applied (no suppressions) | per-thread inputs | **0 reports**, 0 divergences |
| `shared`, 66 iterations x 4 threads x 5 operations = 1320 calls, 421 s wall | same | one const Brep, Mesh, PointCloud, welded solids and booleans on the same operands, caches reset every iteration | **0 reports**, 0 divergences |
| `features` fillet, chamfer, revolve, extrude, pipe, convex hull, 200 iterations x 4 threads = 800 calls each, 140 s | same, patch applied | per-thread inputs | **0 reports**, 0 divergences |
| `features` SubD: 100 iterations unpatched; 200 iterations (800 calls) patched | same | per-thread inputs | unpatched 2 reports, one location: `ON_NextContentSerialNumber`; **patched: 0 reports** |
| `features` 3dm + IGES + STEP round trip: 12 iterations (before the `localtime_r` fix); 200 iterations (800 calls, patched) | same | per-thread files | 94 reports in 7 locations; 4 cold processes add the libuuid ones (75 reports); the 200-iteration run: 236 reports, **every one inside the file I/O operation** (`ON_Hash32Table` 113, `ON_IncrementErrorCount` 55, manifest serial numbers 38, `free` 19, `ON_ClassId::Create` 6, `gmtime`/`localtime` 5); root causes 6 to 13 of section 4 |
| `--cold`, `indep`, 6 processes | same | no warm-up | **0 reports** |
| `--cold`, `shared`, 8 processes | same | no warm-up | 2 of 8 processes report, 13 reports, one location (`ON_Brep` bounding box cache); with `--prewarm-bbox`: **0 of 8** |
| plain build (no sanitizer), `all` phases, 60 iterations x 4 threads (4320 calls: indep 8 operations, features 5, shared 5, 240 calls each), 131 s | `-O1`, no instrumentation | all | **0 divergences, 0 exceptions** (the 5456 openNURBS error events are the file I/O operation's own, section 2.3) |

`ON_Surface::IsSphere` (item 7.1) is suppressed in the long runs with `race:ON_Surface::IsSphere` so that it cannot hide
other reports; it matched 8 to 34 times per two-iteration process, and the unsuppressed 6-iteration run shows it as 5
reports (4 "heap-use-after-free", 1 "data race"). With the one-character fix the same run gives 0 reports and identical
results.

Determinism (plan 5.3, replay tests): the serial reference was reproduced exactly on a second call for every operation,
and **every concurrent result equalled its serial result bit for bit** (0 divergences in all runs above): neither the
results nor the face counts depend on thread interleaving, in kernel, openNURBS or Manifold (Manifold's mesh IDs depend on
call order but never reach the results).

Stack: peak use per call on a worker thread, measured on a pattern-filled private stack (the instrument reads 6.2 KiB for
an empty function and 104.4 KiB for a 100 KiB `alloca`): 15.5 to 27.8 KiB over all 13 operations at `-O1`. A Rust thread's
2 MiB default is ample for these paths; deep-recursion paths not in the probe (adaptive subdivision at depth 10, file
importers) were not measured.

### 3.1 Scaling (plain `-O1` build, `indep` phase, 12 iterations per thread, stagger 0)

Measured on a quiet machine (load average 1.8; a pure-CPU control with no kernel code gave wall/CPU 1.01 at 1 and 2
threads and 1.40 at 4 threads, which is the noise floor of this shared box). "CPU" is per-call thread CPU time
(`CLOCK_THREAD_CPUTIME_ID`), "wall" the elapsed time of the same call; `plain_runs2/scaling2.txt` has the raw output.

| operation | CPU ms per call at 1 / 2 / 4 threads | wall ms per call at 4 threads | wall / CPU at 4 threads |
|---|---|---|---|
| `box_cyl_mixed` (mixed boolean) | 17.0 / 17.7 / 17.7 | 19.9 | 1.12 |
| `box_cyl_general` (general boolean) | 197.7 / 203.9 / 203.6 | 229.5 | 1.13 |
| `sphere_box_union` | 150.8 / 149.3 / 149.0 | 162.4 | 1.09 |
| `compound_cutter` | 139.5 / 142.2 / 142.9 | 152.1 | 1.06 |
| `compound_planar` | 7.6 / 7.6 / 7.4 | 7.7 | 1.04 |
| `mass_props` | 4.2 / 4.3 / 4.2 | 4.6 | 1.10 |
| `mesh_boolean` (Manifold) | 12.4 / 12.2 / 12.2 | 13.5 | 1.11 |
| `tessellate` | 13.3 / 31.5 / 90.5 | 94.3 | 1.04 |

Seven of the eight operations cost the same CPU on 4 threads as on 1 (within 4 percent) and wall time exceeds CPU time by at most 13 percent, less than the
40 percent the pure-CPU control loses at 4 threads: no operation waits on a lock or on the allocator. `tessellate` is not a contention effect:
thread `tid` builds a sphere centred at `(tid, 0, 0)` with radius `1.5 + 0.2 tid`, and `TessellateToClosedMeshAdaptive`
costs about 7 ms for `tid` 0 but about 43 ms, 47 ms and 220 ms for 1, 2 and 3 (serial, whole-program time over 3 calls) (section 7.5), so the mean over the threads grows with
the thread count while the cost per input does not. Whole phase: 96 calls in 6.6 s on 1 thread, 192 in 7.1 s on 2, 384 in
9.8 s on 4. Total thread CPU divided by wall is **0.99 cores busy at 1 thread, 3.07 of 4 cores at 4 threads**; the rest
is the one thread whose `tessellate` input is 30 times dearer than the others. An earlier run on a machine with load
average 7 to 20 (`plain_runs2/scaling.txt`) shows the same CPU per call (booleans within 13 percent) and 4-5 times worse
wall time, which is other agents' load, not the kernel.

Caveat: the probe is built at `-O1` (production is `-O3 -DNDEBUG`); optimisation level changes how often a race shows,
not whether the shared variable exists, and the static inventory above is independent of it. The probe was extended
during the audit (modes, ops, an output column); every run used the same operations on the same sources.

## 4. Findings (13 distinct root causes)

Kernel code (both fixed on this branch, section 6):

1. `PointCloud::SpatialIndex() const` builds and stores the R-tree without a lock (`point_cloud.cpp:55` read, `:62` write
   of the `shared_ptr`, plus its reference count). Shared input only. 8 reports in 3 iterations.
2. `std::localtime` at `iges.cpp:192`, `step_curve.cpp:174`, `:723` (libc static `tm`, `tzset`): three call sites, one
   cause. 15 reports from the writers in 12 iterations.

openNURBS, on paths the engine would use (patch or rule in section 7):

3. `ON_Surface::IsSphere` out-of-bounds read, `opennurbs_revsurface.cpp:1775`. Independent inputs, `BooleanCombineGeneral`
   on a NURBS sphere. TSan reports it as a data race or heap-use-after-free; it is a plain bug that no thread count is
   needed to trigger.
4. `ON_NextContentSerialNumber`, `opennurbs_statics.cpp:96`. Independent inputs, every SubD operation.
5. `ON_Brep::m_bbox`, lazily written by the const `BoundingBox()` (`opennurbs_brep.cpp:6722`), read by `IsValid`. Shared
   const Brep, first use only. (`ON_RevSurface::m_bbox`, `opennurbs_revsurface.cpp:1561`, has the same pattern; it was
   not reported separately and the same warm-up covers it.)

openNURBS and libuuid, in 3dm/STEP/IGES file I/O (`ONX_Model` construction, `Model::Save`/`Load`, `Model::AddBrep`):

6. `ON_Hash32Table` serial number; 7. `ON_ComponentManifestTableIndex` serial number; 8. `ON_ComponentManifestImpl` serial
number; 9. `ON_ERROR_COUNT` through `ON_IncrementErrorCount` (`opennurbs_error.cpp:116`); 10. the global UUID written by
`ON_ClassId::Create` (`opennurbs_object.cpp:847`); 11. `ON_3dmRevisionHistory::NewRevision`; 12. `localtime`/`tzset`
reached through `ON_SunEngine` from every `ONX_Model` constructor (`CurrentLocalTimeAsStructTM`); 13. the first-call
state of the bundled libuuid (`get_random_fd`, `gen_uuid.c:144`, and the `read` on it, `:183`).

### 4.3 How races are counted

One race = one root cause: one shared variable or one faulty statement, however many reports and stack pairs
ThreadSanitizer printed for it (it prints a new report for every distinct pair of stacks). Raw report counts, for scale:
8 (PointCloud, 3 iterations), 15 (kernel `localtime`, 12 iterations), 5 (`IsSphere`, 6 iterations), 2 (SubD, 100
iterations), 13 (bounding box, 8 cold processes), 94 (file I/O, 12 iterations), 75 (file I/O, 4 cold processes). The
file I/O counts include item 2's `tzset`. Items 1 to 13 are the total: 13.

## 5. Classification

"Independent" means every thread builds its own inputs; "shared" means one immutable object read by several threads.

| subsystem | files | independent inputs | shared const input | evidence |
|---|---|---|---|---|
| Primitives, `NurbsCurve`/`NurbsSurface`/`Brep` construction, transforms | `curve.cpp`, `surface.cpp`, `brep.cpp`, `brep_seam.cpp`, `surface_edit.cpp` | **safe now** | safe after bounding-box warm-up (7.4) | probe ops, 0 reports |
| Tessellation (`Tessellate*`, `TessellateToClosedMesh`, conforming, adaptive, `MergeAndWeld`) | `brep.cpp`, `mesh.cpp`, `surface.cpp` | **safe now** (app already does this) | safe after warm-up | `tessellate`, `shared_brep_queries` |
| Mass properties (`Volume`, `Area`, `VolumeMassProperties`, `VolumeOfSealedSolid`, `Mesh::VolumeMassProperties`) | `brep.cpp`, `mesh.cpp` | **safe now** (`thread_local` knobs are RAII-scoped to one call) | safe after warm-up | `mass_props`, `shared_welded_queries` |
| Mesh queries (`ContainsPoint`, `ClosestPoint`, `SignedDistance`) | `mesh.cpp` | safe now | **safe** (per-object mutex) | `shared_mesh_queries` |
| Mesh booleans, split, hull, simplify (Manifold) | `boolean.cpp` | **safe now** with `MANIFOLD_PAR=-1`; boolean verified, the others by audit | safe | `mesh_boolean` |
| B-rep mixed and planar booleans, N-ary, compound operands | `boolean.cpp` | **safe now** | safe after warm-up | `box_cyl_mixed`, `compound_planar`, `shared_boolean_operands` |
| B-rep general boolean, `MakeHole(s)`, compound cutter | `boolean_general.cpp`, `surface_intersect.cpp`, `sweep.cpp` (pipe cutter) | **safe now apart from the `IsSphere` out-of-bounds read** (benign in practice: 8 bytes past a small block, never a crash seen; fix is one character, 7.1) | same, plus warm-up | `box_cyl_general`, `sphere_box_union`, `compound_cutter` |
| Fillet, chamfer, revolve, extrude, pipe, convex hull | `fillet.cpp`, `sweep.cpp`, `convex_hull.cpp` | **safe now** (200 iterations, 0 reports) | not run | `fillet_chamfer`, `sweep_family`, `convex_hull` |
| Other features (`features.cpp`, `rib.cpp`, `boss.cpp`, `sheet_metal.cpp`, `offset_solid.cpp`, `trimmed_offset.cpp`, `mesh_unwrap.cpp`, loft and sweep) | those files | safe by audit (no mutable state in them, `nm`); **not exercised** | not run | coverage gap, section 8 |
| Point clouds | `point_cloud.cpp` | safe now | **fixed here** (was unsafe) | `shared_pointcloud_knearest`: 8 reports before, 0 after |
| SubD | `subd.cpp` + openNURBS `ON_SubD` | **unsafe** (global serial counter) | not run | `subd`: 2 reports; patch verified: 0 |
| File I/O: 3dm `Model`, STEP and IGES curve and surface, `Model::Load` | `file_io.cpp`, `iges.cpp`, `step_curve.cpp`, `acis_sat.cpp` | **unsafe** (openNURBS and libuuid globals, 8 root causes: section 4, items 6 to 13) | not applicable | `file_io`: 94 reports |
| Mesh format readers and writers (OBJ, STL, PLY, ...) | `mesh.cpp`, `point_cloud.cpp` | safe by audit (own `std::ifstream`/`std::ofstream` only) | not applicable | not exercised |
| Debug and test switches | listed in 2.1 | do not change while calls are in flight | | |

## 6. Fixes made on this branch (small, verified)

| commit | change | verification |
|---|---|---|
| `9555319d` | `detail::PointCloudSpatialCache` with `std::mutex`, same pattern as `MeshSpatialCache`; copying a cloud still shares the built tree | TSan: 8 reports before, 0 after (50 iterations, 4 threads, index reset every iteration); kernel CI run 43533020521: filters `PointCloud` 195 ok, `ModelAddPointCloud` 18 ok, 0 failed |
| `7f75a3bd` | `localtime_r`/`localtime_s` at three call sites in `iges.cpp` and `step_curve.cpp` | TSan: writer reports 15 before, 0 after; CI: `SaveIges` 13, `LoadIges` 6, `SaveStep` 91, `LoadStep` 54, `IgesWrites` 46, `IgesConverts` 354, `StepWrites` 309, `StepReads` 50 ok, 0 failed |

## 7. Unsafe items not fixed in the repository, with the minimal fix

openNURBS is fetched by CMake (`FetchContent`, tag `v8.34.26223.11001`), not vendored, so these fixes are a patch
(`dino8-core/kernel_tsan_probe/opennurbs-8.34-thread-safety.patch`, applies cleanly, both hunks verified under
ThreadSanitizer) to be wired in with `FetchContent_Declare(... PATCH_COMMAND ...)` by whoever owns the build; I did not
change `CMakeLists.txt` because a configure and full rebuild cannot be run on the shared machine.

1. **`ON_Surface::IsSphere` out-of-bounds read** (`opennurbs_revsurface.cpp:1775`): `for (int j = 0; j <= sc1; j++)` to
   `j < sc1`. Verified: same run 5 reports before, 0 after, identical volumes and face counts. Until patched, general
   booleans on NURBS spheres read 8 bytes past a heap block; no crash or wrong result was observed (8 to 34 hits per
   two-iteration process).
2. **`ON_NextContentSerialNumber`** (`opennurbs_statics.cpp:96`): `static std::atomic<ON__UINT64> serial_number(0)`.
   Verified: SubD 2 reports before, 0 after (100 iterations). Without the patch, run SubD under the engine's kernel lock.
3. **File I/O**: 7 more counters and statics (error counters, hash table and manifest serial numbers, `ON_ClassId::Create`
   UUID, revision history, `localtime` in the `ONX_Model` constructor, libuuid first-call state) would each be a one-line
   `std::atomic`/`call_once`/`thread_local` change; not verified. Cheaper and safer: **all 3dm, STEP and IGES I/O through
   one lock or one dedicated I/O thread**.
4. **Shared immutable Brep**: nothing to fix, one rule: call `brep.raw().BoundingBox()` once, before the object becomes
   visible to other threads (the engine's snapshot-publish step). Verified: cold shared runs, 2 of 8 processes reported
   without it, 0 of 8 with it. A shared `PointCloud` needs the fix in section 6.

5. **Not thread related, found while measuring scaling: `Brep::TessellateToClosedMeshAdaptive` on a sphere that is not
   centred at the origin.** Serial, one thread: `Brep::Sphere(Point3d(cx, 0, 0), 1.5).TessellateToClosedMeshAdaptive(0.5)`
   returns 8 faces for `cx` = 0 but 2 683 676 faces for `cx` = 1 and 741 462 for `cx` = 2; with tolerance 0.01 it returns
   702 faces for `cx` = 0, `std::bad_alloc` for `cx` = 1 (under a 4 GB address-space limit), 9 639 006 for `cx` = 2 and
   702 for `cx` = 3. A translated sphere should mesh like the same sphere at the origin. Not fixed (outside this audit;
   the root cause is in the adaptive refinement or the conforming fallback, `brep.cpp:8569`); the engine should not feed it
   off-origin spheres until it is. It also makes the `tessellate` probe operation unequal across threads (section 3.1) and
   killed one 4-thread scaling run by exhausting memory. The reproducer is a 20-line program: build a `Brep::Sphere` and
   call the function above for the values listed.

## 8. Recommendation for the engine

**Concurrent kernel calls are allowed** (not serialised behind one lock, no process isolation) for tessellation, mass
properties, mesh and B-rep booleans, fillets, sweeps and hulls, with these conditions:

1. Run a kernel call to completion on one thread (the `thread_local` knobs are scoped per call). Do not migrate a call.
2. Warm each published Brep once (`BoundingBox()`) and build on this branch's `PointCloud` fix; apply the openNURBS patch
   (7.1 and 7.2) when convenient.
3. **Serialise SubD and all 3dm/STEP/IGES file I/O** behind one engine-wide kernel mutex (or a single I/O thread) until
   the patch is extended.
4. Never call the test switches (`detail::Set*Enabled`, `SetBrepNakedEdgeIndexMinimum`), `setenv`, or write Manifold's
   `ManifoldParams()`/quality settings while workers run. Keep `MANIFOLD_PAR=-1`.
5. Give workers at least 256 KiB of stack (measured peak 28 KiB, section 3); catch `std::exception` per task (the
   kernel reports failure by throwing).
6. Expect no cooperative cancellation: a running B-rep boolean cannot be aborted; the engine should discard stale
   results instead. Manifold 3.5.2 offers `ExecutionContext` cancellation for mesh booleans if wanted later.

Plan 5.4's fallback ("if the kernel cannot be called concurrently, the engine runs kernel calls serialized and the speedup
target is re-stated") is **not forced by thread-safety**: only SubD and file I/O sit in a serial lane. What speedup the
scheduler then reaches is a Spike C measurement; the scaling measurement in section 3.1 shows the kernel itself adds no
serialisation. Measured (section 3.1): 3.07 of 4 cores busy on 4 threads on an otherwise quiet machine, CPU per call
unchanged for every operation but one, and that one is a workload effect.

Coverage gaps (not measured): features other than fillet, chamfer, revolve, extrude, pipe and hull (section 5); shared
SubD; Manifold operations other than `Boolean`; deep-recursion stack use; Windows and macOS (the probe is POSIX only).

## 9. Re-running

```
bash dino8-core/kernel_tsan_probe/run_probe.sh --threads 4 --iters 200 --phase both          # builds, links, runs
KT_PATCH=1 bash dino8-core/kernel_tsan_probe/run_probe.sh --phase features --ops subd        # with the openNURBS patch
bash dino8-core/kernel_tsan_probe/run_probe.sh --phase shared --cold --iters 2               # first-use races
```

Verified end to end on 2026-11-14 with `KT_DEPS=<_deps of a configured build> KT_PATCH=1 KT_JOBS=2 run_probe.sh --threads 4
--iters 3 --phase both`: it compiles 142 translation units that were not yet built, links, runs both phases and prints
`ThreadSanitizer warnings ... so far: 0` with 0 divergences (about 131 MB in `KT_OUT`).

`run_probe.sh` needs the `_deps` directory of any configured `dino8-kernel` build (`KT_DEPS`), writes about 150 MB to
`KT_OUT`, and does not touch the repository's build tree. `probe.cpp --list` shows the operations, `--ops a,b` selects
some, `--stagger 0|1|2`, `--seconds N` caps the run, `--prewarm-bbox`, `--stack-report`. The probe is a candidate for the
sanitizer jobs of plan 9.2 ("Sanitizers and soak") and for the plan's risk row "kernel not thread-safe".
