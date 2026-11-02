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
(`tests/smoke.sh`, `TUT_` checks at the end) records one tutorial and checks
file size, frame count, duration, resolution, non-blank frames, valid WebVTT
with increasing cues, and that `index.html` is self-contained.

## What a human still has to do

Nothing here is published. The repository produces files; it does not host
anything.

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

## Known limitations

* Every capture shows the app's own "Saved fr/fNNNN.bmp" toasts in the lower
  right and the capture command in the status bar. Suppressing them needs a
  small change in `src/main.cpp` (the window-capture path calls `Notify`).
* The video is stills per command, with no mouse pointer or animation.
* `MaxViewport` leaves a narrow empty strip beside the maximized viewport in
  the capture; this is how the headless layout currently renders.
* ffmpeg is required. Without it `record.sh` stops with a clear message and
  the `TUT_` smoke checks print a `skip` line instead of failing.
* Verified on Linux with ffmpeg 6.1 only. Windows and macOS were not tried.
