# Update::DeltaPatcher — Software Design Document (SDD)

`Update.DeltaPatcher` reconstructs a new firmware image from an old image and an uplinked **SPatch** delta file.
It exists to minimize uplink volume (a delta is typically a few percent of the image) while holding flight RAM to
under 1 KB of fixed state. The reconstructed image is then installed with the existing
`Update.Updater.UPDATE_IMAGE_FROM` command.

## Requirements

| ID | Requirement | Verification |
| --- | --- | --- |
| DP-001 | The component shall reconstruct `new_file` from `old_file` and an SPatch `patch_file`. | UT Nominal |
| DP-002 | The component shall never write to `old_file`. | Inspection (`READ_ONLY` media) |
| DP-003 | Fixed codec state shall not exceed 1 KB with default configuration. | UT `DeltaCodec.MemoryFootprint` (Delta library) |
| DP-004 | All work shall occur in bounded steps driven by `run` (a rate group); no thread is created. | Inspection, UT |
| DP-005 | The old image, each chunk's output, and the completed new image (flushed and read back from the media) shall be CRC32-verified. | UT |
| DP-006 | An interrupted or failed patch shall resume at the last verified chunk boundary when re-commanded. | UT `AbortAndResume`, `ChunkFailed` |
| DP-007 | The decompression coder shall be replaceable by the project (`setCoder`). | Inspection, UT |
| DP-008 | `APPLY_PATCH` shall complete `OK` only after the new image is fully written and verified. | UT |
| DP-009 | A second `APPLY_PATCH` while patching shall be rejected with `BUSY`. | UT `Busy` |
| DP-010 | `ABORT_PATCH` shall stop patching and retain the partial output for resume. | UT `AbortAndResume` |

## Design

```
            APPLY_PATCH / ABORT_PATCH (async, queued)
                          |
 rateGroup ---> run ----> DeltaPatcher ----> patchComplete (optional)
                            |  DeltaCodec (streaming engine, 3 x 256 B buffers)
                            |    DeltaCoder (LZSS default | RLE | None | project)
                            |    DeltaMedia x3 (Os::File; flash region later)
```

* **Queued component.** `run_handler` dispatches at most `MAX_DISPATCH_PER_TICK` queued messages and then, if
  patching, performs exactly one `DeltaCodec::step()`. A step verifies at most `DELTA_VERIFY_BYTES_PER_STEP` bytes
  or processes one chunk of at most `DELTA_MAX_CHUNK_BYTES` output bytes decoded from at most
  `DELTA_MAX_CODED_CHUNK_BYTES` patch bytes; headers exceeding these caps (or `DELTA_MAX_IMAGE_SIZE`) are rejected
  with `BAD_HEADER`/`BAD_OPCODE`. Because a coder may expand its input (LZSS up to 129:1), the decoded operation
  stream is additionally bounded by construction: a `SEEK` must be followed by a producing op, so a chunk never
  parses more than `2 * chunk_bytes` operations before it is rejected with `BAD_OPCODE`. Per-tick work is thus
  bounded by flight configuration rather than by the patch. Media I/O is bounded separately: a step issues at most
  `DELTA_MAX_IO_PER_STEP` media reads/writes (plus the up-to-three calls of the operation in flight) and a chunk
  that needs more carries over to the next tick; adjacent `COPY`/`ADD` reads are served from a `DELTA_OLD_BUFFER_SIZE`
  old-image cache so operation-dense patches do not cost one media read per operation. The step performs
  `Os::File` I/O on the rate-group thread (`NO_WAIT` writes; the media is flushed once per committed chunk and once
  before read-back, never per write); drive `run` from a slow, non-critical rate group. Both commands use the FPP `hook` overflow
  policy: a command arriving on a full queue is answered `BUSY` immediately on the caller's thread (`APPLY_PATCH`
  also emits `PatchRejected(BUSY)`), so `Svc::CmdDispatcher` never waits on a lost response.
* **Media.** `old_file`, `patch_file` and `new_file` must be distinct (see *Path aliasing*). Before any open,
  `DeltaFileMedia` classifies each path with `Os::FileSystem::getPathType`: `old_file` and `patch_file` must be
  regular files and `new_file` must be a regular file or absent; directories, FIFOs, devices and symbolic links are
  rejected `OPEN_FAILED` so a path with no writer can never block the rate-group thread in `open()`. `old_file` and
  `patch_file` are opened `READ_ONLY`; `new_file` is opened `READ_WRITE` (created if absent, preserved otherwise so a
  partial output can be resumed; an output created by a command whose header is then rejected is removed again). If
  an existing `new_file` is larger than the header's `new_size` the command is rejected with `OUTPUT_STALE`; the
  operator removes it (e.g. `Svc.FileManager.RemoveFile`) and re-issues `APPLY_PATCH`. Media whose size changes
  underneath a running patch is reported as `TRUNCATED` (patch) or `OLD_IMAGE_MISMATCH` (old image); an existing
  output that becomes unreadable during the resume walk restarts patching at that chunk.
