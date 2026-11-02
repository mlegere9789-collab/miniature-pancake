# CYCLE-00 baseline: 20,000-agent tick time

Recorded 2026-11-02 with `target/release/hd-sim bench --agents 20000 --ticks 600 --seed 42`
(release profile, single thread, toy scenario of SPEC-003). This is the reference for the
20,000-agent tick budget.

| Agents | Ticks | Mean tick (us) | p99 tick (us) | Total wall (s) | Final hash |
|---:|---:|---:|---:|---:|---|
| 20000 | 600 | 11776 | 16783 | 7.07 | dd9a31d011f3ecd8 |
| 20000 | 600 | 12276 | 17384 | 7.37 | dd9a31d011f3ecd8 |
| 20000 | 600 | 11820 | 17504 | 7.09 | dd9a31d011f3ecd8 |

Typical figure: mean about 11.8 ms, p99 about 17 ms per tick, against the 50 ms budget of a
20 Hz tick.

Machine note: Linux x86_64 cloud container, 4 vCPU, Intel Xeon @ 2.10 GHz, rustc 1.97.0.
The simulation is single-threaded. Timings vary by a few percent between runs and by machine;
the final hash does not.
