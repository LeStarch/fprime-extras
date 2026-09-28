// ======================================================================
// \title  DeltaCodec.cpp
// \author starchmd
// \brief  cpp file for the SPatch v1 streaming patch engine
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"

#include "Fw/Types/Assert.hpp"
#include "Utils/Hash/Crc32/Crc32.hpp"

namespace Update {

static const U8 SPATCH_MAGIC[4] = {'S', 'P', 'A', 'T'};
static constexpr FwSizeType VARINT_MAX_BYTES = 5;  // 35 bits: covers U32 lengths and zigzag 33-bit seeks

DeltaCodec::DeltaCodec(DeltaCoder& coder)
    : m_coder(&coder),
      m_old(nullptr),
      m_patch(nullptr),
      m_new(nullptr),
      m_patchBuffer(),
      m_ringBuffer(),
      m_outBuffer(),
      m_ring(m_ringBuffer, DELTA_RING_SIZE),
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

    // Header (little-endian): magic[4] version u8 coder u8 flags u8 reserved u8 | old_size u32 old_crc u32
    //                         new_size u32 new_crc u32 chunk_bytes u32 header_crc u32
    static_assert(DELTA_PATCH_BUFFER_SIZE >= HEADER_SIZE, "Patch buffer must hold the SPatch header");
    if (patch.size(this->m_patchSize) != DeltaMedia::OP_OK) {
        return this->fail(READ_ERROR);
    }
    if (this->m_patchSize < HEADER_SIZE) {
        return this->fail(TRUNCATED);
    }
    if (patch.read(0, this->m_patchBuffer, HEADER_SIZE) != DeltaMedia::OP_OK) {
        return this->fail(READ_ERROR);
    }
    const U8* header = this->m_patchBuffer;
    for (FwSizeType i = 0; i < sizeof(SPATCH_MAGIC); i++) {
        if (header[i] != SPATCH_MAGIC[i]) {
            return this->fail(BAD_HEADER);
        }
    }
    if (header[4] != VERSION || header[5] != this->m_coder->id() || header[6] != 0 || header[7] != 0) {
        return this->fail(BAD_HEADER);
    }
    if (crcFinal(crcUpdate(crcInit(), header, HEADER_SIZE - sizeof(U32))) != readU32(header + 28)) {
        return this->fail(BAD_HEADER);
    }
    this->m_oldSize = readU32(header + 8);
    this->m_oldCrc = readU32(header + 12);
    this->m_newSize = readU32(header + 16);
    this->m_newCrc = readU32(header + 20);
    this->m_chunkBytes = readU32(header + 24);
    if (this->m_chunkBytes == 0) {
        return this->fail(BAD_HEADER);
    }
    this->m_chunkCount =
        static_cast<U32>((static_cast<U64>(this->m_newSize) + this->m_chunkBytes - 1) / this->m_chunkBytes);
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
        this->m_verifyChunks = static_cast<U32>(newSize / this->m_chunkBytes);
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
    const U64 written = static_cast<U64>(this->m_chunkIndex) * this->m_chunkBytes;
    return static_cast<FwSizeType>((written > this->m_newSize) ? this->m_newSize : written);
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
    if (this->m_patchPos + CHUNK_HEADER_SIZE > this->m_patchSize) {
        return TRUNCATED;
    }
    if (this->m_patch->read(this->m_patchPos, this->m_patchBuffer, CHUNK_HEADER_SIZE) != DeltaMedia::OP_OK) {
        return READ_ERROR;
    }
    codedLength = readU32(this->m_patchBuffer);
    crc = readU32(this->m_patchBuffer + 4);
    if (this->m_patchPos + CHUNK_HEADER_SIZE + codedLength > this->m_patchSize) {
        return TRUNCATED;
    }
    return OP_OK;
}

