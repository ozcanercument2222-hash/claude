#!/usr/bin/env python3
"""Target-note changes that should not happen (MEASURED, exact-truth synthetic corpus, hard tune by default).
  steady_per_min  target changes while the sung (rounded) note is constant for +-100 ms around
  vibrato_per_min target changes while the vibrato-free note (250 ms mean of the sung pitch) is constant for
                  +-150 ms but the raw pitch crosses into a neighbour note (vibrato turned into a trill)
  changes_per_min all target changes per voiced minute
    false_switch.py --manifest M --probe P --out DIR [--setting ...] [--realtime 1] [--groups g1,g2]
"""
import argparse, json, os, subprocess, sys, tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gt_eval

def job(a):
    case, probe, setting, rt, out = a
    fd, tmp = tempfile.mkstemp(suffix=".wav", dir=out); os.close(fd)
    hops = tmp + ".csv"
    p = subprocess.run([probe, case["path"], tmp, "--csv", hops] + setting.split() + [f"range={case['range']}", f"realtime={rt}"],
                       capture_output=True, text=True)
    os.unlink(tmp)
    if p.returncode:
        return case["id"], None
    h = gt_eval.read_hops(hops); os.unlink(hops)
    te = h["time_s"] - h["est_delay_s"]
    tgt = np.round(h["out_midi"], 3)
    vo = h["voiced"]
    g = np.load(case["gt"]); tg, f0 = g["t"], g["f0"]
    step = float(tg[1] - tg[0]); v = f0 > 0
    m = np.where(v, 69.0 + 12.0 * np.log2(np.maximum(f0, 1.0) / 440.0), np.nan)
    q = np.round(m)
    k = max(1, int(round(0.25 / step)))
    mm = np.where(v, m, 0.0); cnt = np.convolve(v.astype(float), np.ones(k), "same")
    centre = np.where(cnt > 0.5 * k, np.convolve(mm, np.ones(k), "same") / np.maximum(cnt, 1), np.nan)
    qc = np.round(centre)
    def const_around(arr, i, half):
        a0, b0 = max(0, i - half), min(len(arr), i + half + 1)
        seg = arr[a0:b0]
        return np.all(np.isfinite(seg)) and np.all(seg == seg[0]) and np.all(v[a0:b0])
    h100, h150 = int(round(0.10 / step)), int(round(0.15 / step))
    ch = np.flatnonzero(vo[1:] & vo[:-1] & (np.abs(tgt[1:] - tgt[:-1]) > 0.5)) + 1
    steady = vib = 0
    for j in ch:
        i = int(np.clip(np.searchsorted(tg, te[j]), 0, len(tg) - 1))
        if const_around(q, i, h100):
            steady += 1
        elif const_around(qc, i, h150):
            vib += 1
    voiced_min = max(1e-9, float(np.sum(vo)) * float(np.median(np.diff(h["time_s"]))) / 60.0)
    return case["id"], {"group": case["group"], "changes": int(len(ch)), "steady": steady, "vibrato": vib, "voiced_min": voiced_min}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True); ap.add_argument("--probe", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--setting", default="key=0 scale=0 robotism=0 retune=0 humanize=0"); ap.add_argument("--realtime", default="1")
    ap.add_argument("--groups", default=""); ap.add_argument("--jobs", type=int, default=4)
    a = ap.parse_args()
    man = json.loads(Path(a.manifest).read_text())
    cases = [c for c in man["files"] if not a.groups or any(c["group"].startswith(g) for g in a.groups.split(","))]
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    with ProcessPoolExecutor(a.jobs) as ex:
        res = {k: v for k, v in ex.map(job, [(c, a.probe, a.setting, a.realtime, str(out)) for c in cases]) if v}
    (out / "false_switch.json").write_text(json.dumps(res, indent=0))
    groups = {}
    for r in res.values():
        groups.setdefault(r["group"], []).append(r)
    groups["ALL"] = list(res.values())
    summ = {}
    for gname, rs in sorted(groups.items()):
        mins = sum(r["voiced_min"] for r in rs)
        summ[gname] = {"files": len(rs), "steady_per_min": round(sum(r["steady"] for r in rs) / mins, 2),
                       "vibrato_per_min": round(sum(r["vibrato"] for r in rs) / mins, 2),
                       "changes_per_min": round(sum(r["changes"] for r in rs) / mins, 1),
                       "steady": sum(r["steady"] for r in rs), "vibrato": sum(r["vibrato"] for r in rs)}
        print(gname, summ[gname])
    (out / "summary.json").write_text(json.dumps({"setting": a.setting, "realtime": a.realtime, "probe": a.probe, "summary": summ}, indent=1))

if __name__ == "__main__":
    main()
