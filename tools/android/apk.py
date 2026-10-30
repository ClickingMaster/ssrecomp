#!/usr/bin/env python3
"""Packs the Android build into an installable APK and signs it, with nothing but Python.

usage: apk.py <skeleton.apk> <abi> <libmain.so> <libSDL2.so> <rom.nds> <key file> <out.apk>

<abi> is the Android name of the CPU type: arm64-v8a for phones and handhelds,
x86_64 for the Android emulator on a PC.

The skeleton (tools/android/skeleton.apk, made by make_skeleton.sh) holds the manifest,
the resources and SDL's Java code; this adds the game library, SDL's library and the
player's ROM, then signs the result with APK Signature Scheme v2. The key is made on the
first run and kept in <key file>: Android only installs an update over an app signed with
the same key, so it has to be kept to update without uninstalling (which deletes the save).
"""
import hashlib
import json
import os
import secrets
import struct
import sys
import zipfile

# ---------------------------------------------------------------------------
# RSA (key generation and PKCS#1 v1.5 signatures with SHA-256).

def _probably_prime(n, rounds=40):
    if n < 2:
        return False
    for p in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0:
            return n == p
    d, r = n - 1, 0
    while d % 2 == 0:
        d //= 2
        r += 1
    for _ in range(rounds):
        a = secrets.randbelow(n - 3) + 2
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(r - 1):
            x = pow(x, 2, n)
            if x == n - 1:
                break
        else:
            return False
    return True


def _prime(bits):
    while True:
        c = secrets.randbits(bits) | (1 << (bits - 1)) | (1 << (bits - 2)) | 1
        if _probably_prime(c):
            return c


def new_key(bits=2048):
    e = 65537
    while True:
        p, q = _prime(bits // 2), _prime(bits // 2)
        phi = (p - 1) * (q - 1)
        if p != q and phi % e and (p * q).bit_length() == bits:
            return {'n': p * q, 'e': e, 'd': pow(e, -1, phi)}


def load_key(path):
    if os.path.exists(path):
        k = json.load(open(path))
        return {'n': int(k['n'], 16), 'e': k['e'], 'd': int(k['d'], 16)}
    k = new_key()
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'w') as f:
        json.dump({'n': '%x' % k['n'], 'e': k['e'], 'd': '%x' % k['d'],
                   'note': 'Signing key of your ssrecomp Android builds. Keep it: updates must be signed with it.'}, f)
    try:
        os.chmod(path, 0o600)
    except OSError:
        pass
    return k


SHA256_DIGEST_INFO = bytes.fromhex('3031300d060960864801650304020105000420')


def rsa_sign(key, data):
    k = (key['n'].bit_length() + 7) // 8
    t = SHA256_DIGEST_INFO + hashlib.sha256(data).digest()
    em = b'\x00\x01' + b'\xff' * (k - len(t) - 3) + b'\x00' + t
    return pow(int.from_bytes(em, 'big'), key['d'], key['n']).to_bytes(k, 'big')


# ---------------------------------------------------------------------------
# DER, for the public key and a self-signed certificate.

def _len(n):
    if n < 0x80:
        return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, 'big')
    return bytes([0x80 | len(b)]) + b


def der(tag, body):
    return bytes([tag]) + _len(len(body)) + body


