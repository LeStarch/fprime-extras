// ======================================================================
// \title  DeltaPatcherTester.cpp
// \author starchmd
// \brief  cpp file for DeltaPatcher component test harness implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "DeltaPatcherTester.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "FprimeExtras/Update/Delta/test/ut/DeltaTestVectors.hpp"

namespace Update {

using namespace TestVectors;

static constexpr U32 CHUNK_COUNT = (NEW_IMAGE_SIZE + CHUNK_BYTES - 1) / CHUNK_BYTES;

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

DeltaPatcherTester ::DeltaPatcherTester(DeltaCoder* coder)
    : DeltaPatcherGTestBase("DeltaPatcherTester", DeltaPatcherTester::MAX_HISTORY_SIZE),
      component("DeltaPatcher"),
      m_coderId(DeltaCoder::ID_LZSS),
      m_haveFiles(false),
      m_busy(false),
      m_applySeq(0),
      m_nextSeq(1),
      m_expectResumed(0),
      m_resumedEvents(0) {
    memset(&this->m_rc, 0, sizeof this->m_rc);
    this->initComponents();
    this->connectPorts();
    if (coder != nullptr) {
        this->component.setCoder(*coder);
        this->m_coderId = coder->id();
    }
    char dir[] = "/tmp/dpXXXXXX";
    EXPECT_NE(mkdtemp(dir), nullptr);
    this->m_dir = dir;
    this->m_old = this->m_dir + "/old.bin";
    this->m_patch = this->m_dir + "/u.spatch";
    this->m_new = this->m_dir + "/new.bin";
    writeFile(this->m_old, OLD_IMAGE, OLD_IMAGE_SIZE);
    writeFile(this->m_patch, PATCH_LZSS, PATCH_LZSS_SIZE);
}

DeltaPatcherTester ::~DeltaPatcherTester() {
    this->component.deinit();
    (void)std::remove(this->m_old.c_str());
    (void)std::remove(this->m_patch.c_str());
    (void)std::remove(this->m_new.c_str());
    (void)rmdir(this->m_dir.c_str());
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void DeltaPatcherTester ::writeFile(const std::string& path, const U8* data, FwSizeType size) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    ASSERT_TRUE(out.good());
}

std::string DeltaPatcherTester ::readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

FwSizeType DeltaPatcherTester ::readSize(const U8* data) {
    FwSizeType value = 0;
    for (FwSizeType i = 0; i < DeltaCodec::SIZE_FIELD_WIDTH; i++) {
        value = static_cast<FwSizeType>((value << 8) | data[i]);
    }
    return value;
}

void DeltaPatcherTester ::apply(U32 cmdSeq) {
    this->sendCmd_APPLY_PATCH(0, cmdSeq, Fw::CmdStringArg(this->m_old.c_str()), Fw::CmdStringArg(this->m_patch.c_str()),
                              Fw::CmdStringArg(this->m_new.c_str()));
    this->invoke_to_run(0, 0);
}

U32 DeltaPatcherTester ::runUntilResponse() {
    U32 ticks = 0;
    while (this->cmdResponseHistory->size() == 0 && ticks < MAX_TICKS) {
        this->invoke_to_run(0, 0);
        ticks++;
    }
    return ticks;
}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void DeltaPatcherTester ::nominal() {
    this->apply(1);
    ASSERT_EVENTS_PatchStarted_SIZE(1);
    ASSERT_EVENTS_PatchStarted(0, this->m_patch.c_str(), this->m_old.c_str(), this->m_new.c_str(),
                               this->component.m_codec.chunkCount());
    ASSERT_TLM_State(0, DeltaPatchState::VERIFY_OLD);
    ASSERT_TLM_ChunksTotal_SIZE(1);
    ASSERT_TLM_ChunksTotal(0, CHUNK_COUNT);
    ASSERT_TLM_ChunksDone(0, 0);
    ASSERT_CMD_RESPONSE_SIZE(0);  // deferred

    const U32 ticks = this->runUntilResponse();
    ASSERT_LT(ticks, static_cast<U32>(MAX_TICKS));
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::OK);
    ASSERT_EVENTS_PatchComplete_SIZE(1);
    ASSERT_EVENTS_PatchComplete(0, this->m_new.c_str(), NEW_IMAGE_SIZE, NEW_CRC);
    ASSERT_EVENTS_PatchResumed_SIZE(0);
    ASSERT_from_patchComplete_SIZE(1);
    ASSERT_from_patchComplete(0, Fw::String(this->m_new.c_str()), NEW_CRC);
    ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::IDLE);
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::OP_OK);
    ASSERT_TLM_BytesWritten(this->tlmHistory_BytesWritten->size() - 1, NEW_IMAGE_SIZE);
    ASSERT_TLM_ChunksDone(this->tlmHistory_ChunksDone->size() - 1, CHUNK_COUNT);
    // Every state entry is announced on both the State channel and the
    // StateEntered event: the full nominal sequence ends back in IDLE
    ASSERT_TLM_State_SIZE(4);
    ASSERT_TLM_State(0, DeltaPatchState::VERIFY_OLD);
    ASSERT_TLM_State(1, DeltaPatchState::PATCHING);
    ASSERT_TLM_State(2, DeltaPatchState::VERIFY_FINAL);
    ASSERT_TLM_State(3, DeltaPatchState::IDLE);
    ASSERT_EVENTS_StateEntered_SIZE(4);
    ASSERT_EVENTS_StateEntered(0, DeltaPatchState::VERIFY_OLD);
    ASSERT_EVENTS_StateEntered(1, DeltaPatchState::PATCHING);
    ASSERT_EVENTS_StateEntered(2, DeltaPatchState::VERIFY_FINAL);
    ASSERT_EVENTS_StateEntered(3, DeltaPatchState::IDLE);
    // The machine rests in IDLE with nothing bound
    ASSERT_FALSE(this->component.isPatchBound());

    const std::string produced = readFile(this->m_new);
    ASSERT_EQ(produced.size(), NEW_IMAGE_SIZE);
    ASSERT_EQ(0, memcmp(produced.data(), NEW_IMAGE, NEW_IMAGE_SIZE));
    // The old image is never modified
    const std::string old = readFile(this->m_old);
    ASSERT_EQ(old.size(), OLD_IMAGE_SIZE);
    ASSERT_EQ(0, memcmp(old.data(), OLD_IMAGE, OLD_IMAGE_SIZE));
}

