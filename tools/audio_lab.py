#!/usr/bin/env python3
"""Dependency-light PCM WAV measurement and A/B comparison. This does not listen."""
import argparse, json, math, struct, wave
from pathlib import Path

def read_pcm(path):
    with wave.open(str(path), "rb") as w:
        ch, width, sr, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(frames)
    if width == 1:
        vals = [(b - 128) / 128.0 for b in raw]
    elif width == 2:
        vals = [v / 32768.0 for v in struct.unpack("<" + "h" * (len(raw)//2), raw)]
    elif width == 3:
        vals=[]
        for i in range(0, len(raw), 3):
            v=int.from_bytes(raw[i:i+3], "little", signed=False)
            if v & 0x800000: v -= 1 << 24
            vals.append(v / 8388608.0)
    elif width == 4:
        vals = [v / 2147483648.0 for v in struct.unpack("<" + "i" * (len(raw)//4), raw)]
    else:
        raise ValueError(f"Unsupported PCM width {width}")
    return {"path": str(path), "channels": ch, "sample_rate": sr, "frames": frames,
            "duration_s": frames/sr, "samples": vals, "width_bytes": width}

def stats(a):
    x=a["samples"]; n=max(1,len(x))
    peak=max((abs(v) for v in x), default=0.0)
    rms=math.sqrt(sum(v*v for v in x)/n)
    db=lambda v: 20*math.log10(v) if v>0 else None
    return {"path":a["path"],"channels":a["channels"],"sample_rate":a["sample_rate"],
            "duration_s":round(a["duration_s"],6),"peak":peak,"peak_dbfs":db(peak),
            "rms":rms,"rms_dbfs":db(rms),"dc_offset":sum(x)/n,
            "clipped_samples":sum(abs(v)>=0.999 for v in x),
            "evidence":"MEASURED — PCM measurements only; no subjective listening or LUFS."}

def main():
    p=argparse.ArgumentParser()
    sub=p.add_subparsers(dest="cmd",required=True)
    pa=sub.add_parser("analyze"); pa.add_argument("wav")
    pc=sub.add_parser("compare"); pc.add_argument("dry"); pc.add_argument("wet")
    args=p.parse_args()
    if args.cmd=="analyze":
        print(json.dumps(stats(read_pcm(args.wav)),indent=2))
    else:
        a,b=read_pcm(args.dry),read_pcm(args.wet)
        sa,sb=stats(a),stats(b)
        same=(a["channels"]==b["channels"] and a["sample_rate"]==b["sample_rate"])
        n=min(len(a["samples"]),len(b["samples"]))
        xa,xb=a["samples"][:n],b["samples"][:n]
        diff=[y-x for x,y in zip(xa,xb)]
        rms=lambda x: math.sqrt(sum(v*v for v in x)/max(1,len(x)))
        result={"dry":sa,"wet":sb,"same_channels_and_sample_rate":same,
                "compared_samples":n,"duration_difference_s":b["duration_s"]-a["duration_s"],
                "rms_difference_db": (20*math.log10(sb["rms"]/sa["rms"]) if sa["rms"] and sb["rms"] else None),
                "sample_aligned_difference_rms":rms(diff),
                "warning":"Not loudness-matched, latency-aligned, or perceptually evaluated. This is not a listening judgment.",
                "evidence":"MEASURED"}
        print(json.dumps(result,indent=2))
if __name__=="__main__": main()
