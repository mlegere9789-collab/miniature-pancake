//! The scripted editing demo: two architects change the same floor plan at the same time.
//!
//! The scenario is deterministic (fixed sessions, fixed clock), so the number of conflict items
//! it produces is a reproducible result, asserted by `tests/demo.rs` and printed by the
//! `oplog-demo` binary.

use crate::crdt::ConflictKind;
use crate::engine::{MergeKind, StepClock, Workspace};
use crate::error::Result;
use crate::ids::{ObjectId, OpId};
use crate::value::Value;

/// One conflict item in a readable form.
#[derive(Debug, Clone, PartialEq)]
pub struct ConflictLine {
    pub object: String,
    pub prop: String,
    pub kind: ConflictKind,
    pub winner: String,
    pub others: Vec<String>,
    pub sessions: Vec<u64>,
}

#[derive(Debug, Clone)]
pub struct DemoReport {
    pub ana_ops: usize,
    pub ben_ops: usize,
    /// Operations in the shared log after the exchange.
    pub total_ops: usize,
    /// The merge operation both replicas created (identical id).
    pub merge_op: OpId,
    pub merge_ops_identical: bool,
    pub converged_after_merge: bool,
    pub conflicts: Vec<ConflictLine>,
    /// Properties edited by both users that merged without any conflict.
    pub clean_merges: Vec<String>,
    pub conflicts_after_resolution: usize,
    pub converged_after_resolution: bool,
    pub final_state: String,
    pub timeline: Vec<String>,
}

const ANA: u64 = 0xA11C_E000_0000_0001;
const BEN: u64 = 0xBE17_0000_0000_0002;

fn mv(id: ObjectId, d: [f64; 3]) -> Value {
    Value::map([("ids", Value::List(vec![Value::Obj(id)])), ("delta", Value::f3(d))])
}

fn set(id: ObjectId, prop: &str, v: Value) -> Value {
    Value::map([("id", Value::Obj(id)), ("prop", Value::str(prop)), ("value", v)])
}

fn tag(id: ObjectId, t: &str) -> Value {
    Value::map([("id", Value::Obj(id)), ("tag", Value::str(t))])
}

fn pt(id: ObjectId, index: i64, p: [f64; 3]) -> Value {
    Value::map([("id", Value::Obj(id)), ("index", Value::Int(index)), ("point", Value::f3(p))])
}

fn boxp(name: &str, pos: [f64; 3], size: [f64; 3], layer: &str, tags: &[&str]) -> Value {
    Value::map([
        ("name", Value::str(name)),
        ("pos", Value::f3(pos)),
        ("size", Value::f3(size)),
        ("layer", Value::str(layer)),
        ("color", Value::Int(7)),
        ("geom_bytes", Value::Int(0)),
        ("tags", Value::List(tags.iter().map(|t| Value::str(t)).collect())),
    ])
}

/// Create a box and return the id of the new object.
fn make(ws: &mut Workspace, params: Value) -> Result<ObjectId> {
    let op = ws.run("create_box", params)?;
    Ok(ws.op(op)?.outputs.iter().find(|o| o.before.is_none()).map(|o| o.id).unwrap_or_default())
}

fn show(v: &Value) -> String {
    match v {
        Value::Str(s) => format!("\"{s}\""),
        Value::List(l) => format!("[{}]", l.iter().map(show).collect::<Vec<_>>().join(", ")),
        Value::Float(f) => format!("{f}"),
        Value::Int(i) => format!("{i}"),
        Value::Bool(b) => format!("{b}"),
        other => other.to_json(),
    }
}

fn name_of(ws: &Workspace, id: ObjectId) -> String {
    ws.object(id)
        .ok()
        .flatten()
        .and_then(|o| o.value("name").and_then(|n| n.as_str().map(str::to_string)))
        .unwrap_or_else(|| format!("{id:?}"))
}

fn lines(ws: &Workspace) -> Result<Vec<ConflictLine>> {
    let mut v: Vec<ConflictLine> = ws
        .conflicts()?
        .into_iter()
        .map(|c| ConflictLine {
            object: name_of(ws, c.object),
            prop: match c.elem {
                Some(_) => format!("{} (vertex)", c.prop),
                None => c.prop.clone(),
            },
            kind: c.kind,
            winner: show(&c.winner),
            others: c.others.iter().map(show).collect(),
            sessions: c.sessions,
        })
        .collect();
    v.sort_by(|a, b| (&a.object, &a.prop).cmp(&(&b.object, &b.prop)));
    Ok(v)
}

