// ======================================================================
// \title  UpdaterTester.cpp
// \author starchmd
// \brief  cpp file for Updater component test harness implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/Updater/test/ut/UpdaterTester.hpp"

namespace Update {

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

UpdaterTester ::UpdaterTester()
    : UpdaterGTestBase("UpdaterTester", UpdaterTester::MAX_HISTORY_SIZE), component("Updater") {
    this->initComponents();
    this->connectPorts();
}

UpdaterTester ::~UpdaterTester() {
    this->component.deinit();
}

// ----------------------------------------------------------------------
// Test worker
// ----------------------------------------------------------------------

Update::UpdateStatus UpdaterTester ::from_nextBoot_handler(FwIndexType portNum, const Update::NextBootMode& mode) {
    this->pushFromPortEntry_nextBoot(mode);
    return this->shadow.shadow_workerReturn;
}

Update::UpdateStatus UpdaterTester ::from_confirmImage_handler(FwIndexType portNum) {
    this->pushFromPortEntry_confirmImage();
    return this->shadow.shadow_workerReturn;
}

// ----------------------------------------------------------------------
// Helper functions
// ----------------------------------------------------------------------

void UpdaterTester ::dispatch() {
    ASSERT_EQ(this->component.doDispatch(), Fw::QueuedComponentBase::MSG_DISPATCH_OK);
}

void UpdaterTester ::assertRejectedBusy(FwOpcodeType opCode, U32 cmdSeq) {
    ASSERT_FROM_PORT_HISTORY_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, opCode, cmdSeq, Fw::CmdResponse::BUSY);
}

void UpdaterTester ::assertPendingResponse(Fw::CmdResponse response) {
    ASSERT_FROM_PORT_HISTORY_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, this->shadow.shadow_pendingOpCode, this->shadow.shadow_pendingCmdSeq, response);
}

}  // namespace Update
