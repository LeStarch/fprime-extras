// ======================================================================
// \title  DeltaCoder.cpp
// \author starchmd
// \brief  cpp file for the shipped SPatch payload decoders
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include "FprimeExtras/Update/Delta/DeltaCoder.hpp"

#include "Fw/Types/Assert.hpp"

namespace Update {

// ----------------------------------------------------------------------
// DeltaCoderNone
// ----------------------------------------------------------------------

DeltaCoder::Status DeltaCoderNone::decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaRing& ring) {
    FW_ASSERT(in != nullptr || inLen == 0);
    consumed = 0;
    while (consumed < inLen && ring.space() > 0) {
        ring.push(in[consumed]);
        consumed++;
    }
    return OP_OK;
}

// ----------------------------------------------------------------------
// DeltaCoderRle
// ----------------------------------------------------------------------

DeltaCoderRle::DeltaCoderRle() : m_mode(CONTROL), m_value(0), m_remaining(0) {}

void DeltaCoderRle::reset() {
    this->m_mode = CONTROL;
    this->m_value = 0;
    this->m_remaining = 0;
}

DeltaCoder::Status DeltaCoderRle::decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaRing& ring) {
    FW_ASSERT(in != nullptr || inLen == 0);
    consumed = 0;
    while (true) {
        switch (this->m_mode) {
            case CONTROL: {
                if (consumed >= inLen) {
                    return OP_OK;
                }
                const U8 control = in[consumed];
                consumed++;
                if (control < 0x80) {
                    this->m_remaining = static_cast<FwSizeType>(control) + 1;
                    this->m_mode = LITERAL;
                } else {
                    this->m_remaining = static_cast<FwSizeType>(control - 0x80) + 2;
                    this->m_mode = REPEAT_VALUE;
                }
                break;
            }
            case LITERAL:
                if (consumed >= inLen || ring.space() == 0) {
                    return OP_OK;
                }
                ring.push(in[consumed]);
                consumed++;
                this->m_remaining--;
                if (this->m_remaining == 0) {
                    this->m_mode = CONTROL;
                }
                break;
            case REPEAT_VALUE:
                if (consumed >= inLen) {
                    return OP_OK;
                }
                this->m_value = in[consumed];
                consumed++;
                this->m_mode = REPEAT;
                break;
            case REPEAT:
                if (ring.space() == 0) {
                    return OP_OK;
                }
                ring.push(this->m_value);
                this->m_remaining--;
                if (this->m_remaining == 0) {
                    this->m_mode = CONTROL;
                }
                break;
            default:
                FW_ASSERT(false, static_cast<FwAssertArgType>(this->m_mode));
                return MALFORMED;
        }
    }
}

// ----------------------------------------------------------------------
// DeltaCoderLzss
// ----------------------------------------------------------------------

DeltaCoderLzss::DeltaCoderLzss() : m_mode(FLAGS), m_flags(0), m_bitsLeft(0), m_distance(0), m_remaining(0) {}

void DeltaCoderLzss::reset() {
    this->m_mode = FLAGS;
    this->m_flags = 0;
    this->m_bitsLeft = 0;
    this->m_distance = 0;
    this->m_remaining = 0;
}

DeltaCoder::Status DeltaCoderLzss::decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaRing& ring) {
    FW_ASSERT(in != nullptr || inLen == 0);
    FW_ASSERT(ring.capacity() >= WINDOW, static_cast<FwAssertArgType>(ring.capacity()));
    consumed = 0;
    while (true) {
        switch (this->m_mode) {
            case FLAGS:
                if (consumed >= inLen) {
                    return OP_OK;
                }
                this->m_flags = in[consumed];
                consumed++;
                this->m_bitsLeft = 8;
                this->m_mode = ITEM;
                break;
            case ITEM: {
                if (this->m_bitsLeft == 0) {
                    this->m_mode = FLAGS;
                    break;
                }
                if (consumed >= inLen) {
                    return OP_OK;
                }
                const bool isMatch = (this->m_flags & 0x80) != 0;
                if (isMatch) {
                    this->m_distance = static_cast<FwSizeType>(in[consumed]) + 1;
                    consumed++;
                    this->m_mode = MATCH_LENGTH;
                } else {
                    if (ring.space() == 0) {
                        return OP_OK;
                    }
                    ring.push(in[consumed]);
                    consumed++;
                    this->m_flags = static_cast<U8>(this->m_flags << 1);
                    this->m_bitsLeft--;
                }
                break;
            }
            case MATCH_LENGTH:
                if (consumed >= inLen) {
                    return OP_OK;
                }
                this->m_remaining = static_cast<FwSizeType>(in[consumed]) + MIN_MATCH;
                consumed++;
                if (this->m_distance > ring.historyAvailable()) {
                    return MALFORMED;
                }
                this->m_mode = MATCH_COPY;
                break;
            case MATCH_COPY:
                if (ring.space() == 0) {
                    return OP_OK;
                }
                ring.push(ring.history(this->m_distance));
                this->m_remaining--;
                if (this->m_remaining == 0) {
                    this->m_flags = static_cast<U8>(this->m_flags << 1);
                    this->m_bitsLeft--;
                    this->m_mode = ITEM;
                }
                break;
            default:
                FW_ASSERT(false, static_cast<FwAssertArgType>(this->m_mode));
                return MALFORMED;
        }
    }
}

}  // namespace Update
