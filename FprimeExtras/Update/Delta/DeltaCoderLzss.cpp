// ======================================================================
// \title  DeltaCoderLzss.cpp
// \author starchmd
// \brief  cpp file for the 256-byte-window LZSS SPatch payload coder
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include "FprimeExtras/Update/Delta/DeltaCoderLzss.hpp"

#include "Fw/Types/Assert.hpp"

namespace Update {

DeltaCoderLzss::DeltaCoderLzss() : m_token() {
    this->reset();
}

void DeltaCoderLzss::reset() {
    this->m_token.mode = FLAGS;
    this->m_token.flags = 0;
    this->m_token.bitsLeft = 0;
    this->m_token.distance = 0;
    this->m_token.remaining = 0;
}

// Payload layout (coder id 2): flag groups of one flag byte followed by up to 8 items, MSB first
//
//     +--------+----------+----------+-----+----------+
//     | flags  | item[7]  | item[6]  | ... | item[0]  |      item[k] is described by flags bit k
//     +--------+----------+----------+-----+----------+
//
//     flags bit = 0, literal item (1 byte):      flags bit = 1, match item (2 bytes):
//     +--------+                                 +------------+------------+
//     | byte   |                                 | distance-1 | length-3   |
//     +--------+                                 +------------+------------+
//                                                distance 1..256 back in the decoded history (must not reach
//                                                before the chunk start), length 3..258, overlap allowed
//
// The last flag group may be short: the payload may end after any complete item and the unused low flag bits
// are ignored. A flag byte followed by no item, or a match cut short, is reported through pending() and
// rejected by the engine.
DeltaCoder::Status DeltaCoderLzss::decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaWindow& window) {
    FW_ASSERT(in != nullptr || inLen == 0);
    FW_ASSERT(window.capacity() >= WINDOW, static_cast<FwAssertArgType>(window.capacity()));
    consumed = 0;
    Token& token = this->m_token;
    // Every iteration consumes an input byte, emits an output byte, or is the single ITEM->FLAGS transition that
    // precedes one of those, so twice the available work (plus one) drains both input and space
    const FwSizeType maxIterations = 2 * (inLen + window.space()) + 1;
    for (FwSizeType iteration = 0; iteration < maxIterations; iteration++) {
        switch (token.mode) {
            case FLAGS:
                if (consumed >= inLen) {
                    return OP_OK;
                }
                token.flags = in[consumed];
                consumed++;
                token.bitsLeft = ITEMS_PER_FLAG_BYTE;
                token.mode = ITEM;
                break;
            case ITEM: {
                if (token.bitsLeft == 0) {
                    token.mode = FLAGS;
                    break;
                }
                if (consumed >= inLen) {
                    return OP_OK;
                }
                const bool isMatch = (token.flags & 0x80) != 0;
                if (isMatch) {
                    token.distance = static_cast<FwSizeType>(in[consumed]) + 1;
                    consumed++;
                    token.mode = MATCH_LENGTH;
                } else {
                    if (window.space() == 0) {
                        return OP_OK;
                    }
                    window.push(in[consumed]);
                    consumed++;
                    token.flags = static_cast<U8>(token.flags << 1);
                    token.bitsLeft--;
                }
                break;
            }
            case MATCH_LENGTH:
                if (consumed >= inLen) {
                    return OP_OK;
                }
                token.remaining = static_cast<FwSizeType>(in[consumed]) + MIN_MATCH;
                consumed++;
                if (token.distance > window.historyAvailable()) {
                    return MALFORMED;
                }
                token.mode = MATCH_COPY;
                break;
            case MATCH_COPY:
                if (window.space() == 0) {
                    return OP_OK;
                }
                window.push(window.history(token.distance));
                token.remaining--;
                if (token.remaining == 0) {
                    token.flags = static_cast<U8>(token.flags << 1);
                    token.bitsLeft--;
                    token.mode = ITEM;
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
