// ======================================================================
// \title  DeltaCoderNone.cpp
// \author starchmd
// \brief  cpp file for the pass-through SPatch payload coder
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include "FprimeExtras/Update/Delta/DeltaCoderNone.hpp"

#include "Fw/Types/Assert.hpp"

namespace Update {

// Payload layout (coder id 0): every byte is one decoded byte; there are no tokens
//
//     +--------+--------+-----+--------+
//     | op[0]  | op[1]  | ... | op[n-1]|      n = coded_len of the chunk
//     +--------+--------+-----+--------+
DeltaCoder::Status DeltaCoderNone::decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaWindow& window) {
    FW_ASSERT(in != nullptr || inLen == 0);
    for (consumed = 0; consumed < inLen && window.space() > 0; consumed++) {
        window.push(in[consumed]);
    }
    return OP_OK;
}

}  // namespace Update
