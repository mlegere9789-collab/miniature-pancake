//! Golden hash and canonical layout tests (SPEC-002 section 5, test 1).

use core_math::{Angle, Fx, RngStreams, Vec2Fx};
use sim_state::{
    canonical_bytes, fnv1a64, state_hash, AgentInit, StateHasher, WorldState, FNV_OFFSET_BASIS,
};

fn v(x: i64, y: i64) -> Vec2Fx {
    Vec2Fx {
        x: Fx::from_raw(x),
        y: Fx::from_raw(y),
    }
}

fn init(i: i64) -> AgentInit {
    AgentInit {
        pos: v(i * 0x1_0000_0001, -i * 0x7fff_ffff),
        vel: v(-i * 3, i * 5 + 1),
        facing: Angle::from_raw((i as u32).wrapping_mul(0x9e37_79b9)),
        hp: (100 + i) as u16,
        flags: (1u16 << (i % 16)) | 0x8000,
        team: (i % 3) as u8,
        unit: 1000 + i as u32,
    }
}

/// The fixed world used by the golden test.
fn golden_world() -> WorldState {
    let mut w = WorldState::new(RngStreams::new(0xA11C_E5EE_D000_0001));
    let ids: Vec<_> = (0..6).map(|i| w.agents.spawn(init(i))).collect();
    assert!(w.agents.despawn(ids[1]));
    assert!(w.agents.despawn(ids[4]));
    w.agents.spawn(init(10)); // reuses slot 4 (LIFO)
    for _ in 0..7 {
        w.advance_tick();
    }
    w
}

// Recorded by running the implementation once after it was verified correct
// against the layout documented in the `world` module; it was also reproduced
// independently by a separate script that assembled the documented byte layout
// by hand and applied FNV-1a 64. Do not change it to make a test pass.
const GOLDEN_HASH: u64 = 11_502_733_545_658_071_244;

#[test]
fn golden_hash_matches_checked_in_literal() {
    let w = golden_world();
    assert_eq!(state_hash(&w), GOLDEN_HASH);
}

#[test]
fn canonical_length_matches_documented_layout() {
    let w = golden_world();
    let slots = w.agents.slot_count();
    let free = w.agents.free_list().len();
    let expected = 8 + 8 + 4 + slots * 50 + 4 + free * 4;
    assert_eq!(canonical_bytes(&w).len(), expected);
}

#[test]
fn canonical_prefix_is_tick_seed_slotcount() {
    let w = golden_world();
    let b = canonical_bytes(&w);
    assert_eq!(&b[0..8], &7u64.to_le_bytes());
    assert_eq!(&b[8..16], &0xA11C_E5EE_D000_0001u64.to_le_bytes());
    assert_eq!(&b[16..20], &6u32.to_le_bytes());
}

#[test]
fn fnv_known_vectors() {
    assert_eq!(fnv1a64(b""), 0xcbf2_9ce4_8422_2325);
    assert_eq!(fnv1a64(b"a"), 0xaf63_dc4c_8601_ec8c);
    assert_eq!(fnv1a64(b"foobar"), 0x8594_4171_f739_67e8);
    assert_eq!(FNV_OFFSET_BASIS, 0xcbf2_9ce4_8422_2325);
}

#[test]
fn hash_is_pure_function_of_state() {
    assert_eq!(state_hash(&golden_world()), state_hash(&golden_world()));
}

#[test]
fn incremental_matches_one_shot() {
    let w = golden_world();
    let one = state_hash(&w);

    let mut h = StateHasher::new();
    h.write_world(&w);
    assert_eq!(h.finish(), one);

    let mut h = StateHasher::new();
    h.write_header(w.tick, w.rng.master_seed(), w.agents.slot_count());
    for s in 0..w.agents.slot_count() {
        h.write_slot(&w.agents, s);
    }
    h.write_free_list(&w.agents);
    assert_eq!(h.finish(), one);

    // Feeding the canonical bytes in arbitrary chunks gives the same answer.
    let bytes = canonical_bytes(&w);
    let mut h = StateHasher::new();
    for chunk in bytes.chunks(7) {
        h.write_bytes(chunk);
    }
    assert_eq!(h.finish(), one);
    assert_eq!(fnv1a64(&bytes), one);
}