DeltaCodec::Status DeltaCodec::verifyNewStep() {
    FwSizeType budget = DELTA_VERIFY_BYTES_PER_STEP;
    while (budget > 0 && this->m_chunkIndex < this->m_verifyChunks) {
        const FwSizeType chunkStart = static_cast<FwSizeType>(this->m_chunkIndex) * this->m_chunkBytes;
        const FwSizeType chunkLength = static_cast<FwSizeType>(((this->m_newSize - chunkStart) < this->m_chunkBytes)
                                                                   ? (this->m_newSize - chunkStart)
                                                                   : this->m_chunkBytes);
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
        const FwSizeType remaining = static_cast<FwSizeType>(this->m_oldSize) - this->m_verifyOffset;
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
    this->m_state = COMPLETE;
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
    chunk.newOffset = static_cast<FwSizeType>(this->m_chunkIndex) * this->m_chunkBytes;
    const FwSizeType left = static_cast<FwSizeType>(this->m_newSize) - chunk.newOffset;
    chunk.expected = (left < this->m_chunkBytes) ? left : this->m_chunkBytes;
    chunk.produced = 0;
    chunk.oldCursor = chunk.newOffset;
    chunk.outFill = 0;
    chunk.crc = crcInit();
    chunk.op = OP_COPY;
    chunk.opActive = false;
    chunk.opRemaining = 0;

    this->m_coder->reset();
    this->m_ring.reset();

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
    // Everything declared for this chunk must have been consumed exactly
    if (chunk.codedRemaining != 0 || chunk.patchPos != chunk.patchFill || this->m_ring.count() != 0) {
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

DeltaCodec::Status DeltaCodec::fillRing(Chunk& chunk) {
    while (this->m_ring.space() > 0) {
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
        const FwSizeType before = this->m_ring.count();
        const FwSizeType available = chunk.patchFill - chunk.patchPos;
        const DeltaCoder::Status status =
            this->m_coder->decode(this->m_patchBuffer + chunk.patchPos, available, consumed, this->m_ring);
        if (status != DeltaCoder::OP_OK) {
            return BAD_OPCODE;
        }
        FW_ASSERT(consumed <= available, static_cast<FwAssertArgType>(consumed));
        chunk.patchPos += consumed;
        if (consumed == 0 && this->m_ring.count() == before) {
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
        if (start + i >= this->m_ring.count()) {
            return VARINT_NEED_MORE;
        }
        const U8 byte = this->m_ring.peek(start + i);
        value |= static_cast<U64>(byte & 0x7F) << (7 * i);
        length = i + 1;
        if ((byte & 0x80) == 0) {
            return VARINT_OK;
        }
    }
    return VARINT_INVALID;
}

DeltaCodec::Status DeltaCodec::parseOp(Chunk& chunk) {
    // Ensure the op byte and complete operand are decoded; the ring holds at least 6 bytes so this always fits
    static_assert(DELTA_RING_SIZE > VARINT_MAX_BYTES, "Ring must hold an op byte and its operand");
    U64 operand = 0;
    FwSizeType operandLength = 0;
    while (true) {
        VarintResult result = VARINT_NEED_MORE;
        if (this->m_ring.count() >= 1) {
            result = this->readVarint(1, operand, operandLength);
        }
        if (result == VARINT_INVALID) {
            return BAD_OPCODE;
        } else if (result == VARINT_OK) {
            break;
        }
        const FwSizeType before = this->m_ring.count();
        const Status status = this->fillRing(chunk);
        if (status != OP_OK) {
            return status;
        }
        if (this->m_ring.count() == before) {
            return TRUNCATED;
        }
    }
    const U8 opByte = this->m_ring.peek(0);
    this->m_ring.pop(1 + operandLength);

    switch (opByte) {
        case OP_SEEK: {
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
        if (this->m_ring.count() == 0) {
            const Status status = this->fillRing(chunk);
            if (status != OP_OK) {
                return status;
            }
            if (this->m_ring.count() == 0) {
                return TRUNCATED;
            }
        }
        length = (length < this->m_ring.count()) ? length : this->m_ring.count();
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
            out[i] = static_cast<U8>(out[i] + this->m_ring.peek(i));
        }
        this->m_ring.pop(length);
    } else if (chunk.op == OP_LIT) {
        this->m_ring.popInto(out, length);
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

U32 DeltaCodec::readU32(const U8* data) {
    return static_cast<U32>(data[0]) | (static_cast<U32>(data[1]) << 8) | (static_cast<U32>(data[2]) << 16) |
           (static_cast<U32>(data[3]) << 24);
}

}  // namespace Update
