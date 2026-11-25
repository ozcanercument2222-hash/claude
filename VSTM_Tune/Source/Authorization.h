#pragma once
//==============================================================================
//  VSTM Tune plug-in authorization (1.4)
//
//  An authorization file is a .drp file of type 2, signed with Ed25519 (RFC 8032). The plug-in only
//  contains PUBLIC keys (kTrustedKeys in Authorization.cpp); the issuer keeps the private key offline
//  and signs files with tools/drp_auth.py. What it unlocks is inside VSTM Tune only: the developer
//  panel and the expert engine switches. It never grants operating-system, host or file-system rights,
//  and nothing in it is executed.
//
//  Verification (in this order; the first failure is reported):
//    header (magic, version, type 2, length, CRC) -> payload structure -> plug-in codes ->
//    payload version -> trusted key (key id) -> Ed25519 signature over header + payload ->
//    role / entitlements -> revocation list (serial) -> notBefore / expiry (system clock).
//  A name, an extension or a marker inside the file is never enough: without a valid signature from a
//  trusted key the file is rejected.
//
//  Storage: a successfully verified import is copied byte for byte to
//  <user application data>/VSTM Audio/VSTM Tune/authorization.drp (that one file only), and verified
//  again whenever the plug-in starts or refresh() is called, so a modified or expired copy stops
//  working. remove() deletes that file. No personal data is collected or stored.
//
//  Message thread only.
//==============================================================================
#include "Drp.h"
#include <functional>

namespace vstm
{
struct Authorization
{
    enum class Role : uint8_t { none = 0, admin = 1 };
    Role role = Role::none;
    uint32_t entitlements = 0;
    int64_t issuedAt = 0, notBefore = 0, expiresAt = 0;   // Unix seconds (UTC); 0 = not set
    uint32_t serial = 0;
    std::array<uint8_t, 8> keyId {};
    juce::String label;
};

class AuthorizationManager
{
public:
    static constexpr int kPayloadVersion = 1;
    /** Admin = every admin feature of this plug-in, present and future (the only role of version 1). */
    static constexpr uint32_t kAllEntitlements = 0xFFFFFFFFu;

    struct TrustedKey
    {
        std::array<uint8_t, 8> id;
        std::array<uint8_t, 32> publicKey;
    };

    AuthorizationManager();

    /** Verifies a complete authorization file. */
    drp::Error verify (const void* data, size_t size, Authorization& out) const;

    /** Verifies the file and, if it is valid, stores a copy and becomes authorized. Only the stored
        authorization file is written; nothing else on disk is touched. */
    drp::Error importFile (const juce::File& file);
    drp::Error importData (const void* data, size_t size);

    /** Reads and verifies the stored copy again (expiry, revocation, tampering). */
    void refresh();
    /** Deletes the stored copy and drops the authorization. */
    bool remove();

    bool isAdmin() const noexcept               { return status == drp::Error::none && auth.role == Authorization::Role::admin; }
    const Authorization& current() const noexcept { return auth; }
    /** Why the stored copy is not valid (none if it is, cannotRead if there is none). */
    drp::Error storedStatus() const noexcept    { return status; }

    static juce::File getStoredFile();
    static std::array<uint8_t, 8> keyIdFor (const uint8_t publicKey[32]);

    /** Header + payload (the signed bytes) for an authorization; used by the tests to make files. */
    static juce::MemoryBlock buildSignedPart (const Authorization& a);

    //---------------------------------------------------------------- tests only
    static void setStorageFolderOverride (const juce::File& folder);
    void setTrustedKeysForTesting (std::vector<TrustedKey> keys)   { trusted = std::move (keys); }
    void setRevokedSerialsForTesting (std::vector<uint32_t> s)     { revoked = std::move (s); }
    void setClockForTesting (std::function<int64_t()> nowSeconds)  { clock = std::move (nowSeconds); }

private:
    int64_t now() const;

    std::vector<TrustedKey> trusted;
    std::vector<uint32_t> revoked;
    std::function<int64_t()> clock;
    Authorization auth;
    drp::Error status = drp::Error::cannotRead;
};
} // namespace vstm
