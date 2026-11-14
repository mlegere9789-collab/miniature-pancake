//! The text-diffable serialization: canonical JSON lines round-trip exactly.

mod common;

use common::*;
use oplog::export::{import_jsonl, jsonl_doc_id};
use oplog::{Error, Workspace};
use proptest::prelude::*;

fn fresh_like(text: &str, session: u64) -> Workspace {
    let doc = jsonl_doc_id(text).unwrap();
    Workspace::in_memory(session, &doc).with_clock(clock())
}

fn session_ws(seed: u64, steps: usize) -> Workspace {
    let mut ws = mem(1);
    let mut sc = script(seed, true);
    drive(&mut ws, &mut sc, steps);
    ws
}

#[test]
fn export_import_export_is_byte_identical() {
    let ws = session_ws(31, 700);
    let text = ws.export_jsonl().unwrap();
    assert!(text.lines().count() > ws.len());
    let mut back = fresh_like(&text, 2);
    import_jsonl(&mut back, &text).unwrap();
    assert_eq!(back.export_jsonl().unwrap(), text, "second export differs from the first");
    assert_eq!(back.op_ids(), ws.op_ids());
    assert_eq!(back.head(), ws.head());
    assert_eq!(back.state_hash(), ws.state_hash());
    assert_eq!(back.current_branch(), ws.current_branch());
    assert_eq!(
        back.branches().map(|(n, o)| (n.clone(), *o)).collect::<Vec<_>>(),
        ws.branches().map(|(n, o)| (n.clone(), *o)).collect::<Vec<_>>()
    );
    back.verify().unwrap();
    // the imported log replays to the same state
    let rep = back.replay_check(&oplog::commands::standard()).unwrap();
    assert!(rep.ok(), "{:?}", rep.mismatches);
    assert_eq!(rep.final_root, ws.state_hash());
}

#[test]
fn lines_are_canonical_and_the_text_only_grows_at_the_end() {
    let mut ws = mem(1);
    let mut sc = script(8, false);
    drive(&mut ws, &mut sc, 120);
    let t1 = ws.export_jsonl().unwrap();
    drive(&mut ws, &mut sc, 80);
    let t2 = ws.export_jsonl().unwrap();
    // everything except the trailing ref/head footer is a prefix of the longer export
    let body1: Vec<&str> = t1.lines().filter(|l| !l.contains("\"kind\":\"head\"") && !l.contains("\"kind\":\"ref\"")).collect();
    let body2: Vec<&str> = t2.lines().collect();
    assert_eq!(&body2[..body1.len()], &body1[..], "appending operations must only append lines");
    // sorted keys, no whitespace, one object per line
    for l in t2.lines() {
        assert!(l.starts_with('{') && l.ends_with('}'));
        assert!(!l.contains(": ") && !l.contains(", "));
        assert_eq!(oplog::value::Value::from_json(l).unwrap().to_json(), l);
    }
    // object snapshots appear as readable structure, not opaque bytes
    assert!(t2.contains("\"type\":\"obj\"") && t2.contains("\"life\""));
}

#[test]
fn tampering_with_the_text_is_detected() {
    let ws = session_ws(5, 150);
    let text = ws.export_jsonl().unwrap();
    let try_import = |t: &str| {
        let mut b = fresh_like(&text, 3);
        import_jsonl(&mut b, t)
    };
    assert!(try_import(&text).is_ok());
    // change a value inside an operation line: its id no longer matches
    let tampered = text.replacen("\"seq\":2,", "\"seq\":3,", 1);
    assert_ne!(tampered, text);
    assert!(matches!(try_import(&tampered), Err(Error::Mismatch(_))), "op id must be rechecked");
    // change a value inside an object snapshot: the blob no longer hashes to its name
    let blob_line = text.lines().find(|l| l.contains("\"type\":\"obj\"")).unwrap().to_string();
    let edited = blob_line.replacen("true", "false", 1);
    assert_ne!(edited, blob_line);
    let tampered = text.replacen(&blob_line, &edited, 1);
    assert!(try_import(&tampered).is_err());
    // insignificant whitespace breaks canonical form
    let spaced = text.replacen("\"kind\":\"header\",", "\"kind\":\"header\", ", 1);
    assert!(matches!(try_import(&spaced), Err(Error::Corrupt(_))));
    // an operation before its parent is refused
    let mut lines: Vec<&str> = text.lines().collect();
    let first_op = lines.iter().position(|l| l.contains("\"kind\":\"op\"") && !l.contains("\"parents\":[]")).unwrap();
    let second_op = lines.iter().skip(first_op + 1).position(|l| l.contains("\"kind\":\"op\"")).unwrap() + first_op + 1;
    lines.swap(first_op, second_op);
    let reordered = lines.join("\n") + "\n";
    assert!(try_import(&reordered).is_err());
    // an export of a different document is refused outright
    let mut other = Workspace::in_memory(3, "another-doc");
    assert!(matches!(import_jsonl(&mut other, &text), Err(Error::Mismatch(_))));
}

#[test]
fn geometry_blobs_survive_the_text_round_trip() {
    let mut ws = mem(1);
    let mut p = create_params("big", [0.0; 3]);
    if let oplog::value::Value::Map(m) = &mut p {
        m.insert("geom_bytes".into(), oplog::value::Value::Int(5000));
    }
    ws.run("create_box", p).unwrap();
    let text = ws.export_jsonl().unwrap();
    assert!(text.contains("\"type\":\"raw\""));
    let mut back = fresh_like(&text, 2);
    import_jsonl(&mut back, &text).unwrap();
    assert_eq!(back.export_jsonl().unwrap(), text);
    let (id, obj) = back.objects().unwrap().remove(0);
    let g = obj.value("geom").unwrap().as_hash().unwrap();
    assert_eq!(back.store().get_raw(&g).unwrap().len(), 5000);
    let _ = id;
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 24, ..ProptestConfig::default() })]

    #[test]
    fn random_sessions_round_trip_through_text(seed in 0u64..10_000, steps in 20usize..120, nav in any::<bool>()) {
        let mut ws = mem(1);
        let mut sc = script(seed, nav);
        drive(&mut ws, &mut sc, steps);
        let text = ws.export_jsonl().unwrap();
        let mut back = fresh_like(&text, 2);
        import_jsonl(&mut back, &text).unwrap();
        prop_assert_eq!(back.export_jsonl().unwrap(), text);
        prop_assert_eq!(back.state_hash(), ws.state_hash());
        prop_assert_eq!(back.op_ids(), ws.op_ids());
    }
}
