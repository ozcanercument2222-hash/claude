#pragma once
//==============================================================================
//  Fabrika + kullanici presetleri
//
//  Kullanici presetleri:  <kullanici veri klasoru>/VSTM Audio/VSTM Tune/Presets/*.vstmpreset
//  Bypass, REALTIME ve vokal araligi presetlere dahil degildir (genel ayarlar).
//
//  Ton ve dizi sarkinin ayaridir, presetin degil: "Presetler ton ve diziyi korur" acikken
//  (varsayilan) preset yuklemek Key, Scale, ozel dizi ve notalarini DEGISTIRMEZ. Kullanici
//  presetleri bunlari yine dosyaya yazar; secenek kapatilinca 1.0'daki gibi uygulanir.
//  "Tum parametreleri sifirla" (resetAll) her seyi, ton ve dizi dahil, varsayilana dondurur.
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
    /** Menu section of a factory preset: "" (Default), "Robotic", "Natural" or "Effects" (English key). */
    juce::String getFactoryCategory (int index) const;

    void loadPreset (int index);
    void loadNext();
    void loadPrevious();
    /** Every parameter (key and scale included) back to its default; the preset becomes "Default Preset". */
    void resetAll();

    /** Saves the current settings as a user preset (overwrites one with the same name). */
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (int index);
    /** Renames a user preset file (fails if the name is empty or another preset already has it). */
    bool renameUserPreset (int index, const juce::String& newName);

    juce::String getCurrentName() const                { return currentName; }
    int getCurrentIndex() const;
    bool isDirty() const;

    /** true (default): loading a preset leaves Key, Scale and the custom scale as they are. */
    void setKeepTonal (bool keep) noexcept             { keepTonal = keep; }
    bool getKeepTonal() const noexcept                 { return keepTonal.load(); }

    /** Called after the host restores a session: keeps the name and marks the restored values as clean. */
    void restoreCurrent (const juce::String& name);
    void refreshUserPresets();

    static juce::File getUserPresetFolder();
    static bool isPresetParameter (const juce::String& paramID);
    /** Key, Scale, Custom Scale and its 12 notes: the song's settings. */
    static bool isTonalParameter (const juce::String& paramID);

    /** Tests: keep user presets in another folder (an empty File = the normal location). */
    static void setUserPresetFolderOverride (const juce::File& folder);

private:
    struct Entry { juce::String id; float value; };
    struct Factory { juce::String name, category; std::vector<Entry> values; };

    void applyValues (const std::vector<Entry>& values, bool includeTonal);
    void takeSnapshot();

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Factory> factory;
    juce::Array<juce::File> userFiles;
    juce::String currentName { "Default Preset" };
    std::vector<std::pair<juce::RangedAudioParameter*, float>> snapshot;
    std::atomic<bool> keepTonal { true };       // read by getStateInformation (any thread)
};
} // namespace vstm
