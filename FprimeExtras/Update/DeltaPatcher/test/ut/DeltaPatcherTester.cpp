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

FwSizeType DeltaPatcherTester ::readSize(const U8 *data) {
  FwSizeType value = 0;
  for (FwSizeType i = 0; i < DeltaCodec::SIZE_FIELD_WIDTH; i++) {
    value = static_cast<FwSizeType>((value << 8) | data[i]);
  }
  return value;
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

void DeltaPatcherTester ::coderMismatch() {
  // Valid RLE patch presented to the default LZSS coder
  writeFile(this->m_patch, PATCH_RLE, PATCH_RLE_SIZE);
  this->apply(1);
  ASSERT_EVENTS_PatchRejected_SIZE(1);
  ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::CODER_MISMATCH);
  ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1,
                        DeltaPatchStatus::CODER_MISMATCH);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::EXECUTION_ERROR);
  ASSERT_EVENTS_PatchStarted_SIZE(0);
}

void DeltaPatcherTester ::sizeWidthMismatch() {
  // Valid LZSS patch serialized with the other FwSizeType width
  const U8 *patch = (sizeof(FwSizeType) == 8) ? PATCH_LZSS_W4 : PATCH_LZSS_W8;
  const FwSizeType size =
      (sizeof(FwSizeType) == 8) ? PATCH_LZSS_W4_SIZE : PATCH_LZSS_W8_SIZE;
  writeFile(this->m_patch, patch, size);
  this->apply(1);
  ASSERT_EVENTS_PatchRejected_SIZE(1);
  ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::SIZE_WIDTH_MISMATCH);
  ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1,
                        DeltaPatchStatus::SIZE_WIDTH_MISMATCH);
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
    pos += DeltaCodec::CHUNK_HEADER_SIZE + readSize(&patch[pos]);
  }
  const FwSizeType codedLength = readSize(&patch[pos]);
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

void DeltaPatcherTester ::queueOverflow() {
  // Fill the queue, then one more: the overflow hook must answer BUSY on the
  // caller's thread so the dispatcher never waits on a dropped command
  for (U32 seq = 1; seq <= TEST_INSTANCE_QUEUE_DEPTH; seq++) {
    this->sendCmd_ABORT_PATCH(0, seq);
  }
  ASSERT_CMD_RESPONSE_SIZE(0);
  this->sendCmd_ABORT_PATCH(0, TEST_INSTANCE_QUEUE_DEPTH + 1);
  ASSERT_CMD_RESPONSE_SIZE(1);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_ABORT_PATCH,
                      TEST_INSTANCE_QUEUE_DEPTH + 1, Fw::CmdResponse::BUSY);
  this->sendCmd_APPLY_PATCH(0, TEST_INSTANCE_QUEUE_DEPTH + 2,
                            Fw::CmdStringArg(this->m_old.c_str()),
                            Fw::CmdStringArg(this->m_patch.c_str()),
                            Fw::CmdStringArg(this->m_new.c_str()));
  ASSERT_CMD_RESPONSE_SIZE(2);
  ASSERT_CMD_RESPONSE(1, DeltaPatcher::OPCODE_APPLY_PATCH,
                      TEST_INSTANCE_QUEUE_DEPTH + 2, Fw::CmdResponse::BUSY);
  ASSERT_EVENTS_PatchRejected_SIZE(1);
  ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::BUSY);
  // Queued commands are still dispatched, MAX_DISPATCH_PER_TICK per tick
  this->invoke_to_run(0, 0);
  ASSERT_CMD_RESPONSE_SIZE(2 + DeltaPatcher::MAX_DISPATCH_PER_TICK);
}

