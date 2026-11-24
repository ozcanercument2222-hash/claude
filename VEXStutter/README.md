# VEX Stutter 0.1.0

Vocal stutter / repeat / gate / reverse / glitch effect. JUCE 8, C++17, VST3 + Standalone
(AU optional on macOS).

## What is in it

- **Modes:** Stutter, Repeat, Gate, Reverse, Glitch (Pixel / Burst / Shuffle + Intensity).
- **Controls:** Amount, Length, Rate (host-synced division or free ms), Style (Sharp, Smooth, Tight,
  Loose, Tape, Random, Digital Glitch), Shape, Smoothness, Offset, Mix, Freeze with a waveform
  selection, an FX chain (delay + reverb on the repeats only), and Bypass.
- **24 automatable parameters** with permanent IDs (`Source/Parameters/ParameterIDs.h`).
- **Presets:** 15 factory presets, plus user presets (Save As, Rename, Delete, Import) as
  validated `.vexpreset` XML files.
- **Interface:** the VEX mockup layout, scalable from 70 to 160 %. The waveform, the IN/OUT meters
  and the step display all show real engine data.
- **DSP:** a JUCE-free engine in `Source/DSP`. It never allocates, locks or does I/O in the audio
  thread, and is click-protected (fades with no corners, 3 ms glides on jumps). It locks to the
  host tempo and bar, and runs on an internal clock when the transport is stopped.
  Amount 0 %, Mix 0 % and Bypass give the input bit for bit.

## Build

You need CMake 3.22+, a C++17 compiler and git. JUCE 8.0.6 is downloaded automatically, or you can
pass a local copy with `-DVEX_JUCE_PATH=...`.

**Windows** (Visual Studio 2022):
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target VEXStutter_VST3
```
Copy `build\VEXStutter_artefacts\Release\VST3\VEX Stutter.vst3` to
`C:\Program Files\Common Files\VST3\`.

**macOS** (Xcode; universal arm64 + x86_64):
```
cmake -S . -B build -G Xcode
cmake --build build --config Release --target VEXStutter_VST3
```
Copy the `.vst3` to `~/Library/Audio/Plug-Ins/VST3/`. For the AU build, add `-DVEX_BUILD_AU=ON`.

**Linux:**
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8 --target VEXStutter_VST3
```
Copy the `.vst3` to `~/.vst3/`.

The `VEXStutter_Standalone` target builds the stand-alone app.

## Tests

```
cmake -S . -B build -DVEX_BUILD_TESTS=ON
cmake --build build -j8
build/VEXStutter_EngineTests
build/VEXStutter_Tests_artefacts/Release/VEXStutter_Tests shots --vst3 "build/VEXStutter_artefacts/Release/VST3/VEX Stutter.vst3"
```
On Linux without a display, prefix the second command with `xvfb-run -a`.

| Test program | What it covers |
|---|---|
| `VEXStutter_EngineTests` | the DSP alone: clean paths, grid timing, modes, clicks, bounds / NaN fuzzing, block-size independence, freeze, FX, CPU |
| `VEXStutter_Tests` | parameters, processing, state, presets, editor (screenshots), and the built VST3 loaded like a host |
| `VEXStutter_HostDemo` | loads the VST3 in a window with real-time audio and a playing transport |

Results on Linux (2026-11-24, MEASURED):
- Engine tests: 53/53, also under AddressSanitizer and UndefinedBehaviorSanitizer.
- Plug-in tests: 71/71, including the hosted VST3.
- pluginval 1.0.4 at strictness 10: SUCCESS (25 test groups).

## Folder structure

```
Source/Parameters   IDs, choices, divisions, parameter layout, state helpers
Source/DSP          CaptureBuffer, StutterEngine, Fx (delay, reverb): JUCE-free
Source/Presets      PresetManager and the factory presets
Source/UI           Theme, icons, controls, waveform view
Source/Plugin*      processor (parameters -> engine, state) and editor (layout)
Resources/fonts     Inter (SIL Open Font License 1.1)
tests/              engine tests, plug-in tests, host demo
docs/               architecture notes
```

## Not tested yet

- Real DAWs (Ableton, FL Studio, Logic, Reaper, Cubase).
- Windows and macOS builds. Only the Linux build was compiled and tested here.
- Listening tests. The factory preset values and the style settings are starting points chosen
  from the DSP behaviour, not by ear.
- HiDPI on Windows, and resizing inside each host.

## Licences

- The Inter font is under the SIL OFL 1.1 (`Resources/fonts/OFL-Inter.txt`).
- JUCE 8 is licensed under AGPLv3 or a commercial licence. A closed-source commercial release
  needs a JUCE commercial licence.
