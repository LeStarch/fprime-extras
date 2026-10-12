// ======================================================================
// \title  DeltaCodec.cpp
// \author starchmd
// \brief  cpp file for the SPatch v2 streaming patch engine
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"

#include <cstring>

#include "Fw/Types/Assert.hpp"
#include "Fw/Types/Serializable.hpp"
#include "Utils/Hash/Crc32/Crc32.hpp"

namespace Update {

static constexpr U8 SPATCH_MAGIC[4] = {'S', 'P', 'A', 'T'};
static_assert(sizeof(SPATCH_MAGIC) == DeltaCodec::HDR_VERSION, "Magic precedes the version byte");
static constexpr FwSizeType VARINT_MAX_BYTES = 5;  // 35 bits: covers U32 lengths and zigzag 33-bit seeks

DeltaCodec::DeltaCodec(DeltaCoder& coder)
    : m_coder(&coder),
      m_old(nullptr),
      m_patch(nullptr),
      m_new(nullptr),
      m_patchBuffer(),
      m_windowBuffer(),
      m_outBuffer(),
      m_oldBuffer(),
      m_window(m_windowBuffer, DELTA_WINDOW_SIZE),
      m_header(),
      m_progress(),
      m_verify(),
      m_chunk(),
      m_oldCache(),
      m_ioCount(0) {
    this->reset();
}

// ----------------------------------------------------------------------
// Public interface
// ----------------------------------------------------------------------

void DeltaCodec::reset() {
    this->m_old = nullptr;
    this->m_patch = nullptr;
    this->m_new = nullptr;
    this->m_progress.chunkIndex = 0;
    this->m_progress.patchPos = 0;
    this->m_progress.resumedChunks = 0;
    this->m_progress.verifyChunks = 0;
    this->m_progress.runningCrc = crcInit();
    this->m_progress.committedCrc = crcInit();
    this->m_verify.offset = 0;
    this->m_verify.crc = crcInit();
    this->m_chunk.active = false;
    this->m_oldCache.offset = 0;
    this->m_oldCache.length = 0;
    this->m_ioCount = 0;
}

void DeltaCodec::setCoder(DeltaCoder& coder) {
    FW_ASSERT(this->m_old == nullptr);
    this->m_coder = &coder;
}

DeltaCodec::Status DeltaCodec::begin(DeltaMedia& oldImage, DeltaMedia& patch, DeltaMedia& newImage) {
    this->reset();
    this->m_old = &oldImage;
    this->m_patch = &patch;
    this->m_new = &newImage;

    const Status headerStatus = this->readHeader(patch);
    if (headerStatus != OP_OK) {
        return headerStatus;
    }
    this->m_progress.patchPos = HEADER_SIZE;

    FwSizeType oldSize = 0;
    if (oldImage.size(oldSize) != DeltaMedia::OP_OK) {
        return READ_ERROR;
    }
    if (oldSize != this->m_header.oldSize) {
        return OLD_IMAGE_MISMATCH;
    }

    // Resume: whole chunks already present in the output are verified before patching continues
    FwSizeType newSize = 0;
    if (newImage.size(newSize) != DeltaMedia::OP_OK) {
        return READ_ERROR;
    }
    if (newSize > this->m_header.newSize) {
        return OUTPUT_STALE;
    }
    if (newSize == this->m_header.newSize) {
        this->m_progress.verifyChunks = this->m_header.chunkCount;
    } else {
        this->m_progress.verifyChunks = newSize / this->m_header.chunkBytes;
    }
    return OP_OK;
}

FwSizeType DeltaCodec::bytesWritten() const {
    const FwSizeType written = this->m_progress.chunkIndex * this->m_header.chunkBytes;
    return (written > this->m_header.newSize) ? this->m_header.newSize : written;
}

// ----------------------------------------------------------------------
// Header validation
// ----------------------------------------------------------------------

// SPatch header layout (W = sizeof(FwSizeType), recorded in the header; all integers big-endian)
//
//     offset      size  field
//     0            4    magic "SPAT"
//     4            1    version        = 2
//     5            1    coder_id       (must equal the installed coder's id)
//     6            1    flags          = 0
//     7            1    size_width     = W (4 or 8)
//     8            W    old_size
//     8 + W        4    old_crc32
//     12 + W       W    new_size
//     12 + 2W      4    new_crc32
//     16 + 2W      W    chunk_bytes    (1 .. DELTA_MAX_CHUNK_BYTES)
//     16 + 3W      4    header_crc32   over bytes [0, 16 + 3W)
//     20 + 3W           total: 32 B at W = 4, 44 B at W = 8
//
// Validated in this order: magic/version/flags, size_width (the layout depends on it), length, header CRC,
// coder id (an intact header naming another coder is a configuration disagreement, not corruption), geometry caps.
DeltaCodec::Status DeltaCodec::readHeader(DeltaMedia& patch) {
    static_assert(DELTA_PATCH_BUFFER_SIZE >= HEADER_SIZE, "Patch buffer must hold the SPatch header");
    if (patch.size(this->m_header.patchSize) != DeltaMedia::OP_OK) {
        return READ_ERROR;
    }
    // The fixed prefix (magic, version, coder, flags, size width) is validated before the width-dependent remainder
    // so that a short patch from the other width is still reported as SIZE_WIDTH_MISMATCH
    const FwSizeType prefixLength = (this->m_header.patchSize < HEADER_SIZE) ? this->m_header.patchSize : HEADER_SIZE;
    if (prefixLength < HDR_OLD_SIZE) {
        return TRUNCATED;
    }
    if (patch.read(0, this->m_patchBuffer, prefixLength) != DeltaMedia::OP_OK) {
        return READ_ERROR;
    }
    const U8* header = this->m_patchBuffer;
    for (FwSizeType i = 0; i < sizeof(SPATCH_MAGIC); i++) {
        if (header[i] != SPATCH_MAGIC[i]) {
            return BAD_HEADER;
        }
    }
    if (header[HDR_VERSION] != VERSION || header[HDR_FLAGS] != 0) {
        return BAD_HEADER;
    }
    if (header[HDR_SIZE_WIDTH] != SIZE_FIELD_WIDTH) {
        return SIZE_WIDTH_MISMATCH;
    }
    if (this->m_header.patchSize < HEADER_SIZE) {
        return TRUNCATED;
    }
    Fw::ExternalSerializeBuffer fields(this->m_patchBuffer, HEADER_SIZE);
    U32 headerCrc = 0;
    if (fields.setBuffLen(HEADER_SIZE) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeSkip(HDR_OLD_SIZE) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_header.oldSize) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_header.oldCrc) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_header.newSize) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_header.newCrc) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_header.chunkBytes) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(headerCrc) != Fw::FW_SERIALIZE_OK) {
        return BAD_HEADER;
    }
    if (crcFinal(crcUpdate(crcInit(), header, HDR_CRC)) != headerCrc) {
        return BAD_HEADER;
    }
    if (header[HDR_CODER] != this->m_coder->id()) {
        return CODER_MISMATCH;
    }
    if (this->m_header.chunkBytes == 0 || this->m_header.chunkBytes > DELTA_MAX_CHUNK_BYTES ||
        this->m_header.oldSize > DELTA_MAX_IMAGE_SIZE || this->m_header.newSize > DELTA_MAX_IMAGE_SIZE) {
        return BAD_HEADER;
    }
    // Sizes are capped at DELTA_MAX_IMAGE_SIZE above, so this cannot wrap
    this->m_header.chunkCount = (this->m_header.newSize + this->m_header.chunkBytes - 1) / this->m_header.chunkBytes;
    return OP_OK;
}

