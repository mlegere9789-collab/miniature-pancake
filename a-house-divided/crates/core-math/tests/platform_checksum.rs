//! Platform checksum: 1,000,000 mixed `Fx` / `Vec2Fx` / `Angle` / `Rng` operations folded into one
//! `u64`. Cross-platform CI compares this literal; it must be identical in debug and release.

use core_math::{Angle, Fx, Rng, RngStreams, StreamId, Vec2Fx};

/// Expected value, recorded by running this implementation ONCE (debug and release agreed) after it
/// passed every other test; it was not derived independently.
const EXPECTED_CHECKSUM: u64 = 0x574a_adb0_58c7_0417;

fn fold(h: u64, v: u64) -> u64 {
    (h.rotate_left(27) ^ v)
        .wrapping_mul(0x9E37_79B9_7F4A_7C15)
        .wrapping_add(0xD1B5_4A32_D192_ED03)
}

fn fx(h: u64, v: Fx) -> u64 {
    fold(h, v.raw() as u64)
}

fn vec(h: u64, v: Vec2Fx) -> u64 {
    fx(fx(h, v.x), v.y)
}

/// Draws an operand with a randomly chosen magnitude so every scale and saturation path is hit.
fn operand(rng: &mut Rng) -> Fx {
    let raw = rng.next_u64() as i64;
    let shift = rng.range_u32(0, 64);
    match rng.range_u32(0, 8) {
        0 => Fx::ZERO,
        1 => Fx::from_raw(raw.signum() * ((raw >> shift) & 0xFF)),
        2 => Fx::MAX,
        3 => Fx::MIN,
        _ => Fx::from_raw(raw >> shift),
    }
}

fn run() -> u64 {
    let mut rng = RngStreams::new(0xA_B0DE_C0DE).stream(StreamId::new(777));
    let mut h = 0xCBF2_9CE4_8422_2325u64;
    for _ in 0..1_000_000u32 {
        let a = operand(&mut rng);
        let b = operand(&mut rng);
        let ang = Angle::from_raw(rng.next_u32());
        let va = Vec2Fx::new(a, b);
        let vb = Vec2Fx::new(b, operand(&mut rng));
        match rng.range_u32(0, 30) {
            0 => h = fx(h, a + b),
            1 => h = fx(h, a - b),
            2 => h = fx(h, -a),
            3 => h = fx(h, a * b),
            4 => h = fx(h, a / b),
            5 => h = fx(h, a.abs()),
            6 => h = fx(h, a.sqrt()),
            7 => h = fx(h, a.min(b).max(Fx::MIN)),
            8 => h = fx(h, a.clamp(b.min(Fx::ZERO), b.abs())),
            9 => h = fx(h, a.signum()),
            10 => h = fold(h, a.to_int_floor() as u64),
            11 => h = fold(h, a.to_int_round_half_up() as u64),
            12 => h = fx(h, ang.sin()),
            13 => h = fx(h, ang.cos()),
            14 => h = fold(h, Angle::atan2(a, b).raw().into()),
            15 => h = vec(h, va.scale(b)),
            16 => h = fx(h, va.dot(vb)),
            17 => h = fx(h, va.cross(vb)),
            18 => h = fx(h, va.length_sq()),
            19 => h = fx(h, va.length()),
            20 => h = fx(h, va.distance_sq(vb)),
            21 => h = vec(h, va.normalize()),
            22 => h = vec(h, va.rotate(ang)),
            23 => h = vec(h, Vec2Fx::from_angle(ang)),
            24 => h = vec(h, va.lerp(vb, operand(&mut rng))),
            25 => h = vec(h, va + vb),
            26 => h = vec(h, va - vb),
            27 => h = vec(h, -va),
            28 => {
                h = fold(
                    h,
                    (ang + Angle::from_raw(a.raw() as u32))
                        .to_fx_radians()
                        .raw() as u64,
                )
            }
            _ => h = fx(h, rng.unit_fx()),
        }
    }
    let mut items: Vec<u32> = (0..64).collect();
    rng.shuffle(&mut items);
    for i in items {
        h = fold(h, u64::from(i));
    }
    for w in rng.state() {
        h = fold(h, w);
    }
    h
}

#[test]
fn platform_checksum_matches_literal() {
    assert_eq!(
        run(),
        EXPECTED_CHECKSUM,
        "platform checksum changed: {:#018x}",
        run()
    );
}

#[test]
fn platform_checksum_is_repeatable() {
    assert_eq!(run(), run());
}
