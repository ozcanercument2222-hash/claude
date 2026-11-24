#!/usr/bin/env python3
"""Peak safety on HOT input (MEASURED). Every file is scaled so its dry sample peak is -1 dBFS (a copy in
--work; inputs are never written), rendered with the output guard off and on, latency removed:
  sp_db / tp_db     output sample peak / true peak (4x oversampled), dBFS
  over0             samples above 0 dBFS;  over1 = samples above -1 dBFS
  gr_db             largest gain reduction of the guard (from WavProbe's guard_min)
  touched_pct       share of samples the guard changed
  change_db         energy of (guarded - unguarded) relative to the unguarded signal (how much the guard alters)
  rms_db            level change guarded vs unguarded (whole file)
    hot_peaks.py --manifest M --probe P --out DIR --work SCRATCH [--every 3] [--settings ...]
"""
import argparse, json, os, subprocess, tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
import numpy as np, soundfile as sf
from scipy import signal

HARD = "key=0 scale=0 robotism=0 retune=0 humanize=0"
SETTINGS = {"hard": HARD, "default": "", "natural": "robotism=1 retune=40", "tr+12": "transpose=12", "tr+7": "transpose=7", "tr-12": "transpose=-12",
            "fm+12": "formant=12", "fm-12": "formant=-12", "tr+12_fm-6": "transpose=12 formant=-6", "tr+7_fm+12": "transpose=7 formant=12",
            "hard_tr+12": HARD + " transpose=12"}

def tp(x):
    return float(np.max(np.abs(signal.resample_poly(x, 4, 1, axis=0)))) if x.size else 0.0

def job(a):
    fid, path, rng, probe, sname, setting, work = a
    out = {}
    hot = os.path.join(work, fid + "_hot.wav")
    ys = {}
    for g in (0, 1):
        fd, tmp = tempfile.mkstemp(suffix=".wav", dir=work); os.close(fd)
        p = subprocess.run([probe, hot, tmp] + setting.split() + [f"range={rng}", "realtime=1", f"guard={g}"], capture_output=True, text=True)
        if p.returncode:
            os.unlink(tmp); return fid, sname, {"error": p.stderr[-200:]}
        lat = int(p.stdout.split("latency ")[1].split(" ")[0])
        gm = float(p.stdout.split("guard_min ")[1].split()[0])
        y, sr = sf.read(tmp, always_2d=True, dtype="float64"); os.unlink(tmp)
        ys[g] = (y[lat:], gm)
    u, g = ys[0][0], ys[1][0]
    n = min(len(u), len(g)); u, g = u[:n], g[:n]
    for tag, y in (("off", u), ("on", g)):
        out[f"sp_db_{tag}"] = round(20 * np.log10(max(1e-9, float(np.max(np.abs(y))))), 2)
        out[f"tp_db_{tag}"] = round(20 * np.log10(max(1e-9, tp(y))), 2)
        out[f"over0_{tag}"] = int(np.sum(np.abs(y) > 1.0))
        out[f"over1_{tag}"] = int(np.sum(np.abs(y) > 0.8913))
    out["gr_db"] = round(-20 * np.log10(max(1e-9, ys[1][1])), 2)
    d = g - u
    out["touched_pct"] = round(100.0 * float(np.mean(np.any(np.abs(d) > 1e-7, axis=1))), 3)
    eu = float(np.sum(u * u))
    out["change_db"] = round(10 * np.log10(max(1e-20, float(np.sum(d * d))) / max(1e-20, eu)), 1) if np.any(d) else None
    out["rms_db"] = round(10 * np.log10(max(1e-20, float(np.sum(g * g))) / max(1e-20, eu)), 3)
    return fid, sname, out

def prep(a):
    fid, path, work, target = a
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    pk = float(np.max(np.abs(x)))
    if pk <= 0: return fid, False
    sf.write(os.path.join(work, fid + "_hot.wav"), x * (target / pk), sr, subtype="FLOAT")
    return fid, True

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True); ap.add_argument("--probe", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--work", required=True); ap.add_argument("--every", type=int, default=1)
    ap.add_argument("--settings", default=",".join(SETTINGS)); ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--level", type=float, default=-1.0)
    a = ap.parse_args()
    files = json.loads(Path(a.manifest).read_text())["files"][::a.every]
    os.makedirs(a.work, exist_ok=True); out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    with ProcessPoolExecutor(a.jobs) as ex:
        ok = dict(ex.map(prep, [(f["id"], f["path"], a.work, 10 ** (a.level / 20.0)) for f in files]))
        jobs = [(f["id"], f["path"], f.get("range", 1), a.probe, s, SETTINGS[s], a.work) for f in files if ok.get(f["id"]) for s in a.settings.split(",")]
        res = list(ex.map(job, jobs))
    per = {}
    for fid, s, r in res:
        per.setdefault(s, {})[fid] = r
    summ = {}
    for s, rs in per.items():
        v = [r for r in rs.values() if "error" not in r]
        col = lambda k: np.array([r[k] for r in v], dtype=float)
        summ[s] = {"files": len(v),
                   "files_over0_off": int(np.sum(col("over0_off") > 0)), "files_over0_on": int(np.sum(col("over0_on") > 0)),
                   "samples_over0_off": int(np.sum(col("over0_off"))), "samples_over0_on": int(np.sum(col("over0_on"))),
                   "sp_max_off": float(np.max(col("sp_db_off"))), "sp_max_on": float(np.max(col("sp_db_on"))),
                   "tp_max_off": float(np.max(col("tp_db_off"))), "tp_max_on": float(np.max(col("tp_db_on"))),
                   "tp_p95_on": round(float(np.percentile(col("tp_db_on"), 95)), 2),
                   "files_guard_active": int(np.sum(col("gr_db") > 0.0)), "gr_max_db": float(np.max(col("gr_db"))),
                   "gr_median_active_db": round(float(np.median(col("gr_db")[col("gr_db") > 0])), 2) if np.any(col("gr_db") > 0) else 0.0,
                   "touched_pct_median_active": round(float(np.median(col("touched_pct")[col("gr_db") > 0])), 2) if np.any(col("gr_db") > 0) else 0.0,
                   "touched_pct_max": float(np.max(col("touched_pct"))),
                   "rms_change_min_db": float(np.min(col("rms_db")))}
        print(s, json.dumps(summ[s]))
    (out / "per_file.json").write_text(json.dumps(per, indent=0))
    (out / "summary.json").write_text(json.dumps({"probe": a.probe, "every": a.every, "summary": summ}, indent=1))
    for f in Path(a.work).glob("*_hot.wav"):
        f.unlink()

if __name__ == "__main__":
    main()
