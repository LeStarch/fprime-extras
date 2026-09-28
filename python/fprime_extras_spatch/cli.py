"""fprime-extras-spatch command line: create, apply, verify, and inspect SPatch files.

Copyright (c) 2026 Michael Starch
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import coders, spatch


def _read(path: str) -> bytes:
    return Path(path).read_bytes()


def cmd_create(args: argparse.Namespace) -> int:
    old = _read(args.old)
    new = _read(args.new)
    ops = spatch.ops_literal(new) if args.matcher == "none" else spatch.ops_from_bsdiff(old, new, args.copy_min)
    patch = spatch.create(old, new, coders.NAMES[args.coder], args.chunk_bytes, args.copy_min, ops)
    Path(args.patch).write_bytes(patch)
    print(f"{args.patch}: {len(patch)} bytes ({len(patch) * 100.0 / max(1, len(new)):.1f}% of new image)")
    return 0


def cmd_apply(args: argparse.Namespace) -> int:
    new = spatch.apply(_read(args.old), _read(args.patch))
    Path(args.new).write_bytes(new)
    print(f"{args.new}: {len(new)} bytes")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    new = spatch.apply(_read(args.old), _read(args.patch))
    if args.new is not None and new != _read(args.new):
        print("verify: FAILED (patched output differs from expected new image)", file=sys.stderr)
        return 1
    print("verify: OK")
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
    except (spatch.SPatchError, coders.CoderError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
