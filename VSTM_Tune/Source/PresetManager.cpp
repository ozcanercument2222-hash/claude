#include "PresetManager.h"
#include "Parameters.h"

namespace vstm
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    using namespace ids;
    factory = {
        { "Default Preset",    {} },
        { "Hard Tune",         { { robotism, 0.0f },  { retune, 0.0f },  { humanize, 0.0f } } },
        { "Robot Voice",       { { robotism, 0.0f },  { retune, 0.0f },  { humanize, 0.0f }, { scale, (float) kChromatic }, { formant, -1.0f } } },
        { "Trap Melodic",      { { robotism, 18.0f }, { retune, 4.0f },  { humanize, 15.0f }, { scale, (float) kMinor } } },
        { "Modern Pop",        { { robotism, 42.0f }, { retune, 12.0f }, { humanize, 30.0f } } },
        { "Natural Pop",       { { robotism, 70.0f }, { retune, 25.0f }, { humanize, 50.0f } } },
        { "R&B Smooth",        { { robotism, 64.0f }, { retune, 35.0f }, { humanize, 45.0f }, { vibrato, 115.0f } } },
        { "Subtle Polish",     { { robotism, 88.0f }, { retune, 60.0f }, { humanize, 70.0f }, { correction, 80.0f } } },
        { "Hicaz Vocal",       { { robotism, 45.0f }, { retune, 20.0f }, { humanize, 40.0f }, { scale, (float) kHicaz } } },
        { "Deep Voice",        { { robotism, 55.0f }, { transpose, -5.0f }, { formant, -2.5f } } },
        { "Octave Down Monster", { { robotism, 25.0f }, { retune, 5.0f }, { transpose, -12.0f }, { formant, -4.0f } } },
        { "Chipmunk",          { { robotism, 40.0f }, { transpose, 7.0f }, { formant, 4.0f } } },
        { "Vibrato Boost",     { { robotism, 80.0f }, { retune, 80.0f }, { humanize, 60.0f }, { vibrato, 170.0f } } },
        { "Chromatic Fix",     { { robotism, 60.0f }, { retune, 20.0f }, { scale, (float) kChromatic } } },
    };

    refreshUserPresets();
    takeSnapshot();
}

bool PresetManager::isPresetParameter (const juce::String& id)
{
    return id != ids::bypass && id != ids::realtime && id != ids::range;
}

juce::File PresetManager::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VSTM Audio").getChildFile ("VSTM Tune").getChildFile ("Presets");
}

void PresetManager::refreshUserPresets()
{
    userFiles.clear();
    const auto folder = getUserPresetFolder();
    if (folder.isDirectory())
    {
        userFiles = folder.findChildFiles (juce::File::findFiles, false, "*.vstmpreset");
        std::sort (userFiles.begin(), userFiles.end(),
                   [] (const juce::File& a, const juce::File& b) { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    }
}

int PresetManager::getNumFactoryPresets() const { return (int) factory.size(); }
int PresetManager::getNumPresets() const        { return getNumFactoryPresets() + userFiles.size(); }

juce::String PresetManager::getPresetName (int index) const
{
    if (isFactoryPreset (index))
        return factory[(size_t) index].name;
    const int u = index - getNumFactoryPresets();
    if (u >= 0 && u < userFiles.size())
        return userFiles[u].getFileNameWithoutExtension();
    return {};
}

int PresetManager::getCurrentIndex() const
{
    for (int i = 0; i < getNumPresets(); ++i)
        if (getPresetName (i) == currentName)
            return i;
    return -1;
}

void PresetManager::applyValues (const std::vector<Entry>& values)
{
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || ! isPresetParameter (rp->paramID))
            continue;

        float target = rp->getDefaultValue();
        for (const auto& e : values)
            if (e.id == rp->paramID)
                target = rp->convertTo0to1 (e.value);

        rp->beginChangeGesture();
        rp->setValueNotifyingHost (target);
        rp->endChangeGesture();
    }
}

void PresetManager::loadPreset (int index)
{
    if (isFactoryPreset (index))
    {
        applyValues (factory[(size_t) index].values);
        currentName = factory[(size_t) index].name;
    }
    else
    {
        const int u = index - getNumFactoryPresets();
        if (u < 0 || u >= userFiles.size())
            return;

        if (auto xml = juce::XmlDocument::parse (userFiles[u]))
        {
            std::vector<Entry> values;
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                values.push_back ({ e->getStringAttribute ("id"), (float) e->getDoubleAttribute ("value") });
            applyValues (values);
            currentName = userFiles[u].getFileNameWithoutExtension();
        }
    }
    apvts.state.setProperty ("presetName", currentName, nullptr);
    takeSnapshot();
}

void PresetManager::loadNext()
{
    const int n = getNumPresets();
    if (n > 0)
        loadPreset ((getCurrentIndex() + 1 + n) % n);
}

void PresetManager::loadPrevious()
{
    const int n = getNumPresets();
    if (n > 0)
    {
        const int cur = getCurrentIndex();
        loadPreset (cur < 0 ? n - 1 : (cur - 1 + n) % n);
    }
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;

    juce::XmlElement xml ("VSTMTunePreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isPresetParameter (rp->paramID))
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", rp->paramID);
                e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
            }

    const auto file = folder.getChildFile (name + ".vstmpreset");
    if (! xml.writeTo (file))
        return false;

    refreshUserPresets();
    currentName = name;
    apvts.state.setProperty ("presetName", currentName, nullptr);
    takeSnapshot();
    return true;
}

bool PresetManager::deleteUserPreset (int index)
{
    const int u = index - getNumFactoryPresets();
    if (u < 0 || u >= userFiles.size())
        return false;
    const bool ok = userFiles[u].deleteFile();
    refreshUserPresets();
    return ok;
}

void PresetManager::takeSnapshot()
{
    snapshot.clear();
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isPresetParameter (rp->paramID))
                snapshot.emplace_back (rp, rp->getValue());
}

bool PresetManager::isDirty() const
{
    for (const auto& [param, value] : snapshot)
        if (std::abs (param->getValue() - value) > 1.0e-4f)
            return true;
    return false;
}

void PresetManager::restoreCurrent (const juce::String& name)
{
    currentName = name.isNotEmpty() ? name : juce::String ("Default Preset");
    refreshUserPresets();
    takeSnapshot();
}
} // namespace vstm
