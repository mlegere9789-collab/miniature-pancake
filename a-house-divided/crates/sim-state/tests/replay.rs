//! Replay format v1 (SPEC-002 section 5, test 4).

use proptest::prelude::*;
use sim_state::{fnv1a64, Replay, ReplayError, ReplayReader, ReplayWriter};

fn small_replay() -> Replay {
    let mut w = ReplayWriter::new("tutorial-01", 0xDEAD_BEEF_0102_0304, 30, 1, 10);
    w.push_input(5, 2, 1, vec![1, 2, 3]);
    w.push_input(1, 1, 0, vec![]);
    w.push_input(5, 1, 9, vec![9]);
    w.push_input(5, 2, 0, vec![0xFF, 0xEE]);
    w.push_checkpoint(0, 0x1111);
    w.push_checkpoint(10, 0x2222);
    w.finish(20, 0x3333).unwrap()
}

fn arb_replay() -> impl Strategy<Value = Replay> {
    (
        "[a-z0-9_ -]{0,40}",
        any::<u64>(),
        any::<u16>(),
        0u8..=2,
        any::<u32>(),
        prop::collection::vec(
            (
                any::<u64>(),
                any::<u8>(),
                any::<u16>(),
                prop::collection::vec(any::<u8>(), 0..24),
            ),
            0..30,
        ),
        prop::collection::vec((any::<u64>(), any::<u64>()), 0..20),
        any::<u64>(),
        any::<u64>(),
    )
        .prop_map(|(id, seed, hz, fid, ci, inputs, cps, total, fin)| {
            let mut w = ReplayWriter::new(id, seed, hz, fid, ci);
            for (t, a, s, p) in inputs {
                w.push_input(t, a, s, p);
            }
            for (t, h) in cps {
                w.push_checkpoint(t, h);
            }
            w.finish(total, fin).unwrap()
        })
}

/// Hand-assemble a replay body with a correct trailing body hash.
fn assemble(mut body: Vec<u8>) -> Vec<u8> {
    let h = fnv1a64(&body);
    body.extend_from_slice(&h.to_le_bytes());
    body
}

fn header_bytes(version: u16, fidelity: u8) -> Vec<u8> {
    let mut b = b"HDRP".to_vec();
    b.extend_from_slice(&version.to_le_bytes());
    b.extend_from_slice(&2u16.to_le_bytes());
    b.extend_from_slice(b"ab");
    b.extend_from_slice(&7u64.to_le_bytes());
    b.extend_from_slice(&60u16.to_le_bytes());
    b.push(fidelity);
    b.extend_from_slice(&5u32.to_le_bytes());
    b
}

fn footer(b: &mut Vec<u8>) {
    b.extend_from_slice(&100u64.to_le_bytes());
    b.extend_from_slice(&200u64.to_le_bytes());
}

#[test]
fn writer_sorts_inputs_and_layout_is_exact() {
    let r = small_replay();
    let keys: Vec<_> = r
        .inputs()
        .iter()
        .map(|i| (i.tick, i.actor, i.seq))
        .collect();
    assert_eq!(keys, vec![(1, 1, 0), (5, 1, 9), (5, 2, 0), (5, 2, 1)]);

    let bytes = r.to_bytes();
    assert_eq!(&bytes[0..4], b"HDRP");
    assert_eq!(&bytes[4..6], &1u16.to_le_bytes());
    assert_eq!(&bytes[6..8], &11u16.to_le_bytes());
    assert_eq!(&bytes[8..19], b"tutorial-01");
    // seed, tick_hz, fidelity, checkpoint_interval
    assert_eq!(&bytes[19..27], &0xDEAD_BEEF_0102_0304u64.to_le_bytes());
    assert_eq!(&bytes[27..29], &30u16.to_le_bytes());
    assert_eq!(bytes[29], 1);
    assert_eq!(&bytes[30..34], &10u32.to_le_bytes());
    assert_eq!(&bytes[34..38], &4u32.to_le_bytes()); // input count
                                                     // first input: tick 1, actor 1, seq 0, payload len 0
    assert_eq!(&bytes[38..46], &1u64.to_le_bytes());
    assert_eq!(bytes[46], 1);
    assert_eq!(&bytes[47..49], &0u16.to_le_bytes());
    assert_eq!(&bytes[49..53], &0u32.to_le_bytes());
    // footer: body hash is FNV-1a of everything before it
    let n = bytes.len();
    assert_eq!(&bytes[n - 8..], &fnv1a64(&bytes[..n - 8]).to_le_bytes());
    assert_eq!(&bytes[n - 24..n - 16], &20u64.to_le_bytes());
    assert_eq!(&bytes[n - 16..n - 8], &0x3333u64.to_le_bytes());
}

