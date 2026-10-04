"""SPatch v2 container: operation stream, chunking, creation and reference application.

Copyright (c) 2026 Michael Starch

Format (all integers F Prime serialized, i.e. big-endian; `size` is the flight FwSizeType, 4 or 8 bytes wide,
its width recorded in the header so the flight rejects a patch built for the wrong width):

    Header (20 + 3 * W B): "SPAT" | version U8=2 | coder_id U8 | flags U8=0 | size_width U8=W
                           | old_size size | old_crc32 U32 | new_size size | new_crc32 U32 | chunk_bytes size
                           | header_crc32 U32 (over the preceding bytes)
    Chunk (x ceil(new_size / chunk_bytes)): coded_len size | output_crc32 U32 | payload[coded_len]

Each chunk's payload decodes (with the header's coder) into an operation stream producing exactly
min(chunk_bytes, new_size - chunk_index * chunk_bytes) bytes of new image. At chunk start the old cursor
equals the chunk's new-image offset and coder history is empty, so chunks decode independently.

    COPY 0x00 uvar n           new[o:o+n] = old[c:c+n]; c += n
    ADD  0x01 uvar n, n bytes  new[o:o+n] = (old[c:c+n] + bytes) mod 256; c += n
    LIT  0x02 uvar n, n bytes  new[o:o+n] = bytes
    SEEK 0x03 svar d           c += d   (zigzag-encoded, 0 <= c <= old_size afterwards;
                                        must be followed by COPY/ADD/LIT, never another SEEK)

uvar is unsigned LEB128, at most 5 bytes. Operations never span a chunk boundary and n > 0.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from typing import Iterable, Iterator

from . import coders

MAGIC = b"SPAT"
VERSION = 2
SIZE_WIDTHS = (4, 8)  # sizeof(FwSizeType) on the flight build
DEFAULT_SIZE_WIDTH = 8  # F Prime's default PlatformSizeType is 64-bit; pass --dictionary/--size-width otherwise
_SIZE_FORMATS = {4: "I", 8: "Q"}


def header_size(size_width: int) -> int:
    """Header bytes for a given FwSizeType width: 32 (32-bit) or 44 (64-bit)"""
    return 20 + 3 * size_width


def chunk_header_size(size_width: int) -> int:
    """Chunk header bytes for a given FwSizeType width: 8 (32-bit) or 12 (64-bit)"""
    return size_width + 4


def size_width_from_dictionary(path: str) -> int:
    """Read sizeof(FwSizeType) from an F Prime JSON topology dictionary"""
    import json

    with open(path, encoding="utf-8") as handle:
        dictionary = json.load(handle)
    for definition in dictionary.get("typeDefinitions", []):
        if definition.get("qualifiedName") == "FwSizeType":
            bits = definition["underlyingType"]["size"]
            if bits // 8 not in SIZE_WIDTHS:
                raise SPatchError(f"dictionary FwSizeType is {bits} bits; SPatch supports 32 and 64")
            return bits // 8
    raise SPatchError("dictionary has no FwSizeType definition")


DEFAULT_CHUNK_BYTES = 4096
# Flight-side limits (must match ExtrasConfig/DeltaCodecConfig.hpp); patches exceeding them are rejected on board
MAX_CHUNK_BYTES = 8192
MAX_CODED_CHUNK_BYTES = 2 * MAX_CHUNK_BYTES
MAX_IMAGE_SIZE = 64 * 1024 * 1024
DEFAULT_COPY_MIN = 8

OP_COPY = 0
OP_ADD = 1
OP_LIT = 2
OP_SEEK = 3


class SPatchError(ValueError):
    """Malformed or inapplicable patch"""


def crc32(data: bytes, crc: int = 0) -> int:
    return zlib.crc32(data, crc) & 0xFFFFFFFF


def uvar(value: int) -> bytes:
    if value < 0:
        raise ValueError("uvar requires non-negative value")
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def svar(value: int) -> bytes:
    return uvar((value << 1) ^ (value >> 63)) if value < 0 else uvar(value << 1)


def read_uvar(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    for i in range(5):
        if pos + i >= len(data):
            raise SPatchError("truncated varint")
        byte = data[pos + i]
        value |= (byte & 0x7F) << (7 * i)
        if not byte & 0x80:
            return value, pos + i + 1
    raise SPatchError("varint too long")


def unzigzag(value: int) -> int:
    return (value >> 1) ^ -(value & 1)


# ----------------------------------------------------------------------
# Operation stream
# ----------------------------------------------------------------------
@dataclass
class Op:
    kind: int
    length: int = 0  # COPY/ADD/LIT byte count, or SEEK delta
    data: bytes = b""  # ADD deltas or LIT bytes

    def encode(self) -> bytes:
        if self.kind == OP_SEEK:
            return bytes([OP_SEEK]) + svar(self.length)
        return bytes([self.kind]) + uvar(self.length) + self.data


def ops_from_bsdiff(old: bytes, new: bytes, copy_min: int = DEFAULT_COPY_MIN) -> list[Op]:
    """Derive an op stream using the bsdiff4 matcher (control triples + diff + extra blocks).

    Runs of zero diff bytes of at least `copy_min` are folded into COPY ops so the coder never sees them.
    """
    import bsdiff4.core

    tcontrol, bdiff, bextra = bsdiff4.core.diff(old, new)
    ops: list[Op] = []
    dpos = 0
    epos = 0
    for diff_len, extra_len, seek in tcontrol:
        block = bdiff[dpos : dpos + diff_len]
        dpos += diff_len
        ops.extend(_fold_add(block, copy_min))
        if extra_len:
            ops.append(Op(OP_LIT, extra_len, bextra[epos : epos + extra_len]))
            epos += extra_len
        if seek:
            ops.append(Op(OP_SEEK, seek))
    return ops


def _fold_add(block: bytes, copy_min: int) -> Iterator[Op]:
    i = 0
    n = len(block)
    start = 0
    while i < n:
        if block[i] == 0:
            j = i
            while j < n and block[j] == 0:
                j += 1
            if j - i >= copy_min:
                if i > start:
                    yield Op(OP_ADD, i - start, block[start:i])
                yield Op(OP_COPY, j - i)
                start = j
            i = j
        else:
            i += 1
    if n > start:
        yield Op(OP_ADD, n - start, block[start:n])


def ops_literal(new: bytes) -> list[Op]:
    """Op stream that ignores the old image entirely (baseline for benchmarks)"""
    return [Op(OP_LIT, len(new), new)] if new else []


def chunk_ops(ops: Iterable[Op], old_size: int, new_size: int, chunk_bytes: int) -> list[bytes]:
    """Split a whole-image op stream into per-chunk encoded op streams honouring the chunk invariants"""
    chunks: list[bytearray] = [bytearray() for _ in range((new_size + chunk_bytes - 1) // chunk_bytes)]
    new_pos = 0
    old_cursor = 0  # matcher's old cursor
    chunk_cursor = 0  # decoder's old cursor; reset to the chunk offset at every chunk start

    def emit(op: Op):
        nonlocal chunk_cursor
        idx = new_pos // chunk_bytes
        if new_pos % chunk_bytes == 0:
            chunk_cursor = new_pos
        if op.kind in (OP_COPY, OP_ADD):
            if chunk_cursor != old_cursor:
                chunks[idx].extend(Op(OP_SEEK, old_cursor - chunk_cursor).encode())
                chunk_cursor = old_cursor
            chunk_cursor += op.length
        chunks[idx].extend(op.encode())

    for op in ops:
        if op.kind == OP_SEEK:
            old_cursor += op.length
            if not 0 <= old_cursor <= old_size:
                raise SPatchError("matcher seek left old image bounds")
            continue
        remaining = op.length
        offset = 0
        while remaining:
            room = chunk_bytes - (new_pos % chunk_bytes)
            take = min(room, remaining)
            piece = Op(op.kind, take, op.data[offset : offset + take] if op.data else b"")
            emit(piece)
            if op.kind in (OP_COPY, OP_ADD):
                old_cursor += take
            new_pos += take
            remaining -= take
            offset += take
    if new_pos != new_size:
        raise SPatchError(f"op stream produces {new_pos} bytes, expected {new_size}")
    return [bytes(chunk) for chunk in chunks]


# ----------------------------------------------------------------------
# Container
# ----------------------------------------------------------------------
@dataclass
class Header:
    coder_id: int
    old_size: int
    old_crc: int
    new_size: int
    new_crc: int
    chunk_bytes: int
    size_width: int = DEFAULT_SIZE_WIDTH

    @property
    def chunk_count(self) -> int:
        return (self.new_size + self.chunk_bytes - 1) // self.chunk_bytes

    @property
    def header_size(self) -> int:
        return header_size(self.size_width)

    @property
    def chunk_header_size(self) -> int:
        return chunk_header_size(self.size_width)

    def _format(self) -> str:
        size = _SIZE_FORMATS[self.size_width]
        return f">BBBB{size}I{size}I{size}"

    def pack(self) -> bytes:
        if self.size_width not in SIZE_WIDTHS:
            raise SPatchError(f"size_width must be one of {SIZE_WIDTHS}")
        body = MAGIC + struct.pack(
            self._format(),
            VERSION,
            self.coder_id,
            0,
            self.size_width,
            self.old_size,
            self.old_crc,
            self.new_size,
            self.new_crc,
            self.chunk_bytes,
        )
        return body + struct.pack(">I", crc32(body))

    def pack_chunk_header(self, coded_len: int, out_crc: int) -> bytes:
        return struct.pack(f">{_SIZE_FORMATS[self.size_width]}I", coded_len, out_crc)

    def unpack_chunk_header(self, data: bytes, pos: int) -> tuple[int, int]:
        return struct.unpack_from(f">{_SIZE_FORMATS[self.size_width]}I", data, pos)

    @classmethod
    def unpack(cls, data: bytes) -> Header:
        if len(data) < 8 or data[:4] != MAGIC:
            raise SPatchError("bad magic")
        version, flags, size_width = data[4], data[6], data[7]
        if version != VERSION or flags != 0:
            raise SPatchError("unsupported version or flags")
        if size_width not in SIZE_WIDTHS:
            raise SPatchError(f"unsupported size width {size_width}")
        total = header_size(size_width)
        if len(data) < total:
            raise SPatchError("patch shorter than header")
        header = cls(0, 0, 0, 0, 0, 1, size_width)
        _, coder_id, _, _, old_size, old_crc, new_size, new_crc, chunk_bytes = struct.unpack(
            header._format(), data[4 : total - 4]
        )
        (header_crc,) = struct.unpack(">I", data[total - 4 : total])
        if crc32(data[: total - 4]) != header_crc:
            raise SPatchError("header CRC mismatch")
        if chunk_bytes == 0:
            raise SPatchError("chunk_bytes must be positive")
        return cls(coder_id, old_size, old_crc, new_size, new_crc, chunk_bytes, size_width)


def create(
    old: bytes,
    new: bytes,
    coder_id: int = coders.ID_LZSS,
    chunk_bytes: int = DEFAULT_CHUNK_BYTES,
    copy_min: int = DEFAULT_COPY_MIN,
    ops: list[Op] | None = None,
    size_width: int = DEFAULT_SIZE_WIDTH,
) -> bytes:
    if not 0 < chunk_bytes <= MAX_CHUNK_BYTES:
        raise SPatchError(f"chunk_bytes must be in 1..{MAX_CHUNK_BYTES} (flight DELTA_MAX_CHUNK_BYTES)")
    if len(old) > MAX_IMAGE_SIZE or len(new) > MAX_IMAGE_SIZE:
        raise SPatchError(f"images must not exceed {MAX_IMAGE_SIZE} bytes (flight DELTA_MAX_IMAGE_SIZE)")
    if ops is None:
        ops = ops_from_bsdiff(old, new, copy_min)
    header = Header(coder_id, len(old), crc32(old), len(new), crc32(new), chunk_bytes, size_width)
    encode = coders.ENCODERS[coder_id]
    out = bytearray(header.pack())
    for index, raw in enumerate(chunk_ops(ops, len(old), len(new), chunk_bytes)):
        start = index * chunk_bytes
        expected = new[start : start + chunk_bytes]
        coded = encode(raw)
        if len(coded) > MAX_CODED_CHUNK_BYTES:
            raise SPatchError(f"chunk {index} coded to {len(coded)} bytes, above flight DELTA_MAX_CODED_CHUNK_BYTES")
        out.extend(header.pack_chunk_header(len(coded), crc32(expected)))
        out.extend(coded)
    return bytes(out)


def iter_chunks(patch: bytes, header: Header) -> Iterator[tuple[int, int, bytes]]:
    pos = header.header_size
    for _ in range(header.chunk_count):
        if pos + header.chunk_header_size > len(patch):
            raise SPatchError("truncated chunk header")
        coded_len, out_crc = header.unpack_chunk_header(patch, pos)
        pos += header.chunk_header_size
        if pos + coded_len > len(patch):
            raise SPatchError("truncated chunk payload")
        yield coded_len, out_crc, patch[pos : pos + coded_len]
        pos += coded_len
    if pos != len(patch):
        raise SPatchError("trailing bytes after last chunk")


def apply_chunk(old: bytes, raw: bytes, new_offset: int, expected: int, old_size: int) -> bytes:
    """Reference decoder for one chunk's decoded op stream"""
    out = bytearray()
    cursor = new_offset
    pos = 0
    seek_pending = False
    while len(out) < expected:
        if pos >= len(raw):
            raise SPatchError("chunk op stream truncated")
        kind = raw[pos]
        operand, pos = read_uvar(raw, pos + 1)
        if kind == OP_SEEK:
            if seek_pending:
                raise SPatchError("consecutive SEEK ops")
            seek_pending = True
            cursor += unzigzag(operand)
            if not 0 <= cursor <= old_size:
                raise SPatchError("seek out of old image")
            continue
        if kind not in (OP_COPY, OP_ADD, OP_LIT):
            raise SPatchError(f"bad opcode {kind}")
        if operand == 0 or len(out) + operand > expected:
            raise SPatchError("op length invalid or crosses chunk boundary")
        seek_pending = False
        if kind in (OP_COPY, OP_ADD):
            if cursor + operand > old_size:
                raise SPatchError("copy beyond old image")
            base = old[cursor : cursor + operand]
            cursor += operand
        if kind == OP_COPY:
            out.extend(base)
        else:
            if pos + operand > len(raw):
                raise SPatchError("op payload truncated")
            payload = raw[pos : pos + operand]
            pos += operand
            if kind == OP_LIT:
                out.extend(payload)
            else:
                out.extend(bytes((b + d) & 0xFF for b, d in zip(base, payload)))
    if pos != len(raw):
        raise SPatchError("excess bytes in chunk op stream")
    return bytes(out)


