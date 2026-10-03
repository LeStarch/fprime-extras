// ======================================================================
// \title  Updater.cpp
// \author starchmd
// \brief  cpp file for Updater component implementation class
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/Updater/Updater.hpp"

namespace Update {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

Updater ::Updater(const char* const compName)
    : UpdaterComponentBase(compName), m_opCode(0), m_cmdSeq(0), m_busy(false), m_pending(PendingOperation::NONE) {}

Updater ::~Updater() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void Updater ::prepareImageDone_handler(FwIndexType portNum, const Update::UpdateStatus& status) {
    if (!this->claimPending(PendingOperation::PREPARE)) {
        this->log_WARNING_LO_UnexpectedPrepareDone(status);
        return;
    }
    if (status != Update::UpdateStatus::OP_OK) {
        this->log_WARNING_HI_PrepareUpdateFailed(status);
        this->finishPending(Fw::CmdResponse::EXECUTION_ERROR);
    } else {
        this->log_ACTIVITY_HI_PrepareUpdateSucceeded();
        this->finishPending(Fw::CmdResponse::OK);
    }
}

void Updater ::updateImageDone_handler(FwIndexType portNum, const Update::UpdateStatus& status) {
    if (!this->claimPending(PendingOperation::UPDATE)) {
        this->log_WARNING_LO_UnexpectedUpdateDone(status);
        return;
    }
    if (status != Update::UpdateStatus::OP_OK) {
        this->log_WARNING_HI_UpdateFailed(status);
        this->finishPending(Fw::CmdResponse::EXECUTION_ERROR);
    } else {
        this->log_ACTIVITY_HI_UpdateSucceeded();
        this->finishPending(Fw::CmdResponse::OK);
    }
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void Updater ::CONFIGURE_NEXT_BOOT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Update::NextBootMode& next) {
    // Read busy flag. If it is not already busy, then make it busy and move forward with the update
    bool already_busy = false;
    this->m_busy.compare_exchange_weak(already_busy, true);

    if (!already_busy) {
        Update::UpdateStatus status = this->nextBoot_out(0, next);
        if (status != Update::UpdateStatus::OP_OK) {
            this->log_WARNING_HI_SetNextBootFailed(status);
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        } else {
            this->log_ACTIVITY_HI_SetNextBoot(next);
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
        }
        this->m_busy = false;
    } else {
        this->log_WARNING_HI_SetNextBootFailed(Update::UpdateStatus::BUSY);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
    }
}

void Updater ::PREPARE_UPDATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Read busy flag. If it is not already busy, then make it busy and move forward with the action
    bool already_busy = false;
    this->m_busy.compare_exchange_weak(already_busy, true);

    if (!already_busy) {
        this->m_opCode = opCode;
        this->m_cmdSeq = cmdSeq;
        this->m_pending = PendingOperation::PREPARE;
        this->log_ACTIVITY_HI_PrepareUpdate();
        this->prepareImage_out(0);
    } else {
        this->log_WARNING_HI_PrepareUpdateFailed(Update::UpdateStatus::BUSY);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
    }
}

void Updater ::UPDATE_IMAGE_FROM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdStringArg& file, U32 crc32) {
    // Read busy flag. If it is not already busy, then make it busy and move forward with the action
    bool already_busy = false;
    this->m_busy.compare_exchange_weak(already_busy, true);

    if (!already_busy) {
        this->m_opCode = opCode;
        this->m_cmdSeq = cmdSeq;
        this->m_pending = PendingOperation::UPDATE;
        this->log_ACTIVITY_HI_Update(file);
        this->updateImage_out(0, file, crc32);
    } else {
        this->log_WARNING_HI_UpdateFailed(Update::UpdateStatus::BUSY);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
    }
}

void Updater ::CONFIRM_UPDATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Read busy flag. If it is not already busy, then make it busy and move forward with the update
    bool already_busy = false;
    this->m_busy.compare_exchange_weak(already_busy, true);

    if (!already_busy) {
        Update::UpdateStatus status = this->confirmImage_out(0);
        if (status != Update::UpdateStatus::OP_OK) {
            this->log_WARNING_HI_ConfirmBootFailed(status);
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        } else {
            this->log_ACTIVITY_HI_ConfirmBoot();
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
        }
        this->m_busy = false;
    } else {
        this->log_WARNING_HI_ConfirmBootFailed(Update::UpdateStatus::BUSY);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
    }
}

// ----------------------------------------------------------------------
// Helper functions
// ----------------------------------------------------------------------

bool Updater ::claimPending(PendingOperation operation) {
    // Strong exchange: a spurious failure would drop a legitimate completion
    PendingOperation expected = operation;
    return this->m_pending.compare_exchange_strong(expected, PendingOperation::NONE);
}

void Updater ::finishPending(Fw::CmdResponse response) {
    const FwOpcodeType opCode = this->m_opCode;
    const U32 cmdSeq = this->m_cmdSeq;
    // Release busy before responding so a command sent in reaction to the response is not rejected
    this->m_busy = false;
    this->cmdResponse_out(opCode, cmdSeq, response);
}

}  // namespace Update
