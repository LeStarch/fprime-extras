// ======================================================================
// \title  DeltaCodecConfig.hpp
// \author starchmd
// \brief  hpp file configuring the fixed buffers of the SPatch DeltaCodec
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#ifndef Update_DeltaCodecConfig_HPP
#define Update_DeltaCodecConfig_HPP
#include "Fw/FPrimeBasicTypes.hpp"
namespace Update {
//! Bytes of coded patch data read per media access
constexpr FwSizeType DELTA_PATCH_BUFFER_SIZE = 128;

//! Decoded operation window; also the history window of the shipped LZSS coder (must be >= 256 for that coder)
constexpr FwSizeType DELTA_WINDOW_SIZE = 256;

//! Bytes of new image accumulated before each media write; also the CRC-verification read granularity
constexpr FwSizeType DELTA_OUTPUT_BUFFER_SIZE = 128;

//! Old-image bytes cached per media read so that short adjacent COPY/ADD operations share one read
constexpr FwSizeType DELTA_OLD_BUFFER_SIZE = 128;

//! Bytes CRC-verified (old image, or existing new image on resume) per DeltaCodec::step()
constexpr FwSizeType DELTA_VERIFY_BYTES_PER_STEP = 4096;

//! Most media reads and writes issued in one DeltaCodec::step() in any phase;
//! when reached the chunk or verification pass continues on the next step, so
//! a tick's I/O time is bounded by the media rather than by the patch
constexpr FwSizeType DELTA_MAX_IO_PER_STEP = 64;

//! Largest chunk_bytes accepted from an SPatch header; bounds the new-image
//! bytes produced (and old-image bytes read) per chunk. Per-tick media work is
//! bounded separately by DELTA_MAX_IO_PER_STEP (a chunk may span steps)
constexpr U32 DELTA_MAX_CHUNK_BYTES = 8192;

//! Largest coded_len accepted for one chunk; bounds the patch bytes decoded in a single DeltaCodec::step()
constexpr U32 DELTA_MAX_CODED_CHUNK_BYTES = 2 * DELTA_MAX_CHUNK_BYTES;

//! Most operations (including SEEKs) accepted in one chunk; a valid chunk needs at most 2 per output byte
constexpr U32 DELTA_MAX_OPS_PER_CHUNK = 2 * DELTA_MAX_CHUNK_BYTES;

//! Largest old/new image size accepted from an SPatch header; bounds the storage consumed by the new image
constexpr U32 DELTA_MAX_IMAGE_SIZE = 64u * 1024u * 1024u;

// Configuration minimums: the engine makes progress only when every buffer
// holds a complete header/operation and every per-step budget is non-zero; derived limits must not wrap
static_assert(DELTA_PATCH_BUFFER_SIZE >= 64, "Patch buffer must hold a header (44 B) and a full operation");
static_assert(DELTA_WINDOW_SIZE >= 256, "Window must hold the shipped LZSS history (256 B)");
static_assert(DELTA_OUTPUT_BUFFER_SIZE >= 64, "Output buffer too small for bounded per-step progress");
static_assert(DELTA_OLD_BUFFER_SIZE >= 16, "Old-image cache too small to be useful");
static_assert(DELTA_VERIFY_BYTES_PER_STEP > 0, "Verification would never progress");
static_assert(DELTA_MAX_IO_PER_STEP >= 4, "A step needs at least a patch read, an old read and a write");
static_assert(DELTA_MAX_CHUNK_BYTES > 0 && DELTA_MAX_CHUNK_BYTES <= (1u << 30), "Chunk cap out of range");
static_assert(DELTA_MAX_CODED_CHUNK_BYTES >= DELTA_MAX_CHUNK_BYTES, "Coded cap must admit an uncoded chunk");
static_assert(DELTA_MAX_OPS_PER_CHUNK >= 2 * DELTA_MAX_CHUNK_BYTES, "Op cap must admit any valid chunk");
static_assert(DELTA_MAX_IMAGE_SIZE >= DELTA_MAX_CHUNK_BYTES, "Image cap must admit one chunk");

}  // namespace Update
#endif  // Update_DeltaCodecConfig_HPP
