"""GAPP2: ECDSA-P256/SHA-256 authentication, with no private material in output."""
import hashlib
import json
import pathlib
import struct
import subprocess


def public_key(key):
    return subprocess.check_output(['openssl', 'pkey', '-in', str(key), '-pubout'])


def sign_package(unsigned, key, trusted_public):
    if unsigned[:8] != b'GOSAPP1\0' or hashlib.sha256(unsigned[:20]+unsigned[52:]).digest() != unsigned[20:52]:
        raise ValueError('Expected a valid unsigned GAPP1 package')
    pem = public_key(key)
    if pem != pathlib.Path(trusted_public).read_bytes():
        raise ValueError('Signing key does not match the firmware public key')
    description = subprocess.check_output(['openssl', 'pkey', '-pubin', '-text_pub', '-noout'], input=pem)
    if b'ASN1 OID: prime256v1' not in description:
        raise ValueError('Signing requires an ECDSA P-256 key')
    prefix = b'GOSAPP2\0'+unsigned[8:20]
    key_id = hashlib.sha256(pem).digest()[:8]
    signed = prefix+key_id+unsigned[52:]
    der = subprocess.check_output(['openssl', 'dgst', '-sha256', '-sign', str(key)], input=signed)
    if der[0] != 0x30 or der[1] != len(der)-2:
        raise ValueError('Unexpected ECDSA signature')
    pos = 2
    integers = []
    for _ in range(2):
        if der[pos] != 2: raise ValueError('Invalid ECDSA integer')
        length = der[pos+1]
        integers.append(int.from_bytes(der[pos+2:pos+2+length], 'big').to_bytes(32, 'big'))
        pos += 2+length
    if pos != len(der): raise ValueError('Trailing ECDSA bytes')
    return prefix+hashlib.sha256(signed).digest()+key_id+b''.join(integers)+unsigned[52:]


def inspect_signed(data, public, temporary):
    """Verify before publishing; the firmware independently verifies on install/load."""
    if len(data) < 124 or len(data) > 70908 or data[:8] != b'GOSAPP2\0':
        raise ValueError('Catalog packages must be signed GAPP2')
    metadata, icon, elf = struct.unpack_from('<III', data, 8)
    if not 0 < metadata <= 2048 or metadata % 4 or icon != 3200 or not 0 < elf <= 65536 or len(data) != 124+metadata+icon+elf:
        raise ValueError('Invalid package lengths')
    pem = pathlib.Path(public).read_bytes()
    signed = data[:20]+data[52:60]+data[124:]
    if hashlib.sha256(pem).digest()[:8] != data[52:60] or hashlib.sha256(signed).digest() != data[20:52]:
        raise ValueError('Package key/checksum mismatch')
    parts = []
    for raw in (data[60:92], data[92:124]):
        number = raw.lstrip(b'\0') or b'\0'
        if number[0] & 128: number = b'\0'+number
        parts.append(b'\x02'+bytes([len(number)])+number)
    body = b''.join(parts)
    signature = pathlib.Path(temporary)/'signature.der'
    signature.write_bytes(b'\x30'+bytes([len(body)])+body)
    subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(public), '-signature', str(signature)], input=signed, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return json.loads(data[124:124+metadata]), data[124+metadata:124+metadata+icon]
