#!/usr/bin/env python3
import argparse, shutil, subprocess, sys
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("plugin")
p.add_argument("--pluginval", default="pluginval")
p.add_argument("--strictness", type=int, default=8)
p.add_argument("--output-dir", default="artifacts/audio-runs/pluginval")
a=p.parse_args()
exe=shutil.which(a.pluginval) or (a.pluginval if Path(a.pluginval).exists() else None)
if not exe:
    print("ERROR: pluginval executable not found. Install it or pass --pluginval.", file=sys.stderr); sys.exit(2)
Path(a.output_dir).mkdir(parents=True, exist_ok=True)
cmd=[str(exe),"validate","--strictness-level",str(a.strictness),"--output-dir",a.output_dir,str(Path(a.plugin).resolve())]
print("RUN:", " ".join(cmd))
r=subprocess.run(cmd)
print("RESULT:", "PASS" if r.returncode==0 else f"FAIL (exit {r.returncode})")
sys.exit(r.returncode)
