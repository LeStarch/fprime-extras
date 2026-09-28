// ======================================================================
// \title  DeltaCodec.hpp
// \author starchmd
// \brief  hpp file for the SPatch v1 streaming patch engine
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaCodec_HPP
#define Update_Delta_DeltaCodec_HPP

#include "ExtrasConfig/DeltaCodecConfig.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoder.hpp"
#include "FprimeExtras/Update/Delta/DeltaMedia.hpp"
#include "FprimeExtras/Update/Delta/DeltaRing.hpp"
#include "Fw/FPrimeBasicTypes.hpp"

namespace Update {

//! \brief Allocation-free, bounded-step SPatch v1 engine
//!
//! Applies an SPatch (format: FprimeExtras/Update/DeltaPatcher/docs/sdd.md) held in `patch` media to the `oldImage`
//! media producing the `newImage` media. Work is performed in step() calls, each bounded to one chunk (or
//! DELTA_VERIFY_BYTES_PER_STEP bytes of CRC verification), so the engine may be driven from a rate group. All storage
//! is fixed: three buffers sized by DeltaCodecConfig.hpp plus scalar state.
//!
//! Resume: chunks already present in the new image media are CRC-verified and skipped, so an interrupted patch
//! restarts from the first missing or corrupt chunk with no persisted engine state.
//!
//! Completion: after the last chunk the new image media is flushed and read back in full; COMPLETE is reached only
//! when the stored size and CRC match the header.
class DeltaCodec final {
  public:
    //! Outcome of begin()/step(); mirrors Update.DeltaPatchStatus
    enum Status {
        OP_OK,
        BAD_HEADER,          //!< Magic, version, flags, reserved byte, CRC, or geometry of the SPatch header is invalid
        OLD_IMAGE_MISMATCH,  //!< Old image size or CRC differs from the header
        TRUNCATED,           //!< Patch ended before the declared content
        CHUNK_CRC,           //!< Chunk output CRC failed
        BAD_OPCODE,          //!< Unknown op, operand out of bounds, or coder reported malformed payload
        READ_ERROR,          //!< Media read failed
        WRITE_ERROR,         //!< Media write failed
        NEW_IMAGE_MISMATCH,  //!< Final new image CRC, or stored size/CRC on read-back, differs from the header
        PATCH_SIZE_MISMATCH,  //!< Patch has bytes beyond the last chunk
        OUTPUT_STALE,         //!< Existing new image is larger than the target; caller must recreate it
        CODER_MISMATCH        //!< Header is valid but names a coder other than the installed one
    };

    //! Engine state
    enum State {
        IDLE,
        VERIFY_NEW,  //!< Verifying chunks already present in the new image (resume)
        VERIFY_OLD,  //!< Verifying the old image CRC
        PATCHING,
        VERIFY_FINAL,  //!< Reading the flushed new image back and checking its CRC before reporting COMPLETE
        COMPLETE,
        FAILED
    };

    //! SPatch v1 constants
    static constexpr FwSizeType HEADER_SIZE = 32;
    static constexpr FwSizeType CHUNK_HEADER_SIZE = 8;
    static constexpr U8 VERSION = 1;
    // Header field offsets
    static constexpr FwSizeType HDR_VERSION = 4;
    static constexpr FwSizeType HDR_CODER = 5;
    static constexpr FwSizeType HDR_FLAGS = 6;
    static constexpr FwSizeType HDR_RESERVED = 7;
    static constexpr FwSizeType HDR_OLD_SIZE = 8;
    static constexpr FwSizeType HDR_OLD_CRC = 12;
    static constexpr FwSizeType HDR_NEW_SIZE = 16;
    static constexpr FwSizeType HDR_NEW_CRC = 20;
    static constexpr FwSizeType HDR_CHUNK_BYTES = 24;
    static constexpr FwSizeType HDR_CRC = 28;  //!< CRC32 of header[0:HDR_CRC]
    static_assert(HDR_CRC + sizeof(U32) == HEADER_SIZE, "SPatch header layout");
    // Chunk header field offsets
    static constexpr FwSizeType CHUNK_HDR_CODED_LENGTH = 0;
    static constexpr FwSizeType CHUNK_HDR_CRC = 4;  //!< CRC32 of the chunk's decoded output
    static_assert(CHUNK_HDR_CRC + sizeof(U32) == CHUNK_HEADER_SIZE, "SPatch chunk header layout");

    //! Construct an engine using the supplied coder
    explicit DeltaCodec(DeltaCoder& coder);
    DeltaCodec(const DeltaCodec&) = delete;
    DeltaCodec& operator=(const DeltaCodec&) = delete;