/// Run the whole scenario.
pub fn run_collaboration_demo() -> Result<DemoReport> {
    let clock = || Box::new(StepClock { next: 1_800_000_000_000, step: 1000 });
    let mut ana = Workspace::in_memory(ANA, "floor-plan").with_clock(clock());
    let mut timeline = Vec::new();

    // --- shared starting point, authored by Ana ---
    let w1 = make(&mut ana, boxp("W1", [0.0; 3], [10.0, 0.3, 3.0], "A-WALL", &["exterior"]))?;
    let w2 = make(&mut ana, boxp("W2", [10.0, 0.0, 0.0], [0.3, 8.0, 3.0], "A-WALL", &[]))?;
    let d1 = make(&mut ana, boxp("D1", [4.0, 0.0, 0.0], [1.0, 0.3, 2.1], "A-DOOR", &[]))?;
    ana.run("set_prop", set(d1, "host", Value::Obj(w1)))?;
    let c1 = make(&mut ana, boxp("C1", [10.0, 8.0, 0.0], [0.4, 0.4, 3.0], "S-COLUMN", &[]))?;
    let s1 = make(&mut ana, boxp("S1", [0.0, 0.0, -0.3], [10.0, 8.0, 0.3], "A-SLAB", &[]))?;
    let p1 = make(&mut ana, boxp("P1", [0.0, 0.0, 0.0], [1.0, 1.0, 1.0], "A-ANNO", &[]))?;
    for (i, p) in [[0.0, 0.0, 0.0], [10.0, 0.0, 0.0], [10.0, 8.0, 0.0], [0.0, 8.0, 0.0]].iter().enumerate() {
        ana.run("pts_insert", pt(p1, i as i64, *p))?;
    }
    let base = ana.head();
    timeline.push(format!("Ana builds the shared plan: {} operations, {} objects", ana.len() - 1, ana.object_count()));

    // --- Ben joins: he receives the log and starts from the same head ---
    let mut ben = Workspace::in_memory(BEN, "floor-plan").with_clock(clock());
    ben.pull_from(&ana)?;
    ben.checkout(base)?;
    timeline.push("Ben pulls Ana's log and checks out her head (identical state hash)".to_string());
    assert_eq!(ben.state_hash(), ana.state_hash());

    // --- concurrent edits, nobody online to see the other ---
    let ana_before = ana.len();
    ana.run("move", mv(w1, [1.0, 0.0, 0.0]))?; //                      same property as Ben: conflict
    ana.run("tag_add", tag(w1, "fire-rated"))?; //                     set: no conflict
    ana.run("set_prop", set(w2, "name", Value::str("W2-north")))?; //  different property than Ben: clean
    ana.run("pts_insert", pt(p1, 2, [5.0, 1.0, 0.0]))?; //             list insert at the same place: both kept
    ana.run("set_prop", set(c1, "color", Value::Int(3)))?; //          same register as Ben: conflict
    ana.run("pts_move", pt(p1, 3, [9.0, 9.0, 0.0]))?; //               same vertex as Ben: conflict
    ana.run("delete", Value::map([("ids", Value::List(vec![Value::Obj(s1)]))]))?; // Ben edits it: conflict
    let ben_before = ben.len();
    ben.run("move", mv(w1, [0.0, 2.0, 0.0]))?;
    ben.run("tag_add", tag(w1, "load-bearing"))?;
    ben.run("tag_remove", tag(w1, "exterior"))?; //                    Ana never touched this element
    ben.run("set_prop", set(w2, "layer", Value::str("A-WALL-OLD")))?;
    ben.run("pts_insert", pt(p1, 2, [5.0, -1.0, 0.0]))?;
    ben.run("set_prop", set(c1, "color", Value::Int(9)))?;
    ben.run("pts_move", pt(p1, 3, [8.0, 8.0, 0.0]))?;
    ben.run("set_prop", set(s1, "color", Value::Int(5)))?;
    let (ana_ops, ben_ops) = (ana.len() - ana_before, ben.len() - ben_before);
    timeline.push(format!("offline: Ana makes {ana_ops} edits, Ben makes {ben_ops} edits"));

    // --- exchange operations and merge ---
    let (ana_head, ben_head) = (ana.head(), ben.head());
    ana.pull_from(&ben)?;
    ben.pull_from(&ana)?;
    let ma = ana.merge(ben_head)?;
    let mb = ben.merge(ana_head)?;
    assert_eq!(ma.kind, MergeKind::Merged);
    let merge_op = ma.op.unwrap_or_default();
    let merge_ops_identical = ma.op == mb.op;
    let converged_after_merge = ana.state_hash() == ben.state_hash();
    let conflicts = lines(&ana)?;
    timeline.push(format!(
        "sync + merge: both replicas create merge op {} ({}); states equal: {converged_after_merge}; {} conflict items",
        &merge_op.to_hex()[..10],
        if merge_ops_identical { "identical" } else { "DIFFERENT" },
        conflicts.len()
    ));

    // properties both users touched that needed no decision
    let mut clean = Vec::new();
    let w1o = ana.object(w1)?.unwrap_or_default();
    let tags: Vec<String> =
        w1o.set("tags").map(|s| s.visible().map(show).collect()).unwrap_or_default();
    clean.push(format!("W1.tags = {{{}}} (add-wins set: both additions kept, Ben's removal of \"exterior\" applied)", tags.join(", ")));
    let w2o = ana.object(w2)?.unwrap_or_default();
    clean.push(format!(
        "W2.name = {} and W2.layer = {} (different properties: both edits kept)",
        w2o.value("name").map(show).unwrap_or_default(),
        w2o.value("layer").map(show).unwrap_or_default()
    ));
    let pts = ana.object(p1)?.unwrap_or_default().list("pts").map(|l| l.len()).unwrap_or(0);
    clean.push(format!("P1.pts has {pts} vertices (4 original + both concurrent inserts, in one deterministic order)"));

    // --- resolution: Ana decides, Ben receives the decisions ---
    ana.run("set_prop", set(w1, "pos", Value::f3([0.0, 2.0, 0.0])))?; // Ben's position
    ana.run("set_prop", set(c1, "color", Value::Int(3)))?; //             her own colour
    ana.run("keep", Value::map([("ids", Value::List(vec![Value::Obj(s1)]))]))?; // keep the slab
    // the conflicted vertex is no longer at index 3 (both users inserted a vertex before it):
    // find its current position from the conflict item itself
    let raw = ana.conflicts()?;
    let vertex = raw.iter().find_map(|c| c.elem);
    if let Some(elem) = vertex {
        let index = ana
            .object(p1)?
            .unwrap_or_default()
            .list("pts")
            .map_or(0, |l| l.visible_ids().iter().position(|id| *id == elem).unwrap_or(0));
        ana.run("pts_move", pt(p1, index as i64, [9.0, 9.0, 0.0]))?;
    }
    let after = ana.conflicts()?.len();
    ben.pull_from(&ana)?;
    ben.checkout(ana.head())?;
    let converged_after_resolution = ana.state_hash() == ben.state_hash();
    timeline.push(format!(
        "Ana resolves all four items with ordinary operations; conflicts left: {after}; Ben pulls and matches: {converged_after_resolution}"
    ));

    let rep = ana.replay_check(&crate::commands::standard())?;
    timeline.push(format!(
        "replay of the shared log ({} operations, {} merges) reproduces state {:?}: {}",
        rep.ops,
        rep.merges,
        rep.final_root,
        if rep.ok() && rep.final_root == ana.state_hash() { "identical" } else { "MISMATCH" }
    ));
    Ok(DemoReport {
        ana_ops,
        ben_ops,
        total_ops: ana.len(),
        merge_op,
        merge_ops_identical,
        converged_after_merge,
        conflicts,
        clean_merges: clean,
        conflicts_after_resolution: after,
        converged_after_resolution,
        final_state: format!("{:?}", ana.state_hash()),
        timeline,
    })
}

