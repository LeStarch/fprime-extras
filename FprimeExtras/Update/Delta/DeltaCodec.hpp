// ======================================================================
// \title  DeltaCodec.hpp
// \author starchmd
// \brief  hpp file for the SPatch v2 streaming patch engine
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaCodec_HPP
#define Update_Delta_DeltaCodec_HPP

#include "ExtrasConfig/DeltaCodecConfig.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoder.hpp"
#include "FprimeExtras/Update/Delta/DeltaMedia.hpp"
#include "FprimeExtras/Update/Delta/DeltaWindow.hpp"
#include "Fw/FPrimeBasicTypes.hpp"

namespace Update {

//! \brief Allocation-free, bounded-step SPatch v2 engine
//!
//! Applies an SPatch (format: FprimeExtras/Update/DeltaPatcher/docs/sdd.md) held in `patch` media to the `oldImage`
//! media producing the `newImage` media. The engine is the work delegate of the Update.DeltaPatchMachine state
//! machine: it owns no lifecycle state of its own. The caller runs the stages in order — begin(), then verifyNew()
//! (if hasOutputToVerify()), verifyOld(), patchChunk(), verifyFinal() — calling each stage function once per tick
//! until it reports STAGE_DONE. Each call is bounded to one chunk (or DELTA_VERIFY_BYTES_PER_STEP bytes of CRC
//! verification) and at most DELTA_MAX_IO_PER_STEP media accesses, so the engine may be driven from a rate group.
//! All storage is fixed: four buffers sized by DeltaCodecConfig.hpp plus the state structs below.
//!
//! Resume: chunks already present in the new image media are CRC-verified and skipped, so an interrupted patch
//! restarts from the first missing or corrupt chunk with no persisted engine state.
//!
//! Completion: after the last chunk the new image media is flushed and read back in full; verifyFinal() reports
//! STAGE_DONE only when the stored size and CRC match the header.
class DeltaCodec final {
  public:
    //! Outcome of a stage call; mirrors Update.DeltaPatchStatus
    enum Status {
        OP_OK,
        BAD_HEADER,           //!< Magic, version, flags, CRC, or geometry of the SPatch
                              //!< header is invalid
        OLD_IMAGE_MISMATCH,   //!< Old image size or CRC differs from the header
        TRUNCATED,            //!< Patch ended before the declared content
        CHUNK_CRC,            //!< Chunk output CRC failed
        BAD_OPCODE,           //!< Unknown op, operand out of bounds, or coder reported
                              //!< malformed payload
        READ_ERROR,           //!< Media read failed
        WRITE_ERROR,          //!< Media write failed
        NEW_IMAGE_MISMATCH,   //!< Final new image CRC, or stored size/CRC on
                              //!< read-back, differs from the header
        PATCH_SIZE_MISMATCH,  //!< Patch has bytes beyond the last chunk
        OUTPUT_STALE,         //!< Existing new image is larger than the target; caller must
                              //!< recreate it
        CODER_MISMATCH,       //!< Header is valid but names a coder other than the
                              //!< installed one
        SIZE_WIDTH_MISMATCH   //!< Header sizes were serialized with a different
                              //!< FwSizeType width than this build's
    };

    //! Progress of a stage after one bounded unit of work
    enum StepResult {
        IN_PROGRESS,  //!< The stage has more work; call again next tick
        STAGE_DONE    //!< The stage is complete; advance to the next stage
    };

