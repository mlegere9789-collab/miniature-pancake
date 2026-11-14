//! The scripted demo is a reproducible measurement: this pins its numbers.

use oplog::crdt::ConflictKind;
use oplog::demo::{run_collaboration_demo, run_design_options_demo};

#[test]
fn two_architects_produce_exactly_four_conflicts_and_converge() {
    let r = run_collaboration_demo().unwrap();
    assert!(r.merge_ops_identical && r.converged_after_merge);
    assert_eq!(r.conflicts.len(), 4, "{:#?}", r.conflicts);
    let kinds = |k: ConflictKind| r.conflicts.iter().filter(|c| c.kind == k).count();
    assert_eq!(kinds(ConflictKind::Register), 2);
    assert_eq!(kinds(ConflictKind::ListElement), 1);
    assert_eq!(kinds(ConflictKind::DeleteVsEdit), 1);
    let names: Vec<(&str, &str)> = r.conflicts.iter().map(|c| (c.object.as_str(), c.prop.as_str())).collect();
    assert!(names.contains(&("W1", "pos")));
    assert!(names.contains(&("C1", "color")));
    assert!(names.contains(&("P1", "pts (vertex)")));
    assert!(names.iter().any(|(o, p)| *o == "S1" && p.contains("life")));
    // both authors are named in every conflict
    assert!(r.conflicts.iter().all(|c| c.sessions.len() == 2));
    // the non-conflicting concurrent edits were merged silently
    assert!(r.clean_merges[0].contains("fire-rated") && r.clean_merges[0].contains("load-bearing"));
    let tag_set = r.clean_merges[0].split(" (").next().unwrap();
    assert!(!tag_set.contains("exterior"), "Ben removed it and Ana never touched it: {tag_set}");
    assert!(r.clean_merges[2].contains("6 vertices"));
    assert_eq!(r.conflicts_after_resolution, 0);
    assert!(r.converged_after_resolution);
    assert_eq!(r.ana_ops, 7);
    assert_eq!(r.ben_ops, 8);
    // fully reproducible
    let again = run_collaboration_demo().unwrap();
    assert_eq!(again.final_state, r.final_state);
    assert_eq!(again.merge_op, r.merge_op);
}

#[test]
fn design_options_demo_runs() {
    let lines = run_design_options_demo().unwrap();
    assert!(lines.iter().any(|l| l.contains("3 branches")));
    assert!(lines.iter().any(|l| l.contains("switch to option-brick: color = 1")));
    assert!(lines.iter().any(|l| l.contains("switch to option-glass: color = 2")));
    assert!(lines.iter().any(|l| l.contains("1 conflict item")));
}
