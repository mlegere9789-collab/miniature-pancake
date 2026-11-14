//! The benchmark workload: the incremental engine must reproduce the global-recompute
//! baseline bit for bit after every kind of edit, for several thread counts, and an edit must
//! recompute only what depends on it.

use dino8_graph::workload::*;
use dino8_graph::{Config, Priority};

fn small() -> Params {
    Params { walls: 8 * 4 * 12, levels: 8, types: 5, leaf: 8, fan_in: 4, walls_per_tile: 6, seed: 3, costs: Costs::default() }
}

fn cfg(threads: usize) -> Config {
    Config { threads, serial_budget: 16, split_threshold: 4, ..Config::default() }
}

fn check(wl: &Workload, what: &str) {
    let engine = wl.outputs();
    for parallel in [false, true] {
        let base = global_recompute(&wl.layout, &wl.inputs, parallel);
        assert_eq!(engine, base, "{what}: engine differs from the global recompute (parallel={parallel})");
    }
}

fn kind_computes(wl: &Workload, name: &str) -> u64 {
    wl.db.computes_by_kind().iter().find(|(k, _)| k == name).map_or(0, |(_, n)| *n)
}

#[test]
fn cold_evaluation_equals_global_recompute() {
    for threads in [1, 4] {
        let wl = Workload::build(&small(), cfg(threads));
        let rep = wl.db.evaluate(&wl.all_roots()).unwrap();
        assert!(rep.computed > 500);
        check(&wl, &format!("cold threads={threads}"));
    }
}

#[test]
fn every_edit_kind_keeps_the_engine_equal_to_the_baseline() {
    for threads in [1, 3] {
        let mut wl = Workload::build(&small(), cfg(threads));
        wl.db.evaluate(&wl.all_roots()).unwrap();
        let n = wl.layout.n;
        let mut rng = Rng(11);
        for round in 0..24 {
            let w = rng.below(n);
            let changes = match round % 10 {
                0 => wl.edit_move_wall(w, 0.013),
                1 => wl.edit_move_across_tile(w),
                2 => wl.edit_height(w, 0.05),
                3 => wl.edit_cosmetic(w),
                4 => wl.edit_wall_type(rng.below(5), 0.004),
                5 => wl.edit_level(rng.below(8), 0.02),
                6 => wl.edit_view_cut(rng.below(8), 0.3),
                7 => wl.edit_tag_style(0.05),
                8 => {
                    let mut c = wl.edit_move_wall(w, -0.02);
                    c.extend(wl.edit_move_across_tile(rng.below(n)));
                    c
                }
                _ => wl.edit_move_wall(w, 0.0),
            };
            wl.apply(&format!("round {round}"), &changes);
            // Visible-first evaluation, then the rest.
            let mut reqs: Vec<(dino8_graph::NodeId, Priority)> = vec![(wl.view_roots[0], Priority::Visible), (wl.sched_root, Priority::Visible)];
            reqs.extend(wl.view_roots[1..].iter().map(|&v| (v, Priority::Background)));
            wl.db.evaluate_with(&reqs, &Default::default()).unwrap();
            check(&wl, &format!("threads={threads} round={round}"));
        }
    }
}

#[test]
fn a_single_wall_edit_recomputes_a_tiny_fraction() {
    let mut wl = Workload::build(&small(), cfg(1));
    let cold = wl.db.evaluate(&wl.all_roots()).unwrap();
    let w = 5;
    let ch = wl.edit_move_wall(w, 0.01);
    let er = wl.apply("move wall", &ch);
    assert!(er.marked_stale < 80, "marked {}", er.marked_stale);
    let rep = wl.db.evaluate(&wl.all_roots()).unwrap();
    assert!(rep.computed >= 4, "geom, joins, tag, chunk path, tile, view");
    assert!(rep.computed * 20 < cold.computed, "edit recomputed {} of {} nodes", rep.computed, cold.computed);
    // Only the edited wall's geometry recomputed.
    assert_eq!(kind_computes(&wl, "geom"), wl.layout.n as u64 + 1);
    check(&wl, "single wall edit");
}

