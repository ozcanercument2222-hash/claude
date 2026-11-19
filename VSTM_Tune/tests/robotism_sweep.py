#!/usr/bin/env python3
"""ROBOTISM sweep on real recordings, per voice group (evidence label: MEASURED).

    robotism_sweep.py --manifest vocals.json --probe build/VSTMTune_WavProbe --out DIR
                      [--points 0,10,25,35,50,65,80,90,100] [--only-tech vibrato,straight] [--jobs 4]
                      [--retune 15 --humanize 0.35] [--extra "adaptive=1"]

For every input and ROBOTISM value the input is rendered (chromatic, so the knob alone decides
how hard the notes are pulled) and both the dry and the rendered pitch are tracked with the same
short-window YIN (46 ms frames, 5.8 ms hop; pYIN's 93 ms frames hide most of a 5-6 Hz vibrato).
Per render:
  vibrato_kept_pct   rms of the 3.5 - 9 Hz pitch movement, wet / dry, on held notes of the dry
                     signal only (centre within 0.6 st for >= 0.35 s), so note changes do not count
  tight_ct           median |cents from the nearest semitone| of the wet pitch
  transition_ms      median time the wet pitch needs to cross the middle 80 % of a note change
                     of >= 0.8 semitone in the dry pitch (where the singer's own change is < 60 ms)
DIR/sweep.json holds per-file values, DIR/sweep.md the table per group and point.
Pitch graphs cannot prove naturalness: these numbers show how consistent the knob is across
voices, not how it sounds.
"""
import argparse
import json
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

SR = 22050
HOP = 128
FRAME = 1024


def track(path, latency=0, n=None):
    import librosa
    y, sr = sf.read(str(path), always_2d=True, dtype="float64")
    y = y.mean(axis=1)
    if latency:
        y = y[latency:]
    if n is not None:
        y = y[:n]
    y = librosa.resample(y, orig_sr=sr, target_sr=SR)
    f0 = librosa.yin(y, fmin=60, fmax=1100, sr=SR, frame_length=FRAME, hop_length=HOP, trough_threshold=0.1)
    rms = librosa.feature.rms(y=y, frame_length=FRAME, hop_length=HOP)[0][:len(f0)]
    # voiced: loud enough and a stable estimate (YIN gives a value everywhere)
    midi = 69.0 + 12.0 * np.log2(f0 / 440.0)
    jump = np.abs(np.diff(midi, prepend=midi[0]))
    voiced = (rms > rms.max() * 10 ** (-35 / 20)) & (jump < 1.5)
    return midi, voiced, len(y)


def runs(mask, min_len):
    out, start = [], None
    for i, m in enumerate(np.append(mask, False)):
        if m and start is None:
            start = i
        elif not m and start is not None:
            if i - start >= min_len:
                out.append((start, i))
            start = None
    return out


