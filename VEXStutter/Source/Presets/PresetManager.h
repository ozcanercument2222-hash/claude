#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>
#include <vector>

namespace vex::presets
{
/** One preset: values in real parameter units (%, ms, choice index, 0 / 1). Parameters a preset does
    not list take their default. */
struct Preset
{
    juce::String name, category;
    bool factory = true;
    juce::File file;                                        // user presets only
    std::vector<std::pair<juce::String, float>> values;
};

/** Factory and user presets (specification section 17).

    Threads: message thread only, except readFromState / writeToState, which the host may call
    from another (non-audio) thread; the few fields they share are guarded by a lock that the
    audio thread never takes. Presets never set Bypass or Freeze: those are performance controls.

    User preset files (".vexpreset", XML) live in the user's application-data folder:
        macOS   ~/Library/Application Support/VEX/VEX Stutter/Presets
        Windows %APPDATA%\\VEX\\VEX Stutter\\Presets
        Linux   ~/.config/VEX/VEX Stutter/Presets
    Every file is untrusted: size-limited, parsed with JUCE's XML parser, unknown IDs ignored,
    values validated and clamped like a session state. A malformed file is skipped, never loaded.
*/
class PresetManager final : public juce::ChangeBroadcaster
{
public:
    static constexpr int kFormatVersion = 1;
    static constexpr const char* kFileExtension = ".vexpreset";
    static constexpr const char* kRootTag = "VEXStutterPreset";
    static constexpr juce::int64 kMaxFileBytes = 256 * 1024;
    static constexpr int kMaxNameLength = 48;

    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    /** Factory presets, then the user's (sorted by name). rescan() re-reads the user folder. */
    const std::vector<Preset>& getPresets();
    void rescan();

    bool load (int index);
    bool loadNext()      { return step (+1); }
    bool loadPrevious()  { return step (-1); }

    /** Index into getPresets() of the loaded preset, or -1 (none, or deleted). */
    int getCurrentIndex();
    juce::String getCurrentName() const;
    /** True when the parameters no longer match the loaded preset. */
    bool isModified();

    juce::Result saveAs (const juce::String& name, bool overwrite);
    juce::Result rename (int index, const juce::String& newName);
    juce::Result remove (int index);
    juce::Result importFile (const juce::File& file);
    bool userPresetExists (const juce::String& name) const;
    juce::File getUserDirectory() const;
    /** Uses `dir` instead of the standard folder (tests, portable installs). */
    void setUserDirectory (const juce::File& dir) { userDirOverride = dir; scanned = false; }

    /** Session state: which preset is loaded (its name and whether it is a factory preset). */
    void writeToState (juce::ValueTree& state) const;
    void readFromState (const juce::ValueTree& state);

    /** A name usable as a file name: no path or reserved characters, trimmed, at most 48 characters. */
    static juce::String cleanName (const juce::String& raw);
    /** Parameters a preset sets: every parameter except Bypass and Freeze. */
    static bool isPresetParameter (const juce::String& id);
    /** Reads a preset file; false (and `out` untouched) for anything malformed. */
    static bool parseFile (const juce::File& file, Preset& out);
    static bool parseXml (const juce::XmlElement& xml, Preset& out);
    /** The current settings as a preset document. */
    std::unique_ptr<juce::XmlElement> createXml (const juce::String& name) const;

    static std::vector<Preset> makeFactoryPresets();

private:
    bool step (int delta);
    void apply (const Preset&);
    float presetValue (const Preset&, const juce::String& id) const;
    void resolvePending();

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Preset> presets;
    bool scanned = false;
    int current = 0;                       // "Init"
    juce::File userDirOverride;

    // shared with readFromState / writeToState
    mutable juce::CriticalSection lock;
    juce::String currentName { "Init" };
    bool currentFactory = true;
    bool pending = false;                  // a session named a preset: find it on the message thread
};
}
