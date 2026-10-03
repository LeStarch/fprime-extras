// ======================================================================
// \title  UpdateImage.cpp
// \author starchmd
// \brief  Rule implementations for the UPDATE_IMAGE_FROM command and updateImageDone port
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/Updater/test/ut/UpdaterTester.hpp"
#include "STest/Pick/Pick.hpp"

namespace Update {

// ----------------------------------------------------------------------
// UpdateImage.Start
// ----------------------------------------------------------------------

bool UpdaterTester::UpdateImage__Start__precondition() const {
    return !this->shadow.shadow_isBusy();
}

void UpdaterTester::UpdateImage__Start__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();
    const U32 crc32 = STest::Pick::any();
    Fw::String file;
    UpdaterTestState::pickFileName(file);

    this->sendCmd_UPDATE_IMAGE_FROM(TEST_INSTANCE_ID, cmdSeq, file, crc32);
    this->dispatch();
    this->shadow.shadow_start(UpdaterTestState::Pending::UPDATE, UpdaterComponentBase::OPCODE_UPDATE_IMAGE_FROM,
                              cmdSeq);

    ASSERT_FROM_PORT_HISTORY_SIZE(1);
    ASSERT_from_updateImage_SIZE(1);
    ASSERT_from_updateImage(0, file, crc32);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_Update_SIZE(1);
    ASSERT_EVENTS_Update(0, file.toChar());
    ASSERT_CMD_RESPONSE_SIZE(0);
}

// ----------------------------------------------------------------------
// UpdateImage.Busy
// ----------------------------------------------------------------------

bool UpdaterTester::UpdateImage__Busy__precondition() const {
    return this->shadow.shadow_isBusy();
}

void UpdaterTester::UpdateImage__Busy__action() {
    this->clearHistory();
    const U32 cmdSeq = STest::Pick::any();
    Fw::String file;
    UpdaterTestState::pickFileName(file);

    this->sendCmd_UPDATE_IMAGE_FROM(TEST_INSTANCE_ID, cmdSeq, file, STest::Pick::any());
    this->dispatch();

    this->assertRejectedBusy(UpdaterComponentBase::OPCODE_UPDATE_IMAGE_FROM, cmdSeq);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_UpdateFailed_SIZE(1);
    ASSERT_EVENTS_UpdateFailed(0, Update::UpdateStatus::BUSY);
}

// ----------------------------------------------------------------------
// UpdateImage.DoneOk
// ----------------------------------------------------------------------

bool UpdaterTester::UpdateImage__DoneOk__precondition() const {
    return this->shadow.shadow_pending == UpdaterTestState::Pending::UPDATE;
}

void UpdaterTester::UpdateImage__DoneOk__action() {
    this->clearHistory();

    this->invoke_to_updateImageDone(0, Update::UpdateStatus::OP_OK);

    this->assertPendingResponse(Fw::CmdResponse::OK);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_UpdateSucceeded_SIZE(1);
    this->shadow.shadow_finish();
}

// ----------------------------------------------------------------------
// UpdateImage.DoneFailed
// ----------------------------------------------------------------------

bool UpdaterTester::UpdateImage__DoneFailed__precondition() const {
    return this->shadow.shadow_pending == UpdaterTestState::Pending::UPDATE;
}

void UpdaterTester::UpdateImage__DoneFailed__action() {
    this->clearHistory();
    const Update::UpdateStatus failure = UpdaterTestState::pickFailureStatus();

    this->invoke_to_updateImageDone(0, failure);

    this->assertPendingResponse(Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_UpdateFailed_SIZE(1);
    ASSERT_EVENTS_UpdateFailed(0, failure);
    this->shadow.shadow_finish();
}

// ----------------------------------------------------------------------
// UpdateImage.UnexpectedDone
// ----------------------------------------------------------------------

bool UpdaterTester::UpdateImage__UnexpectedDone__precondition() const {
    return this->shadow.shadow_pending != UpdaterTestState::Pending::UPDATE;
}

void UpdaterTester::UpdateImage__UnexpectedDone__action() {
    this->clearHistory();
    const Update::UpdateStatus status =
        static_cast<Update::UpdateStatus::T>(STest::Pick::lowerUpper(0, Update::UpdateStatus::NUM_CONSTANTS - 1));

    this->invoke_to_updateImageDone(0, status);

    ASSERT_FROM_PORT_HISTORY_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(0);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_UnexpectedUpdateDone_SIZE(1);
    ASSERT_EVENTS_UnexpectedUpdateDone(0, status);
}

}  // namespace Update
