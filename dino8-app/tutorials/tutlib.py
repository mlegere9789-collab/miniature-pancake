"""Shared parsing / timeline logic for Dino 8 tutorials (stdlib only).

A tutorial is dino8-app/tutorials/<id>/tutorial.txt: the app's own --script
lines interleaved with directive comments.  The app's script runner skips
every line that starts with '#' (src/main.cpp), so the same file also runs
unmodified under `Dino8 --script`; the directives below are read only by
the recorder (record.py) and the site builder (build_site.py).

  #title: Text        tutorial title                       (required)
  #summary: Text      one-sentence description             (required)
  #level: Text        e.g. Beginner / Intermediate         (optional)
  #narrate: Text      a narration cue.  Shown as a caption and listed in
                      the transcript; attaches to the NEXT captured frame
                      (or the final frame if nothing follows).
  #hold: SECONDS      extra seconds to keep the next captured frame on screen
  #shot               force a capture after the previous '@' line (normally
                      only plain command lines are captured)

A capture is itself a script line, so it is only inserted where the previous
command has finished: after a line whose NEXT command line starts a new
command (a prompt answer such as `0,0,0`, `Radius=3` or `Enter` is joined
to the command before it and the frame is taken after the last answer).
  #cont               the next command line answers a prompt of the previous
                      command (e.g. a `SelID 2` that picks the cutter after
                      BooleanDifference): no frame is captured in between
  any other '#...'    ordinary comment, ignored by everything
  @TMP@               replaced by a scratch directory in the command lines

Every plain command line (anything not starting with '#' or '@') is run and
followed by one frame capture of the whole application window.  A capture
is also taken before the first line (the empty app) and after the last one.
All durations are multiples of 0.1 s and the video is 10 fps, so the frame
count of the finished video is exactly round(10 * total duration).
"""
import re
import textwrap

FPS = 10
DEFAULT_HOLD = 0.8          # seconds a captured frame stays up with no narration
CUE_PAD = 0.3               # silence after each narrated frame
MIN_CUE = 2.0
WORDS_PER_SEC = 2.6         # speaking rate used to size a cue
INTRO_HOLD = 1.0
OUTRO_HOLD = 1.5


def r1(x):
    """Round to 0.1 s (the frame grid)."""
    return round(x * 10 + 1e-9) / 10.0


def cue_duration(text):
    words = len(text.split())
    return r1(max(MIN_CUE, 0.4 + words / WORDS_PER_SEC))


def parse(path):
    """Return (meta, lines) where lines is a list of (kind, payload)."""
    meta = {"title": "", "summary": "", "level": "Beginner"}
    items = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if not line.strip():
                continue
            m = re.match(r"#(title|summary|level):\s*(.*)$", line)
            if m:
                meta[m.group(1)] = m.group(2).strip()
                continue
            m = re.match(r"#narrate:\s*(.*)$", line)
            if m:
                items.append(("narrate", m.group(1).strip()))
                continue
            m = re.match(r"#hold:\s*([0-9.]+)\s*$", line)
            if m:
                items.append(("hold", float(m.group(1))))
                continue
            if re.match(r"#cont\s*$", line):
                items.append(("cont", None))
                continue
            if re.match(r"#shot\s*$", line):
                items.append(("shot", None))
                continue
            if line.startswith("#"):
                continue
            items.append(("cmd", line))
    return meta, items


INPUT_DIRECTIVES = {"@click", "@clickworld", "@key", "@text", "@drag", "@dragworld", "@down", "@up", "@move", "@world"}


def is_command_start(line):
    """True if a script line begins a new command (not a prompt answer).

    Prompt answers are points (0,0,0), numbers, Key=Value options and the
    bare Enter/Escape lines; command names start with a letter and have no
    '=' in their first word.
    """
    first = line.split(None, 1)[0]
    if first in ("Enter", "Escape"):
        return False
    if "=" in first or not re.match(r"[A-Za-z]", first):
        return False
    return True


