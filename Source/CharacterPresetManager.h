#ifndef SCC_CHARACTER_PRESET_MANAGER_H_INCLUDED
#define SCC_CHARACTER_PRESET_MANAGER_H_INCLUDED

#include <juce_core/juce_core.h>
#include <vector>
#include "SCCCharacterTypes.h"

//==============================================================================
//  CharacterPresetManager  -  karakter presetleri (fabrika + kullanici)
//
//    * Fabrika presetleri kodun icindedir, degistirilemez.
//    * Kullanici presetleri diskte, her biri ayri bir ".scchar" (XML) dosyasidir:
//          macOS   : ~/Library/Application Support/SCC Audio/SCC/Characters
//          Windows : %APPDATA%\SCC Audio\SCC\Characters
//          Linux   : ~/.config/SCC Audio/SCC/Characters
//    * Her preset Character Designer'in BUTUN degerlerini (tip + 15 kontrol) tasir.
//
//  Butun islemler mesaj (arayuz) is parcaciginda yapilir; ses is parcacigi bu
//  sinifa hic dokunmaz. Bir preset yuklendiginde degerler plugin parametrelerine
//  yazilir, dolayisiyla DAW oturumu preset dosyasi silinse bile sesi geri getirir.
//==============================================================================
struct CharacterPreset
{
    juce::String       name;
    bool               factory = false;
    scc::CharacterSpec spec;
};

class CharacterPresetManager
{
public:
    // bos birakilirsa standart kullanici klasoru kullanilir (testler kendi klasorunu verir)
    explicit CharacterPresetManager (const juce::File& userDirectoryOverride = juce::File());

    //--- liste: once fabrika presetleri, sonra kullanici presetleri (ada gore sirali)
    int getNumPresets() const noexcept        { return (int) presets.size(); }
    int getNumFactoryPresets() const noexcept { return numFactory; }
    const CharacterPreset& getPreset (int index) const;
    int indexOf (const juce::String& name) const;       // yoksa -1

    void refresh();                                     // kullanici klasorunu yeniden tara

    //--- kullanici presetleri
    juce::Result saveUserPreset   (const juce::String& name, const scc::CharacterSpec& spec, bool overwriteExisting);
    juce::Result renameUserPreset (const juce::String& oldName, const juce::String& newName);
    juce::Result deleteUserPreset (const juce::String& name);

    bool isFactoryName (const juce::String& name) const;
    juce::File getUserDirectory() const { return userDirectory; }

    //--- yardimcilar
    static juce::String sanitiseName (const juce::String& name);
    static std::unique_ptr<juce::XmlElement> toXml (const juce::String& name, const scc::CharacterSpec& spec);
    static bool fromXml (const juce::XmlElement& xml, juce::String& nameOut, scc::CharacterSpec& specOut);

    static constexpr const char* kFileExtension = ".scchar";

private:
    juce::File fileFor (const juce::String& name) const;
    void addFactoryPresets();

    juce::File userDirectory;
    std::vector<CharacterPreset> presets;
    int numFactory = 0;
};

#endif // SCC_CHARACTER_PRESET_MANAGER_H_INCLUDED
