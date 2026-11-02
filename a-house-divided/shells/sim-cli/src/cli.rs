//! Command-line front end with a small hand-written argument parser.
//!
//! Exit codes: `0` success or match, `1` replay mismatch, `2` malformed file or usage error.

use crate::bench::{micros, run_bench};
use crate::scenario::{final_hash, run_replay, verify_replay, ToyParams, Verdict};
use sim_state::ReplayReader;
use std::io::Write;

const USAGE: &str = "\
usage: hd-sim <command> [options]

commands:
  run    --agents K --ticks N --seed S --checkpoint-interval C --out FILE
         run the toy scenario and write a replay (defaults: K 1000, S 1, C 100)
  verify FILE
         re-run a replay and compare every checkpoint and the final hash
         (exit 0 match, 1 mismatch, 2 malformed file)
  hash   --agents K --ticks N --seed S
         print the final state hash
  bench  --agents K --ticks N [--seed S]
         print mean and p99 tick time in microseconds and total wall time
";

/// A usage or input problem; always exit code 2.
struct CliError(String);

type CliResult = Result<i32, CliError>;

fn err<T>(msg: impl Into<String>) -> Result<T, CliError> {
    Err(CliError(msg.into()))
}

/// Parsed `--name value` / `--name=value` options.
struct Flags {
    pairs: Vec<(String, String)>,
}

impl Flags {
    fn parse(args: &[String], allowed: &[&str]) -> Result<Flags, CliError> {
        let mut pairs: Vec<(String, String)> = Vec::new();
        let mut i = 0;
        while i < args.len() {
            let arg = &args[i];
            let Some(body) = arg.strip_prefix("--") else {
                return err(format!("unexpected argument {arg:?}"));
            };
            let (name, value) = match body.split_once('=') {
                Some((n, v)) => (n.to_string(), v.to_string()),
                None => {
                    i += 1;
                    match args.get(i) {
                        Some(v) => (body.to_string(), v.clone()),
                        None => return err(format!("option --{body} needs a value")),
                    }
                }
            };
            if !allowed.contains(&name.as_str()) {
                return err(format!("unknown option --{name}"));
            }
            if pairs.iter().any(|(n, _)| *n == name) {
                return err(format!("option --{name} given more than once"));
            }
            pairs.push((name, value));
            i += 1;
        }
        Ok(Flags { pairs })
    }

    fn raw(&self, name: &str) -> Option<&str> {
        self.pairs
            .iter()
            .find(|(n, _)| n == name)
            .map(|(_, v)| v.as_str())
    }

    fn number<T: std::str::FromStr>(&self, name: &str, default: Option<T>) -> Result<T, CliError> {
        match self.raw(name) {
            Some(v) => v
                .parse::<T>()
                .or_else(|_| err(format!("option --{name}: {v:?} is not a valid number"))),
            None => match default {
                Some(d) => Ok(d),
                None => err(format!("missing required option --{name}")),
            },
        }
    }
}

fn params_from(flags: &Flags, default_seed: Option<u64>) -> Result<ToyParams, CliError> {
    let agents: u32 = flags.number("agents", Some(1000))?;
    let seed: u64 = flags.number("seed", default_seed)?;
    let tick_hz: u16 = flags.number("tick-hz", Some(crate::scenario::DEFAULT_TICK_HZ))?;
    let mut p = ToyParams::new(agents, seed);
    p.tick_hz = tick_hz;
    Ok(p)
}