#[test]
fn small_round_trip() {
    let r = small_replay();
    assert_eq!(ReplayReader::parse(&r.to_bytes()).unwrap(), r);
    assert_eq!(r.scenario_id(), "tutorial-01");
    assert_eq!(r.seed(), 0xDEAD_BEEF_0102_0304);
    assert_eq!(r.tick_hz(), 30);
    assert_eq!(r.fidelity_class(), 1);
    assert_eq!(r.checkpoint_interval(), 10);
    assert_eq!(r.checkpoints().len(), 2);
    assert_eq!(r.total_ticks(), 20);
    assert_eq!(r.final_hash(), 0x3333);
}

#[test]
fn exhaustive_single_byte_mutation_and_truncation_rejected() {
    let bytes = small_replay().to_bytes();
    for len in 0..bytes.len() {
        assert!(
            ReplayReader::parse(&bytes[..len]).is_err(),
            "truncation to {len} accepted"
        );
    }
    for i in 0..bytes.len() {
        for x in 1..=255u8 {
            let mut m = bytes.clone();
            m[i] ^= x;
            assert!(
                ReplayReader::parse(&m).is_err(),
                "mutation at {i} xor {x:#x} accepted"
            );
        }
    }
}

#[test]
fn trailing_bytes_rejected() {
    let mut bytes = small_replay().to_bytes();
    bytes.push(0);
    assert!(ReplayReader::parse(&bytes).is_err());
}

#[test]
fn bad_magic_and_version() {
    let mut b = header_bytes(1, 0);
    b[0] = b'X';
    assert_eq!(ReplayReader::parse(&b), Err(ReplayError::BadMagic));
    let mut b = header_bytes(2, 0);
    b.extend_from_slice(&0u32.to_le_bytes());
    b.extend_from_slice(&0u32.to_le_bytes());
    footer(&mut b);
    assert_eq!(
        ReplayReader::parse(&assemble(b)),
        Err(ReplayError::UnsupportedVersion(2))
    );
    assert_eq!(ReplayReader::parse(b""), Err(ReplayError::Truncated));
}

#[test]
fn handmade_minimal_replay_parses() {
    let mut b = header_bytes(1, 2);
    b.extend_from_slice(&0u32.to_le_bytes());
    b.extend_from_slice(&0u32.to_le_bytes());
    footer(&mut b);
    let r = ReplayReader::parse(&assemble(b)).unwrap();
    assert_eq!(r.scenario_id(), "ab");
    assert_eq!(r.fidelity_class(), 2);
    assert_eq!(r.total_ticks(), 100);
    assert_eq!(r.final_hash(), 200);
}

#[test]
fn invalid_fidelity_rejected() {
    let mut b = header_bytes(1, 3);
    b.extend_from_slice(&0u32.to_le_bytes());
    b.extend_from_slice(&0u32.to_le_bytes());
    footer(&mut b);
    assert_eq!(
        ReplayReader::parse(&assemble(b)),
        Err(ReplayError::InvalidFidelityClass(3))
    );
}

#[test]
fn unsorted_inputs_rejected() {
    let mut b = header_bytes(1, 0);
    b.extend_from_slice(&2u32.to_le_bytes());
    for tick in [9u64, 3u64] {
        b.extend_from_slice(&tick.to_le_bytes());
        b.push(0);
        b.extend_from_slice(&0u16.to_le_bytes());
        b.extend_from_slice(&0u32.to_le_bytes());
    }
    b.extend_from_slice(&0u32.to_le_bytes());
    footer(&mut b);
    assert!(matches!(
        ReplayReader::parse(&assemble(b)),
        Err(ReplayError::InputsNotSorted { .. })
    ));
}

