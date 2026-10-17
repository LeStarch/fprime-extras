// ======================================================================
// \title  DeltaCoderLzss.hpp
// \author starchmd
// \brief  hpp file for the 256-byte-window LZSS SPatch payload coder
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaCoderLzss_HPP
#define Update_Delta_DeltaCoderLzss_HPP

#include "FprimeExtras/Update/Delta/DeltaCoder.hpp"

namespace Update {

//! \brief LZSS coder (id 2); its 256-byte window lives in the window history (window capacity must be >= WINDOW)
//!
//! Stream: flag byte, then 8 items MSB first. Flag bit 0 => one literal byte. Flag bit 1 => two bytes: distance-1
//! (distance 1..256) and length-3 (length 3..258); bytes are copied from `distance` back in history, overlapping
//! allowed. The stream may end after any item; trailing unused flag bits are ignored. See the layout comment above
//! decode(). Requires a window capacity of at least 256.
class DeltaCoderLzss final : public DeltaCoder {
  public:
    static constexpr FwSizeType WINDOW = 256;
    static constexpr FwSizeType MIN_MATCH = 3;
    static constexpr U8 ITEMS_PER_FLAG_BYTE = 8;
    DeltaCoderLzss();
    U8 id() const override { return ID_LZSS; }
    void reset() override;
    Status decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaWindow& window) override;
    //! A match is pending until copied out; a flag byte must introduce at least one item, so a trailing flag byte
    //! with no items is surplus data and also pending
    bool pending() const override {
        return (this->m_token.mode == MATCH_LENGTH) || (this->m_token.mode == MATCH_COPY) ||
               ((this->m_token.mode == ITEM) && (this->m_token.bitsLeft == ITEMS_PER_FLAG_BYTE));
    }

  private:
    //! Position within the current flag group
    enum Mode : U8 {
        FLAGS,         //!< Expecting a flag byte
        ITEM,          //!< Expecting the next item (literal or match distance)
        MATCH_LENGTH,  //!< Expecting the match length byte
        MATCH_COPY     //!< Copying `remaining` bytes from `distance` back in history
    };

    //! Decoder position within the flag group in progress; the only state carried between decode() calls
    struct Token {
        Mode mode;             //!< Position within the flag group
        U8 flags;              //!< Remaining flag bits, next item's bit in the MSB
        U8 bitsLeft;           //!< Items left in this flag group
        FwSizeType distance;   //!< Back-reference distance of the match in progress
        FwSizeType remaining;  //!< Bytes left to copy for the match in progress
    };

    Token m_token;
};

}  // namespace Update
#endif