void DeltaPatcherTester ::busy() {
    this->apply(1);
    this->apply(2);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::BUSY);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 2, Fw::CmdResponse::BUSY);
    this->clearHistory();
    this->runUntilResponse();
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::OK);
}

void DeltaPatcherTester ::openFailed() {
    (void)std::remove(this->m_patch.c_str());
    this->apply(1);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::OPEN_FAILED);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    // A rejection never reaches the machine: it stays in IDLE, nothing announced
    ASSERT_TLM_State_SIZE(0);
    ASSERT_EVENTS_StateEntered_SIZE(0);
    ASSERT_EVENTS_PatchStarted_SIZE(0);
}

void DeltaPatcherTester ::badHeader() {
    std::vector<U8> patch(PATCH_LZSS, PATCH_LZSS + PATCH_LZSS_SIZE);
    patch[0] ^= 0xFF;
    writeFile(this->m_patch, patch.data(), patch.size());
    this->apply(1);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::BAD_HEADER);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_PatchStarted_SIZE(0);
}

void DeltaPatcherTester ::coderMismatch() {
    // Valid RLE patch presented to the default LZSS coder
    writeFile(this->m_patch, PATCH_RLE, PATCH_RLE_SIZE);
    this->apply(1);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::CODER_MISMATCH);
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::CODER_MISMATCH);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_PatchStarted_SIZE(0);
}

void DeltaPatcherTester ::sizeWidthMismatch() {
    // Valid LZSS patch serialized with the other FwSizeType width
    const U8* patch = (sizeof(FwSizeType) == 8) ? PATCH_LZSS_W4 : PATCH_LZSS_W8;
    const FwSizeType size = (sizeof(FwSizeType) == 8) ? PATCH_LZSS_W4_SIZE : PATCH_LZSS_W8_SIZE;
    writeFile(this->m_patch, patch, size);
    this->apply(1);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::SIZE_WIDTH_MISMATCH);
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::SIZE_WIDTH_MISMATCH);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_PatchStarted_SIZE(0);
}

