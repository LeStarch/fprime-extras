"""SPatch v1 payload coders: none, RLE, and 256-byte-window LZSS.

Copyright (c) 2026 Michael Starch

Each coder exposes ``encode(data) -> bytes`` and ``decode(data) -> bytes``. The decoders here are the ground
reference against which the flight decoders (FprimeExtras/Update/Delta/DeltaCoder.cpp) are cross-checked.
"""

from __future__ import annotations

from typing import Dict, List

ID_NONE = 0
ID_RLE = 1
ID_LZSS = 2

NAMES = {"none": ID_NONE, "rle": ID_RLE, "lzss": ID_LZSS}
IDS = {value: key for key, value in NAMES.items()}


class CoderError(ValueError):
    """Malformed coded payload"""


# ----------------------------------------------------------------------
# none
# ----------------------------------------------------------------------
def none_encode(data: bytes) -> bytes:
    return bytes(data)


def none_decode(data: bytes) -> bytes:
    return bytes(data)


# ----------------------------------------------------------------------
# RLE: control < 0x80 -> control+1 literal bytes follow; control >= 0x80 -> next byte repeated (control-0x80)+2 times
# ----------------------------------------------------------------------
RLE_MAX_LITERAL = 0x80
RLE_MAX_REPEAT = 0x7F + 2


def rle_encode(data: bytes) -> bytes:
    out = bytearray()
    literal = bytearray()

    def flush_literal():
        for start in range(0, len(literal), RLE_MAX_LITERAL):
            piece = literal[start : start + RLE_MAX_LITERAL]
            out.append(len(piece) - 1)
            out.extend(piece)
        literal.clear()

    i = 0
    n = len(data)
    while i < n:
        run = 1
        while i + run < n and data[i + run] == data[i] and run < RLE_MAX_REPEAT:
            run += 1
        if run >= 2:
            flush_literal()
            out.append(0x80 + (run - 2))
            out.append(data[i])
            i += run
        else:
            literal.append(data[i])
            i += 1
    flush_literal()
    return bytes(out)


def rle_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        control = data[i]
        i += 1
        if control < 0x80:
            count = control + 1
            if i + count > n:
                raise CoderError("RLE literal run truncated")
            out.extend(data[i : i + count])
            i += count
        else:
            if i >= n:
                raise CoderError("RLE repeat value truncated")
            out.extend(bytes([data[i]]) * ((control - 0x80) + 2))
            i += 1
    return bytes(out)


# ----------------------------------------------------------------------
# LZSS: flag byte then up to 8 items MSB-first; bit 0 = literal byte, bit 1 = match (distance-1, length-3).
# Distances 1..256 into already-decoded output, lengths 3..258; overlapping matches allowed.
# ----------------------------------------------------------------------
LZSS_WINDOW = 256
LZSS_MIN_MATCH = 3
LZSS_MAX_MATCH = 258


def lzss_encode(data: bytes) -> bytes:
    out = bytearray()
    n = len(data)
    pos = 0
    heads: Dict[bytes, List[int]] = {}
    items: List[bytes] = []
    flags = 0

    def emit(flag: int, payload: bytes):
        nonlocal flags, items
        flags = (flags << 1) | flag
        items.append(payload)
        if len(items) == 8:
            out.append(flags)
            for item in items:
                out.extend(item)
            flags = 0
            items = []

    def index(p: int):
        if p + LZSS_MIN_MATCH <= n:
            key = data[p : p + LZSS_MIN_MATCH]
            chain = heads.setdefault(key, [])
            chain.append(p)
            # Prune positions that fell out of the window
            while chain and p - chain[0] > LZSS_WINDOW:
                chain.pop(0)

    while pos < n:
        best_len = 0
        best_dist = 0
        if pos + LZSS_MIN_MATCH <= n:
            for cand in reversed(heads.get(data[pos : pos + LZSS_MIN_MATCH], [])):
                dist = pos - cand
                if dist < 1 or dist > LZSS_WINDOW:
                    continue
                length = 0
                limit = min(LZSS_MAX_MATCH, n - pos)
                while length < limit and data[cand + length] == data[pos + length]:
                    length += 1
                if length > best_len:
                    best_len, best_dist = length, dist
                    if length == limit:
                        break
        if best_len >= LZSS_MIN_MATCH:
            emit(1, bytes([best_dist - 1, best_len - LZSS_MIN_MATCH]))
            for k in range(best_len):
                index(pos + k)
            pos += best_len
        else:
            emit(0, bytes([data[pos]]))
            index(pos)
            pos += 1
    if items:
        flags <<= 8 - len(items)
        out.append(flags)
        for item in items:
            out.extend(item)
    return bytes(out)


def lzss_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        flags = data[i]
        i += 1
        for bit in range(7, -1, -1):
            if i >= n:
                break  # trailing unused flag bits
            if (flags >> bit) & 1:
                if i + 2 > n:
                    raise CoderError("LZSS match truncated")
                dist = data[i] + 1
                length = data[i + 1] + LZSS_MIN_MATCH
                i += 2
                if dist > len(out):
                    raise CoderError("LZSS match reaches before start of output")
                for _ in range(length):
                    out.append(out[-dist])
            else:
                out.append(data[i])
                i += 1
    return bytes(out)


ENCODERS = {ID_NONE: none_encode, ID_RLE: rle_encode, ID_LZSS: lzss_encode}
DECODERS = {ID_NONE: none_decode, ID_RLE: rle_decode, ID_LZSS: lzss_decode}
