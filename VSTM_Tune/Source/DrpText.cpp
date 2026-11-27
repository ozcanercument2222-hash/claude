#include "DrpText.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vstm::drp::text
{
namespace
{
constexpr char kBinaryMagic[8] = { 'V', 'S', 'T', 'M', 'D', 'R', 'P', 0x1A };

bool validId (const juce::String& id)
{
    if (id.isEmpty() || id.length() > 64)
        return false;
    for (auto c : id)
        if (! ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    return true;
}

/** Strict number: the whole field must be a finite number (no units, no trailing text). */
bool parseNumber (const juce::String& text, float& out)
{
    const auto s = text.trim().toStdString();
    if (s.empty() || s.size() > 40)
        return false;
    char* end = nullptr;
    const double v = std::strtod (s.c_str(), &end);
    if (end == nullptr || *end != '\0' || ! std::isfinite (v) || std::abs (v) > 1.0e9)
        return false;
    out = (float) v;
    return true;
}

bool parseInt (const juce::String& text, int& out)
{
    const auto s = text.trim();
    if (s.isEmpty() || s.length() > 6 || ! s.containsOnly ("0123456789"))
        return false;
    out = s.getIntValue();
    return true;
}

juce::String oneLine (const juce::String& s)
{
    return s.replaceCharacters ("\r\n\t", "   ").trim();
}
} // namespace

juce::String formatNumber (float v)
{
    if (! std::isfinite (v) || std::fpclassify (v) == FP_ZERO)
        return "0";                                         // also -0
    // the shortest text that reads back to the very same float (compared bit for bit: exact round trip); plain
    // decimals ("80", "0.35", "-12.5") for every value a parameter can have, the exponent form only as a last resort.
    // Down to 1e-12 (a tiny value typed into the file, or float noise of a continuous control) up to 24 decimals
    // are tried: enough for the 9 significant digits an exact float needs (1e-6 needed up to 15, 12 were tried).
    auto sameBits = [] (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; };
    char buf[64];
    if (std::abs (v) >= 1.0e-12f && std::abs (v) < 1.0e9f)
        for (int d = 0; d <= 24; ++d)
        {
            std::snprintf (buf, sizeof (buf), "%.*f", d, (double) v);
            if (sameBits (std::strtof (buf, nullptr), v))
                return juce::String (buf);
        }
    for (int p = 1; p <= 9; ++p)
    {
        std::snprintf (buf, sizeof (buf), "%.*g", p, (double) v);
        if (sameBits (std::strtof (buf, nullptr), v))
            break;
    }
    return juce::String (buf);
}

juce::MemoryBlock write (const Document& doc)
{
    juce::String t;
    auto comment = [&t] (const juce::String& line) { t << (line.isEmpty() ? juce::String ("#") : "# " + oneLine (line)) << "\n"; };
    for (const auto& h : doc.header)
        comment (h);
    t << "\n[Preset]\n"
      << "format = " << kFormatTag << "\n"
      << "format_version = " << kFormatVersion << "\n"
      << "plugin = " << kPluginName << "\n"
      << "plugin_version = " << oneLine (doc.pluginVersion) << "\n"
      << "state_version = " << doc.stateVersion << "\n"
      << "name = " << oneLine (doc.name) << "\n"
      << "\n[Parameters]\n";
    for (const auto& p : doc.params)
    {
        t << "\n";
        for (const auto& c : p.comment)
            comment (c);
        t << p.id << " = " << formatNumber (p.value) << "\n";
    }
    if (! doc.footer.isEmpty())
    {
        t << "\n";
        for (const auto& f : doc.footer)
            comment (f);
    }
    const auto utf8 = t.toUTF8();
    return juce::MemoryBlock (utf8.getAddress(), utf8.sizeInBytes() - 1);   // without the terminating 0
}

bool isTextPreset (const void* data, size_t size)
{
    if (data == nullptr || size == 0 || size > kMaxTextSize)
        return false;
    if (size >= sizeof (kBinaryMagic) && std::memcmp (data, kBinaryMagic, sizeof (kBinaryMagic)) == 0)
        return false;
    const auto head = juce::String::fromUTF8 (static_cast<const char*> (data), (int) std::min<size_t> (size, 8192));
    return head.contains (kFormatTag);
}

Error read (const void* data, size_t size, PresetData& out, ReadReport* report)
{
    ReadReport local;
    auto& rep = report != nullptr ? *report : local;
    rep = {};
    out = {};
    if (data == nullptr || size == 0)
        return Error::truncated;
    if (size > kMaxTextSize)
        return Error::tooLarge;
    auto bytes = static_cast<const char*> (data);
    if (size >= 3 && (unsigned char) bytes[0] == 0xEF && (unsigned char) bytes[1] == 0xBB && (unsigned char) bytes[2] == 0xBF)
    {
        bytes += 3;                                         // UTF-8 byte-order mark (e.g. saved by an editor)
        size -= 3;
    }
    if (! juce::CharPointer_UTF8::isValidString (bytes, (int) size) || std::memchr (bytes, 0, size) != nullptr)
        return Error::badMagic;                             // not UTF-8 text

    const auto lines = juce::StringArray::fromLines (juce::String::fromUTF8 (bytes, (int) size));
    juce::String section;
    bool formatSeen = false, versionSeen = false;
    juce::String plugin;
    int version = 0;
    for (int i = 0; i < lines.size(); ++i)
    {
        const auto line = lines[i].trim();
        if (line.isEmpty() || line.startsWithChar ('#') || line.startsWithChar (';'))
            continue;
        if (line.startsWithChar ('[') && line.endsWithChar (']'))
        {
            section = line.substring (1, line.length() - 1).trim().toLowerCase();
            if (section != "preset" && section != "parameters")
            {
                ++rep.unknownSections;
                rep.notes.add ("unknown section [" + section + "] ignored");
            }
            continue;
        }
        const int eq = line.indexOfChar ('=');
        if (eq <= 0)
        {
            ++rep.skippedLines;
            rep.notes.add ("line " + juce::String (i + 1) + ": not key = value");
            continue;
        }
        const auto key = line.substring (0, eq).trim();
        const auto value = line.substring (eq + 1).trim();
        if (section == "preset")
        {
            const auto k = key.toLowerCase();
            if (k == "format")                formatSeen = value == kFormatTag;
            else if (k == "format_version")   versionSeen = parseInt (value, version);
            else if (k == "plugin")           plugin = value;
            else if (k == "name")             out.name = value.substring (0, 128);
            else if (k == "state_version")
            {
                int sv = 0;
                if (parseInt (value, sv) && sv >= 1)
                    out.stateVersion = sv;
            }
            // plugin_version and keys of newer minor versions: information only
        }
        else if (section == "parameters")
        {
            float v = 0.0f;
            const auto id = key.toLowerCase();
            if (! validId (id) || ! parseNumber (value, v))
            {
                ++rep.skippedLines;
                rep.notes.add ("line " + juce::String (i + 1) + ": '" + key.substring (0, 64) + "' skipped (bad ID or number)");
                continue;
            }
            bool duplicate = false;
            for (const auto& e : out.values)
                duplicate = duplicate || e.first == id;
            if (duplicate)
            {
                ++rep.skippedLines;
                rep.notes.add ("line " + juce::String (i + 1) + ": '" + id + "' given twice; the first value is used");
                continue;
            }
            if (out.values.size() >= 4096)
                return Error::badPayload;
            out.values.emplace_back (id, v);
        }
        else if (section.isEmpty())
        {
            ++rep.skippedLines;                             // a value before any section
            rep.notes.add ("line " + juce::String (i + 1) + ": outside a section");
        }
    }
    if (! formatSeen)
        return Error::badMagic;
    if (! plugin.isEmpty() && plugin != kPluginName)
        return Error::wrongPlugin;
    if (! versionSeen || version < 2)
        return Error::badPayload;
    if (version > kFormatVersion)
        return Error::unsupportedVersion;                   // a newer format: refused rather than half-read
    if (out.values.empty())
        rep.notes.add ("no parameter values: every parameter keeps its default");
    return Error::none;
}
} // namespace vstm::drp::text
