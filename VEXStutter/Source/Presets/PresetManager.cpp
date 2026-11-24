#include "Presets/PresetManager.h"
#include "Parameters/ParameterIDs.h"
#include "Parameters/StateUtils.h"

#include <algorithm>
#include <cmath>

namespace vex::presets
{
namespace
{
    namespace ids = vex::ids;

    // choice indices (Parameters/Choices.h, Divisions.h)
    enum ModeIx  { stutter = 0, repeat = 1, gate = 2, reverse = 3, glitch = 4 };
    enum StyleIx { sharp = 0, smooth = 1, tight = 2, loose = 3, tape = 4, random = 5, digital = 6 };
    enum ShapeIx { hard = 0, rampUp = 1, rampDown = 2, smoothShape = 3 };
    enum GlitchIx { pixel = 0, burst = 1, shuffle = 2 };
    enum RateIx  { r1_4D = 2, r1_4 = 4, r1_8D = 5, r1_8 = 7, r1_16 = 10, r1_16T = 12, r1_32 = 13 };

    const juce::Identifier kPresetNameKey    { "presetName" };
    const juce::Identifier kPresetFactoryKey { "presetFactory" };

    Preset make (const char* name, const char* category, std::vector<std::pair<juce::String, float>> values)
    {
        Preset p;
        p.name = name;
        p.category = category;
        p.factory = true;
        p.values = std::move (values);
        return p;
    }
}

//==============================================================================
std::vector<Preset> PresetManager::makeFactoryPresets()
{
    // Starting points chosen from the DSP behaviour (INSPECTED); not yet confirmed by listening
    // tests (docs/TEST_REPORT.md). Unlisted parameters take their default.
    return {
        make ("Init", "Basic", {}),
        make ("Clean Stutter", "Stutter", { { ids::amount, 60 }, { ids::smoothness, 30 } }),
        make ("Tight 1/8", "Stutter", { { ids::amount, 100 }, { ids::style, tight }, { ids::smoothness, 20 } }),
        make ("Fast 1/16", "Stutter", { { ids::amount, 100 }, { ids::rate, r1_16 }, { ids::smoothness, 20 } }),
        make ("Vocal Machine Gun", "Stutter", { { ids::amount, 100 }, { ids::rate, r1_32 }, { ids::length, 50 }, { ids::smoothness, 10 } }),
        make ("Soft Repeat", "Repeat", { { ids::mode, repeat }, { ids::amount, 70 }, { ids::length, 100 }, { ids::style, smooth },
                                         { ids::shape, smoothShape }, { ids::smoothness, 70 }, { ids::mix, 80 } }),
        make ("Rhythmic Gate", "Gate", { { ids::mode, gate }, { ids::amount, 100 }, { ids::rate, r1_16 }, { ids::length, 45 },
                                         { ids::smoothness, 25 } }),
        make ("Glitch Burst", "Glitch", { { ids::mode, glitch }, { ids::glitchType, burst }, { ids::intensity, 80 }, { ids::amount, 100 },
                                          { ids::length, 75 }, { ids::style, digital }, { ids::smoothness, 15 } }),
        make ("Pixel Glitch", "Glitch", { { ids::mode, glitch }, { ids::glitchType, pixel }, { ids::intensity, 70 }, { ids::amount, 100 },
                                          { ids::rate, r1_16 }, { ids::length, 75 }, { ids::smoothness, 20 } }),
        make ("Reverse Glitch", "Reverse", { { ids::mode, reverse }, { ids::amount, 75 }, { ids::length, 75 }, { ids::shape, rampUp },
                                             { ids::smoothness, 30 }, { ids::reverbMix, 20 } }),
        make ("Trap Fill", "Fills", { { ids::amount, 100 }, { ids::rate, r1_16T }, { ids::length, 100 }, { ids::style, tight },
                                      { ids::smoothness, 15 }, { ids::delayMix, 15 } }),
        make ("Pop Vocal Fill", "Fills", { { ids::mode, repeat }, { ids::amount, 75 }, { ids::style, smooth }, { ids::smoothness, 45 },
                                           { ids::mix, 70 }, { ids::delayRate, r1_8D }, { ids::delayMix, 20 }, { ids::delayFeedback, 35 },
                                           { ids::reverbMix, 15 } }),
        make ("Ambient Repeat", "Space", { { ids::mode, repeat }, { ids::amount, 80 }, { ids::rate, r1_4 }, { ids::length, 100 },
                                           { ids::style, smooth }, { ids::shape, smoothShape }, { ids::smoothness, 80 }, { ids::mix, 60 },
                                           { ids::delayRate, r1_4D }, { ids::delayMix, 30 }, { ids::delayFeedback, 50 },
                                           { ids::reverbSize, 80 }, { ids::reverbMix, 45 } }),
        make ("Telephone Fragment", "Lo-Fi", { { ids::mode, glitch }, { ids::glitchType, shuffle }, { ids::intensity, 50 }, { ids::amount, 70 },
                                               { ids::rate, r1_16 }, { ids::style, tape }, { ids::smoothness, 30 } }),
        make ("Free 90 ms", "Free Time", { { ids::sync, 0 }, { ids::rateMs, 90 }, { ids::amount, 100 }, { ids::length, 100 },
                                           { ids::smoothness, 20 } }),
    };
}

//==============================================================================
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    presets = makeFactoryPresets();
}

