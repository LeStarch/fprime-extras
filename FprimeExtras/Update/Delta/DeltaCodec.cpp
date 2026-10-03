// ======================================================================
// \title  DeltaCodec.cpp
// \author starchmd
// \brief  cpp file for the SPatch v2 streaming patch engine
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"

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
      m_window(m_windowBuffer, DELTA_WINDOW_SIZE),
      m_state(IDLE),
      m_lastStatus(OP_OK),
      m_oldSize(0),
      m_oldCrc(0),
      m_newSize(0),
      m_newCrc(0),
      m_chunkCount(0),
      m_chunkBytes(0),
      m_chunkIndex(0),
      m_resumedChunks(0),
      m_verifyChunks(0),
      m_patchSize(0),
      m_patchPos(0),
      m_verifyOffset(0),
      m_verifyCrc(0),
      m_runningCrc(0),
      m_committedCrc(0) {}

// ----------------------------------------------------------------------
// Public interface
// ----------------------------------------------------------------------

void DeltaCodec::reset() {
    this->m_old = nullptr;
    this->m_patch = nullptr;
    this->m_new = nullptr;
    this->m_state = IDLE;
    this->m_lastStatus = OP_OK;
    this->m_chunkIndex = 0;
    this->m_resumedChunks = 0;
    this->m_verifyChunks = 0;
    this->m_patchPos = 0;
    this->m_verifyOffset = 0;
    this->m_verifyCrc = crcInit();
    this->m_runningCrc = crcInit();
    this->m_committedCrc = crcInit();
}

void DeltaCodec::setCoder(DeltaCoder& coder) {
    FW_ASSERT(this->m_state == IDLE, static_cast<FwAssertArgType>(this->m_state));
    this->m_coder = &coder;
}

DeltaCodec::Status DeltaCodec::begin(DeltaMedia& oldImage, DeltaMedia& patch, DeltaMedia& newImage) {
    this->reset();
    this->m_old = &oldImage;
    this->m_patch = &patch;
    this->m_new = &newImage;

    // Header (F Prime big-endian): magic[4] version u8 coder u8 flags u8 size_width u8 | old_size FwSizeType
    //                               old_crc u32 new_size FwSizeType new_crc u32 chunk_bytes FwSizeType header_crc u32
    static_assert(DELTA_PATCH_BUFFER_SIZE >= HEADER_SIZE, "Patch buffer must hold the SPatch header");
    static_assert(DELTA_WINDOW_SIZE >= DeltaCoderLzss::WINDOW, "DELTA_WINDOW_SIZE must cover the default LZSS window");
    if (patch.size(this->m_patchSize) != DeltaMedia::OP_OK) {
        return this->fail(READ_ERROR);
    }
    // The fixed prefix (magic, version, coder, flags, size width) is validated before the width-dependent remainder
    // so that a short patch from the other width is still reported as SIZE_WIDTH_MISMATCH
    const FwSizeType prefixLength = (this->m_patchSize < HEADER_SIZE) ? this->m_patchSize : HEADER_SIZE;
    if (prefixLength < HDR_OLD_SIZE) {
        return this->fail(TRUNCATED);
    }
    if (patch.read(0, this->m_patchBuffer, prefixLength) != DeltaMedia::OP_OK) {
        return this->fail(READ_ERROR);
    }
    const U8* header = this->m_patchBuffer;
    for (FwSizeType i = 0; i < sizeof(SPATCH_MAGIC); i++) {
        if (header[i] != SPATCH_MAGIC[i]) {
            return this->fail(BAD_HEADER);
        }
    }
    if (header[HDR_VERSION] != VERSION || header[HDR_FLAGS] != 0) {
        return this->fail(BAD_HEADER);
    }
    // The header layout depends on the size width, so this must be checked before the CRC can be located
    if (header[HDR_SIZE_WIDTH] != SIZE_FIELD_WIDTH) {
        return this->fail(SIZE_WIDTH_MISMATCH);
    }
    if (this->m_patchSize < HEADER_SIZE) {
        return this->fail(TRUNCATED);
    }
    Fw::ExternalSerializeBuffer fields(this->m_patchBuffer, HEADER_SIZE);
    U32 headerCrc = 0;
    if (fields.setBuffLen(HEADER_SIZE) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeSkip(HDR_OLD_SIZE) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_oldSize) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_oldCrc) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_newSize) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_newCrc) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(this->m_chunkBytes) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(headerCrc) != Fw::FW_SERIALIZE_OK) {
        return this->fail(BAD_HEADER);
    }
    if (crcFinal(crcUpdate(crcInit(), header, HDR_CRC)) != headerCrc) {
        return this->fail(BAD_HEADER);
    }
    // Header is intact: a coder id mismatch is a ground/flight configuration disagreement, not corruption
    if (header[HDR_CODER] != this->m_coder->id()) {
        return this->fail(CODER_MISMATCH);
    }
    if (this->m_chunkBytes == 0 || this->m_chunkBytes > DELTA_MAX_CHUNK_BYTES ||
        this->m_oldSize > DELTA_MAX_IMAGE_SIZE || this->m_newSize > DELTA_MAX_IMAGE_SIZE) {
        return this->fail(BAD_HEADER);
    }
    // Sizes are capped at DELTA_MAX_IMAGE_SIZE above, so this cannot wrap
    this->m_chunkCount = (this->m_newSize + this->m_chunkBytes - 1) / this->m_chunkBytes;
    this->m_patchPos = HEADER_SIZE;

    FwSizeType oldSize = 0;
    if (oldImage.size(oldSize) != DeltaMedia::OP_OK) {
        return this->fail(READ_ERROR);
    }
    if (oldSize != this->m_oldSize) {
        return this->fail(OLD_IMAGE_MISMATCH);
    }

    // Resume: whole chunks already present in the output are verified before patching continues
    FwSizeType newSize = 0;
    if (newImage.size(newSize) != DeltaMedia::OP_OK) {
        return this->fail(READ_ERROR);
    }
    if (newSize > this->m_newSize) {
        return this->fail(OUTPUT_STALE);
    }
    if (newSize == this->m_newSize) {
        this->m_verifyChunks = this->m_chunkCount;
    } else {
        this->m_verifyChunks = newSize / this->m_chunkBytes;
    }
    this->m_state = (this->m_verifyChunks > 0) ? VERIFY_NEW : VERIFY_OLD;
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::step() {
    switch (this->m_state) {
        case VERIFY_NEW:
            return this->verifyNewStep();
        case VERIFY_OLD:
            return this->verifyOldStep();
        case PATCHING:
            if (this->m_chunkIndex >= this->m_chunkCount) {
                return this->finish();
            }
            return this->patchChunk();
        case VERIFY_FINAL:
            return this->verifyFinalStep();
        case IDLE:
        case COMPLETE:
        case FAILED:
            return this->m_lastStatus;
        default:
            FW_ASSERT(false, static_cast<FwAssertArgType>(this->m_state));
            return this->m_lastStatus;
    }
}

