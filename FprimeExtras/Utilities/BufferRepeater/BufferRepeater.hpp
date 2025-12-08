// ======================================================================
// \title  BufferRepeater.hpp
// \author starchmd
// \brief  hpp file for BufferRepeater component implementation class
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================

#ifndef Utilities_BufferRepeater_HPP
#define Utilities_BufferRepeater_HPP

#include "ExtrasConfig/FppConstantsAc.hpp"
#include "FprimeExtras/Utilities/BufferRepeater/BufferRepeaterComponentAc.hpp"
#include "Fw/DataStructures/RedBlackTreeMap.hpp"
#include "Os/Mutex.hpp"

namespace Utilities {

class BufferRepeater final : public BufferRepeaterComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct BufferRepeater object
    BufferRepeater(const char* const compName  //!< The component name
    );

    //! Destroy BufferRepeater object
    ~BufferRepeater();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for multiIn
    //!
    //! Incoming buffers on the multi-port pair side. Used to return buffers when `singleOut` is the source of the
    //! original buffer. Otherwise, these collect incoming buffers.
    void multiIn_handler(FwIndexType portNum,  //!< The port number
                         Fw::Buffer& fwBuffer  //!< The buffer
                         ) override;

    //! Handler implementation for singleIn
    //!
    //! Incoming buffer on the single port pair side. This collects the returned buffer when `multiOut` is the source
    //! of the original buffer. Otherwise, this receives the buffer to be forwarded to the multi ports.
    void singleIn_handler(FwIndexType portNum,  //!< The port number
                          Fw::Buffer& fwBuffer  //!< The buffer
                          ) override;

  private:
    Fw::RedBlackTreeMap<U8*, FwIndexType, Utilities::BUFFER_FANOUT_MAX_BUFFERS_IN_FLIGHT> m_bufferToCount;
    Os::Mutex m_mapLock;
};

}  // namespace Utilities

#endif
