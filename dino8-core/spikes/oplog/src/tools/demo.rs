//! Prints the scripted collaboration demo and the design-options demo.

use oplog::crdt::ConflictKind;
use oplog::demo::{run_collaboration_demo, run_design_options_demo};

fn main() {
    let r = run_collaboration_demo().expect("collaboration demo");
    println!("== two architects, one floor plan ==");
    for line in &r.timeline {
        println!("  {line}");
    }
    println!();
    println!("conflict items surfaced by the merge ({}):", r.conflicts.len());
    for c in &r.conflicts {
        let kind = match c.kind {
            ConflictKind::Register => "register",
            ConflictKind::ListElement => "list element",
            ConflictKind::DeleteVsEdit => "delete vs edit",
        };
        println!(
            "  {:<3} {:<14} [{kind}] shows {} ; other value(s): {} ; sessions {}",
            c.object,
            c.prop,
            c.winner,
            c.others.join(", "),
            c.sessions.iter().map(|s| format!("{s:016x}")).collect::<Vec<_>>().join(" & ")
        );
    }
    println!();
    println!("merged without a decision:");
    for l in &r.clean_merges {
        println!("  {l}");
    }
    println!();
    println!(
        "summary: {} Ana ops + {} Ben ops, {} ops in the log, merge op identical on both replicas: {}, conflicts: {} -> {} after resolution, final state {}",
        r.ana_ops, r.ben_ops, r.total_ops, r.merge_ops_identical, r.conflicts.len(), r.conflicts_after_resolution, r.final_state
    );
    println!();
    println!("== design options as branches ==");
    for l in run_design_options_demo().expect("options demo") {
        println!("  {l}");
    }
}
