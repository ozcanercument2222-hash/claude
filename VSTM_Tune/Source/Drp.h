#pragma once
//==============================================================================
//  VSTM Tune .drp files (1.4): presets and plug-in authorization files
//
//  Layout (all integers little-endian), format version 1:
//
//    offset  size  field
//    0       8     magic  "VSTMDRP" 0x1A
//    8       2     format version (1)
//    10      2     file type: 1 = preset, 2 = authorization
//    12      4     payload length N (<= kMaxPayload)
//    16      4     CRC-32 (IEEE) of the payload
//    20      N     payload (type specific, see Drp.cpp)
//    20+N    64    authorization files only: Ed25519 signature of bytes 0 .. 20+N-1
//
//  A preset payload is gzip-compressed binary data (parameter IDs and float values), so the file is
//  not readable in a text editor. That is NOT security: anyone can decompress it and change values;
//  the CRC only detects damage. Authorization files are protected by the Ed25519 signature, which
//  only the holder of the private key can produce (see Authorization.h). Full description:
//  docs/1.4.0/VSTM_Tune_1.4.0_DRP_Format.md.
//
//  Everything here is message-thread code (file I/O); nothing is called from the audio thread.
//==============================================================================
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vstm::drp
{
constexpr int kFormatVersion = 1;
constexpr int kHeaderSize = 20;
constexpr int kSignatureSize = 64;
constexpr int kMaxPayload = 64 * 1024;                    // a preset needs < 1 KiB
constexpr int kMaxFileSize = kHeaderSize + kMaxPayload + kSignatureSize;
constexpr int kMaxDecompressed = 64 * 1024;               // decompression-bomb limit for preset payloads
constexpr uint32_t kManufacturerCode = 0x5673746d;         // 'Vstm' (CMake PLUGIN_MANUFACTURER_CODE)
constexpr uint32_t kPluginCode = 0x56746e31;               // 'Vtn1' (CMake PLUGIN_CODE)

enum class FileType : uint16_t { preset = 1, authorization = 2 };

enum class Error
{
    none = 0,
    cannotRead,           // the file could not be opened / read
    tooLarge,             // larger than kMaxFileSize
    truncated,            // shorter than its header or its declared length
    badMagic,             // not a .drp file
    unsupportedVersion,   // a newer (or unknown) format version
    wrongType,            // a preset where an authorization file is expected, or the other way round
    badLength,            // declared payload length does not match the file size
    badChecksum,          // CRC mismatch: damaged file
    badPayload,           // payload structure invalid (bounds, encoding, counts)
    wrongPlugin,          // made for another plug-in
    unknownKey,           // authorization: signed with a key this build does not trust
    badSignature,         // authorization: signature invalid (forged or modified)
    unsupportedRole,      // authorization: role / entitlements this build does not know
    revoked,              // authorization: serial on the revocation list
    notYetValid,          // authorization: notBefore in the future
    expired               // authorization: expiry passed
};

/** English description for logs and tests (the UI uses its own translated texts). */
const char* toString (Error e) noexcept;

//------------------------------------------------------------------------------ presets
struct PresetData
{
    juce::String name;                                     // display name stored inside the file
    int stateVersion = 2;                                  // the plug-in state version that wrote it
    std::vector<std::pair<juce::String, float>> values;    // parameter ID -> value in its real units
};

/** Serialises a preset into a complete .drp file. */
juce::MemoryBlock writePreset (const PresetData& preset);

/** Parses and validates a complete .drp preset file. Unknown parameter IDs are kept (the caller
    ignores them); IDs with characters outside [a-z0-9_] or longer than 64 are rejected. */
Error readPreset (const void* data, size_t size, PresetData& out);

//------------------------------------------------------------------------------ common
/** Checks magic, version, type, length and CRC (not the signature); reports the file type. */
Error readHeader (const void* data, size_t size, FileType& type, uint32_t& payloadLength);

/** Reads a whole file, refusing anything larger than kMaxFileSize. */
Error readFile (const juce::File& file, juce::MemoryBlock& out);

uint32_t crc32 (const void* data, size_t size) noexcept;

/** Little helpers used by Drp.cpp and Authorization.cpp. */
struct Writer
{
    juce::MemoryBlock& mb;
    void u8 (uint8_t v)   { mb.append (&v, 1); }
    void u16 (uint16_t v) { const uint8_t b[2] = { (uint8_t) v, (uint8_t) (v >> 8) }; mb.append (b, 2); }
    void u32 (uint32_t v) { const uint8_t b[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; mb.append (b, 4); }
    void u64 (uint64_t v) { u32 ((uint32_t) v); u32 ((uint32_t) (v >> 32)); }
    void bytes (const void* p, size_t n) { mb.append (p, n); }
};

struct Reader
{
    const uint8_t* p;
    size_t size, pos = 0;
    bool ok = true;
    bool need (size_t n) noexcept { if (! ok || pos > size || size - pos < n) ok = false; return ok; }
    uint8_t u8() noexcept   { if (! need (1)) return 0; return p[pos++]; }
    uint16_t u16() noexcept { if (! need (2)) return 0; const uint16_t v = (uint16_t) (p[pos] | (p[pos + 1] << 8)); pos += 2; return v; }
    uint32_t u32() noexcept
    {
        if (! need (4)) return 0;
        const uint32_t v = (uint32_t) p[pos] | ((uint32_t) p[pos + 1] << 8) | ((uint32_t) p[pos + 2] << 16) | ((uint32_t) p[pos + 3] << 24);
        pos += 4;
        return v;
    }
    uint64_t u64() noexcept { const uint64_t lo = u32(); const uint64_t hi = u32(); return lo | (hi << 32); }
    const uint8_t* bytes (size_t n) noexcept { if (! need (n)) return nullptr; const auto* q = p + pos; pos += n; return q; }
    bool atEnd() const noexcept { return ok && pos == size; }
};

/** Writes the 20-byte header for a payload. */
void writeHeader (juce::MemoryBlock& out, FileType type, const juce::MemoryBlock& payload);
} // namespace vstm::drp
