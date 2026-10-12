// ======================================================================
// \title  DeltaPatcher.cpp
// \author starchmd
// \brief  cpp file for DeltaPatcher component implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/DeltaPatcher/DeltaPatcher.hpp"

#include "Fw/Types/FileNameString.hpp"
#include "Os/FilePathUtils.hpp"
#include "Os/FileSystem.hpp"

#if defined(TGT_OS_TYPE_LINUX) || defined(TGT_OS_TYPE_DARWIN)
#include <sys/stat.h>
#define DELTA_PATCHER_HAS_FILE_IDENTITY 1
#endif

namespace Update {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

DeltaPatcher ::DeltaPatcher(const char* const compName)
    : DeltaPatcherComponentBase(compName),
      m_defaultCoder(),
      m_codec(m_defaultCoder),
      m_oldMedia(),
      m_patchMedia(),
      m_newMedia(),
      m_pending() {
    this->m_pending.opCode = 0;
    this->m_pending.cmdSeq = 0;
}

DeltaPatcher ::~DeltaPatcher() {
    this->closeMedia();
}

void DeltaPatcher ::setCoder(DeltaCoder& coder) {
    this->m_codec.setCoder(coder);
}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void DeltaPatcher ::init(FwSizeType queueDepth, FwEnumStoreType instance) {
    FW_ASSERT(queueDepth >= 2 * MAX_DISPATCH_PER_TICK, static_cast<FwAssertArgType>(queueDepth));
    DeltaPatcherComponentBase::init(queueDepth, instance);
}

void DeltaPatcher ::run_handler(FwIndexType portNum, U32 context) {
    // Commands (and signals left from the previous tick) first, so ABORT_PATCH
    // takes effect this tick and the queue has room for the tick; then the tick
    // and the stage transition its action may produce
    this->drainQueue();
    this->deltaPatchMachine_sendSignal_tick();
    this->drainQueue();
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void DeltaPatcher ::APPLY_PATCH_cmdHandler(FwOpcodeType opCode,
                                           U32 cmdSeq,
                                           const Fw::CmdStringArg& old_file,
                                           const Fw::CmdStringArg& patch_file,
                                           const Fw::CmdStringArg& new_file) {
    // Busy is "a patch is bound", not the machine state: a `start` sent by an
    // earlier APPLY_PATCH in this same drain may not have been dispatched yet, so
    // the machine can still read IDLE while a patch is already bound
    if (this->isPatchBound()) {
        this->rejectApply(opCode, cmdSeq, DeltaPatchStatus::BUSY, Fw::CmdResponse::BUSY);
        return;
    }
    // The old image is never written: refuse aliased paths before any file is
    // opened. Paths are compared after textual resolution (`.`, `..`, duplicate
    // separators, CWD) and, where the platform exposes file identity, by
    // device/inode so hard links cannot slip past. Symbolic links and special
    // files are refused by the media open.
    const PathCheck check = DeltaPatcher::checkPaths(old_file, patch_file, new_file);
    if (check != PATHS_DISTINCT) {
        const DeltaPatchStatus status =
            (check == PATHS_ALIASED) ? DeltaPatchStatus::SAME_FILE : DeltaPatchStatus::OPEN_FAILED;
        this->rejectApply(opCode, cmdSeq, status, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_codec.reset();

    const bool newExisted = Os::FileSystem::exists(new_file.toChar());
    bool opened = (this->m_oldMedia.open(old_file.toChar(), DeltaFileMedia::READ_ONLY) == Os::File::OP_OK);
    opened = opened && (this->m_patchMedia.open(patch_file.toChar(), DeltaFileMedia::READ_ONLY) == Os::File::OP_OK);
    opened = opened && (this->m_newMedia.open(new_file.toChar(), DeltaFileMedia::READ_WRITE) == Os::File::OP_OK);
    if (!opened) {
        this->closeMedia();
        this->rejectApply(opCode, cmdSeq, DeltaPatchStatus::OPEN_FAILED, Fw::CmdResponse::EXECUTION_ERROR);
        this->deltaPatchMachine_sendSignal_reject();
        return;
    }

    const DeltaCodec::Status status = this->m_codec.begin(this->m_oldMedia, this->m_patchMedia, this->m_newMedia);
    if (status != DeltaCodec::OP_OK) {
        this->closeMedia();
        this->m_codec.reset();
        if (!newExisted) {
            // A rejected header must not leave an empty output file behind
            (void)Os::FileSystem::removeFile(new_file.toChar());
        }
        this->rejectApply(opCode, cmdSeq, DeltaPatcher::toStatus(status), Fw::CmdResponse::EXECUTION_ERROR);
        this->deltaPatchMachine_sendSignal_reject();
        return;
    }

    this->m_pending.newFile = new_file;
    this->m_pending.opCode = opCode;
    this->m_pending.cmdSeq = cmdSeq;
    this->log_ACTIVITY_HI_PatchStarted(patch_file, old_file, new_file, static_cast<U32>(this->m_codec.chunkCount()));
    this->tlmWrite_ChunksTotal(static_cast<U32>(this->m_codec.chunkCount()));
    this->tlmWrite_ChunksDone(0);
    this->tlmWrite_BytesWritten(0);
    // The machine takes over; the command response is deferred until it reports
    // completion, failure or abort
    this->deltaPatchMachine_sendSignal_start();
}

void DeltaPatcher ::ABORT_PATCH_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (!this->isPatchBound()) {
        this->ignoreAbort(opCode, cmdSeq);
        return;
    }
    // Answered by reportAborted once the machine has honoured the abort, after
    // the pending APPLY_PATCH
    this->deltaPatchMachine_sendSignal_abort(DeltaAbortRequest(opCode, cmdSeq));
}

void DeltaPatcher ::APPLY_PATCH_cmdOverflowHook(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_WARNING_HI_PatchRejected(DeltaPatchStatus::BUSY);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
}

void DeltaPatcher ::ABORT_PATCH_cmdOverflowHook(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
}

// ----------------------------------------------------------------------
// Implementations for internal state machine actions
// ----------------------------------------------------------------------

void DeltaPatcher ::Update_DeltaPatchMachine_action_doVerifyNew(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    DeltaCodec::StepResult result = DeltaCodec::IN_PROGRESS;
    // Evaluate the delegate before reading `result`: argument evaluation order is
    // unspecified
    const DeltaCodec::Status status = this->m_codec.verifyNew(result);
    this->reportStep(status, result);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_doVerifyOld(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    DeltaCodec::StepResult result = DeltaCodec::IN_PROGRESS;
    // Evaluate the delegate before reading `result`: argument evaluation order is
    // unspecified
    const DeltaCodec::Status status = this->m_codec.verifyOld(result);
    this->reportStep(status, result);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_doPatchChunk(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    DeltaCodec::StepResult result = DeltaCodec::IN_PROGRESS;
    // Evaluate the delegate before reading `result`: argument evaluation order is
    // unspecified
    const DeltaCodec::Status status = this->m_codec.patchChunk(result);
    this->reportStep(status, result);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_doVerifyFinal(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    DeltaCodec::StepResult result = DeltaCodec::IN_PROGRESS;
    // Evaluate the delegate before reading `result`: argument evaluation order is
    // unspecified
    const DeltaCodec::Status status = this->m_codec.verifyFinal(result);
    this->reportStep(status, result);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_tlmVerifyNew(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    this->tlmWrite_State(DeltaPatchState::VERIFY_NEW);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_tlmVerifyOld(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    this->tlmWrite_State(DeltaPatchState::VERIFY_OLD);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_tlmPatching(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    this->tlmWrite_State(DeltaPatchState::PATCHING);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_tlmVerifyFinal(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    this->tlmWrite_State(DeltaPatchState::VERIFY_FINAL);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_tlmComplete(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    this->tlmWrite_State(DeltaPatchState::COMPLETE);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_tlmFailed(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    this->tlmWrite_State(DeltaPatchState::FAILED);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_reportResumed(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    if (this->m_codec.resumedChunks() > 0) {
        this->log_ACTIVITY_HI_PatchResumed(this->m_pending.newFile, static_cast<U32>(this->m_codec.resumedChunks()),
                                           static_cast<U32>(this->m_codec.chunkCount()));
    }
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_reportComplete(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    const U32 crc = this->m_codec.newCrc();
    this->log_ACTIVITY_HI_PatchComplete(this->m_pending.newFile, this->m_codec.newSize(), crc);
    this->completePending(DeltaPatchStatus::OP_OK, Fw::CmdResponse::OK);
    if (this->isConnected_patchComplete_OutputPort(0)) {
        this->patchComplete_out(0, this->m_pending.newFile, crc);
    }
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_reportFailed(SmId smId,
                                                                 Update_DeltaPatchMachine::Signal signal,
                                                                 const Update::DeltaPatchStatus& value) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    if (value == DeltaPatchStatus::OLD_IMAGE_MISMATCH) {
        FwSizeType actualSize = 0;
        (void)this->m_oldMedia.size(actualSize);  // size is reported as-is; 0 when the media cannot be sized
        this->log_WARNING_HI_OldImageMismatch(this->m_codec.oldCrc(), this->m_codec.actualOldCrc(),
                                              this->m_codec.oldSize(), actualSize);
    } else {
        this->log_WARNING_HI_ChunkFailed(static_cast<U32>(this->m_codec.chunkIndex()), value);
    }
    this->completePending(value, Fw::CmdResponse::EXECUTION_ERROR);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_reportAborted(SmId smId,
                                                                  Update_DeltaPatchMachine::Signal signal,
                                                                  const Update::DeltaAbortRequest& value) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    this->log_ACTIVITY_HI_PatchAborted(static_cast<U32>(this->m_codec.chunkIndex()));
    this->completePending(DeltaPatchStatus::ABORTED, Fw::CmdResponse::EXECUTION_ERROR);
    this->cmdResponse_out(value.get_opCode(), value.get_cmdSeq(), Fw::CmdResponse::OK);
    // IDLE has no entry action (the initial transition runs before ports are
    // connected), so report it here
    this->tlmWrite_State(DeltaPatchState::IDLE);
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_reportAbortIgnored(SmId smId,
                                                                       Update_DeltaPatchMachine::Signal signal,
                                                                       const Update::DeltaAbortRequest& value) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    // The patch ended (an earlier abort, a failure or completion) between command
    // dispatch and signal delivery
    this->ignoreAbort(value.get_opCode(), value.get_cmdSeq());
}

void DeltaPatcher ::deltaPatchMachine_stateMachineOverflowHook(SmId smId,
                                                               FwEnumStoreType signal,
                                                               Fw::SerialBufferBase& buffer) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    this->log_WARNING_HI_SignalDropped(static_cast<U32>(signal));
    // tick, stageDone and fail are re-derived from the codec on the next tick and
    // reject only refreshes telemetry; start and abort carry a command that must
    // still be answered
    if (signal == static_cast<FwEnumStoreType>(Update_DeltaPatchMachine::Signal::start)) {
        // The machine never saw the patch: unbind and answer the APPLY_PATCH
        // recorded in m_pending (SignalDropped(start) is the unique trace of this
        // path; LastStatus records the outcome)
        this->m_codec.reset();
        this->closeMedia();
        this->completePending(DeltaPatchStatus::BUSY, Fw::CmdResponse::BUSY);
    } else if (signal == static_cast<FwEnumStoreType>(Update_DeltaPatchMachine::Signal::abort)) {
        // The buffer was produced by the autocoded sendSignal_abort, so a
        // deserialization failure is a defect
        DeltaAbortRequest request;
        const Fw::SerializeStatus status = buffer.deserializeTo(request);
        FW_ASSERT(status == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(status));
        this->cmdResponse_out(request.get_opCode(), request.get_cmdSeq(), Fw::CmdResponse::BUSY);
    }
}

void DeltaPatcher ::Update_DeltaPatchMachine_action_doRelease(SmId smId, Update_DeltaPatchMachine::Signal signal) {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    this->m_codec.reset();
    this->closeMedia();
}

// ----------------------------------------------------------------------
// Implementations for internal state machine guards
// ----------------------------------------------------------------------

bool DeltaPatcher ::Update_DeltaPatchMachine_guard_hasOutputToVerify(SmId smId,
                                                                     Update_DeltaPatchMachine::Signal signal) const {
    FW_ASSERT(smId == SmId::deltaPatchMachine, static_cast<FwAssertArgType>(smId));
    return this->m_codec.hasOutputToVerify();
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void DeltaPatcher ::drainQueue() {
    for (FwSizeType i = 0; i < MAX_DISPATCH_PER_TICK; i++) {
        if (this->doDispatch() == MSG_DISPATCH_EMPTY) {
            break;
        }
    }
}

void DeltaPatcher ::reportStep(DeltaCodec::Status status, DeltaCodec::StepResult result) {
    this->tlmWrite_ChunksDone(static_cast<U32>(this->m_codec.chunkIndex()));
    this->tlmWrite_BytesWritten(this->m_codec.bytesWritten());
    if (status != DeltaCodec::OP_OK) {
        this->deltaPatchMachine_sendSignal_fail(DeltaPatcher::toStatus(status));
    } else if (result == DeltaCodec::STAGE_DONE) {
        this->deltaPatchMachine_sendSignal_stageDone();
    }
}

void DeltaPatcher ::rejectApply(FwOpcodeType opCode, U32 cmdSeq, DeltaPatchStatus status, Fw::CmdResponse response) {
    this->log_WARNING_HI_PatchRejected(status);
    this->tlmWrite_LastStatus(status);
    this->cmdResponse_out(opCode, cmdSeq, response);
}

void DeltaPatcher ::ignoreAbort(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_WARNING_LO_AbortIgnored(DeltaPatcher::toState(this->deltaPatchMachine_getState()));
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
}

void DeltaPatcher ::completePending(DeltaPatchStatus status, Fw::CmdResponse response) {
    this->tlmWrite_LastStatus(status);
    this->cmdResponse_out(this->m_pending.opCode, this->m_pending.cmdSeq, response);
}

void DeltaPatcher ::closeMedia() {
    this->m_oldMedia.close();
    this->m_patchMedia.close();
    this->m_newMedia.close();
}

bool DeltaPatcher ::isPatchBound() const {
    // Bound from APPLY_PATCH acceptance until doRelease: the old image is the one
    // medium the codec never closes or re-resolves mid-stage (new_file is
    // re-opened by refresh() and may be left closed on failure)
    return this->m_oldMedia.isOpen();
}

DeltaPatcher::PathCheck DeltaPatcher::checkPaths(const Fw::CmdStringArg& first,
                                                 const Fw::CmdStringArg& second,
                                                 const Fw::CmdStringArg& third) {
    const Fw::CmdStringArg* const paths[] = {&first, &second, &third};
    Fw::FileNameString resolved[3];
    for (FwSizeType i = 0; i < 3; i++) {
        char buffer[Os::FilePathUtils::MAX_PATH_LENGTH];
        const Os::FilePathUtils::Status status =
            Os::FilePathUtils::resolveFromCwd(paths[i]->toChar(), buffer, sizeof(buffer));
        if (status != Os::FilePathUtils::VALID || paths[i]->length() == 0) {
            return PATHS_INVALID;
        }
        resolved[i] = buffer;
    }
    for (FwSizeType i = 0; i < 3; i++) {
        for (FwSizeType j = i + 1; j < 3; j++) {
            if ((resolved[i] == resolved[j]) ||
                DeltaPatcher::sameIdentity(resolved[i].toChar(), resolved[j].toChar())) {
                return PATHS_ALIASED;
            }
        }
    }
    return PATHS_DISTINCT;
}

bool DeltaPatcher::sameIdentity(const char* first, const char* second) {
    FW_ASSERT(first != nullptr);
    FW_ASSERT(second != nullptr);
#if defined(DELTA_PATCHER_HAS_FILE_IDENTITY)
    struct stat firstStat;
    struct stat secondStat;
    if ((::stat(first, &firstStat) != 0) || (::stat(second, &secondStat) != 0)) {
        return false;
    }
    return (firstStat.st_dev == secondStat.st_dev) && (firstStat.st_ino == secondStat.st_ino);
#else
    return false;
#endif
}

DeltaPatchStatus DeltaPatcher ::toStatus(DeltaCodec::Status status) {
    switch (status) {
        case DeltaCodec::OP_OK:
            return DeltaPatchStatus::OP_OK;
        case DeltaCodec::BAD_HEADER:
            return DeltaPatchStatus::BAD_HEADER;
        case DeltaCodec::OLD_IMAGE_MISMATCH:
            return DeltaPatchStatus::OLD_IMAGE_MISMATCH;
        case DeltaCodec::TRUNCATED:
            return DeltaPatchStatus::TRUNCATED;
        case DeltaCodec::CHUNK_CRC:
            return DeltaPatchStatus::CHUNK_CRC;
        case DeltaCodec::BAD_OPCODE:
            return DeltaPatchStatus::BAD_OPCODE;
        case DeltaCodec::READ_ERROR:
            return DeltaPatchStatus::READ_ERROR;
        case DeltaCodec::WRITE_ERROR:
            return DeltaPatchStatus::WRITE_ERROR;
        case DeltaCodec::NEW_IMAGE_MISMATCH:
            return DeltaPatchStatus::NEW_IMAGE_MISMATCH;
        case DeltaCodec::PATCH_SIZE_MISMATCH:
            return DeltaPatchStatus::PATCH_SIZE_MISMATCH;
        case DeltaCodec::OUTPUT_STALE:
            return DeltaPatchStatus::OUTPUT_STALE;
        case DeltaCodec::CODER_MISMATCH:
            return DeltaPatchStatus::CODER_MISMATCH;
        case DeltaCodec::SIZE_WIDTH_MISMATCH:
            return DeltaPatchStatus::SIZE_WIDTH_MISMATCH;
        default:
            FW_ASSERT(0, static_cast<FwAssertArgType>(status));
            return DeltaPatchStatus::BAD_OPCODE;
    }
}

DeltaPatchState DeltaPatcher ::toState(Update_DeltaPatchMachine::State state) {
    switch (state) {
        case Update_DeltaPatchMachine::State::IDLE:
            return DeltaPatchState::IDLE;
        case Update_DeltaPatchMachine::State::VERIFY_NEW:
            return DeltaPatchState::VERIFY_NEW;
        case Update_DeltaPatchMachine::State::VERIFY_OLD:
            return DeltaPatchState::VERIFY_OLD;
        case Update_DeltaPatchMachine::State::PATCHING:
            return DeltaPatchState::PATCHING;
        case Update_DeltaPatchMachine::State::VERIFY_FINAL:
            return DeltaPatchState::VERIFY_FINAL;
        case Update_DeltaPatchMachine::State::COMPLETE:
            return DeltaPatchState::COMPLETE;
        case Update_DeltaPatchMachine::State::FAILED:
            return DeltaPatchState::FAILED;
        default:
            FW_ASSERT(0, static_cast<FwAssertArgType>(state));
            return DeltaPatchState::IDLE;
    }
}

}  // namespace Update
