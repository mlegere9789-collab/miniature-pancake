//! Spike A benchmark harness (plain, no external crates).
//!
//! usage: bench [--n 5000000] [--gate-n 1000000] [--skip-gate] [--skip-big]
//! Memory is measured with a counting global allocator (live heap bytes, no
//! allocator slack) plus VmRSS/VmHWM from /proc.

use dino8_scene::rng::Rng;
use dino8_scene::*;
use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering::Relaxed};
use std::time::Instant;

struct Counting;
static CUR: AtomicUsize = AtomicUsize::new(0);
static PEAK: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        let p = System.alloc(l);
        if !p.is_null() {
            let c = CUR.fetch_add(l.size(), Relaxed) + l.size();
            PEAK.fetch_max(c, Relaxed);
        }
        p
    }
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        System.dealloc(p, l);
        CUR.fetch_sub(l.size(), Relaxed);
    }
    unsafe fn realloc(&self, p: *mut u8, l: Layout, new: usize) -> *mut u8 {
        let q = System.realloc(p, l, new);
        if !q.is_null() {
            if new >= l.size() {
                let c = CUR.fetch_add(new - l.size(), Relaxed) + (new - l.size());
                PEAK.fetch_max(c, Relaxed);
            } else {
                CUR.fetch_sub(l.size() - new, Relaxed);
            }
        }
        q
    }
}

#[global_allocator]
static A: Counting = Counting;

fn live() -> usize {
    CUR.load(Relaxed)
}

fn mb(b: usize) -> f64 {
    b as f64 / 1048576.0
}

fn proc_kb(field: &str) -> usize {
    std::fs::read_to_string("/proc/self/status")
        .ok()
        .and_then(|s| s.lines().find(|l| l.starts_with(field)).map(|l| l.split_whitespace().nth(1).unwrap_or("0").parse().unwrap_or(0)))
        .unwrap_or(0)
}

#[derive(Clone, Default)]
struct Stats(Vec<u64>);

impl Stats {
    fn push(&mut self, ns: u64) {
        self.0.push(ns);
    }
    fn pct(&mut self, p: f64) -> u64 {
        if self.0.is_empty() {
            return 0;
        }
        self.0.sort_unstable();
        let i = ((self.0.len() as f64 - 1.0) * p).round() as usize;
        self.0[i]
    }
}

fn fmt_ns(ns: u64) -> String {
    if ns < 10_000 {
        format!("{ns} ns")
    } else if ns < 10_000_000 {
        format!("{:.1} us", ns as f64 / 1e3)
    } else {
        format!("{:.2} ms", ns as f64 / 1e6)
    }
}

fn row(name: &str, s: &mut Stats, note: &str) {
    println!(
        "| {} | {} | {} | {} | {} | {} | {} |",
        name,
        s.0.len(),
        fmt_ns(s.pct(0.5)),
        fmt_ns(s.pct(0.9)),
        fmt_ns(s.pct(0.99)),
        fmt_ns(*s.0.iter().max().unwrap_or(&0)),
        note
    );
}

struct Meta {
    buildings: Vec<Ix>,
    storeys: Vec<Ix>,
    walls: Vec<Ix>,
    doors: Vec<Ix>,
    elems: Vec<Ix>,
    height: PropKey,
}

const C_BUILDING: u16 = 2;
const C_STOREY: u16 = 3;
const C_WALL: u16 = 10;
const C_DOOR: u16 = 12;
const C_WINDOW: u16 = 13;
const C_RARE: u16 = 49;

fn pick_class(rng: &mut Rng) -> u16 {
    let r = rng.f64();
    if r < 0.0005 {
        C_RARE
    } else if r < 0.20 {
        C_WALL
    } else if r < 0.28 {
        11
    } else if r < 0.34 {
        C_DOOR
    } else if r < 0.40 {
        C_WINDOW
    } else if r < 0.45 {
        14
    } else if r < 0.50 {
        15
    } else {
        16 + rng.below(33) as u16 // 16..48
    }
}

