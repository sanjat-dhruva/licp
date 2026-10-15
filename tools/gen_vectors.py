#!/usr/bin/env python3
"""
@file gen_vectors.py
@brief Independent generator for the test vectors used in tests/unit.

The C code under test is NOT used here: frames, integrity codes and airtimes
are computed with the Python "cryptography" package and plain floating-point
formulas, so the Google Test suite checks the C implementation against a
second, independent implementation.

Usage:  python3 tools/gen_vectors.py
Needs:  pip install cryptography
"""
import math
import struct

from cryptography.hazmat.primitives import cmac
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def aes_ecb(key: bytes, block: bytes) -> bytes:
    enc = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    return enc.update(block) + enc.finalize()


def aes_cmac(key: bytes, msg: bytes) -> bytes:
    c = cmac.CMAC(algorithms.AES(key))
    c.update(msg)
    return c.finalize()


def crypt_payload(key, direction, dev_addr, fcnt32, data):
    out = bytearray()
    for i in range(0, len(data), 16):
        a = bytes([1, 0, 0, 0, 0, direction]) + struct.pack("<II", dev_addr, fcnt32) + bytes([0, i // 16 + 1])
        s = aes_ecb(key, a)
        out += bytes(x ^ y for x, y in zip(data[i:i + 16], s))
    return bytes(out)


def mic(key, direction, dev_addr, fcnt32, msg):
    b0 = bytes([0x49, 0, 0, 0, 0, direction]) + struct.pack("<II", dev_addr, fcnt32) + bytes([0, len(msg)])
    return aes_cmac(key, b0 + msg)[:4]


def build_uplink(nwk, app, dev_addr, fcnt32, fport, plain, mtype=2):
    hdr = bytes([mtype << 5]) + struct.pack("<I", dev_addr) + bytes([0x00]) + struct.pack("<H", fcnt32 & 0xFFFF)
    body = hdr + bytes([fport]) + crypt_payload(app, 0, dev_addr, fcnt32, plain)
    return body + mic(nwk, 0, dev_addr, fcnt32, body)


def airtime_us(sf, bw, cr, preamble, payload_len, explicit=True, crc=True):
    tsym = (2 ** sf) / bw
    de = 1 if tsym >= 0.016 else 0
    ih = 0 if explicit else 1
    num = 8 * payload_len - 4 * sf + 28 + 16 * (1 if crc else 0) - 20 * ih
    n = 8 + max(math.ceil(num / (4 * (sf - 2 * de))) * (cr + 4), 0)
    return round(((preamble + 4.25) * tsym + n * tsym) * 1e6)


def c_array(b: bytes) -> str:
    return ", ".join(f"0x{x:02X}" for x in b)


if __name__ == "__main__":
    # 1. Public example (widely used lora-packet vector): payload "test".
    nwk = bytes.fromhex("44024241ED4CE9A68C6A8BC055233FD3")
    app = bytes.fromhex("EC925802AE430CA77FD3DD73CB2CC588")
    frame = bytes.fromhex("40F17DBE4900020001954378762B11FF0D")
    body = frame[:-4]
    assert mic(nwk, 0, 0x49BE7DF1, 2, body) == frame[-4:], "public vector MIC mismatch"
    assert crypt_payload(app, 0, 0x49BE7DF1, 2, frame[9:-4]) == b"test", "public vector payload mismatch"
    print("public vector OK")

    # 2. Project vector with a counter above 16 bits (tests counter rebuild).
    nwk2 = bytes(range(0x00, 0x10))
    app2 = bytes(range(0x10, 0x20))
    f2 = build_uplink(nwk2, app2, 0x26011234, 0x00010005, 1, b"hello S-MIoT")
    print("project uplink (dev 0x26011234, fcnt32 0x00010005):")
    print("  {" + c_array(f2) + "}")

    # 3. Airtimes (chirp, explicit header).
    for sf, bw, n, crc in [(10, 125000, 13, False), (10, 125000, 50, False), (7, 125000, 20, True),
                           (12, 125000, 51, True), (11, 125000, 30, False)]:
        print(f"airtime SF{sf} BW{bw} len{n} crc{int(crc)}: {airtime_us(sf, bw, 1, 8, n, True, crc)} us")
