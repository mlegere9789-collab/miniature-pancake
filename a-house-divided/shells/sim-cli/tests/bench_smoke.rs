//! `bench` prints the expected fields (the only place wall-clock time appears).

mod common;

use common::process;

fn field(out: &str, name: &str) -> String {
    out.lines()
        .find_map(|l| l.strip_prefix(&format!("{name}=")))
        .unwrap_or_else(|| panic!("field {name} missing from:\n{out}"))
        .trim()
        .to_string()
}

#[test]
fn bench_prints_mean_p99_and_total() {
    let (code, out, err) = process(&["bench", "--agents", "1000", "--ticks", "50"]);
    assert_eq!(code, 0, "{err}");
    assert_eq!(field(&out, "agents"), "1000");
    assert_eq!(field(&out, "ticks"), "50");
    let mean = field(&out, "mean_tick_us");
    let p99 = field(&out, "p99_tick_us");
    let total = field(&out, "total_wall_us");
    for v in [&mean, &p99, &total] {
        assert!(
            !v.is_empty() && v.chars().all(|c| c.is_ascii_digit() || c == '.'),
            "not a number: {v}"
        );
    }
    assert_eq!(field(&out, "final_hash").len(), 16);
    let hash_run = process(&["hash", "--agents", "1000", "--ticks", "50", "--seed", "1"]);
    assert_eq!(field(&out, "final_hash"), hash_run.1.trim());
}
