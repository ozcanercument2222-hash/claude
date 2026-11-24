#!/usr/bin/env python3
"""Where does the hard-tune switching delay come from? (MEASURED, exact-truth corpus)
For every clean input note change (as transition_eval.py) report:
  det_ms      first hop whose detected pitch is past the midpoint (closer to the new note) - truth crossing
  switch_ms   first hop whose target is the new note - truth crossing   (= transition_eval timing_ms)
  decide_ms   switch_ms - det_ms (hysteresis + dwell + hop grid)
  settle_ct   |truth - new note| 40 ms after the crossing (does the singer really arrive at a note?)
    transition_decompose.py --manifest M --probe P --out DIR [--setting ...] [--realtime 1]
"""
import argparse, json, os, subprocess, sys, tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gt_eval
from transition_eval import stable_crossings

def job(a):
    case, probe, setting, rt, out = a
    fd, tmp = tempfile.mkstemp(suffix=".wav", dir=out); os.close(fd)
    hops = tmp + ".csv"
    p = subprocess.run([probe, case["path"], tmp, "--csv", hops] + setting.split() + [f"range={case['range']}", f"realtime={rt}"],
                       capture_output=True, text=True)
    if p.returncode:
        os.unlink(tmp)
        return case["id"], []
    import soundfile as sf, librosa
    lat = int(p.stdout.split("latency ")[1].split(" ")[0])
    y, sr = sf.read(tmp, always_2d=True, dtype="float64"); os.unlink(tmp)
    yy = y.mean(axis=1)[lat:]
    h = gt_eval.read_hops(hops); os.unlink(hops)
    dl = h["est_delay_s"]
    te = h["time_s"] - dl
    tgt = np.where(h["voiced"], np.round(h["out_midi"], 3), np.nan)
    inm = np.where(h["voiced"], h["in_midi"], np.nan)
    g = np.load(case["gt"]); tg, f0 = g["t"], g["f0"]
    step = float(tg[1] - tg[0]); v = f0 > 0
    m_in = np.where(v, 69.0 + 12.0 * np.log2(np.maximum(f0, 1.0) / 440.0), np.nan)
    q = np.where(v, np.round(m_in), np.nan)
    rows = []
    for i in stable_crossings(q, v, step):
        t_in, a, b = tg[i], q[i], q[i + 1]
        w = (te > t_in - 0.08) & (te < t_in + 0.10) & np.isfinite(tgt)
        if w.sum() < 10:
            continue
        tw, gw, iw = te[w], tgt[w], inm[w]
        hit = np.flatnonzero(np.abs(gw - b) < 0.01)
        if hit.size == 0:
            continue
        past = np.flatnonzero(np.abs(iw - b) < np.abs(iw - a))
        k40 = min(len(m_in) - 1, i + int(round(0.04 / step)))
        # the audible switch: output pitch (YIN, frame-centred, 0.5 ms hop) first closer to b than to a
        # and staying so for 10 ms, searched from 60 ms before the truth crossing
        lo_hz = 440.0 * 2.0 ** ((min(a, b) - 69.0 - 1.0) / 12.0); hi_hz = 440.0 * 2.0 ** ((max(a, b) - 69.0 + 1.0) / 12.0)
        fl = int(2 ** np.ceil(np.log2(2.2 * sr / lo_hz)))
        s0 = max(0, int((t_in - 0.06) * sr) - fl); s1 = min(len(yy), int((t_in + 0.12) * sr) + fl)
        out_ms = None
        if s1 - s0 > 2 * fl:
            hp = max(1, int(sr * 0.0005))
            f = librosa.yin(yy[s0:s1], fmin=lo_hz, fmax=hi_hz, sr=sr, frame_length=fl, hop_length=hp, center=True)
            to = s0 / sr + np.arange(len(f)) * hp / sr
            mo = 69.0 + 12.0 * np.log2(np.maximum(f, 1.0) / 440.0)
            closer = np.abs(mo - b) < np.abs(mo - a)
            need = int(round(0.010 / (hp / sr)))
            ok = (to >= t_in - 0.06)
            for j in np.flatnonzero(closer & ok):
                if j + need <= len(closer) and np.all(closer[j:j + need]):
                    out_ms = round(1000 * (to[j] - t_in), 2); break
        rows.append({"case": case["id"], "group": case["group"], "a": a, "b": b, "out_ms": out_ms,
                     "switch_ms": round(1000 * (tw[hit[0]] - t_in), 2),
                     "det_ms": round(1000 * (tw[past[0]] - t_in), 2) if past.size else None,
                     "settle_ct": round(100 * abs(m_in[k40] - b), 1)})
    return case["id"], rows

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True); ap.add_argument("--probe", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--setting", default="key=0 scale=0 robotism=0 retune=0 humanize=0"); ap.add_argument("--realtime", default="1")
    ap.add_argument("--group", default="sy_fast,sy_portamento,sy_glide,sy_male,sy_register,sy_bass_line,sy_soprano,sy_consonant")
    ap.add_argument("--jobs", type=int, default=4)
    a = ap.parse_args()
    man = json.loads(Path(a.manifest).read_text())
    cases = [c for c in man["files"] if any(c["group"].startswith(g) for g in a.group.split(","))]
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    with ProcessPoolExecutor(a.jobs) as ex:
        res = dict(ex.map(job, [(c, a.probe, a.setting, a.realtime, str(out)) for c in cases]))
    rows = [r for v in res.values() for r in v]
    (out / "decompose.json").write_text(json.dumps(rows, indent=0))
    clean = [r for r in rows if r["settle_ct"] <= 30]
    for name, rs in (("all", rows), ("clean (settles within 30 ct of the new note)", clean), ("ambiguous", [r for r in rows if r["settle_ct"] > 30])):
        if not rs: continue
        sw = np.array([r["switch_ms"] for r in rs]); dt = np.array([r["det_ms"] for r in rs if r["det_ms"] is not None])
        dc = np.array([r["switch_ms"] - r["det_ms"] for r in rs if r["det_ms"] is not None])
        print(f"{name}: n={len(rs)} switch median {np.median(sw):.2f} |p95| {np.percentile(np.abs(sw),95):.2f} max {np.max(np.abs(sw)):.2f} late>15 {np.mean(sw>15):.3f}"
              f" | det median {np.median(dt):.2f} | decide median {np.median(dc):.2f} p95 {np.percentile(dc,95):.2f}")
        om = np.array([r["out_ms"] for r in rs if r["out_ms"] is not None])
        if om.size:
            print(f"     audible (output pitch) median {np.median(om):.2f} |p95| {np.percentile(np.abs(om),95):.2f} late>15 {np.mean(om>15):.3f} n={om.size}")
    for r in rows:
        print(f"  {r['case']:28s} {r['a']:.0f}->{r['b']:.0f} switch {r['switch_ms']:7.2f} out {r['out_ms']} det {r['det_ms']} settle {r['settle_ct']}")

if __name__ == "__main__":
    main()
