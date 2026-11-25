#!/usr/bin/env python3
"""Notes as MIDI (1.5, roadmap 49 / 53) against exact ground truth (evidence label: MEASURED).

    midi_eval.py run --manifest GT_DIR/manifest.json --probe VSTMTune_WavProbe --out DIR
                     [--realtime 1|0] [--extra "name=value ..."] [--jobs 4] [--limit N]

Every case (gt_corpus.py: resynthesised real recordings, f0 known exactly) is rendered by WavProbe with
the hard chromatic setting (key C, chromatic, retune 0, humanize 0), so the note the voice is tuned to
is the nearest semitone of the truth. --notes gives the MIDI note changes on the output time axis,
--csv the per-hop notes before the MIDI filter.

  agree_ms0        share of judged truth ms-frames where the MIDI note sounding at output time
                   (t + latency) equals round(truth(t)). Judged: truth voiced and within 40 dB of the
                   file's maximum, 15 ms away from a voicing edge, within 25 ct of a semitone (the
                   corrector's hysteresis decides near the boundary) and on a note held >= 60 ms.
  agree_best_ms    the shift of the MIDI (-40 .. +40 ms, 1 ms steps) with the highest agreement:
                   > 0 = the MIDI is late against the truth, < 0 = early (the corrector's own note
                   decision is part of it: the OUTPUT changes note after the truth does too)
  out_agree_ms0    the same against the OUTPUT: the processed file is analysed again (WavProbe's
  out_best_ms      detector, its analysis delay compensated); judged where the output is voiced and
                   within 25 ct of a semitone. out_best_ms > 0 = the MIDI is late against the audio
  onset_ms         per truth note change (both notes held >= 60 ms, inside judged material): MIDI
                   change to the new note minus (change time + latency); median, p90 of |.|, missed
  notes_per_min, short30, short60   MIDI notes and those shorter than 30 / 60 ms
  raw_short5/8/15  per-hop note segments (before the MIDI filter) shorter than 5 / 8 / 15 ms, per minute
"""
import argparse
import csv
import json
import os
import subprocess
import tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

HARD = "key=0 scale=0 robotism=0 retune=0 humanize=0"


def read_notes(path):
    rows = list(csv.DictReader(open(path)))
    return np.array([float(r["out_time_s"]) for r in rows]), np.array([int(r["note"]) for r in rows])


def read_hops(path):
    rows = list(csv.DictReader(open(path)))
    return np.array([float(r["time_s"]) for r in rows]), np.array([int(r["note"]) for r in rows])


def note_track(times, notes, t):
    """MIDI note sounding at each time in t (-1 = none)."""
    idx = np.searchsorted(times, t, side="right") - 1
    out = np.where(idx >= 0, notes[np.clip(idx, 0, None)], -1)
    return out