* **Chunk transaction.** A chunk's output is staged through the output buffer and may be partially written while
  the chunk is in progress, but the last slice is held back until the chunk's structure has been validated (coded
  bytes exhausted, operation stream drained, window empty, coder not `pending()`) and the chunk CRC matches. Only
  then is the slice written and the media flushed. A rejected chunk therefore never exists on the media as a
  complete, CRC-valid chunk; the committed prefix is exactly the chunks that passed.
* **Resume.** On `begin()`, the codec walks the existing `new_file` chunk-by-chunk, comparing the CRC of each
  prefix chunk to the CRC stored in the patch. Patching restarts at the first chunk that does not verify. No coder
  state is persisted because each chunk is coded independently. `PatchResumed(chunk)` reports the restart point.
  Skipped chunks are trusted on CRC alone (they are not re-decoded); CRC32 detects accidental corruption, not a
  deliberate forgery — see *Integrity*.
* **Read-back.** After the last chunk the codec calls `DeltaMedia::flush()` on the new image (`WRITE_ERROR` on
  failure), then `DeltaMedia::refresh()` — `DeltaFileMedia` re-resolves `new_file` by path (closing the handle and
  re-opening it, `NEW_IMAGE_MISMATCH` if the path no longer names a regular file) so `COMPLETE` attests to the file
  the operator will find at `new_file`, not to an orphaned inode that was replaced during patching — and enters
  `VERIFY_FINAL`, re-reading the stored image in `DELTA_VERIFY_BYTES_PER_STEP` slices and
  comparing size and CRC to the header (`NEW_IMAGE_MISMATCH` otherwise). `COMPLETE` therefore attests to the bytes
  on the media, not to the bytes handed to it; a project flash-region `DeltaMedia` gets write-then-verify for free.
* **Command completion.** `APPLY_PATCH` responds when the codec reaches `COMPLETE` (OK) or `FAILED`
  (EXECUTION_ERROR). Rejections detected in the command handler (`BUSY`, `SAME_FILE`, `OPEN_FAILED`, header
  failures, old-image *size* mismatch, `OUTPUT_STALE`) respond at once with `PatchRejected`. Failures detected while
  stepping (old-image *CRC* mismatch → `OldImageMismatch`; resume-verification, chunk, or final-image errors →
  `ChunkFailed`) complete the deferred command with EXECUTION_ERROR. For final-image errors (`PATCH_SIZE_MISMATCH`,
  `NEW_IMAGE_MISMATCH`) `ChunkFailed.chunk` equals `ChunksTotal`: every chunk verified, but the patch has trailing
  bytes or the stored image differs from the header. Resuming will not help; regenerate and re-uplink the patch.
* **Abort.** `ABORT_PATCH` closes the media, responds `EXECUTION_ERROR` to the pending `APPLY_PATCH`, emits
  `PatchAborted` and leaves `new_file` in place. When idle it emits `AbortIgnored` and responds `VALIDATION_ERROR`.
* **Coder seam.** `DeltaPatcher::setCoder(DeltaCoder&)` (call during topology setup, before rate groups start; `FW_ASSERT`s if a patch is in progress; the coder must outlive the component) substitutes a
  project-supplied decompressor. The coder id in the SPatch header must match the installed coder; an otherwise
  valid header naming a different coder is rejected with `CODER_MISMATCH` (distinct from `BAD_HEADER`, which
  indicates corruption or an out-of-range geometry).
  A coder implements `id()`, `reset()`, `decode()` and `pending()`; after a chunk's coded bytes are exhausted the
  engine requires `pending()` to be false (no unfinished token, no undelivered output) or the chunk fails
  `BAD_OPCODE` even when the delivered bytes matched the chunk CRC.
* **Path aliasing.** `old_file`, `patch_file` and `new_file` are resolved textually (`Os::FilePathUtils::resolveFromCwd`:
  `.`/`..`/duplicate separators, CWD for relative paths) and must name three distinct paths or the command is rejected
  `SAME_FILE` before any file is opened. Symbolic links are rejected as non-regular files (above). On POSIX targets
  (`TGT_OS_TYPE_LINUX`/`TGT_OS_TYPE_DARWIN`) the component additionally compares device/inode identity of the
  existing paths, so a hard link to the old image is also `SAME_FILE`; other platforms rely on the textual check and
  the `FileSystem` type check only (FatFs has neither links nor special files).
* **Integrity.** All CRCs are CRC32 (IEEE 802.3), chosen for the uplink and RAM budgets. CRC32 is linear and
  detects accidental corruption only; it is not authentication. A patch that is deliberately crafted to collide is
  not detected by this component — authenticity of uplinked files is the responsibility of the uplink path (e.g.
  signed images checked by the bootloader) and is out of scope for SPatch.
