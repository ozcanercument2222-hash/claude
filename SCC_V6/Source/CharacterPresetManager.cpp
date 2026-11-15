#include "CharacterPresetManager.h"

namespace
{
    //--------------------------------------------------------------------------
    //  Fabrika karakterleri (CUSTOM zinciri icin tasarlanmis baslangic noktalari)
    //  Sira: harm even odd hbal | sat drive shape mix | trans tatk tround | dens thick | low high
    //--------------------------------------------------------------------------
    struct FactoryCharacter
    {
        const char* name;
        float v[scc::kNumCustomParams];
    };

    const FactoryCharacter kFactory[] =
    {
        //                     harm even  odd hbal   sat drv shp  mix   trn atk rnd   dns thk   low  high
        { "Init (Blank)",    {   0,   0,   0,   0,    0,  0,  0, 100,    0, 50,  0,    0,  0,    0,    0 } },
        { "Default",         {  30,  60,  40,  20,   40, 30, 25, 100,    0, 50,  0,   20,  0,    0,    0 } },
        { "Velvet Even",     {  55, 100,  15,  10,   35, 20, 10, 100,    0, 50, 20,   30, 20,   20,  -20 } },
        { "Odd Edge",        {  50,  10, 100,  35,   55, 40, 55, 100,   15, 35,  0,   15,  0,  -20,   15 } },
        { "Vintage Glue",    {  25,  60,  40,  10,   70, 35, 15,  80,    0, 55, 35,   45, 10,   15,  -10 } },
        { "Thick Body",      {  30,  80,  30,   0,   40, 25, 20, 100,    0, 50,  0,   35, 80,   50,    0 } },
        { "Air Polish",      {  35,  60,  60,  75,   15, 10, 10, 100,   10, 30,  0,   10,  0,    0,  -30 } },
        { "Punch",           {  15,  50,  50,  10,   45, 30, 40, 100,   60, 35,  0,   10,  0,    0,    0 } },
        { "Smooth Operator", {  20,  70,  30,   0,   50, 25,  0, 100,  -40, 60, 60,   25, 10,    0,   40 } },
        { "Dense Radio",     {  30,  30,  70,  20,   75, 55, 35, 100,    0, 45, 40,   80, 25,    0,    0 } },
        { "Crunch",          {  40,  40,  80,  40,   90, 80, 75,  70,   20, 40,  0,   10,  0,  -40,  -10 } },
        { "Parallel Grit",   {   0,   0,   0,   0,  100,100, 90,  30,    0, 50,  0,   20,  0,  -60,    0 } },
    };
}

//==============================================================================
CharacterPresetManager::CharacterPresetManager (const juce::File& userDirectoryOverride)
{
    if (userDirectoryOverride != juce::File())
    {
        userDirectory = userDirectoryOverride;
    }
    else
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif
        userDirectory = base.getChildFile ("SCC Audio").getChildFile ("SCC").getChildFile ("Characters");
    }

    refresh();
}

void CharacterPresetManager::addFactoryPresets()
{
    for (const auto& f : kFactory)
    {
        CharacterPreset p;
        p.name    = f.name;
        p.factory = true;
        p.spec.type = scc::kCharCustom;
        for (int i = 0; i < scc::kNumCustomParams; ++i)
            p.spec.custom[i] = f.v[i];
        presets.push_back (p);
    }
    numFactory = (int) presets.size();
}

void CharacterPresetManager::refresh()
{
    presets.clear();
    addFactoryPresets();

    if (! userDirectory.isDirectory())
        return;

    juce::Array<juce::File> files;
    userDirectory.findChildFiles (files, juce::File::findFiles, false, juce::String ("*") + kFileExtension);

    std::vector<CharacterPreset> user;
    for (const auto& file : files)
    {
        if (auto xml = juce::XmlDocument::parse (file))
        {
            CharacterPreset p;
            if (fromXml (*xml, p.name, p.spec))
            {
                // gorunen ad her zaman dosya adidir (dosya elle yeniden adlandirilmis olabilir)
                p.name = file.getFileNameWithoutExtension();
                if (! isFactoryName (p.name))
                    user.push_back (p);
            }
        }
    }

    std::sort (user.begin(), user.end(), [] (const CharacterPreset& a, const CharacterPreset& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });

    for (auto& p : user)
        presets.push_back (p);
}

const CharacterPreset& CharacterPresetManager::getPreset (int index) const
{
    jassert (! presets.empty());
    return presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, index)];
}

int CharacterPresetManager::indexOf (const juce::String& name) const
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name.equalsIgnoreCase (name))
            return (int) i;
    return -1;
}

bool CharacterPresetManager::isFactoryName (const juce::String& name) const
{
    for (int i = 0; i < numFactory; ++i)
        if (presets[(size_t) i].name.equalsIgnoreCase (name))
            return true;
    return false;
}