void DeltaPatcherTester ::oldImageMismatch() {
    std::vector<U8> old(OLD_IMAGE, OLD_IMAGE + OLD_IMAGE_SIZE);
    old[OLD_IMAGE_SIZE / 2] ^= 0x01;
    writeFile(this->m_old, old.data(), old.size());
    this->apply(1);
    ASSERT_EVENTS_PatchStarted_SIZE(1);
    this->runUntilResponse();
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_OldImageMismatch_SIZE(1);
    ASSERT_EQ(this->eventHistory_OldImageMismatch->at(0).expected_crc, OLD_CRC);
    ASSERT_NE(this->eventHistory_OldImageMismatch->at(0).actual_crc, OLD_CRC);
    ASSERT_EQ(this->eventHistory_OldImageMismatch->at(0).expected_size, OLD_IMAGE_SIZE);
    ASSERT_EQ(this->eventHistory_OldImageMismatch->at(0).actual_size, OLD_IMAGE_SIZE);
    ASSERT_EVENTS_PatchComplete_SIZE(0);
    ASSERT_from_patchComplete_SIZE(0);
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::OLD_IMAGE_MISMATCH);
}

void DeltaPatcherTester ::chunkFailed() {
    // Corrupt the middle of the fourth chunk's payload by walking the chunk
    // headers
    const U32 target = 3;
    std::vector<U8> patch(PATCH_LZSS, PATCH_LZSS + PATCH_LZSS_SIZE);
    FwSizeType pos = DeltaCodec::HEADER_SIZE;
    for (U32 i = 0; i < target; i++) {
        pos += DeltaCodec::CHUNK_HEADER_SIZE + readSize(&patch[pos]);
    }
    const FwSizeType codedLength = readSize(&patch[pos]);
    ASSERT_GT(codedLength, 2u);
    patch[pos + DeltaCodec::CHUNK_HEADER_SIZE + (codedLength / 2)] ^= 0xFF;
    writeFile(this->m_patch, patch.data(), patch.size());

    this->apply(1);
    ASSERT_EVENTS_PatchStarted_SIZE(1);
    this->runUntilResponse();
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_ChunkFailed_SIZE(1);
    ASSERT_EQ(this->eventHistory_ChunkFailed->at(0).chunk, target);
    // A corrupted LZSS payload may surface as a CRC miss, a malformed op or a
    // short chunk depending on the byte hit
    const DeltaPatchStatus failStatus = this->eventHistory_ChunkFailed->at(0).status;
    ASSERT_TRUE(failStatus == DeltaPatchStatus::CHUNK_CRC || failStatus == DeltaPatchStatus::BAD_OPCODE ||
                failStatus == DeltaPatchStatus::TRUNCATED);
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, failStatus);
    ASSERT_EVENTS_PatchComplete_SIZE(0);
    ASSERT_from_patchComplete_SIZE(0);
    ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::IDLE);
    ASSERT_FALSE(this->component.isPatchBound());
    ASSERT_TLM_ChunksDone(this->tlmHistory_ChunksDone->size() - 1, target);

    // Repairing the patch and re-applying resumes from the verified prefix and
    // completes
    this->clearHistory();
    writeFile(this->m_patch, PATCH_LZSS, PATCH_LZSS_SIZE);
    this->apply(2);
    this->runUntilResponse();
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 2, Fw::CmdResponse::OK);
    ASSERT_EVENTS_PatchResumed_SIZE(1);
    ASSERT_EVENTS_PatchResumed(0, this->m_new.c_str(), target, CHUNK_COUNT);
    ASSERT_EVENTS_PatchComplete_SIZE(1);
    const std::string produced = readFile(this->m_new);
    ASSERT_EQ(produced.size(), NEW_IMAGE_SIZE);
    ASSERT_EQ(0, memcmp(produced.data(), NEW_IMAGE, NEW_IMAGE_SIZE));

    // Abort after completion is ignored (the machine is back in IDLE)
    this->clearHistory();
    this->sendCmd_ABORT_PATCH(0, 2);
    this->invoke_to_run(0, 0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_ABORT_PATCH, 2, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_AbortIgnored_SIZE(1);

    // Re-applying after success goes through the resume path and back to IDLE
    this->clearHistory();
    this->apply(3);
    this->runUntilResponse();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 3, Fw::CmdResponse::OK);
    ASSERT_EVENTS_PatchComplete_SIZE(1);
    ASSERT_TLM_State(0, DeltaPatchState::VERIFY_RESUME);
    ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::IDLE);
    ASSERT_EQ(readFile(this->m_new).size(), NEW_IMAGE_SIZE);

    // A rejected re-apply after success leaves the machine in IDLE, output kept
    const U8 garbage[16] = {0};
    writeFile(this->m_patch, garbage, sizeof(garbage));
    this->clearHistory();
    this->apply(4);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::BAD_HEADER);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_TLM_State_SIZE(0);
    ASSERT_EVENTS_StateEntered_SIZE(0);
    ASSERT_EQ(this->component.deltaPatchMachine_getState(), DeltaPatcher::Update_DeltaPatchMachine::State::IDLE);
    ASSERT_EQ(readFile(this->m_new).size(), NEW_IMAGE_SIZE);
}

