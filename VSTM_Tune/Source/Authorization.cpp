#include "Authorization.h"
#include "third_party/monocypher/monocypher-ed25519.h"

namespace vstm
{
namespace
{
// Public keys this build trusts. The matching private key is held by the issuer, offline; it is not in
// this repository or in any plug-in binary. Key id = first 8 bytes of SHA-512(public key).
//   a2f22028b35ac70d: VSTM Tune authorization key 1 (generated 2026-11-24)
const AuthorizationManager::TrustedKey kTrustedKeys[] =
{
    { { 0xa2, 0xf2, 0x20, 0x28, 0xb3, 0x5a, 0xc7, 0x0d },
      { 0xa6, 0x1e, 0x95, 0xf0, 0xfa, 0x0f, 0x78, 0xf3, 0xde, 0xbf, 0xec, 0x17, 0x46, 0x6c, 0x63, 0x15,
        0x9e, 0xc5, 0x5e, 0xa6, 0x69, 0xe8, 0x83, 0xb2, 0x62, 0xbf, 0x6e, 0xc3, 0x18, 0xd7, 0xc0, 0x3d } },
};

// Serials of issued authorization files that must no longer work (a key that leaked is removed from
// kTrustedKeys instead). Empty in 1.4.0.
const uint32_t kRevokedSerials[] = { 0 };     // 0 is never issued (tools/drp_auth.py starts at 1)

constexpr int kMaxLabelBytes = 64;
constexpr size_t kFixedPayload = 44;           // u32 u32 u16 u8 u8 u32 u64 u64 u64 u32

juce::File& storageOverride()
{
    static juce::File folder;
    return folder;
}
} // namespace

AuthorizationManager::AuthorizationManager()
    : trusted (std::begin (kTrustedKeys), std::end (kTrustedKeys)),
      revoked (std::begin (kRevokedSerials), std::end (kRevokedSerials))
{
    refresh();
}

void AuthorizationManager::setStorageFolderOverride (const juce::File& folder)
{
    storageOverride() = folder;
}

juce::File AuthorizationManager::getStoredFile()
{
    const auto base = storageOverride() != juce::File()
                          ? storageOverride()
                          : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                .getChildFile ("VSTM Audio").getChildFile ("VSTM Tune");
    return base.getChildFile ("authorization.drp");
}

std::array<uint8_t, 8> AuthorizationManager::keyIdFor (const uint8_t publicKey[32])
{
    uint8_t h[64];
    crypto_sha512 (h, publicKey, 32);
    std::array<uint8_t, 8> id {};
    std::memcpy (id.data(), h, 8);
    return id;
}

int64_t AuthorizationManager::now() const
{
    return clock ? clock() : (int64_t) (juce::Time::currentTimeMillis() / 1000);
}

juce::MemoryBlock AuthorizationManager::buildSignedPart (const Authorization& a)
{
    juce::MemoryBlock payload;
    drp::Writer w { payload };
    w.u32 (drp::kManufacturerCode);
    w.u32 (drp::kPluginCode);
    w.u16 ((uint16_t) kPayloadVersion);
    w.u8 ((uint8_t) a.role);
    w.u8 (0);
    w.u32 (a.entitlements);
    w.u64 ((uint64_t) a.issuedAt);
    w.u64 ((uint64_t) a.notBefore);
    w.u64 ((uint64_t) a.expiresAt);
    w.u32 (a.serial);
    w.bytes (a.keyId.data(), 8);
    const auto label = a.label.toRawUTF8();
    const size_t n = std::min ((size_t) kMaxLabelBytes, std::strlen (label));
    w.u8 ((uint8_t) n);
    w.bytes (label, n);

    juce::MemoryBlock file;
    drp::writeHeader (file, drp::FileType::authorization, payload);
    file.append (payload.getData(), payload.getSize());
    return file;
}

drp::Error AuthorizationManager::verify (const void* data, size_t size, Authorization& out) const
{
    using drp::Error;
    drp::FileType type {};
    uint32_t len = 0;
    if (const auto e = drp::readHeader (data, size, type, len); e != Error::none)
        return e;
    if (type != drp::FileType::authorization)
        return Error::wrongType;

    const auto* bytes = static_cast<const uint8_t*> (data);
    drp::Reader r { bytes + drp::kHeaderSize, len };
    const uint32_t manufacturer = r.u32();
    const uint32_t plugin = r.u32();
    const int payloadVersion = r.u16();
    Authorization a;
    a.role = (Authorization::Role) r.u8();
    r.u8();                                                  // reserved
    a.entitlements = r.u32();
    a.issuedAt = (int64_t) r.u64();
    a.notBefore = (int64_t) r.u64();
    a.expiresAt = (int64_t) r.u64();
    a.serial = r.u32();
    const auto* kid = r.bytes (8);
    const size_t labelBytes = r.u8();
    const auto* label = r.bytes (labelBytes);
    if (! r.ok || ! r.atEnd() || labelBytes > (size_t) kMaxLabelBytes
        || ! juce::CharPointer_UTF8::isValidString ((const char*) label, (int) labelBytes))
        return Error::badPayload;
    if (manufacturer != drp::kManufacturerCode || plugin != drp::kPluginCode)
        return Error::wrongPlugin;
    if (payloadVersion != kPayloadVersion)
        return Error::unsupportedVersion;
    std::memcpy (a.keyId.data(), kid, 8);
    a.label = juce::String::fromUTF8 ((const char*) label, (int) labelBytes);

    const TrustedKey* key = nullptr;
    for (const auto& k : trusted)
        if (k.id == a.keyId)
            key = &k;
    if (key == nullptr)
        return Error::unknownKey;

    const uint8_t* signature = bytes + drp::kHeaderSize + len;
    if (crypto_ed25519_check (signature, key->publicKey.data(), bytes, (size_t) drp::kHeaderSize + len) != 0)
        return Error::badSignature;

    if (a.role != Authorization::Role::admin || a.entitlements != kAllEntitlements)
        return Error::unsupportedRole;
    for (const auto s : revoked)
        if (s == a.serial)
            return Error::revoked;
    const int64_t t = now();
    if (a.notBefore != 0 && t < a.notBefore)
        return Error::notYetValid;
    if (a.expiresAt != 0 && t >= a.expiresAt)
        return Error::expired;

    out = a;
    return Error::none;
}

drp::Error AuthorizationManager::importFile (const juce::File& file)
{
    juce::MemoryBlock mb;
    if (const auto e = drp::readFile (file, mb); e != drp::Error::none)
        return e;
    return importData (mb.getData(), mb.getSize());
}

drp::Error AuthorizationManager::importData (const void* data, size_t size)
{
    Authorization a;
    if (const auto e = verify (data, size, a); e != drp::Error::none)
        return e;                                            // nothing is written for a rejected file

    const auto dest = getStoredFile();
    if (! dest.getParentDirectory().createDirectory())
        return drp::Error::cannotRead;
    // write a temporary sibling first, then replace: a failed write leaves the old state as it was
    const auto tmp = dest.getSiblingFile ("authorization.drp.tmp");
    if (! tmp.replaceWithData (data, size) || ! tmp.moveFileTo (dest))
    {
        tmp.deleteFile();
        return drp::Error::cannotRead;
    }
    auth = a;
    status = drp::Error::none;
    return drp::Error::none;
}

void AuthorizationManager::refresh()
{
    auth = {};
    juce::MemoryBlock mb;
    status = drp::readFile (getStoredFile(), mb);
    if (status == drp::Error::none)
    {
        Authorization a;
        status = verify (mb.getData(), mb.getSize(), a);
        if (status == drp::Error::none)
            auth = a;
    }
}

bool AuthorizationManager::remove()
{
    const auto f = getStoredFile();
    const bool ok = ! f.exists() || f.deleteFile();
    auth = {};
    status = drp::Error::cannotRead;
    return ok;
}
} // namespace vstm