//==============================================================================
juce::String CharacterPresetManager::sanitiseName (const juce::String& name)
{
    // dosya adinda sorun cikaracak karakterleri at, bas / son bosluklari ve noktalari temizle
    auto n = name.removeCharacters ("\\/:*?\"<>|\r\n\t").trim();
    while (n.endsWithChar ('.'))
        n = n.dropLastCharacters (1).trim();
    return n.substring (0, 48).trim();
}

juce::File CharacterPresetManager::fileFor (const juce::String& name) const
{
    return userDirectory.getChildFile (name + kFileExtension);
}

std::unique_ptr<juce::XmlElement> CharacterPresetManager::toXml (const juce::String& name, const scc::CharacterSpec& spec)
{
    auto xml = std::make_unique<juce::XmlElement> ("SCC_CHARACTER");
    xml->setAttribute ("version", 1);
    xml->setAttribute ("name", name);
    xml->setAttribute ("type", juce::String (scc::characterTypeName (spec.type)));
    for (int i = 0; i < scc::kNumCustomParams; ++i)
        xml->setAttribute (scc::customParamInfo (i).id, (double) spec.custom[i]);
    return xml;
}

bool CharacterPresetManager::fromXml (const juce::XmlElement& xml, juce::String& nameOut, scc::CharacterSpec& specOut)
{
    if (! xml.hasTagName ("SCC_CHARACTER"))
        return false;

    nameOut = xml.getStringAttribute ("name");

    scc::CharacterSpec spec;                              // eksik alanlar varsayilan degerinde kalir
    spec.type = scc::kCharCustom;
    const auto typeName = xml.getStringAttribute ("type", "Custom");
    for (int t = 0; t < scc::kNumCharacterTypes; ++t)
        if (typeName.equalsIgnoreCase (scc::characterTypeName (t)))
            spec.type = t;

    for (int i = 0; i < scc::kNumCustomParams; ++i)
    {
        const auto& info = scc::customParamInfo (i);
        const double v = xml.getDoubleAttribute (info.id, (double) info.defaultValue);
        spec.custom[i] = juce::jlimit (info.minValue, info.maxValue, std::isfinite (v) ? (float) v : info.defaultValue);
    }

    specOut = spec;
    return true;
}

//==============================================================================
juce::Result CharacterPresetManager::saveUserPreset (const juce::String& rawName, const scc::CharacterSpec& spec, bool overwriteExisting)
{
    const auto name = sanitiseName (rawName);
    if (name.isEmpty())
        return juce::Result::fail ("Please enter a name.");
    if (isFactoryName (name))
        return juce::Result::fail ("\"" + name + "\" is a factory character. Choose another name.");

    if (! userDirectory.isDirectory() && ! userDirectory.createDirectory().wasOk())
        return juce::Result::fail ("Could not create the preset folder.");

    const auto file = fileFor (name);
    if (file.existsAsFile() && ! overwriteExisting)
        return juce::Result::fail ("\"" + name + "\" already exists.");

    if (! toXml (name, spec)->writeTo (file))
        return juce::Result::fail ("Could not write the preset file.");

    refresh();
    return juce::Result::ok();
}

juce::Result CharacterPresetManager::renameUserPreset (const juce::String& oldName, const juce::String& rawNewName)
{
    const auto newName = sanitiseName (rawNewName);
    if (newName.isEmpty())
        return juce::Result::fail ("Please enter a name.");
    if (isFactoryName (oldName))
        return juce::Result::fail ("Factory characters cannot be renamed.");
    if (isFactoryName (newName))
        return juce::Result::fail ("\"" + newName + "\" is a factory character. Choose another name.");

    const auto from = fileFor (oldName);
    const auto to   = fileFor (newName);
    if (! from.existsAsFile())
        return juce::Result::fail ("\"" + oldName + "\" was not found.");
    if (from == to)
        return juce::Result::ok();
    if (to.existsAsFile() && ! newName.equalsIgnoreCase (oldName))
        return juce::Result::fail ("\"" + newName + "\" already exists.");

    // icindeki adi da guncelleyerek yeni dosyaya yaz, sonra eskisini sil
    scc::CharacterSpec spec;
    juce::String ignored;
    auto xml = juce::XmlDocument::parse (from);
    if (xml == nullptr || ! fromXml (*xml, ignored, spec))
        return juce::Result::fail ("The preset file could not be read.");
    if (! toXml (newName, spec)->writeTo (to))
        return juce::Result::fail ("Could not write the preset file.");
    if (to != from)
        from.deleteFile();

    refresh();
    return juce::Result::ok();
}

juce::Result CharacterPresetManager::deleteUserPreset (const juce::String& name)
{
    if (isFactoryName (name))
        return juce::Result::fail ("Factory characters cannot be deleted.");

    const auto file = fileFor (name);
    if (! file.existsAsFile())
        return juce::Result::fail ("\"" + name + "\" was not found.");
    if (! file.deleteFile())
        return juce::Result::fail ("Could not delete the preset file.");

    refresh();
    return juce::Result::ok();
}
