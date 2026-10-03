// ======================================================================
// \title  UpdaterTester.cpp
// \author starchmd
// \brief  cpp file for Updater component test harness implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/Updater/test/ut/UpdaterTester.hpp"
#include "STest/Pick/Pick.hpp"

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

void UpdaterTester ::cmdResponseIn(FwOpcodeType opCode, U32 cmdSeq, Fw::CmdResponse response) {
    UpdaterGTestBase::cmdResponseIn(opCode, cmdSeq, response);
    if (this->probeOnResponse) {
        this->probeOnResponse = false;
        this->sendCmd_CONFIRM_UPDATE(TEST_INSTANCE_ID, 0);
        this->dispatch();
    }
}

// ----------------------------------------------------------------------
// Directed tests
// ----------------------------------------------------------------------

void UpdaterTester ::testBusyReleasedBeforeResponse(bool update) {
    const U32 cmdSeq = STest::Pick::any();
    FwOpcodeType opCode = UpdaterComponentBase::OPCODE_PREPARE_UPDATE;
    if (update) {
        opCode = UpdaterComponentBase::OPCODE_UPDATE_IMAGE_FROM;
        this->sendCmd_UPDATE_IMAGE_FROM(TEST_INSTANCE_ID, cmdSeq, Fw::String("image.bin"), 0);
    } else {
        this->sendCmd_PREPARE_UPDATE(TEST_INSTANCE_ID, cmdSeq);
    }
    this->dispatch();
    this->clearHistory();

    this->probeOnResponse = true;
    if (update) {
        this->invoke_to_updateImageDone(0, Update::UpdateStatus::OP_OK);
    } else {
        this->invoke_to_prepareImageDone(0, Update::UpdateStatus::OP_OK);
    }

    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(0, opCode, cmdSeq, Fw::CmdResponse::OK);
    ASSERT_CMD_RESPONSE(1, UpdaterComponentBase::OPCODE_CONFIRM_UPDATE, 0, Fw::CmdResponse::OK);
    ASSERT_from_confirmImage_SIZE(1);
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
