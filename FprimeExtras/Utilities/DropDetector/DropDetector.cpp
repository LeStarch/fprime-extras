// ======================================================================
// \title  DropDetector.cpp
// \author starchmd
// \brief  cpp file for DropDetector component implementation class
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================

#include "FprimeExtras/Utilities/DropDetector/DropDetector.hpp"
#include "ExtrasConfig/DropDetectorConfig.hpp"
namespace Utilities {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

DropDetector ::DropDetector(const char* const compName) : DropDetectorComponentBase(compName), m_packetSize(0), m_index(0), m_opCode(0), m_cmdSeq(0) {}

DropDetector ::~DropDetector() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

DropDetector::PacketStatus DropDetector ::readPacket(Os::File::Status& fileStatus) {
    DropDetector::PacketStatus result = PacketStatus::POSSIBLE_DROP;
    fileStatus = Os::File::OP_OK;
    U8 buffer[Utilities::DROP_DETECTOR_FILE_READ_BUFFER_SIZE];
    constexpr FwSizeType BOUND = std::numeric_limits<FwSizeType>::max() - Utilities::DROP_DETECTOR_FILE_READ_BUFFER_SIZE;

    // Loop for enough chunks to cover the packet size
    for (FwSizeType i = 0; i < this->m_packetSize && i < BOUND; i += Utilities::DROP_DETECTOR_FILE_READ_BUFFER_SIZE) {
        FwSizeType to_read = FW_MIN(Utilities::DROP_DETECTOR_FILE_READ_BUFFER_SIZE, this->m_packetSize - i);
        fileStatus = this->m_file.read(buffer, to_read);
        if (fileStatus != Os::File::OP_OK) {
            return PacketStatus::FILE_ERROR;
        }
        // Check for end of file
        if (to_read == 0) {
            return PacketStatus::FILE_EOF;
        }
        // Check for any non-zero byte in the buffer, if it exists, this is not a drop.
        for (FwSizeType j = 0; j < to_read; j++) {
            if (buffer[j] != 0) {
                return PacketStatus::GOOD;
            }
        }
    }
    return result;
}

void DropDetector ::schedIn_handler(FwIndexType portNum, U32 context) {
    if (this->m_file.isOpen()) {
        for (FwSizeType i = 0; i < Utilities::DROP_DETECTOR_CHUNKS_PER_RATE_TICK; i++) {
            PacketStatus status = PacketStatus::POSSIBLE_DROP;
            FW_ASSERT(static_cast<FwSizeType>(std::numeric_limits<FwSignedSizeType>::max()) / this->m_packetSize > this->m_index);
            Os::File::Status fileStatus = Os::File::OP_OK;
            status = this->readPacket(fileStatus);
            // Check for file errors in the seek/read
            if (fileStatus != Os::File::OP_OK) {
                // File packets are reported as a one-based index because the no-data start packet is zero
                this->log_WARNING_HI_FileReadError(this->m_index + 1, Os::FileStatus(static_cast<Os::FileStatus::T>(fileStatus)));
                this->m_file.close();
                this->m_index = 0;
                this->cmdResponse_out(this->m_opCode, this->m_cmdSeq, Fw::CmdResponse::OK);
                break;
            }
            // Report event
            if (status == PacketStatus::POSSIBLE_DROP) {
                // File packets are reported as a one-based index because the no-data start packet is zero
                this->log_ACTIVITY_HI_PossibleDrop(this->m_index + 1);
            }
            // Check for end of file and hanldle completion
            if (status == PacketStatus::FILE_EOF) {
                // Close the file and send command response
                this->log_ACTIVITY_HI_DetectingDropsCompleted();
                this->m_file.close();
                this->m_index = 0;
                this->cmdResponse_out(this->m_opCode, this->m_cmdSeq, Fw::CmdResponse::OK);
                break;
            }
            this->m_index += 1;
        }
    }
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void DropDetector ::DETECT_DROPS_cmdHandler(FwOpcodeType opCode,
                                            U32 cmdSeq,
                                            const Fw::CmdStringArg& file,
                                            FwSizeType packet_size) {
    // Check if the component is already busy                                                
    if (this->m_file.isOpen()) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
    }
    // Check for valid packet size
    else if (packet_size == 0) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
    }
    // Check if the supplied file can be opened
    else if (this->m_file.open(file.toChar(), Os::File::OPEN_READ) != Os::File::OP_OK) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
    }
    // Start the drop detection
    else {
        this->log_ACTIVITY_HI_DetectingDrops();
        this->m_packetSize = packet_size;
        this->m_index = 0;
        this->m_opCode = opCode;
        this->m_cmdSeq = cmdSeq;
    }
}

}  // namespace Utilities
