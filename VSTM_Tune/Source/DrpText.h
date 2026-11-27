#pragma once
//==============================================================================
//  VSTM Tune readable .drp presets (1.6): the same preset data as the binary .drp (Drp.h), written as
//  UTF-8 text that a text editor shows as it is, with comments that explain every parameter.
//
//    # comment lines (ignored when reading; also ';')
//    [Preset]
//    format = VSTM-DRP-TEXT
//    format_version = 2
//    plugin = VSTM Tune
//    plugin_version = 1.6.0
//    state_version = 2
//    name = My preset
//
//    [Parameters]
//    # explanation of the next value ...
//    retune = 15
//
//  - Values are the parameters' real units (as in the binary format), written in the shortest form that reads
//    back to the same 32-bit float, so save -> load -> save is exact.
//  - UTF-8 without a byte-order mark (Notepad on Windows 10 / 11 detects UTF-8); a BOM and CRLF line endings
//    are accepted when reading.
//  - Readable is not secret and not tamper-proof: anyone can read and change the values. There is no checksum
//    in the text form (the binary form's CRC only detects damage); nothing about the authorization is written.
//  - Reading is tolerant where it can be: unknown sections and parameter IDs are kept (the caller ignores IDs
//    it does not know), a damaged line is skipped and counted; a missing parameter keeps its default (the
//    caller's job). A file without the [Preset] format line, of another plug-in or of a newer major format
//    version is refused.
//
//  Message-thread code (file I/O); never called from the audio thread.
//==============================================================================
#include "Drp.h"

namespace vstm::drp::text
{
constexpr int kFormatVersion = 2;                          // 1 = the binary .drp (Drp.h)
constexpr const char* kFormatTag = "VSTM-DRP-TEXT";
constexpr const char* kPluginName = "VSTM Tune";
constexpr size_t kMaxTextSize = 512 * 1024;

struct Line
{
    juce::String id;                                       // parameter ID
    float value = 0.0f;                                    // real units
    juce::StringArray comment;                             // comment lines written above it (without "# ")
};

struct Document
{
    juce::String name, pluginVersion;
    int stateVersion = 2;
    juce::StringArray header;                              // comment lines at the top
    std::vector<Line> params;
    juce::StringArray footer;                              // comment lines after the parameters
};

/** The preset as UTF-8 text (no BOM, LF line endings). */
juce::MemoryBlock write (const Document& doc);

/** True when the bytes are a readable .drp (an optional BOM, then text whose [Preset] section says
    format = VSTM-DRP-TEXT). The binary .drp starts with its magic and is never mistaken for text. */
bool isTextPreset (const void* data, size_t size);

struct ReadReport
{
    int skippedLines = 0;                                  // lines that are not a comment, a section or key = number
    int unknownSections = 0;
    juce::StringArray notes;                               // short English descriptions (tests, logs)
};

/** Parses a readable .drp into the same PresetData as the binary reader. */
Error read (const void* data, size_t size, PresetData& out, ReadReport* report = nullptr);

/** Shortest decimal text that reads back to exactly the same float ("15", "0.35", "-12.5"). */
juce::String formatNumber (float v);
} // namespace vstm::drp::text