* **Scheduling.** `run` is a `guarded` input: exactly one rate group must drive it. Connecting `run` is mandatory
  (commands are queued and never dispatched otherwise, and `Svc::CmdDispatcher` then accumulates unanswered
  entries). Driving `run` from two rate groups is not supported — the guard serialises the calls so state cannot be
  corrupted, but it also makes the faster group block on the slower one's I/O. Queue sizing: each `run` drains at
  most `MAX_DISPATCH_PER_TICK` commands, so the component's queue depth bounds outstanding commands and the `hook`
  overflow answers `BUSY` beyond it.
* **Command path sizing.** Command string arguments are deserialised in flight into `Fw::CmdStringArg`
  (`FW_CMD_STRING_MAX_SIZE`, F Prime default 40), whatever size the FPP declares. `APPLY_PATCH` therefore declares
  its three path arguments as `string size FW_CMD_STRING_MAX_SIZE`, so the dictionary tells the ground the true
  limit and an over-long path is rejected at the GDS instead of being silently truncated on board (and then
  rejected `OPEN_FAILED`, or worse, naming a different file). Events keep `FileNameStringSize`. Deployments with
  longer update paths raise `FW_CMD_STRING_MAX_SIZE` in their `FpConstants.fpp`; all three arguments must still fit
  one command packet (`FW_COM_BUFFER_MAX_SIZE`).

### SPatch v2 container

Integers are serialized F Prime style: big-endian, with sizes carried as `FwSizeType` (`W` = `sizeof(FwSizeType)`,
4 or 8, recorded in the header so a patch built for the wrong width is rejected with `SIZE_WIDTH_MISMATCH`) and
CRCs as fixed `U32`. CRC is IEEE 802.3 CRC32 (`Utils::crc32_ieee802_3`). The flight side decodes the fields with
`Fw::ExternalSerializeBuffer`; the ground tool takes the width from the deployment dictionary (`--dictionary`) or
`--size-width`.

```
Header (20 + 3W B; 32 B at W=4, 44 B at W=8):
               "SPAT" | version U8 = 2 | coder_id U8 | flags U8 = 0 | size_width U8 = W
               | old_size FwSizeType | old_crc32 U32 | new_size FwSizeType | new_crc32 U32
               | chunk_bytes FwSizeType | header_crc32 U32 (over all preceding header bytes)
Chunk (W + 4 B header, repeated ceil(new_size / chunk_bytes) times):
               coded_len FwSizeType | output_crc32 U32 | payload[coded_len]
```

Each chunk's payload is independently decoded by the coder and produces exactly `chunk_bytes` bytes (the final
chunk produces the remainder). The decoded operation stream is:

```
COPY  0x00  uvar n            new[o:o+n] = old[c:c+n]      (c = old cursor, advances)
ADD   0x01  uvar n, n bytes   new[o+i]   = old[c+i] + d[i]  (mod 256)
LIT   0x02  uvar n, n bytes   new[o:o+n] = bytes
SEEK  0x03  svar d            old cursor += d              (zigzag)
```

`uvar` is LEB128 (at most 5 bytes). Operations may not cross a chunk boundary; `n > 0`; a `SEEK` must be directly
followed by `COPY`/`ADD`/`LIT` (two consecutive `SEEK`s are `BAD_OPCODE`); a chunk may hold at most
`DELTA_MAX_OPS_PER_CHUNK` operations. The old cursor resets to
`chunk_index * chunk_bytes` at each chunk start so chunks are independent.

Coder ids: `0` none, `1` RLE, `2` LZSS (256-byte window, original format; see `Delta/DeltaCoder.hpp`).

The chunk count and each chunk's raw length are derived from `new_size` and `chunk_bytes` rather than carried in
the file, and the per-chunk CRC covers the *decoded output* rather than the coded payload: a corrupted payload
either fails to decode (reported as `BAD_OPCODE` or `TRUNCATED`) or produces output whose CRC does not match, while the output
CRC additionally guards against decoder faults and is what makes the resume scan (re-reading the new image only)
possible without touching the patch payload.

## Ports

| Name | Kind | Type | Description |
| --- | --- | --- | --- |
| `run` | guarded input | `Svc.Sched` | Rate-group tick (mandatory, exactly one driver); drains the command queue and performs one bounded patch step |
| `patchComplete` | output | `Update.UpdateFile` | `(new_file, new_crc32)` on success; optional |

## Commands

| Name | Arguments | Description |
| --- | --- | --- |
| `APPLY_PATCH` | `old_file`, `patch_file`, `new_file` | Start or resume reconstruction; completes at end of patch |
| `ABORT_PATCH` | — | Stop patching, retain partial output |

