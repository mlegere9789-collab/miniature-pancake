//! Tick timing. This is the only module that reads a wall clock; nothing it measures ever feeds
//! back into simulation state.

use crate::scenario::{Scenario, ScenarioError, ToyParams};
use std::time::Instant;

/// Timing summary of a benchmark run, in nanoseconds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BenchReport {
    /// Number of ticks timed.
    pub ticks: u64,
    /// Mean tick time.
    pub mean_ns: u64,
    /// 99th percentile (nearest rank) tick time.
    pub p99_ns: u64,
    /// Wall time for the whole run, scenario setup included.
    pub total_ns: u64,
    /// State hash at the end of the run.
    pub final_hash: u64,
}

fn clamp_ns(d: std::time::Duration) -> u64 {
    u64::try_from(d.as_nanos()).unwrap_or(u64::MAX)
}

/// Runs `ticks` ticks (at least one) and times each one.
pub fn run_bench(params: &ToyParams, ticks: u64) -> Result<BenchReport, ScenarioError> {
    if ticks == 0 {
        return Err(ScenarioError::InvalidParams(
            "ticks must be at least 1".to_string(),
        ));
    }
    let started = Instant::now();
    let mut scenario = Scenario::new(params)?;
    let mut samples: Vec<u64> = Vec::new();
    for _ in 0..ticks {
        let t = Instant::now();
        scenario.step();
        samples.push(clamp_ns(t.elapsed()));
    }
    let total_ns = clamp_ns(started.elapsed());
    let sum: u128 = samples.iter().map(|&s| u128::from(s)).sum();
    let mean_ns = (sum / u128::from(ticks)) as u64;
    samples.sort_unstable();
    let rank = (samples.len() * 99).div_ceil(100).max(1);
    Ok(BenchReport {
        ticks,
        mean_ns,
        p99_ns: samples[rank - 1],
        total_ns,
        final_hash: scenario.hash(),
    })
}

/// Formats nanoseconds as microseconds with three decimals, using integers only.
pub fn micros(ns: u64) -> String {
    format!("{}.{:03}", ns / 1000, ns % 1000)
}