    //! SPatch v2 constants. Integers are F Prime serialized (big-endian); sizes are FwSizeType, whose width is recorded
    //! in the header so a ground/flight disagreement is detected rather than misparsed
    static constexpr U8 VERSION = 2;
    static constexpr FwSizeType SIZE_FIELD_WIDTH = sizeof(FwSizeType);
    static_assert(SIZE_FIELD_WIDTH == 4 || SIZE_FIELD_WIDTH == 8, "SPatch supports 32- and 64-bit FwSizeType");
    // Header field offsets
    static constexpr FwSizeType HDR_VERSION = 4;
    static constexpr FwSizeType HDR_CODER = 5;
    static constexpr FwSizeType HDR_FLAGS = 6;
    static constexpr FwSizeType HDR_SIZE_WIDTH = 7;
    static constexpr FwSizeType HDR_OLD_SIZE = 8;
    static constexpr FwSizeType HDR_OLD_CRC = HDR_OLD_SIZE + SIZE_FIELD_WIDTH;
    static constexpr FwSizeType HDR_NEW_SIZE = HDR_OLD_CRC + sizeof(U32);
    static constexpr FwSizeType HDR_NEW_CRC = HDR_NEW_SIZE + SIZE_FIELD_WIDTH;
    static constexpr FwSizeType HDR_CHUNK_BYTES = HDR_NEW_CRC + sizeof(U32);
    static constexpr FwSizeType HDR_CRC = HDR_CHUNK_BYTES + SIZE_FIELD_WIDTH;  //!< CRC32 of header[0:HDR_CRC]
    static constexpr FwSizeType HEADER_SIZE = HDR_CRC + sizeof(U32);           //!< 32 B (32-bit sizes) or 44 B (64-bit)
    // Chunk header field offsets
    static constexpr FwSizeType CHUNK_HDR_CODED_LENGTH = 0;
    static constexpr FwSizeType CHUNK_HDR_CRC = CHUNK_HDR_CODED_LENGTH + SIZE_FIELD_WIDTH;  //!< CRC32 of decoded output
    static constexpr FwSizeType CHUNK_HEADER_SIZE = CHUNK_HDR_CRC + sizeof(U32);

    //! Construct an engine using the supplied coder
    explicit DeltaCodec(DeltaCoder& coder);
    DeltaCodec(const DeltaCodec&) = delete;
    DeltaCodec& operator=(const DeltaCodec&) = delete;

    //! Replace the coder (project plugin seam); only permitted while no media is bound (before begin() or after
    //! reset())
    void setCoder(DeltaCoder& coder);

    //! \brief Bind the media, read and validate the header, and size the resume
    //!
    //! Media must remain bound until the caller has finished the stages or calls reset().
    Status begin(DeltaMedia& oldImage, DeltaMedia& patch, DeltaMedia& newImage);

    //! True when the existing new image holds whole chunks that verifyNew() must check before patching (resume)
    bool hasOutputToVerify() const { return this->m_progress.verifyChunks > 0; }

    //! \brief Stage 1 (resume only): CRC-verify up to DELTA_VERIFY_BYTES_PER_STEP bytes of the existing new image
    //!
    //! Chunks whose stored bytes match the chunk CRC in the patch are skipped; the first chunk that does not match
    //! (or cannot be read) is where patching restarts.
    Status verifyNew(StepResult& result);

    //! Stage 2: CRC-verify up to DELTA_VERIFY_BYTES_PER_STEP bytes of the old image against the header
    Status verifyOld(StepResult& result);

    //! \brief Stage 3: apply one chunk (or the slice of it the I/O budget allows)
    //!
    //! After the last chunk, checks that the patch holds no trailing bytes and that the committed output CRC
    //! matches the header, then flushes and re-resolves the new image and reports STAGE_DONE.
    Status patchChunk(StepResult& result);

    //! Stage 4: read back up to DELTA_VERIFY_BYTES_PER_STEP bytes of the stored new image; STAGE_DONE only when the
    //! stored size and CRC match the header
    Status verifyFinal(StepResult& result);

    //! Forget the media and all progress
    void reset();

    FwSizeType chunkIndex() const { return this->m_progress.chunkIndex; }
    FwSizeType chunkCount() const { return this->m_header.chunkCount; }
    FwSizeType resumedChunks() const { return this->m_progress.resumedChunks; }
    FwSizeType bytesWritten() const;
    FwSizeType oldSize() const { return this->m_header.oldSize; }
    U32 oldCrc() const { return this->m_header.oldCrc; }
    //! CRC computed over the old image by verifyOld(); valid once that stage has reported
    U32 actualOldCrc() const { return this->m_verify.crc; }
    FwSizeType newSize() const { return this->m_header.newSize; }
    U32 newCrc() const { return this->m_header.newCrc; }

  private:
    enum Op : U8 { OP_COPY = 0, OP_ADD = 1, OP_LIT = 2, OP_SEEK = 3 };

