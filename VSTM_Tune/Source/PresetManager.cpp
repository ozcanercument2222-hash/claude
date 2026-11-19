#include "PresetManager.h"
#include "Parameters.h"

namespace vstm
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    using namespace ids;
    // Sound presets: none of them sets Key or Scale (the song's settings). 1.0 had "Hicaz Vocal" and
    // "Chromatic Fix", whose only difference was the scale, and "Robot Voice" / "Trap Melodic" also
    // switched the scale: with presets that keep the key and scale those names would no longer be
    // true. The scales themselves are in the SCALE menu. Sessions that name an old preset keep their
    // values (they are stored separately) and still show the name.
    factory = {
        { "Default Preset",      "",        {} },
        { "Hard Tune",           "Robotic", { { robotism, 0.0f },  { retune, 0.0f },  { humanize, 0.0f } } },
        { "Robot Voice",         "Robotic", { { robotism, 0.0f },  { retune, 0.0f },  { humanize, 0.0f }, { formant, -1.0f } } },
        { "Trap Melodic",        "Robotic", { { robotism, 18.0f }, { retune, 4.0f },  { humanize, 15.0f } } },
        { "Modern Pop",          "Natural", { { robotism, 42.0f }, { retune, 12.0f }, { humanize, 30.0f } } },
        { "Natural Pop",         "Natural", { { robotism, 70.0f }, { retune, 25.0f }, { humanize, 50.0f } } },
        { "R&B Smooth",          "Natural", { { robotism, 64.0f }, { retune, 35.0f }, { humanize, 45.0f }, { vibrato, 115.0f } } },
        { "Subtle Polish",       "Natural", { { robotism, 88.0f }, { retune, 60.0f }, { humanize, 70.0f }, { correction, 80.0f } } },
        { "Vibrato Boost",       "Natural", { { robotism, 80.0f }, { retune, 80.0f }, { humanize, 60.0f }, { vibrato, 170.0f } } },
        { "Deep Voice",          "Effects", { { robotism, 55.0f }, { transpose, -5.0f }, { formant, -2.5f } } },
        { "Octave Down Monster", "Effects", { { robotism, 25.0f }, { retune, 5.0f }, { transpose, -12.0f }, { formant, -4.0f } } },
        { "Chipmunk",            "Effects", { { robotism, 40.0f }, { transpose, 7.0f }, { formant, 4.0f } } },
    };

    refreshUserPresets();
    takeSnapshot();
}

bool PresetManager::isPresetParameter (const juce::String& id)
{
    return id != ids::bypass && id != ids::realtime && id != ids::range;
}

bool PresetManager::isTonalParameter (const juce::String& id)
{
    return id == ids::key || id == ids::scale || id == ids::customScale || id.startsWith ("scalenote");
}

juce::String PresetManager::getFactoryCategory (int index) const
{
    return isFactoryPreset (index) ? factory[(size_t) index].category : juce::String();
}

namespace
{
juce::File& presetFolderOverride()
{
    static juce::File folder;            // set only by tests, before any preset is read
    return folder;
}
} // namespace

void PresetManager::setUserPresetFolderOverride (const juce::File& folder)
{
    presetFolderOverride() = folder;
}

juce::File PresetManager::getUserPresetFolder()
{
    if (presetFolderOverride() != juce::File())
        return presetFolderOverride();
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

void PresetManager::applyValues (const std::vector<Entry>& values, bool includeTonal)
{
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || ! isPresetParameter (rp->paramID))
            continue;
        if (! includeTonal && isTonalParameter (rp->paramID))
            continue;                                              // the song's key and scale stay

        float target = rp->getDefaultValue();
        for (const auto& e : values)
            if (e.id == rp->paramID && std::isfinite (e.value))       // "nan" / "inf" in a damaged file: default
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
        applyValues (factory[(size_t) index].values, ! keepTonal);
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
            applyValues (values, ! keepTonal);
            currentName = userFiles[u].getFileNameWithoutExtension();
        }
    }
    apvts.state.setProperty ("presetName", currentName, nullptr);
    takeSnapshot();
}

void PresetManager::resetAll()
{
    applyValues ({}, true);
    currentName = factory.front().name;
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

bool PresetManager::renameUserPreset (int index, const juce::String& rawName)
{
    const int u = index - getNumFactoryPresets();
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (u < 0 || u >= userFiles.size() || name.isEmpty())
        return false;

    const auto from = userFiles[u];
    const auto to = from.getSiblingFile (name + ".vstmpreset");
    const bool sameFile = to == from;                       // JUCE compares as the file system does (case)
    if (to.exists() && ! sameFile)
        return false;                                       // never overwrite another preset by renaming
    for (int i = 0; i < getNumFactoryPresets(); ++i)
        if (factory[(size_t) i].name.equalsIgnoreCase (name))
            return false;                                   // would be hidden behind a factory name

    auto xml = juce::XmlDocument::parse (from);
    if (xml == nullptr)
        return false;
    xml->setAttribute ("name", name);
    // write the new file first, then remove the old one: a failed write loses nothing
    if (sameFile)
    {
        if (! xml->writeTo (from))
            return false;
        if (from.getFileName() != to.getFileName() && ! from.moveFileTo (to))
            return false;                                   // only the letter case changed
    }
    else
    {
        if (! xml->writeTo (to))
            return false;
        from.deleteFile();
    }

    const bool wasCurrent = currentName == from.getFileNameWithoutExtension();
    refreshUserPresets();
    if (wasCurrent)
    {
        currentName = name;
        apvts.state.setProperty ("presetName", currentName, nullptr);
    }
    return true;
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
        if (std::abs (param->getValue() - value) > 1.0e-4f && ! (keepTonal && isTonalParameter (param->paramID)))
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
