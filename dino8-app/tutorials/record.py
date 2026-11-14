#!/usr/bin/env python3
"""Record one Dino 8 tutorial: run the real app, capture frames, encode.

  record.py --bin build/Dino8 --id 01-first-shapes --out tutorials/out [--format mp4]

Runs `Dino8 --smoke 10 --script <generated>` under Xvfb 1280x720 (when
xvfb-run exists), where the generated script is tutorial.txt with a
ScreenCaptureToFile after every command line (the app's own whole-window
capture, no X screen grabber).  Frames are encoded with ffmpeg at 10 fps,
scaled to 1280x720.  Outputs, in <out>/<id>/:

  <id>.<fmt>   video (mp4 / webm / gif / apng)
  <id>.vtt     WebVTT captions from the #narrate: cues
  <id>.txt     timestamped narration transcript (voice-over script)
  <id>.png     poster frame
  <id>.json    manifest: frames, fps, duration, size, keyframes, cues
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tutlib  # noqa: E402

VF = "scale=1280:720:flags=lanczos,fps=%d" % tutlib.FPS


def die(msg):
    print("record: ERROR: " + msg, file=sys.stderr)
    sys.exit(1)


def encode(fmt, listfile, out, frames):
    # -frames:v pins the count: the concat demuxer emits one extra frame for the repeated last file.
    base = ["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", listfile,
            "-frames:v", str(frames)]
    if fmt == "mp4":
        cmd = base + ["-vf", VF + ",format=yuv420p", "-c:v", "libx264", "-preset", "medium", "-crf", "23",
                      "-movflags", "+faststart", "-an", out]
    elif fmt == "webm":
        cmd = base + ["-vf", VF + ",format=yuv420p", "-c:v", "libvpx-vp9", "-b:v", "0", "-crf", "34", "-an", out]
    elif fmt == "gif":
        cmd = base + ["-vf", VF + ",split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=none", "-loop", "0", out]
    elif fmt == "apng":
        cmd = base + ["-vf", VF, "-plays", "0", "-f", "apng", out]
    else:
        die("unknown format " + fmt)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        die("ffmpeg failed: " + r.stderr[-800:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--id", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--format", default="mp4", choices=["mp4", "webm", "gif", "apng"])
    ap.add_argument("--work", default=None, help="scratch dir (default: a temp dir, removed on success)")
    ap.add_argument("--keep", action="store_true", help="keep the scratch dir (raw BMP frames)")
    a = ap.parse_args()

    src = os.path.join(HERE, a.id, "tutorial.txt")
    if not os.path.isfile(src):
        die("no such tutorial: " + src)
    if not shutil.which("ffmpeg"):
        die("ffmpeg not found (apt-get install ffmpeg)")
    binp = os.path.abspath(a.bin)
    if not os.access(binp, os.X_OK):
        die("app binary not executable: " + binp)

    meta, items = tutlib.parse(src)
    if not meta["title"] or not meta["summary"]:
        die("tutorial.txt needs #title: and #summary:")

    import tempfile
    work = os.path.abspath(a.work) if a.work else tempfile.mkdtemp(prefix="dino8-tut-")
    os.makedirs(os.path.join(work, "fr"), exist_ok=True)
    os.makedirs(os.path.join(work, "files"), exist_ok=True)
    os.makedirs(os.path.join(work, "config"), exist_ok=True)
    for old in glob.glob(os.path.join(work, "fr", "*.bmp")):
        os.remove(old)

    tl = tutlib.build_timeline(items, tmpdir=os.path.join(work, "files"))
    script = os.path.join(work, "run.txt")
    with open(script, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(tl["run_lines"]) + "\n")

    # DINO8_TUTORIAL_CAPTURE: no "Saved ... .bmp" toast in the captured frames, and a pointer arrow
    # drawn at the scripted @move/@click position (see main.cpp, tutorial_capture).
    env = dict(os.environ, XDG_CONFIG_HOME=os.path.join(work, "config"), DINO8_TUTORIAL_CAPTURE="1")
    cmd = [binp, "--smoke", "10", "--script", script]
    if shutil.which("xvfb-run"):
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24"] + cmd
    try:
        r = subprocess.run(cmd, cwd=work, env=env, capture_output=True, text=True, timeout=900)
    except subprocess.TimeoutExpired:
        die("app timed out (900 s)")
    with open(os.path.join(work, "app.log"), "w", encoding="utf-8") as f:
        f.write(r.stdout + r.stderr)
    bad = [l for l in (r.stdout + r.stderr).splitlines() if "FAIL" in l or "expect_" in l and "expected" in l]
    if r.returncode != 0 or bad:
        print((r.stdout + r.stderr)[-3000:], file=sys.stderr)
        die("app run failed (exit %d)%s" % (r.returncode, ": " + bad[0] if bad else ""))

    missing = [k["file"] for k in tl["keyframes"] if not os.path.isfile(os.path.join(work, k["file"]))]
    if missing:
        print((r.stdout + r.stderr)[-2000:], file=sys.stderr)
        die("app did not write %d frame(s), first: %s" % (len(missing), missing[0]))

    # concat list: every keyframe held for its hold time
    listfile = os.path.join(work, "frames.txt")
    with open(listfile, "w", encoding="utf-8", newline="\n") as f:
        for k in tl["keyframes"]:
            f.write("file '%s'\nduration %.1f\n" % (os.path.join(work, k["file"]), k["hold"]))
        f.write("file '%s'\n" % os.path.join(work, tl["keyframes"][-1]["file"]))

    outdir = os.path.join(os.path.abspath(a.out), a.id)
    os.makedirs(outdir, exist_ok=True)
    vid = os.path.join(outdir, "%s.%s" % (a.id, a.format))
    encode(a.format, listfile, vid, tl["frames"])

    # poster: the frame ~60% through, scaled like the video
    poster = os.path.join(outdir, a.id + ".png")
    pk = tl["keyframes"][min(len(tl["keyframes"]) - 1, int(len(tl["keyframes"]) * 0.6))]
    subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", os.path.join(work, pk["file"]),
                    "-vf", "scale=1280:720:flags=lanczos", poster], check=True)

    tutlib.write_vtt(os.path.join(outdir, a.id + ".vtt"), tl["cues"])
    tutlib.write_transcript(os.path.join(outdir, a.id + ".txt"), meta, tl["cues"])
    manifest = {"id": a.id, "title": meta["title"], "summary": meta["summary"], "level": meta["level"],
                "format": a.format, "video": os.path.basename(vid), "fps": tutlib.FPS,
                "width": 1280, "height": 720, "frames": tl["frames"], "duration": tl["duration"],
                "keyframes": [{"start": k["start"], "hold": k["hold"], "texts": k["texts"]} for k in tl["keyframes"]],
                "cues": tl["cues"], "size_bytes": os.path.getsize(vid)}
    with open(os.path.join(outdir, a.id + ".json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")
    print("record: %s -> %s (%d bytes, %d frames, %.1f s, %d cues)" % (
        a.id, vid, manifest["size_bytes"], tl["frames"], tl["duration"], len(tl["cues"])))
    if a.keep or a.work:
        print("record: scratch kept in " + work)
    else:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
