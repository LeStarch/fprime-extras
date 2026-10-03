// ======================================================================
// \title  TestState.cpp
// \author starchmd
// \brief  Shadow state model for Updater rule-based testing
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/Updater/test/ut/TestState/TestState.hpp"
#include "STest/Pick/Pick.hpp"

namespace Update {

bool UpdaterTestState::shadow_isBusy() const {
    return this->shadow_pending != Pending::NONE;
}

void UpdaterTestState::shadow_start(Pending pending, FwOpcodeType opCode, U32 cmdSeq) {
    this->shadow_pending = pending;
    this->shadow_pendingOpCode = opCode;
    this->shadow_pendingCmdSeq = cmdSeq;
}

void UpdaterTestState::shadow_finish() {
    this->shadow_pending = Pending::NONE;
}

Update::UpdateStatus UpdaterTestState::pickFailureStatus() {
    Update::UpdateStatus status = Update::UpdateStatus::OP_OK;
    while (status == Update::UpdateStatus::OP_OK) {
        status =
            static_cast<Update::UpdateStatus::T>(STest::Pick::lowerUpper(0, Update::UpdateStatus::NUM_CONSTANTS - 1));
    }
    return status;
}

Update::NextBootMode UpdaterTestState::pickBootMode() {
    return static_cast<Update::NextBootMode::T>(STest::Pick::lowerUpper(0, Update::NextBootMode::NUM_CONSTANTS - 1));
}

void UpdaterTestState::pickFileName(Fw::String& file) {
    // Command string arguments are capped at FW_CMD_STRING_MAX_SIZE regardless of the FPP declared size
    char buffer[FW_CMD_STRING_MAX_SIZE];
    const U32 length = STest::Pick::lowerUpper(1, FW_CMD_STRING_MAX_SIZE - 1);
    for (U32 i = 0; i < length; i++) {
        buffer[i] = static_cast<char>(STest::Pick::lowerUpper('!', '~'));
    }
    buffer[length] = '\0';
    file = buffer;
}

}  // namespace Update
