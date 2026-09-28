"""test_delta_patcher.py:

Copyright (c) 2026 Michael Starch
Licensed under the Apache License, Version 2.0. See LICENSE for details.

Integration tests for Update.DeltaPatcher. The deployment under test must instantiate the Update subtopology,
connect a rate group to `deltaPatcher.run`, and configure FileUplink to accept files under UPLINK_DIR. The test
generates a synthetic old/new image pair, builds an SPatch with the fprime-extras-spatch ground tool, uplinks
the old image and the patch, commands APPLY_PATCH, and verifies the produced image on the local filesystem
(the GDS and flight software run on the same host in this test configuration).

The instance is resolved through the integration config key "Update.DeltaPatcher".

@author Michael Starch
@copyright Michael Starch, 2025
@license Apache-2.0
"""

import os
import random
import struct
import tempfile
import zlib
from pathlib import Path

import pytest
from fprime_extras_spatch import coders, spatch
from fprime_gds.common.testing_fw import predicates

UPLINK_DIR = "/tmp/uplink"
OLD_FILE = f"{UPLINK_DIR}/old.bin"
PATCH_FILE = f"{UPLINK_DIR}/u.spatch"
NEW_FILE = f"{UPLINK_DIR}/new.bin"
CHUNK_BYTES = 1024
IMAGE_BYTES = 12 * CHUNK_BYTES


def make_images(seed=1):
    rng = random.Random(seed)
    old = bytearray(rng.randbytes(IMAGE_BYTES))
    new = bytearray(old)
    for _ in range(64):
        pos = rng.randrange(IMAGE_BYTES)
        new[pos] = (new[pos] + rng.randrange(1, 256)) & 0xFF
    new[2000:2000] = rng.randbytes(37)
    del new[7000:7100]
    return bytes(old), bytes(new)


def patcher(fprime_test_api, item):
    return fprime_test_api.get_mnemonic("Update.DeltaPatcher", item)


def send_and_await_error(fprime_test_api, args, timeout, command="APPLY_PATCH"):
    """Send a patcher command and await the dispatcher's OpCodeError completion for it."""
    dispatcher = fprime_test_api.get_mnemonic("Svc.CommandDispatcher")
    command = patcher(fprime_test_api, command)
    opcode = fprime_test_api.translate_command_name(command)
    error = fprime_test_api.get_event_pred(f"{dispatcher}.OpCodeError", [opcode, None])
    assert fprime_test_api.send_and_await_event(command, args, [error], timeout=timeout)


def uplink(fprime_test_api, local, remote):
    assert fprime_test_api.uplink_file_and_await_completion(
        local, destination=remote, timeout=60
    )


@pytest.fixture
def images(fprime_test_api):
    os.makedirs(UPLINK_DIR, exist_ok=True)
    for path in (OLD_FILE, PATCH_FILE, NEW_FILE):
        if os.path.exists(path):
            os.unlink(path)
    old, new = make_images()
    patch = spatch.create(old, new, coder_id=coders.ID_LZSS, chunk_bytes=CHUNK_BYTES)
    assert spatch.apply(old, patch) == new
    with tempfile.TemporaryDirectory() as tmp:
        Path(tmp, "old.bin").write_bytes(old)
        Path(tmp, "u.spatch").write_bytes(patch)
        uplink(fprime_test_api, str(Path(tmp, "old.bin")), OLD_FILE)
        uplink(fprime_test_api, str(Path(tmp, "u.spatch")), PATCH_FILE)
    fprime_test_api.clear_histories()
    yield old, new, patch