void DeltaPatcherTester ::abortAndResume() {
    this->apply(1);
    // apply() already ran one tick (verify-old completes in it: OLD_IMAGE_SIZE
    // fits one verify step); each further tick applies exactly one chunk
    for (U32 i = 0; i < 4; i++) {
        this->invoke_to_run(0, 0);
    }
    ASSERT_CMD_RESPONSE_SIZE(0);
    const U32 written = this->component.m_codec.chunkIndex();
    ASSERT_EQ(written, 4u);

    this->sendCmd_ABORT_PATCH(0, 2);
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PatchAborted_SIZE(1);
    ASSERT_EVENTS_PatchAborted(0, written);
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_CMD_RESPONSE(1, DeltaPatcher::OPCODE_ABORT_PATCH, 2, Fw::CmdResponse::OK);
    ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::IDLE);
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::ABORTED);
    // Further ticks do nothing
    this->clearHistory();
    this->invoke_to_run(0, 0);
    ASSERT_TLM_SIZE(0);
    ASSERT_EVENTS_SIZE(0);

    this->apply(3);
    this->runUntilResponse();
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 3, Fw::CmdResponse::OK);
    ASSERT_TLM_State_SIZE(5);
    ASSERT_TLM_State(0, DeltaPatchState::VERIFY_RESUME);
    ASSERT_TLM_State(1, DeltaPatchState::VERIFY_OLD);
    ASSERT_TLM_State(2, DeltaPatchState::PATCHING);
    ASSERT_TLM_State(3, DeltaPatchState::VERIFY_FINAL);
    ASSERT_TLM_State(4, DeltaPatchState::IDLE);
    ASSERT_EVENTS_StateEntered_SIZE(5);
    ASSERT_EVENTS_StateEntered(0, DeltaPatchState::VERIFY_RESUME);
    ASSERT_EVENTS_StateEntered(4, DeltaPatchState::IDLE);
    ASSERT_EVENTS_PatchResumed_SIZE(1);
    ASSERT_EVENTS_PatchResumed(0, this->m_new.c_str(), written, CHUNK_COUNT);
    ASSERT_EVENTS_PatchComplete_SIZE(1);
    ASSERT_from_patchComplete_SIZE(1);
    const std::string produced = readFile(this->m_new);
    ASSERT_EQ(produced.size(), NEW_IMAGE_SIZE);
    ASSERT_EQ(0, memcmp(produced.data(), NEW_IMAGE, NEW_IMAGE_SIZE));
}

void DeltaPatcherTester ::abortDuringVerify() {
    // ABORT_PATCH queued behind APPLY_PATCH: the abort signal lands in VERIFY_OLD
    this->sendCmd_APPLY_PATCH(0, 1, Fw::CmdStringArg(this->m_old.c_str()), Fw::CmdStringArg(this->m_patch.c_str()),
                              Fw::CmdStringArg(this->m_new.c_str()));
    this->sendCmd_ABORT_PATCH(0, 2);
    // A second abort in the same tick finds the patch already ended: answered,
    // not dropped
    this->sendCmd_ABORT_PATCH(0, 9);
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PatchStarted_SIZE(1);
    ASSERT_EVENTS_PatchAborted_SIZE(1);
    ASSERT_EVENTS_PatchAborted(0, 0u);
    ASSERT_EVENTS_PatchComplete_SIZE(0);
    ASSERT_EVENTS_AbortIgnored_SIZE(1);
    ASSERT_CMD_RESPONSE_SIZE(3);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_CMD_RESPONSE(1, DeltaPatcher::OPCODE_ABORT_PATCH, 2, Fw::CmdResponse::OK);
    ASSERT_CMD_RESPONSE(2, DeltaPatcher::OPCODE_ABORT_PATCH, 9, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::IDLE);
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::ABORTED);

    // Abort during final read-back: tick until VERIFY_FINAL, then abort
    this->clearHistory();
    this->apply(3);
    for (U32 i = 0; i < MAX_TICKS && this->component.deltaPatchMachine_getState() !=
                                         DeltaPatcher::Update_DeltaPatchMachine::State::VERIFY_FINAL;
         i++) {
        this->invoke_to_run(0, 0);
    }
    ASSERT_EQ(this->component.deltaPatchMachine_getState(),
              DeltaPatcher::Update_DeltaPatchMachine::State::VERIFY_FINAL);
    ASSERT_CMD_RESPONSE_SIZE(0);
    this->sendCmd_ABORT_PATCH(0, 4);
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PatchAborted_SIZE(1);
    ASSERT_EVENTS_PatchAborted(0, CHUNK_COUNT);
    ASSERT_EVENTS_PatchComplete_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 3, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_CMD_RESPONSE(1, DeltaPatcher::OPCODE_ABORT_PATCH, 4, Fw::CmdResponse::OK);
    ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::IDLE);
}

