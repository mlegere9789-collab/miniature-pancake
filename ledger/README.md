# Progress ledger and Parity Register tooling

Plan reference: Dino 8 Master Plan, Revision 2, section 7 (ledger), section 4 and Appendix C
(Register), sections 6.4, 8 and 11.2 (pace, cloud operations, re-plan).

The ledger turns "how far are we" into a computed number. Three rules carry everything:

1. **Evidence-gated.** A row counts only when CI evidence shows every one of its acceptance
   tests passing. An agent's own statement counts as nothing.
2. **Computed, not typed.** `ledger/percent.json` is produced by `parity/tools/percent.py` from
   the Register and the evidence. A hand-typed number is rejected by `--check`.
3. **Conflict-free.** One file per task, one append-only heartbeat file per agent per day, one
   git ref per claim. No shared mutable file.

```
 test runners (kernel, app sections)           agents
   one JSONL line per check                      |  ledger/tools/heartbeat.py -> heartbeats/<date>/<agent>.jsonl
          |                                      |  ledger/tools/claims.py    -> refs/heads/claim/<task>
          v                                      v
 ledger/evidence/<run>/  (written by CI only)   Register rows: parity/register/<program>/<domain>.jsonl
          \________________________  ____________/
                                   \/
                parity/tools/validate_register.py   (every push: schema, ids, evidence gate, aliases)
                parity/tools/percent.py             -> ledger/percent.json, ledger/credits.jsonl
                                   |
                ledger/tools/watchdog.py            -> incidents (JSON), ledger/incidents/<id>.md
```

## Layout

| Path | Written by | Content |
|---|---|---|
| `parity/register/<program>/<domain>.jsonl` | research and factory agents | one Register row per line (Appendix C) |
| `parity/schema/row.schema.json` | review role | the row schema (extended, see below) |
| `parity/tools/` | ledger owner | `validate_register.py`, `percent.py`, `regio.py` |
| `ledger/tasks/<task-id>.json` | orchestrator | id, rows[], points (1, 2, 3, 5, 8), deps[], status, branch, retries, handoff note |
| `ledger/heartbeats/<date>/<agent>.jsonl` | the agent, append-only | time, what changed, commit, evidence |
| `ledger/evidence/<run>/` | **CI only** | `run.json`, `<task>.json`, check lines |
| `ledger/percent.json`, `ledger/credits.jsonl` | **CI only** | computed progress, one line per credited row |
| `ledger/incidents/<id>.md` | watchdog, then the owner of the task | stalls, failed gates, re-plans, root cause |
| `ledger/usage/<date>.json` | sessions, audited against billing | usage per task (format not defined here) |
| `ledger/pace.json`, `ledger/legacy.json` | orchestrator | plan 6.4 line; frozen legacy map numbers |
| `ledger/tools/` | ledger owner | `claims.py`, `watchdog.py`, `heartbeat.py`, `guard.py`, `ledgerlib.py` |
| `ledger/examples/` | ledger owner | every format, by example (tests assert the hand count) |

Task status values the tools understand: `open`, `claimed`, `in_progress`, `verify`, `review`,
`merged`, `counted`, `cancelled`. `claims.py claim --tasks ledger/tasks` refuses a task that has
no file or whose status is `merged`, `counted`, `cancelled`, `done`, `verified` or `closed`.
The watchdog reads `retries` (relaunches used so far) from the task file.

## Register rows

The schema is the coordinator's `row.schema.json` plus these optional fields (rows written
against the original schema stay valid):

* `wave` (0 to 4): the wave in which the row is planned to complete (plan 6.2). It is what
  `by_wave` in `percent.json` groups by; rows without it fall in `unassigned`.
* `superiority_tests`: test ids that machine-check the Best tier. A row reaches Best only when
  all of them pass as well.
* `evidence`: `null` until CI counts the row, then an object with `run_id` and `commit`
  (required), and optionally `task`, `tests`, `recorded_at` and `spec_sha`.
* `acceptance[].test_id` must be non-empty, and acceptance entries take no unknown keys.

`spec_sha` is the seal of what the row has to prove: the SHA-256 of the acceptance list, the
superiority tests and the target (`regio.spec_sha`). CI stores it when it credits a row. If a
tolerance, corpus, test list or target changes afterwards, the seal no longer matches and the
credit is void until CI verifies the row again (plan 7.4: a weakened test reverts the row).

Aliases (`alias_of`) merge cosmetic duplicates into one user outcome: they are never counted.

### Validating: `python3 parity/tools/validate_register.py`

