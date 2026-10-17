// ======================================================================
// \title  DeltaCoderRle.hpp
// \author starchmd
// \brief  hpp file for the byte run-length SPatch payload coder
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaCoderRle_HPP
#define Update_Delta_DeltaCoderRle_HPP

#include "FprimeExtras/Update/Delta/DeltaCoder.hpp"

namespace Update {

//! \brief Byte run-length coder (id 1)
//!
//! Stream: control byte c; c < 0x80 => the next c+1 bytes are literals; c >= 0x80 => the next byte is repeated
//! (c - 0x80) + 2 times. See the layout comment above decode().
class DeltaCoderRle final : public DeltaCoder {
  public:
    DeltaCoderRle();
    U8 id() const override { return ID_RLE; }
    void reset() override;
    Status decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaWindow& window) override;
    //! A token is pending until its last literal or repeated byte has been pushed
    bool pending() const override { return this->m_token.mode != CONTROL; }

  private:
    //! Position within the current token
    enum Mode : U8 {
        CONTROL,       //!< Expecting a control byte
        LITERAL,       //!< Copying `remaining` literal bytes
        REPEAT_VALUE,  //!< Expecting the byte to repeat
        REPEAT         //!< Emitting `remaining` copies of `value`
    };

    //! Decoder position within the token in progress; the only state carried between decode() calls
    struct Token {
        Mode mode;             //!< Position within the token
        U8 value;              //!< Byte being repeated (REPEAT)
        FwSizeType remaining;  //!< Output bytes left in the token
    };

    Token m_token;
};

}  // namespace Update
#endif
