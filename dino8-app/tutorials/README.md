# Dino 8 tutorials

Ten scripted tutorials (`NN-name/tutorial.txt`) and the pipeline that turns each
into a video with captions: `record.sh` / `record.py` (run the real app under
Xvfb, capture a frame after each command, encode with ffmpeg), `verify.py`
(checks a recorded tutorial from its files), `build-site.sh` (static
`index.html`). Full instructions: `../docs/TUTORIALS.md`.

## What the output is

* **Silent, stills per command.** One 1280x720 frame per command, held a few
  seconds, 10 fps, with a WebVTT caption file and a voice-over script (`.txt`).
  No narration, no audio, no mouse-travel animation. A pointer arrow is drawn at
  the position a script's `@move` / `@click` set; the "Saved ... .bmp" toast is
  suppressed in captured frames (`DINO8_TUTORIAL_CAPTURE=1`, set by `record.py`).
* **Not committed, not published.** `out/` is git-ignored. No video is stored in
  the repository and nothing hosts them; there is no community forum.
* **CI artifact.** The Linux job of `.github/workflows/dino8-app.yml` records all
  ten, probes each with ffprobe (non-zero duration and frames) and uploads them
  as the workflow artifact `Dino8-tutorial-videos` (14 days retention). That is
  evidence the pipeline works, not a published tutorial library.
