# VSTM Audio R&D — Claude Code + JUCE

This is a starter automation lab for autonomous audio-plugin development.

## What is included

- Claude Code skill: `.claude/skills/vstm-audio-intelligence/SKILL.md`
- Project instructions: `CLAUDE.md`
- JUCE offline VST3 host source: `juce-host/`
- WAV signal analysis and A/B comparison: `tools/audio_lab.py`
- pluginval runner: `tools/run_pluginval.py`
- Test configuration template: `config/lab.example.json`

## Important truth about "hearing"

The JUCE host can load a plugin, process real audio offline, change exposed parameters, and render WAV files. The analysis script measures the resulting signal. Neither measurement nor a waveform plot is subjective listening.

Claude Code does not gain human-like hearing merely by installing a skill. For actual timbral/quality judgments, connect an audio-perception model/tool that accepts WAV audio, or provide a human listening checkpoint. This starter deliberately refuses to label measurements as listening. The skill tells Claude to use a real audio-capable tool if one is configured and otherwise report `MEASURED_ONLY`.

## Requirements

- CMake 3.22+
- C++17 compiler
- JUCE source checkout (JUCE 7/8/9 may require small API adjustments)
- Python 3.10+
- `pluginval` (recommended; install separately from Tracktion/pluginval)
- A built VST3 plugin and a test WAV

## Setup

1. Extract this folder into your plugin repository (or copy its `.claude`, `CLAUDE.md`, `juce-host`, and `tools` folders into the repository root).
2. Set `JUCE_DIR` to your JUCE checkout, or pass it to CMake.
3. Build the offline host:

   ```bash
   cmake -S juce-host -B juce-host/build -DJUCE_DIR=/path/to/JUCE
   cmake --build juce-host/build --config Release
   ```

4. Run the host (parameter identifiers must match the plugin's JUCE parameter IDs):

   ```bash
   # macOS/Linux
   ./juce-host/build/VSTMOfflineHost_artefacts/Release/VSTMOfflineHost "/path/to/YourPlugin.vst3" input.wav output.wav 48000 512 "threshold=-18" "ratio=4"

   # Windows (path may be under Release/)
   .\juce-host\build\VSTMOfflineHost_artefacts\Release\VSTMOfflineHost.exe "C:\path\YourPlugin.vst3" input.wav output.wav 48000 512 "threshold=-18"
   ```

5. Analyze the render:

   ```bash
   python tools/audio_lab.py analyze output.wav
   python tools/audio_lab.py compare dry.wav wet.wav
   ```

6. Validate the plugin:

   ```bash
   python tools/run_pluginval.py "/path/to/YourPlugin.vst3" --pluginval /path/to/pluginval
   ```

7. Open Claude Code in the project and invoke `/vstm-audio-intelligence`.

## JUCE host limitations

- First version supports one input WAV, one plugin instance, offline processing, parameter setting by JUCE parameter ID, and WAV output.
- It uses the plugin's main input/output buses and processes MIDI-free audio effects.
- Instruments requiring MIDI need a MIDI-file/input extension before this host can test them.
- Some plugins require a message loop, custom licensing, activation, sidechain setup, or host-specific extensions. If instantiation fails, use a dedicated host/DAW or extend this harness.
- Do not run unknown third-party plugins in a privileged environment.

## Safety defaults

The agent may inspect and build freely, but must not overwrite source audio, delete project files, alter signing/licensing, or commit changes without approval. Render outputs go under `artifacts/audio-runs/` by default. Keep a clean Git baseline before autonomous edits.