fn generate(n: usize, cfg: Config, seed: u64) -> (Scene, Meta) {
    let mut rng = Rng(seed);
    let per_storey = 5000usize;
    let n_storeys = (n / per_storey).max(1);
    let n_buildings = (n_storeys / 50).max(1);
    let mut b = SceneBuilder::with_capacity(cfg, n);
    let height = b.register_prop("Height", PropKind::F64);
    let cost = b.register_prop("Cost", PropKind::F64);
    let fire = b.register_prop("FireRating", PropKind::I64);
    let name = b.register_prop("Name", PropKind::Str);
    let names: Vec<u32> = (0..1000).map(|i| b.intern(&format!("Element {i}"))).collect();

    let mut meta = Meta { buildings: vec![], storeys: vec![], walls: vec![], doors: vec![], elems: Vec::new(), height };
    let uuid = |rng: &mut Rng| Uuid::from_random_bits(rng.next_u128());

    for bi in 0..n_buildings {
        let ix = b
            .add(
                uuid(&mut rng),
                EntityParts {
                    semantic: Some(Semantic { class: C_BUILDING, ..Default::default() }),
                    geometry: Some(Geometry { bbox: Aabb::new([bi as f32 * 300.0, 0.0, 0.0], [bi as f32 * 300.0 + 100.0, 100.0, 200.0]), handle: 0 }),
                    ..Default::default()
                },
            )
            .unwrap();
        meta.buildings.push(ix);
    }
    for si in 0..n_storeys {
        let bi = si % n_buildings;
        let level = si / n_buildings;
        let ix = b
            .add(
                uuid(&mut rng),
                EntityParts {
                    semantic: Some(Semantic { class: C_STOREY, ..Default::default() }),
                    geometry: Some(Geometry { bbox: Aabb::new([bi as f32 * 300.0, 0.0, level as f32 * 3.5], [bi as f32 * 300.0 + 100.0, 100.0, level as f32 * 3.5 + 3.5]), handle: 0 }),
                    ..Default::default()
                },
            )
            .unwrap();
        b.link(RelKind::Containment, ix, meta.buildings[bi]);
        meta.storeys.push(ix);
    }
    let n_elems = n.saturating_sub(n_buildings + n_storeys);
    let mut last_wall: Vec<Option<Ix>> = vec![None; n_storeys];
    let mut recent: Vec<Vec<Ix>> = vec![Vec::new(); n_storeys];
    meta.elems.reserve(n_elems.min(2_000_000));
    for e in 0..n_elems {
        let si = (e * n_storeys / n_elems.max(1)).min(n_storeys - 1);
        let bi = si % n_buildings;
        let level = si / n_buildings;
        let class = pick_class(&mut rng);
        let p = [bi as f32 * 300.0 + rng.f32() * 100.0, rng.f32() * 100.0, level as f32 * 3.5 + rng.f32() * 2.0];
        let sz = [0.1 + rng.f32() * 4.0, 0.1 + rng.f32() * 4.0, 0.1 + rng.f32() * 3.0];
        let classification = if rng.below(10) < 7 { 1 + rng.below(2000) as u32 } else { 0 };
        let parts = EntityParts {
            geometry: Some(Geometry { bbox: Aabb::new(p, [p[0] + sz[0], p[1] + sz[1], p[2] + sz[2]]), handle: rng.next_u64() }),
            semantic: Some(Semantic {
                classification,
                class,
                ptype: rng.below(4) as u16,
                phase: if rng.below(10) < 8 { 0 } else { rng.below(4) as u8 },
                option: if rng.below(10) < 9 { 0 } else { 1 + rng.below(2) as u8 },
            }),
            procedural: if rng.below(10) == 0 { Some(Procedural { graph: rng.below(1000) as u32, node: rng.below(100_000) as u32, output: rng.below(4) as u16 }) } else { None },
            presentation: Some(Presentation { material: rng.below(500) as u32, color: rng.next_u64() as u32, layer: rng.below(64) as u16, flags: 0 }),
        };
        let ix = b.add(uuid(&mut rng), parts).unwrap();
        b.set_prop(ix, height, PropVal::F64(rng.f64() * 30.0)).unwrap();
        if rng.below(2) == 0 {
            b.set_prop(ix, cost, PropVal::F64(rng.f64() * 10_000.0)).unwrap();
        }
        if rng.below(10) < 3 {
            b.set_prop(ix, fire, PropVal::I64(30 * rng.below(5) as i64)).unwrap();
        }
        if rng.below(5) == 0 {
            b.set_prop(ix, name, PropVal::Str(names[rng.below(1000) as usize])).unwrap();
        }
        b.link(RelKind::Containment, ix, meta.storeys[si]);
        match class {
            C_WALL => {
                if let Some(prev) = last_wall[si] {
                    b.link(RelKind::Connection, ix, prev);
                }
                last_wall[si] = Some(ix);
                if meta.walls.len() < 200_000 && rng.below(5) == 0 {
                    meta.walls.push(ix);
                }
            }
            C_DOOR | C_WINDOW => {
                if let Some(w) = last_wall[si] {
                    b.link(RelKind::Hosting, ix, w);
                    if class == C_DOOR && meta.doors.len() < 200_000 {
                        meta.doors.push(ix);
                    }
                }
            }
            _ => {}
        }
        let r = &mut recent[si];
        if r.len() < 64 {
            r.push(ix);
        } else {
            let k = rng.below(64) as usize;
            r[k] = ix;
        }
        if rng.below(50) == 0 {
            let t = r[rng.below(r.len() as u64) as usize];
            if t != ix {
                b.link(RelKind::Reference, ix, t);
            }
        }
        if rng.below(100) == 0 {
            if let Some(w) = last_wall[si] {
                if w != ix {
                    b.constraint(w, ix, if rng.below(2) == 0 { CT_FIXED_OFFSET } else { CT_DISTANCE }, rng.f64() * 5.0);
                }
            }
        }
        if meta.elems.len() < 2_000_000 && rng.below(3) == 0 {
            meta.elems.push(ix);
        }
    }
    let s = b.finish().unwrap();
    (s, meta)
}

