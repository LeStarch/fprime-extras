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
| DP-003 | Fixed codec state shall not exceed 1 KB with default configuration. | UT `RamBudget` |
| DP-004 | All work shall occur in bounded steps driven by `run` (a rate group); no thread is created. | Inspection, UT |
| DP-005 | The old image, each chunk's output, and the completed new image shall be CRC32-verified. | UT |
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
  or processes at most one chunk of `chunk_bytes` output, so per-tick time is bounded by configuration.
* **Media.** `old_file` and `patch_file` are opened `READ_ONLY`; `new_file` is opened `READ_WRITE` (created if
  absent, preserved otherwise so a partial output can be resumed).
* **Resume.** On `begin()`, the codec walks the existing `new_file` chunk-by-chunk, comparing the CRC of each
  prefix chunk to the CRC stored in the patch. Patching restarts at the first chunk that does not verify. No coder
  state is persisted because each chunk is coded independently. `PatchResumed(chunk)` reports the restart point.
* **Command completion.** `APPLY_PATCH` responds when the codec reaches `COMPLETE` (OK) or `FAILED`
  (EXECUTION_ERROR). Immediate rejections (`BUSY`, `OPEN_FAILED`, header/old-image failures) respond at once.
* **Abort.** `ABORT_PATCH` closes the media, responds `EXECUTION_ERROR` to the pending `APPLY_PATCH`, emits
  `PatchAborted` and leaves `new_file` in place. When idle it responds `VALIDATION_ERROR`.
* **Coder seam.** `DeltaPatcher::setCoder(DeltaCoder&)` (call before the topology starts) substitutes a
  project-supplied decompressor. The coder id in the SPatch header must match the installed coder.

### SPatch v1 container

All integers little-endian. CRC is IEEE 802.3 CRC32 (`Utils::crc32_ieee802_3`).

```
Header (32 B): "SPAT" | version U8 = 1 | coder_id U8 | flags U8 = 0 | reserved U8 = 0
               | old_size U32 | old_crc32 U32 | new_size U32 | new_crc32 U32
               | chunk_bytes U32 | header_crc32 U32 (over the first 28 bytes)
Chunk (repeated ceil(new_size / chunk_bytes) times):
               coded_len U32 | output_crc32 U32 | payload[coded_len]
```

Each chunk's payload is independently decoded by the coder and produces exactly `chunk_bytes` bytes (the final
chunk produces the remainder). The decoded operation stream is:

```
COPY  0x00  uvar n            new[o:o+n] = old[c:c+n]      (c = old cursor, advances)
ADD   0x01  uvar n, n bytes   new[o+i]   = old[c+i] + d[i]  (mod 256)
LIT   0x02  uvar n, n bytes   new[o:o+n] = bytes
SEEK  0x03  svar d            old cursor += d              (zigzag)
```

`uvar` is LEB128 (at most 5 bytes). Operations may not cross a chunk boundary. The old cursor resets to
`chunk_index * chunk_bytes` at each chunk start so chunks are independent.

Coder ids: `0` none, `1` RLE, `2` LZSS (256-byte window, original format; see `Delta/DeltaCoder.hpp`).

## Ports

| Name | Kind | Type | Description |
| --- | --- | --- | --- |
| `run` | sync input | `Svc.Sched` | Rate-group tick; drives command dispatch and one patch step |
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
| `OldImageMismatch` | warning high | Old image size/CRC differs from header |
| `ChunkFailed` | warning high | Chunk failed with `DeltaPatchStatus` |
| `PatchComplete` | activity high | New image written and verified |
| `PatchAborted` | activity high | Operator abort |

## Telemetry

| Name | Type | Description |
| --- | --- | --- |
| `State` | `DeltaPatchState` | IDLE / PATCHING / FAILED / COMPLETE |
| `ChunksDone` | U32 | Chunks verified or written |
| `ChunksTotal` | U32 | Chunk count from header |
| `BytesWritten` | U64 | Output bytes produced |
| `LastStatus` | `DeltaPatchStatus` | Last terminal status |

## Configuration

`FprimeExtras/FprimeExtrasConfig/ExtrasConfig/DeltaCodecConfig.hpp`

| Constant | Default | Purpose |
| --- | --- | --- |
| `DELTA_PATCH_BUFFER_SIZE` | 256 | Coded patch read buffer |
| `DELTA_RING_SIZE` | 256 | Decoded op stream ring / LZSS window |
| `DELTA_OUTPUT_BUFFER_SIZE` | 256 | Output staging buffer |
| `DELTA_VERIFY_BYTES_PER_STEP` | 4096 | CRC bytes per `run` tick during verification |

RAM (measured, x86-64): `DeltaCodec` 928 B + `DeltaCoderLzss` 32 B = 960 B. `Os::File` handles inside
`DeltaFileMedia` are platform-owned and outside this budget.

## Ground tooling

`python/fprime_extras_spatch` (`fprime-extras-spatch create|apply|verify|info`) produces and checks SPatch files. It
uses `bsdiff4` (BSD-2-Clause) or an external `hdiffz` (MIT) as the matcher; neither is part of flight code.

## Unit tests

`test/ut/` covers nominal, busy, open failure, bad header, old-image mismatch, chunk corruption + resume, abort +
resume, and abort while idle. The codec library tests live in `FprimeExtras/Update/Delta/test/ut/`.
