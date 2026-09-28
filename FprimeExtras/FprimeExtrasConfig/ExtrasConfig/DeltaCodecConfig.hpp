// ======================================================================
// \title  DeltaCodecConfig.hpp
// \author starchmd
// \brief  hpp file configuring the fixed buffers of the SPatch DeltaCodec
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#ifndef Update_DeltaCodecConfig_HPP
#define Update_DeltaCodecConfig_HPP
#include "Fw/FPrimeBasicTypes.hpp"
namespace Update {
//! Bytes of coded patch data read per media access
constexpr FwSizeType DELTA_PATCH_BUFFER_SIZE = 256;

//! Decoded operation ring; also the history window of the shipped LZSS coder (must be >= 256 for that coder)
constexpr FwSizeType DELTA_RING_SIZE = 256;

//! Bytes of new image accumulated before each media write; also the old-image read granularity
constexpr FwSizeType DELTA_OUTPUT_BUFFER_SIZE = 256;

//! Bytes CRC-verified (old image, or existing new image on resume) per DeltaCodec::step()
constexpr FwSizeType DELTA_VERIFY_BYTES_PER_STEP = 4096;

}  // namespace Update
#endif  // Update_DeltaCodecConfig_HPP