/// Design options as branches: two alternatives from one base, compared and combined.
pub fn run_design_options_demo() -> Result<Vec<String>> {
    let mut ws = Workspace::in_memory(ANA, "options").with_clock(Box::new(StepClock { next: 1_800_000_000_000, step: 1000 }));
    let mut out = Vec::new();
    let wall = make(&mut ws, boxp("wall", [0.0; 3], [10.0, 0.3, 3.0], "A-WALL", &[]))?;
    ws.branch("main")?;
    ws.branch("option-brick")?;
    ws.run("set_prop", set(wall, "color", Value::Int(1)))?;
    ws.run("set_prop", set(wall, "layer", Value::str("A-WALL-BRICK")))?;
    ws.switch("main")?;
    ws.branch("option-glass")?;
    ws.run("set_prop", set(wall, "color", Value::Int(2)))?;
    ws.run("move", mv(wall, [0.0, 0.5, 0.0]))?;
    let (brick, glass) = (ws.branches().find(|(n, _)| *n == "option-brick").map(|(_, o)| *o), ws.branches().find(|(n, _)| *n == "option-glass").map(|(_, o)| *o));
    let (brick, glass) = (brick.unwrap_or_default(), glass.unwrap_or_default());
    let d = ws.diff_ops(brick, glass)?;
    out.push(format!("options: {} branches ({}), tips differ in {} object(s)", ws.branches().count(), ws.branches().map(|(n, _)| n.as_str()).collect::<Vec<_>>().join(", "), d.len()));
    ws.switch("option-brick")?;
    out.push(format!("switch to option-brick: color = {}", show(ws.object(wall)?.unwrap_or_default().value("color").unwrap_or(&Value::Null))));
    ws.switch("option-glass")?;
    out.push(format!("switch to option-glass: color = {}", show(ws.object(wall)?.unwrap_or_default().value("color").unwrap_or(&Value::Null))));
    let m = ws.merge(brick)?;
    out.push(format!("merge option-brick into option-glass: {:?}, {} conflict item(s) (both set colour)", m.kind, m.conflicts.len()));
    ws.undo()?;
    out.push(format!("undo the merge: back on option-glass tip, state {:?}", ws.state_hash()));
    ws.redo()?;
    out.push(format!("redo: merged state {:?}", ws.state_hash()));
    Ok(out)
}