void DeltaPatcherTester ::signalOverflow() {
    // A bound patch, then ground holds the queue full of ABORT_PATCH: every
    // signal the drain emits competes with those commands for queue space. The
    // overflow hook must absorb the loss (no assert) and every command must
    // still be answered exactly once.
    this->apply(1);
    ASSERT_CMD_RESPONSE_SIZE(0);
    for (U32 seq = 2; seq <= TEST_INSTANCE_QUEUE_DEPTH + 1; seq++) {
        this->sendCmd_ABORT_PATCH(0, seq);
    }
    ASSERT_CMD_RESPONSE_SIZE(0);
    U32 ticks = 0;
    for (; ticks < MAX_TICKS && this->cmdResponseHistory->size() < TEST_INSTANCE_QUEUE_DEPTH + 1; ticks++) {
        this->invoke_to_run(0, 0);
    }
    ASSERT_LT(ticks, static_cast<U32>(MAX_TICKS));
    ASSERT_CMD_RESPONSE_SIZE(TEST_INSTANCE_QUEUE_DEPTH + 1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_CMD_RESPONSE(1, DeltaPatcher::OPCODE_ABORT_PATCH, 2, Fw::CmdResponse::OK);
    U32 ignored = 0;
    U32 busy = 0;
    for (U32 i = 2; i < this->cmdResponseHistory->size(); i++) {
        const CmdResponse& response = this->cmdResponseHistory->at(i);
        ASSERT_EQ(response.opCode, DeltaPatcher::OPCODE_ABORT_PATCH);
        if (response.response == Fw::CmdResponse::VALIDATION_ERROR) {
            ignored++;
        } else if (response.response == Fw::CmdResponse::BUSY) {
            busy++;
        } else {
            FAIL() << "unexpected abort response " << response.response;
        }
    }
    // Every abort signal fits (its command dispatch freed a slot first); only the
    // tick of the first run finds the queue full
    ASSERT_EQ(busy, 0u);
    ASSERT_EQ(ignored, TEST_INSTANCE_QUEUE_DEPTH - 1);
    ASSERT_EVENTS_PatchAborted_SIZE(1);
    ASSERT_EVENTS_AbortIgnored_SIZE(ignored);
    ASSERT_EVENTS_SignalDropped_SIZE(1);
    ASSERT_EVENTS_SignalDropped(0, static_cast<U32>(DeltaPatcher::Update_DeltaPatchMachine::Signal::tick));
    ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::IDLE);
    // Further ticks are quiet and the patcher accepts a new patch
    this->clearHistory();
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_SIZE(0);
    this->apply(20);
    this->runUntilResponse();
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 20, Fw::CmdResponse::OK);
}

