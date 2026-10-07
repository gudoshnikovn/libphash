"""Digests as site_stages prints them, hexadecimal, read back as bytes and bits."""
import numpy as np


def digest_bytes(hexstr):
    return np.frombuffer(bytes.fromhex(hexstr), np.uint8)


def digest_bits(hexstr, n=None, lsb_first=False):
    """The digest's bits in the library's order: the most significant bit of byte 0
    first (mHash), or bit i as bit i % 8 of byte i / 8 (BMH, `lsb_first`); the first `n`."""
    bits = np.unpackbits(digest_bytes(hexstr), bitorder="little" if lsb_first else "big")
    return bits if n is None else bits[:n]


def bits_apart(a, b):
    """How many bits two hexadecimal hashes or digests of one length differ in."""
    return bin(int(a, 16) ^ int(b, 16)).count("1")
