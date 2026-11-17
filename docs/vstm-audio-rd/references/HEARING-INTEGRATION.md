# Real audio-perception integration

A plugin host and signal-analysis script do not provide subjective hearing. They only produce and measure sound.

To enable an autonomous `LISTENED` stage, configure an audio-capable model/tool that:
1. Accepts the actual rendered WAV bytes (not just a filename or waveform screenshot).
2. Can evaluate non-speech sound, timbre, distortion, transients, pumping, stereo image, pitch artifacts, and musical context.
3. Returns structured observations with timestamps, confidence, and limitations.
4. Receives level-matched dry/wet excerpts and the test settings.
5. Is called through a Claude Code MCP server or another explicit tool integration.

Do not assume a text-only model can hear audio. Do not use speech-to-text as a substitute for critical listening. Do not put API keys in this repository; use environment variables or a local secret manager.

Suggested evaluation prompt for the perception provider:

"You are evaluating an audio plugin render. Compare the supplied dry and wet audio excerpts. Describe only audible evidence. Identify any pumping, transient loss, harshness, fizz, metallic/warbling artifacts, sibilance changes, stereo instability, clipping, noise, or tonal imbalance. Give approximate timestamps and confidence. Distinguish clear defects from subjective preference. Do not infer plugin settings or claim measurements you did not perform."

Until this integration is configured and tested end-to-end, the agent must label its report `MEASURED_ONLY`.

## Status in this repository

A local integration is installed: `tools/ear.py` (Qwen2.5-Omni-7B via llama.cpp), set up by `tools/setup_ear.sh`. It meets points 1, 3 (partially: timestamps are rough) and 4. Point 2 is only partly met: see the calibration in `../EAR.md` for what it does and does not hear. Its output is labelled `LISTENED-MODEL`, never plain `LISTENED`.