void DeltaPatcherTester ::startDropped() {
    // Queue full of ABORT_PATCH when APPLY_PATCH is dispatched: the start signal
    // is dropped, so the hook must unbind the patch and answer the command
    for (U32 seq = 10; seq < 10 + TEST_INSTANCE_QUEUE_DEPTH; seq++) {
        this->sendCmd_ABORT_PATCH(0, seq);
    }
    ASSERT_CMD_RESPONSE_SIZE(0);
    this->component.APPLY_PATCH_cmdHandler(DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdStringArg(this->m_old.c_str()),
                                           Fw::CmdStringArg(this->m_patch.c_str()),
                                           Fw::CmdStringArg(this->m_new.c_str()));
    ASSERT_EVENTS_PatchStarted_SIZE(1);
    ASSERT_EVENTS_SignalDropped_SIZE(1);
    ASSERT_EVENTS_SignalDropped(0, static_cast<U32>(DeltaPatcher::Update_DeltaPatchMachine::Signal::start));
    ASSERT_EVENTS_PatchRejected_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::BUSY);
    ASSERT_FALSE(this->component.isPatchBound());
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::BUSY);
    // The queued aborts are answered as ignored, then a fresh patch runs to
    // completion
    this->clearHistory();
    for (U32 i = 0; i < 2 * TEST_INSTANCE_QUEUE_DEPTH / DeltaPatcher::MAX_DISPATCH_PER_TICK + 1; i++) {
        this->invoke_to_run(0, 0);
    }
    ASSERT_CMD_RESPONSE_SIZE(TEST_INSTANCE_QUEUE_DEPTH);
    ASSERT_EVENTS_AbortIgnored_SIZE(TEST_INSTANCE_QUEUE_DEPTH);
    this->clearHistory();
    this->apply(2);
    this->runUntilResponse();
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 2, Fw::CmdResponse::OK);
    ASSERT_EVENTS_PatchComplete_SIZE(1);
}

void DeltaPatcherTester ::hookAnswersDroppedAbort() {
    // Drive the overflow hook directly with an abort payload: the ABORT_PATCH it
    // carries must be answered BUSY from the hook
    U8 storage[DeltaAbortRequest::SERIALIZED_SIZE];
    Fw::ExternalSerializeBuffer buffer(storage, sizeof(storage));
    const DeltaAbortRequest request(DeltaPatcher::OPCODE_ABORT_PATCH, 7);
    ASSERT_EQ(buffer.serializeFrom(request), Fw::FW_SERIALIZE_OK);
    this->component.deltaPatchMachine_stateMachineOverflowHook(
        DeltaPatcher::SmId::deltaPatchMachine,
        static_cast<FwEnumStoreType>(DeltaPatcher::Update_DeltaPatchMachine::Signal::abort), buffer);
    ASSERT_EVENTS_SignalDropped_SIZE(1);
    ASSERT_EVENTS_SignalDropped(0, static_cast<U32>(DeltaPatcher::Update_DeltaPatchMachine::Signal::abort));
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_ABORT_PATCH, 7, Fw::CmdResponse::BUSY);
}

void DeltaPatcherTester ::abortOvertakenByCompletion() {
    // An ABORT_PATCH dispatched in the same drain as the tick that completes
    // read-back: the abort signal reaches the machine back in IDLE and must be
    // answered from there
    this->apply(1);
    for (U32 i = 0; i < MAX_TICKS && this->component.deltaPatchMachine_getState() !=
                                         DeltaPatcher::Update_DeltaPatchMachine::State::VERIFY_FINAL;
         i++) {
        this->invoke_to_run(0, 0);
    }
    ASSERT_EQ(this->component.deltaPatchMachine_getState(),
              DeltaPatcher::Update_DeltaPatchMachine::State::VERIFY_FINAL);
    // Eight inert APPLY_PATCH (rejected BUSY) occupy both drains of one run, so
    // its tick stays queued
    for (U32 seq = 20; seq < 20 + 2 * DeltaPatcher::MAX_DISPATCH_PER_TICK; seq++) {
        this->sendCmd_APPLY_PATCH(0, seq, Fw::CmdStringArg(this->m_old.c_str()),
                                  Fw::CmdStringArg(this->m_patch.c_str()), Fw::CmdStringArg(this->m_new.c_str()));
    }
    this->clearHistory();
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PatchRejected_SIZE(2 * DeltaPatcher::MAX_DISPATCH_PER_TICK);
    ASSERT_EVENTS_PatchComplete_SIZE(0);
    // Queue is [tick]; the abort lands behind it
    this->sendCmd_ABORT_PATCH(0, 30);
    this->clearHistory();
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PatchComplete_SIZE(1);
    ASSERT_EVENTS_AbortIgnored_SIZE(1);
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::OK);
    ASSERT_CMD_RESPONSE(1, DeltaPatcher::OPCODE_ABORT_PATCH, 30, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_FALSE(this->component.isPatchBound());
}

