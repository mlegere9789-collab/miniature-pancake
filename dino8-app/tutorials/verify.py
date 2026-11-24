#!/usr/bin/env python3
"""Verify a recorded tutorial (stdlib + ffmpeg/ffprobe).  Used by tests/smoke.sh.

  verify.py OUTDIR ID        check <OUTDIR>/<ID>/ and print `TUT_key=value` lines
  verify.py --selftest       prove the validators reject bad input

Facts printed (one per line, all derived from the files, not from the
recorder's own claims, except TUT_manifest_*):
  TUT_video_bytes, TUT_probe_frames, TUT_expected_frames, TUT_probe_duration,
  TUT_expected_duration, TUT_probe_size, TUT_probe_fps, TUT_sampled_frames,
  TUT_blank_frames, TUT_min_variance, TUT_first_last_diff,
  TUT_vtt (ok|bad:<reason>), TUT_vtt_cues, TUT_manifest_frames
"""
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tutlib  # noqa: E402

BLANK_VARIANCE = 4.0   # luminance variance (0..255 scale) below which a frame counts as blank


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not m:
        raise ValueError("not a P6 PPM: " + path)
    w, h = int(m.group(1)), int(m.group(2))
    return w, h, data[m.end():m.end() + w * h * 3]


def luma_variance(rgb):
    """Variance of per-pixel luminance of packed RGB bytes (stdlib only)."""
    n = len(rgb) // 3
    if n == 0:
        return 0.0
    r, g, b = rgb[0::3], rgb[1::3], rgb[2::3]
    s = s2 = 0
    for i in range(n):
        y = (299 * r[i] + 587 * g[i] + 114 * b[i]) // 1000
        s += y
        s2 += y * y
    mean = s / n
    return s2 / n - mean * mean


def check_vtt(path, duration):
    """Return (ok, reason, cue_count).  Valid WebVTT, cues strictly increasing."""
    try:
        text = open(path, encoding="utf-8").read()
    except OSError as e:
        return False, "unreadable: %s" % e, 0
    lines = text.replace("\r\n", "\n").split("\n")
    if not lines or not lines[0].startswith("WEBVTT"):
        return False, "missing WEBVTT header", 0
    stamp = r"(\d{2,}):([0-5]\d):([0-5]\d)\.(\d{3})"
    rx = re.compile(r"^%s --> %s(\s.*)?$" % (stamp, stamp))
    cues = []
    i = 1
    while i < len(lines):
        m = rx.match(lines[i])
        if m:
            g = m.groups()
            a = int(g[0]) * 3600 + int(g[1]) * 60 + int(g[2]) + int(g[3]) / 1000.0
            b = int(g[4]) * 3600 + int(g[5]) * 60 + int(g[6]) + int(g[7]) / 1000.0
            j = i + 1
            body = []
            while j < len(lines) and lines[j].strip():
                body.append(lines[j])
                j += 1
            if not body:
                return False, "cue %d has no text" % (len(cues) + 1), len(cues)
            cues.append((a, b))
            i = j
        else:
            i += 1
    if not cues:
        return False, "no cues", 0
    prev_start = -1.0
    prev_end = 0.0
    for n, (a, b) in enumerate(cues, 1):
        if b <= a:
            return False, "cue %d ends before it starts" % n, len(cues)
        if a <= prev_start:
            return False, "cue %d start not after cue %d start" % (n, n - 1), len(cues)
        if a < prev_end - 1e-6:
            return False, "cue %d overlaps cue %d" % (n, n - 1), len(cues)
        prev_start, prev_end = a, b
    if duration and prev_end > duration + 0.06:
        return False, "last cue ends at %.1f after the video (%.1f s)" % (prev_end, duration), len(cues)
    return True, "ok", len(cues)