void DeltaPatcherTester ::sameFileAliased() {
  const std::string alias = this->m_dir + "/./../" +
                            this->m_dir.substr(this->m_dir.rfind('/') + 1) +
                            "//old.bin";
  this->sendCmd_APPLY_PATCH(0, 1, Fw::CmdStringArg(this->m_old.c_str()),
                            Fw::CmdStringArg(this->m_patch.c_str()),
                            Fw::CmdStringArg(alias.c_str()));
  this->invoke_to_run(0, 0);
  ASSERT_EVENTS_PatchRejected_SIZE(1);
  ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::SAME_FILE);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::VALIDATION_ERROR);
  ASSERT_EVENTS_PatchStarted_SIZE(0);
  const std::string old = readFile(this->m_old);
  ASSERT_EQ(old.size(), OLD_IMAGE_SIZE);
  ASSERT_EQ(0, memcmp(old.data(), OLD_IMAGE, OLD_IMAGE_SIZE));
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

void DeltaPatcherTester ::sameFileHardLink() {
  const std::string link = this->m_dir + "/linked.bin";
  ASSERT_EQ(::link(this->m_old.c_str(), link.c_str()), 0);
  this->sendCmd_APPLY_PATCH(0, 1, Fw::CmdStringArg(this->m_old.c_str()),
                            Fw::CmdStringArg(this->m_patch.c_str()),
                            Fw::CmdStringArg(link.c_str()));
  this->invoke_to_run(0, 0);
  ASSERT_EVENTS_PatchRejected_SIZE(1);
  ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::SAME_FILE);
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::VALIDATION_ERROR);
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
      {fifo, this->m_patch, this->m_new}, // FIFO with no peer: a blocking open
                                          // would hang the rate group
      {this->m_old, this->m_dir, this->m_new}, // directory as patch
      {this->m_old, this->m_patch, fifo},      // FIFO as output
      {symlinkPath, this->m_patch,
       this->m_new}, // symlink to the old image (alias not visible textually)
      {this->m_old, this->m_patch, this->m_dir}, // directory as output
  };
  U32 seq = 1;
  for (const Case &c : cases) {
    this->sendCmd_APPLY_PATCH(0, seq, Fw::CmdStringArg(c.oldPath.c_str()),
                              Fw::CmdStringArg(c.patchPath.c_str()),
                              Fw::CmdStringArg(c.newPath.c_str()));
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_PatchRejected_SIZE(seq);
    ASSERT_EVENTS_PatchRejected(seq - 1, DeltaPatchStatus::OPEN_FAILED);
    ASSERT_CMD_RESPONSE_SIZE(seq);
    ASSERT_CMD_RESPONSE(seq - 1, DeltaPatcher::OPCODE_APPLY_PATCH, seq,
                        Fw::CmdResponse::EXECUTION_ERROR);
    seq++;
  }
  ASSERT_EVENTS_PatchStarted_SIZE(0);
  ASSERT_EQ(::access(this->m_new.c_str(), F_OK), -1); // nothing created
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
         this->component.m_codec.state() == DeltaCodec::PATCHING &&
         this->component.m_codec.chunkIndex() <
             this->component.m_codec.chunkCount()) {
    this->invoke_to_run(0, 0);
    ticks++;
  }
  ASSERT_EQ(this->component.m_codec.state(), DeltaCodec::PATCHING);
  ASSERT_EQ(this->component.m_codec.chunkIndex(),
            this->component.m_codec.chunkCount());
  // Replace the output path with a same-sized impostor; the patcher's open
  // handle still refers to the (now orphaned) correct image, so only a by-name
  // re-resolution can catch this
  const std::string impostor = this->m_dir + "/impostor.bin";
  std::vector<U8> junk(NEW_IMAGE_SIZE, 0x5A);
  writeFile(impostor, junk.data(), junk.size());
  ASSERT_EQ(::rename(impostor.c_str(), this->m_new.c_str()), 0);
  ticks += this->runUntilResponse();
  ASSERT_LT(ticks, static_cast<U32>(MAX_TICKS));
  ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, 1,
                      Fw::CmdResponse::EXECUTION_ERROR);
  ASSERT_EVENTS_PatchComplete_SIZE(0);
  ASSERT_from_patchComplete_SIZE(0);
  ASSERT_EVENTS_ChunkFailed_SIZE(1);
  ASSERT_EQ(this->eventHistory_ChunkFailed->at(0).status,
            DeltaPatchStatus::NEW_IMAGE_MISMATCH);
}

} // namespace Update
