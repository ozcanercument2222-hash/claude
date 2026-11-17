#pragma once
//==============================================================================
//  Fabrika + kullanici presetleri
//
//  Kullanici presetleri:  <kullanici veri klasoru>/VSTM Audio/VSTM Tune/Presets/*.vstmpreset
//  Bypass, REALTIME ve vokal araligi presetlere dahil degildir (genel ayarlar).
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

namespace vstm
{
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    int getNumFactoryPresets() const;
    int getNumPresets() const;                         // factory + user
    juce::String getPresetName (int index) const;
    bool isFactoryPreset (int index) const noexcept    { return index >= 0 && index < getNumFactoryPresets(); }

    void loadPreset (int index);
    void loadNext();
    void loadPrevious();

    /** Saves the current settings as a user preset (overwrites one with the same name). */
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (int index);

    juce::String getCurrentName() const                { return currentName; }
    int getCurrentIndex() const;
    bool isDirty() const;

    /** Called after the host restores a session: keeps the name and marks the restored values as clean. */
    void restoreCurrent (const juce::String& name);
    void refreshUserPresets();

    static juce::File getUserPresetFolder();
    static bool isPresetParameter (const juce::String& paramID);

    /** Tests: keep user presets in another folder (an empty File = the normal location). */
    static void setUserPresetFolderOverride (const juce::File& folder);

private:
    struct Entry { juce::String id; float value; };
    struct Factory { juce::String name; std::vector<Entry> values; };

    void applyValues (const std::vector<Entry>& values);
    void takeSnapshot();

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Factory> factory;
    juce::Array<juce::File> userFiles;
    juce::String currentName { "Default Preset" };
    std::vector<std::pair<juce::RangedAudioParameter*, float>> snapshot;
};
} // namespace vstm
