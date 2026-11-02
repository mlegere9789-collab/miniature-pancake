//! Hash independence and sensitivity (SPEC-002 section 5, test 5).

use core_math::{Angle, Fx, RngStreams, Vec2Fx};
use proptest::prelude::*;
use sim_state::{state_hash, AgentInit, WorldState};

fn init(n: u32) -> AgentInit {
    AgentInit {
        pos: Vec2Fx {
            x: Fx::from_raw(i64::from(n) * 1000),
            y: Fx::from_raw(i64::from(n) * -77),
        },
        vel: Vec2Fx {
            x: Fx::from_raw(i64::from(n)),
            y: Fx::from_raw(5),
        },
        facing: Angle::from_raw(n * 31),
        hp: n as u16,
        flags: 3,
        team: (n % 4) as u8,
        unit: n,
    }
}

type Mutation = Box<dyn Fn(&mut WorldState)>;

fn world() -> WorldState {
    WorldState::new(RngStreams::new(99))
}

#[test]
fn identical_final_layout_gives_identical_hash() {
    // Sequence 1: spawn a, b, c; despawn b; spawn d (reuses slot 1).
    let mut w1 = world();
    w1.agents.spawn(init(1));
    let b = w1.agents.spawn(init(2));
    w1.agents.spawn(init(3));
    w1.agents.despawn(b);
    w1.agents.spawn(init(4));

    // Sequence 2: interleaved differently, same final layout.
    let mut w2 = world();
    w2.agents.spawn(init(1));
    let b = w2.agents.spawn(init(2));
    w2.agents.despawn(b);
    w2.agents.spawn(init(4));
    w2.agents.spawn(init(3));

    // Layout differs in slot assignment of c and d: slot 2 holds c in both,
    // slot 1 holds d at generation 1 in both.
    assert_eq!(w1.agents, w2.agents);
    assert_eq!(state_hash(&w1), state_hash(&w2));
}

#[test]
fn different_layout_gives_different_hash() {
    // Same alive set, different free-list order.
    let build = |first: usize, second: usize| {
        let mut w = world();
        let ids: Vec<_> = (0..4).map(|i| w.agents.spawn(init(i))).collect();
        w.agents.despawn(ids[first]);
        w.agents.despawn(ids[second]);
        w
    };
    let a = build(0, 1);
    let b = build(1, 0);
    assert_eq!(a.agents.len(), b.agents.len());
    assert_ne!(a.agents.free_list(), b.agents.free_list());
    assert_ne!(state_hash(&a), state_hash(&b));
}

#[test]
fn tick_and_seed_affect_hash() {
    let a = world();
    let mut b = world();
    b.advance_tick();
    let c = WorldState::new(RngStreams::new(100));
    assert_ne!(state_hash(&a), state_hash(&b));
    assert_ne!(state_hash(&a), state_hash(&c));
}

#[test]
fn every_single_field_change_changes_hash() {
    let mut base = world();
    let ids: Vec<_> = (0..5).map(|i| base.agents.spawn(init(i + 1))).collect();
    let h0 = state_hash(&base);

    for &id in &ids {
        let cur = base.agents.get(id).unwrap();
        let mutations: Vec<Mutation> = vec![
            Box::new(move |w| {
                let mut p = cur.pos;
                p.x = Fx::from_raw(p.x.raw() + 1);
                w.agents.set_pos(id, p);
            }),
            Box::new(move |w| {
                let mut p = cur.pos;
                p.y = Fx::from_raw(p.y.raw() - 1);
                w.agents.set_pos(id, p);
            }),
            Box::new(move |w| {
                let mut p = cur.vel;
                p.x = Fx::from_raw(p.x.raw() + 1);
                w.agents.set_vel(id, p);
            }),
            Box::new(move |w| {
                let mut p = cur.vel;
                p.y = Fx::from_raw(p.y.raw() + 1);
                w.agents.set_vel(id, p);
            }),
            Box::new(move |w| {
                w.agents
                    .set_facing(id, Angle::from_raw(cur.facing.raw().wrapping_add(1)));
            }),
            Box::new(move |w| {
                w.agents.set_hp(id, cur.hp.wrapping_add(1));
            }),
            Box::new(move |w| {
                w.agents.set_flags(id, cur.flags ^ 0x8000);
            }),
            Box::new(move |w| {
                w.agents.set_team(id, cur.team.wrapping_add(1));
            }),
            Box::new(move |w| {
                w.agents.set_unit(id, cur.unit ^ 0x8000_0000);
            }),
            Box::new(move |w| {
                w.agents.despawn(id);
            }),
        ];
        for (k, m) in mutations.iter().enumerate() {
            let mut w = base.clone();
            m(&mut w);
            assert_ne!(
                state_hash(&w),
                h0,
                "mutation {k} of agent {id:?} did not change hash"
            );
        }
    }
}

proptest! {
    #[test]
    fn random_single_field_change_changes_hash(
        n in 1usize..12,
        which in 0usize..9,
        target in 0usize..12,
        delta in 1u32..=u32::MAX,
    ) {
        let mut w = world();
        let ids: Vec<_> = (0..n).map(|i| w.agents.spawn(init(i as u32 + 1))).collect();
        let id = ids[target % n];
        let h0 = state_hash(&w);
        let cur = w.agents.get(id).unwrap();
        match which {
            0 => { w.agents.set_pos(id, Vec2Fx { x: Fx::from_raw(cur.pos.x.raw().wrapping_add(i64::from(delta))), y: cur.pos.y }); }
            1 => { w.agents.set_pos(id, Vec2Fx { x: cur.pos.x, y: Fx::from_raw(cur.pos.y.raw().wrapping_add(i64::from(delta))) }); }
            2 => { w.agents.set_vel(id, Vec2Fx { x: Fx::from_raw(cur.vel.x.raw().wrapping_add(i64::from(delta))), y: cur.vel.y }); }
            3 => { w.agents.set_vel(id, Vec2Fx { x: cur.vel.x, y: Fx::from_raw(cur.vel.y.raw().wrapping_add(i64::from(delta))) }); }
            4 => { w.agents.set_facing(id, Angle::from_raw(cur.facing.raw().wrapping_add(delta))); }
            5 => { w.agents.set_hp(id, cur.hp ^ (delta as u16 | 1)); }
            6 => { w.agents.set_flags(id, cur.flags ^ (delta as u16 | 1)); }
            7 => { w.agents.set_team(id, cur.team ^ (delta as u8 | 1)); }
            _ => { w.agents.set_unit(id, cur.unit.wrapping_add(delta)); }
        }
        prop_assert_ne!(state_hash(&w), h0);
    }
}