fn report_memory(label: &str, s: &Scene, n: usize) {
    println!("\n#### Memory by structure ({label})\n");
    println!("| structure | MB | bytes/entity |");
    println!("|---|---|---|");
    let mut tot = 0;
    for (k, v) in s.memory_report() {
        println!("| {} | {:.1} | {:.1} |", k, mb(v), v as f64 / n as f64);
        tot += v;
    }
    println!("| node total (estimate) | {:.1} | {:.1} |", mb(tot), tot as f64 / n as f64);
}

fn bench_queries(s: &Scene, m: &Meta, rng: &mut Rng) {
    println!("\n#### Query latency\n");
    println!("| shape | runs | p50 | p90 | p99 | max | result |");
    println!("|---|---|---|---|---|---|---|");
    let sample: Vec<Uuid> = (0..100_000).map(|_| s.uuid_of(m.elems[rng.below(m.elems.len() as u64) as usize]).unwrap()).collect();

    // Q1 point lookup by UUID with all components
    let mut st = Stats::default();
    let mut chk = 0u64;
    for i in 0..200_000 {
        let id = sample[i % sample.len()];
        let t = Instant::now();
        let v = s.entity(id).unwrap();
        chk = chk.wrapping_add(v.geometry.map(|g| g.handle).unwrap_or(0));
        st.push(t.elapsed().as_nanos() as u64);
    }
    row("Q1 UUID -> all components", &mut st, &format!("1 entity (chk {})", chk & 0xff));

    macro_rules! timed {
        ($name:expr, $runs:expr, $body:expr) => {{
            let mut st = Stats::default();
            let mut sz = 0usize;
            for _ in 0..$runs {
                let t = Instant::now();
                let r = $body;
                st.push(t.elapsed().as_nanos() as u64);
                sz = r.len();
                std::hint::black_box(&r);
            }
            row($name, &mut st, &format!("{} hits", sz));
        }};
    }

    timed!("Q2 class, selective, indexed (0.05%)", 500, s.by_class(C_RARE));
    timed!("Q3 class, broad, indexed (20%)", 20, s.by_class(C_WALL));
    let h = m.height;
    timed!("Q4 property range scan (0.03% of column)", 20, s.prop_range_f64(h, 10.0, 10.01));
    timed!("Q5 class AND property range (6% x 3%)", 20, s.by_class_and_prop_range(C_DOOR, h, 0.0, 1.0));
    let storeys = m.storeys.clone();
    let mut k = 0usize;
    timed!("Q6 children of a storey (containment)", 2000, {
        k += 1;
        s.contents_of(storeys[k % storeys.len()])
    });
    let walls = m.walls.clone();
    let mut k = 0usize;
    timed!("Q7 wall neighbourhood: hosted + connected + constraints", 200_000, {
        k += 7;
        let w = walls[k % walls.len()];
        let mut v = s.hosted_by(w);
        v.extend(s.connected(w));
        v.extend(s.constraints_of(w).into_iter().map(|c| c.0));
        v
    });
    let bld = m.buildings.clone();
    timed!("Q8 all descendants of a building (BFS)", 10, s.descendants(RelKind::Containment, bld[0]));
    let mut k = 0usize;
    let ws = walls.clone();
    timed!("Q9 spatial AABB, 6 m window", 20_000, {
        k += 13;
        let g = s.geometry(ws[k % ws.len()]).unwrap().bbox;
        let q = Aabb::new([g.min[0] - 3.0, g.min[1] - 3.0, g.min[2] - 3.0], [g.min[0] + 3.0, g.min[1] + 3.0, g.min[2] + 3.0]);
        s.query_aabb(&q)
    });
    timed!("Q10 spatial AABB, 30 m window", 2000, {
        k += 13;
        let g = s.geometry(ws[k % ws.len()]).unwrap().bbox;
        let q = Aabb::new([g.min[0] - 15.0, g.min[1] - 15.0, g.min[2] - 15.0], [g.min[0] + 15.0, g.min[1] + 15.0, g.min[2] + 15.0]);
        s.query_aabb(&q)
    });
    timed!("Q11 raycast, 100 m through a storey (all hits, sorted)", 500, {
        k += 13;
        let g = s.geometry(ws[k % ws.len()]).unwrap().bbox;
        s.raycast([g.min[0] - 50.0, g.min[1], g.min[2] + 0.5], [1.0, 0.0001, 0.0])
    });
    let mut k = 0u32;
    timed!("Q12 classification code, indexed (~0.035%)", 2000, {
        k += 1;
        s.by_classification(1 + k % 2000)
    });
    timed!("Q13 phase/option filter (full semantic scan)", 10, s.by_phase_option(1, 1));
}