// Chunk header layout (W = sizeof(FwSizeType), big-endian), one per chunk, ceil(new_size / chunk_bytes) chunks
//
//     offset   size        field
//     0        W           coded_len      payload bytes that follow (0 .. DELTA_MAX_CODED_CHUNK_BYTES)
//     W        4           output_crc32   CRC32 of the chunk's decoded output, not of the payload
//     W + 4    coded_len   payload        coder stream decoding to the operation stream (see parseOp())
//
// The chunk's raw output length is not carried: it is min(chunk_bytes, new_size - index * chunk_bytes).
DeltaCodec::Status DeltaCodec::readChunkHeader(FwSizeType& codedLength, U32& crc) {
    const Progress& progress = this->m_progress;
    FW_ASSERT(progress.patchPos <= this->m_header.patchSize, static_cast<FwAssertArgType>(progress.patchPos));
    if (CHUNK_HEADER_SIZE > this->m_header.patchSize - progress.patchPos) {
        return TRUNCATED;
    }
    this->m_ioCount++;
    if (this->m_patch->read(progress.patchPos, this->m_patchBuffer, CHUNK_HEADER_SIZE) != DeltaMedia::OP_OK) {
        return this->readFailure(*this->m_patch, this->m_header.patchSize, TRUNCATED);
    }
    Fw::ExternalSerializeBuffer fields(this->m_patchBuffer, CHUNK_HEADER_SIZE);
    if (fields.setBuffLen(CHUNK_HEADER_SIZE) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(codedLength) != Fw::FW_SERIALIZE_OK || fields.deserializeTo(crc) != Fw::FW_SERIALIZE_OK) {
        return BAD_OPCODE;
    }
    if (codedLength > DELTA_MAX_CODED_CHUNK_BYTES) {
        return BAD_OPCODE;
    }
    // Subtraction form: patchPos + CHUNK_HEADER_SIZE <= patchSize was established above, so this cannot wrap
    if (codedLength > this->m_header.patchSize - (progress.patchPos + CHUNK_HEADER_SIZE)) {
        return TRUNCATED;
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::readFailure(DeltaMedia& media, FwSizeType expectedSize, Status mismatch) {
    FwSizeType currentSize = 0;
    if (media.size(currentSize) == DeltaMedia::OP_OK && currentSize != expectedSize) {
        return mismatch;
    }
    return READ_ERROR;
}

// ----------------------------------------------------------------------
// Verification stages
// ----------------------------------------------------------------------

DeltaCodec::Status DeltaCodec::verifySlice(DeltaMedia& media,
                                           FwSizeType regionStart,
                                           FwSizeType regionLength,
                                           FwSizeType& budget) {
    FW_ASSERT(this->m_verify.offset < regionLength, static_cast<FwAssertArgType>(this->m_verify.offset));
    const FwSizeType remaining = regionLength - this->m_verify.offset;
    FwSizeType length = (remaining < DELTA_OUTPUT_BUFFER_SIZE) ? remaining : DELTA_OUTPUT_BUFFER_SIZE;
    length = (length < budget) ? length : budget;
    this->m_ioCount++;
    if (media.read(regionStart + this->m_verify.offset, this->m_outBuffer, length) != DeltaMedia::OP_OK) {
        return READ_ERROR;
    }
    this->m_verify.crc = crcUpdate(this->m_verify.crc, this->m_outBuffer, length);
    this->m_verify.offset += length;
    budget -= length;
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::verifyNew(StepResult& result) {
    FW_ASSERT(this->m_new != nullptr);
    result = IN_PROGRESS;
    this->m_ioCount = 0;
    Progress& progress = this->m_progress;
    const Header& header = this->m_header;
    FwSizeType budget = DELTA_VERIFY_BYTES_PER_STEP;
    for (FwSizeType pass = 0; pass < MAX_VERIFY_PASSES && budget > 0 && progress.chunkIndex < progress.verifyChunks;
         pass++) {
        const FwSizeType chunkStart = progress.chunkIndex * header.chunkBytes;
        const FwSizeType chunkLength =
            ((header.newSize - chunkStart) < header.chunkBytes) ? (header.newSize - chunkStart) : header.chunkBytes;
        const U32 crcBefore = progress.runningCrc;
        const FwSizeType offsetBefore = this->m_verify.offset;
        if (this->verifySlice(*this->m_new, chunkStart, chunkLength, budget) != OP_OK) {
            // The existing output cannot be read here (e.g. it shrank underneath us): patch from this chunk on
            progress.runningCrc = progress.committedCrc;
            progress.verifyChunks = progress.chunkIndex;
            break;
        }
        progress.runningCrc = crcUpdate(crcBefore, this->m_outBuffer, this->m_verify.offset - offsetBefore);
        if (this->m_verify.offset == chunkLength) {
            FwSizeType codedLength = 0;
            U32 expectedCrc = 0;
            const Status status = this->readChunkHeader(codedLength, expectedCrc);
            if (status != OP_OK) {
                return status;
            }
            if (crcFinal(this->m_verify.crc) == expectedCrc) {
                progress.committedCrc = progress.runningCrc;
                progress.chunkIndex++;
                progress.resumedChunks++;
                progress.patchPos += CHUNK_HEADER_SIZE + codedLength;
            } else {
                // Chunk is corrupt: resume from here, discarding its contribution
                progress.runningCrc = progress.committedCrc;
                progress.verifyChunks = progress.chunkIndex;
            }
            this->m_verify.offset = 0;
            this->m_verify.crc = crcInit();
        }
    }
    if (progress.chunkIndex >= progress.verifyChunks) {
        this->m_verify.offset = 0;
        this->m_verify.crc = crcInit();
        result = STAGE_DONE;
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::verifyOld(StepResult& result) {
    FW_ASSERT(this->m_old != nullptr);
    result = IN_PROGRESS;
    this->m_ioCount = 0;
    const Header& header = this->m_header;
    FwSizeType budget = DELTA_VERIFY_BYTES_PER_STEP;
    for (FwSizeType pass = 0; pass < MAX_VERIFY_PASSES && budget > 0 && this->m_verify.offset < header.oldSize;
         pass++) {
        if (this->verifySlice(*this->m_old, 0, header.oldSize, budget) != OP_OK) {
            return this->readFailure(*this->m_old, header.oldSize, OLD_IMAGE_MISMATCH);
        }
    }
    if (this->m_verify.offset >= header.oldSize) {
        // Kept finalized so actualOldCrc() can be reported alongside the header's value
        this->m_verify.crc = crcFinal(this->m_verify.crc);
        if (this->m_verify.crc != header.oldCrc) {
            return OLD_IMAGE_MISMATCH;
        }
        result = STAGE_DONE;
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::verifyFinal(StepResult& result) {
    FW_ASSERT(this->m_new != nullptr);
    result = IN_PROGRESS;
    this->m_ioCount = 0;
    const Header& header = this->m_header;
    FwSizeType budget = DELTA_VERIFY_BYTES_PER_STEP;
    for (FwSizeType pass = 0; pass < MAX_VERIFY_PASSES && budget > 0 && this->m_verify.offset < header.newSize;
         pass++) {
        if (this->verifySlice(*this->m_new, 0, header.newSize, budget) != OP_OK) {
            return this->readFailure(*this->m_new, header.newSize, NEW_IMAGE_MISMATCH);
        }
    }
    if (this->m_verify.offset >= header.newSize) {
        FwSizeType storedSize = 0;
        if (this->m_new->size(storedSize) != DeltaMedia::OP_OK) {
            return READ_ERROR;
        }
        if (storedSize != header.newSize || crcFinal(this->m_verify.crc) != header.newCrc) {
            return NEW_IMAGE_MISMATCH;
        }
        result = STAGE_DONE;
    }
    return OP_OK;
}

// ----------------------------------------------------------------------
// Patching stage
// ----------------------------------------------------------------------

DeltaCodec::Status DeltaCodec::patchChunk(StepResult& result) {
    FW_ASSERT(this->m_new != nullptr);
    result = IN_PROGRESS;
    this->m_ioCount = 0;
    if (this->m_progress.chunkIndex >= this->m_header.chunkCount) {
        return this->finishImage(result);
    }
    Chunk& chunk = this->m_chunk;
    if (!chunk.active) {
        const Status status = this->beginChunk();
        if (status != OP_OK) {
            return this->failChunk(status);
        }
    }
    // Every pass parses one operation (capped by DELTA_MAX_OPS_PER_CHUNK) or
    // produces at least one output byte (capped by chunk.expected), so their sum
    // bounds the loop; the I/O budget may end the call earlier
    const FwSizeType maxPasses = DELTA_MAX_OPS_PER_CHUNK + chunk.expected + 1;
    for (FwSizeType pass = 0; pass < maxPasses && chunk.produced < chunk.expected; pass++) {
        const Status status = chunk.opActive ? this->executeOp(chunk) : this->parseOp(chunk);
        if (status != OP_OK) {
            return this->failChunk(status);
        }
        if (this->m_ioCount >= DELTA_MAX_IO_PER_STEP) {
            // Budget spent: the chunk (and the unwritten tail of its output) carries over to the next tick
            return OP_OK;
        }
    }
    if (chunk.produced < chunk.expected) {
        return this->failChunk(BAD_OPCODE);
    }
    const Status status = this->finishChunk();
    if (status != OP_OK) {
        return this->failChunk(status);
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::failChunk(Status status) {
    FW_ASSERT(status != OP_OK);
    this->m_progress.runningCrc = this->m_progress.committedCrc;
    this->m_chunk.active = false;
    return status;
}

DeltaCodec::Status DeltaCodec::finishImage(StepResult& result) {
    if (this->m_progress.patchPos != this->m_header.patchSize) {
        return PATCH_SIZE_MISMATCH;
    }
    if (crcFinal(this->m_progress.committedCrc) != this->m_header.newCrc) {
        return NEW_IMAGE_MISMATCH;
    }
    // The CRC above covers what was handed to the media; completion requires the stored bytes to agree
    if (this->m_new->flush() != DeltaMedia::OP_OK) {
        return WRITE_ERROR;
    }
    // Read back through a freshly resolved handle so an output path replaced or
    // removed beneath the open handle (an orphaned inode would still read back
    // bit-exact) is reported as a mismatch rather than attested
    if (this->m_new->refresh() != DeltaMedia::OP_OK) {
        return NEW_IMAGE_MISMATCH;
    }
    this->m_verify.offset = 0;
    this->m_verify.crc = crcInit();
    result = STAGE_DONE;
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::beginChunk() {
    Chunk& chunk = this->m_chunk;
    chunk.codedLength = 0;
    chunk.expectedCrc = 0;
    const Status status = this->readChunkHeader(chunk.codedLength, chunk.expectedCrc);
    if (status != OP_OK) {
        return status;
    }
    chunk.codedRemaining = chunk.codedLength;
    chunk.patchFill = 0;
    chunk.patchPos = 0;
    chunk.newOffset = this->m_progress.chunkIndex * this->m_header.chunkBytes;
    const FwSizeType left = this->m_header.newSize - chunk.newOffset;
    chunk.expected = (left < this->m_header.chunkBytes) ? left : this->m_header.chunkBytes;
    chunk.produced = 0;
    chunk.oldCursor = chunk.newOffset;
    chunk.outFill = 0;
    chunk.crc = crcInit();
    chunk.op = OP_COPY;
    chunk.opActive = false;
    chunk.opRemaining = 0;
    chunk.seekPending = false;
    chunk.opCount = 0;

    this->m_coder->reset();
    this->m_window.reset();
    chunk.active = true;
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::finishChunk() {
    Chunk& chunk = this->m_chunk;
    FW_ASSERT(chunk.produced == chunk.expected);
    // The chunk is validated in full before its final output slice reaches the
    // media: everything declared for the chunk must have been consumed exactly,
    // the coder must end at a token boundary, and the output CRC must match. A
    // chunk that fails here therefore never exists on the media as a complete,
    // CRC-valid chunk that a later resume could trust.
    if (chunk.codedRemaining != 0 || chunk.patchPos != chunk.patchFill || this->m_window.count() != 0 ||
        this->m_coder->pending()) {
        return BAD_OPCODE;
    }
    const U32 finalCrc = crcUpdate(chunk.crc, this->m_outBuffer, chunk.outFill);
    if (crcFinal(finalCrc) != chunk.expectedCrc) {
        return CHUNK_CRC;
    }
    Status status = this->flushOutput(chunk);
    if (status != OP_OK) {
        return status;
    }
    // One media flush per chunk boundary (not per write) keeps the tick's I/O time bounded
    if (this->m_new->flush() != DeltaMedia::OP_OK) {
        return WRITE_ERROR;
    }
    this->m_progress.committedCrc = this->m_progress.runningCrc;
    this->m_progress.patchPos += CHUNK_HEADER_SIZE + chunk.codedLength;
    this->m_progress.chunkIndex++;
    chunk.active = false;
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::fillWindow(Chunk& chunk) {
    // Each pass pushes at least one byte or returns, so the free window space bounds the passes
    const FwSizeType maxPasses = this->m_window.space();
    for (FwSizeType pass = 0; pass < maxPasses && this->m_window.space() > 0; pass++) {
        if (chunk.patchPos >= chunk.patchFill && chunk.codedRemaining > 0) {
            const FwSizeType length =
                (chunk.codedRemaining < DELTA_PATCH_BUFFER_SIZE) ? chunk.codedRemaining : DELTA_PATCH_BUFFER_SIZE;
            const FwSizeType offset =
                this->m_progress.patchPos + CHUNK_HEADER_SIZE + (chunk.codedLength - chunk.codedRemaining);
            this->m_ioCount++;
            if (this->m_patch->read(offset, this->m_patchBuffer, length) != DeltaMedia::OP_OK) {
                return this->readFailure(*this->m_patch, this->m_header.patchSize, TRUNCATED);
            }
            chunk.codedRemaining -= length;
            chunk.patchFill = length;
            chunk.patchPos = 0;
        }
        // Called even with no input left so a coder may drain pending output (e.g. an in-progress match)
        FwSizeType consumed = 0;
        const FwSizeType before = this->m_window.count();
        const FwSizeType available = chunk.patchFill - chunk.patchPos;
        const DeltaCoder::Status status =
            this->m_coder->decode(this->m_patchBuffer + chunk.patchPos, available, consumed, this->m_window);
        if (status != DeltaCoder::OP_OK) {
            return BAD_OPCODE;
        }
        FW_ASSERT(consumed <= available, static_cast<FwAssertArgType>(consumed));
        chunk.patchPos += consumed;
        if (consumed == 0 && this->m_window.count() == before) {
            // No progress: either input is exhausted (caller decides) or the coder is stuck on input it has
            return (available == 0) ? OP_OK : BAD_OPCODE;
        }
    }
    return OP_OK;
}

// Varint layout: unsigned LEB128, 7 payload bits per byte, least significant group first, at most 5 bytes
//
//     +---------------+---------------+-----+---------------+
//     | 1 b6..b0      | 1 b13..b7     | ... | 0 b34..b28    |     continuation bit set on all but the last byte
//     +---------------+---------------+-----+---------------+
DeltaCodec::VarintResult DeltaCodec::readVarint(FwSizeType start, U64& value, FwSizeType& length) const {
    value = 0;
    length = 0;
    for (FwSizeType i = 0; i < VARINT_MAX_BYTES; i++) {
        if (start + i >= this->m_window.count()) {
            return VARINT_NEED_MORE;
        }
        const U8 byte = this->m_window.peek(start + i);
        value |= static_cast<U64>(byte & 0x7F) << (7 * i);
        length = i + 1;
        if ((byte & 0x80) == 0) {
            return VARINT_OK;
        }
    }
    return VARINT_INVALID;
}

// Operation layout (the decoded chunk payload is a sequence of these; o = new-image offset, c = old cursor,
// c starts at the chunk's new-image offset)
//
//     +--------+------------------+----------------+
//     | op U8  | operand varint   | data           |
//     +--------+------------------+----------------+
//     COPY 0x00  n (uvar, n > 0)   -                 new[o:o+n] = old[c:c+n];           c += n
//     ADD  0x01  n (uvar, n > 0)   n bytes d[]       new[o+i]   = old[c+i] + d[i] mod 256; c += n
//     LIT  0x02  n (uvar, n > 0)   n bytes           new[o:o+n] = bytes
//     SEEK 0x03  d (zigzag svar)   -                 c += d, 0 <= c <= old_size; must be followed by COPY/ADD/LIT
//
// n may not exceed the chunk's remaining output; operations never span a chunk boundary; at most
// DELTA_MAX_OPS_PER_CHUNK operations per chunk. The data bytes of ADD/LIT are consumed by executeOp().
DeltaCodec::Status DeltaCodec::parseOp(Chunk& chunk) {
    // Ensure the op byte and complete operand are decoded; the window holds at least 6 bytes so this always fits
    static_assert(DELTA_WINDOW_SIZE > VARINT_MAX_BYTES, "Window must hold an op byte and its operand");
    U64 operand = 0;
    FwSizeType operandLength = 0;
    // Each pass adds at least one window byte or returns; an op byte plus operand needs at most 1 + VARINT_MAX_BYTES
    VarintResult result = VARINT_NEED_MORE;
    for (FwSizeType pass = 0; pass <= 1 + VARINT_MAX_BYTES && result == VARINT_NEED_MORE; pass++) {
        if (this->m_window.count() >= 1) {
            result = this->readVarint(1, operand, operandLength);
        }
        if (result == VARINT_INVALID) {
            return BAD_OPCODE;
        } else if (result == VARINT_NEED_MORE) {
            const FwSizeType before = this->m_window.count();
            const Status status = this->fillWindow(chunk);
            if (status != OP_OK) {
                return status;
            }
            if (this->m_window.count() == before) {
                return TRUNCATED;
            }
        }
    }
    if (result != VARINT_OK) {
        return BAD_OPCODE;
    }
    const U8 opByte = this->m_window.peek(0);
    this->m_window.pop(1 + operandLength);
    // Explicit work bound: every producing op emits at least one byte and at most one SEEK precedes it
    chunk.opCount++;
    if (chunk.opCount > DELTA_MAX_OPS_PER_CHUNK) {
        return BAD_OPCODE;
    }

    switch (opByte) {
        case OP_SEEK: {
            // A SEEK is only meaningful before a producing op; consecutive SEEKs let a patch burn a whole coded
            // chunk (expanded by the coder) without producing output, defeating the per-step work bound
            if (chunk.seekPending) {
                return BAD_OPCODE;
            }
            chunk.seekPending = true;
            // Zigzag-decoded signed delta applied to the old cursor
            const I64 delta = static_cast<I64>(operand >> 1) ^ -static_cast<I64>(operand & 1);
            const I64 cursor = static_cast<I64>(chunk.oldCursor) + delta;
            if (cursor < 0 || cursor > static_cast<I64>(this->m_header.oldSize)) {
                return BAD_OPCODE;
            }
            chunk.oldCursor = static_cast<FwSizeType>(cursor);
            return OP_OK;
        }
        case OP_COPY:
        case OP_ADD:
        case OP_LIT:
            if (operand == 0 || operand > static_cast<U64>(chunk.expected - chunk.produced)) {
                return BAD_OPCODE;
            }
            chunk.seekPending = false;
            chunk.op = static_cast<Op>(opByte);
            chunk.opRemaining = static_cast<FwSizeType>(operand);
            chunk.opActive = true;
            return OP_OK;
        default:
            return BAD_OPCODE;
    }
}

DeltaCodec::Status DeltaCodec::executeOp(Chunk& chunk) {
    FwSizeType length = chunk.opRemaining;
    const FwSizeType outSpace = DELTA_OUTPUT_BUFFER_SIZE - chunk.outFill;
    length = (length < outSpace) ? length : outSpace;

    if (chunk.op == OP_ADD || chunk.op == OP_LIT) {
        if (this->m_window.count() == 0) {
            const Status status = this->fillWindow(chunk);
            if (status != OP_OK) {
                return status;
            }
            if (this->m_window.count() == 0) {
                return TRUNCATED;
            }
        }
        length = (length < this->m_window.count()) ? length : this->m_window.count();
    }
    U8* out = this->m_outBuffer + chunk.outFill;
    if (chunk.op == OP_COPY || chunk.op == OP_ADD) {
        if (chunk.oldCursor + length > this->m_header.oldSize) {
            return BAD_OPCODE;
        }
        const Status status = this->readOld(chunk.oldCursor, out, length);
        if (status != OP_OK) {
            return status;
        }
        chunk.oldCursor += length;
    }
    if (chunk.op == OP_ADD) {
        for (FwSizeType i = 0; i < length; i++) {
            out[i] = static_cast<U8>(out[i] + this->m_window.peek(i));
        }
        this->m_window.pop(length);
    } else if (chunk.op == OP_LIT) {
        this->m_window.popInto(out, length);
    }
    chunk.outFill += length;
    chunk.produced += length;
    chunk.opRemaining -= length;
    if (chunk.opRemaining == 0) {
        chunk.opActive = false;
    }
    // The final slice of a chunk is held back until finishChunk() has validated the whole chunk
    if (chunk.outFill == DELTA_OUTPUT_BUFFER_SIZE && chunk.produced < chunk.expected) {
        return this->flushOutput(chunk);
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::readOld(FwSizeType offset, U8* out, FwSizeType& length) {
    FW_ASSERT(out != nullptr);
    FW_ASSERT(length > 0);
    FW_ASSERT(offset + length <= this->m_header.oldSize);
    OldCache& cache = this->m_oldCache;
    const bool cached = (cache.length > 0) && (offset >= cache.offset) && (offset < cache.offset + cache.length);
    if (!cached) {
        const FwSizeType available = this->m_header.oldSize - offset;
        const FwSizeType fill = (available < DELTA_OLD_BUFFER_SIZE) ? available : DELTA_OLD_BUFFER_SIZE;
        this->m_ioCount++;
        cache.length = 0;
        if (this->m_old->read(offset, this->m_oldBuffer, fill) != DeltaMedia::OP_OK) {
            return READ_ERROR;
        }
        cache.offset = offset;
        cache.length = fill;
    }
    // Serve what the cache holds from `offset`; the caller re-enters for any remainder
    const FwSizeType start = offset - cache.offset;
    const FwSizeType inCache = cache.length - start;
    length = (length < inCache) ? length : inCache;
    (void)memcpy(out, this->m_oldBuffer + start, length);
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::flushOutput(Chunk& chunk) {
    if (chunk.outFill == 0) {
        return OP_OK;
    }
    const FwSizeType offset = chunk.newOffset + chunk.produced - chunk.outFill;
    this->m_ioCount++;
    if (this->m_new->write(offset, this->m_outBuffer, chunk.outFill) != DeltaMedia::OP_OK) {
        return WRITE_ERROR;
    }
    chunk.crc = crcUpdate(chunk.crc, this->m_outBuffer, chunk.outFill);
    this->m_progress.runningCrc = crcUpdate(this->m_progress.runningCrc, this->m_outBuffer, chunk.outFill);
    chunk.outFill = 0;
    return OP_OK;
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

U32 DeltaCodec::crcInit() {
    return 0xFFFFFFFFu;
}

U32 DeltaCodec::crcUpdate(U32 crc, const U8* data, FwSizeType length) {
    return Utils::crc32_ieee802_3_update(data, length, crc);
}

U32 DeltaCodec::crcFinal(U32 crc) {
    return ~crc;
}

}  // namespace Update
