// ======================================================================
// \title  ConfirmUpdate.cpp
// \author starchmd
// \brief  Rule implementations for the CONFIRM_UPDATE command
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/Updater/test/ut/UpdaterTester.hpp"
#include "STest/Pick/Pick.hpp"

namespace Update {

// ----------------------------------------------------------------------
// ConfirmUpdate.Ok
// ----------------------------------------------------------------------

bool UpdaterTester::ConfirmUpdate__Ok__precondition() const {
    return !this->shadow.shadow_isBusy();
}

void UpdaterTester::ConfirmUpdate__Ok__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();
    this->shadow.shadow_workerReturn = Update::UpdateStatus::OP_OK;

    this->sendCmd_CONFIRM_UPDATE(TEST_INSTANCE_ID, cmdSeq);
    this->dispatch();

    ASSERT_FROM_PORT_HISTORY_SIZE(1);
    ASSERT_from_confirmImage_SIZE(1);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_ConfirmBoot_SIZE(1);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, UpdaterComponentBase::OPCODE_CONFIRM_UPDATE, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// ConfirmUpdate.Failed
// ----------------------------------------------------------------------

bool UpdaterTester::ConfirmUpdate__Failed__precondition() const {
    return !this->shadow.shadow_isBusy();
}

void UpdaterTester::ConfirmUpdate__Failed__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();
    const Update::UpdateStatus failure = UpdaterTestState::pickFailureStatus();
    this->shadow.shadow_workerReturn = failure;

    this->sendCmd_CONFIRM_UPDATE(TEST_INSTANCE_ID, cmdSeq);
    this->dispatch();

    ASSERT_FROM_PORT_HISTORY_SIZE(1);
    ASSERT_from_confirmImage_SIZE(1);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_ConfirmBootFailed_SIZE(1);
    ASSERT_EVENTS_ConfirmBootFailed(0, failure);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, UpdaterComponentBase::OPCODE_CONFIRM_UPDATE, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
}

// ----------------------------------------------------------------------
// ConfirmUpdate.Busy
// ----------------------------------------------------------------------

bool UpdaterTester::ConfirmUpdate__Busy__precondition() const {
    return this->shadow.shadow_isBusy();
}

void UpdaterTester::ConfirmUpdate__Busy__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();

    this->sendCmd_CONFIRM_UPDATE(TEST_INSTANCE_ID, cmdSeq);
    this->dispatch();

    this->assertRejectedBusy(UpdaterComponentBase::OPCODE_CONFIRM_UPDATE, cmdSeq);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_ConfirmBootFailed_SIZE(1);
    ASSERT_EVENTS_ConfirmBootFailed(0, Update::UpdateStatus::BUSY);
}

}  // namespace Update
