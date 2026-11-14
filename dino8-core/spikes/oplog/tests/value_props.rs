//! Property tests for the two canonical encodings of the typed value tree.

use oplog::ids::{Hash, ObjectId};
use oplog::value::Value;
use proptest::prelude::*;
use std::collections::BTreeMap;

fn leaf() -> impl Strategy<Value = Value> {
    prop_oneof![
        Just(Value::Null),
        any::<bool>().prop_map(Value::Bool),
        any::<i64>().prop_map(Value::Int),
        prop_oneof![
            any::<f64>(),
            Just(f64::NAN),
            Just(f64::INFINITY),
            Just(f64::NEG_INFINITY),
            Just(-0.0),
            Just(0.0),
            Just(f64::MIN_POSITIVE),
            Just(5e-324),
            Just(1e16),
            Just(9.999e15),
            Just(1e-5),
            Just(0.1 + 0.2),
        ]
        .prop_map(Value::Float),
        any::<String>().prop_map(Value::Str),
        prop::collection::vec(any::<u8>(), 0..24).prop_map(Value::Bytes),
        any::<[u8; 32]>().prop_map(|b| Value::Hash(Hash(b))),
        any::<[u8; 16]>().prop_map(|b| Value::Obj(ObjectId(b))),
    ]
}

fn value() -> impl Strategy<Value = Value> {
    leaf().prop_recursive(4, 48, 6, |inner| {
        prop_oneof![
            prop::collection::vec(inner.clone(), 0..6).prop_map(Value::List),
            prop::collection::btree_map("[a-zA-Z_][a-zA-Z0-9_ ]{0,6}", inner, 0..6)
                .prop_map(|m: BTreeMap<String, Value>| Value::Map(m)),
        ]
    })
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 400, ..ProptestConfig::default() })]

    #[test]
    fn binary_round_trips_and_is_canonical(v in value()) {
        let b = v.to_bytes();
        let back = Value::from_bytes(&b).unwrap();
        prop_assert_eq!(&back, &v);
        prop_assert_eq!(back.to_bytes(), b);
    }

    #[test]
    fn json_round_trips_and_is_canonical(v in value()) {
        let j = v.to_json();
        let back = Value::from_json(&j).unwrap();
        prop_assert_eq!(&back, &v);
        prop_assert_eq!(back.to_json(), j.clone());
        prop_assert!(!j.contains('\n'), "one line");
    }

    #[test]
    fn the_two_encodings_agree(v in value()) {
        let via_json = Value::from_json(&v.to_json()).unwrap();
        prop_assert_eq!(via_json.to_bytes(), v.to_bytes());
    }

    /// Any byte string the decoder accepts must be exactly what the encoder would produce:
    /// there is no second spelling of a value.
    #[test]
    fn decoder_accepts_only_canonical_bytes(bytes in prop::collection::vec(any::<u8>(), 0..40)) {
        if let Ok(v) = Value::from_bytes(&bytes) {
            prop_assert_eq!(v.to_bytes(), bytes);
        }
    }

    #[test]
    fn decoder_never_panics_on_garbage_json(s in "\\PC{0,60}") {
        let _ = Value::from_json(&s);
    }

    #[test]
    fn ordering_is_consistent_with_equality(a in value(), b in value()) {
        prop_assert_eq!(a == b, a.cmp(&b) == std::cmp::Ordering::Equal);
        prop_assert_eq!(a == b, a.to_bytes() == b.to_bytes());
    }
}
