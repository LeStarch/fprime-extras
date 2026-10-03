// ======================================================================
// \title  PrepareUpdate.cpp
// \author starchmd
// \brief  Rule implementations for the PREPARE_UPDATE command and prepareImageDone port
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/Updater/test/ut/UpdaterTester.hpp"
#include "STest/Pick/Pick.hpp"

namespace Update {

// ----------------------------------------------------------------------
// PrepareUpdate.Start
// ----------------------------------------------------------------------

bool UpdaterTester::PrepareUpdate__Start__precondition() const {
    return !this->shadow.shadow_isBusy();
}

void UpdaterTester::PrepareUpdate__Start__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();

    this->sendCmd_PREPARE_UPDATE(TEST_INSTANCE_ID, cmdSeq);
    this->dispatch();
    this->shadow.shadow_start(UpdaterTestState::Pending::PREPARE, UpdaterComponentBase::OPCODE_PREPARE_UPDATE, cmdSeq);

    ASSERT_FROM_PORT_HISTORY_SIZE(1);
    ASSERT_from_prepareImage_SIZE(1);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_PrepareUpdate_SIZE(1);
    ASSERT_CMD_RESPONSE_SIZE(0);
}

// ----------------------------------------------------------------------
// PrepareUpdate.Busy
// ----------------------------------------------------------------------

bool UpdaterTester::PrepareUpdate__Busy__precondition() const {
    return this->shadow.shadow_isBusy();
}

void UpdaterTester::PrepareUpdate__Busy__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();

    this->sendCmd_PREPARE_UPDATE(TEST_INSTANCE_ID, cmdSeq);
    this->dispatch();

    this->assertRejectedBusy(UpdaterComponentBase::OPCODE_PREPARE_UPDATE, cmdSeq);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_PrepareUpdateFailed_SIZE(1);
    ASSERT_EVENTS_PrepareUpdateFailed(0, Update::UpdateStatus::BUSY);
}

// ----------------------------------------------------------------------
// PrepareUpdate.DoneOk
// ----------------------------------------------------------------------

bool UpdaterTester::PrepareUpdate__DoneOk__precondition() const {
    return this->shadow.shadow_pending == UpdaterTestState::Pending::PREPARE;
}

void UpdaterTester::PrepareUpdate__DoneOk__action() {
    this->clearHistory();

    this->invoke_to_prepareImageDone(0, Update::UpdateStatus::OP_OK);

    this->assertPendingResponse(Fw::CmdResponse::OK);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_PrepareUpdateSucceeded_SIZE(1);
    this->shadow.shadow_finish();
}

// ----------------------------------------------------------------------
// PrepareUpdate.DoneFailed
// ----------------------------------------------------------------------

bool UpdaterTester::PrepareUpdate__DoneFailed__precondition() const {
    return this->shadow.shadow_pending == UpdaterTestState::Pending::PREPARE;
}

void UpdaterTester::PrepareUpdate__DoneFailed__action() {
    this->clearHistory();
    const Update::UpdateStatus failure = UpdaterTestState::pickFailureStatus();

    this->invoke_to_prepareImageDone(0, failure);

    this->assertPendingResponse(Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_PrepareUpdateFailed_SIZE(1);
    ASSERT_EVENTS_PrepareUpdateFailed(0, failure);
    this->shadow.shadow_finish();
}

// ----------------------------------------------------------------------
// PrepareUpdate.UnexpectedDone
// ----------------------------------------------------------------------

bool UpdaterTester::PrepareUpdate__UnexpectedDone__precondition() const {
    return this->shadow.shadow_pending != UpdaterTestState::Pending::PREPARE;
}

void UpdaterTester::PrepareUpdate__UnexpectedDone__action() {
    this->clearHistory();
    const Update::UpdateStatus status =
        static_cast<Update::UpdateStatus::T>(STest::Pick::lowerUpper(0, Update::UpdateStatus::NUM_CONSTANTS - 1));

    this->invoke_to_prepareImageDone(0, status);

    ASSERT_FROM_PORT_HISTORY_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(0);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_UnexpectedPrepareDone_SIZE(1);
    ASSERT_EVENTS_UnexpectedPrepareDone(0, status);
}

}  // namespace Update
