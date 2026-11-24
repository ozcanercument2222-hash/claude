#!/usr/bin/env python3
"""Variants of a ground-truth corpus (gt_corpus.py) for robustness tests (evidence: MEASURED).

    gt_variants.py --manifest GT/manifest.json --out DIR --kind rate=96000 | gain=-20 | snr=10 | stereo=dup|left|right2
                   [--only id,...] [--group prefix,...] [--limit N]

rate=R     resampled to R Hz (polyphase); the ground truth is on a time grid and is reused as is
gain=G     scaled by G dB (float WAV: no clipping above 0 dBFS)
snr=S      red-ish noise added at S dB below the voiced power (truth unchanged: the voice is still there)
stereo=dup     the voice on both channels (must equal the mono result)
stereo=left    the voice left, silence right
stereo=right2  the voice on both channels, right 6 dB quieter
Writes DIR/<id>.wav and DIR/manifest.json pointing to the original .gt.npz files.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--kind", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--group", default="")
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    man = json.loads(Path(a.manifest).read_text())
    cases = [c for c in man["files"] if not a.only or c["id"] in a.only.split(",")]
    if a.group:
        cases = [c for c in cases if any(c["group"].startswith(g) for g in a.group.split(","))]
    if a.limit:
        cases = cases[: a.limit]
    kind, val = a.kind.split("=")
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(5)
    entries = []
    for c in cases:
        y, sr = sf.read(c["path"], always_2d=True, dtype="float64")
        y = y.mean(axis=1)
        e = dict(c)
        if kind == "rate":
            R = int(val)
            g = math.gcd(R, sr)
            y = signal.resample_poly(y, R // g, sr // g)
            sr = R
        elif kind == "gain":
            y = y * 10 ** (float(val) / 20.0)
        elif kind == "snr":
            gt = np.load(c["gt"])
            v = np.interp(np.arange(len(y)) / sr, gt["t"], (gt["f0"] > 0).astype(float)) > 0.5
            p_s = float(np.mean(y[v] ** 2)) if v.any() else float(np.mean(y ** 2))
            nz = signal.lfilter([1.0], [1.0, -0.9], rng.standard_normal(len(y)))
            y = y + nz * math.sqrt(p_s / float(np.mean(nz ** 2)) / 10 ** (float(val) / 10.0))
        elif kind == "stereo":
            if val == "dup":
                y = np.stack([y, y], axis=1)
            elif val == "left":
                y = np.stack([y, np.zeros_like(y)], axis=1)
            elif val == "right2":
                y = np.stack([y, 0.5 * y], axis=1)
            else:
                sys.exit("stereo=dup|left|right2")
        else:
            sys.exit("kind: rate|gain|snr|stereo")
        wav = out / f"{c['id']}.wav"
        sf.write(str(wav), y.astype(np.float32), sr, subtype="FLOAT")
        e.update({"path": str(wav), "rate": sr, "note": c.get("note", "") + f" [{a.kind}]"})
        entries.append(e)
    (out / "manifest.json").write_text(json.dumps({"kind": man.get("kind", "") + f"+{a.kind}", "evidence": man.get("evidence", ""),
                                                   "files": entries}, indent=1))
    print(f"{len(entries)} cases -> {out / 'manifest.json'}")


if __name__ == "__main__":
    sys.exit(main())