#[test]
fn view_cut_edit_touches_only_that_view() {
    let mut wl = Workload::build(&small(), cfg(1));
    wl.db.evaluate(&wl.all_roots()).unwrap();
    let (g0, j0, t0, s0) = (kind_computes(&wl, "geom"), kind_computes(&wl, "join"), kind_computes(&wl, "tag"), kind_computes(&wl, "chunk"));
    let ch = wl.edit_view_cut(3, 0.3);
    wl.apply("cut", &ch);
    wl.db.evaluate(&wl.all_roots()).unwrap();
    assert_eq!((kind_computes(&wl, "geom"), kind_computes(&wl, "join"), kind_computes(&wl, "tag"), kind_computes(&wl, "chunk")), (g0, j0, t0, s0));
    assert_eq!(kind_computes(&wl, "view"), wl.layout.p.levels as u64 + 1);
    assert_eq!(kind_computes(&wl, "tile"), (wl.layout.p.levels * wl.layout.tiles2()) as u64 + wl.layout.tiles2() as u64);
    check(&wl, "view cut");
}

#[test]
fn cosmetic_edit_is_cut_off_at_the_geometry() {
    let mut wl = Workload::build(&small(), cfg(2));
    wl.db.evaluate(&wl.all_roots()).unwrap();
    let ch = wl.edit_cosmetic(7);
    wl.apply("cosmetic", &ch);
    let rep = wl.db.evaluate(&wl.all_roots()).unwrap();
    assert_eq!((rep.computed, rep.cutoffs), (1, 1), "only geom(7) recomputes and its result is unchanged");
    assert!(rep.verified >= 3, "join, chunk path and the rest revalidate without running");
    check(&wl, "cosmetic");
}

#[test]
fn edits_on_levels_that_are_not_demanded_cost_nothing_until_demanded() {
    let mut wl = Workload::build(&small(), cfg(1));
    wl.db.evaluate(&wl.all_roots()).unwrap();
    let w = wl.level_first_wall(5) + 2;
    let ch = wl.edit_move_wall(w, 0.02);
    wl.apply("level 5 wall", &ch);
    // Visible: views 0 and 1 only (level 5 is off screen) and no schedule.
    let tiles_before = kind_computes(&wl, "tile");
    wl.db.evaluate(&[wl.view_roots[0], wl.view_roots[1]]).unwrap();
    assert_eq!(kind_computes(&wl, "tile"), tiles_before, "views 0 and 1 do not depend on the edited wall");
    assert_eq!(kind_computes(&wl, "geom"), wl.layout.n as u64, "geometry is demanded only through a view or the schedule");
    // Demand view 5: now the dependent tile recomputes.
    wl.db.evaluate(&[wl.view_roots[5]]).unwrap();
    assert!(kind_computes(&wl, "tile") > tiles_before);
    wl.db.evaluate(&wl.all_roots()).unwrap();
    check(&wl, "off-screen edit");
}

#[test]
fn schedule_reduction_tree_has_logarithmic_edit_cost() {
    let p = Params { walls: 4 * 8 * 64, levels: 8, types: 5, leaf: 16, fan_in: 4, walls_per_tile: 8, seed: 5, costs: Costs::default() };
    let mut wl = Workload::build(&p, cfg(1));
    wl.db.evaluate(&[wl.sched_root]).unwrap();
    let depth = wl.layout.tree.depth();
    assert!(depth >= 4, "test needs a deep tree, got {depth}");
    let before = kind_computes(&wl, "chunk") + kind_computes(&wl, "rnode");
    let ch = wl.edit_height(100, 0.05);
    wl.apply("height", &ch);
    wl.db.evaluate(&[wl.sched_root]).unwrap();
    let after = kind_computes(&wl, "chunk") + kind_computes(&wl, "rnode");
    assert_eq!(after - before, depth as u64, "one node per tree level recomputes");
}
