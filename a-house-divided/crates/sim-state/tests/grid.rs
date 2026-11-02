//! Spatial grid vs brute force (SPEC-002 section 5, test 3).

use core_math::{Fx, Vec2Fx};
use proptest::prelude::*;
use sim_state::{AgentId, AgentInit, AgentStore, SpatialGrid};

const CELL: i64 = 4 << 32;

fn v(x: i64, y: i64) -> Vec2Fx {
    Vec2Fx {
        x: Fx::from_raw(x),
        y: Fx::from_raw(y),
    }
}

fn brute(store: &AgentStore, center: Vec2Fx, radius: Fx) -> Vec<AgentId> {
    if radius.raw() < 0 {
        return Vec::new();
    }
    let r = i128::from(radius.raw());
    let mut hits: Vec<(i128, u32, AgentId)> = Vec::new();
    for id in store.iter_alive() {
        let p = store.pos(id).unwrap();
        let dx = i128::from(p.x.raw()) - i128::from(center.x.raw());
        let dy = i128::from(p.y.raw()) - i128::from(center.y.raw());
        let d = dx * dx + dy * dy;
        if d <= r * r {
            hits.push((d, id.index, id));
        }
    }
    hits.sort_by_key(|h| (h.0, h.1));
    hits.into_iter().map(|h| h.2).collect()
}

fn new_grid() -> SpatialGrid {
    // 8 x 6 cells of size 4.0, origin (-10, -10).
    SpatialGrid::new(Fx::from_raw(CELL), 8, 6, v(-10 << 32, -10 << 32))
}

/// Coordinates that frequently land exactly on, or next to, cell borders and
/// outside the grid.
fn coord() -> impl Strategy<Value = i64> {
    prop_oneof![
        // exact border +- 1 raw unit
        ((-4i64..=12), (-1i64..=1)).prop_map(|(k, d)| (-10 << 32) + k * CELL + d),
        // anywhere roughly around and well beyond the grid
        (-30i64 << 32)..(40i64 << 32),
        // tiny values
        -3i64..=3,
    ]
}

fn pos_strategy() -> impl Strategy<Value = Vec2Fx> {
    (coord(), coord()).prop_map(|(x, y)| v(x, y))
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(200))]

    #[test]
    fn query_matches_brute_force(
        positions in prop::collection::vec(pos_strategy(), 0..120),
        despawn_mask in prop::collection::vec(any::<bool>(), 0..120),
        centers in prop::collection::vec((pos_strategy(), 0i64..(14 << 32)), 1..8),
    ) {
        let mut store = AgentStore::new();
        let ids: Vec<_> = positions
            .iter()
            .map(|&p| store.spawn(AgentInit { pos: p, ..AgentInit::default() }))
            .collect();
        for (id, kill) in ids.iter().zip(despawn_mask.iter()) {
            if *kill {
                store.despawn(*id);
            }
        }
        let mut grid = new_grid();
        grid.rebuild(&store);
        let mut out = Vec::new();
        for (c, r) in centers {
            let radius = Fx::from_raw(r);
            grid.query_radius(c, radius, &mut out);
            let expect = brute(&store, c, radius);
            prop_assert_eq!(&out, &expect);
            prop_assert_eq!(grid.nearest(c, radius), expect.first().copied());
        }
    }

    #[test]
    fn rebuild_is_independent_of_history(
        positions in prop::collection::vec(pos_strategy(), 1..60),
        c in pos_strategy(),
        r in 0i64..(20 << 32),
    ) {
        // Store A: spawn all. Store B: spawn extra agents, despawn them, so
        // slots are recycled; but the *alive* agents land in the same slots.
        let mut a = AgentStore::new();
        for &p in &positions {
            a.spawn(AgentInit { pos: p, ..AgentInit::default() });
        }
        let mut b = AgentStore::new();
        for &p in &positions {
            b.spawn(AgentInit { pos: p, ..AgentInit::default() });
        }
        let mut ga = new_grid();
        let mut gb = new_grid();
        ga.rebuild(&a);
        // Rebuilding twice (with a different store in between) changes nothing.
        gb.rebuild(&AgentStore::new());
        gb.rebuild(&b);
        gb.rebuild(&b);
        let (mut oa, mut ob) = (Vec::new(), Vec::new());
        ga.query_radius(c, Fx::from_raw(r), &mut oa);
        gb.query_radius(c, Fx::from_raw(r), &mut ob);
        prop_assert_eq!(oa, ob);
    }
}

#[test]
fn border_and_outside_agents_exact() {
    let mut store = AgentStore::new();
    let on_border = store.spawn(AgentInit {
        pos: v(-10 << 32, -10 << 32),
        ..AgentInit::default()
    });
    let far_outside = store.spawn(AgentInit {
        pos: v(1000 << 32, -1000 << 32),
        ..AgentInit::default()
    });
    let far_neg = store.spawn(AgentInit {
        pos: v(i64::MIN, i64::MAX),
        ..AgentInit::default()
    });
    let mut grid = new_grid();
    grid.rebuild(&store);
    let mut out = Vec::new();
    grid.query_radius(v(1000 << 32, -1000 << 32), Fx::from_raw(1), &mut out);
    assert_eq!(out, vec![far_outside]);
    grid.query_radius(v(-10 << 32, -10 << 32), Fx::ZERO, &mut out);
    assert_eq!(out, vec![on_border]);
    grid.query_radius(v(i64::MIN, i64::MAX), Fx::ZERO, &mut out);
    assert_eq!(out, vec![far_neg]);
    // Extreme radius, sorted by (distance_sq, index). The (MIN, MAX) corner is
    // sqrt(2) * 2^63 away from the origin, so it is correctly excluded.
    grid.query_radius(v(0, 0), Fx::from_raw(i64::MAX), &mut out);
    assert_eq!(out, vec![on_border, far_outside]);
    grid.query_radius(v(i64::MIN + 5, i64::MAX - 5), Fx::from_raw(100), &mut out);
    assert_eq!(out, vec![far_neg]);
}