fn mixed_edit(s: &mut Scene, rng: &mut Rng, m: &Meta) {
    let r = rng.below(100);
    let pick = |rng: &mut Rng| m.elems[rng.below(m.elems.len() as u64) as usize];
    if r < 40 {
        let ix = pick(rng);
        s.translate(ix, [rng.f32() - 0.5, rng.f32() - 0.5, 0.0]);
    } else if r < 55 {
        let ix = pick(rng);
        let _ = s.set_prop(ix, m.height, PropVal::F64(rng.f64() * 30.0));
    } else if r < 70 {
        let ix = pick(rng);
        if let Some(mut sem) = s.semantic(ix).copied() {
            sem.class = 16 + rng.below(33) as u16;
            s.set_semantic(ix, sem);
        }
    } else if r < 80 {
        let ix = pick(rng);
        s.set_presentation(ix, Presentation { material: rng.below(500) as u32, color: rng.next_u64() as u32, layer: 1, flags: 1 });
    } else if r < 90 {
        let (a, b) = (pick(rng), pick(rng));
        let _ = s.link_ix(RelKind::Reference, a, b);
    } else if r < 95 {
        let id = Uuid::from_random_bits(rng.next_u128());
        let _ = s.create(
            id,
            EntityParts {
                geometry: Some(Geometry { bbox: Aabb::new([1.0, 1.0, 1.0], [2.0, 2.0, 2.0]), handle: 1 }),
                semantic: Some(Semantic { class: 20, ..Default::default() }),
                ..Default::default()
            },
        );
    } else {
        let ix = pick(rng);
        if let Some(id) = s.uuid_of(ix) {
            let _ = s.delete(id);
        }
    }
}

fn checksum(s: &Scene, probe: &[Ix]) -> u64 {
    let mut h = 0xcbf29ce484222325u64;
    for &ix in probe {
        let g = s.geometry(ix).map(|g| g.bbox.min[0].to_bits() as u64 ^ g.handle).unwrap_or(7);
        h = (h ^ g).wrapping_mul(0x100000001b3);
        h = (h ^ s.semantic(ix).map(|x| x.class as u64).unwrap_or(9)).wrapping_mul(0x100000001b3);
    }
    h ^ s.len() as u64
}