Options: `--register DIR`, `--schema FILE`, `--inventory FILE`, `--strict`, `--github`,
`--json`, `--engine auto|jsonschema|builtin`. Exit 0 clean, 1 on an error, 2 on a usage
problem. It uses `jsonschema` when installed and otherwise a built-in checker for the draft-07
keywords the schema uses (it refuses a schema keyword it does not know instead of ignoring it;
a test checks that both engines agree on a battery of good and bad rows).

| Code | Meaning |
|---|---|
| `bad-json`, `not-object` | a line is not a JSON object |
| `schema` | the row breaks the schema |
| `duplicate-id` | an id is defined twice (the message names the first place) |
| `tier-without-evidence` | `tier_now` is Strong or Best and `evidence` has no run id and commit |
| `verified-without-evidence`, `verified-below-target`, `no-acceptance` | `status: verified` needs evidence, a tier that meets the target and at least one acceptance test; a credited tier needs a test |
| `best-without-superiority-test` | `tier_now: Best` needs `superiority_tests` |
| `alias-missing`, `alias-self`, `alias-cycle` | `alias_of` must name another, existing row and must not loop |
| `unknown-test` | with `--inventory`: tests of rows that are in progress, verified or credited must exist |

Warnings (errors with `--strict`): `spec-changed`, `path-mismatch` and `path-layout` (a row
should live in `parity/register/<program>/<domain>.jsonl`), `duplicate-statement`, `blank-line`.

## Evidence

Everything below `ledger/evidence/` is written by CI. A run is a directory named by the run id
(GitHub run ids grow, so the directory name orders the runs; numbers sort numerically).

```
ledger/evidence/43486663063/run.json              {"run_id","sha","kind":"full"|"lite","finished_at"}
ledger/evidence/43486663063/<anything>.jsonl      the check lines of the test runners (below)
ledger/evidence/43486663063/<task-id>.json        {"run_id","task","sha","finished_at","kind","passed":[ids],"failed":[ids],"skipped":[ids],"artifacts":[urls]}
```

The check lines are either **plain**, one per check, `{"suite","test","name","result","ms"}`, or
the **kinded** lines that `dino8_kernel_tests` (`DINO8_TEST_JSONL`) and `run_sections.sh`
(`DINO8_SECTION_JSONL`) write: `run` (the commit and shard of one process), `check`, `test` (one
per test, carrying the result of the whole test and its check count), `summary` and `job`
(`ledger/examples/evidence/43486663001` is a kernel run in this format, `43486663063` is plain).

* `test` is the test id that Register rows name in `acceptance[].test_id` (a kernel test name, an
  app section name, or any name a runner emits). When two suites use the same name, a row can name
  `<suite>/<test>`, for example `kernel/TestCurveLength` or `app/SME`; the bare name sees both.
* All the check lines of one test in one process are one **execution**, folded into one result:
  `ok` only if every check is ok. In the kinded format the `test` line is the verdict and the check
  lines must agree with it (a test line that says ok over a failed check is reported as malformed
  and fails). Check lines without their `test` line mean the process died inside the test: fail.
  A test that ran no check (`checks: 0`) proves nothing and is reported as `empty`.
* `result` is `ok` for a pass (any case); `fail`, `throw`, `skip` and anything else never count.
  Lines of a kind this tool does not know are counted in `evidence.ignored_lines` and ignored.
* The raw check lines can stay CI artifacts (`--checks FILE` reads loose files as the newest run,
  `adhoc`); `run.json` and the per-task JSON are what the repository keeps.
* `kind: full` in `run.json` means the run covers every test, `lite` only the tests of one change.
  Without a `run.json` a run is `lite`, and the deleted-test rule below cannot work.
* A task file must agree with its directory (`run_id`) and its name (`task`).

### When a test counts (the rule of `percent.py`)

The latest run that contains a test decides it. The test is **ok** only if every execution of it in
that run, and in any other run at the same commit, is ok. It does **not** count when it is

* `missing`: no evidence at all;
* `failing`, `empty` or `skipped`;
* `conflicting`: several executions for the same commit or run that disagree (a flaky test, or an
  id used by two suites). A flaky test blocks crediting until a new commit passes cleanly (plan 7.4);
* `vanished`: it ran in an earlier run but is absent from the latest `full` run (deleted or
  disabled after crediting; the row reverts automatically).

A **row passes** when it names at least one acceptance test and every one of them is ok, and its
seal (if it has one) still matches. It is **Best** when its `superiority_tests` are also ok.
`passing` counts rows at Strong or better; `target_met` counts rows that reached their own
target (Best for rows whose target is Best). The headline number is `passing / total`.

## Computing progress: `python3 parity/tools/percent.py`

```
python3 parity/tools/percent.py [--register DIR] [--evidence DIR] [--checks FILE ...]
        [--legacy FILE] [--pace FILE] [--previous FILE] [--out FILE|-] [--credits-out FILE]
        [--now TIME] [--run-id ID] [--commit SHA] [--check] [--strict]
```

