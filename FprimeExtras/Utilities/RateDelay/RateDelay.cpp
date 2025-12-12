// ======================================================================
// \title  RateDelay.cpp
// \author starchmd
// \brief  cpp file for RateDelay component implementation class
// ======================================================================

#include "FprimeExtras/Utilities/RateDelay/RateDelay.hpp"
#include "FprimeExtras/Utilities/RateDelay/FppConstantsAc.hpp"
namespace Utilities {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

RateDelay ::RateDelay(const char* const compName) : RateDelayComponentBase(compName), m_tick_count(0) {}

RateDelay ::~RateDelay() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void RateDelay ::runIn_handler(FwIndexType portNum, U32 context) {
    // On reset, output on the divided rate port
    if (this->m_tick_count == 0) {
        this->runOut_out(0, context);
    }
    // Unless there is corruption, the parameter should always be valid via its default value; however, in the interest
    // of failing-safe and continuing some sort of rate divisions.
    Fw::ParamValid is_valid;
    U8 current_divisor = this->paramGet_DIVIDER(is_valid);

    // Increment and module the tick count by the divisor
    if ((is_valid == Fw::ParamValid::INVALID) || (is_valid == Fw::ParamValid::UNINIT)) {
        current_divisor = Utilities::RateDelay_DEFAULT_DIVIDER;
    }
    // Count this new tick, resetting whenever the current count is at or higher than the current divider.
    this->m_tick_count = (this->m_tick_count >= current_divisor) ? 0 : this->m_tick_count + 1;
}

}  // namespace Utilities