def build_timeline(items, tmpdir="@TMP@"):
    """Turn parsed items into a run script + keyframe timeline.

    Returns dict(run_lines, keyframes, cues, duration, frames).
    keyframe = dict(file, start, hold, texts)
    """
    run = []
    keyframes = []
    pend_text = []      # narration waiting for the next capture
    pend_hold = 0.0

    def capture(label_hold_default):
        nonlocal pend_text, pend_hold
        idx = len(keyframes)
        fname = "fr/f%04d.bmp" % idx
        # Let the app finish the command (mesh/display update) before the grab.
        run.extend(["@wait 3", "ScreenCaptureToFile " + fname, "@wait 4"])
        narr = sum(cue_duration(t) for t in pend_text)
        hold = narr + (CUE_PAD if pend_text else 0.0)
        hold = max(hold, pend_hold, label_hold_default)
        keyframes.append({"file": fname, "hold": r1(hold), "texts": list(pend_text)})
        pend_text = []
        pend_hold = 0.0

    # Opening frame: the empty application (intro narration lands here).
    first_cmd = next((i for i, (k, _) in enumerate(items) if k == "cmd"), len(items))
    for i, (k, v) in enumerate(items[:first_cmd]):
        if k == "narrate":
            pend_text.append(v)
        elif k == "hold":
            pend_hold = max(pend_hold, v)
    capture(INTRO_HOLD)
    rest = items[first_cmd:]
    last_was_at = False
    cont_next = set()
    for pos, (kind, val) in enumerate(rest):
        if kind == "cont":
            nx = next((j for j in range(pos + 1, len(rest)) if rest[j][0] == "cmd" and not rest[j][1].startswith("@")), None)
            if nx is not None:
                cont_next.add(nx)
    for pos, (kind, val) in enumerate(rest):
        if kind == "narrate":
            pend_text.append(val)
        elif kind == "hold":
            pend_hold = max(pend_hold, val)
        elif kind == "shot":
            if last_was_at:
                capture(DEFAULT_HOLD)
        elif kind == "cmd":
            run.append(val.replace("@TMP@", tmpdir))
            if val.startswith("@"):
                last_was_at = True
                continue
            last_was_at = False
            nxt = None
            for j2, (k2, v2) in enumerate(rest[pos + 1:], pos + 1):
                if k2 != "cmd":
                    continue
                if v2.startswith("@"):
                    if v2.split(None, 1)[0] in INPUT_DIRECTIVES:
                        nxt = v2   # synthetic mouse/keyboard input: answers the prompt
                        break
                    continue
                nxt = v2
                if j2 in cont_next:
                    nxt = "@key"   # explicit prompt answer
                break
            # A capture is itself a script line: if the command is still
            # waiting for input (nxt answers its prompt) it would be eaten.
            if nxt is None or (not nxt.startswith("@") and is_command_start(nxt)):
                capture(DEFAULT_HOLD)
    # Closing frame (outro narration lands here).
    capture(OUTRO_HOLD)
    run.append("@wait 8")

    t = 0.0
    cues = []
    for kf in keyframes:
        kf["start"] = r1(t)
        ct = t
        for txt in kf["texts"]:
            d = cue_duration(txt)
            cues.append({"start": r1(ct), "end": r1(ct + d), "text": txt})
            ct += d
        t += kf["hold"]
    duration = r1(t)
    frames = sum(int(round(kf["hold"] * FPS)) for kf in keyframes)
    return {"run_lines": run, "keyframes": keyframes, "cues": cues,
            "duration": duration, "frames": frames}


def vtt_time(s):
    ms = int(round(s * 1000))
    h, ms = divmod(ms, 3600000)
    m, ms = divmod(ms, 60000)
    sec, ms = divmod(ms, 1000)
    return "%02d:%02d:%02d.%03d" % (h, m, sec, ms)


def write_vtt(path, cues):
    out = ["WEBVTT", ""]
    for i, c in enumerate(cues, 1):
        out.append(str(i))
        out.append("%s --> %s" % (vtt_time(c["start"]), vtt_time(c["end"])))
        out.append(textwrap.fill(c["text"], 44))
        out.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


def write_transcript(path, meta, cues):
    out = [meta["title"], "=" * len(meta["title"]), ""]
    for c in cues:
        out.append("[%s - %s] %s" % (vtt_time(c["start"])[3:-4], vtt_time(c["end"])[3:-4], c["text"]))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