FwSizeType DeltaCodec::bytesWritten() const {
    const FwSizeType written = this->m_chunkIndex * this->m_chunkBytes;
    return (written > this->m_newSize) ? this->m_newSize : written;
}

// ----------------------------------------------------------------------
// State steps
// ----------------------------------------------------------------------

DeltaCodec::Status DeltaCodec::fail(Status status) {
    FW_ASSERT(status != OP_OK);
    this->m_state = FAILED;
    this->m_lastStatus = status;
    return status;
}

DeltaCodec::Status DeltaCodec::readChunkHeader(FwSizeType& codedLength, U32& crc) {
    FW_ASSERT(this->m_patchPos <= this->m_patchSize, static_cast<FwAssertArgType>(this->m_patchPos));
    if (CHUNK_HEADER_SIZE > this->m_patchSize - this->m_patchPos) {
        return TRUNCATED;
    }
    if (this->m_patch->read(this->m_patchPos, this->m_patchBuffer, CHUNK_HEADER_SIZE) != DeltaMedia::OP_OK) {
        return READ_ERROR;
    }
    Fw::ExternalSerializeBuffer fields(this->m_patchBuffer, CHUNK_HEADER_SIZE);
    if (fields.setBuffLen(CHUNK_HEADER_SIZE) != Fw::FW_SERIALIZE_OK ||
        fields.deserializeTo(codedLength) != Fw::FW_SERIALIZE_OK || fields.deserializeTo(crc) != Fw::FW_SERIALIZE_OK) {
        return BAD_OPCODE;
    }
    if (codedLength > DELTA_MAX_CODED_CHUNK_BYTES) {
        return BAD_OPCODE;
    }
    // Subtraction form: m_patchPos + CHUNK_HEADER_SIZE <= m_patchSize was established above, so this cannot wrap
    if (codedLength > this->m_patchSize - (this->m_patchPos + CHUNK_HEADER_SIZE)) {
        return TRUNCATED;
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::verifyNewStep() {
    FwSizeType budget = DELTA_VERIFY_BYTES_PER_STEP;
    while (budget > 0 && this->m_chunkIndex < this->m_verifyChunks) {
        const FwSizeType chunkStart = this->m_chunkIndex * this->m_chunkBytes;
        const FwSizeType chunkLength =
            ((this->m_newSize - chunkStart) < this->m_chunkBytes) ? (this->m_newSize - chunkStart) : this->m_chunkBytes;
        FwSizeType remaining = chunkLength - this->m_verifyOffset;
        FwSizeType length = (remaining < DELTA_OUTPUT_BUFFER_SIZE) ? remaining : DELTA_OUTPUT_BUFFER_SIZE;
        length = (length < budget) ? length : budget;
        if (length > 0) {
            if (this->m_new->read(chunkStart + this->m_verifyOffset, this->m_outBuffer, length) != DeltaMedia::OP_OK) {
                return this->fail(READ_ERROR);
            }
            this->m_verifyCrc = crcUpdate(this->m_verifyCrc, this->m_outBuffer, length);
            this->m_runningCrc = crcUpdate(this->m_runningCrc, this->m_outBuffer, length);
            this->m_verifyOffset += length;
            budget -= length;
        }
        if (this->m_verifyOffset == chunkLength) {
            FwSizeType codedLength = 0;
            U32 expectedCrc = 0;
            const Status status = this->readChunkHeader(codedLength, expectedCrc);
            if (status != OP_OK) {
                return this->fail(status);
            }
            if (crcFinal(this->m_verifyCrc) == expectedCrc) {
                this->m_committedCrc = this->m_runningCrc;
                this->m_chunkIndex++;
                this->m_resumedChunks++;
                this->m_patchPos += CHUNK_HEADER_SIZE + codedLength;
            } else {
                // Chunk is corrupt: resume from here, discarding its contribution
                this->m_runningCrc = this->m_committedCrc;
                this->m_verifyChunks = this->m_chunkIndex;
            }
            this->m_verifyOffset = 0;
            this->m_verifyCrc = crcInit();
        }
    }
    if (this->m_chunkIndex >= this->m_verifyChunks) {
        this->m_verifyOffset = 0;
        this->m_verifyCrc = crcInit();
        this->m_state = VERIFY_OLD;
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::verifyOldStep() {
    FwSizeType budget = DELTA_VERIFY_BYTES_PER_STEP;
    while (budget > 0 && this->m_verifyOffset < this->m_oldSize) {
        const FwSizeType remaining = this->m_oldSize - this->m_verifyOffset;
        FwSizeType length = (remaining < DELTA_OUTPUT_BUFFER_SIZE) ? remaining : DELTA_OUTPUT_BUFFER_SIZE;
        length = (length < budget) ? length : budget;
        if (this->m_old->read(this->m_verifyOffset, this->m_outBuffer, length) != DeltaMedia::OP_OK) {
            return this->fail(READ_ERROR);
        }
        this->m_verifyCrc = crcUpdate(this->m_verifyCrc, this->m_outBuffer, length);
        this->m_verifyOffset += length;
        budget -= length;
    }
    if (this->m_verifyOffset >= this->m_oldSize) {
        this->m_verifyCrc = crcFinal(this->m_verifyCrc);
        if (this->m_verifyCrc != this->m_oldCrc) {
            return this->fail(OLD_IMAGE_MISMATCH);
        }
        this->m_state = PATCHING;
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::finish() {
    if (this->m_patchPos != this->m_patchSize) {
        return this->fail(PATCH_SIZE_MISMATCH);
    }
    if (crcFinal(this->m_committedCrc) != this->m_newCrc) {
        return this->fail(NEW_IMAGE_MISMATCH);
    }
    // The CRC above covers what was handed to the media; COMPLETE requires the stored bytes to agree
    if (this->m_new->flush() != DeltaMedia::OP_OK) {
        return this->fail(WRITE_ERROR);
    }
    this->m_verifyOffset = 0;
    this->m_verifyCrc = crcInit();
    this->m_state = VERIFY_FINAL;
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::verifyFinalStep() {
    FwSizeType budget = DELTA_VERIFY_BYTES_PER_STEP;
    while (budget > 0 && this->m_verifyOffset < this->m_newSize) {
        const FwSizeType remaining = this->m_newSize - this->m_verifyOffset;
        FwSizeType length = (remaining < DELTA_OUTPUT_BUFFER_SIZE) ? remaining : DELTA_OUTPUT_BUFFER_SIZE;
        length = (length < budget) ? length : budget;
        if (this->m_new->read(this->m_verifyOffset, this->m_outBuffer, length) != DeltaMedia::OP_OK) {
            return this->fail(READ_ERROR);
        }
        this->m_verifyCrc = crcUpdate(this->m_verifyCrc, this->m_outBuffer, length);
        this->m_verifyOffset += length;
        budget -= length;
    }
    if (this->m_verifyOffset >= this->m_newSize) {
        FwSizeType storedSize = 0;
        if (this->m_new->size(storedSize) != DeltaMedia::OP_OK) {
            return this->fail(READ_ERROR);
        }
        if (storedSize != this->m_newSize || crcFinal(this->m_verifyCrc) != this->m_newCrc) {
            return this->fail(NEW_IMAGE_MISMATCH);
        }
        this->m_state = COMPLETE;
    }
    return OP_OK;
}

// ----------------------------------------------------------------------
// Chunk processing
// ----------------------------------------------------------------------

DeltaCodec::Status DeltaCodec::patchChunk() {
    Chunk chunk;
    chunk.codedLength = 0;
    chunk.expectedCrc = 0;
    Status status = this->readChunkHeader(chunk.codedLength, chunk.expectedCrc);
    if (status != OP_OK) {
        return this->fail(status);
    }
    chunk.codedRemaining = chunk.codedLength;
    chunk.patchFill = 0;
    chunk.patchPos = 0;
    chunk.newOffset = this->m_chunkIndex * this->m_chunkBytes;
    const FwSizeType left = this->m_newSize - chunk.newOffset;
    chunk.expected = (left < this->m_chunkBytes) ? left : this->m_chunkBytes;
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

    while (chunk.produced < chunk.expected) {
        status = chunk.opActive ? this->executeOp(chunk) : this->parseOp(chunk);
        if (status != OP_OK) {
            this->m_runningCrc = this->m_committedCrc;
            return this->fail(status);
        }
    }
    status = this->flushOutput(chunk);
    if (status != OP_OK) {
        this->m_runningCrc = this->m_committedCrc;
        return this->fail(status);
    }
    // Everything declared for this chunk must have been consumed exactly, and the coder must end at a token boundary
    if (chunk.codedRemaining != 0 || chunk.patchPos != chunk.patchFill || this->m_window.count() != 0 ||
        this->m_coder->pending()) {
        this->m_runningCrc = this->m_committedCrc;
        return this->fail(BAD_OPCODE);
    }
    if (crcFinal(chunk.crc) != chunk.expectedCrc) {
        this->m_runningCrc = this->m_committedCrc;
        return this->fail(CHUNK_CRC);
    }
    this->m_committedCrc = this->m_runningCrc;
    this->m_patchPos += CHUNK_HEADER_SIZE + chunk.codedLength;
    this->m_chunkIndex++;
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::fillWindow(Chunk& chunk) {
    // Each pass pushes at least one byte or returns, so the free window space bounds the passes
    const FwSizeType maxPasses = this->m_window.space();
    for (FwSizeType pass = 0; pass < maxPasses && this->m_window.space() > 0; pass++) {
        if (chunk.patchPos >= chunk.patchFill && chunk.codedRemaining > 0) {
            const FwSizeType length =
                (chunk.codedRemaining < DELTA_PATCH_BUFFER_SIZE) ? chunk.codedRemaining : DELTA_PATCH_BUFFER_SIZE;
            const FwSizeType offset = this->m_patchPos + CHUNK_HEADER_SIZE + (chunk.codedLength - chunk.codedRemaining);
            if (this->m_patch->read(offset, this->m_patchBuffer, length) != DeltaMedia::OP_OK) {
                return READ_ERROR;
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
    static_assert(DELTA_MAX_OPS_PER_CHUNK >= 2 * DELTA_MAX_CHUNK_BYTES, "Op cap must admit any valid chunk");
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
            if (cursor < 0 || cursor > static_cast<I64>(this->m_oldSize)) {
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
        if (chunk.oldCursor + length > this->m_oldSize) {
            return BAD_OPCODE;
        }
        if (this->m_old->read(chunk.oldCursor, out, length) != DeltaMedia::OP_OK) {
            return READ_ERROR;
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
    if (chunk.outFill == DELTA_OUTPUT_BUFFER_SIZE) {
        return this->flushOutput(chunk);
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::flushOutput(Chunk& chunk) {
    if (chunk.outFill == 0) {
        return OP_OK;
    }
    const FwSizeType offset = chunk.newOffset + chunk.produced - chunk.outFill;
    if (this->m_new->write(offset, this->m_outBuffer, chunk.outFill) != DeltaMedia::OP_OK) {
        return WRITE_ERROR;
    }
    chunk.crc = crcUpdate(chunk.crc, this->m_outBuffer, chunk.outFill);
    this->m_runningCrc = crcUpdate(this->m_runningCrc, this->m_outBuffer, chunk.outFill);
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