void DeltaPatcherTester ::abortIdle() {
    this->sendCmd_ABORT_PATCH(0, 1);
    this->invoke_to_run(0, 0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_ABORT_PATCH, 1, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_AbortIgnored_SIZE(1);
}

void DeltaPatcherTester ::queueOverflow() {
    // Fill the queue, then one more: the overflow hook must answer BUSY on the
    // caller's thread so the dispatcher never waits on a dropped command
    for (U32 seq = 1; seq <= TEST_INSTANCE_QUEUE_DEPTH; seq++) {
        this->sendCmd_ABORT_PATCH(0, seq);
    }
    ASSERT_CMD_RESPONSE_SIZE(0);
    this->sendCmd_ABORT_PATCH(0, TEST_INSTANCE_QUEUE_DEPTH + 1);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_ABORT_PATCH, TEST_INSTANCE_QUEUE_DEPTH + 1, Fw::CmdResponse::BUSY);
    this->sendCmd_APPLY_PATCH(0, TEST_INSTANCE_QUEUE_DEPTH + 2, Fw::CmdStringArg(this->m_old.c_str()),
                              Fw::CmdStringArg(this->m_patch.c_str()), Fw::CmdStringArg(this->m_new.c_str()));
    ASSERT_CMD_RESPONSE_SIZE(2);
    ASSERT_CMD_RESPONSE(1, DeltaPatcher::OPCODE_APPLY_PATCH, TEST_INSTANCE_QUEUE_DEPTH + 2, Fw::CmdResponse::BUSY);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::BUSY);
    // Queued commands are still dispatched: run drains MAX_DISPATCH_PER_TICK
    // before and after sending the state-machine tick
    this->invoke_to_run(0, 0);
    ASSERT_CMD_RESPONSE_SIZE(2 + 2 * DeltaPatcher::MAX_DISPATCH_PER_TICK);
}

void DeltaPatcherTester ::sameFileAliased() {
    const std::string alias = this->m_dir + "/./../" + this->m_dir.substr(this->m_dir.rfind('/') + 1) + "//old.bin";
    this->sendCmd_APPLY_PATCH(0, 1, Fw::CmdStringArg(this->m_old.c_str()), Fw::CmdStringArg(this->m_patch.c_str()),
                              Fw::CmdStringArg(alias.c_str()));
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::SAME_FILE);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_PatchStarted_SIZE(0);
    const std::string old = readFile(this->m_old);
    ASSERT_EQ(old.size(), OLD_IMAGE_SIZE);
    ASSERT_EQ(0, memcmp(old.data(), OLD_IMAGE, OLD_IMAGE_SIZE));
}

void DeltaPatcherTester ::sameFile() {
    // new_file aliasing old_file would destroy the only copy of the old image:
    // rejected before any open
    this->sendCmd_APPLY_PATCH(0, 1, Fw::CmdStringArg(this->m_old.c_str()), Fw::CmdStringArg(this->m_patch.c_str()),
                              Fw::CmdStringArg(this->m_old.c_str()));
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::SAME_FILE);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_PatchStarted_SIZE(0);
    ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::SAME_FILE);
    // The old image is untouched
    const std::string old = readFile(this->m_old);
    ASSERT_EQ(old.size(), OLD_IMAGE_SIZE);
    ASSERT_EQ(0, memcmp(old.data(), OLD_IMAGE, OLD_IMAGE_SIZE));
}

void DeltaPatcherTester ::sameFileHardLink() {
    const std::string link = this->m_dir + "/linked.bin";
    ASSERT_EQ(::link(this->m_old.c_str(), link.c_str()), 0);
    this->sendCmd_APPLY_PATCH(0, 1, Fw::CmdStringArg(this->m_old.c_str()), Fw::CmdStringArg(this->m_patch.c_str()),
                              Fw::CmdStringArg(link.c_str()));
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::SAME_FILE);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_PatchStarted_SIZE(0);
    const std::string old = readFile(this->m_old);
    ASSERT_EQ(old.size(), OLD_IMAGE_SIZE);
    ASSERT_EQ(0, memcmp(old.data(), OLD_IMAGE, OLD_IMAGE_SIZE));
}

