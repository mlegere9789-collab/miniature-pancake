# Video tutorials: authoring and publishing

Every Dino 8 tutorial video is generated from the real app by a script, so it
is reproducible, diffable and testable. This file explains how to record the
existing ten, how to write a new one, and what still needs a human before
anything is public.

## What exists

`dino8-app/tutorials/` holds:

| Path | What it is |
| --- | --- |
| `NN-name/tutorial.txt` | One tutorial: the app's own `--script` lines plus narration cues |
| `record.sh`, `record.py` | Runs the app under Xvfb (1280x720 screen), drives the script, captures frames, encodes the video, writes captions |
| `tutlib.py` | Parser and timeline shared by everything (single source of timing truth) |
| `verify.py` | Checks a recorded tutorial from its files (used by `tests/smoke.sh`) |
| `build-site.sh`, `build_site.py` | Generates `tutorials/index.html`, a static page (no CDN, no scripts, no trackers) |
| `out/` | Generated videos, captions, transcripts, posters (git-ignored) |

The ten tutorials: first shapes; selection and snaps; layers; extrude and
revolve a vase; booleans on a bracket; fillets and chamfers; loft and sweep;
SubD basics; dimensions and layouts; exporting and printing. Several extend
the scripts in `docs/tutorial_scripts/` and `tests/*_script.txt` that the smoke
suite already runs.

## Recording

Needs `python3`, `ffmpeg` (with libx264), `xvfb-run`, and a built `Dino8`:

```sh
dino8-app/tutorials/record.sh -b dino8-app/build/Dino8            # all ten
dino8-app/tutorials/record.sh -b dino8-app/build/Dino8 03-layers   # one
dino8-app/tutorials/record.sh -b ... -f webm                       # mp4 (default), webm, gif or apng
dino8-app/tutorials/build-site.sh                                  # regenerate index.html
```

Output per tutorial, in `tutorials/out/<id>/`: `<id>.mp4`, `<id>.vtt` (WebVTT
captions), `<id>.txt` (timestamped voice-over script), `<id>.png` (poster) and
`<id>.json` (frame count, duration, cue list). Videos are 1280x720, 10 fps,
silent. Recording all ten takes about three and a half minutes, mostly
encoding.

How frames are made: `record.py` turns `tutorial.txt` into a plain app script
that inserts `ScreenCaptureToFile fr/fNNNN.bmp` after each command (the app's
own whole-window capture, so no X screen grabber is involved), runs
`Dino8 --smoke 10 --script ...`, then has ffmpeg hold each captured frame for
its hold time and scale it to 1280x720 (the app lays out its hidden window at
1600x900 whatever the X screen size). The video is therefore a sequence of
"state after each command" stills, not a screen recording of mouse movement.

## Writing a tutorial

Create `tutorials/NN-short-name/tutorial.txt`. It is an ordinary app script:
every line is a command exactly as you would type it in the command line, or
an `@` test directive (`@expect_objects 2`, `@clickworld Top 10 5 0`,
`@snap end 1`, `@key Enter`, `@wait 2`; see the header of `src/main.cpp`). The
app skips every line that starts with `#`, so the file also runs unmodified
with `Dino8 --smoke 200 --script tutorial.txt`. Directives for the recorder
are `#` lines:

```
#title: Layers                       (required)
#summary: One sentence.              (required)
#level: Beginner                     (optional)
#narrate: What the voice says at this point. It becomes the caption and the transcript line.
Box 0,0,0 10,10,0 5                  <- a frame is captured after this command
#hold: 3                             (keep the next frame up 3 s longer)
#cont                                (next line answers the previous command's prompt)
#shot                                (also capture after the previous @ line)
```

Rules that matter:

* A `#narrate:` line attaches to the next captured frame (or the last frame).
  Its caption lasts `max(2 s, 0.4 s + words / 2.6)`, which is the time the
  frame stays on screen. Write what you would say aloud, one idea per cue.
* A capture is itself a script line. If a command is still waiting for input,
  the capture would be eaten by its prompt. The recorder therefore only
  captures after a command whose next line starts a new command. Point lines
  (`0,0,0`), numbers, `Key=Value` options, `Enter`, `Escape` and `@click`/`@key`
  lines count as answers. A prompt answer that looks like a command, such as
  `SelID 2` picking the cutter after `BooleanDifference`, needs `#cont` on the
  line before it. Forgetting this fails loudly ("app did not write N frame(s)").
* Typed coordinates resolve against the active viewport, so switch with
  `SetActiveViewport` first. `MaxViewport` then `ZoomExtents` makes the model
  fill the screen. Draw 2D profiles in Top, then look at them in Perspective.
* Use `@expect_objects` / `@expect_selected` freely. The recorder refuses to
  produce a video if any expectation fails or the app exits non-zero, so a
  tutorial cannot silently drift away from what the app really does.
