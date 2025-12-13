// ======================================================================
// \title  BufferRepeater.cpp
// \author starchmd
// \brief  cpp file for BufferRepeater component implementation class
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================

#include "FprimeExtras/Utilities/BufferRepeater/BufferRepeater.hpp"
#include "Fw/Logger/Logger.hpp"


namespace Utilities {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

BufferRepeater ::BufferRepeater(const char* const compName) : BufferRepeaterComponentBase(compName) {}

BufferRepeater ::~BufferRepeater() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void BufferRepeater ::multiIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    bool last_to_return = false;
    // Decrement the count for this buffer in the map and update last_to_return if this was the last outstanding buffer
    {
        Os::ScopeLock lock(this->m_mapLock);
        Fw::Logger::log("BufferRepeater: multiIn_handler called on port %" PRI_FwIndexType " with %p\n", portNum, fwBuffer.getData());
        // First get the original value ensuring no unknown buffers were returned
        FwIndexType value = 0;
        Fw::Success status = this->m_bufferToCount.find(fwBuffer.getData(), value);
        FW_ASSERT(status == Fw::Success::SUCCESS);

        // Next store the decremented value back into the map, unless it is 0 then remove it.
        value -= 1;
        if (value == 0) {
            last_to_return = true;
            status = this->m_bufferToCount.remove(fwBuffer.getData(), value);
            FW_ASSERT(status == Fw::Success::SUCCESS);
        } else {
            status = this->m_bufferToCount.insert(fwBuffer.getData(), value);
            FW_ASSERT(status == Fw::Success::SUCCESS);
        }
    }
    // All multiOut ports have returned the buffer, return it to singleOut exactly once
    if (last_to_return) {
        Fw::Logger::log("BufferRepeater: singleOut_out: %p\n", fwBuffer.getData());
        this->singleOut_out(0, fwBuffer);
    }
}

void BufferRepeater ::singleIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    Fw::ParamValid isValid = Fw::ParamValid::INVALID;
    const auto enabled_array = this->paramGet_CHANNEL_ENABLED(isValid);
    FW_ASSERT((isValid == Fw::ParamValid::VALID) || (isValid == Fw::ParamValid::DEFAULT), static_cast<FwAssertArgType>(isValid));
    
    // Determine the number of connected multiOut ports. This must be done before the fan out to avoid buffer returns
    // while computing the necessary return count.
    FwIndexType connected_ports = 0;
    for (FwIndexType i = 0; i < this->NUM_MULTIOUT_OUTPUT_PORTS; i++) {
        if (this->isConnected_multiOut_OutputPort(i) && (enabled_array[i] == Fw::Enabled::ENABLED)) {
            connected_ports += 1;
        }
    }

    // Update the map with the count of connected ports for this buffer when there are any connected ports
    if (connected_ports > 0) {
        // Lock the map and insert the buffer with the count of connected ports
        {
            Os::ScopeLock lock(this->m_mapLock);
            // Ensure we are not handling memory already in-flight by ensuring that the buffer is not already in the map
            FwIndexType _ = 0;
            Fw::Success status = this->m_bufferToCount.find(fwBuffer.getData(), _);
            FW_ASSERT(status != Fw::Success::SUCCESS);
            // Insert the buffer with the count of connected ports and ensure it was successful
            status = this->m_bufferToCount.insert(fwBuffer.getData(), connected_ports);
            FW_ASSERT(
                status ==
                Fw::Success::SUCCESS);  // If this trips, Utilities::BUFFER_FANOUT_MAX_BUFFERS_IN_FLIGHT is too small
        }
        // Perform the multiOut fan out
        for (FwIndexType i = 0; i < this->NUM_MULTIOUT_OUTPUT_PORTS; i++) {
            if (this->isConnected_multiOut_OutputPort(i) && (enabled_array[i] == Fw::Enabled::ENABLED)) {
                Fw::Logger::log("BufferRepeater: multiOut_out called on port %" PRI_FwIndexType " with %p\n", i, fwBuffer.getData());
                this->multiOut_out(i, fwBuffer);
            }
        }
    } else {
        // No connected multiOut ports, return buffer immediately
        Fw::Logger::log("BufferRepeater: singleOut_out called directly with %p\n", fwBuffer.getData());
        this->singleOut_out(0, fwBuffer);
    }
}

}  // namespace Utilities