`ledger/percent.json` (all keys sorted, output reproducible for equal inputs):

| Key | Content |
|---|---|
| `headline`, `total`, `passing`, `percent`, `best`, `target_met`, `target_met_percent` | the numbers; `total` leaves out alias rows |
| `by_program`, `by_factory`, `by_wave` | the same five numbers per bucket. A row that names two programs counts in both, so program totals can add up to more than `total` |
| `register` | file, row and alias counts; the tiers written in the Register and the tiers computed; rows that pass but are not yet written by CI |
| `not_passing_by_reason` | why the others do not pass: `spec-changed`, `conflicting`, `vanished`, `failing`, `empty`, `skipped`, `missing`, `no-tests` |
| `flaky_tests`, `vanished_tests`, `failing_tests` | count and the first 200 ids (`failing_tests` includes `empty` ones) |
| `most_shared_tests` | tests that more than one row leans on, for the reviewers' oracle check |
| `unproven_claims` | rows whose Register tier is higher than the evidence supports (a typed credit) |
| `reverted` | rows credited at the previous computation (`--previous`, default the credits file) that lost it, with the reason; a credited row that disappeared from the Register is listed as `row-removed` (needs a written rationale, plan 4.4 step 7) |
| `evidence` | runs, files, check lines, distinct tests, `incomplete_tests` (a process died inside a test), `ignored_lines`, a digest, and `malformed` entries (skipped and reported) |
| `legacy` | the frozen legacy map numbers next to the Register number, never added: `kernel_pct`, `app_pct`, `as_of_commit`, `status` (`frozen`, or `placeholder` with nulls when `ledger/legacy.json` is absent) and a ready sentence `dual` |
| `trajectory` | rows required today by the plan 6.4 line, rows passing, `status` wave0, ahead, on_pace, behind or hardening (plan 1.5) |
| `produced_by` | `run_id` and `commit` of the CI run (from `GITHUB_RUN_ID` and `GITHUB_SHA`) |

`ledger/credits.jsonl` has one line per credited row: `id`, `tier`, `run`, `at` (when the
row's tests began their current clean streak, from the run times; the previous time is kept, or
the clock is used, when the evidence has no times) and `tests`. The watchdog counts rows
verified per six-hour window from `at`.

`--check` recomputes and fails (exit 1) when the file on disk differs, ignoring only the clock
and history dependent parts. `--strict` exits 1 when evidence is malformed or the Register
claims a credit that the evidence does not support.

## Claims: `python3 ledger/tools/claims.py`

A claim is the ref `refs/heads/claim/<task-id>`, created with
`git push --force-with-lease=<ref>: <remote> <sha>:<ref>`: the empty lease means "the ref must
not exist", and the remote repeats the check in its own ref transaction, so of any number of
simultaneous claimants exactly one wins (tested with 8 concurrent processes). Every other
operation is a compare-and-swap on the sha that was read. The full protocol is the module
docstring of `claims.py`.

```
claims.py --remote R claim    TASK --agent A [--branch B] [--note N] [--tasks ledger/tasks]
claims.py --remote R renew    TASK --agent A              # a sign of life; fails if the task changed hands
claims.py --remote R release  TASK --agent A [--force]
claims.py --remote R show     TASK [--heartbeats ledger/heartbeats]
claims.py --remote R list     [--heartbeats ledger/heartbeats] [--expiry-minutes 45]
claims.py --remote R takeover TASK --agent A [--heartbeats ...]   # only if the claim expired
claims.py --remote R reap     [--heartbeats ...] [--apply]        # list, or delete, expired claims
```

Exit 0 success, 3 refused because of the claim (held by someone else, still alive, lost),
4 refused because of the task file, 2 usage or git problem; one JSON document on stdout.
A claim expires 45 minutes after its last sign of life: the newest of `claimed_at`, a `renew`,
and a heartbeat of the owner for that task. A takeover writes a new claim commit on top of the
old one with `attempt + 1`, so the history of the ref is the retry count: relaunches = attempt
minus 1, at most two, then the task is split (plan 7.6). The tool builds its objects in a
throw-away bare repository and never touches a ref of the repository it runs in.
`--remote` takes a URL, a path, or a remote name (default `origin`).

## Heartbeats and the watchdog

Heartbeats are written with `ledger/tools/heartbeat.py --agent A --what "..." --evidence "..."`
(commit defaults to `HEAD`). Use the same agent name for `claims.py --agent`, for the heartbeat
file and for the branch owner: a claim is kept alive only by heartbeats of the agent that holds it. One JSON line: `time`, `what`, `commit`, `evidence`; optional
`task`, `metric` (a number that moves when work moves), `state` and `waiting_on`.

