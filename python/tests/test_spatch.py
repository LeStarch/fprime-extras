"""Ground-tool regressions: flight-limit enforcement, malformed inputs, CLI hygiene.

Copyright (c) 2026 Michael Starch
"""

import json
import os

import pytest

from fprime_extras_spatch import cli, coders, spatch

OLD = bytes(range(256)) * 4
NEW = OLD[:300] + b"INSERT" + OLD[300:900] + b"\xaa" * 50


def make_patch(coder="lzss", chunk_bytes=256, width=8):
    return spatch.create(OLD, NEW, coders.NAMES[coder], chunk_bytes, size_width=width)


def write(path, data):
    path.write_bytes(data)
    return str(path)


# ---------------------------------------------------------------- flight limits


@pytest.mark.parametrize("width", spatch.SIZE_WIDTHS)
def test_round_trip(width):
    patch = make_patch(width=width)
    assert spatch.apply(OLD, patch) == NEW
    assert spatch.info(patch)["size_width"] == width


def test_create_rejects_chunk_bytes_above_flight_cap():
    with pytest.raises(spatch.SPatchError, match="DELTA_MAX_CHUNK_BYTES"):
        spatch.create(OLD, NEW, coders.ID_NONE, spatch.MAX_CHUNK_BYTES + 1)
    with pytest.raises(spatch.SPatchError):
        spatch.create(OLD, NEW, coders.ID_NONE, 0)


def test_header_unpack_rejects_oversized_geometry():
    header = spatch.Header(coders.ID_NONE, 10, spatch.crc32(b"x" * 10), 10, 0, spatch.MAX_CHUNK_BYTES + 1)
    with pytest.raises(spatch.SPatchError, match="DELTA_MAX_CHUNK_BYTES"):
        spatch.Header.unpack(header.pack())
    header = spatch.Header(coders.ID_NONE, spatch.MAX_IMAGE_SIZE + 1, 0, 10, 0, 256)
    with pytest.raises(spatch.SPatchError, match="DELTA_MAX_IMAGE_SIZE"):
        spatch.Header.unpack(header.pack())


def test_chunk_coded_len_above_flight_cap_rejected_by_apply_verify_info():
    header = spatch.Header.unpack(make_patch("none"))
    patch = header.pack() + header.pack_chunk_header(spatch.MAX_CODED_CHUNK_BYTES + 1, 0) + b"\x00"
    for entry in (lambda p: spatch.apply(OLD, p), spatch.info):
        with pytest.raises(spatch.SPatchError, match="DELTA_MAX_CODED_CHUNK_BYTES"):
            entry(patch)


def test_short_header_is_truncated_not_bad_magic():
    patch = make_patch()
    with pytest.raises(spatch.SPatchError, match="shorter than header"):
        spatch.Header.unpack(patch[:12])
    with pytest.raises(spatch.SPatchError, match="bad magic"):
        spatch.Header.unpack(b"XXXX" + patch[4:])
    with pytest.raises(spatch.SPatchError, match="size width"):
        spatch.Header.unpack(patch[:7] + b"\x03" + patch[8:])


# ---------------------------------------------------------------- coders


def test_lzss_trailing_flag_byte_is_malformed():
    # Mirrors DeltaCoderLzss::pending(): a flag byte that introduces no item is surplus
    assert coders.lzss_decode(bytes([0x40, ord("a"), 0x00, 0x00])) == b"aaaa"
    with pytest.raises(coders.CoderError, match="flag byte without items"):
        coders.lzss_decode(bytes([0x00]) + b"abcdefgh" + bytes([0x00]))
    # Legal unused flag bits in a final partial group still decode
    assert coders.lzss_decode(bytes([0x00, ord("a"), ord("b")])) == b"ab"


# ---------------------------------------------------------------- dictionary


def dictionary(tmp_path, body):
    path = tmp_path / "dict.json"
    path.write_text(body if isinstance(body, str) else json.dumps(body), encoding="utf-8")
    return str(path)


def test_dictionary_width(tmp_path):
    good = {"typeDefinitions": [{"qualifiedName": "FwSizeType", "underlyingType": {"size": 32}}]}
    assert spatch.size_width_from_dictionary(dictionary(tmp_path, good)) == 4


