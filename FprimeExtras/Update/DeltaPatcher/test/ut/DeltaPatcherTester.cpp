// ======================================================================
// \title  DeltaPatcherTester.cpp
// \author starchmd
// \brief  cpp file for DeltaPatcher component test harness implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "DeltaPatcherTester.hpp"

#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "FprimeExtras/Update/Delta/test/ut/DeltaTestVectors.hpp"

namespace Update {

using namespace TestVectors;

static constexpr U32 CHUNK_COUNT =
    (NEW_IMAGE_SIZE + CHUNK_BYTES - 1) / CHUNK_BYTES;

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

DeltaPatcherTester ::DeltaPatcherTester()
    : DeltaPatcherGTestBase("DeltaPatcherTester",
                            DeltaPatcherTester::MAX_HISTORY_SIZE),
      component("DeltaPatcher") {
  this->initComponents();
  this->connectPorts();
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

void DeltaPatcherTester ::writeFile(const std::string &path, const U8 *data,
                                    FwSizeType size) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(data),
            static_cast<std::streamsize>(size));
  ASSERT_TRUE(out.good());
}

std::string DeltaPatcherTester ::readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

U32 DeltaPatcherTester ::readLe32(const U8 *data) {
  return static_cast<U32>(data[0]) | (static_cast<U32>(data[1]) << 8) |
         (static_cast<U32>(data[2]) << 16) | (static_cast<U32>(data[3]) << 24);
}

void DeltaPatcherTester ::apply(U32 cmdSeq) {
  this->sendCmd_APPLY_PATCH(0, cmdSeq, Fw::CmdStringArg(this->m_old.c_str()),
                            Fw::CmdStringArg(this->m_patch.c_str()),
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
  ASSERT_EVENTS_PatchStarted(0, this->m_patch.c_str(), this->m_old.c_str(),
                             this->m_new.c_str(),
                             this->component.m_codec.chunkCount());
  ASSERT_TLM_State(0, DeltaPatchState::PATCHING);
  ASSERT_TLM_ChunksTotal_SIZE(1);
  ASSERT_TLM_ChunksTotal(0, CHUNK_COUNT);
  ASSERT_TLM_ChunksDone(0, 0);
  ASSERT_CMD_RESPONSE_SIZE(0); // deferred

  const U32 ticks = this->runUntilResponse();
  ASSERT_LT(ticks, static_cast<U32>(MAX_TICKS));
  ASSERT_CMD_RESPONSE_SIZE(1);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::OK);
  ASSERT_EVENTS_PatchComplete_SIZE(1);
  ASSERT_EVENTS_PatchComplete(0, this->m_new.c_str(), NEW_IMAGE_SIZE, NEW_CRC);
  ASSERT_EVENTS_PatchResumed_SIZE(0);
  ASSERT_from_patchComplete_SIZE(1);
  ASSERT_from_patchComplete(0, Fw::String(this->m_new.c_str()), NEW_CRC);
  ASSERT_TLM_State(this->tlmHistory_State->size() - 1,
                   DeltaPatchState::COMPLETE);
  ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1,
                        DeltaPatchStatus::OP_OK);
  ASSERT_TLM_BytesWritten(this->tlmHistory_BytesWritten->size() - 1,
                          NEW_IMAGE_SIZE);
  ASSERT_TLM_ChunksDone(this->tlmHistory_ChunksDone->size() - 1, CHUNK_COUNT);

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
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 2,
                      Fw::CmdResponse::BUSY);
  this->clearHistory();
  this->runUntilResponse();
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::OK);
}

void DeltaPatcherTester ::openFailed() {
  (void)std::remove(this->m_patch.c_str());
  this->apply(1);
  ASSERT_EVENTS_PatchRejected_SIZE(1);
  ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::OPEN_FAILED);
  ASSERT_CMD_RESPONSE_SIZE(1);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::EXECUTION_ERROR);
  ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::FAILED);
  ASSERT_EVENTS_PatchStarted_SIZE(0);
}