void DeltaPatcherTester ::specialFiles() {
    const std::string fifo = this->m_dir + "/fifo";
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
    const std::string symlinkPath = this->m_dir + "/old-link.bin";
    ASSERT_EQ(::symlink(this->m_old.c_str(), symlinkPath.c_str()), 0);
    struct Case {
        std::string oldPath;
        std::string patchPath;
        std::string newPath;
    };
    const Case cases[] = {
        {fifo, this->m_patch, this->m_new},         // FIFO with no peer: a blocking open
                                                    // would hang the rate group
        {this->m_old, this->m_dir, this->m_new},    // directory as patch
        {this->m_old, this->m_patch, fifo},         // FIFO as output
        {symlinkPath, this->m_patch, this->m_new},  // symlink to the old image (alias not visible textually)
        {this->m_old, this->m_patch, this->m_dir},  // directory as output
    };
    U32 seq = 1;
    for (const Case& c : cases) {
        this->sendCmd_APPLY_PATCH(0, seq, Fw::CmdStringArg(c.oldPath.c_str()), Fw::CmdStringArg(c.patchPath.c_str()),
                                  Fw::CmdStringArg(c.newPath.c_str()));
        this->invoke_to_run(0, 0);
        ASSERT_EVENTS_PatchRejected_SIZE(seq);
        ASSERT_EVENTS_PatchRejected(seq - 1, DeltaPatchStatus::OPEN_FAILED);
        ASSERT_CMD_RESPONSE_SIZE(seq);
        ASSERT_CMD_RESPONSE(seq - 1, DeltaPatcher::OPCODE_APPLY_PATCH, seq, Fw::CmdResponse::EXECUTION_ERROR);
        seq++;
    }
    ASSERT_EVENTS_PatchStarted_SIZE(0);
    ASSERT_EQ(::access(this->m_new.c_str(), F_OK), -1);  // nothing created
    const std::string old = readFile(this->m_old);
    ASSERT_EQ(old.size(), OLD_IMAGE_SIZE);
    ASSERT_EQ(0, memcmp(old.data(), OLD_IMAGE, OLD_IMAGE_SIZE));
}

void DeltaPatcherTester ::badHeaderLeavesNoOutput() {
    std::vector<U8> patch(PATCH_LZSS, PATCH_LZSS + PATCH_LZSS_SIZE);
    patch[0] ^= 0xFF;
    writeFile(this->m_patch, patch.data(), patch.size());
    this->apply(1);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::BAD_HEADER);
    ASSERT_EQ(::access(this->m_new.c_str(), F_OK), -1);
    // An output that already existed is left alone
    writeFile(this->m_new, OLD_IMAGE, 10);
    this->apply(2);
    ASSERT_EVENTS_PatchRejected_SIZE(2);
    ASSERT_EQ(readFile(this->m_new).size(), 10u);
}

void DeltaPatcherTester ::outputReplacedBeforeVerify() {
    this->apply(1);
    ASSERT_EVENTS_PatchStarted_SIZE(1);
    // Tick until every chunk is written but read-back has not begun
    U32 ticks = 0;
    while (ticks < MAX_TICKS &&
           this->component.deltaPatchMachine_getState() == DeltaPatcher::Update_DeltaPatchMachine::State::PATCHING &&
           this->component.m_codec.chunkIndex() < this->component.m_codec.chunkCount()) {
        this->invoke_to_run(0, 0);
        ticks++;
    }
    ASSERT_EQ(this->component.deltaPatchMachine_getState(), DeltaPatcher::Update_DeltaPatchMachine::State::PATCHING);
    ASSERT_EQ(this->component.m_codec.chunkIndex(), this->component.m_codec.chunkCount());
    // Replace the output path with a same-sized impostor; the patcher's open
    // handle still refers to the (now orphaned) correct image, so only a by-name
    // re-resolution can catch this
    const std::string impostor = this->m_dir + "/impostor.bin";
    std::vector<U8> junk(NEW_IMAGE_SIZE, 0x5A);
    writeFile(impostor, junk.data(), junk.size());
    ASSERT_EQ(::rename(impostor.c_str(), this->m_new.c_str()), 0);
    ticks += this->runUntilResponse();
    ASSERT_LT(ticks, static_cast<U32>(MAX_TICKS));
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_PatchComplete_SIZE(0);
    ASSERT_from_patchComplete_SIZE(0);
    ASSERT_EVENTS_ChunkFailed_SIZE(1);
    ASSERT_EQ(this->eventHistory_ChunkFailed->at(0).status, DeltaPatchStatus::NEW_IMAGE_MISMATCH);
}

}  // namespace Update
