#!/usr/bin/env bash
# Installs the local listening stack used by tools/ear.py and tools/audio_view.py:
#   - Python packages for measurement/plots (numpy, scipy, soundfile, librosa, matplotlib, pyloudnorm)
#   - llama.cpp `llama-mtmd-cli`, built from source (static) for this CPU
#   - Qwen2.5-Omni-7B audio-language model (GGUF Q4_K_M + Q8_0 audio projector), SHA-256 verified
#
# llama.cpp source comes from the llama-cpp-python sdist on PyPI, which vendors the
# full llama.cpp tree; this works where github.com downloads are blocked.
# Idempotent: finished steps are skipped. Needs ~7 GB disk, ~11 GB free RAM to run the model.
#
#   tools/setup_ear.sh            install
#   VSTM_EAR_HOME=/path tools/setup_ear.sh
set -euo pipefail

EAR_HOME="${VSTM_EAR_HOME:-$HOME/.cache/vstm-ear}"
LLAMA_PY_VERSION="${VSTM_EAR_LLAMA_PY_VERSION:-0.3.37}"
HF_BASE="https://huggingface.co/ggml-org/Qwen2.5-Omni-7B-GGUF/resolve/main"
MODEL="Qwen2.5-Omni-7B-Q4_K_M.gguf"
MODEL_SHA256="09883dff531dc56923a041c9c99c7c779e26ffde32caa83adeeb7502ec3b50fe"
MMPROJ="mmproj-Qwen2.5-Omni-7B-Q8_0.gguf"
MMPROJ_SHA256="4a7bc5478a2ec8c5d186d63532eb22e75b79ba75ec3c0ce821676157318ef4ad"
JOBS="$(nproc 2>/dev/null || echo 4)"

log() { printf '[setup_ear] %s\n' "$*"; }
mkdir -p "$EAR_HOME/bin" "$EAR_HOME/models" "$EAR_HOME/src"

# 1. Python packages (only installs what is missing)
missing=()
for mod in numpy scipy soundfile librosa matplotlib pyloudnorm; do
  python3 -c "import $mod" 2>/dev/null || missing+=("$mod")
done
if ((${#missing[@]})); then
  log "pip install ${missing[*]}"
  python3 -m pip install -q "${missing[@]}"
else
  log "python packages present"
fi

# 2. llama.cpp
BIN="$EAR_HOME/bin/llama-mtmd-cli"
if [[ -x "$BIN" ]]; then
  log "llama-mtmd-cli present: $BIN"
else
  command -v cmake >/dev/null || { log "cmake is required"; exit 1; }
  SRC="$EAR_HOME/src/llama-cpp-python-$LLAMA_PY_VERSION"
  if [[ ! -d "$SRC/vendor/llama.cpp/tools/mtmd" ]]; then
    log "fetching llama.cpp source (llama-cpp-python $LLAMA_PY_VERSION sdist from PyPI)"
    rm -rf "$EAR_HOME/src/dl" "$SRC"
    python3 -m pip download -q --no-deps --no-binary :all: "llama-cpp-python==$LLAMA_PY_VERSION" -d "$EAR_HOME/src/dl"
    mkdir -p "$SRC"
    tar -xzf "$EAR_HOME/src/dl/llama_cpp_python-$LLAMA_PY_VERSION.tar.gz" -C "$SRC" --strip-components=1
  fi
  GEN=()
  command -v ninja >/dev/null && GEN=(-G Ninja)
  log "building llama-mtmd-cli with $JOBS jobs (several minutes)"
  cmake -S "$SRC/vendor/llama.cpp" -B "$EAR_HOME/src/build-static" "${GEN[@]}" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=ON -DLLAMA_CURL=OFF \
    -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_SERVER=OFF >/dev/null
  cmake --build "$EAR_HOME/src/build-static" -j "$JOBS" --target llama-mtmd-cli >/dev/null
  install -m 755 "$EAR_HOME/src/build-static/bin/llama-mtmd-cli" "$BIN"
  log "installed $BIN"
fi

# 3. Model files
fetch() {
  local name="$1" sha="$2" dest="$EAR_HOME/models/$1"
  if [[ -f "$dest" ]] && echo "$sha  $dest" | sha256sum -c --status; then
    log "$name present (sha256 ok)"
    return
  fi
  log "downloading $name"
  curl -fL --retry 4 -C - -o "$dest.part" "$HF_BASE/$name"
  echo "$sha  $dest.part" | sha256sum -c --status || { log "sha256 mismatch for $name"; rm -f "$dest.part"; exit 1; }
  mv "$dest.part" "$dest"
}
fetch "$MODEL" "$MODEL_SHA256"
fetch "$MMPROJ" "$MMPROJ_SHA256"

log "ready. try: python3 tools/ear.py describe some.wav"
[[ "$EAR_HOME" == "$HOME/.cache/vstm-ear" ]] || log "remember: export VSTM_EAR_HOME=$EAR_HOME"