`python3 ledger/tools/watchdog.py [--ledger ledger] [--remote R | --claims-file FILE] [--now T]
[--write-incidents ledger/incidents] [--fail-on-incident]` is stateless: run it every 5 to 10
minutes from a scheduled workflow, and a missed run loses nothing. It prints one JSON report
(`agents`, `claims`, `pace`, `incidents`).

| Rule (plan 7.6) | Threshold (option) | Incident | Action |
|---|---|---|---|
| first verifiable output after launch | 10 min after the claim (`--first-output`): a heartbeat with a commit or evidence | `late_first_output` | relaunch |
| heartbeat | none for 15 min (`--heartbeat-timeout`) | `no_heartbeat` | relaunch |
| measurable change | the last 3 heartbeats share task, commit, evidence, metric, state (`--no-change-beats`); the message text is ignored; waiting on a CI run is excused for 40 min (`--ci-wait-grace`) | `no_change` | relaunch |
| claim expiry | 45 min (`--claim-expiry`) | `claim_expired` | takeover, or `reap_claim` if the owner finished |
| commit cadence (plan 7.3) | 30 min (`--commit-interval`) | `commit_overdue` (warn) | none |
| pace | each rolling 6-hour window after wave 0 (starting 18 Nov): rows verified at least 50% of one quarter of that day's required rows | `pace_miss`, with `root_cause_due` two hours later | reallocate to the critical path |
| two consecutive pace misses | plan 11.2 | `replan_trigger` (critical) | replan |

A stalled agent's action is `relaunch`, or `split_task` once two relaunches are used up
(from the claim's attempt count or `retries` in the task file). Incident ids are stable
(`<type>-<subject>-<start>`), so repeated runs report the same incident, and
`--write-incidents` writes `ledger/incidents/<id>.md` only if it does not exist yet. Agents
that end with state `done`, `released`, `handoff` or `idle` are not watched.

The required rows per day come from `ledger/pace.json` (plan 6.4: 10% by 24 Nov, 25% by 30 Nov,
50% by 7 Dec, 80% by 14 Dec, 100% by 18 Dec of the frozen Register; for 8,200 rows 117, 205,
293, 351 and 410 rows per day). Recompute it on freeze day and publish the change.

## Guards: `python3 ledger/tools/guard.py`

`guard.py paths` rejects a push by a non-CI actor that touches `ledger/evidence/`,
`ledger/percent.json` or `ledger/credits.jsonl`. `guard.py rows` rejects a push by a non-CI
actor that raises a row's tier, sets status `verified`, or sets evidence (lowering a credit is
always allowed). The CI actors come from the repository variable `LEDGER_CI_ACTORS`
(comma separated, default `github-actions[bot]`).

## The CI workflow

`.github/workflows/ledger-check.yml` runs on pushes and pull requests that touch `parity/` or
`ledger/`: validation with both engines, a strict dry run of `percent.py` (headline in the job
summary), the two guards, `percent.py --check` when `percent.json` is part of the change, and
all unit tests. It reads the repository and writes nothing.

Run the same checks by hand:

```
python3 parity/tools/validate_register.py --strict
python3 parity/tools/percent.py --strict --out - --credits-out /dev/null
python3 -m unittest discover -s parity/tests -t .
python3 -m unittest discover -s ledger/tests -t .      # (cd ledger && python3 -m unittest) works too
```

## Not done here (the next steps of the ledger)

* The **count step**: a CI job that runs `percent.py` on the integration branch and commits
  `ledger/percent.json`, `ledger/credits.jsonl` and the `tier_now`/`status`/`evidence` of newly
  passing rows (and reverts the lost ones). Today `percent.py` computes and reports; nothing
  writes the Register fields.
* The **evidence writers**: the kernel runner and the app section runner must emit the check
  lines (`suite`, `test`, `name`, `result`, `ms`), and CI must write `run.json` and the task
  files into `ledger/evidence/<run>/`.
* The scheduled **watchdog workflow** (every 5 to 10 minutes, `watchdog.py --remote origin
  --write-incidents ledger/incidents`) and the relaunch of stalled agents from its incidents.
* Verifying that an evidence run id is a real, successful CI run (a call to the Actions API).
* The Register **freeze** guard (30 Nov) that rejects a deleted row without a written rationale.
* Owner prerequisites (plan 8.4): permission to push `claim/*`, `task/*` and ledger branches,
  required status checks and branch protection on the integration branch, and a code owner
  rule on `ledger/evidence/` and `ledger/percent.json`. The guards check, but the repository
  settings are the hard barrier.
