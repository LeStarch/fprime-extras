// ======================================================================
// \title  DeltaCoderRle.cpp
// \author starchmd
// \brief  cpp file for the byte run-length SPatch payload coder
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include "FprimeExtras/Update/Delta/DeltaCoderRle.hpp"

#include "Fw/Types/Assert.hpp"

namespace Update {

DeltaCoderRle::DeltaCoderRle() : m_token() {
    this->reset();
}

void DeltaCoderRle::reset() {
    this->m_token.mode = CONTROL;
    this->m_token.value = 0;
    this->m_token.remaining = 0;
}

// Payload layout (coder id 1): a sequence of tokens, each introduced by one control byte c
//
//     Literal run (c in 0x00..0x7F): the next c + 1 bytes are copied to the output
//     +--------+--------+-----+------------+
//     |   c    | lit[0] | ... | lit[c]     |      1..128 literal bytes
//     +--------+--------+-----+------------+
//
//     Repeat run (c in 0x80..0xFF): one value byte, emitted (c - 0x80) + 2 times
//     +--------+--------+
//     |   c    | value  |                        2..129 copies of value
//     +--------+--------+
//
// A valid payload ends exactly at a token boundary; a token cut short by the end of the chunk is reported through
// pending() and rejected by the engine.
DeltaCoder::Status DeltaCoderRle::decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaWindow& window) {
    FW_ASSERT(in != nullptr || inLen == 0);
    consumed = 0;
    Token& token = this->m_token;
    // Every iteration consumes an input byte or emits an output byte, so this many iterations drain both
    const FwSizeType maxIterations = inLen + window.space();
    for (FwSizeType iteration = 0; iteration < maxIterations; iteration++) {
        switch (token.mode) {
            case CONTROL: {
                if (consumed >= inLen) {
                    return OP_OK;
                }
                const U8 control = in[consumed];
                consumed++;
                if (control < 0x80) {
                    token.remaining = static_cast<FwSizeType>(control) + 1;
                    token.mode = LITERAL;
                } else {
                    token.remaining = static_cast<FwSizeType>(control - 0x80) + 2;
                    token.mode = REPEAT_VALUE;
                }
                break;
            }
            case LITERAL:
                if (consumed >= inLen || window.space() == 0) {
                    return OP_OK;
                }
                window.push(in[consumed]);
                consumed++;
                token.remaining--;
                if (token.remaining == 0) {
                    token.mode = CONTROL;
                }
                break;
            case REPEAT_VALUE:
                if (consumed >= inLen) {
                    return OP_OK;
                }
                token.value = in[consumed];
                consumed++;
                token.mode = REPEAT;
                break;
            case REPEAT:
                if (window.space() == 0) {
                    return OP_OK;
                }
                window.push(token.value);
                token.remaining--;
                if (token.remaining == 0) {
                    token.mode = CONTROL;
                }
                break;
            default:
                FW_ASSERT(false, static_cast<FwAssertArgType>(token.mode));
                return MALFORMED;
        }
    }
    // Input and space are exhausted (the bound above is exact for the progress each iteration makes)
    return OP_OK;
}

}  // namespace Update
