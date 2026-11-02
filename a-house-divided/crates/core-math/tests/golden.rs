//! Golden vectors for SplitMix64 and xoshiro256**.
//!
//! The literals were produced by an independent reference implementation of the two published
//! public-domain algorithms (not by this crate), and the SplitMix64 seed-0 prefix matches the
//! widely published values (0xE220A8397B1DCDAF, 0x6E789E6AA1B965F4, ...).
//! xoshiro256** is seeded by taking four consecutive SplitMix64 outputs as the state.

use core_math::{Rng, SplitMix64};

pub const SPLITMIX_SEED_0: [u64; 16] = [
    0xE220A8397B1DCDAF,
    0x6E789E6AA1B965F4,
    0x06C45D188009454F,
    0xF88BB8A8724C81EC,
    0x1B39896A51A8749B,
    0x53CB9F0C747EA2EA,
    0x2C829ABE1F4532E1,
    0xC584133AC916AB3C,
    0x3EE5789041C98AC3,
    0xF3B8488C368CB0A6,
    0x657EECDD3CB13D09,
    0xC2D326E0055BDEF6,
    0x8621A03FE0BBDB7B,
    0x8E1F7555983AA92F,
    0xB54E0F1600CC4D19,
    0x84BB3F97971D80AB,
];
pub const XOSHIRO_SEED_0: [u64; 16] = [
    0x99EC5F36CB75F2B4,
    0xBF6E1F784956452A,
    0x1A5F849D4933E6E0,
    0x6AA594F1262D2D2C,
    0xBBA5AD4A1F842E59,
    0xFFEF8375D9EBCACA,
    0x6C160DEED2F54C98,
    0x8920AD648FC30A3F,
    0xDB032C0BA7539731,
    0xEB3A475A3E749A3D,
    0x1D42993FA43F2A54,
    0x11361BF526A14BB5,
    0x1B4F07A5AB3D8E9C,
    0xA7A3257F6986DB7F,
    0x7EFDAA95605DFC9C,
    0x4BDE97C0A78EAAB8,
];
pub const SPLITMIX_SEED_1: [u64; 16] = [
    0x910A2DEC89025CC1,
    0xBEEB8DA1658EEC67,
    0xF893A2EEFB32555E,
    0x71C18690EE42C90B,
    0x71BB54D8D101B5B9,
    0xC34D0BFF90150280,
    0xE099EC6CD7363CA5,
    0x85E7BB0F12278575,
    0x491718DE357E3DA8,
    0xCB435C8E74616796,
    0x6775DC7701564F61,
    0x9AFCD44D14CF8BFE,
    0x7476CF8A4BAA5DC0,
    0x87B341D690D7A28A,
    0x6F9B6DAE6F4C57A8,
    0x2AC2CE17A5794A3B,
];
pub const XOSHIRO_SEED_1: [u64; 16] = [
    0xB3F2AF6D0FC710C5,
    0x853B559647364CEA,
    0x92F89756082A4514,
    0x642E1C7BC266A3A7,
    0xB27A48E29A233673,
    0x24C123126FFDA722,
    0x123004EF8DF510E6,
    0x61954DCC47B1E89D,
    0xDDFDB48AB9ED4A21,
    0x8D3CDB8C3AA5B1D0,
    0xEEBD114BD87226D1,
    0xF50C3FF1E7D7E8A6,
    0xEECA3115E23BC8F1,
    0xAB49ED3DB4C66435,
    0x99953C6C57808DD7,
    0xE3FA941B05219325,
];
pub const SPLITMIX_SEED_DEADBEEF: [u64; 16] = [
    0x4ADFB90F68C9EB9B,
    0xDE586A3141A10922,
    0x021FBC2F8E1CFC1D,
    0x7466CE737BE16790,
    0x3BFA8764F685BD1C,
    0xAB203E503CB55B3F,
    0x5A2FDC2BF68CEDB3,
    0xB30A4CCF430B1B5A,
    0x0A90415039BD5985,
    0x26AE50847745EB7E,
    0xE239ED306D9B1929,
    0xFB7D9A8D444D41BC,
    0x1BB52E523960D559,
    0xCF8631B40292B5D5,
    0xF6186C41B838B122,
    0x432497FFB78C1173,
];
pub const XOSHIRO_SEED_DEADBEEF: [u64; 16] = [
    0xC5555444A74D7E83,
    0x65C30D37B4B16E38,
    0x54F773200A4EFA23,
    0x429AED75FB958AF7,
    0xFB0E1DD69C255B2E,
    0x9D6D02EC58814A27,
    0xF4199B9DA2E4B2A3,
    0x54BC5B2C11A4540A,
    0xE85B77DF60AFCA9B,
    0xA8B8BA7EA74319BE,
    0x63450B50B59306C6,
    0x7200F11C574C1433,
    0xAFF625604F16B53B,
    0x0341C563213FE478,
    0xA4B9B9415211D8D4,
    0x80F7CFC260A86FA9,
];

fn check_splitmix(seed: u64, expected: &[u64; 16]) {
    let mut sm = SplitMix64::new(seed);
    for (i, e) in expected.iter().enumerate() {
        assert_eq!(sm.next_u64(), *e, "splitmix seed {seed:#x} output {i}");
    }
}

fn check_xoshiro(seed: u64, expected: &[u64; 16]) {
    let mut rng = Rng::from_seed(seed);
    for (i, e) in expected.iter().enumerate() {
        assert_eq!(rng.next_u64(), *e, "xoshiro seed {seed:#x} output {i}");
    }
}

#[test]
fn splitmix64_golden() {
    check_splitmix(0, &SPLITMIX_SEED_0);
    check_splitmix(1, &SPLITMIX_SEED_1);
    check_splitmix(0xDEAD_BEEF, &SPLITMIX_SEED_DEADBEEF);
}

#[test]
fn xoshiro256ss_golden() {
    check_xoshiro(0, &XOSHIRO_SEED_0);
    check_xoshiro(1, &XOSHIRO_SEED_1);
    check_xoshiro(0xDEAD_BEEF, &XOSHIRO_SEED_DEADBEEF);
}

#[test]
fn xoshiro_state_is_four_splitmix_outputs() {
    let rng = Rng::from_seed(0);
    let st = rng.state();
    assert_eq!(
        st,
        [
            SPLITMIX_SEED_0[0],
            SPLITMIX_SEED_0[1],
            SPLITMIX_SEED_0[2],
            SPLITMIX_SEED_0[3]
        ]
    );
}
