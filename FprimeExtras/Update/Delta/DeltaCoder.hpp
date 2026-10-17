// ======================================================================
// \title  DeltaCoder.hpp
// \author starchmd
// \brief  hpp file for the project-overridable SPatch payload decoder interface
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaCoder_HPP
#define Update_Delta_DeltaCoder_HPP

#include "FprimeExtras/Update/Delta/DeltaWindow.hpp"
#include "Fw/FPrimeBasicTypes.hpp"

namespace Update {

//! \brief Streaming decompressor for SPatch chunk payloads
//!
//! The DeltaCodec feeds coded bytes in and receives decoded operation bytes in a DeltaWindow. Implementations must be
//! allocation-free and hold all state in members so that decoding may pause at any byte boundary (input exhausted or
//! window full) and resume later. reset() is called at the start of every chunk.
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
        ID_LZSS = 2   //!< LZSS, fixed 256-byte window held in the window history
    };

    virtual ~DeltaCoder() = default;

    //! Coder id as recorded in the SPatch header
    virtual U8 id() const = 0;

    //! Reset state at chunk start
    virtual void reset() = 0;

    //! \brief Decode as much as possible
    //!
    //! Consumes bytes from `in` (setting `consumed`) and pushes decoded bytes into `window` until input is exhausted or
    //! the window has no space. Must make progress whenever both input and space are available.
    virtual Status decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaWindow& window) = 0;

    //! \brief True while the coder holds an unfinished token or undelivered output
    //!
    //! Queried by DeltaCodec once a chunk's coded bytes are exhausted: a valid stream ends at a token boundary with
    //! every decoded byte already pushed into the window. Legal trailing padding (such as unused LZSS flag bits) must
    //! not count as pending.
    virtual bool pending() const = 0;
};

}  // namespace Update
#endif
