"""test_ref_variants.py:

Copyright (c) 2026 Michael Starch
Licensed under the Apache License, Version 2.0. See LICENSE for details.

Ref-variant campaign for Update.DeltaPatcher: patch one real deployment binary into another on board, once per
shipped coder. The images are supplied by the caller (built variants of the Ref deployment: components added or
removed, configuration changed, ...):

    pytest test_ref_variants.py --dictionary <dict> --deployment-config <int_config.json> \
        --old-image build/Ref.base --new-image build/Ref.variant [--coders none,rle,lzss] [--chunk-bytes 4096]

The old image and the patch are placed in the uplink directory directly (GDS and flight software share the host, so
multi-megabyte binaries need not go through FileUplink), APPLY_PATCH is commanded and the produced image is compared
byte for byte with the target. The flight coder id in each patch header must match the coder installed in the
deployment: with the shipped default (LZSS) the none/rle patches are expected to be rejected CODER_MISMATCH and the
test records that instead of failing, so one run reports the behaviour of all three coders.

@author Michael Starch
@copyright Michael Starch, 2026
@license Apache-2.0
"""

import os
import shutil
import zlib
from pathlib import Path

import pytest
from fprime_extras_spatch import coders, spatch
from fprime_gds.common.testing_fw import predicates


def patcher(fprime_test_api, item):
    return fprime_test_api.get_mnemonic("Update.DeltaPatcher", item)


class AnyEvent(predicates.event_predicate):
    """Event predicate satisfied by any one of several event predicates (get_event_pred passes it through)."""

    def __init__(self, options):
        super().__init__()
        self.options = list(options)

    def __call__(self, event):
        return any(option(event) for option in self.options)

    def __str__(self):
        return " or ".join(str(option) for option in self.options)


@pytest.fixture(scope="module")
def size_width(request):
    return spatch.size_width_from_dictionary(request.config.getoption("--dictionary"))


@pytest.fixture(scope="module")
def images(request):
    old_path = request.config.getoption("--old-image")
    new_path = request.config.getoption("--new-image")
    if not old_path or not new_path:
        pytest.skip("--old-image and --new-image are required for the Ref-variant campaign")
    old = Path(old_path).read_bytes()
    new = Path(new_path).read_bytes()
    assert old and new, "images must not be empty"
    return old, new


@pytest.fixture
def uplink(request):
    directory = Path(request.config.getoption("--uplink-dir"))
    directory.mkdir(parents=True, exist_ok=True)
    paths = {name: directory / f"variant_{name}" for name in ("old.bin", "u.spatch", "new.bin")}
    for path in paths.values():
        if path.exists():
            path.unlink()
    yield paths
    for path in paths.values():
        if path.exists():
            path.unlink()


def expected_chunks(new_size, chunk_bytes):
    return (new_size + chunk_bytes - 1) // chunk_bytes


def test_variant_patch(fprime_test_api, images, size_width, uplink, coder_name, request):
    old, new = images
    chunk_bytes = request.config.getoption("--chunk-bytes")
    coder_id = coders.NAMES[coder_name]
    patch = spatch.create(old, new, coder_id, chunk_bytes, size_width=size_width)
    # Ground check first: the reference applier must reproduce the target
    assert spatch.apply(old, patch) == new
    uplink["old.bin"].write_bytes(old)
    uplink["u.spatch"].write_bytes(patch)
    ratio = len(patch) / max(1, len(new))
    print(
        f"[variant] coder={coder_name} old={len(old)} new={len(new)} patch={len(patch)} ({ratio:.1%})"
        f" chunks={expected_chunks(len(new), chunk_bytes)}"
    )

    args = [str(uplink["old.bin"]), str(uplink["u.spatch"]), str(uplink["new.bin"])]
    command = patcher(fprime_test_api, "APPLY_PATCH")
    # One tick per chunk plus the verification passes; allow generously for large images on a slow rate group
    timeout = 60 + 2 * expected_chunks(len(new), chunk_bytes)
    rejected = fprime_test_api.get_event_pred(patcher(fprime_test_api, "PatchRejected"), ["CODER_MISMATCH"])
    complete = fprime_test_api.get_event_pred(patcher(fprime_test_api, "PatchComplete"))
    outcome = AnyEvent([rejected, complete])
    event = fprime_test_api.send_and_await_event(command, args, outcome, timeout=timeout)
    assert event is not None, f"neither PatchComplete nor PatchRejected(CODER_MISMATCH) within {timeout} s"
    if rejected(event):
        # The deployment's installed coder differs from this patch's coder: documented behaviour, not a fault
        pytest.skip(f"coder {coder_name} is not the coder installed in this deployment (CODER_MISMATCH)")
    produced = uplink["new.bin"].read_bytes()
    assert len(produced) == len(new)
    assert produced == new
    assert event.get_args()[2].val == (zlib.crc32(new) & 0xFFFFFFFF)
    fprime_test_api.assert_telemetry(patcher(fprime_test_api, "State"), "COMPLETE", timeout=10)
    fprime_test_api.assert_telemetry(patcher(fprime_test_api, "LastStatus"), "OP_OK", timeout=10)