#[test]
fn ties_break_by_index() {
    let mut store = AgentStore::new();
    let center = v(0, 0);
    let a = store.spawn(AgentInit {
        pos: v(3 << 32, 0),
        ..AgentInit::default()
    });
    let b = store.spawn(AgentInit {
        pos: v(-3 << 32, 0),
        ..AgentInit::default()
    });
    let c = store.spawn(AgentInit {
        pos: v(0, 3 << 32),
        ..AgentInit::default()
    });
    let d = store.spawn(AgentInit {
        pos: v(1 << 32, 0),
        ..AgentInit::default()
    });
    let mut grid = new_grid();
    grid.rebuild(&store);
    let mut out = Vec::new();
    grid.query_radius(center, Fx::from_raw(3 << 32), &mut out);
    assert_eq!(out, vec![d, a, b, c]);
    assert_eq!(grid.nearest(center, Fx::from_raw(3 << 32)), Some(d));
    assert_eq!(grid.nearest(center, Fx::from_raw(1 << 31)), None);
    store.despawn(d);
    grid.rebuild(&store);
    assert_eq!(grid.nearest(center, Fx::from_raw(3 << 32)), Some(a));
}

#[test]
fn negative_radius_is_empty() {
    let mut store = AgentStore::new();
    store.spawn(AgentInit::default());
    let mut grid = new_grid();
    grid.rebuild(&store);
    let mut out = vec![store.ids_alive()[0]];
    grid.query_radius(v(0, 0), Fx::from_raw(-1), &mut out);
    assert!(out.is_empty());
    assert_eq!(grid.nearest(v(0, 0), Fx::from_raw(-1)), None);
}

#[test]
fn cells_touched_is_bounded_by_radius() {
    let grid = {
        let mut g = SpatialGrid::new(Fx::from_raw(CELL), 100, 100, v(0, 0));
        g.rebuild(&AgentStore::new());
        g
    };
    let mut out = Vec::new();
    grid.reset_cells_touched();
    assert_eq!(grid.cells_touched(), 0);
    grid.query_radius(v(50 << 32, 50 << 32), Fx::from_raw(1 << 31), &mut out);
    // Radius smaller than a cell: at most a 2x2 block.
    assert!(grid.cells_touched() >= 1 && grid.cells_touched() <= 4);
    grid.reset_cells_touched();
    grid.query_radius(v(50 << 32, 50 << 32), Fx::from_raw(CELL), &mut out);
    assert!(grid.cells_touched() <= 9 + 16);
    assert!(grid.cells_touched() < 100 * 100);
}

#[test]
fn twenty_thousand_agents() {
    let mut store = AgentStore::new();
    for i in 0..20_000i64 {
        let x = (i * 7919) % 400;
        let y = (i * 104_729) % 400;
        store.spawn(AgentInit {
            pos: v(x << 32, y << 32),
            ..AgentInit::default()
        });
    }
    let mut grid = SpatialGrid::new(Fx::from_raw(8 << 32), 50, 50, v(0, 0));
    grid.rebuild(&store);
    let mut out = Vec::new();
    for c in [v(0, 0), v(200 << 32, 200 << 32), v(399 << 32, 5 << 32)] {
        let r = Fx::from_raw(10 << 32);
        grid.query_radius(c, r, &mut out);
        assert_eq!(out, brute(&store, c, r));
    }
}

#[test]
#[ignore = "micro benchmark; run with --ignored --nocapture"]
fn bench_rebuild_and_query() {
    let mut store = AgentStore::new();
    for i in 0..20_000i64 {
        let x = (i * 7919) % 1000;
        let y = (i * 104_729) % 1000;
        store.spawn(AgentInit {
            pos: v(x << 32, y << 32),
            ..AgentInit::default()
        });
    }
    let mut grid = SpatialGrid::new(Fx::from_raw(16 << 32), 64, 64, v(0, 0));
    let t = std::time::Instant::now();
    for _ in 0..100 {
        grid.rebuild(&store);
    }
    println!("rebuild 20k agents: {:?} per rebuild", t.elapsed() / 100);
    let mut out = Vec::new();
    let t = std::time::Instant::now();
    let mut total = 0usize;
    for i in 0..20_000i64 {
        grid.query_radius(
            v((i % 1000) << 32, ((i * 3) % 1000) << 32),
            Fx::from_raw(16 << 32),
            &mut out,
        );
        total += out.len();
    }
    println!("20k queries: {:?} total ({total} hits)", t.elapsed());
}