def apply(old: bytes, patch: bytes) -> bytes:
    header = Header.unpack(patch)
    if len(old) != header.old_size or crc32(old) != header.old_crc:
        raise SPatchError("old image mismatch")
    decode = coders.DECODERS.get(header.coder_id)
    if decode is None:
        raise SPatchError(f"unknown coder id {header.coder_id}")
    out = bytearray()
    for index, (_, out_crc, coded) in enumerate(iter_chunks(patch, header)):
        start = index * header.chunk_bytes
        expected = min(header.chunk_bytes, header.new_size - start)
        try:
            raw = decode(coded)
        except coders.CoderError as exc:
            raise SPatchError(f"chunk {index}: {exc}") from exc
        produced = apply_chunk(old, raw, start, expected, header.old_size)
        if crc32(produced) != out_crc:
            raise SPatchError(f"chunk {index} CRC mismatch")
        out.extend(produced)
    if crc32(bytes(out)) != header.new_crc:
        raise SPatchError("new image CRC mismatch")
    return bytes(out)


def info(patch: bytes) -> dict:
    header = Header.unpack(patch)
    chunks = list(iter_chunks(patch, header))
    return {
        "coder": coders.IDS.get(header.coder_id, str(header.coder_id)),
        "old_size": header.old_size,
        "old_crc32": f"0x{header.old_crc:08x}",
        "new_size": header.new_size,
        "new_crc32": f"0x{header.new_crc:08x}",
        "size_width": header.size_width,
        "chunk_bytes": header.chunk_bytes,
        "chunk_count": header.chunk_count,
        "patch_size": len(patch),
        "largest_chunk_payload": max((len(c[2]) for c in chunks), default=0),
    }