@pytest.mark.parametrize(
    "body",
    [
        "not json",
        "[]",
        {"typeDefinitions": {}},
        {"typeDefinitions": []},
        {"typeDefinitions": [{"qualifiedName": "FwSizeType", "underlyingType": {"size": 48}}]},
        {"typeDefinitions": [{"qualifiedName": "FwSizeType", "underlyingType": {"size": True}}]},
        {"typeDefinitions": [{"qualifiedName": "FwSizeType", "underlyingType": "U32"}]},
    ],
)
def test_dictionary_malformed(tmp_path, body):
    with pytest.raises(spatch.SPatchError):
        spatch.size_width_from_dictionary(dictionary(tmp_path, body))


def test_dictionary_invalid_utf8_and_empty(tmp_path):
    path = tmp_path / "dict.json"
    path.write_bytes(b"\xff\xfe\x00")
    with pytest.raises(spatch.SPatchError, match="not valid JSON"):
        spatch.size_width_from_dictionary(str(path))
    with pytest.raises(spatch.SPatchError, match="empty"):
        spatch.size_width_from_dictionary("")


# ---------------------------------------------------------------- CLI


def test_cli_empty_dictionary_is_error(tmp_path, capsys):
    old = write(tmp_path / "old.bin", OLD)
    new = write(tmp_path / "new.bin", NEW)
    out = str(tmp_path / "p.spatch")
    assert cli.main(["create", old, new, out, "--dictionary", ""]) != 0
    assert "dictionary" in capsys.readouterr().err
    assert not os.path.exists(out)


def test_cli_output_aliasing_input_rejected(tmp_path):
    old = write(tmp_path / "old.bin", OLD)
    new = write(tmp_path / "new.bin", NEW)
    alias = str(tmp_path / "." / "old.bin")
    assert cli.main(["create", old, new, alias]) != 0
    assert (tmp_path / "old.bin").read_bytes() == OLD
    os.symlink(old, tmp_path / "link.bin")
    assert cli.main(["create", old, new, str(tmp_path / "link.bin")]) != 0
    assert (tmp_path / "old.bin").read_bytes() == OLD


def test_cli_stdout_clean_and_info_json(tmp_path, capsys):
    old = write(tmp_path / "old.bin", OLD)
    new = write(tmp_path / "new.bin", NEW)
    out = str(tmp_path / "p.spatch")
    assert cli.main(["create", old, new, out]) == 0
    captured = capsys.readouterr()
    assert captured.out == ""
    assert "bytes" in captured.err
    assert cli.main(["info", out]) == 0
    info = json.loads(capsys.readouterr().out)
    assert info["new_size"] == len(NEW)
    produced = str(tmp_path / "re.bin")
    assert cli.main(["apply", old, out, produced]) == 0
    assert capsys.readouterr().out == ""
    assert open(produced, "rb").read() == NEW
    assert cli.main(["verify", old, out, new]) == 0


def test_cli_atomic_write_preserves_output_on_failure(tmp_path, monkeypatch):
    target = tmp_path / "out.bin"
    target.write_bytes(b"keep")

    def boom(*_args, **_kwargs):
        raise OSError("disk full")

    monkeypatch.setattr(cli.os, "fsync", boom)
    with pytest.raises(OSError):
        cli._write(str(target), b"new")
    assert target.read_bytes() == b"keep"
    assert [p.name for p in tmp_path.iterdir()] == ["out.bin"]  # no temp file left behind


def test_cli_non_regular_input_rejected(tmp_path, capsys):
    old = write(tmp_path / "old.bin", OLD)
    assert cli.main(["info", str(tmp_path)]) != 0
    assert "not a regular file" in capsys.readouterr().err
    assert cli.main(["apply", old, str(tmp_path / "missing.spatch"), str(tmp_path / "x")]) != 0


def test_corrupt_patch_rejected():
    patch = bytearray(make_patch("rle"))
    header = spatch.Header.unpack(bytes(patch))
    patch[header.header_size + header.chunk_header_size] ^= 0xFF
    with pytest.raises(spatch.SPatchError):
        spatch.apply(OLD, bytes(patch))
    good = make_patch("none")
    with pytest.raises(spatch.SPatchError, match="trailing bytes"):
        spatch.apply(OLD, good + b"\x00")
    with pytest.raises(spatch.SPatchError, match="truncated"):
        spatch.apply(OLD, good[:-3])
