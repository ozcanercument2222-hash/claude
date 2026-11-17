#!/usr/bin/env python3
"""Model-based listening: feeds real audio to a local audio-language model
(Qwen2.5-Omni-7B via llama.cpp `llama-mtmd-cli`) and returns its text judgments.

Evidence label: LISTENED-MODEL (<model>). This is an AI model's perception of the
audio it received (16 kHz mono). It is NOT human listening and NOT a measurement.
What it can and cannot hear is recorded by `calibrate`; see docs/vstm-audio-rd/EAR.md.

Method (chosen by calibration, see EAR.md): every clip is judged ON ITS OWN with
short, plain questions. Long checklists ("any distortion, clicks, noise...?") and
two-clip comparisons in one prompt made this model answer "clean" even for heavily
clipped audio, so they are not used. Each clip is also judged in several "views"
(the same audio after 0/10/30 ms of leading silence): a single answer can flip
between 10/10 and 1/10 on such a shift, so only unanimous views count as robust.

  describe   one clip  -> quality score 1-10 + short description (+ optional yes/no question)
  compare    two clips -> each judged separately, then the answers are compared
  calibrate  known degradations of a clean clip -> what this ear detects

Locations (override with env vars):
  VSTM_EAR_HOME    default ~/.cache/vstm-ear  (bin/, models/, cache/)
  VSTM_EAR_BIN     llama-mtmd-cli path
  VSTM_EAR_MODEL   main GGUF;  VSTM_EAR_MMPROJ  audio projector GGUF
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

HOME = Path(os.environ.get("VSTM_EAR_HOME", "~/.cache/vstm-ear")).expanduser()
MODEL_NAME = "Qwen2.5-Omni-7B Q4_K_M (llama.cpp)"
EVIDENCE = f"LISTENED-MODEL ({MODEL_NAME})"
EAR_SR = 16000  # the model's audio encoder works at 16 kHz mono

PROMPT_RATE = ("Audio: <__media__>\nRate the sound quality of this recording from 1 (very poor, "
               "heavily degraded) to 10 (excellent, studio clean). Give the number first, then one "
               "short sentence.")
PROMPT_DESCRIBE = ("Audio: <__media__>\nDescribe the sound quality of this recording in one or two "
                   "sentences. Mention any problems you hear.")
PROMPT_YESNO = "Audio: <__media__>\n{question} Answer yes or no first, then one short sentence."

# Targeted questions per artifact class (used by calibrate; usable with --question).
QUESTIONS = {
    "distortion": "Is the sound in this recording distorted?",
    "clicks": "Do you hear clicks or pops in this recording?",
    "noise": "Is there audible background hiss or noise in this recording?",
    "muffled": "Does this recording sound muffled or dull, as if the high frequencies were removed?",
    "pumping": "Does the volume of this recording pump or fluctuate unnaturally?",
    "metallic": "Does the voice in this recording sound metallic, phasey or robotic?",
    "autotune": "Does the voice in this recording sound robotic or auto-tuned?",
    "thumps": "Do you hear low thumps or bumps when the notes change in this recording?",
}


def find_bin():
    cands = [os.environ.get("VSTM_EAR_BIN"), HOME / "bin" / "llama-mtmd-cli",
             HOME / "src" / "build" / "bin" / "llama-mtmd-cli"]
    for c in cands:
        if c and Path(c).is_file() and os.access(c, os.X_OK):
            return str(c)
    sys.exit("llama-mtmd-cli not found. Run tools/setup_ear.sh or set VSTM_EAR_BIN.")


def find_models():
    m = os.environ.get("VSTM_EAR_MODEL") or str(HOME / "models" / "Qwen2.5-Omni-7B-Q4_K_M.gguf")
    p = os.environ.get("VSTM_EAR_MMPROJ") or str(HOME / "models" / "mmproj-Qwen2.5-Omni-7B-Q8_0.gguf")
    for f in (m, p):
        if not Path(f).is_file():
            sys.exit(f"model file missing: {f}. Run tools/setup_ear.sh or set VSTM_EAR_MODEL/MMPROJ.")
    return m, p


def load_mono(path, start=0.0, dur=None):
    sr = sf.info(str(path)).samplerate
    frames = -1 if dur is None else int(round(dur * sr))
    y, sr = sf.read(str(path), start=int(round(start * sr)), frames=frames,
                    dtype="float64", always_2d=True)
    return y.mean(axis=1), sr


def to_ear(y, sr):
    if sr != EAR_SR:
        g = np.gcd(int(sr), EAR_SR)
        y = signal.resample_poly(y, EAR_SR // g, int(sr) // g)
    return y


def lufs(y, sr):
    import pyloudnorm
    if len(y) < int(0.4 * sr):
        return None
    v = pyloudnorm.Meter(sr).integrated_loudness(y)
    return float(v) if np.isfinite(v) else None


def normalize(y, sr, target=-20.0):
    """Loudness-match to `target` LUFS, keep peaks under -1 dBFS (scaled, never clipped)."""
    l = lufs(y, sr)
    if l is not None:
        y = y * 10 ** ((target - l) / 20.0)
    pk = float(np.max(np.abs(y))) if y.size else 0.0
    if pk > 0.89:
        y = y * (0.89 / pk)
    return y


def prepare(path, start, dur):
    y, sr = load_mono(path, start, dur)
    return normalize(to_ear(y, sr), EAR_SR)


def run_model(audio_path, prompt, n_predict=80, threads=None, timeout=900):
    exe = find_bin()
    model, mmproj = find_models()
    cmd = [exe, "-m", model, "--mmproj", mmproj, "--audio", audio_path, "-p", prompt,
           "--temp", "0", "--seed", "1", "-n", str(n_predict), "-c", "4096",
           "-t", str(threads or os.cpu_count() or 4)]
    t0 = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0:
        raise RuntimeError(f"llama-mtmd-cli exit {r.returncode}:\n{r.stderr[-2000:]}")
    return r.stdout.strip(), round(time.time() - t0, 1)


class Ear:
    """Runs prompts on 16 kHz clips, with an on-disk cache keyed by audio + prompt + model."""

    def __init__(self, n_predict=80, threads=None, use_cache=True):
        self.n_predict, self.threads, self.use_cache = n_predict, threads, use_cache
        self.cache_dir = HOME / "cache"
        self.tmp = tempfile.TemporaryDirectory()

    def ask(self, y, prompt):
        pcm = (np.clip(y, -1, 1) * 32767).astype("<i2")
        key = hashlib.sha256(pcm.tobytes() + prompt.encode() + MODEL_NAME.encode()).hexdigest()[:32]
        cfile = self.cache_dir / f"{key}.json"
        if self.use_cache and cfile.is_file():
            return json.loads(cfile.read_text())
        wav = Path(self.tmp.name) / f"{key}.wav"
        sf.write(str(wav), pcm, EAR_SR, subtype="PCM_16")
        text, secs = run_model(str(wav), prompt, self.n_predict, self.threads)
        out = {"prompt": prompt.split("\n", 1)[1], "response": text, "seconds": secs}
        if self.use_cache:
            self.cache_dir.mkdir(parents=True, exist_ok=True)
            cfile.write_text(json.dumps(out))
        return out

    def rate(self, y):
        r = self.ask(y, PROMPT_RATE)
        return {**r, "score": parse_score(r["response"])}

    def describe(self, y):
        return self.ask(y, PROMPT_DESCRIBE)

    def rate_views(self, y, n):
        rs = [self.rate(v) for v in views(y, n)]
        return {"scores": [r["score"] for r in rs], "responses": [r["response"] for r in rs]}

    def yesno_views(self, y, question, n):
        rs = [self.yesno(v, question) for v in views(y, n)]
        return {"answers": [r["answer"] for r in rs], "responses": [r["response"] for r in rs]}

    def yesno(self, y, question):
        r = self.ask(y, PROMPT_YESNO.format(question=question))
        m = re.match(r"\W*(yes|no)\b", r["response"], re.I)
        return {**r, "answer": m.group(1).lower() if m else None}


VIEW_PADS_MS = (0, 10, 30, 50, 70)


def views(y, n):
    """The same clip with 0/10/30/... ms of leading silence (inaudible change)."""
    return [np.concatenate([np.zeros(int(EAR_SR * ms / 1000)), y]) for ms in VIEW_PADS_MS[:max(1, n)]]


def score_verdict(sa, sb):
    """Robust only if every view of one clip scores >= 2 below every view of the other."""
    if None in sa or None in sb:
        return "inconclusive (unparsed score)"
    if max(sb) <= min(sa) - 2:
        return "b worse (robust)"
    if max(sa) <= min(sb) - 2:
        return "a worse (robust)"
    if len(set(sa)) == 1 and sa == sb:
        return "no difference"
    if len(set(sa)) > 1 or len(set(sb)) > 1:
        return "inconclusive (scores unstable across views)"
    return "no clear difference (<2 points)"


def answer_verdict(aa, ab):
    if all(x == "no" for x in aa) and all(x == "yes" for x in ab):
        return "b: yes, a: no (robust)"
    if all(x == "yes" for x in aa) and all(x == "no" for x in ab):
        return "a: yes, b: no (robust)"
    if len(set(aa)) == 1 and aa == ab:
        return f"no difference (both {aa[0]})"
    return "inconclusive (answers unstable across views)"


def parse_score(text):
    """1-10 score from a reply, without mistaking an echoed '1 to 10' scale for the answer."""
    n = r"(10|[1-9])"
    for pat in (rf"^\W*{n}\b(?!\s*(?:to|-)\s*10)",           # "7. Clear vocal..."
                rf"\b{n}\s*(?:/|out of)\s*10\b",              # "7/10", "7 out of 10"
                rf"\b(?:rate|rating|score|give)\w*\b[^0-9]{{0,25}}\b{n}\b(?!\s*(?:to|-)\s*10)"):
        m = re.search(pat, text, re.I)
        if m:
            return int(m.group(1))
    return None


def resolve_question(q):
    return QUESTIONS.get(q, q) if q else None


def cmd_describe(args):
    ear = Ear(args.n_predict, args.threads, not args.no_cache)
    y = prepare(args.wav, args.start, args.dur)
    out = {"evidence": EVIDENCE, "file": args.wav, "start_s": args.start,
           "duration_s": round(len(y) / EAR_SR, 2), "views": args.views,
           "rating": ear.rate_views(y, args.views), "description": ear.describe(y)}
    q = resolve_question(args.question)
    if q:
        out["question"] = {"text": q, **ear.yesno_views(y, q, args.views)}
    out["caveat"] = ("AI model perception at 16 kHz mono; not human listening. Answers that differ "
                     "across views are not evidence. See EAR.md calibration.")
    print(json.dumps(out, indent=2))


def cmd_compare(args):
    ear = Ear(args.n_predict, args.threads, not args.no_cache)
    ya = prepare(args.a, args.start, args.dur)
    yb = prepare(args.b, args.start, args.dur)
    ra, rb = ear.rate_views(ya, args.views), ear.rate_views(yb, args.views)
    res = {"evidence": EVIDENCE, "a": args.a, "b": args.b, "start_s": args.start,
           "views": args.views, "rating": {"a": ra, "b": rb},
           "verdict": {"score": score_verdict(ra["scores"], rb["scores"])}}
    q = resolve_question(args.question)
    if q:
        qa, qb = ear.yesno_views(ya, q, args.views), ear.yesno_views(yb, q, args.views)
        res["question"] = {"text": q, "a": qa, "b": qb}
        res["verdict"]["question"] = answer_verdict(qa["answers"], qb["answers"])
    if args.describe:
        res["description"] = {"a": ear.describe(ya), "b": ear.describe(yb)}
    res["caveat"] = ("Only '(robust)' verdicts are evidence, and only for artifact classes this ear "
                     "detected in calibration (EAR.md). 'inconclusive' is not a pass.")
    print(json.dumps(res, indent=2))


def degradations(y, sr, seed=0):
    """Known degradations of a clean clip: name -> (signal, targeted question key)."""
    rng = np.random.default_rng(seed)
    pk = np.max(np.abs(y)) or 1.0
    # Inaudible change (an identical clip would just hit the cache): tests answer stability.
    near_null = y * 10 ** (0.2 / 20) + rng.standard_normal(len(y)) * 10 ** (-96 / 20)
    out = {"near-null (+0.2 dB, dither -96 dBFS)": (near_null, None)}
    out["hard clip (+18 dB into clipper)"] = (np.clip(y / pk * 8.0, -1, 1) * pk, "distortion")
    out["mild saturation (tanh, ~1-3% THD)"] = (np.tanh(y / pk * 1.2) / np.tanh(1.2) * pk, "distortion")
    out["bit crush (6-bit)"] = (np.round(y / pk * 32) / 32 * pk, "distortion")
    c = y.copy()
    for i in rng.choice(len(y) - 100, size=12, replace=False):
        c[i:i + 3] += pk * np.array([0.9, -0.7, 0.4])
    out["clicks (12 short impulses)"] = (c, "clicks")
    rms = np.sqrt(np.mean(y ** 2))
    out["hiss (white noise -30 dB re signal RMS)"] = (
        y + rng.standard_normal(len(y)) * rms * 10 ** (-30 / 20), "noise")
    sos = signal.butter(4, 2500, "low", fs=sr, output="sos")
    out["muffled (4th-order lowpass 2.5 kHz)"] = (signal.sosfilt(sos, y), "muffled")
    t = np.arange(len(y)) / sr
    out["pumping (4 Hz gain dip, 10 dB)"] = (
        y * 10 ** (-10 * (0.5 + 0.5 * np.sin(2 * np.pi * 4 * t)) / 20), "pumping")
    try:  # classic pitch-shifter artifact: phasiness / metallic smear
        import librosa
        pv = librosa.effects.pitch_shift(y.astype(np.float32), sr=sr, n_steps=3)
        pv = librosa.effects.pitch_shift(pv, sr=sr, n_steps=-3)
        out["phase-vocoder smear (+3 then -3 semitones)"] = (pv.astype(np.float64)[:len(y)], "metallic")
    except ImportError:
        pass
    return out


def cmd_calibrate(args):
    ear = Ear(args.n_predict, args.threads, not args.no_cache)
    y, sr = load_mono(args.wav, args.start, args.dur)
    cases = degradations(y, sr)
    for spec in args.extra or []:
        name, _, rest = spec.partition("=")
        path, _, qkey = rest.partition("@")
        ye, sre = load_mono(path, args.start, args.dur)
        if sre != sr:
            g = np.gcd(int(sr), int(sre))
            ye = signal.resample_poly(ye, int(sr) // g, int(sre) // g)
        cases[name] = (ye, qkey or "autotune")
    if args.only:
        cases = {k: v for k, v in cases.items() if any(s.lower() in k.lower() for s in args.only)}

    n = args.views
    clean = normalize(to_ear(y, sr), EAR_SR)
    base = ear.rate_views(clean, n)
    log = open(args.log, "a") if args.log else None
    results = []
    for name, (yd, qkey) in cases.items():
        yd = normalize(to_ear(yd, sr), EAR_SR)
        r = ear.rate_views(yd, n)
        row = {"case": name, "scores_clean": base["scores"], "scores_case": r["scores"], "rating": r}
        v = score_verdict(base["scores"], r["scores"])
        if name.startswith("near-null"):
            row["rating_outcome"] = "correct (no change)" if v == "no difference" else f"false alarm: {v}"
        else:
            row["rating_outcome"] = ("detected" if v == "b worse (robust)" else
                                     "unstable" if v.startswith("inconclusive") else "missed")
        if qkey:
            q = QUESTIONS[qkey]
            qc, qd = ear.yesno_views(clean, q, n), ear.yesno_views(yd, q, n)
            row["question"] = {"key": qkey, "text": q, "clean": qc, "case": qd}
            yes = sum(a == "yes" for a in qd["answers"])
            if any(a != "no" for a in qc["answers"]):
                row["question_outcome"] = "unusable (not 'no' on clean)"
            elif yes == len(qd["answers"]):
                row["question_outcome"] = "detected"
            elif yes:
                row["question_outcome"] = f"partial ({yes}/{len(qd['answers'])})"
            else:
                row["question_outcome"] = "missed"
        results.append(row)
        if log:
            log.write(json.dumps(row) + "\n")
            log.flush()
        print(f"[calibrate] {name}: scores {base['scores']}->{r['scores']} {row['rating_outcome']}"
              + (f" | {qkey}: {row['question_outcome']}" if qkey else ""), file=sys.stderr, flush=True)
    summary = {r["case"]: {"rating": f"{r['scores_clean']}->{r['scores_case']} {r['rating_outcome']}",
                           "question": r.get("question_outcome")} for r in results}
    print(json.dumps({"evidence": f"{EVIDENCE} calibration", "clean": args.wav,
                      "clean_rating": base, "summary": summary, "results": results}, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--threads", type=int, default=None)
    p.add_argument("--n-predict", type=int, default=80)
    p.add_argument("--no-cache", action="store_true")
    p.add_argument("--views", type=int, default=3,
                   help="judge each clip this many times with 0/10/30/50/70 ms leading silence; "
                        "only unanimous answers count (default 3; 1 = fast but unreliable)")
    sub = p.add_subparsers(dest="cmd", required=True)
    qhelp = "yes/no question, or a preset key: " + ", ".join(QUESTIONS)

    def window(sp, default_dur):
        sp.add_argument("--start", type=float, default=0.0)
        sp.add_argument("--dur", type=float, default=default_dur,
                        help="excerpt seconds (keep <= 30; shorter is faster)")

    s = sub.add_parser("describe")
    s.add_argument("wav")
    s.add_argument("--question", help=qhelp)
    window(s, 15.0)
    s.set_defaults(fn=cmd_describe)

    s = sub.add_parser("compare")
    s.add_argument("a", help="reference, e.g. dry or previous build")
    s.add_argument("b", help="candidate, e.g. wet or new build")
    s.add_argument("--question", help=qhelp)
    s.add_argument("--describe", action="store_true", help="also get free descriptions")
    window(s, 10.0)
    s.set_defaults(fn=cmd_compare)

    s = sub.add_parser("calibrate")
    s.add_argument("wav", help="clean reference clip")
    s.add_argument("--extra", action="append", metavar="NAME=WAV[@QUESTION_KEY]",
                   help="add a real processed render as a case (default question: autotune)")
    s.add_argument("--only", action="append", help="run only cases whose name contains this")
    s.add_argument("--log", help="append each case result as a JSON line while running")
    window(s, 6.0)
    s.set_defaults(fn=cmd_calibrate)

    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