void DeltaPatcherTester ::badHeader() {
  std::vector<U8> patch(PATCH_LZSS, PATCH_LZSS + PATCH_LZSS_SIZE);
  patch[0] ^= 0xFF;
  writeFile(this->m_patch, patch.data(), patch.size());
  this->apply(1);
  ASSERT_EVENTS_PatchRejected_SIZE(1);
  ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::BAD_HEADER);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::EXECUTION_ERROR);
  ASSERT_EVENTS_PatchStarted_SIZE(0);
}

void DeltaPatcherTester ::oldImageMismatch() {
  std::vector<U8> old(OLD_IMAGE, OLD_IMAGE + OLD_IMAGE_SIZE);
  old[OLD_IMAGE_SIZE / 2] ^= 0x01;
  writeFile(this->m_old, old.data(), old.size());
  this->apply(1);
  ASSERT_EVENTS_PatchStarted_SIZE(1);
  this->runUntilResponse();
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::EXECUTION_ERROR);
  ASSERT_EVENTS_OldImageMismatch_SIZE(1);
  ASSERT_EQ(this->eventHistory_OldImageMismatch->at(0).expected_crc, OLD_CRC);
  ASSERT_NE(this->eventHistory_OldImageMismatch->at(0).actual_crc, OLD_CRC);
  ASSERT_EQ(this->eventHistory_OldImageMismatch->at(0).expected_size,
            OLD_IMAGE_SIZE);
  ASSERT_EQ(this->eventHistory_OldImageMismatch->at(0).actual_size,
            OLD_IMAGE_SIZE);
  ASSERT_EVENTS_PatchComplete_SIZE(0);
  ASSERT_from_patchComplete_SIZE(0);
  ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1,
                        DeltaPatchStatus::OLD_IMAGE_MISMATCH);
}

void DeltaPatcherTester ::chunkFailed() {
  // Corrupt the middle of the fourth chunk's payload by walking the chunk
  // headers
  const U32 target = 3;
  std::vector<U8> patch(PATCH_LZSS, PATCH_LZSS + PATCH_LZSS_SIZE);
  FwSizeType pos = DeltaCodec::HEADER_SIZE;
  for (U32 i = 0; i < target; i++) {
    pos += DeltaCodec::CHUNK_HEADER_SIZE + readLe32(&patch[pos]);
  }
  const U32 codedLength = readLe32(&patch[pos]);
  ASSERT_GT(codedLength, 2u);
  patch[pos + DeltaCodec::CHUNK_HEADER_SIZE + (codedLength / 2)] ^= 0xFF;
  writeFile(this->m_patch, patch.data(), patch.size());

  this->apply(1);
  ASSERT_EVENTS_PatchStarted_SIZE(1);
  this->runUntilResponse();
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::EXECUTION_ERROR);
  ASSERT_EVENTS_ChunkFailed_SIZE(1);
  ASSERT_EQ(this->eventHistory_ChunkFailed->at(0).chunk, target);
  // A corrupted LZSS payload may surface as a CRC miss, a malformed op or a
  // short chunk depending on the byte hit
  const DeltaPatchStatus failStatus =
      this->eventHistory_ChunkFailed->at(0).status;
  ASSERT_TRUE(failStatus == DeltaPatchStatus::CHUNK_CRC ||
              failStatus == DeltaPatchStatus::BAD_OPCODE ||
              failStatus == DeltaPatchStatus::TRUNCATED);
  ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, failStatus);
  ASSERT_EVENTS_PatchComplete_SIZE(0);
  ASSERT_from_patchComplete_SIZE(0);
  ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::FAILED);
  ASSERT_TLM_ChunksDone(this->tlmHistory_ChunksDone->size() - 1, target);

  // Repairing the patch and re-applying resumes from the verified prefix and
  // completes
  this->clearHistory();
  writeFile(this->m_patch, PATCH_LZSS, PATCH_LZSS_SIZE);
  this->apply(2);
  this->runUntilResponse();
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 2,
                      Fw::CmdResponse::OK);
  ASSERT_EVENTS_PatchResumed_SIZE(1);
  ASSERT_EVENTS_PatchResumed(0, this->m_new.c_str(), target, CHUNK_COUNT);
  ASSERT_EVENTS_PatchComplete_SIZE(1);
  const std::string produced = readFile(this->m_new);
  ASSERT_EQ(produced.size(), NEW_IMAGE_SIZE);
  ASSERT_EQ(0, memcmp(produced.data(), NEW_IMAGE, NEW_IMAGE_SIZE));
}