## Events

| Name | Severity | Description |
| --- | --- | --- |
| `PatchStarted` | activity high | Files accepted, header verified |
| `PatchResumed` | activity high | Resumed at the given chunk |
| `PatchRejected` | warning high | Command rejected with `DeltaPatchStatus` |
| `OldImageMismatch` | warning high | Old image CRC differs from header (size mismatch is reported via `PatchRejected`) |
| `ChunkFailed` | warning high | Chunk (or, with `chunk == ChunksTotal`, final image) failed with `DeltaPatchStatus` |
| `PatchComplete` | activity high | New image written and verified |
| `PatchAborted` | activity high | Operator abort |
| `AbortIgnored` | warning low | `ABORT_PATCH` received while not patching |

## Telemetry

| Name | Type | Description |
| --- | --- | --- |
| `State` | `DeltaPatchState` | IDLE / PATCHING / FAILED / COMPLETE |
| `ChunksDone` | U32 | Chunks verified or written |
| `ChunksTotal` | U32 | Chunk count from header |
| `BytesWritten` | FwSizeType | New-image bytes written or verified so far (chunk granularity) |
| `LastStatus` | `DeltaPatchStatus` | Last terminal status |

## Configuration

`FprimeExtras/FprimeExtrasConfig/ExtrasConfig/DeltaCodecConfig.hpp`

| Constant | Default | Purpose |
| --- | --- | --- |
| `DELTA_PATCH_BUFFER_SIZE` | 128 | Coded patch read buffer |
| `DELTA_WINDOW_SIZE` | 256 | Decoded op stream window / LZSS history (`DeltaWindow` over `Types::CircularBuffer`) |
| `DELTA_OUTPUT_BUFFER_SIZE` | 128 | Output staging buffer; also the verification read granularity |
| `DELTA_OLD_BUFFER_SIZE` | 128 | Old-image read cache serving COPY/ADD operations |
| `DELTA_MAX_IO_PER_STEP` | 64 | Media reads+writes per `run` tick while patching (a chunk may span ticks) |
| `DELTA_VERIFY_BYTES_PER_STEP` | 4096 | CRC bytes per `run` tick during verification |
| `DELTA_MAX_CHUNK_BYTES` | 8192 | Largest header `chunk_bytes` accepted; bounds output bytes per `run` tick |
| `DELTA_MAX_CODED_CHUNK_BYTES` | 16384 | Largest `coded_len` accepted; bounds patch bytes decoded per tick |
| `DELTA_MAX_OPS_PER_CHUNK` | 16384 | Most operations parsed per chunk; explicit per-tick work bound (2 per output byte suffices) |
| `DELTA_MAX_IMAGE_SIZE` | 64 MiB | Largest old/new image accepted; bounds storage used by `new_file` |

`static_assert`s in the header reject configurations below the working minimums (buffers that cannot hold a header
or an operation, a window smaller than the LZSS history, zero verification progress, an I/O budget below one
operation, caps that cannot admit a valid chunk).

RAM (measured, x86-64, 64-bit `FwSizeType`): `DeltaCodec` 984 B + `DeltaCoderLzss` 32 B = 1016 B. `Os::File` handles inside
`DeltaFileMedia` are platform-owned and outside this budget.

## Ground tooling

`python/fprime_extras_spatch` (`fprime-extras-spatch create|apply|verify|info`) produces and checks SPatch files. It
uses `bsdiff4` (BSD-2-Clause) as the matcher (an `hdiffz`/HDiffPatch, MIT, backend is planned); no matcher code is
part of flight code. The tool enforces the same chunk/image caps as `DeltaCodecConfig.hpp` on every entry point
(`create`, `apply`, `verify`, `info`), applies the flight coders' rule that a trailing LZSS flag byte introducing no
item is malformed, writes outputs atomically (temporary file + rename), refuses an output path that aliases an
input, validates the dictionary it reads `FwSizeType` from, and keeps stdout clean for binary output.

## Unit tests

`test/ut/` covers nominal, busy, open failure, bad header, coder mismatch, old-image mismatch, chunk corruption +
resume, abort + resume, abort while idle, same-file rejection, and queue overflow. The codec library tests live in
`FprimeExtras/Update/Delta/test/ut/`.

## Integration tests

`test/int/test_delta_patcher.py` runs against a deployment that instantiates the Update subtopology, connects a
rate group to `deltaPatcher.run`, and lets FileUplink write to `/tmp/uplink` (GDS and FSW on the same host). It
needs `fprime-gds` and `pip install ./python`, and resolves the instance through the integration-config key
`Update.DeltaPatcher`:

    pytest FprimeExtras/Update/DeltaPatcher/test/int --dictionary <deployment>/dict/<Topology>TopologyDictionary.json \
        --deployment-config <deployment>/test/int/int_config.json