    //! Replace the coder (project plugin seam); only permitted while IDLE
    void setCoder(DeltaCoder& coder);

    //! \brief Read and validate the header, size the resume, and enter the first verify state
    //!
    //! Media must remain open until the engine reaches COMPLETE or FAILED or reset() is called.
    Status begin(DeltaMedia& oldImage, DeltaMedia& patch, DeltaMedia& newImage);

    //! \brief Perform one bounded unit of work
    //!
    //! Returns OP_OK while progressing (including on reaching COMPLETE); any other value sets state FAILED.
    Status step();

    //! Return to IDLE, forgetting media
    void reset();

    State state() const { return this->m_state; }
    Status lastStatus() const { return this->m_lastStatus; }
    U32 chunkIndex() const { return this->m_chunkIndex; }
    U32 chunkCount() const { return this->m_chunkCount; }
    U32 resumedChunks() const { return this->m_resumedChunks; }
    FwSizeType bytesWritten() const;
    U32 oldSize() const { return this->m_oldSize; }
    U32 oldCrc() const { return this->m_oldCrc; }
    U32 actualOldCrc() const { return this->m_verifyCrc; }
    U32 newSize() const { return this->m_newSize; }
    U32 newCrc() const { return this->m_newCrc; }

  private:
    enum Op : U8 { OP_COPY = 0, OP_ADD = 1, OP_LIT = 2, OP_SEEK = 3 };

    //! Per-chunk working set; lives on the stack for the duration of one step()
    struct Chunk {
        FwSizeType codedLength;     //!< Coded payload bytes declared by the chunk header
        FwSizeType codedRemaining;  //!< Coded payload bytes not yet read from media
        FwSizeType patchFill;       //!< Valid bytes in the patch buffer
        FwSizeType patchPos;        //!< Next unconsumed byte in the patch buffer
        FwSizeType newOffset;       //!< New image offset of this chunk
        FwSizeType expected;        //!< Output bytes this chunk must produce
        FwSizeType produced;        //!< Output bytes produced so far
        FwSizeType oldCursor;       //!< Old image read cursor
        FwSizeType outFill;         //!< Valid bytes in the output buffer
        U32 expectedCrc;            //!< Output CRC declared by the chunk header
        U32 crc;                    //!< Running output CRC
        Op op;                      //!< Operation in progress
        bool opActive;              //!< True while op has bytes remaining
        FwSizeType opRemaining;     //!< Output bytes remaining for op
        bool seekPending;           //!< A SEEK was parsed and must be followed by a producing op
        FwSizeType opCount;         //!< Operations parsed in this chunk, capped at DELTA_MAX_OPS_PER_CHUNK
    };

    Status fail(Status status);
    Status readChunkHeader(FwSizeType& codedLength, U32& crc);
    Status verifyNewStep();
    Status verifyOldStep();
    Status patchChunk();
    Status finish();
    Status verifyFinalStep();
    Status fillRing(Chunk& chunk);
    Status parseOp(Chunk& chunk);
    Status executeOp(Chunk& chunk);
    Status flushOutput(Chunk& chunk);
    enum VarintResult { VARINT_OK, VARINT_NEED_MORE, VARINT_INVALID };
    VarintResult readVarint(FwSizeType start, U64& value, FwSizeType& length) const;

    static U32 crcInit();
    static U32 crcUpdate(U32 crc, const U8* data, FwSizeType length);
    static U32 crcFinal(U32 crc);
    static U32 readU32(const U8* data);

    DeltaCoder* m_coder;
    DeltaMedia* m_old;
    DeltaMedia* m_patch;
    DeltaMedia* m_new;
    U8 m_patchBuffer[DELTA_PATCH_BUFFER_SIZE];
    U8 m_ringBuffer[DELTA_RING_SIZE];
    U8 m_outBuffer[DELTA_OUTPUT_BUFFER_SIZE];
    DeltaRing m_ring;

    State m_state;
    Status m_lastStatus;
    U32 m_oldSize;
    U32 m_oldCrc;
    U32 m_newSize;
    U32 m_newCrc;
    U32 m_chunkCount;
    U32 m_chunkBytes;
    U32 m_chunkIndex;
    U32 m_resumedChunks;
    U32 m_verifyChunks;  //!< Chunks of existing output to verify on resume
    FwSizeType m_patchSize;
    FwSizeType m_patchPos;  //!< Media offset of the next chunk header
    FwSizeType m_verifyOffset;
    U32 m_verifyCrc;     //!< Running CRC of the region under verification
    U32 m_runningCrc;    //!< New image CRC including the chunk in progress
    U32 m_committedCrc;  //!< New image CRC through the last accepted chunk
};

}  // namespace Update
#endif