def der_int(v):
    b = v.to_bytes(v.bit_length() // 8 + 1, 'big')   # leading zero keeps it positive
    return der(0x02, b)


def der_oid(dotted):
    parts = [int(x) for x in dotted.split('.')]
    body = bytes([40 * parts[0] + parts[1]])
    for p in parts[2:]:
        chunk = [p & 0x7F]
        p >>= 7
        while p:
            chunk.insert(0, 0x80 | (p & 0x7F))
            p >>= 7
        body += bytes(chunk)
    return der(0x06, body)


def seq(*items):
    return der(0x30, b''.join(items))


NULL = b'\x05\x00'
RSA_ENCRYPTION = seq(der_oid('1.2.840.113549.1.1.1'), NULL)
SHA256_WITH_RSA = seq(der_oid('1.2.840.113549.1.1.11'), NULL)


def public_key_der(key):
    rsa_pub = seq(der_int(key['n']), der_int(key['e']))
    return seq(RSA_ENCRYPTION, der(0x03, b'\x00' + rsa_pub))


def certificate(key):
    """Self-signed, and the same every time for the same key (Android compares the
    certificate when updating an app)."""
    name = seq(der(0x31, seq(der_oid('2.5.4.3'), der(0x0C, b'ssrecomp'))))
    serial = int.from_bytes(hashlib.sha256(key['n'].to_bytes(256, 'big')).digest()[:8], 'big') | 1
    validity = seq(der(0x17, b'260101000000Z'), der(0x18, b'21251231000000Z'))
    tbs = seq(der(0xA0, der_int(2)), der_int(serial), SHA256_WITH_RSA, name, validity, name, public_key_der(key))
    return seq(tbs, SHA256_WITH_RSA, der(0x03, b'\x00' + rsa_sign(key, tbs)))


# ---------------------------------------------------------------------------
# APK Signature Scheme v2.

def lp(b):
    """Length-prefixed (uint32, little endian)."""
    return struct.pack('<I', len(b)) + b


def find_eocd(data):
    pos = data.rfind(b'PK\x05\x06')
    if pos < 0:
        raise ValueError('not a zip file')
    return pos


def content_digest(sections):
    chunk_digests = []
    for sec in sections:
        for i in range(0, len(sec), 1 << 20):
            chunk = sec[i:i + (1 << 20)]
            chunk_digests.append(hashlib.sha256(b'\xa5' + struct.pack('<I', len(chunk)) + chunk).digest())
    return hashlib.sha256(b'\x5a' + struct.pack('<I', len(chunk_digests)) + b''.join(chunk_digests)).digest()


def sign_v2(unsigned, key):
    eocd = find_eocd(unsigned)
    cd_size, cd_offset = struct.unpack_from('<II', unsigned, eocd + 12)
    entries, cd, eocd_bytes = unsigned[:cd_offset], unsigned[cd_offset:eocd], unsigned[eocd:]
    algo = 0x0103   # RSASSA-PKCS1-v1_5 with SHA2-256
    digest = content_digest([entries, cd, eocd_bytes])
    cert = certificate(key)
    signed_data = lp(lp(struct.pack('<I', algo) + lp(digest))) + lp(lp(cert)) + lp(b'')
    signer = lp(signed_data) + lp(lp(struct.pack('<I', algo) + lp(rsa_sign(key, signed_data)))) + lp(public_key_der(key))
    v2_value = lp(lp(signer))
    pair = struct.pack('<Q', 4 + len(v2_value)) + struct.pack('<I', 0x7109871A) + v2_value
    # Padding, so that the central directory starts on a 4096-byte boundary as apksigner does.
    block_len = 8 + len(pair) + 8 + 16
    pad = (-(len(entries) + block_len)) % 4096
    if pad:
        if pad < 12:
            pad += 4096
        pad_value = b'\x00' * (pad - 12)
        pair += struct.pack('<Q', 4 + len(pad_value)) + struct.pack('<I', 0x42726577) + pad_value
    size = len(pair) + 8 + 16
    block = struct.pack('<Q', size) + pair + struct.pack('<Q', size) + b'APK Sig Block 42'
    new_eocd = bytearray(eocd_bytes)
    struct.pack_into('<I', new_eocd, 16, cd_offset + len(block))
    return entries + block + cd + bytes(new_eocd)


# ---------------------------------------------------------------------------
# Packing.

def write_aligned(z, name, data, compress, align):
    """Adds an entry; stored entries are padded (in the local header's extra field) so
    their data starts on an `align`-byte boundary, as Android requires for some files."""
    info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
    info.external_attr = 0o644 << 16
    if not compress and align > 1:
        offset = z.fp.tell() + 30 + len(name.encode())
        info.extra = b'\x00' * ((-offset) % align)
    z.writestr(info, data, compresslevel=6 if compress else None)


def build(skeleton, abi, libmain, libsdl, rom, key_path, out):
    return build_multi(skeleton, {abi: (libmain, libsdl)}, rom, key_path, out)


def build_multi(skeleton, libs, rom, key_path, out):
    """libs: ABI -> (libmain.so, libSDL2.so), one pair per CPU type the APK runs on; rom may
    be None (the TV launcher, whose start menu finds the ROM on the device)."""
    tmp = out + '.unsigned'
    with zipfile.ZipFile(skeleton) as src, zipfile.ZipFile(tmp, 'w') as z:
        for item in src.infolist():
            data = src.read(item.filename)
            # Android 11 and later want resources.arsc uncompressed and 4-byte aligned.
            stored = item.filename == 'resources.arsc' or item.filename.endswith('.png')
            write_aligned(z, item.filename, data, not stored, 4)
        for abi, (libmain, libsdl) in libs.items():
            for path, name in ((libsdl, f'lib/{abi}/libSDL2.so'), (libmain, f'lib/{abi}/libmain.so')):
                write_aligned(z, name, open(path, 'rb').read(), True, 4)
        if rom:
            write_aligned(z, 'assets/rom.nds', open(rom, 'rb').read(), True, 4)
    signed = sign_v2(open(tmp, 'rb').read(), load_key(key_path))
    with open(out, 'wb') as f:
        f.write(signed)
    os.remove(tmp)
    return out


if __name__ == '__main__':
    if len(sys.argv) != 8:
        sys.exit(__doc__)
    print('->', build(*sys.argv[1:8]))
