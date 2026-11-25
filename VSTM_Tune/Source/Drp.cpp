#include "Drp.h"

namespace vstm::drp
{
namespace
{
constexpr uint8_t kMagic[8] = { 'V', 'S', 'T', 'M', 'D', 'R', 'P', 0x1A };
constexpr int kMaxNameBytes = 256;
constexpr int kMaxValues = 256;
constexpr int kMaxIdBytes = 64;

bool isValidId (const uint8_t* s, size_t n) noexcept
{
    if (n == 0 || n > (size_t) kMaxIdBytes)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (! ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_'))
            return false;
    return true;
}
} // namespace

const char* toString (Error e) noexcept
{
    switch (e)
    {
        case Error::none:               return "ok";
        case Error::cannotRead:         return "cannot read the file";
        case Error::tooLarge:           return "file too large";
        case Error::truncated:          return "file truncated";
        case Error::badMagic:           return "not a .drp file";
        case Error::unsupportedVersion: return "unsupported .drp version";
        case Error::wrongType:          return "wrong file type";
        case Error::badLength:          return "length mismatch";
        case Error::badChecksum:        return "checksum mismatch (damaged file)";
        case Error::badPayload:         return "invalid contents";
        case Error::wrongPlugin:        return "made for another plug-in";
        case Error::unknownKey:         return "signed with an unknown key";
        case Error::badSignature:       return "invalid signature";
        case Error::unsupportedRole:    return "unsupported role or entitlements";
        case Error::revoked:            return "revoked";
        case Error::notYetValid:        return "not valid yet";
        case Error::expired:            return "expired";
    }
    return "unknown error";
}

uint32_t crc32 (const void* data, size_t size) noexcept
{
    static const auto table = []
    {
        std::array<uint32_t, 256> t {};
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    const auto* p = static_cast<const uint8_t*> (data);
    for (size_t i = 0; i < size; ++i)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void writeHeader (juce::MemoryBlock& out, FileType type, const juce::MemoryBlock& payload)
{
    Writer w { out };
    w.bytes (kMagic, sizeof (kMagic));
    w.u16 ((uint16_t) kFormatVersion);
    w.u16 ((uint16_t) type);
    w.u32 ((uint32_t) payload.getSize());
    w.u32 (crc32 (payload.getData(), payload.getSize()));
}

Error readHeader (const void* data, size_t size, FileType& type, uint32_t& payloadLength)
{
    if (size > (size_t) kMaxFileSize)
        return Error::tooLarge;
    if (data == nullptr || size < (size_t) kHeaderSize)
        return size >= sizeof (kMagic) && data != nullptr && std::memcmp (data, kMagic, sizeof (kMagic)) != 0
                   ? Error::badMagic : Error::truncated;
    Reader r { static_cast<const uint8_t*> (data), size };
    const auto* magic = r.bytes (sizeof (kMagic));
    if (magic == nullptr || std::memcmp (magic, kMagic, sizeof (kMagic)) != 0)
        return Error::badMagic;
    const uint16_t version = r.u16();
    const uint16_t t = r.u16();
    payloadLength = r.u32();
    const uint32_t crc = r.u32();
    if (version != (uint16_t) kFormatVersion)
        return Error::unsupportedVersion;
    if (t != (uint16_t) FileType::preset && t != (uint16_t) FileType::authorization)
        return Error::badPayload;
    type = (FileType) t;
    if (payloadLength > (uint32_t) kMaxPayload)
        return Error::badLength;
    const size_t expected = (size_t) kHeaderSize + payloadLength + (type == FileType::authorization ? (size_t) kSignatureSize : 0);
    if (size < expected)
        return Error::truncated;
    if (size != expected)
        return Error::badLength;                       // trailing bytes: not what was written
    if (crc32 (static_cast<const uint8_t*> (data) + kHeaderSize, payloadLength) != crc)
        return Error::badChecksum;
    return Error::none;
}

Error readFile (const juce::File& file, juce::MemoryBlock& out)
{
    if (! file.existsAsFile())
        return Error::cannotRead;
    if (file.getSize() > (int64_t) kMaxFileSize)
        return Error::tooLarge;
    std::unique_ptr<juce::FileInputStream> in (file.createInputStream());
    if (in == nullptr || ! in->openedOk())
        return Error::cannotRead;
    out.reset();
    // juce::pointer_sized_int, not ssize_t: MSVC has no ssize_t outside the juce namespace
    in->readIntoMemoryBlock (out, (juce::pointer_sized_int) kMaxFileSize + 1);
    if (out.getSize() > (size_t) kMaxFileSize)
        return Error::tooLarge;
    return Error::none;
}

//------------------------------------------------------------------------------ presets
// Preset payload (gzip-compressed):
//   u32 manufacturer code, u32 plug-in code, u16 state version,
//   u16 name length + UTF-8 name,
//   u16 value count, then per value: u8 id length + ASCII id, f32 value (IEEE 754, little-endian)
juce::MemoryBlock writePreset (const PresetData& preset)
{
    juce::MemoryBlock raw;
    Writer w { raw };
    w.u32 (kManufacturerCode);
    w.u32 (kPluginCode);
    w.u16 ((uint16_t) preset.stateVersion);
    const auto name = preset.name.toRawUTF8();
    const size_t nameBytes = std::min ((size_t) kMaxNameBytes, std::strlen (name));
    w.u16 ((uint16_t) nameBytes);
    w.bytes (name, nameBytes);
    const size_t count = std::min (preset.values.size(), (size_t) kMaxValues);
    w.u16 ((uint16_t) count);
    for (size_t i = 0; i < count; ++i)
    {
        const auto id = preset.values[i].first.toRawUTF8();
        const size_t idBytes = std::min ((size_t) kMaxIdBytes, std::strlen (id));
        w.u8 ((uint8_t) idBytes);
        w.bytes (id, idBytes);
        uint32_t bits;
        const float v = preset.values[i].second;
        std::memcpy (&bits, &v, 4);
        w.u32 (bits);
    }

    juce::MemoryBlock payload;
    {
        juce::MemoryOutputStream mo (payload, false);
        juce::GZIPCompressorOutputStream gz (mo, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        gz.write (raw.getData(), raw.getSize());
        gz.flush();
    }

    juce::MemoryBlock file;
    writeHeader (file, FileType::preset, payload);
    file.append (payload.getData(), payload.getSize());
    return file;
}

Error readPreset (const void* data, size_t size, PresetData& out)
{
    FileType type {};
    uint32_t len = 0;
    if (const auto e = readHeader (data, size, type, len); e != Error::none)
        return e;
    if (type != FileType::preset)
        return Error::wrongType;

    // decompress with a hard output limit (a small file must not expand into gigabytes)
    juce::MemoryBlock raw;
    {
        auto* src = new juce::MemoryInputStream (static_cast<const uint8_t*> (data) + kHeaderSize, len, false);
        juce::GZIPDecompressorInputStream gz (src, true, juce::GZIPDecompressorInputStream::gzipFormat);
        char buf[4096];
        for (;;)
        {
            const int n = gz.read (buf, (int) sizeof (buf));
            if (n <= 0)
                break;
            raw.append (buf, (size_t) n);
            if (raw.getSize() > (size_t) kMaxDecompressed)
                return Error::badPayload;
        }
    }

    Reader r { static_cast<const uint8_t*> (raw.getData()), raw.getSize() };
    const uint32_t manufacturer = r.u32();
    const uint32_t plugin = r.u32();
    const int stateVersion = r.u16();
    if (! r.ok)
        return Error::badPayload;
    if (manufacturer != kManufacturerCode || plugin != kPluginCode)
        return Error::wrongPlugin;

    PresetData p;
    p.stateVersion = stateVersion;
    const size_t nameBytes = r.u16();
    if (! r.ok || nameBytes > (size_t) kMaxNameBytes)
        return Error::badPayload;
    const auto* name = r.bytes (nameBytes);
    if (! r.ok || ! juce::CharPointer_UTF8::isValidString ((const char*) name, (int) nameBytes))
        return Error::badPayload;
    p.name = juce::String::fromUTF8 ((const char*) name, (int) nameBytes);

    const size_t count = r.u16();
    if (! r.ok || count > (size_t) kMaxValues)
        return Error::badPayload;
    p.values.reserve (count);
    for (size_t i = 0; i < count; ++i)
    {
        const size_t idBytes = r.u8();
        const auto* id = r.bytes (idBytes);
        const uint32_t bits = r.u32();
        if (! r.ok || ! isValidId (id, idBytes))
            return Error::badPayload;
        float v;
        std::memcpy (&v, &bits, 4);
        p.values.emplace_back (juce::String::fromUTF8 ((const char*) id, (int) idBytes), v);
    }
    if (! r.atEnd())
        return Error::badPayload;                      // trailing data inside the payload
    out = std::move (p);
    return Error::none;
}
} // namespace vstm::drp