* Use `@TMP@` for output files; it is replaced by a scratch directory.
* Run the new tutorial through `record.sh`, then look at a few frames in
  `/tmp` (set `--keep` or `--work DIR` on `record.py` to keep the BMPs).

After recording, `build-site.sh` regenerates `index.html`. The smoke suite
(`tests/smoke.sh`, `TUT_` and `TUTV_` checks) records one tutorial (`TUT_`) and checks
file size, frame count, duration, resolution, non-blank frames, valid WebVTT
with increasing cues, and that `index.html` is self-contained.

## What a human still has to do

Nothing here is published. The repository produces files (and CI keeps them as a
14-day workflow artifact, see above); it does not host anything.

1. **Narration voice-over.** The videos are silent. `out/<id>/<id>.txt` is the
   script with timestamps. A person or an approved text-to-speech voice has to
   record it, and the audio has to be muxed in (for example
   `ffmpeg -i video.mp4 -i voice.wav -c:v copy -c:a aac out.mp4`). The captions
   are timed to the on-screen frames; real speech will not match the estimated
   2.6 words per second exactly, so expect to adjust `#hold:` or the cue text
   and re-record.
2. **Hosting.** Videos are not committed (about 0.7 to 1.1 MB per tutorial
   at this length, ten in total under 10 MB, but they are build outputs).
   Someone must choose a host (a static web server, object storage, a video
   platform), upload `tutorials/index.html` together with `tutorials/out/`,
   keep relative paths intact, and decide who pays for and maintains it.
3. **Review.** A human should watch each video once. The recorder checks that
   frames are not blank and that the commands succeeded, not that the
   explanation is good.
4. **Community forum.** Not addressed by this pipeline. A forum needs an
   audience, moderation, accounts and ongoing hosting.

## What CI produces (and what it does not)

The Linux job of `.github/workflows/dino8-app.yml` installs ffmpeg, then
`tests/smoke.sh` runs with `DINO8_REQUIRE_TUTORIALS=1` (a missing ffmpeg is a
FAIL, not a skip) and `DINO8_TUTORIAL_VIDEO_DIR` set. That makes two blocks run:

* `TUT_`: records tutorial 04 into a scratch directory and verifies it from the
  files (size, frame count, duration, resolution, non-blank frames, WebVTT).
* `TUTV_`: records **all ten** tutorials into `tutorial-videos/` and runs
  `ffprobe` on each mp4, failing unless it has a non-zero duration and frame
  count. A workflow step then greps the smoke log to prove neither block was
  skipped.

The workflow uploads `tutorial-videos/` (mp4, vtt, txt, png, json per tutorial)
plus `smoke.log` as the workflow artifact **Dino8-tutorial-videos**, kept for
14 days. What that artifact is, honestly:

* **Silent.** No narration, no audio track. The `.txt` is a voice-over script
  nobody has recorded.
* **Stills per command.** Each video is the app's state after each command held
  for a few seconds, 10 fps, 1280x720, with a WebVTT caption file. It is not a
  screen recording of mouse movement or animation.
* **Not published.** It is a build artifact on the Actions run page, visible to
  people with access to the repository for 14 days. No video is committed, no
  site or video platform hosts it, and there is no community forum.
* **Unreviewed.** CI proves the videos exist, decode, are not blank and have the
  expected length. It does not prove they explain anything well.

Until a human has recorded narration, chosen a host and reviewed the videos,
the "video tutorials / community forum" capability is a working generation
pipeline with CI evidence, not a published tutorial library.

## Capture quality

* **No "Saved fr/fNNNN.bmp" toasts.** `record.py` sets `DINO8_TUTORIAL_CAPTURE=1`
  for the app; with it set `src/main.cpp` records the "Saved ..." line in the
  command history only and does not raise the toast, so the captured frames no
  longer carry one. Without the variable (every normal run) nothing changes.
* **Mouse pointer.** With the same variable set, a frame captured after a
  scripted `@move` / `@click` / `@clickworld` / `@drag` has a pointer arrow
  drawn at the logical pointer position the script last set (the tip is the
  position). It is drawn into the frame by the app, because Xvfb has no visible
  cursor. A tutorial that never moves the pointer shows none. It is a pointer
  marker at the click position, not an animation of the cursor travelling there.

## Known limitations

* The capture command is still echoed in the command line / status bar of each
  frame (the command-history line "Saved fr/fNNNN.bmp" remains in the history
  panel when it is open).
* The video is stills per command; there is no animation between them.
* `MaxViewport` leaves a narrow empty strip beside the maximized viewport in
  the capture; this is how the headless layout currently renders.
* ffmpeg is required. Without it `record.sh` stops with a clear message and
  the `TUT_` smoke checks print a `skip` line instead of failing (CI sets
  `DINO8_REQUIRE_TUTORIALS=1`, which turns that skip into a FAIL).
* Verified on Linux only (ffmpeg 6.1 locally; CI uses the Ubuntu 22.04 package).
  Windows and macOS were not tried.
