//! Parameters of the committed golden replays and the format of `expected.json`.

/// Parameters that fully determine one golden replay.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GoldenSpec {
    /// Base name; the file is `<name>.hdrp`.
    pub name: &'static str,
    /// Number of agents.
    pub agents: u32,
    /// Number of ticks.
    pub ticks: u64,
    /// Master seed.
    pub seed: u64,
    /// Ticks between checkpoint hashes.
    pub checkpoint_interval: u32,
}

impl GoldenSpec {
    /// The file name inside `qc/golden-replays/`.
    pub fn file_name(&self) -> String {
        format!("{}.hdrp", self.name)
    }
}

/// The three committed goldens.
pub const GOLDENS: &[GoldenSpec] = &[
    GoldenSpec {
        name: "toy-small",
        agents: 100,
        ticks: 2000,
        seed: 1,
        checkpoint_interval: 100,
    },
    GoldenSpec {
        name: "toy-medium",
        agents: 1000,
        ticks: 2000,
        seed: 7,
        checkpoint_interval: 100,
    },
    GoldenSpec {
        name: "toy-large",
        agents: 20000,
        ticks: 600,
        seed: 42,
        checkpoint_interval: 100,
    },
];

/// A golden together with its final state hash.
#[derive(Clone, Copy, Debug)]
pub struct GoldenEntry<'a> {
    /// The parameters.
    pub spec: &'a GoldenSpec,
    /// The final state hash recorded in the replay.
    pub final_hash: u64,
}

/// Renders `expected.json`: file name and final hash (16 lower-case hex digits) per golden,
/// plus the parameters that produced it.
pub fn expected_json(entries: &[GoldenEntry<'_>]) -> String {
    let mut s = String::from("{\n  \"format\": 1,\n  \"replays\": [\n");
    for (i, e) in entries.iter().enumerate() {
        s.push_str(&format!(
            "    {{\"file\": \"{}\", \"agents\": {}, \"ticks\": {}, \"seed\": {}, \"checkpoint_interval\": {}, \"final_hash\": \"{:016x}\"}}",
            e.spec.file_name(),
            e.spec.agents,
            e.spec.ticks,
            e.spec.seed,
            e.spec.checkpoint_interval,
            e.final_hash
        ));
        s.push_str(if i + 1 < entries.len() { ",\n" } else { "\n" });
    }
    s.push_str("  ]\n}\n");
    s
}
