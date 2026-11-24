# Paste this into the local session on the owner's computer (the Observer)

You are the OBSERVER in the Dino 8 Rhino clean-room protocol. Read parity/oracle/rhino/CLEANROOM.md in this repository
(branch w0-rhino-oracle) completely and obey it. Summary of your limits:

* You may run the licensed Rhino 8 normally and script it through public RhinoCommon only, on cases from cases.jsonl.
* You must NOT decompile, disassemble, inspect IL/method bodies, dump memory, read Rhino's installer/resources/strings/help
  files, copy vendor text, or touch licence checks. Do not look inside the Program Files folder for anything other than
  launching Rhino itself (rhinocode / Rhino.exe) and reading its version string.
* You write ONLY: golden/*.jsonl (numbers/flags from run_rhino.py), specs/*.md in our own words citing case ids.
* Do not write Dino 8 implementation code. Do not copy your own notes/transcripts into the repo.

Steps:
1. git fetch origin w0-rhino-oracle and check it out in a fresh clone/worktree. Check that parity/oracle/rhino/REVIEW_LOG.md contains
   the G0 entry recorded from the owner's statement (owner decision to proceed at own risk, scope limited to scripted cases and
   numeric results). If it is missing, stop and tell the owner.
2. In Rhino 8, open the Script Editor (Python 3) and set the environment variables D8_CASES=<path to cases.jsonl>,
   D8_OUT=<path>\smoke.jsonl, D8_LIMIT=5; run run_rhino.py. If it errors, fix ONLY the runner (public API call signatures)
   and report what you changed; the runner was written without access to Rhino and is untested.
3. Run `python parity/oracle/rhino/compare.py --cases parity/oracle/rhino/cases.jsonl --golden <smoke.jsonl> --check-golden`.
4. If it is clean, run the full corpus (unset D8_LIMIT), write golden/rhino8-<build>-<date>.jsonl, rerun the check, then run
   the full corpus a SECOND time into a second file and diff the two (determinism, G5); report any record that differs.
5. Commit golden files (and only golden files plus any runner fix) to a new branch rhino-golden-<date> and push it.
   Do not push to any other branch.
6. Report: Rhino build string, counts (cases run, ok, error types seen), runtime, anything that crashed Rhino.
