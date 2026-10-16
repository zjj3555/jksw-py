#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_ref.py —— Python 权威实现对照器 (与 test_juku_pc.c 逐字段 diff)"""
import os, base64, hashlib

def _sha256_hex(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()

def spki_b64_from_raw(priv32: bytes, pub64: bytes) -> str:
    from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat, load_der_private_key
    # EC PRIVATE KEY DER: SEQ{ INT 1, OCTET d, [0] OID prime256v1 }
    der = b"\x30\x31\x02\x01\x01\x04\x20" + priv32 + b"\xa0\x0a\x06\x08\x2a\x86\x48\xce\x3d\x03\x01\x07"
    key = load_der_private_key(der, password=None)
    assert key.public_key().public_numbers().x.to_bytes(32, "big") == pub64[:32]
    return base64.b64encode(key.public_key().public_bytes(Encoding.DER, PublicFormat.SubjectPublicKeyInfo)).decode()

def main():
    kp = "test_key.bin"
    if not os.path.exists(kp):
        from cryptography.hazmat.primitives.asymmetric import ec
        key = ec.generate_private_key(ec.SECP256R1())
        d = key.private_numbers().private_value.to_bytes(32, "big")
        x = key.public_key().public_numbers().x.to_bytes(32, "big")
        y = key.public_key().public_numbers().y.to_bytes(32, "big")
        open(kp, "wb").write(d + x + y)
        print("generated test_key.bin (96B)")
    raw = open(kp, "rb").read()
    assert len(raw) == 96, len(raw)
    priv32, pub64 = raw[:32], raw[32:]
    print("[PY] priv[:4]:", priv32[:4].hex())
    print("[PY] pub[:16]:", pub64[:16].hex())
    print("[PY] SPKI: %s" % spki_b64_from_raw(priv32, pub64))

    body = '{"publicKey":"TEST","appVersion":"1.15.0","platform":"desktop","installId":"x"}'
    bh = _sha256_hex(body.encode())
    ts = "1760000000000"; nonce = "00112233445566778899aabbccddeeff"
    ss = "\n".join(["POST", "/auth/device/register", "", bh, ts, nonce, "DEV-TEST-123", "TOK-TEST-456"])
    print("[PY] bodyhash: %s" % bh)
    print("[PY] signing_string:")
    print(ss)
    print("<<END")

if __name__ == "__main__":
    main()