fn bench_edits(mut scene: Scene, m: &Meta, rng: &mut Rng, n: usize) -> Scene {
    println!("\n#### Edits, snapshots, undo\n");
    // spatial index first, so edits go through the dirty overlay as in an interactive session
    let t = Instant::now();
    scene.refresh_spatial();
    let bvh_build = t.elapsed();
    let bvh_mb = scene.memory_report().last().map(|x| x.1).unwrap_or(0);
    println!("BVH build: {:.2} s, {:.1} MB ({:.1} B/entity)\n", bvh_build.as_secs_f64(), mb(bvh_mb), bvh_mb as f64 / n as f64);

    // snapshot cost
    let mut st = Stats::default();
    let mut dr = Stats::default();
    for _ in 0..20_000 {
        let t = Instant::now();
        let snap = scene.clone();
        st.push(t.elapsed().as_nanos() as u64);
        let t = Instant::now();
        drop(snap);
        dr.push(t.elapsed().as_nanos() as u64);
    }
    println!("| operation | runs | p50 | p90 | p99 | max | note |");
    println!("|---|---|---|---|---|---|---|");
    row("snapshot (Scene::clone)", &mut st, "O(#tables), independent of N");
    row("snapshot drop", &mut dr, "last holder frees nothing");

    let mut h = History::new(scene);

    // E1 single edit with propagation through hosting + constraints, snapshot per edit
    let mut st = Stats::default();
    let mut deps = 0usize;
    let walls = m.walls.clone();
    for i in 0..3000 {
        let w = walls[rng.below(walls.len() as u64) as usize];
        let d = [(i % 5) as f32 * 0.1 + 0.1, 0.05, 0.0];
        let t = Instant::now();
        let c = h.edit(|s| s.move_with_dependents(w, d));
        st.push(t.elapsed().as_nanos() as u64);
        deps += c;
    }
    row("E1 move wall + dependents, snapshot kept per edit", &mut st, &format!("avg {:.2} entities touched", deps as f64 / 3000.0));
    let depth_after_e1 = h.undo_depth();

    // E1b same, no history (in place)
    let mut st = Stats::default();
    {
        let mut cur = h.scene().clone();
        drop(h);
        for i in 0..3000 {
            let w = walls[rng.below(walls.len() as u64) as usize];
            let d = [(i % 5) as f32 * 0.1 + 0.1, 0.05, 0.0];
            let t = Instant::now();
            cur.move_with_dependents(w, d);
            st.push(t.elapsed().as_nanos() as u64);
        }
        h = History::new(cur);
    }
    row("E1b move wall + dependents, in place (no snapshot held)", &mut st, "");
    let _ = depth_after_e1;

    // E2 batch of 10,000 moves in one undoable edit
    let mut st = Stats::default();
    for _ in 0..5 {
        let ids: Vec<Ix> = (0..10_000).map(|_| m.elems[rng.below(m.elems.len() as u64) as usize]).collect();
        let t = Instant::now();
        h.edit(|s| {
            for &ix in &ids {
                s.translate(ix, [0.25, 0.0, 0.0]);
            }
        });
        st.push(t.elapsed().as_nanos() as u64);
    }
    row("E2 move 10,000 objects as one undoable edit", &mut st, "gate: < 100 ms");

    // E3 individual edit kinds (with snapshot)
    let mut stp = Stats::default();
    let mut sts = Stats::default();
    let mut stl = Stats::default();
    let mut stc = Stats::default();
    for _ in 0..2000 {
        let ix = m.elems[rng.below(m.elems.len() as u64) as usize];
        let v = rng.f64() * 30.0;
        let t = Instant::now();
        h.edit(|s| {
            let _ = s.set_prop(ix, m.height, PropVal::F64(v));
        });
        stp.push(t.elapsed().as_nanos() as u64);
        let cl = 16 + rng.below(33) as u16;
        let t = Instant::now();
        h.edit(|s| {
            if let Some(mut x) = s.semantic(ix).copied() {
                x.class = cl;
                s.set_semantic(ix, x);
            }
        });
        sts.push(t.elapsed().as_nanos() as u64);
        let other = m.elems[rng.below(m.elems.len() as u64) as usize];
        let t = Instant::now();
        h.edit(|s| {
            let _ = s.link_ix(RelKind::Reference, ix, other);
        });
        stl.push(t.elapsed().as_nanos() as u64);
        let id = Uuid::from_random_bits(rng.next_u128());
        let t = Instant::now();
        h.edit(|s| {
            let ix = s.create(id, EntityParts { geometry: Some(Geometry { bbox: Aabb::new([0.0; 3], [1.0; 3]), handle: 0 }), semantic: Some(Semantic { class: 20, ..Default::default() }), ..Default::default() }).unwrap();
            let _ = s.link_ix(RelKind::Containment, ix, m.storeys[0]);
        });
        stc.push(t.elapsed().as_nanos() as u64);
    }
    row("E3a set property (snapshot kept)", &mut stp, "");
    row("E3b change IFC class (index update)", &mut sts, "");
    row("E3c add reference edge", &mut stl, "");
    row("E3d create entity + containment edge", &mut stc, "");

    // drop history to isolate the memory experiment
    let mut scene = h.scene().clone();
    drop(h);

    // E4 memory delta of 1,000 mixed edits against a held snapshot
    println!("\n#### Snapshot memory delta\n");
    let probe: Vec<Ix> = (0..2000).map(|_| m.elems[rng.below(m.elems.len() as u64) as usize]).collect();
    let before_live = live();
    let snap0 = scene.clone();
    let cs0 = checksum(&snap0, &probe);
    for _ in 0..1000 {
        mixed_edit(&mut scene, rng, m);
    }
    let delta_held = live() as i64 - before_live as i64;
    assert_eq!(checksum(&snap0, &probe), cs0, "snapshot changed!");
    drop(snap0);
    let delta_released = live() as i64 - before_live as i64;
    println!("- 1,000 mixed random edits against ONE held snapshot: +{:.2} MB retained while the snapshot lives ({:.0} B/edit); {:+.2} MB after the snapshot is dropped (net growth: new entities, index and dirty-overlay entries)", delta_held as f64 / 1048576.0, delta_held as f64 / 1000.0, delta_released as f64 / 1048576.0);

    // E5 undo history: snapshot per edit, 1,000 edits
    let base_live = live();
    let mut h = History::new(scene);
    let mut states = Vec::new();
    for i in 0..1000 {
        if i % 100 == 0 {
            states.push((h.undo_depth(), checksum(h.scene(), &probe)));
        }
        h.edit(|s| mixed_edit(s, rng, m));
    }
    let hist_delta = live() as i64 - base_live as i64;
    println!("- 1,000 undoable edits (one snapshot per edit, all retained): +{:.2} MB total, {:.0} KB/edit (includes the edits' own new data)", hist_delta as f64 / 1048576.0, hist_delta as f64 / 1024.0 / 1000.0);

    // undo / redo latency
    let final_cs = checksum(h.scene(), &probe);
    let mut su = Stats::default();
    let mut sq = Stats::default();
    for _ in 0..1000 {
        let t = Instant::now();
        let ok = h.undo();
        su.push(t.elapsed().as_nanos() as u64);
        assert!(ok);
        let t = Instant::now();
        let _ = std::hint::black_box(h.scene().entity(h.scene().uuid_of(probe[0]).unwrap_or_default()));
        let _ = std::hint::black_box(h.scene().by_class(C_RARE).len());
        sq.push(t.elapsed().as_nanos() as u64);
    }
    // the oldest state must match what we recorded at depth 0
    assert_eq!(checksum(h.scene(), &probe), states[0].1, "undo to depth 0 does not restore the original state");
    let mut sr = Stats::default();
    for _ in 0..1000 {
        let t = Instant::now();
        let ok = h.redo();
        sr.push(t.elapsed().as_nanos() as u64);
        assert!(ok);
    }
    assert_eq!(checksum(h.scene(), &probe), final_cs, "redo to the head does not restore the final state");
    println!();
    println!("| operation | runs | p50 | p90 | p99 | max | note |");
    println!("|---|---|---|---|---|---|---|");
    row("U1 undo (snapshot swap)", &mut su, "gate: < 30 ms; state verified by checksum");
    row("U2 first reads after undo (lookup + rare-class query)", &mut sq, "");
    row("U3 redo (snapshot swap)", &mut sr, "");
    h.scene().check_integrity().expect("integrity after the edit run");
    println!("\nIntegrity check after the full edit/undo/redo run: passed.");

    // spatial after edits: dirty overlay
    let mut scene = h.scene().clone();
    drop(h);
    let t = Instant::now();
    scene.refresh_spatial();
    println!("\nBVH rebuild (off the edit path): {:.2} s", t.elapsed().as_secs_f64());
    println!("\n#### Spatial query with the dirty overlay (edits since the last BVH build)\n");
    println!("| operation | runs | p50 | p90 | p99 | max | note |");
    println!("|---|---|---|---|---|---|---|");
    let mut applied = 0usize;
    for target in [0usize, 100, 1000, 10_000, 50_000] {
        while applied < target {
            let ix = m.elems[rng.below(m.elems.len() as u64) as usize];
            scene.translate(ix, [0.1, 0.0, 0.0]);
            applied += 1;
        }
        let mut sq = Stats::default();
        for i in 0..3000 {
            let w = m.walls[i % m.walls.len()];
            if let Some(g) = scene.geometry(w) {
                let g = g.bbox;
                let q = Aabb::new([g.min[0] - 3.0, g.min[1] - 3.0, g.min[2] - 3.0], [g.min[0] + 3.0, g.min[1] + 3.0, g.min[2] + 3.0]);
                let t = Instant::now();
                let r = scene.query_aabb(&q);
                sq.push(t.elapsed().as_nanos() as u64);
                std::hint::black_box(r);
            }
        }
        row(&format!("Q9 with {} dirty entities", scene.dirty_count()), &mut sq, "");
    }
    scene
}

