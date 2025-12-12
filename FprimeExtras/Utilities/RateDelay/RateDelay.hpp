// ======================================================================
// \title  RateDelay.hpp
// \author starchmd
// \brief  hpp file for RateDelay component implementation class
// ======================================================================

#ifndef Utilities_RateDelay_HPP
#define Utilities_RateDelay_HPP

#include "FprimeExtras/Utilities/RateDelay/RateDelayComponentAc.hpp"

namespace Utilities {

class RateDelay final : public RateDelayComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct RateDelay object
    RateDelay(const char* const compName  //!< The component name
    );

    //! Destroy RateDelay object
    ~RateDelay();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for runIn
    //!
    //! Rate schedule port used to trigger divider
    void runIn_handler(FwIndexType portNum,  //!< The port number
                       U32 context           //!< The call order
                       ) override;
  private:
    // Tick count for rate division
    U8 m_tick_count = 0;
};

}  // namespace Utilities

#endif