bool PresetManager::isPresetParameter (const juce::String& id)
{
    return id != ids::bypass && id != ids::freeze;
}

juce::File PresetManager::getUserDirectory() const
{
    if (userDirOverride != juce::File())
        return userDirOverride;
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("VEX").getChildFile ("VEX Stutter").getChildFile ("Presets");
}

juce::String PresetManager::cleanName (const juce::String& raw)
{
    auto name = raw.removeCharacters ("\\/:*?\"<>|\t\r\n").trim();
    while (name.startsWith ("."))
        name = name.substring (1).trimStart();
    return name.substring (0, kMaxNameLength).trim();
}

const std::vector<Preset>& PresetManager::getPresets()
{
    if (! scanned)
        rescan();
    return presets;
}

void PresetManager::rescan()
{
    // keep the loaded preset selected across the rescan (by name)
    juce::String name;
    bool factory = true, wasPending = false;
    {
        const juce::ScopedLock sl (lock);
        name = currentName;
        factory = currentFactory;
        wasPending = pending;
    }
    const bool hadCurrent = current >= 0;

    presets = makeFactoryPresets();
    std::vector<Preset> user;
    const auto dir = getUserDirectory();
    if (dir.isDirectory())
    {
        int files = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, juce::String ("*") + kFileExtension, juce::File::findFiles))
        {
            if (++files > 2000)
                break;                                        // a sane upper bound for one folder
            Preset p;
            if (parseFile (entry.getFile(), p))
                user.push_back (std::move (p));
        }
    }
    std::sort (user.begin(), user.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    for (auto& p : user)
        presets.push_back (std::move (p));
    scanned = true;

    current = -1;
    if (hadCurrent || wasPending)
        for (int i = 0; i < (int) presets.size(); ++i)
            if (presets[(size_t) i].factory == factory && presets[(size_t) i].name == name)
                current = i;
    sendChangeMessage();
}

int PresetManager::getCurrentIndex()
{
    resolvePending();
    return current;
}

juce::String PresetManager::getCurrentName() const
{
    const juce::ScopedLock sl (lock);
    return currentName;
}

void PresetManager::resolvePending()
{
    bool wasPending = false;
    {
        const juce::ScopedLock sl (lock);
        wasPending = pending;
    }
    if (! wasPending)
        return;
    rescan();
    const juce::ScopedLock sl (lock);
    pending = false;
}

float PresetManager::presetValue (const Preset& preset, const juce::String& id) const
{
    auto* param = apvts.getParameter (id);
    if (param == nullptr)
        return 0.0f;
    for (const auto& [pid, value] : preset.values)
        if (pid == id)
            return state::legalValue (*param, value);
    return param->convertFrom0to1 (param->getDefaultValue());
}

void PresetManager::apply (const Preset& preset)
{
    for (const char* id : ids::all)
    {
        if (! isPresetParameter (id))
            continue;
        if (auto* param = apvts.getParameter (id))
        {
            const float target = param->convertTo0to1 (presetValue (preset, id));
            if (! juce::exactlyEqual (param->getValue(), target))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (target);
                param->endChangeGesture();
            }
        }
    }
}

bool PresetManager::load (int index)
{
    resolvePending();
    const auto& list = getPresets();
    if (index < 0 || index >= (int) list.size())
        return false;
    const auto preset = list[(size_t) index];       // copy: apply() may trigger callbacks that rescan
    apply (preset);
    current = index;
    {
        const juce::ScopedLock sl (lock);
        currentName = preset.name;
        currentFactory = preset.factory;
        pending = false;
    }
    sendChangeMessage();
    return true;
}