def test_apply_patch_nominal(fprime_test_api, images):
    """APPLY_PATCH produces a CRC-verified new image and reports completion."""
    _old, new, _patch = images
    chunks = -(-len(new) // CHUNK_BYTES)
    fprime_test_api.send_and_assert_command(
        patcher(fprime_test_api, "APPLY_PATCH"),
        [OLD_FILE, PATCH_FILE, NEW_FILE],
        timeout=120,
    )
    fprime_test_api.assert_event(
        patcher(fprime_test_api, "PatchStarted"),
        [PATCH_FILE, OLD_FILE, NEW_FILE, chunks],
    )
    fprime_test_api.assert_event(
        patcher(fprime_test_api, "PatchComplete"),
        [NEW_FILE, len(new), zlib.crc32(new) & 0xFFFFFFFF],
    )
    assert Path(NEW_FILE).read_bytes() == new
    fprime_test_api.assert_telemetry(
        patcher(fprime_test_api, "State"), "COMPLETE", timeout=10
    )
    fprime_test_api.assert_telemetry(
        patcher(fprime_test_api, "ChunksDone"), chunks, timeout=10
    )


def test_apply_patch_missing_old(fprime_test_api, images):
    """APPLY_PATCH against a missing old image fails with OPEN_FAILED and rejects the command."""
    send_and_await_error(
        fprime_test_api, [f"{UPLINK_DIR}/missing.bin", PATCH_FILE, NEW_FILE], timeout=30
    )
    fprime_test_api.assert_event(patcher(fprime_test_api, "PatchRejected"), None)


def test_apply_patch_corrupt_chunk_then_resume(fprime_test_api, images):
    """A corrupted chunk fails (CRC or malformed stream); re-uplinking a good patch resumes and completes."""
    _old, new, patch = images
    header = spatch.Header.unpack(patch)
    bad = bytearray(patch)
    # Flip a byte in the payload of the second chunk (zero-based chunk index 1)
    offset = spatch.HEADER_SIZE
    (coded_len,) = struct.unpack_from("<I", bad, offset)
    offset += spatch.CHUNK_HEADER_SIZE + coded_len
    bad[offset + spatch.CHUNK_HEADER_SIZE + 3] ^= 0xFF
    with tempfile.TemporaryDirectory() as tmp:
        Path(tmp, "u.spatch").write_bytes(bad)
        uplink(fprime_test_api, str(Path(tmp, "u.spatch")), PATCH_FILE)
    fprime_test_api.clear_histories()
    send_and_await_error(fprime_test_api, [OLD_FILE, PATCH_FILE, NEW_FILE], timeout=120)
    failure = predicates.is_a_member_of(["CHUNK_CRC", "BAD_OPCODE", "TRUNCATED"])
    fprime_test_api.assert_event(patcher(fprime_test_api, "ChunkFailed"), [1, failure])
    fprime_test_api.assert_telemetry(
        patcher(fprime_test_api, "State"), "FAILED", timeout=10
    )

    with tempfile.TemporaryDirectory() as tmp:
        Path(tmp, "u.spatch").write_bytes(patch)
        uplink(fprime_test_api, str(Path(tmp, "u.spatch")), PATCH_FILE)
    fprime_test_api.clear_histories()
    fprime_test_api.send_and_assert_command(
        patcher(fprime_test_api, "APPLY_PATCH"),
        [OLD_FILE, PATCH_FILE, NEW_FILE],
        timeout=120,
    )
    fprime_test_api.assert_event(patcher(fprime_test_api, "PatchResumed"), None)
    fprime_test_api.assert_event(patcher(fprime_test_api, "PatchComplete"), None)
    assert Path(NEW_FILE).read_bytes() == new
    assert header.new_crc == zlib.crc32(new) & 0xFFFFFFFF


def test_abort_idle(fprime_test_api):
    """ABORT_PATCH with no patch in progress is rejected with a validation error."""
    send_and_await_error(fprime_test_api, None, timeout=10, command="ABORT_PATCH")


def test_abort_in_progress_then_resume(fprime_test_api, images):
    """ABORT_PATCH mid-patch fails the pending APPLY_PATCH with ABORTED; a second APPLY_PATCH resumes."""
    _old, new, _patch = images
    dispatcher = fprime_test_api.get_mnemonic("Svc.CommandDispatcher")
    apply_cmd = patcher(fprime_test_api, "APPLY_PATCH")
    apply_opcode = fprime_test_api.translate_command_name(apply_cmd)
    fprime_test_api.send_command(apply_cmd, [OLD_FILE, PATCH_FILE, NEW_FILE])
    assert (
        fprime_test_api.await_event(
            patcher(fprime_test_api, "PatchStarted"), timeout=10
        )
        is not None
    )
    fprime_test_api.send_and_assert_command(
        patcher(fprime_test_api, "ABORT_PATCH"), timeout=10
    )
    fprime_test_api.assert_event(
        patcher(fprime_test_api, "PatchAborted"), None, timeout=10
    )
    fprime_test_api.assert_event(
        f"{dispatcher}.OpCodeError", [apply_opcode, None], timeout=10
    )
    fprime_test_api.assert_telemetry(
        patcher(fprime_test_api, "State"), "IDLE", timeout=10
    )
    assert Path(NEW_FILE).read_bytes() != new

    fprime_test_api.clear_histories()
    fprime_test_api.send_and_assert_command(
        apply_cmd, [OLD_FILE, PATCH_FILE, NEW_FILE], timeout=120
    )
    fprime_test_api.assert_event(patcher(fprime_test_api, "PatchComplete"), None)
    assert Path(NEW_FILE).read_bytes() == new
