"""conftest.py:

Copyright (c) 2026 Michael Starch
Licensed under the Apache License, Version 2.0. See LICENSE for details.

Options for the Ref-variant campaign (test_ref_variants.py). The GDS options (--dictionary, --deployment-config)
come from the fprime-gds pytest plugin.
"""


def pytest_addoption(parser):
    group = parser.getgroup("spatch")
    group.addoption("--old-image", action="store", default=None, help="Old (on-board) image file for test_ref_variants")
    group.addoption("--new-image", action="store", default=None, help="New (target) image file for test_ref_variants")
    group.addoption(
        "--coders", action="store", default="none,rle,lzss", help="Comma-separated coders to exercise (none,rle,lzss)"
    )
    group.addoption("--chunk-bytes", action="store", type=int, default=4096, help="SPatch chunk size")
    group.addoption(
        "--uplink-dir", action="store", default=None, help="Directory the deployment's FileUplink accepts (required; GDS and flight software share the host)"
    )


def pytest_generate_tests(metafunc):
    if "coder_name" in metafunc.fixturenames:
        coders = [c.strip() for c in metafunc.config.getoption("--coders").split(",") if c.strip()]
        metafunc.parametrize("coder_name", coders)
