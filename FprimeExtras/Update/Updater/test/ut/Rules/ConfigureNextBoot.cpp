// ======================================================================
// \title  ConfigureNextBoot.cpp
// \author starchmd
// \brief  Rule implementations for the CONFIGURE_NEXT_BOOT command
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/Updater/test/ut/UpdaterTester.hpp"
#include "STest/Pick/Pick.hpp"

namespace Update {

// ----------------------------------------------------------------------
// ConfigureNextBoot.Ok
// ----------------------------------------------------------------------

bool UpdaterTester::ConfigureNextBoot__Ok__precondition() const {
    return !this->shadow.shadow_isBusy();
}

void UpdaterTester::ConfigureNextBoot__Ok__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();
    const Update::NextBootMode mode = UpdaterTestState::pickBootMode();
    this->shadow.shadow_workerReturn = Update::UpdateStatus::OP_OK;

    this->sendCmd_CONFIGURE_NEXT_BOOT(TEST_INSTANCE_ID, cmdSeq, mode);
    this->dispatch();

    ASSERT_FROM_PORT_HISTORY_SIZE(1);
    ASSERT_from_nextBoot_SIZE(1);
    ASSERT_from_nextBoot(0, mode);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_SetNextBoot_SIZE(1);
    ASSERT_EVENTS_SetNextBoot(0, mode);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, UpdaterComponentBase::OPCODE_CONFIGURE_NEXT_BOOT, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// ConfigureNextBoot.Failed
// ----------------------------------------------------------------------

bool UpdaterTester::ConfigureNextBoot__Failed__precondition() const {
    return !this->shadow.shadow_isBusy();
}

void UpdaterTester::ConfigureNextBoot__Failed__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();
    const Update::NextBootMode mode = UpdaterTestState::pickBootMode();
    const Update::UpdateStatus failure = UpdaterTestState::pickFailureStatus();
    this->shadow.shadow_workerReturn = failure;

    this->sendCmd_CONFIGURE_NEXT_BOOT(TEST_INSTANCE_ID, cmdSeq, mode);
    this->dispatch();

    ASSERT_FROM_PORT_HISTORY_SIZE(1);
    ASSERT_from_nextBoot_SIZE(1);
    ASSERT_from_nextBoot(0, mode);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_SetNextBootFailed_SIZE(1);
    ASSERT_EVENTS_SetNextBootFailed(0, failure);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, UpdaterComponentBase::OPCODE_CONFIGURE_NEXT_BOOT, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
}

// ----------------------------------------------------------------------
// ConfigureNextBoot.Busy
// ----------------------------------------------------------------------

bool UpdaterTester::ConfigureNextBoot__Busy__precondition() const {
    return this->shadow.shadow_isBusy();
}

void UpdaterTester::ConfigureNextBoot__Busy__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();

    this->sendCmd_CONFIGURE_NEXT_BOOT(TEST_INSTANCE_ID, cmdSeq, UpdaterTestState::pickBootMode());
    this->dispatch();

    this->assertRejectedBusy(UpdaterComponentBase::OPCODE_CONFIGURE_NEXT_BOOT, cmdSeq);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_SetNextBootFailed_SIZE(1);
    ASSERT_EVENTS_SetNextBootFailed(0, Update::UpdateStatus::BUSY);
}

}  // namespace Update
