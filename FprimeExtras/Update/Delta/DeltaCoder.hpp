// ======================================================================
// \title  DeltaCoder.hpp
// \author starchmd
// \brief  hpp file for the project-overridable SPatch payload decoder interface
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaCoder_HPP
#define Update_Delta_DeltaCoder_HPP

#include "FprimeExtras/Update/Delta/DeltaRing.hpp"
#include "Fw/FPrimeBasicTypes.hpp"

namespace Update {

//! \brief Streaming decompressor for SPatch chunk payloads
//!
//! The DeltaCodec feeds coded bytes in and receives decoded operation bytes in a DeltaRing. Implementations must
//! be allocation-free and hold all state in members so that decoding may pause at any byte boundary (input
//! exhausted or ring full) and resume later. reset() is called at the start of every chunk.
//!
//! Projects supply their own coder by implementing this interface and passing it to DeltaPatcher. The coder id
//! written in the SPatch header by the ground tool must equal id().
class DeltaCoder {
  public:
    //! Decode outcome
    enum Status {
        OP_OK,     //!< Progressed (or nothing to do) without error
        MALFORMED  //!< Input is not a valid stream for this coder
    };

    //! Well-known coder ids (values 0x80 and above are reserved for projects)
    enum Id : U8 {
        ID_NONE = 0,  //!< Pass-through
        ID_RLE = 1,   //!< Byte run-length encoding
        ID_LZSS = 2   //!< LZSS with a window equal to the ring capacity
    };

    virtual ~DeltaCoder() = default;

    //! Coder id as recorded in the SPatch header
    virtual U8 id() const = 0;

    //! Reset state at chunk start
    virtual void reset() = 0;

    //! \brief Decode as much as possible
    //!
    //! Consumes bytes from `in` (setting `consumed`) and pushes decoded bytes into `ring` until input is exhausted
    //! or the ring has no space. Must make progress whenever both input and space are available.
    virtual Status decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaRing& ring) = 0;
};

//! \brief Pass-through coder (id 0)
class DeltaCoderNone final : public DeltaCoder {
  public:
    U8 id() const override { return ID_NONE; }
    void reset() override {}
    Status decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaRing& ring) override;
};

//! \brief Byte run-length coder (id 1)
//!
//! Stream: control byte c; c < 0x80 => the next c+1 bytes are literals; c >= 0x80 => the next byte is repeated
//! (c - 0x80) + 2 times.
class DeltaCoderRle final : public DeltaCoder {
  public:
    DeltaCoderRle();
    U8 id() const override { return ID_RLE; }
    void reset() override;
    Status decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaRing& ring) override;

  private:
    enum Mode : U8 { CONTROL, LITERAL, REPEAT_VALUE, REPEAT };
    Mode m_mode;
    U8 m_value;
    FwSizeType m_remaining;
};

//! \brief LZSS coder (id 2) whose window is the ring itself
//!
//! Stream: flag byte, then 8 items MSB first. Flag bit 0 => one literal byte. Flag bit 1 => two bytes:
//! distance-1 (distance 1..256) and length-3 (length 3..258); bytes are copied from `distance` back in history,
//! overlapping allowed. The stream may end after any item; trailing unused flag bits are ignored. Requires a ring
//! capacity of at least 256.
class DeltaCoderLzss final : public DeltaCoder {
  public:
    static constexpr FwSizeType WINDOW = 256;
    static constexpr FwSizeType MIN_MATCH = 3;
    DeltaCoderLzss();
    U8 id() const override { return ID_LZSS; }
    void reset() override;
    Status decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaRing& ring) override;

  private:
    enum Mode : U8 { FLAGS, ITEM, MATCH_LENGTH, MATCH_COPY };
    Mode m_mode;
    U8 m_flags;
    U8 m_bitsLeft;
    FwSizeType m_distance;
    FwSizeType m_remaining;
};

}  // namespace Update
#endif
