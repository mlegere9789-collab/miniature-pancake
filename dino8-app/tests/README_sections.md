# App smoke tests: `tests/sections/` and `run_sections.sh`

`tests/smoke.sh` is one ~12,600-line script (15-40 minutes). Everybody appending a section to its end conflicts
with everybody else, and verifying one feature means running everything.

## Adding a section (no edit to smoke.sh)

Create `dino8-app/tests/sections/<NN_>name.sh` (for example `sections/60_widgets.sh`). `smoke.sh` sources every
`sections/*.sh` in C-locale sorted order just before its final `exit $fail`, **into the same shell**, so the file sees
all harness variables and helpers: `BIN`, `HERE`, `HEREW`, `TMP`, `TMPW`, `CONFIG_DIR`, `IS_WINDOWS`, `OUT`,
`check`, `near`, and sets `fail=1` on failure exactly like an inline section. It must not `exit`; do not rely on
`set +e` (the script runs under `set -euo pipefail`). Print `ok   <what>` / `FAIL <what>` lines as the inline sections
do. Define your helper functions inside your own file with a unique prefix. Use `$TMPW/<your-prefix>_*` for scratch
files. Launch the app the way the existing sections do (`xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke N
--script ...`, with the `DISPLAY` / `xset q` branch if you copy a neighbour).

A section file is selected by its name without the number prefix: `sections/60_widgets.sh` is `widgets`.

## Running a few sections: `run_sections.sh`

```
dino8-app/tests/run_sections.sh [-b BIN] [-j N] [-t SECS] [-k] [-l] NAME-OR-PREFIX...
dino8-app/tests/run_sections.sh -l                       # list sections and their line ranges
dino8-app/tests/run_sections.sh -b build/Dino8 KWR       # one inline section of smoke.sh
dino8-app/tests/run_sections.sh -j2 -b build/Dino8 KWR HSO FRH widgets
```

* Inline sections of `smoke.sh` are found by their header comment: a line `# PREFIX: ...` (2-6 capitals/digits,
  optional trailing `_`) after a blank line or a `# ----` rule. A section runs to the line before the next header.
  Names are case-insensitive; an exact match wins, otherwise every section whose name starts with the argument is
  selected (`TS` selects TS2..TS5). `base` is the un-headed part between the harness and the first header. Nothing in
  `smoke.sh` is moved or renamed; the table is derived from it each time (`-l`).
* The runner assembles: smoke.sh lines 1 up to the `check() {` definition (the harness, including its initial
  `--smoke` run), then the selected section text verbatim (section files are `.`-sourced), then `exit $fail`. It
  runs that with `trap '' PIPE` (SIGPIPE ignored, as on the CI runner) under `timeout` (`-t`, default 3000 s).
* `-j N` (default 1): each selected section becomes its own job with its own temp directory (`TMPDIR`, and so
  smoke.sh's `mktemp -d`) and its own persistent Xvfb display; the `xvfb-run -a ...` calls inside the section are
  redirected (PATH shim) to that display, so parallel jobs never race for a display number. Without `-j` all
  selected sections run in one script, in order. Results are merged: each job's log, then a summary table
  (exit code, seconds, ok and FAIL counts), then a total. Exit status is nonzero if any job exits nonzero or prints
  a `FAIL` line.
* The machine has few cores: `-j2` is the sensible maximum; run it through the shared gate if one exists.

### Limits

* A section that uses a helper function or variable defined in a different section is not self-contained. Run
  alone it fails with `command not found`; the summary flags that. Name the defining section as well (they then run
  in one job only without `-j`), or move the helper into the section.
* Big early parts of smoke.sh are not split into headed sections: `SSAO` is 3,000 lines (it contains many
  un-headed sub-sections), `BASE` is 5,800. Selecting them runs the whole range.
* Sections under `-j` see a persistent Xvfb for the whole job instead of a fresh one per app launch (as the full
  script gets from `xvfb-run -a`); in measured runs this made no difference to results, but it is a difference.
* Each job repeats the harness's initial app launch (a few seconds).
