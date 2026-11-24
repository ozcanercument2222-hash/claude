#!/usr/bin/env python3
"""Note-change timing of the hard-tune target, and steady accuracy of the output (evidence: MEASURED).

    transition_eval.py run --manifest GT/manifest.json --probe VSTMTune_WavProbe --out DIR
                       [--setting "key=0 scale=0 robotism=0 retune=0 humanize=0"] [--realtime 1] [--group ...]
    transition_eval.py compare A/summary.json B/summary.json [--names a,b]

Exact-truth corpus (gt_corpus.py synth). A note change of the input = the rounded sung pitch moves to
another semitone and both notes hold for 80 ms (vibrato and scoops do not count). The engine's hop
log gives its target note per hop at the time the engine files it under (where the correction is
applied: hop time minus the detector delay; older WavProbes: the 1.0 - 1.2 frame layout). For every
input note change:
  timing_ms     first hop whose target is the new note, minus the input crossing (> 0 late; < 0 the
                output changes note before the singer does, the tail of the old note is retuned)
  wrong_target  a target on the way that is neither note (a third note)
The rendered output is also measured (YIN, octave-safe range): steady error against the sung note
away from note changes (median / p95 ct).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
import soundfile as sf


def stable_crossings(q, v, step, hold_s=0.08):
    """Indices where the rounded truth note changes and stays changed: the note before is constant for
    hold_s before the change and the new one for hold_s after it (vibrato and scoops come back sooner)."""
    k = int(round(hold_s / step))
    idx = np.flatnonzero(v[1:] & v[:-1] & (q[1:] != q[:-1]))
    out = []
    for i in idx:
        a0, b0 = max(0, i - k), min(len(q), i + 1 + k)
        if np.all(v[a0:i + 1]) and np.all(v[i + 1:b0]) and np.all(q[a0:i + 1] == q[i]) and np.all(q[i + 1:b0] == q[i + 1]):
            out.append(i)
    return out


def job(a):
    case, probe, setting, rt, out = a
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import gt_eval
    fd, tmp = tempfile.mkstemp(suffix=".wav", dir=out)
    os.close(fd)
    hops = tmp + ".csv"
    args = [probe, case["path"], tmp, "--csv", hops] + setting.split() + [f"range={case['range']}", f"realtime={rt}"]
    p = subprocess.run(args, capture_output=True, text=True)
    if p.returncode:
        os.unlink(tmp)
        return case["id"], {"error": p.stderr[-200:]}
    lat = int(p.stdout.split("latency ")[1].split(" ")[0])
    y, sr = sf.read(tmp, always_2d=True, dtype="float64")
    os.unlink(tmp)
    h = gt_eval.read_hops(hops)
    os.unlink(hops)
    dl = h["est_delay_s"] if h["est_delay_s"] is not None else gt_eval.fallback_delay(h["in_midi"], sr, case["range"])
    te = h["time_s"] - np.where(dl > 0, dl, np.median(dl[dl > 0]) if np.any(dl > 0) else 0.02)
    tgt = np.where(h["voiced"], np.round(h["out_midi"], 3), np.nan)      # hard tune: the target note itself
    g = np.load(case["gt"])
    tg, f0 = g["t"], g["f0"]
    step = float(tg[1] - tg[0])
    v = f0 > 0
    m_in = np.where(v, 69.0 + 12.0 * np.log2(np.maximum(f0, 1.0) / 440.0), np.nan)
    q = np.where(v, np.round(m_in), np.nan)
    res = {"transitions": 0, "timing_ms": [], "wrong_target": 0}
    for i in stable_crossings(q, v, step):
        t_in, a_note, b_note = tg[i], q[i], q[i + 1]
        w = (te > t_in - 0.08) & (te < t_in + 0.10) & np.isfinite(tgt)
        if w.sum() < 10:
            continue
        tw, gw = te[w], tgt[w]
        hit = np.flatnonzero(np.abs(gw - b_note) < 0.01)
        if hit.size == 0:
            continue
        res["timing_ms"].append(round(1000.0 * (tw[hit[0]] - t_in), 2))
        res["transitions"] += 1
        # a target that is neither note of the transition (a wrong note on the way)
        if np.any((np.abs(gw - a_note) > 0.01) & (np.abs(gw - b_note) > 0.01)
                  & (np.abs(gw - 0.5 * (a_note + b_note)) > 0.6)):
            res["wrong_target"] += 1
    # steady accuracy of the rendered output (latency removed), away from note changes
    import librosa
    yy = y.mean(axis=1)[lat:]
    lo_hz = max(48.0, 0.7 * float(np.min(f0[v])))
    fl = int(2 ** np.ceil(np.log2(2.2 * sr / lo_hz)))
    f = librosa.yin(yy, fmin=lo_hz, fmax=min(0.45 * sr, 1.4 * float(np.max(f0[v]))), sr=sr, frame_length=fl, hop_length=int(sr * 0.002))
    to = librosa.times_like(f, sr=sr, hop_length=int(sr * 0.002))
    mo = 69.0 + 12.0 * np.log2(np.maximum(f, 1.0) / 440.0)
    near = np.zeros_like(v)
    for i in np.flatnonzero(v[1:] & v[:-1] & (q[1:] != q[:-1])):
        near[max(0, i - 60):i + 80] = True
    steady = v & ~near & (tg > 0.2)
    err = np.abs(np.interp(tg[steady], to, mo) - q[steady]) * 100.0
    err = err[err < 300.0]
    res["steady_median_ct"] = round(float(np.median(err)), 2) if err.size else None
    res["steady_p95_ct"] = round(float(np.percentile(err, 95)), 2) if err.size else None
    return case["id"], res


def summarise(per):
    tm = np.array([x for r in per.values() if "timing_ms" in r for x in r["timing_ms"]], dtype=float)
    n = int(tm.size)
    bl = sum(r.get("wrong_target", 0) for r in per.values())
    st = [r["steady_p95_ct"] for r in per.values() if r.get("steady_p95_ct") is not None]
    sm = [r["steady_median_ct"] for r in per.values() if r.get("steady_median_ct") is not None]
    return {"transitions": n,
            "timing_median_ms": round(float(np.median(tm)), 2) if n else None,
            "timing_abs_p95_ms": round(float(np.percentile(np.abs(tm), 95)), 2) if n else None,
            "timing_abs_max_ms": round(float(np.max(np.abs(tm))), 2) if n else None,
            "early_gt5ms": round(float(np.mean(tm < -5.0)), 4) if n else None,
            "late_gt15ms": round(float(np.mean(tm > 15.0)), 4) if n else None,
            "wrong_target_per_100": round(100.0 * bl / max(1, n), 2),
            "steady_median_ct": round(float(np.median(sm)), 2) if sm else None,
            "steady_p95_ct_median": round(float(np.median(st)), 2) if st else None}


def cmd_run(a):
    man = json.loads(Path(a.manifest).read_text())
    cases = [c for c in man["files"] if any(c["group"].startswith(g) for g in a.group.split(","))]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    with ProcessPoolExecutor(a.jobs) as ex:
        per = dict(ex.map(job, [(c, a.probe, a.setting, a.realtime, str(out)) for c in cases]))
    groups = {}
    for c in cases:
        groups.setdefault(c["group"], {})[c["id"]] = per[c["id"]]
    summ = {"ALL": summarise(per)}
    summ.update({g: summarise(v) for g, v in sorted(groups.items())})
    meta = {"evidence": "MEASURED (exact-truth input, output pitch measured)", "probe": a.probe, "setting": a.setting,
            "realtime": a.realtime, "cases": len(cases)}
    (out / "per_case.json").write_text(json.dumps(per, indent=0))
    (out / "summary.json").write_text(json.dumps({"meta": meta, "summary": summ}, indent=1))
    print(json.dumps(summ["ALL"]))


def cmd_compare(a):
    names = a.names.split(",") if a.names else [Path(p).parent.name for p in a.summaries]
    sums = {n: json.loads(Path(p).read_text())["summary"] for n, p in zip(names, a.summaries)}
    cols = ["transitions", "timing_median_ms", "timing_abs_p95_ms", "timing_abs_max_ms", "early_gt5ms", "late_gt15ms",
            "wrong_target_per_100", "steady_median_ct", "steady_p95_ct_median"]
    print("| group | build | " + " | ".join(cols) + " |")
    print("|---|---" + "|---" * len(cols) + "|")
    for g in sums[names[0]]:
        for n in names:
            r = sums[n].get(g)
            if r:
                print(f"| {g} | {n} | " + " | ".join(str(r.get(c)) for c in cols) + " |")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--manifest", required=True)
    r.add_argument("--probe", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--setting", default="key=0 scale=0 robotism=0 retune=0 humanize=0")
    r.add_argument("--realtime", default="1")
    r.add_argument("--group", default="sy_fast,sy_portamento,sy_glide,sy_male,sy_register,sy_bass_line,sy_soprano,sy_consonant")
    r.add_argument("--jobs", type=int, default=3)
    c = sub.add_parser("compare")
    c.add_argument("summaries", nargs="+")
    c.add_argument("--names", default="")
    a = ap.parse_args()
    return cmd_run(a) if a.cmd == "run" else cmd_compare(a)


if __name__ == "__main__":
    sys.exit(main())
