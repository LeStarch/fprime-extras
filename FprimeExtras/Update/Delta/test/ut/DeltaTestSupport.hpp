// ======================================================================
// \title  DeltaTestSupport.hpp
// \author starchmd
// \brief  Shared test doubles for the Delta unit tests: in-memory media with
// fault injection, a stage driver mirroring Update.DeltaPatchMachine, and
// byte helpers
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_Test_DeltaTestSupport_HPP
#define Update_Delta_Test_DeltaTestSupport_HPP

#include <vector>

#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoder.hpp"
#include "Utils/Hash/Crc32/Crc32.hpp"

namespace Update {

//! In-memory media with fault injection
class MemoryMedia final : public DeltaMedia {
  public:
    explicit MemoryMedia(const U8* data = nullptr, FwSizeType size = 0)
        : data(data, data + size),
          failReads(false),
          failWrites(false),
          failFlush(false),
          corruptStored(false),
          failRefresh(false),
          reads(0),
          writes(0),
          flushes(0),
          refreshes(0) {}

    Status size(FwSizeType& size) override {
        if (this->failSize) {
            return IO_ERROR;
        }
        size = this->data.size();
        return OP_OK;
    }
    Status read(FwSizeType offset, U8* buffer, FwSizeType length) override {
        this->reads++;
        if (this->failReads) {
            return IO_ERROR;
        }
        if (offset + length > this->data.size()) {
            return OUT_OF_RANGE;
        }
        for (FwSizeType i = 0; i < length; i++) {
            buffer[i] = this->data[offset + i];
        }
        return OP_OK;
    }
    Status write(FwSizeType offset, const U8* buffer, FwSizeType length) override {
        this->writes++;
        if (this->failWrites) {
            return IO_ERROR;
        }
        if (offset + length > this->data.size()) {
            this->data.resize(offset + length);
        }
        for (FwSizeType i = 0; i < length; i++) {
            this->data[offset + i] = buffer[i];
        }
        if (this->corruptStored) {
            // Storage accepts the write but keeps a flipped bit: only read-back can catch this
            this->data[offset] ^= 0x01;
        }
        return OP_OK;
    }
    Status flush() override {
        this->flushes++;
        return this->failFlush ? IO_ERROR : OP_OK;
    }
    Status refresh() override {
        this->refreshes++;
        return this->failRefresh ? IO_ERROR : OP_OK;
    }

    std::vector<U8> data;
    bool failSize = false;  //!< size() fails
    bool failReads;
    bool failWrites;
    bool failFlush;
    bool corruptStored;
    bool failRefresh;
    U32 reads;
    U32 writes;
    U32 flushes;
    U32 refreshes;
};

//! Drives the stage functions of a DeltaCodec the way Update.DeltaPatchMachine
//! does, exposing the lifecycle as a state so the engine tests can observe
//! where a patch stops. Mirrors the machine: begin() selects the resume stage,
//! each step() delegates one unit of work, STAGE_DONE advances, any error
//! fails and latches.
class TestCodec {
  public:
    enum State { IDLE, VERIFY_RESUME, VERIFY_OLD, PATCHING, VERIFY_FINAL, COMPLETE, FAILED };

    explicit TestCodec(DeltaCoder& coder) : codec(coder), m_state(IDLE), m_last(DeltaCodec::OP_OK) {}

    DeltaCodec::Status begin(DeltaMedia& old, DeltaMedia& patch, DeltaMedia& out) {
        this->m_last = this->codec.begin(old, patch, out);
        if (this->m_last != DeltaCodec::OP_OK) {
            this->m_state = FAILED;
            return this->m_last;
        }
        this->m_state = this->codec.hasOutputToVerify() ? VERIFY_RESUME : VERIFY_OLD;
        return DeltaCodec::OP_OK;
    }

