// ======================================================================
// \title  BufferCollector.cpp
// \author starchmd
// \brief  cpp file for BufferCollector component implementation class
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================

#include "FprimeExtras/Utilities/BufferCollector/BufferCollector.hpp"

#include "Fw/Logger/Logger.hpp"

namespace Utilities {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

BufferCollector ::BufferCollector(const char* const compName) : BufferCollectorComponentBase(compName) {}

BufferCollector ::~BufferCollector() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void BufferCollector ::multiIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    {
        Os::ScopeLock lock(this->m_mapLock);
        // Ensure no duplicate entries for this buffer
        FwIndexType value;
        Fw::Success status = this->m_bufferToIndex.find(fwBuffer.getData(), value);
        FW_ASSERT(status != Fw::Success::SUCCESS);

        // Insert the buffer with the port index it came from
        status = this->m_bufferToIndex.insert(fwBuffer.getData(), portNum);
        FW_ASSERT(status == Fw::Success::SUCCESS);
    }
    this->singleOut_out(0, fwBuffer);
}

void BufferCollector ::singleIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    FwIndexType origin;
    {
        Os::ScopeLock lock(this->m_mapLock);
        // Find the entry ensuring it exists
        Fw::Success status = this->m_bufferToIndex.remove(fwBuffer.getData(), origin);
        FW_ASSERT(status == Fw::Success::SUCCESS);
    }
    this->multiOut_out(origin, fwBuffer);
}

}  // namespace Utilities