fn run_suite(n: usize, full_queries: bool, seed: u64) {
    println!("\n### N = {} entities\n", n);
    let cfg = Config::default();
    PEAK.store(live(), Relaxed);
    let base = live();
    let t = Instant::now();
    let (scene, meta) = generate(n, cfg, seed);
    let dt = t.elapsed();
    let steady = live() - base;
    let meta_bytes = meta.walls.capacity() * 4 + meta.doors.capacity() * 4 + meta.elems.capacity() * 4 + meta.storeys.capacity() * 4;
    let scene_bytes = steady - meta_bytes;
    println!("- generate + bulk insert (builder, includes data generation and sorting): {:.2} s ({:.0} ns/entity)", dt.as_secs_f64(), dt.as_nanos() as f64 / n as f64);
    println!("- live heap after load: {:.1} MB = {:.1} B/entity (components, 4 property columns, 2 secondary indexes, {} edges x2 directions)", mb(scene_bytes), scene_bytes as f64 / n as f64, scene.edge_count());
    println!("- peak heap during bulk load: {:.1} MB; VmRSS {:.1} MB, VmHWM {:.1} MB", mb(PEAK.load(Relaxed) - base), proc_kb("VmRSS") as f64 / 1024.0, proc_kb("VmHWM") as f64 / 1024.0);
    report_memory(&format!("{n}"), &scene, n);
    // reference point: a flat deep copy of the same number of bytes (what a snapshot would cost without sharing)
    {
        let buf = vec![1u8; scene_bytes];
        let t = Instant::now();
        let copy = buf.clone();
        let dt = t.elapsed();
        std::hint::black_box(&copy);
        println!("\nReference: memcpy of {:.0} MB (a flat deep-copy snapshot of this scene would cost at least this): {:.1} ms", mb(scene_bytes), dt.as_secs_f64() * 1e3);
    }
    let t = Instant::now();
    scene.check_integrity().expect("integrity after bulk load");
    println!("\nFull integrity check after bulk load: passed ({:.1} s)", t.elapsed().as_secs_f64());

    let mut rng = Rng(seed ^ 0xABCDEF);
    let mut scene = scene;
    if full_queries {
        scene.refresh_spatial();
        bench_queries(&scene, &meta, &mut rng);
    }
    let _scene = bench_edits(scene, &meta, &mut rng, n);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let get = |k: &str, d: usize| args.iter().position(|a| a == k).and_then(|i| args.get(i + 1)).and_then(|v| v.parse().ok()).unwrap_or(d);
    let n = get("--n", 5_000_000);
    let gate = get("--gate-n", 1_000_000);
    println!("# Spike A bench, {} threads available, counting allocator", std::thread::available_parallelism().map(|x| x.get()).unwrap_or(0));
    if !args.iter().any(|a| a == "--skip-gate") {
        run_suite(gate, true, 1);
    }
    if !args.iter().any(|a| a == "--skip-big") {
        run_suite(n, true, 2);
    }
}