    //! Geometry declared by the SPatch header (plus the patch media size); fixed for the life of one patch
    struct Header {
        FwSizeType oldSize;     //!< Old image size
        U32 oldCrc;             //!< Old image CRC32
        FwSizeType newSize;     //!< New image size
        U32 newCrc;             //!< New image CRC32
        FwSizeType chunkBytes;  //!< Output bytes per chunk (last chunk may be shorter)
        FwSizeType chunkCount;  //!< ceil(newSize / chunkBytes)
        FwSizeType patchSize;   //!< Size of the patch media at begin()
    };

    //! Position through the patch; advances only at chunk boundaries
    struct Progress {
        FwSizeType chunkIndex;     //!< Next chunk to verify or apply
        FwSizeType patchPos;       //!< Media offset of the next chunk header
        FwSizeType resumedChunks;  //!< Chunks skipped because the existing output verified
        FwSizeType verifyChunks;   //!< Chunks of existing output to verify on resume
        U32 runningCrc;            //!< New image CRC including the chunk in progress
        U32 committedCrc;          //!< New image CRC through the last accepted chunk
    };

    //! Region under CRC verification (existing output, old image, or final read-back)
    struct VerifySpan {
        FwSizeType offset;  //!< Next byte to read
        U32 crc;            //!< Running CRC of the bytes read so far
    };

    //! Chunk in progress; persists across ticks when the I/O budget is exhausted
    struct Chunk {
        bool active;                //!< A chunk header has been read and the chunk is partially applied
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
        bool seekPending;           //!< A SEEK was parsed and must be followed by a producing
                                    //!< op
        FwSizeType opCount;         //!< Operations parsed in this chunk, capped at
                                    //!< DELTA_MAX_OPS_PER_CHUNK
    };

    //! Old-image bytes held in m_oldBuffer so short adjacent COPY/ADD operations share one media read
    struct OldCache {
        FwSizeType offset;  //!< Old-image offset of m_oldBuffer[0]
        FwSizeType length;  //!< Valid bytes in m_oldBuffer (0 when empty)
    };

    //! Most passes a verification stage makes per call: each pass CRCs at least one byte of the budget
    static constexpr FwSizeType MAX_VERIFY_PASSES = DELTA_VERIFY_BYTES_PER_STEP;

    //! Read and validate the SPatch header into m_header
    Status readHeader(DeltaMedia& patch);
    //! Classify a failed media read: the documented mismatch status when the
    //! media no longer has the size captured at begin(), READ_ERROR otherwise
    Status readFailure(DeltaMedia& media, FwSizeType expectedSize, Status mismatch);
    Status readChunkHeader(FwSizeType& codedLength, U32& crc);
    //! Verify one slice (at most `budget` bytes, one media read) of `media` into m_verify
    Status verifySlice(DeltaMedia& media, FwSizeType regionStart, FwSizeType regionLength, FwSizeType& budget);
    //! Abandon the chunk in progress and report `status`
    Status failChunk(Status status);
    Status beginChunk();
    Status finishChunk();
    Status finishImage(StepResult& result);
    Status fillWindow(Chunk& chunk);
    Status parseOp(Chunk& chunk);
    Status executeOp(Chunk& chunk);
    Status readOld(FwSizeType offset, U8* out, FwSizeType& length);
    Status flushOutput(Chunk& chunk);
    enum VarintResult { VARINT_OK, VARINT_NEED_MORE, VARINT_INVALID };
    VarintResult readVarint(FwSizeType start, U64& value, FwSizeType& length) const;

    static U32 crcInit();
    static U32 crcUpdate(U32 crc, const U8* data, FwSizeType length);
    static U32 crcFinal(U32 crc);

    DeltaCoder* m_coder;
    DeltaMedia* m_old;
    DeltaMedia* m_patch;
    DeltaMedia* m_new;
    U8 m_patchBuffer[DELTA_PATCH_BUFFER_SIZE];
    U8 m_windowBuffer[DELTA_WINDOW_SIZE];
    U8 m_outBuffer[DELTA_OUTPUT_BUFFER_SIZE];
    U8 m_oldBuffer[DELTA_OLD_BUFFER_SIZE];
    DeltaWindow m_window;
    Header m_header;
    Progress m_progress;
    VerifySpan m_verify;
    Chunk m_chunk;
    OldCache m_oldCache;
    FwSizeType m_ioCount;  //!< Media reads and writes issued in the current stage call
};

}  // namespace Update
#endif