fn cmd_run(args: &[String], out: &mut dyn Write) -> CliResult {
    let flags = Flags::parse(
        args,
        &[
            "agents",
            "ticks",
            "seed",
            "checkpoint-interval",
            "out",
            "tick-hz",
        ],
    )?;
    let params = params_from(&flags, Some(1))?;
    let ticks: u64 = flags.number("ticks", None)?;
    let interval: u32 = flags.number("checkpoint-interval", Some(100))?;
    let Some(path) = flags.raw("out") else {
        return err("missing required option --out");
    };
    let replay = run_replay(&params, ticks, interval).or_else(|e| err(e.to_string()))?;
    std::fs::write(path, replay.to_bytes())
        .or_else(|e| err(format!("cannot write {path}: {e}")))?;
    let _ = writeln!(
        out,
        "wrote {path} agents={} ticks={ticks} seed={} checkpoints={} final_hash={:016x}",
        params.agents,
        params.seed,
        replay.checkpoints().len(),
        replay.final_hash()
    );
    Ok(0)
}

fn cmd_verify(args: &[String], out: &mut dyn Write, errw: &mut dyn Write) -> CliResult {
    let [path] = args else {
        return err("verify takes exactly one FILE argument");
    };
    let bytes = std::fs::read(path).or_else(|e| err(format!("cannot read {path}: {e}")))?;
    let replay =
        ReplayReader::parse(&bytes).or_else(|e| err(format!("{path}: malformed replay: {e}")))?;
    match verify_replay(&replay) {
        Verdict::Match => {
            let _ = writeln!(
                out,
                "OK {path}: {} checkpoints and final hash {:016x} match at tick {}",
                replay.checkpoints().len(),
                replay.final_hash(),
                replay.total_ticks()
            );
            Ok(0)
        }
        Verdict::Mismatch {
            tick,
            expected,
            actual,
        } => {
            let _ = writeln!(
                errw,
                "MISMATCH {path}: first difference at tick {tick}: stored {expected:016x}, computed {actual:016x}"
            );
            Ok(1)
        }
        Verdict::Malformed(why) => err(format!("{path}: {why}")),
    }
}

fn cmd_hash(args: &[String], out: &mut dyn Write) -> CliResult {
    let flags = Flags::parse(args, &["agents", "ticks", "seed", "tick-hz"])?;
    let params = params_from(&flags, None)?;
    let ticks: u64 = flags.number("ticks", None)?;
    let h = final_hash(&params, ticks).or_else(|e| err(e.to_string()))?;
    let _ = writeln!(out, "{h:016x}");
    Ok(0)
}

fn cmd_bench(args: &[String], out: &mut dyn Write) -> CliResult {
    let flags = Flags::parse(args, &["agents", "ticks", "seed", "tick-hz"])?;
    let params = params_from(&flags, Some(1))?;
    let ticks: u64 = flags.number("ticks", Some(100))?;
    let report = run_bench(&params, ticks).or_else(|e| err(e.to_string()))?;
    let _ = writeln!(out, "agents={}", params.agents);
    let _ = writeln!(out, "ticks={}", report.ticks);
    let _ = writeln!(out, "mean_tick_us={}", micros(report.mean_ns));
    let _ = writeln!(out, "p99_tick_us={}", micros(report.p99_ns));
    let _ = writeln!(out, "total_wall_us={}", micros(report.total_ns));
    let _ = writeln!(out, "final_hash={:016x}", report.final_hash);
    Ok(0)
}

/// Runs one command line (without the program name); returns the process exit code.
pub fn run_cli(args: &[String], out: &mut dyn Write, errw: &mut dyn Write) -> i32 {
    let Some((command, rest)) = args.split_first() else {
        let _ = write!(errw, "hd-sim: no command given\n\n{USAGE}");
        return 2;
    };
    let result = match command.as_str() {
        "run" => cmd_run(rest, out),
        "verify" => cmd_verify(rest, out, errw),
        "hash" => cmd_hash(rest, out),
        "bench" => cmd_bench(rest, out),
        "help" | "--help" | "-h" => {
            let _ = write!(out, "{USAGE}");
            Ok(0)
        }
        other => err(format!("unknown command {other:?}\n\n{USAGE}")),
    };
    match result {
        Ok(code) => code,
        Err(CliError(msg)) => {
            let _ = writeln!(errw, "hd-sim: {msg}");
            2
        }
    }
}
