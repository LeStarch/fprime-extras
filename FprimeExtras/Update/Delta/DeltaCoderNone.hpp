// ======================================================================
// \title  DeltaCoderNone.hpp
// \author starchmd
// \brief  hpp file for the pass-through SPatch payload coder
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaCoderNone_HPP
#define Update_Delta_DeltaCoderNone_HPP

#include "FprimeExtras/Update/Delta/DeltaCoder.hpp"

namespace Update {

//! \brief Pass-through coder (id 0): the chunk payload is the operation stream
class DeltaCoderNone final : public DeltaCoder {
  public:
    U8 id() const override { return ID_NONE; }
    void reset() override {}
    Status decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaWindow& window) override;
    bool pending() const override { return false; }
};

}  // namespace Update
#endif
