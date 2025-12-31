// ======================================================================
// \title  DropDetectorConfig.hpp
// \author starchmd
// \brief  hpp file for DropDetector component implementation class
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================

#ifndef Utilities_DropDetectorConfig_HPP
#define Utilities_DropDetectorConfig_HPP
#include "Fw/FPrimeBasicTypes.hpp"
namespace Utilities {
//! Number of chunks of data to process per rate tick
constexpr FwSizeType DROP_DETECTOR_CHUNKS_PER_RATE_TICK = 20;

//! Size of stack buffer for file reads
constexpr FwSizeType DROP_DETECTOR_FILE_READ_BUFFER_SIZE = 256;

}  // namespace Utilities
#endif // Utilities_DropDetectorConfig_HPP