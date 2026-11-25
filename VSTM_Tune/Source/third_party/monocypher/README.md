# Monocypher 4.0.2 (vendored, unmodified)

- Source: https://monocypher.org/download/monocypher-4.0.2.tar.gz (identical to the GitHub release asset
  https://github.com/LoupVaillant/Monocypher/releases/download/4.0.2/monocypher-4.0.2.tar.gz).
- Archive SHA-512 (matches the published `monocypher-4.0.2.tar.gz.sha512`):
  `bf275d4c53ff94af6cdc723a4e002e9f080f4d1436c86c76bb37870b34807f1d7b32331d8ff8a1aeb369e946f3769021e03e63efac25b82efc5abf54dc084714`
- Files: `src/monocypher.{c,h}` and `src/optional/monocypher-ed25519.{c,h}` (RFC 8032 Ed25519 with SHA-512).
- Licence: BSD 2-clause or CC0, at your choice (`LICENCE.md`).
- Used by VSTM Tune only to **verify** Ed25519 signatures of authorization files (`Source/Authorization.cpp`).
  The tests also use it to make throw-away key pairs. No private key is stored in the plug-in.