void DeltaPatcherTester ::abortAndResume() {
  this->apply(1);
  // Verify-new, verify-old, then two chunks
  for (U32 i = 0; i < 4; i++) {
    this->invoke_to_run(0, 0);
  }
  ASSERT_CMD_RESPONSE_SIZE(0);
  const U32 written = this->component.m_codec.chunkIndex();
  ASSERT_GE(written, 1u);

  this->sendCmd_ABORT_PATCH(0, 2);
  this->invoke_to_run(0, 0);
  ASSERT_EVENTS_PatchAborted_SIZE(1);
  ASSERT_EVENTS_PatchAborted(0, written);
  ASSERT_CMD_RESPONSE_SIZE(2);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::EXECUTION_ERROR);
  ASSERT_CMD_RESPONSE(1, DeltaPatcher::OPCODE_ABORT_PATCH, 2,
                      Fw::CmdResponse::OK);
  ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::IDLE);
  ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1,
                        DeltaPatchStatus::ABORTED);
  // Further ticks do nothing
  this->clearHistory();
  this->invoke_to_run(0, 0);
  ASSERT_TLM_SIZE(0);
  ASSERT_EVENTS_SIZE(0);

  this->apply(3);
  this->runUntilResponse();
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 3,
                      Fw::CmdResponse::OK);
  ASSERT_EVENTS_PatchResumed_SIZE(1);
  ASSERT_EVENTS_PatchResumed(0, this->m_new.c_str(), written, CHUNK_COUNT);
  ASSERT_EVENTS_PatchComplete_SIZE(1);
  ASSERT_from_patchComplete_SIZE(1);
  const std::string produced = readFile(this->m_new);
  ASSERT_EQ(produced.size(), NEW_IMAGE_SIZE);
  ASSERT_EQ(0, memcmp(produced.data(), NEW_IMAGE, NEW_IMAGE_SIZE));
}

void DeltaPatcherTester ::abortIdle() {
  this->sendCmd_ABORT_PATCH(0, 1);
  this->invoke_to_run(0, 0);
  ASSERT_CMD_RESPONSE_SIZE(1);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_ABORT_PATCH, 1,
                      Fw::CmdResponse::VALIDATION_ERROR);
  ASSERT_EVENTS_SIZE(1);
  ASSERT_EVENTS_AbortIgnored_SIZE(1);
  ASSERT_EVENTS_AbortIgnored(0, DeltaPatchState::IDLE);
}

void DeltaPatcherTester ::sameFile() {
  // new_file aliasing old_file would destroy the only copy of the old image:
  // rejected before any open
  this->sendCmd_APPLY_PATCH(0, 1, Fw::CmdStringArg(this->m_old.c_str()),
                            Fw::CmdStringArg(this->m_patch.c_str()),
                            Fw::CmdStringArg(this->m_old.c_str()));
  this->invoke_to_run(0, 0);
  ASSERT_EVENTS_PatchRejected_SIZE(1);
  ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::SAME_FILE);
  ASSERT_CMD_RESPONSE_SIZE(1);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::VALIDATION_ERROR);
  ASSERT_EVENTS_PatchStarted_SIZE(0);
  ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1,
                        DeltaPatchStatus::SAME_FILE);
  // The old image is untouched
  const std::string old = readFile(this->m_old);
  ASSERT_EQ(old.size(), OLD_IMAGE_SIZE);
  ASSERT_EQ(0, memcmp(old.data(), OLD_IMAGE, OLD_IMAGE_SIZE));
}

} // namespace Update
