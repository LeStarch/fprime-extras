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
      m_newFile(),
      m_opCode(0),
      m_cmdSeq(0),
      m_state(DeltaPatchState::IDLE),
      m_resumeReported(false) {}

DeltaPatcher ::~DeltaPatcher() {
    this->closeMedia();
}

void DeltaPatcher ::setCoder(DeltaCoder& coder) {
    this->m_codec.setCoder(coder);
}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void DeltaPatcher ::run_handler(FwIndexType portNum, U32 context) {
    // Drain the command queue (bounded) before doing patch work so ABORT_PATCH takes effect this tick
    for (FwSizeType i = 0; i < MAX_DISPATCH_PER_TICK; i++) {
        if (this->doDispatch() == MSG_DISPATCH_EMPTY) {
            break;
        }
    }
    if (this->m_state == DeltaPatchState::PATCHING) {
        this->stepPatch();
    }
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

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
    struct stat firstStat = {};
    struct stat secondStat = {};
    if ((::stat(first, &firstStat) != 0) || (::stat(second, &secondStat) != 0)) {
        return false;
    }
    return (firstStat.st_dev == secondStat.st_dev) && (firstStat.st_ino == secondStat.st_ino);
#else
    return false;
#endif
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void DeltaPatcher ::APPLY_PATCH_cmdHandler(FwOpcodeType opCode,
                                           U32 cmdSeq,
                                           const Fw::CmdStringArg& old_file,
                                           const Fw::CmdStringArg& patch_file,
                                           const Fw::CmdStringArg& new_file) {
    if (this->m_state == DeltaPatchState::PATCHING) {
        this->log_WARNING_HI_PatchRejected(DeltaPatchStatus::BUSY);
        this->tlmWrite_LastStatus(DeltaPatchStatus::BUSY);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
        return;
    }
    // The old image is never written: refuse aliased paths before any file is
    // opened. Paths are compared after textual resolution (`.`, `..`, duplicate
    // separators, CWD) and, where the platform exposes file identity, by
    // device/inode so hard links cannot slip past. Symbolic links and special files are refused by the media open.
    const PathCheck check = DeltaPatcher::checkPaths(old_file, patch_file, new_file);
    if (check != PATHS_DISTINCT) {
        const DeltaPatchStatus status =
            (check == PATHS_ALIASED) ? DeltaPatchStatus::SAME_FILE : DeltaPatchStatus::BAD_PATH;
        this->log_WARNING_HI_PatchRejected(status);
        this->tlmWrite_LastStatus(status);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->closeMedia();
    this->m_codec.reset();

    const bool newExisted = Os::FileSystem::exists(new_file.toChar());
    bool opened = (this->m_oldMedia.open(old_file.toChar(), DeltaFileMedia::READ_ONLY) == Os::File::OP_OK);
    opened = opened && (this->m_patchMedia.open(patch_file.toChar(), DeltaFileMedia::READ_ONLY) == Os::File::OP_OK);
    opened = opened && (this->m_newMedia.open(new_file.toChar(), DeltaFileMedia::READ_WRITE) == Os::File::OP_OK);
    if (!opened) {
        this->closeMedia();
        this->log_WARNING_HI_PatchRejected(DeltaPatchStatus::OPEN_FAILED);
        this->tlmWrite_LastStatus(DeltaPatchStatus::OPEN_FAILED);
        this->m_state = DeltaPatchState::FAILED;
        this->tlmWrite_State(this->m_state);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    const DeltaCodec::Status status = this->m_codec.begin(this->m_oldMedia, this->m_patchMedia, this->m_newMedia);
    if (status != DeltaCodec::OP_OK) {
        this->closeMedia();
        this->m_codec.reset();
        if (!newExisted) {
            // A rejected header must not leave an empty output file behind
            if (Os::FileSystem::removeFile(new_file.toChar()) != Os::FileSystem::OP_OK) {
                this->log_WARNING_LO_OutputRemoveFailed(new_file);
            }
        }
        this->log_WARNING_HI_PatchRejected(DeltaPatcher::toStatus(status));
        this->tlmWrite_LastStatus(DeltaPatcher::toStatus(status));
        this->m_state = DeltaPatchState::FAILED;
        this->tlmWrite_State(this->m_state);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    this->m_newFile = new_file;
    this->m_opCode = opCode;
    this->m_cmdSeq = cmdSeq;
    this->m_resumeReported = false;
    this->m_state = DeltaPatchState::PATCHING;
    this->log_ACTIVITY_HI_PatchStarted(patch_file, old_file, new_file, static_cast<U32>(this->m_codec.chunkCount()));
    this->tlmWrite_State(this->m_state);
    this->tlmWrite_ChunksTotal(static_cast<U32>(this->m_codec.chunkCount()));
    this->tlmWrite_ChunksDone(0);
    this->tlmWrite_BytesWritten(0);
    // Command response is deferred until the patch completes or fails
}

void DeltaPatcher ::ABORT_PATCH_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->m_state != DeltaPatchState::PATCHING) {
        this->log_WARNING_LO_AbortIgnored(this->m_state);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->log_ACTIVITY_HI_PatchAborted(static_cast<U32>(this->m_codec.chunkIndex()));
    this->finish(DeltaPatchState::IDLE, DeltaPatchStatus::ABORTED, Fw::CmdResponse::EXECUTION_ERROR);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void DeltaPatcher ::APPLY_PATCH_cmdOverflowHook(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_WARNING_HI_PatchRejected(DeltaPatchStatus::BUSY);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
}

void DeltaPatcher ::ABORT_PATCH_cmdOverflowHook(FwOpcodeType opCode, U32 cmdSeq) {
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void DeltaPatcher ::stepPatch() {
    const DeltaCodec::Status status = this->m_codec.step();
    this->tlmWrite_ChunksDone(static_cast<U32>(this->m_codec.chunkIndex()));
    this->tlmWrite_BytesWritten(this->m_codec.bytesWritten());

    switch (this->m_codec.state()) {
        case DeltaCodec::PATCHING:
            if (!this->m_resumeReported) {
                this->m_resumeReported = true;
                if (this->m_codec.resumedChunks() > 0) {
                    this->log_ACTIVITY_HI_PatchResumed(this->m_newFile, static_cast<U32>(this->m_codec.resumedChunks()),
                                                       static_cast<U32>(this->m_codec.chunkCount()));
                }
            }
            break;
        case DeltaCodec::COMPLETE: {
            const U32 crc = this->m_codec.newCrc();
            this->log_ACTIVITY_HI_PatchComplete(this->m_newFile, this->m_codec.newSize(), crc);
            this->finish(DeltaPatchState::COMPLETE, DeltaPatchStatus::OP_OK, Fw::CmdResponse::OK);
            if (this->isConnected_patchComplete_OutputPort(0)) {
                this->patchComplete_out(0, this->m_newFile, crc);
            }
            break;
        }
        case DeltaCodec::FAILED: {
            const DeltaPatchStatus reported = DeltaPatcher::toStatus(status);
            if (status == DeltaCodec::OLD_IMAGE_MISMATCH) {
                FwSizeType actualSize = 0;
                (void)this->m_oldMedia.size(actualSize);
                this->log_WARNING_HI_OldImageMismatch(this->m_codec.oldCrc(), this->m_codec.actualOldCrc(),
                                                      this->m_codec.oldSize(), actualSize);
            } else {
                this->log_WARNING_HI_ChunkFailed(static_cast<U32>(this->m_codec.chunkIndex()), reported);
            }
            this->finish(DeltaPatchState::FAILED, reported, Fw::CmdResponse::EXECUTION_ERROR);
            break;
        }
        case DeltaCodec::IDLE:
        case DeltaCodec::VERIFY_NEW:
        case DeltaCodec::VERIFY_OLD:
        case DeltaCodec::VERIFY_FINAL:
        default:
            break;
    }
}

void DeltaPatcher ::finish(DeltaPatchState state, DeltaPatchStatus status, Fw::CmdResponse response) {
    this->m_codec.reset();
    this->closeMedia();
    this->m_state = state;
    this->tlmWrite_State(this->m_state);
    this->tlmWrite_LastStatus(status);
    this->cmdResponse_out(this->m_opCode, this->m_cmdSeq, response);
}

void DeltaPatcher ::closeMedia() {
    this->m_oldMedia.close();
    this->m_patchMedia.close();
    this->m_newMedia.close();
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

}  // namespace Update