    DeltaCodec::Status step() {
        DeltaCodec::StepResult result = DeltaCodec::IN_PROGRESS;
        DeltaCodec::Status status = DeltaCodec::OP_OK;
        State next = this->m_state;
        switch (this->m_state) {
            case VERIFY_RESUME:
                status = this->codec.verifyResume(result);
                next = VERIFY_OLD;
                break;
            case VERIFY_OLD:
                status = this->codec.verifyOld(result);
                next = PATCHING;
                break;
            case PATCHING:
                status = this->codec.patchChunk(result);
                next = VERIFY_FINAL;
                break;
            case VERIFY_FINAL:
                status = this->codec.verifyFinal(result);
                next = COMPLETE;
                break;
            default:
                return this->m_last;  // idle, complete or failed: nothing to do
        }
        if (status != DeltaCodec::OP_OK) {
            this->m_state = FAILED;
            this->m_last = status;
            return status;
        }
        if (result == DeltaCodec::STAGE_DONE) {
            this->m_state = next;
        }
        return DeltaCodec::OP_OK;
    }

    void reset() {
        this->codec.reset();
        this->m_state = IDLE;
        this->m_last = DeltaCodec::OP_OK;
    }
    void setCoder(DeltaCoder& coder) { this->codec.setCoder(coder); }
    State state() const { return this->m_state; }
    DeltaCodec::Status lastStatus() const { return this->m_last; }
    FwSizeType chunkIndex() const { return this->codec.chunkIndex(); }
    FwSizeType chunkCount() const { return this->codec.chunkCount(); }
    FwSizeType resumedChunks() const { return this->codec.resumedChunks(); }
    FwSizeType bytesWritten() const { return this->codec.bytesWritten(); }
    FwSizeType oldSize() const { return this->codec.oldSize(); }
    U32 oldCrc() const { return this->codec.oldCrc(); }
    FwSizeType newSize() const { return this->codec.newSize(); }
    U32 newCrc() const { return this->codec.newCrc(); }

    DeltaCodec codec;

  private:
    State m_state;
    DeltaCodec::Status m_last;
};

inline std::vector<U8> toVector(const U8* data, FwSizeType size) {
    return std::vector<U8>(data, data + size);
}

//! Run an engine to completion or failure; returns the final status. Caps steps
//! to guarantee termination.
inline DeltaCodec::Status run(TestCodec& codec, U32& steps) {
    DeltaCodec::Status status = DeltaCodec::OP_OK;
    for (steps = 0; steps < 10000 && codec.state() != TestCodec::COMPLETE && codec.state() != TestCodec::FAILED;
         steps++) {
        status = codec.step();
    }
    return status;
}

//! Feed coded data through a coder in `pieceSize` slices, draining the ring
//! into `out`; mimics DeltaCodec::fillWindow
inline DeltaCoder::Status decodeAll(DeltaCoder& coder,
                                    const U8* coded,
                                    FwSizeType codedSize,
                                    FwSizeType pieceSize,
                                    FwSizeType drainSize,
                                    std::vector<U8>& out) {
    U8 storage[DELTA_WINDOW_SIZE];
    DeltaWindow ring(storage, sizeof storage);
    ring.reset();
    coder.reset();
    FwSizeType pos = 0;
    while (true) {
        FwSizeType consumed = 0;
        const FwSizeType before = ring.count();
        const FwSizeType avail = ((codedSize - pos) < pieceSize) ? (codedSize - pos) : pieceSize;
        const DeltaCoder::Status status = coder.decode(coded + pos, avail, consumed, ring);
        if (status != DeltaCoder::OP_OK) {
            return status;
        }
        pos += consumed;
        const bool produced = ring.count() != before;
        const FwSizeType n = (ring.count() < drainSize) ? ring.count() : drainSize;
        for (FwSizeType i = 0; i < n; i++) {
            out.push_back(ring.peek(i));
        }
        ring.pop(n);
        if (consumed == 0 && !produced && n == 0) {
            return (pos < codedSize) ? DeltaCoder::MALFORMED : DeltaCoder::OP_OK;  // stuck vs. drained
        }
    }
}

// Write an F Prime serialized (big-endian) value of `width` bytes
inline void putBe(U8* dest, U64 value, FwSizeType width) {
    for (FwSizeType i = 0; i < width; i++) {
        dest[i] = static_cast<U8>(value >> (8 * (width - 1 - i)));
    }
}

// CRC32 (IEEE 802.3, init 0xFFFFFFFF, final inversion) as written into SPatch
inline U32 crc32(const U8* data, FwSizeType length) {
    if (length == 0) {
        return 0;  // CRC32 of no bytes; Utils asserts on a null pointer
    }
    return ~Utils::crc32_ieee802_3_update(data, length, 0xFFFFFFFFu);
}

}  // namespace Update

#endif
