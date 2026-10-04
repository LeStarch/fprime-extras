# fprime-extras-spatch

Ground tooling for **SPatch**, the delta-update container consumed by `Update.DeltaPatcher` in fprime-extras.
This package contains the reference encoder/decoder for the container, the operation stream and the three
shipped coders (`none`, `rle`, `lzss`), and a command line to create, apply, verify and inspect patches.

```bash
pip install ./python
fprime-extras-spatch create old.bin new.bin update.spatch            # lzss coder, bsdiff4 matcher
fprime-extras-spatch create old.bin new.bin update.spatch --coder rle --chunk-bytes 4096
fprime-extras-spatch info   update.spatch
fprime-extras-spatch verify old.bin update.spatch new.bin            # decode and compare
fprime-extras-spatch apply  old.bin update.spatch out.bin            # reference decoder
```

`--chunk-bytes` (default 4096) trades a little compression for finer-grained resume and shorter per-tick work on
the flight side. The coder must match the coder installed in the flight `DeltaPatcher` (LZSS by default).
Sizes are serialized as the flight build's `FwSizeType`: pass `--dictionary <deployment dictionary.json>` to read
its width, or `--size-width 4|8` (default 8); a mismatch is rejected on board with `SIZE_WIDTH_MISMATCH`.
Status messages (including `verify: OK`/`FAILED`) go to stderr; only `info` writes JSON to stdout. Errors exit 2
(`verify` mismatch exits 1, interrupt 130). Outputs are written atomically and may not alias an input.

## Format

See `FprimeExtras/Update/DeltaPatcher/docs/sdd.md` for the SPatch v2 container, operation and coder formats. The
C++ decoder in `FprimeExtras/Update/Delta/` is the flight implementation; `spatch.py`/`coders.py` are the
reference implementation; `FprimeExtras/Update/Delta/test/ut/generate_vectors.py` (run after `pip install ./python`,
with the `bsdiff4` version it records) emits ground-produced vectors that `DeltaTestMain.cpp` checks the flight
decoder against.

## Matchers and licensing

The **matcher** (the algorithm that finds which parts of the old image to reuse) runs only on the ground. The
matcher's output is translated into SPatch operations, so the flight decoder is independent of the matcher.

| Matcher | Where | License | Notes |
| --- | --- | --- | --- |
| `bsdiff4` (default) | pip dependency, in-process | BSD-2-Clause (bsdiff algorithm by Colin Percival, Python binding by Ilan Schnell) | Used unmodified as a library; not redistributed here |
| `none` | built in | — | Emits the new image as literals; benchmarking baseline |

No bsdiff/bspatch source is vendored into this repository or into flight code. The flight-side operation
decoder, coders and container are original work released under the repository's Apache-2.0 license. The LZSS
coder is an original small-window format and decoder; it does not include code from heatshrink or other
compressors.

Adding an `hdiffz` (HDiffPatch, MIT) backend is planned; in an informal comparison on two small binaries its
uncoded patches were 10–30 % smaller than `bsdiff4`'s.
