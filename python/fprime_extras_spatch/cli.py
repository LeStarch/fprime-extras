"""fprime-extras-spatch command line: create, apply, verify, and inspect SPatch files.

Copyright (c) 2026 Michael Starch
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

from . import coders, spatch


def _status(message: str) -> None:
    """Status text goes to stderr so stdout stays clean for machine consumers (e.g. `info` JSON)"""
    print(message, file=sys.stderr)


def _read(path: str) -> bytes:
    if not path:
        raise spatch.SPatchError("empty path")
    target = Path(path)
    if not target.is_file():
        raise spatch.SPatchError(f"{path}: not a regular file")
    return target.read_bytes()


def _check_output(output: str, *inputs: str) -> None:
    """Refuse an output path that aliases one of the inputs (would clobber the data being read)"""
    if not output:
        raise spatch.SPatchError("empty output path")
    if Path(output).is_dir():
        raise spatch.SPatchError(f"{output}: is a directory")
    for source in inputs:
        if source and os.path.exists(output) and os.path.exists(source) and os.path.samefile(output, source):
            raise spatch.SPatchError(f"output {output} aliases input {source}")
        if os.path.realpath(output) == os.path.realpath(source):
            raise spatch.SPatchError(f"output {output} aliases input {source}")


def _write(output: str, data: bytes) -> None:
    """Write atomically: a failure (e.g. ENOSPC) never leaves a partial file at `output`"""
    directory = os.path.dirname(os.path.abspath(output)) or "."
    handle, temp = tempfile.mkstemp(prefix=".spatch-", dir=directory)
    try:
        with os.fdopen(handle, "wb") as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temp, output)
    except BaseException:
        try:
            os.unlink(temp)
        except OSError:
            pass
        raise


def cmd_create(args: argparse.Namespace) -> int:
    _check_output(args.patch, args.old, args.new)
    if args.dictionary is not None and not args.dictionary:
        raise spatch.SPatchError("--dictionary requires a path")
    old = _read(args.old)
    new = _read(args.new)
    ops = spatch.ops_literal(new) if args.matcher == "none" else spatch.ops_from_bsdiff(old, new, args.copy_min)
    size_width = spatch.size_width_from_dictionary(args.dictionary) if args.dictionary else args.size_width
    patch = spatch.create(old, new, coders.NAMES[args.coder], args.chunk_bytes, args.copy_min, ops, size_width)
    _write(args.patch, patch)
    _status(f"{args.patch}: {len(patch)} bytes ({len(patch) * 100.0 / max(1, len(new)):.1f}% of new image)")
    if len(patch) > len(new):
        _status("warning: patch is larger than the new image; uplinking the image directly is cheaper")
    return 0


def cmd_apply(args: argparse.Namespace) -> int:
    _check_output(args.new, args.old, args.patch)
    new = spatch.apply(_read(args.old), _read(args.patch))
    _write(args.new, new)
    _status(f"{args.new}: {len(new)} bytes")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    new = spatch.apply(_read(args.old), _read(args.patch))
    if args.new is not None and new != _read(args.new):
        _status("verify: FAILED (patched output differs from expected new image)")
        return 1
    _status("verify: OK")
    return 0


def cmd_info(args: argparse.Namespace) -> int:
    print(json.dumps(spatch.info(_read(args.patch)), indent=2))
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="fprime-extras-spatch", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    create = sub.add_parser("create", help="create a patch turning OLD into NEW")
    create.add_argument("old")
    create.add_argument("new")
    create.add_argument("patch")
    create.add_argument("--coder", choices=sorted(coders.NAMES), default="lzss")
    create.add_argument("--matcher", choices=["bsdiff4", "none"], default="bsdiff4")
    create.add_argument("--chunk-bytes", type=int, default=spatch.DEFAULT_CHUNK_BYTES)
    create.add_argument(
        "--copy-min", type=int, default=spatch.DEFAULT_COPY_MIN, help="minimum zero-delta run folded into a COPY op"
    )
    width = create.add_mutually_exclusive_group()
    width.add_argument(
        "--size-width",
        type=int,
        choices=spatch.SIZE_WIDTHS,
        default=spatch.DEFAULT_SIZE_WIDTH,
        help="sizeof(FwSizeType) of the flight build (default %(default)s)",
    )
    width.add_argument("--dictionary", help="F Prime JSON topology dictionary from which to read the FwSizeType width")
    create.set_defaults(func=cmd_create)

    apply_ = sub.add_parser("apply", help="apply PATCH to OLD producing NEW (reference decoder)")
    apply_.add_argument("old")
    apply_.add_argument("patch")
    apply_.add_argument("new")
    apply_.set_defaults(func=cmd_apply)

    verify = sub.add_parser("verify", help="apply PATCH to OLD in memory and check CRCs (and NEW if given)")
    verify.add_argument("old")
    verify.add_argument("patch")
    verify.add_argument("new", nargs="?")
    verify.set_defaults(func=cmd_verify)

    info = sub.add_parser("info", help="print header and chunk statistics")
    info.add_argument("patch")
    info.set_defaults(func=cmd_info)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (spatch.SPatchError, coders.CoderError, OSError, MemoryError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("error: interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
