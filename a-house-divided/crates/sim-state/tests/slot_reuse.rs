//! Agent store slot reuse and determinism (SPEC-002 section 5, test 2).

use core_math::{Angle, Fx, Vec2Fx};
use sim_state::{AgentId, AgentInit, AgentStore};

fn init(n: u32) -> AgentInit {
    AgentInit {
        pos: Vec2Fx {
            x: Fx::from_raw(i64::from(n)),
            y: Fx::from_raw(-i64::from(n)),
        },
        unit: n,
        hp: n as u16,
        ..AgentInit::default()
    }
}

fn run_script() -> (Vec<AgentId>, Vec<AgentId>, AgentStore) {
    let mut s = AgentStore::new();
    let mut seen = Vec::new();
    let a = s.spawn(init(1));
    let b = s.spawn(init(2));
    let c = s.spawn(init(3));
    seen.extend([a, b, c]);
    assert!(s.despawn(b));
    assert!(s.despawn(a));
    let d = s.spawn(init(4)); // LIFO: reuses slot of a
    let e = s.spawn(init(5)); // then slot of b
    let f = s.spawn(init(6)); // fresh slot
    seen.extend([d, e, f]);
    assert!(s.despawn(c));
    let g = s.spawn(init(7));
    seen.push(g);
    let alive = s.ids_alive();
    (seen, alive, s)
}

#[test]
fn lifo_reuse_order_is_exact() {
    let (seen, alive, s) = run_script();
    let idx: Vec<u32> = seen.iter().map(|i| i.index).collect();
    assert_eq!(idx, vec![0, 1, 2, 0, 1, 3, 2]);
    let gens: Vec<u32> = seen.iter().map(|i| i.generation).collect();
    assert_eq!(gens, vec![0, 0, 0, 1, 1, 0, 1]);
    let alive_idx: Vec<u32> = alive.iter().map(|i| i.index).collect();
    assert_eq!(alive_idx, vec![0, 1, 2, 3]);
    assert_eq!(s.len(), 4);
    assert_eq!(s.capacity(), 4);
}

#[test]
fn repeated_runs_are_identical() {
    let (s1, a1, w1) = run_script();
    let (s2, a2, w2) = run_script();
    assert_eq!(s1, s2);
    assert_eq!(a1, a2);
    assert_eq!(w1, w2);
}

#[test]
fn stale_ids_are_rejected_without_panic() {
    let mut s = AgentStore::new();
    let a = s.spawn(init(1));
    assert!(s.is_alive(a));
    assert!(s.despawn(a));
    assert!(!s.is_alive(a));
    assert!(!s.despawn(a));
    assert_eq!(s.pos(a), None);
    assert!(!s.set_hp(a, 5));
    let b = s.spawn(init(2));
    assert_eq!(b.index, a.index);
    assert_ne!(b.generation, a.generation);
    assert!(!s.is_alive(a));
    assert!(!s.despawn(a));
    assert_eq!(s.hp(a), None);
    assert_eq!(s.hp(b), Some(2));
    let bogus = AgentId {
        index: 9999,
        generation: 0,
    };
    assert!(!s.is_alive(bogus));
    assert!(!s.despawn(bogus));
    assert_eq!(s.get(bogus), None);
}

#[test]
fn getters_and_setters_round_trip() {
    let mut s = AgentStore::new();
    let id = s.spawn(AgentInit::default());
    let p = Vec2Fx {
        x: Fx::from_raw(11),
        y: Fx::from_raw(-12),
    };
    assert!(s.set_pos(id, p));
    assert!(s.set_vel(id, p));
    assert!(s.set_facing(id, Angle::from_raw(77)));
    assert!(s.set_hp(id, 65535));
    assert!(s.set_flags(id, 0xBEEF));
    assert!(s.set_team(id, 9));
    assert!(s.set_unit(id, 123_456));
    assert_eq!(s.pos(id), Some(p));
    assert_eq!(s.vel(id), Some(p));
    assert_eq!(s.facing(id), Some(Angle::from_raw(77)));
    assert_eq!(s.hp(id), Some(65535));
    assert_eq!(s.flags(id), Some(0xBEEF));
    assert_eq!(s.team(id), Some(9));
    assert_eq!(s.unit(id), Some(123_456));
    let got = s.get(id).unwrap();
    assert_eq!(got.hp, 65535);
    assert_eq!(got.unit, 123_456);
}

#[test]
fn iteration_is_ascending_slot_order() {
    let mut s = AgentStore::new();
    let ids: Vec<_> = (0..10).map(|i| s.spawn(init(i))).collect();
    for i in [7usize, 2, 5] {
        s.despawn(ids[i]);
    }
    let order: Vec<u32> = s.iter_alive().map(|i| i.index).collect();
    assert_eq!(order, vec![0, 1, 3, 4, 6, 8, 9]);
    assert_eq!(s.ids_alive().len(), s.len());
    // The free list is LIFO: last despawned (5) is reused first.
    assert_eq!(s.free_list(), &[7, 2, 5]);
    assert_eq!(s.spawn(init(99)).index, 5);
    assert_eq!(s.spawn(init(98)).index, 2);
    assert_eq!(s.spawn(init(97)).index, 7);
    assert_eq!(s.spawn(init(96)).index, 10);
}

#[test]
fn despawned_slot_data_is_cleared() {
    let mut s = AgentStore::new();
    let a = s.spawn(init(42));
    s.despawn(a);
    let b = s.spawn(AgentInit::default());
    assert_eq!(s.get(b), Some(AgentInit::default()));
}
