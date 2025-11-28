// ======================================================================
// \title  StubWorker.cpp
// \author starchmd
// \brief  cpp file for StubWorker component implementation class
// ======================================================================

#include "FprimeExtras/Update/StubWorker/StubWorker.hpp"

namespace Update {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

StubWorker ::StubWorker(const char* const compName) : StubWorkerComponentBase(compName) {}

StubWorker ::~StubWorker() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

Update::UpdateStatus StubWorker ::confirmImage_handler(FwIndexType portNum) {
    FW_ASSERT(0);  // Not implemented
    return Update::UpdateStatus::NOT_IMPLEMENTED;
}

Update::UpdateStatus StubWorker ::nextBoot_handler(FwIndexType portNum, const Update::NextBootMode& mode) {
    FW_ASSERT(0);  // Not implemented
    return Update::UpdateStatus::NOT_IMPLEMENTED;
}

void StubWorker ::prepareImage_handler(FwIndexType portNum) {
    FW_ASSERT(0);  // Not implemented
}

void StubWorker ::updateImage_handler(FwIndexType portNum, const Fw::StringBase& file) {
    FW_ASSERT(0);  // Not implemented
}

}  // namespace Update
