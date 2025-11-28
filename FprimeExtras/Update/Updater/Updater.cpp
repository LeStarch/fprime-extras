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
    : UpdaterComponentBase(compName), m_opCode(0), m_cmdSeq(0), m_busy(false) {}

Updater ::~Updater() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void Updater ::prepareImageDone_handler(FwIndexType portNum, const Update::UpdateStatus& status) {
    if (status != Update::UpdateStatus::OP_OK) {
        this->log_WARNING_HI_PrepareUpdateFailed(status);
        this->cmdResponse_out(this->m_opCode, this->m_cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    } else {
        this->log_ACTIVITY_HI_PrepareUpdateSucceeded();
        this->cmdResponse_out(this->m_opCode, this->m_cmdSeq, Fw::CmdResponse::OK);
    }
    this->m_busy = false;
}

void Updater ::updateImageDone_handler(FwIndexType portNum, const Update::UpdateStatus& status) {
    if (status != Update::UpdateStatus::OP_OK) {
        this->log_WARNING_HI_UpdateFailed(status);
        this->cmdResponse_out(this->m_opCode, this->m_cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    } else {
        this->log_ACTIVITY_HI_UpdateSucceeded();
        this->cmdResponse_out(this->m_opCode, this->m_cmdSeq, Fw::CmdResponse::OK);
    }
    this->m_busy = false;
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void Updater ::CONFIGURE_NEXT_BOOT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Update::NextBootMode next) {
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
        this->m_busy = true;
        this->log_ACTIVITY_HI_PrepareUpdate();
        this->prepareImage_out(0);
    } else {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
    }
}

void Updater ::UPDATE_IMAGE_FROM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdStringArg& file) {
    // Read busy flag. If it is not already busy, then make it busy and move forward with the action
    bool already_busy = false;
    this->m_busy.compare_exchange_weak(already_busy, true);

    if (!already_busy) {
        this->m_opCode = opCode;
        this->m_cmdSeq = cmdSeq;
        this->m_busy = true;
        this->log_ACTIVITY_HI_Update(file);
        this->updateImage_out(0, file);
    } else {
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
        this->log_WARNING_HI_SetNextBootFailed(Update::UpdateStatus::BUSY);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
    }
}

}  // namespace Update
