# Batch scripting in Dino 8

`Dino8 --script FILE` (given **without** `--smoke`) is a supported,
standalone batch/automation mode: it runs each line of `FILE` as a command
once, then the process exits on its own - no window is ever shown, and no
human has to close one. This is separate from `--smoke N --script FILE`,
the pre-existing headless QC combination used by `tests/smoke.sh` (that one
also renders and counts frames, and is meant for this repo's own CI, not
end users).

## What it does

```
Dino8 model.3dm --script build.txt
```

- Opens `model.3dm` (if given) the same as an interactive launch would.
- Reads `build.txt` and runs each non-`#`, non-empty line as one command,
  exactly as if typed on the command line (`Box 0,0,0 5,5,5`, `SaveAs
  out.3dm`, ...). Lines starting with `@` are the synthetic-UI-input tokens
  documented at the top of `src/main.cpp` (`@move`, `@click`, `@wait`, ...) -
  a plain batch script normally has none of these and just lists commands.
- Exits the moment the script finishes (exit code 0, or 2 if any
  `@expect_selected`/`@expect_objects` check in the script failed), instead
  of falling into the normal interactive loop.
- Runs with its window created but hidden (`GLFW_VISIBLE` off) and treated
  as headless the same way `--smoke` is: `ShowFileDialog`'s real OS file
  picker and the unsaved-changes-confirm prompt are both skipped, since
  there is no user present to click either - which is also why a batch
  script has to pass paths explicitly (`SaveAs out.3dm`, not bare `SaveAs`
  followed by an interactive file dialog).
- Does **not** touch or load the interactive session's saved window layout
  (`layout.ini`) - a hidden batch run has no real layout, and must not
  clobber the one the user has saved for their own interactive sessions.

Every command line's result is still printed to stdout the instant it
happens (`history: ...`), flushed line by line - the same
crash-diagnosis-friendly behavior `--smoke` QC runs already rely on - so a
batch job's log always shows exactly which command was running if
something crashes partway through.

## Still needs a real (or virtual) display

This does **not** eliminate the underlying GL-context requirement: Dino 8's
rendering, picking and viewport framing are built on GLFW/OpenGL, so the
process still needs a display to hand back a context - a real one, or a
virtual one. On Linux (servers, CI, containers with no monitor) that means
running under Xvfb with the llvmpipe software rasterizer, exactly like
`tests/smoke.sh` already does for its own headless QC runs:

```
xvfb-run -a -s "-screen 0 1600x900x24" Dino8 model.3dm --script build.txt
```

This is the same "acceptable headless story" this project already relies
on for CI; nothing about batch scripting removes that requirement, and
nothing here claims otherwise.

## Example: a real batch geometry job

```
# build.txt
Box 0,0,0 5,5,5
Sphere 2.5,2.5,2.5 1.5
BooleanDifference
SaveAs result.3dm
```

```
xvfb-run -a Dino8 --script build.txt
echo "exit code: $?"   # 0 on success, 2 if an @expect_* check in the
                        # script failed, matching the pre-existing --smoke
                        # convention this batch mode shares its exit-code
                        # semantics with
```

`result.3dm` is written by the real `SaveAs` command, the same one the
interactive UI's File > Save As menu item runs - there is no separate
"export API" batch scripts have to use instead.

## Relationship to `rs.*`/`dino8.*` scripting

Lines can also be `RunScript`/`RunPythonScript`, so a batch job can drive
the full Lua (`rs.*`) or Python (`dino8.*`) object-model API documented in
`tests/script_script.txt`/`tests/python_script.txt` instead of (or in
addition to) plain command lines - the same object model this batch mode
itself runs on top of.