bool PresetManager::step (int delta)
{
    const auto& list = getPresets();
    if (list.empty())
        return false;
    const int n = (int) list.size();
    const int from = getCurrentIndex();
    const int next = from < 0 ? (delta > 0 ? 0 : n - 1) : ((from + delta) % n + n) % n;
    return load (next);
}

bool PresetManager::isModified()
{
    const int index = getCurrentIndex();
    if (index < 0 || index >= (int) presets.size())
        return true;
    const auto& preset = presets[(size_t) index];
    for (const char* id : ids::all)
    {
        if (! isPresetParameter (id))
            continue;
        if (auto* param = apvts.getParameter (id))
        {
            const auto& range = param->getNormalisableRange();
            const float tolerance = 1.0e-4f * (range.end - range.start);
            if (std::abs (state::realValue (*param) - presetValue (preset, id)) > tolerance)
                return true;
        }
    }
    return false;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> PresetManager::createXml (const juce::String& name) const
{
    auto xml = std::make_unique<juce::XmlElement> (kRootTag);
    xml->setAttribute ("formatVersion", kFormatVersion);
    xml->setAttribute ("pluginVersion", VEX_VERSION_STRING);
    xml->setAttribute ("name", name);
    xml->setAttribute ("category", "User");
    for (const char* id : ids::all)
    {
        if (! isPresetParameter (id))
            continue;
        if (auto* param = apvts.getParameter (id))
        {
            auto* node = xml->createNewChildElement (state::kParamType.toString());
            node->setAttribute (state::kIdKey.toString(), id);
            node->setAttribute (state::kValueKey.toString(), (double) state::realValue (*param));
        }
    }
    return xml;
}

bool PresetManager::parseXml (const juce::XmlElement& xml, Preset& out)
{
    if (! xml.hasTagName (kRootTag))
        return false;
    const int version = xml.getIntAttribute ("formatVersion", 0);
    if (version < 1)
        return false;                       // newer versions load what this build knows

    Preset p;
    p.factory = false;
    p.name = cleanName (xml.getStringAttribute ("name"));
    p.category = cleanName (xml.getStringAttribute ("category", "User"));
    if (p.category.isEmpty())
        p.category = "User";

    int children = 0;
    for (auto* child : xml.getChildIterator())
    {
        if (++children > 512)
            return false;
        if (! child->hasTagName (state::kParamType.toString()))
            continue;
        const auto id = child->getStringAttribute (state::kIdKey.toString());
        bool known = false;
        for (const char* k : ids::all)
            known = known || id == k;
        if (! known || ! isPresetParameter (id))
            continue;
        bool seen = false;
        for (const auto& v : p.values)
            seen = seen || v.first == id;
        if (seen)
            continue;                       // the first of duplicate IDs wins
        const double value = state::parseStoredNumber (juce::var (child->getStringAttribute (state::kValueKey.toString())));
        if (std::isfinite (value))
            p.values.emplace_back (id, (float) value);
    }
    out = std::move (p);
    return true;
}

bool PresetManager::parseFile (const juce::File& file, Preset& out)
{
    if (! file.existsAsFile() || file.getSize() <= 0 || file.getSize() > kMaxFileBytes)
        return false;
    juce::XmlDocument doc (file.loadFileAsString());
    const auto xml = doc.getDocumentElement();
    if (xml == nullptr)
        return false;
    Preset p;
    if (! parseXml (*xml, p))
        return false;
    if (p.name.isEmpty())
        p.name = cleanName (file.getFileNameWithoutExtension());
    if (p.name.isEmpty())
        return false;
    p.file = file;
    out = std::move (p);
    return true;
}

bool PresetManager::userPresetExists (const juce::String& name) const
{
    const auto clean = cleanName (name);
    return clean.isNotEmpty() && getUserDirectory().getChildFile (clean + kFileExtension).existsAsFile();
}

juce::Result PresetManager::saveAs (const juce::String& rawName, bool overwrite)
{
    const auto name = cleanName (rawName);
    if (name.isEmpty())
        return juce::Result::fail ("Please enter a preset name.");
    const auto dir = getUserDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory())
        return juce::Result::fail ("The preset folder could not be created:\n" + dir.getFullPathName());
    const auto file = dir.getChildFile (name + kFileExtension);
    if (file.existsAsFile() && ! overwrite)
        return juce::Result::fail ("A user preset called \"" + name + "\" already exists.");

    const auto xml = createXml (name);
    if (! xml->writeTo (file))
        return juce::Result::fail ("The preset could not be written:\n" + file.getFullPathName());

    {
        const juce::ScopedLock sl (lock);
        currentName = name;
        currentFactory = false;
        pending = false;
    }
    current = 0;          // so rescan() looks for it
    rescan();
    return juce::Result::ok();
}