def held_notes(dry_midi, voiced):
    """Held notes of the DRY pitch: voiced runs where its 2.5 Hz low-passed centre stays within
    0.6 semitone for >= 0.35 s (vibrato is measured there only, not across note changes)."""
    fs = SR / HOP
    bl, al = signal.butter(2, 2.5 / (fs / 2))
    keep = np.zeros(len(dry_midi), dtype=bool)
    for s, e in runs(voiced, int(0.4 * fs)):
        c = signal.filtfilt(bl, al, dry_midi[s:e]) if e - s > 15 else dry_midi[s:e]
        w = int(0.35 * fs)
        for i in range(0, e - s - w + 1, max(1, w // 4)):
            seg = c[i:i + w]
            if seg.max() - seg.min() <= 0.6:
                keep[s + i:s + i + w] = True
    return keep


def vib_rms(midi, mask):
    fs = SR / HOP
    b, a = signal.butter(2, [3.5 / (fs / 2), 9.0 / (fs / 2)], btype="band")
    acc, n = 0.0, 0
    for s, e in runs(mask, int(0.35 * fs)):
        seg = signal.filtfilt(b, a, midi[s:e] - np.mean(midi[s:e]))
        k = int(0.08 * fs)                                 # skip filter edges
        if e - s > 2 * k + 4:
            seg = seg[k:-k]
            acc += float(np.sum(seg ** 2))
            n += len(seg)
    return np.sqrt(acc / n) if n else None


def transitions(dm, dv, wm, wv):
    """Wet crossing time of the middle 80 % of fast dry note changes."""
    fs = SR / HOP
    res = []
    sm = signal.medfilt(np.where(dv, dm, 0), 5)
    i = 0
    L = len(sm)
    while i < L - int(0.2 * fs):
        a, b = i, i + int(0.06 * fs)
        if dv[a] and dv[b] and dv[a:b].all() and abs(sm[b] - sm[a]) >= 0.8:
            lo, hi = sm[a], sm[b]
            seg = wm[a:a + int(0.4 * fs)]
            ok = wv[a:a + int(0.4 * fs)]
            if len(seg) and ok.mean() > 0.9:
                frac = (seg - lo) / (hi - lo)
                t10 = np.argmax(frac > 0.1) if (frac > 0.1).any() else None
                t90 = np.argmax(frac > 0.9) if (frac > 0.9).any() else None
                if t10 is not None and t90 is not None and t90 >= t10:
                    res.append((t90 - t10) * 1000.0 / fs)
            i = b
        else:
            i += 2
    return float(np.median(res)) if res else None, len(res)


def one(job):
    f, rb, probe, out, retune, humanize, extra = job
    wav = Path(out) / "wav" / f"{f['id']}__r{rb}.wav"
    args = [probe, f["path"], str(wav), "key=0", "scale=0", f"robotism={rb / 100.0}", f"retune={retune}",
            f"humanize={humanize}", f"range={f.get('range', 1)}"] + extra.split()
    p = subprocess.run(args, capture_output=True, text=True)
    if p.returncode != 0:
        return {"id": f["id"], "group": f.get("group"), "robotism": rb, "error": p.stderr[-300:]}
    lat = int(p.stdout.split("latency ")[1].split(" ")[0])
    dm, dv, n = track(f["path"])
    wm, wv, _ = track(wav, lat)
    m = min(len(dm), len(wm))
    dm, dv, wm, wv = dm[:m], dv[:m], wm[:m], wv[:m]
    both = dv & wv
    held = held_notes(dm, both) & both
    vd, vw = vib_rms(dm, held), vib_rms(wm, held)
    tr, ntr = transitions(dm, dv, wm, wv)
    wav.unlink()
    cents = np.abs(100.0 * (wm[both] - np.round(wm[both])))
    return {"id": f["id"], "group": f.get("group"), "technique": f.get("technique", ""), "robotism": rb,
            "vibrato_dry_ct": None if vd is None else round(100 * vd, 2),
            "vibrato_kept_pct": None if (vd is None or vd < 0.03) else round(100.0 * vw / vd, 1),
            "tight_ct": round(float(np.median(cents)), 2) if cents.size else None,
            "transition_ms": None if tr is None else round(tr, 1), "transitions": ntr}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--probe", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--points", default="0,10,25,35,50,65,80,90,100")
    ap.add_argument("--only-tech", default="")
    ap.add_argument("--retune", type=float, default=15.0)
    ap.add_argument("--humanize", type=float, default=0.35)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--extra", default="", help="more WavProbe name=value pairs for every render, e.g. adaptive=1")
    a = ap.parse_args()
    man = json.loads(Path(a.manifest).read_text())["files"]
    if a.only_tech:
        keep = a.only_tech.split(",")
        man = [f for f in man if f.get("technique", "song") in keep or not f["id"].startswith("vs_")]
    pts = [int(x) for x in a.points.split(",")]
    (Path(a.out) / "wav").mkdir(parents=True, exist_ok=True)
    jobs = [(f, rb, a.probe, a.out, a.retune, a.humanize, a.extra) for f in man for rb in pts]
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        res = list(ex.map(one, jobs))
    Path(a.out, "sweep.json").write_text(json.dumps(res, indent=1))
    groups = sorted({r["group"] for r in res})
    lines = ["| group | metric | " + " | ".join(f"{p} %" for p in pts) + " |", "|---" * (len(pts) + 2) + "|"]
    for g in groups + ["ALL"]:
        for metric in ("vibrato_kept_pct", "tight_ct", "transition_ms"):
            cells = []
            for p in pts:
                v = [r[metric] for r in res if r.get(metric) is not None and r["robotism"] == p and (g == "ALL" or r["group"] == g)]
                cells.append(f"{np.median(v):.1f}" if v else "–")
            lines.append(f"| {g} | {metric} | " + " | ".join(cells) + " |")
    md = "\n".join(lines)
    Path(a.out, "sweep.md").write_text(md + "\n")
    print(md)


if __name__ == "__main__":
    sys.exit(main())
