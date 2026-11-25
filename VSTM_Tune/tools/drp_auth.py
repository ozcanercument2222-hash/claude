#!/usr/bin/env python3
"""VSTM Tune authorization files (.drp, type 2): key generation, signing, inspection.

The ISSUER runs this offline. The private key never goes into the plug-in, the repository or a preset.

    drp_auth.py keygen  --out DIR
        writes DIR/vstm_tune_auth_private_key.pem (keep it secret, offline; file mode 600)
        and DIR/vstm_tune_auth_public_key.txt (32-byte public key + 8-byte key id, hex)
    drp_auth.py sign    --key PRIVATE.pem --out FILE.drp [--label TEXT] [--serial N]
                        [--expires YYYY-MM-DD] [--not-before YYYY-MM-DD]
        writes an admin authorization for VSTM Tune (role admin = every admin feature)
    drp_auth.py inspect FILE.drp
    drp_auth.py verify  FILE.drp --public HEX64

File layout: docs/1.4.0/VSTM_Tune_1.4.0_DRP_Format.md (same as Source/Drp.h and Source/Authorization.cpp).
Requires the Python package "cryptography" (Ed25519, RFC 8032).
"""
import argparse
import datetime as dt
import hashlib
import os
import struct
import sys
import time
import zlib
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

MAGIC = b"VSTMDRP\x1a"
FORMAT_VERSION = 1
TYPE_AUTH = 2
MANUFACTURER = 0x5673746D     # 'Vstm'
PLUGIN = 0x56746E31           # 'Vtn1'
PAYLOAD_VERSION = 1
ROLE_ADMIN = 1
ALL_ENTITLEMENTS = 0xFFFFFFFF
SIG_SIZE = 64


def key_id(public_raw: bytes) -> bytes:
    return hashlib.sha512(public_raw).digest()[:8]


def to_unix(day: str) -> int:
    return int(dt.datetime.strptime(day, "%Y-%m-%d").replace(tzinfo=dt.timezone.utc).timestamp())


def payload(role, entitlements, issued, not_before, expires, serial, kid, label: str) -> bytes:
    lab = label.encode("utf-8")[:64]
    return (struct.pack("<IIHBBIQQQI", MANUFACTURER, PLUGIN, PAYLOAD_VERSION, role, 0, entitlements,
                        issued, not_before, expires, serial) + kid + struct.pack("<B", len(lab)) + lab)


def header(file_type: int, body: bytes) -> bytes:
    return MAGIC + struct.pack("<HHII", FORMAT_VERSION, file_type, len(body), zlib.crc32(body) & 0xFFFFFFFF)


def parse(data: bytes):
    if len(data) < 20 or data[:8] != MAGIC:
        raise ValueError("not a .drp file")
    ver, typ, n, crc = struct.unpack("<HHII", data[8:20])
    if ver != FORMAT_VERSION:
        raise ValueError(f"unsupported version {ver}")
    if typ != TYPE_AUTH:
        raise ValueError(f"not an authorization file (type {typ})")
    if len(data) != 20 + n + SIG_SIZE:
        raise ValueError("length mismatch")
    body = data[20:20 + n]
    if zlib.crc32(body) & 0xFFFFFFFF != crc:
        raise ValueError("checksum mismatch")
    fixed = struct.calcsize("<IIHBBIQQQI")          # 44 bytes
    if len(body) < fixed + 9:
        raise ValueError("payload too short")
    m, p, pv, role, _res, ent, issued, nb, exp, serial = struct.unpack("<IIHBBIQQQI", body[:fixed])
    kid = body[fixed:fixed + 8]
    ln = body[fixed + 8]
    if len(body) != fixed + 9 + ln:
        raise ValueError("payload length mismatch")
    label = body[fixed + 9:fixed + 9 + ln].decode("utf-8")
    return dict(manufacturer=m, plugin=p, payload_version=pv, role=role, entitlements=ent, issued=issued,
                not_before=nb, expires=exp, serial=serial, key_id=kid.hex(), label=label,
                signed=data[:20 + n], signature=data[20 + n:])


def cmd_keygen(a):
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    priv = Ed25519PrivateKey.generate()
    pem = priv.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    pub = priv.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    kp = out / "vstm_tune_auth_private_key.pem"
    fd = os.open(kp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(pem)
    (out / "vstm_tune_auth_public_key.txt").write_text(
        f"public_key {pub.hex()}\nkey_id {key_id(pub).hex()}\n")
    print(f"private key: {kp} (mode 600, keep it offline)")
    print(f"public key : {pub.hex()}")
    print(f"key id     : {key_id(pub).hex()}")


def cmd_sign(a):
    priv = serialization.load_pem_private_key(Path(a.key).read_bytes(), password=None)
    if not isinstance(priv, Ed25519PrivateKey):
        sys.exit("not an Ed25519 private key")
    pub = priv.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    issued = int(time.time())
    nb = to_unix(a.not_before) if a.not_before else 0
    exp = to_unix(a.expires) if a.expires else 0
    body = payload(ROLE_ADMIN, ALL_ENTITLEMENTS, issued, nb, exp, a.serial, key_id(pub), a.label)
    signed = header(TYPE_AUTH, body) + body
    data = signed + priv.sign(signed)
    Path(a.out).write_bytes(data)
    print(f"wrote {a.out}: admin, serial {a.serial}, key id {key_id(pub).hex()}, "
          f"expires {'never' if not exp else a.expires}, {len(data)} bytes")


def cmd_inspect(a):
    info = parse(Path(a.file).read_bytes())
    for k in ("manufacturer", "plugin", "payload_version", "role", "entitlements", "issued", "not_before",
              "expires", "serial", "key_id", "label"):
        v = info[k]
        if k in ("manufacturer", "plugin", "entitlements"):
            v = f"0x{v:08x}"
        elif k in ("issued", "not_before", "expires") and v:
            v = f"{v} ({dt.datetime.fromtimestamp(v, dt.timezone.utc).isoformat()})"
        print(f"{k:16} {v}")


def cmd_verify(a):
    info = parse(Path(a.file).read_bytes())
    pub = bytes.fromhex(a.public)
    try:
        Ed25519PublicKey.from_public_bytes(pub).verify(info["signature"], info["signed"])
    except Exception:
        sys.exit("INVALID signature")
    print("signature OK (key id %s)" % key_id(pub).hex())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keygen"); k.add_argument("--out", required=True); k.set_defaults(fn=cmd_keygen)
    s = sub.add_parser("sign"); s.add_argument("--key", required=True); s.add_argument("--out", required=True)
    s.add_argument("--label", default="VSTM Tune admin"); s.add_argument("--serial", type=int, default=1)
    s.add_argument("--expires"); s.add_argument("--not-before", dest="not_before"); s.set_defaults(fn=cmd_sign)
    i = sub.add_parser("inspect"); i.add_argument("file"); i.set_defaults(fn=cmd_inspect)
    v = sub.add_parser("verify"); v.add_argument("file"); v.add_argument("--public", required=True); v.set_defaults(fn=cmd_verify)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