juce::Result PresetManager::rename (int index, const juce::String& rawName)
{
    const auto& list = getPresets();
    if (index < 0 || index >= (int) list.size() || list[(size_t) index].factory)
        return juce::Result::fail ("Factory presets cannot be renamed.");
    const auto name = cleanName (rawName);
    if (name.isEmpty())
        return juce::Result::fail ("Please enter a preset name.");
    const auto oldFile = list[(size_t) index].file;
    const auto newFile = oldFile.getSiblingFile (name + kFileExtension);
    if (newFile != oldFile && newFile.existsAsFile())
        return juce::Result::fail ("A user preset called \"" + name + "\" already exists.");

    auto xml = juce::XmlDocument (oldFile.loadFileAsString()).getDocumentElement();
    if (xml == nullptr || ! xml->hasTagName (kRootTag))
        return juce::Result::fail ("The preset file could not be read.");
    xml->setAttribute ("name", name);
    if (! xml->writeTo (newFile))
        return juce::Result::fail ("The preset could not be written.");
    if (newFile != oldFile)
        oldFile.deleteFile();

    const bool wasCurrent = index == current;
    if (wasCurrent)
    {
        const juce::ScopedLock sl (lock);
        currentName = name;
    }
    rescan();
    return juce::Result::ok();
}

juce::Result PresetManager::remove (int index)
{
    const auto& list = getPresets();
    if (index < 0 || index >= (int) list.size() || list[(size_t) index].factory)
        return juce::Result::fail ("Factory presets cannot be deleted.");
    const auto file = list[(size_t) index].file;
    if (! file.deleteFile())
        return juce::Result::fail ("The preset file could not be deleted.");
    rescan();
    return juce::Result::ok();
}

juce::Result PresetManager::importFile (const juce::File& source)
{
    Preset p;
    if (! parseFile (source, p))
        return juce::Result::fail ("This file is not a valid VEX Stutter preset.");
    const auto dir = getUserDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory())
        return juce::Result::fail ("The preset folder could not be created.");

    // never overwrite: "Name", "Name 2", "Name 3" ...
    auto name = p.name;
    for (int n = 2; dir.getChildFile (name + kFileExtension).existsAsFile() && n < 1000; ++n)
        name = cleanName (p.name.substring (0, kMaxNameLength - 5) + " " + juce::String (n));

    // re-written from the validated values, so nothing unexpected is copied along
    juce::XmlElement xml (kRootTag);
    xml.setAttribute ("formatVersion", kFormatVersion);
    xml.setAttribute ("pluginVersion", VEX_VERSION_STRING);
    xml.setAttribute ("name", name);
    xml.setAttribute ("category", p.category);
    for (const auto& [id, value] : p.values)
    {
        auto* node = xml.createNewChildElement (state::kParamType.toString());
        node->setAttribute (state::kIdKey.toString(), id);
        node->setAttribute (state::kValueKey.toString(), (double) value);
    }
    const auto file = dir.getChildFile (name + kFileExtension);
    if (! xml.writeTo (file))
        return juce::Result::fail ("The preset could not be written.");

    rescan();
    for (int i = 0; i < (int) presets.size(); ++i)
        if (! presets[(size_t) i].factory && presets[(size_t) i].file == file)
            load (i);
    return juce::Result::ok();
}

//==============================================================================
void PresetManager::writeToState (juce::ValueTree& state) const
{
    const juce::ScopedLock sl (lock);
    state.setProperty (kPresetNameKey, currentName, nullptr);
    state.setProperty (kPresetFactoryKey, currentFactory, nullptr);
}

void PresetManager::readFromState (const juce::ValueTree& state)
{
    const auto name = cleanName (state.getProperty (kPresetNameKey, "Init").toString());
    const juce::ScopedLock sl (lock);
    currentName = name.isNotEmpty() ? name : juce::String ("Init");
    currentFactory = (bool) state.getProperty (kPresetFactoryKey, true);
    pending = true;
}
}