def selftest():
    flat = bytes([40, 40, 40]) * 5000
    noisy = bytes((i * 37) % 256 for i in range(15000))
    print("TUT_selftest_flat_blank=%d" % (luma_variance(flat) < BLANK_VARIANCE))
    print("TUT_selftest_noisy_not_blank=%d" % (luma_variance(noisy) >= BLANK_VARIANCE))
    d = tempfile.mkdtemp(prefix="tutvtt-")
    good = os.path.join(d, "good.vtt")
    bad = os.path.join(d, "bad.vtt")
    with open(good, "w") as f:
        f.write("WEBVTT\n\n1\n00:00:00.000 --> 00:00:02.000\nhello\n\n2\n00:00:02.500 --> 00:00:04.000\nworld\n")
    with open(bad, "w") as f:
        f.write("WEBVTT\n\n1\n00:00:05.000 --> 00:00:06.000\nlater\n\n2\n00:00:01.000 --> 00:00:02.000\nearlier\n")
    nohdr = os.path.join(d, "nohdr.vtt")
    with open(nohdr, "w") as f:
        f.write("1\n00:00:00.000 --> 00:00:02.000\nhello\n")
    print("TUT_selftest_good_vtt=%d" % check_vtt(good, 5)[0])
    print("TUT_selftest_bad_order_rejected=%d" % (not check_vtt(bad, 10)[0]))
    print("TUT_selftest_no_header_rejected=%d" % (not check_vtt(nohdr, 10)[0]))
    print("TUT_selftest_too_long_rejected=%d" % (not check_vtt(good, 2.0)[0]))
    for p in (good, bad, nohdr):
        os.remove(p)
    os.rmdir(d)


def main():
    if sys.argv[1:2] == ["--selftest"]:
        selftest()
        return 0
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    outdir, tid = sys.argv[1], sys.argv[2]
    d = os.path.join(outdir, tid)
    man = json.load(open(os.path.join(d, tid + ".json"), encoding="utf-8"))
    vid = os.path.join(d, man["video"])
    print("TUT_video_bytes=%d" % os.path.getsize(vid))
    print("TUT_manifest_frames=%d" % man["frames"])

    # Expected values recomputed from the tutorial source, independent of the run.
    meta, items = tutlib.parse(os.path.join(HERE, tid, "tutorial.txt"))
    tl = tutlib.build_timeline(items)
    print("TUT_expected_frames=%d" % tl["frames"])
    print("TUT_expected_duration=%.1f" % tl["duration"])

    p = run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
             "-show_entries", "stream=nb_read_frames,width,height,r_frame_rate:format=duration",
             "-of", "default=nw=1", vid])
    facts = dict(l.split("=", 1) for l in p.stdout.split() if "=" in l)
    print("TUT_probe_frames=%s" % facts.get("nb_read_frames", "?"))
    print("TUT_probe_duration=%.1f" % float(facts.get("duration", "0")))
    print("TUT_probe_size=%sx%s" % (facts.get("width", "?"), facts.get("height", "?")))
    print("TUT_probe_fps=%s" % facts.get("r_frame_rate", "?"))

    # Sample ~12 evenly spaced frames, downscaled, and measure pixel variance.
    sd = tempfile.mkdtemp(prefix="tutframes-")
    nframes = int(facts.get("nb_read_frames", "0") or 0)
    step = max(1, nframes // 12)
    r = run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", vid, "-vf",
             "select='not(mod(n\\,%d))',scale=320:180" % step, "-vsync", "0",
             os.path.join(sd, "s%03d.ppm")])
    files = sorted(f for f in os.listdir(sd) if f.endswith(".ppm"))
    variances = []
    sigs = []
    for f in files:
        w, h, rgb = read_ppm(os.path.join(sd, f))
        variances.append(luma_variance(rgb))
        sigs.append(rgb)
    print("TUT_sampled_frames=%d" % len(files))
    print("TUT_blank_frames=%d" % sum(1 for v in variances if v < BLANK_VARIANCE))
    print("TUT_min_variance=%.1f" % (min(variances) if variances else 0.0))
    if len(sigs) >= 2:
        a, b = sigs[0], sigs[-1]
        diff = sum(abs(a[i] - b[i]) for i in range(0, len(a), 7)) / (len(a) / 7)
    else:
        diff = 0.0
    print("TUT_first_last_diff=%.2f" % diff)
    for f in files:
        os.remove(os.path.join(sd, f))
    os.rmdir(sd)

    ok, why, n = check_vtt(os.path.join(d, tid + ".vtt"), float(facts.get("duration", "0")))
    print("TUT_vtt=%s" % ("ok" if ok else "bad:" + why))
    print("TUT_vtt_cues=%d" % n)
    print("TUT_expected_cues=%d" % len(tl["cues"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