def evaluate(case, probe, realtime, extra, workdir):
    rid = case["id"]
    out_wav = os.path.join(workdir, rid + ".wav")
    hops = os.path.join(workdir, rid + ".hops.csv")
    notes = os.path.join(workdir, rid + ".notes.csv")
    args = [probe, case["path"], out_wav, "--csv", hops, "--notes", notes] + HARD.split() + [f"realtime={realtime}"]
    if case.get("range") is not None:
        args.append(f"range={case['range']}")
    args += extra.split()
    res = subprocess.run(args, capture_output=True, text=True)
    if res.returncode != 0:
        return {"id": rid, "error": res.stderr[-300:]}
    lat_s = None
    for tok in res.stdout.split(","):
        if "latency" in tok:
            lat_s = float(tok.split("(")[1].split("ms")[0]) / 1000.0
    # the output analysed again: its pitch on the output time axis
    out_hops = os.path.join(workdir, rid + ".out.csv")
    res2 = subprocess.run([probe, out_wav, os.devnull, "--csv", out_hops, "key=0", "scale=0", "realtime=0", "amount=0"]
                          + ([f"range={case['range']}"] if case.get("range") is not None else []),
                          capture_output=True, text=True)
    os.remove(out_wav)
    orows = list(csv.DictReader(open(out_hops))) if res2.returncode == 0 else []
    if os.path.exists(out_hops):
        os.remove(out_hops)
    ot = np.array([float(r["time_s"]) - float(r["est_delay_s"]) for r in orows])
    om = np.array([float(r["in_midi"]) for r in orows])
    ov = np.array([int(r["voiced"]) > 0 for r in orows]) & (np.abs(om - np.round(om)) <= 0.25)

    g = np.load(case["gt"])
    t, f0, gv, lev = g["t"], g["f0"], g["voiced"], g["level_db"]
    midi_gt = np.where(gv, 69.0 + 12.0 * np.log2(np.maximum(f0, 1.0) / 440.0), np.nan)
    near = np.round(midi_gt)
    loud = lev > (np.max(lev) - 40.0)
    step = float(t[1] - t[0])
    edge = int(round(0.015 / step))
    # voicing edges: not judged within 15 ms
    v = gv.astype(np.int8)
    d = np.flatnonzero(np.diff(v) != 0)
    edge_mask = np.zeros_like(gv)
    for k in d:
        edge_mask[max(0, k - edge): k + edge + 1] = True
    # held notes: runs of the same rounded note >= 60 ms (voiced)
    held = np.zeros_like(gv)
    run_note = np.where(gv, near, -999)
    starts = np.flatnonzero(np.r_[True, run_note[1:] != run_note[:-1]])
    ends = np.r_[starts[1:], len(run_note)]
    runs = []
    for s, e in zip(starts, ends):
        if run_note[s] != -999 and (e - s) * step >= 0.060:
            held[s:e] = True
        runs.append((s, e, run_note[s]))
    judged = gv & loud & ~edge_mask & (np.abs(midi_gt - near) <= 0.25) & held

    nt, nn = read_notes(notes)
    shifts = np.arange(-40, 41)
    agree = []
    for sh in shifts:
        m = note_track(nt, nn, t[judged] + lat_s + sh / 1000.0)
        agree.append(float(np.mean(m == near[judged])) if judged.any() else np.nan)
    agree = np.array(agree)

    out_agree = []
    for sh in shifts:
        m = note_track(nt, nn, ot[ov] + sh / 1000.0)
        out_agree.append(float(np.mean(m == np.round(om[ov]))) if ov.any() else np.nan)
    out_agree = np.array(out_agree)

    # onsets: truth note changes between two held runs that are adjacent (allow a <= 30 ms gap of other notes)
    onset = []
    missed = 0
    held_runs = [(s, e, n) for (s, e, n) in runs if n != -999 and (e - s) * step >= 0.060]
    for (s0, e0, n0), (s1, e1, n1) in zip(held_runs[:-1], held_runs[1:]):
        if n0 == n1 or (s1 - e0) * step > 0.030 or not (loud[s0:e1].all()):
            continue
        tc = 0.5 * (t[e0 - 1] + t[s1]) + lat_s          # truth change on the output axis
        cand = np.flatnonzero((nn == n1) & (np.abs(nt - tc) <= 0.080))
        if len(cand) == 0:
            missed += 1
            continue
        onset.append(1000.0 * (nt[cand[np.argmin(np.abs(nt[cand] - tc))]] - tc))

    # MIDI note lengths
    lengths = []
    for i in range(len(nt) - 1):
        if nn[i] >= 0:
            lengths.append(nt[i + 1] - nt[i])
    lengths = np.array(lengths)
    minutes = float(t[-1]) / 60.0

    ht, hn = read_hops(hops)
    seg = []
    s0 = 0
    for i in range(1, len(hn) + 1):
        if i == len(hn) or hn[i] != hn[s0]:
            if hn[s0] >= 0:
                seg.append(ht[min(i, len(hn) - 1)] - ht[s0])
            s0 = i
    seg = np.array(seg)
    os.remove(hops)
    os.remove(notes)
    return {
        "id": rid, "minutes": minutes, "latency_ms": 1000.0 * lat_s, "judged_s": float(judged.sum() * step),
        "agree_ms0": float(agree[shifts == 0][0]), "agree_best_ms": int(shifts[np.nanargmax(agree)]) if judged.any() else 0,
        "agree_curve": agree.tolist(), "out_judged_hops": int(ov.sum()), "out_curve": out_agree.tolist(),
        "onsets": onset, "onset_missed": missed,
        "notes": int(len(lengths)), "short30": int(np.sum(lengths < 0.030)), "short60": int(np.sum(lengths < 0.060)),
        "raw_segments": int(len(seg)), "raw_short5": int(np.sum(seg < 0.005)), "raw_short8": int(np.sum(seg < 0.008)),
        "raw_short15": int(np.sum(seg < 0.015)),
    }


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--manifest", required=True)
    r.add_argument("--probe", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--realtime", default="1")
    r.add_argument("--extra", default="")
    r.add_argument("--jobs", type=int, default=4)
    r.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()

    cases = json.load(open(a.manifest))["files"]
    if a.limit:
        cases = cases[: a.limit]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        with ProcessPoolExecutor(a.jobs) as ex:
            res = list(ex.map(evaluate, cases, [a.probe] * len(cases), [a.realtime] * len(cases),
                              [a.extra] * len(cases), [tmp] * len(cases)))
    errs = [x for x in res if "error" in x]
    res = [x for x in res if "error" not in x]
    w = np.array([x["judged_s"] for x in res])
    curve = np.nansum([np.array(x["agree_curve"]) * x["judged_s"] for x in res], axis=0) / w.sum()
    shifts = np.arange(-40, 41)
    wo = np.array([x["out_judged_hops"] for x in res], dtype=float)
    ocurve = np.nansum([np.array(x["out_curve"]) * x["out_judged_hops"] for x in res], axis=0) / wo.sum()
    onsets = np.concatenate([np.array(x["onsets"]) for x in res if x["onsets"]]) if any(x["onsets"] for x in res) else np.array([])
    minutes = sum(x["minutes"] for x in res)
    summary = {
        "evidence": "MEASURED", "realtime": a.realtime, "extra": a.extra, "files": len(res), "errors": len(errs),
        "minutes": minutes, "judged_minutes": float(w.sum() / 60.0),
        "agree_ms0": float(curve[shifts == 0][0]), "agree_best_ms": int(shifts[np.argmax(curve)]),
        "agree_best": float(np.max(curve)),
        "out_agree_ms0": float(ocurve[shifts == 0][0]), "out_best_ms": int(shifts[np.argmax(ocurve)]),
        "out_agree_best": float(np.max(ocurve)),
        "onset_count": int(len(onsets)), "onset_missed": int(sum(x["onset_missed"] for x in res)),
        "onset_median_ms": float(np.median(onsets)) if len(onsets) else None,
        "onset_p90_abs_ms": float(np.percentile(np.abs(onsets), 90)) if len(onsets) else None,
        "onset_late_over_20ms": float(np.mean(onsets > 20.0)) if len(onsets) else None,
        "notes_per_min": sum(x["notes"] for x in res) / minutes,
        "short30_per_min": sum(x["short30"] for x in res) / minutes,
        "short60_per_min": sum(x["short60"] for x in res) / minutes,
        "raw_segments_per_min": sum(x["raw_segments"] for x in res) / minutes,
        "raw_short5_per_min": sum(x["raw_short5"] for x in res) / minutes,
        "raw_short8_per_min": sum(x["raw_short8"] for x in res) / minutes,
        "raw_short15_per_min": sum(x["raw_short15"] for x in res) / minutes,
    }
    json.dump({"summary": summary, "cases": res, "errors": errs}, open(out / "midi_eval.json", "w"), indent=1)
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