#[test]
fn allocation_bombs_rejected() {
    // Huge input count.
    let mut b = header_bytes(1, 0);
    b.extend_from_slice(&u32::MAX.to_le_bytes());
    footer(&mut b);
    assert!(matches!(
        ReplayReader::parse(&assemble(b)),
        Err(ReplayError::LengthExceedsData { .. })
    ));
    // Huge payload length.
    let mut b = header_bytes(1, 0);
    b.extend_from_slice(&1u32.to_le_bytes());
    b.extend_from_slice(&0u64.to_le_bytes());
    b.push(0);
    b.extend_from_slice(&0u16.to_le_bytes());
    b.extend_from_slice(&u32::MAX.to_le_bytes());
    footer(&mut b);
    assert!(matches!(
        ReplayReader::parse(&assemble(b)),
        Err(ReplayError::LengthExceedsData { .. })
    ));
    // Huge checkpoint count.
    let mut b = header_bytes(1, 0);
    b.extend_from_slice(&0u32.to_le_bytes());
    b.extend_from_slice(&u32::MAX.to_le_bytes());
    footer(&mut b);
    assert!(matches!(
        ReplayReader::parse(&assemble(b)),
        Err(ReplayError::LengthExceedsData { .. })
    ));
    // Scenario id length beyond data.
    let mut b = b"HDRP".to_vec();
    b.extend_from_slice(&1u16.to_le_bytes());
    b.extend_from_slice(&u16::MAX.to_le_bytes());
    b.extend_from_slice(b"ab");
    assert!(matches!(
        ReplayReader::parse(&b),
        Err(ReplayError::LengthExceedsData { .. })
    ));
}

#[test]
fn invalid_utf8_rejected() {
    let mut b = b"HDRP".to_vec();
    b.extend_from_slice(&1u16.to_le_bytes());
    b.extend_from_slice(&2u16.to_le_bytes());
    b.extend_from_slice(&[0xFF, 0xFE]);
    b.extend_from_slice(&7u64.to_le_bytes());
    b.extend_from_slice(&60u16.to_le_bytes());
    b.push(0);
    b.extend_from_slice(&5u32.to_le_bytes());
    b.extend_from_slice(&0u32.to_le_bytes());
    b.extend_from_slice(&0u32.to_le_bytes());
    footer(&mut b);
    assert_eq!(
        ReplayReader::parse(&assemble(b)),
        Err(ReplayError::InvalidUtf8)
    );
}

#[test]
fn body_hash_mismatch_reported() {
    let mut bytes = small_replay().to_bytes();
    let n = bytes.len();
    bytes[n - 1] ^= 1;
    assert!(matches!(
        ReplayReader::parse(&bytes),
        Err(ReplayError::BodyHashMismatch { .. })
    ));
}

#[test]
fn writer_rejects_invalid_fields() {
    let w = ReplayWriter::new("x", 1, 30, 3, 1);
    assert_eq!(w.finish(0, 0), Err(ReplayError::InvalidFidelityClass(3)));
    let w = ReplayWriter::new("x".repeat(70_000), 1, 30, 0, 1);
    assert!(matches!(
        w.finish(0, 0),
        Err(ReplayError::ScenarioIdTooLong(70_000))
    ));
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(128))]

    #[test]
    fn round_trip(r in arb_replay()) {
        let bytes = r.to_bytes();
        let back = ReplayReader::parse(&bytes).unwrap();
        prop_assert_eq!(&back, &r);
        prop_assert_eq!(back.to_bytes(), bytes);
    }

    #[test]
    fn sampled_mutation_and_truncation_rejected(
        r in arb_replay(),
        pos in any::<prop::sample::Index>(),
        x in 1u8..=255,
        cut in any::<prop::sample::Index>(),
    ) {
        let bytes = r.to_bytes();
        let mut m = bytes.clone();
        let i = pos.index(m.len());
        m[i] ^= x;
        prop_assert!(ReplayReader::parse(&m).is_err());
        let len = cut.index(bytes.len());
        prop_assert!(ReplayReader::parse(&bytes[..len]).is_err());
    }

    #[test]
    fn arbitrary_bytes_never_panic(data in prop::collection::vec(any::<u8>(), 0..256)) {
        let _ = ReplayReader::parse(&data);
    }

    #[test]
    fn arbitrary_bytes_with_valid_prefix_never_panic(
        data in prop::collection::vec(any::<u8>(), 0..256),
    ) {
        let mut b = b"HDRP".to_vec();
        b.extend_from_slice(&1u16.to_le_bytes());
        b.extend_from_slice(&data);
        let _ = ReplayReader::parse(&b);
        let _ = ReplayReader::parse(&assemble(b));
    }
}
