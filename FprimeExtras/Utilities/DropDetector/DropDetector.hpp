// ======================================================================
// \title  DropDetector.hpp
// \author starchmd
// \brief  hpp file for DropDetector component implementation class
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================

#ifndef Utilities_DropDetector_HPP
#define Utilities_DropDetector_HPP

#include "FprimeExtras/Utilities/DropDetector/DropDetectorComponentAc.hpp"
#include "Os/File.hpp"


namespace Utilities {

class DropDetector final : public DropDetectorComponentBase {
  public:
    //! \brief Packet status enumeration for read packet function
    enum PacketStatus {
        GOOD, //!< No drop detected
        POSSIBLE_DROP, //!< Possible drop detected (all zeros)
        FILE_ERROR, //!< File read error occurred, check i/o parameter
        FILE_EOF //!< End of file reached
    };
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct DropDetector object
    DropDetector(const char* const compName  //!< The component name
    );

    //! Destroy DropDetector object
    ~DropDetector();

    //! Read a packet from the file and determine if it is a drop
    PacketStatus readPacket(Os::File::Status& fileStatus); 

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for schedIn
    //!
    //! Scheduler input port for rate group operations
    void schedIn_handler(FwIndexType portNum,  //!< The port number
                         U32 context           //!< The call order
                         ) override;

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command DETECT_DROPS
    //!
    //! Search the specified file for sequences of zeros of length packet_size and report their
    //! one-based indices via events.
    void DETECT_DROPS_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                 U32 cmdSeq,           //!< The command sequence number
                                 const Fw::CmdStringArg& file,
                                 FwSizeType packet_size) override;
    Os::File m_file;
    FwSizeType m_packetSize;
    FwSizeType m_index;

    FwOpcodeType m_opCode;
    U32 m_cmdSeq;

};

}  // namespace Utilities

#endif
