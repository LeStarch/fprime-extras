// ======================================================================
// \title  PatcherRules.cpp
// \author starchmd
// \brief  Rules for the DeltaPatcher component: a random walk over
// APPLY_PATCH / ABORT_PATCH / run ticks with damaged inputs, damaged
// output, missing files and a flooded queue. The intended image, the FPP
// state machine (State / StateEntered) and the one-response-per-command
// contract are the oracle.
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>

#include "DeltaPatcherTester.hpp"

namespace Update {

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void DeltaPatcherTester ::writeFile(const std::string& path, const std::vector<U8>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
}

static std::vector<U8> readBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<U8>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static FwSizeType pickImageSize() {
    switch (STest::Pick::lowerUpper(0, 4)) {
        case 0:
            return 0;
        case 1:
            return STest::Pick::lowerUpper(1, 16);
        case 2:
            return STest::Pick::lowerUpper(DELTA_WINDOW_SIZE - 2, DELTA_WINDOW_SIZE + 2);
        default:
            return STest::Pick::lowerUpper(1, 3000);
    }
}

void DeltaPatcherTester ::newRandomPatch() {
    FwSizeType chunkBytes = 0;
    switch (STest::Pick::lowerUpper(0, 2)) {
        case 0:
            chunkBytes = STest::Pick::lowerUpper(1, 8);
            break;
        case 1:
            chunkBytes = STest::Pick::lowerUpper(DELTA_OUTPUT_BUFFER_SIZE - 1, DELTA_OUTPUT_BUFFER_SIZE + 1);
            break;
        default:
            chunkBytes = STest::Pick::lowerUpper(1, 1024);
            break;
    }
    this->m_rp = TestEncoders::buildRandomPatch(this->m_coderId, pickImageSize(), pickImageSize(), chunkBytes);
    writeFile(this->m_old, this->m_rp.oldImage);
    writeFile(this->m_patch, this->m_rp.patch);
    (void)unlink(this->m_new.c_str());
    this->m_haveFiles = true;
    this->m_history.clear();
}

FwSizeType DeltaPatcherTester ::outputSize() const {
    struct stat st;
    return (stat(this->m_new.c_str(), &st) == 0) ? static_cast<FwSizeType>(st.st_size) : 0;
}

bool DeltaPatcherTester ::outputMatches() const {
    struct stat st;
    if (stat(this->m_new.c_str(), &st) != 0) {
        return this->m_rp.newImage.empty();
    }
    return readBytes(this->m_new) == this->m_rp.newImage;
}

FwSizeType DeltaPatcherTester ::expectedResumedChunks() const {
    const std::vector<U8> have = readBytes(this->m_new);
    const std::vector<U8>& want = this->m_rp.newImage;
    const FwSizeType chunkBytes = this->m_rp.chunkBytes;
    if (have.size() > want.size()) {
        return 0;  // stale output is rejected before any resume
    }
    const FwSizeType verify = (have.size() == want.size()) ? this->m_rp.chunkCount() : have.size() / chunkBytes;
    FwSizeType count = 0;
    for (; count < verify; count++) {
        const FwSizeType start = count * chunkBytes;
        const FwSizeType length = ((want.size() - start) < chunkBytes) ? (want.size() - start) : chunkBytes;
        if (memcmp(&have[start], &want[start], static_cast<size_t>(length)) != 0) {
            break;
        }
    }
    return count;
}

U32 DeltaPatcherTester ::runUntilResponses(FwSizeType responses) {
    U32 ticks = 0;
    for (; ticks < MAX_TICKS && this->cmdResponseHistory->size() < responses; ticks++) {
        this->invoke_to_run(0, 0);
        this->m_rc.ticks++;
    }
    return ticks;
}

void DeltaPatcherTester ::checkEnded() {
    ASSERT_FALSE(this->component.isPatchBound());
    ASSERT_EQ(this->component.deltaPatchMachine_getState(), DeltaPatcher::Update_DeltaPatchMachine::State::IDLE);
    if (this->tlmHistory_State->size() > 0) {
        ASSERT_TLM_State(this->tlmHistory_State->size() - 1, DeltaPatchState::IDLE);
    }
    if (this->eventHistory_PatchComplete->size() > 0) {
        ASSERT_TRUE(this->outputMatches()) << "PatchComplete with a wrong image";
        ASSERT_from_patchComplete_SIZE(this->eventHistory_PatchComplete->size());
    }
    this->m_busy = false;
}

void DeltaPatcherTester ::printRuleCounters() const {
    const RuleCounters& c = this->m_rc;
    printf(
        "Patcher rules (coder %u): applies %u (resumes %u), completions %u, aborts %u (overtaken %u), rejections %u, "
        "chunk failures %u, old mismatches %u, output corruptions %u, floods %u, ticks %u\n",
        this->m_coderId, c.applies, c.resumes, c.completions, c.aborts, c.abortsOvertaken, c.rejections,
        c.chunkFailures, c.oldMismatches, c.outputCorruptions, c.floods, c.ticks);
}

// ----------------------------------------------------------------------
// Patcher.Apply: bind a fresh patch, or re-apply the current one over its
// existing output; stale output is rejected and cleared
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__Apply__precondition() const {
    return !this->m_busy;
}

void DeltaPatcherTester ::Patcher__Apply__action() {
    this->m_history += " Apply";
    const bool reuse = this->m_haveFiles && STest::Pick::lowerUpper(0, 3) != 0;
    if (!reuse) {
        this->newRandomPatch();
    }
    this->clearHistory();
    const FwSizeType existing = this->outputSize();
    if (existing > this->m_rp.newImage.size()) {
        this->apply(this->m_nextSeq);
        ASSERT_CMD_RESPONSE_SIZE(1);
        ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, this->m_nextSeq, Fw::CmdResponse::EXECUTION_ERROR);
        ASSERT_EVENTS_PatchRejected_SIZE(1);
        ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::OUTPUT_STALE);
        ASSERT_EVENTS_PatchStarted_SIZE(0);
        ASSERT_TLM_State_SIZE(0);
        this->m_nextSeq++;
        this->m_rc.rejections++;
        (void)unlink(this->m_new.c_str());
        this->checkEnded();
        return;
    }
    this->m_expectResumed = this->expectedResumedChunks();
    this->m_outputAtApply = readBytes(this->m_new);
    this->m_applySeq = this->m_nextSeq++;
    this->apply(this->m_applySeq);
    ASSERT_EQ(this->cmdResponseHistory->size(), 0u)
        << "APPLY_PATCH answered synchronously: " << this->cmdResponseHistory->at(0).response << " rejected "
        << this->eventHistory_PatchRejected->size();
    ASSERT_EVENTS_PatchStarted_SIZE(1);
    ASSERT_EVENTS_PatchRejected_SIZE(0);
    // ChunksTotal is update-on-change: only a changed chunk count is downlinked
    if (this->tlmHistory_ChunksTotal->size() > 0) {
        ASSERT_TLM_ChunksTotal(0, static_cast<U32>(this->m_rp.chunkCount()));
    }
    const bool verifies =
        existing >= this->m_rp.chunkBytes || (existing == this->m_rp.newImage.size() && this->m_rp.chunkCount() > 0);
    ASSERT_GE(this->tlmHistory_State->size(), 1u);
    ASSERT_TLM_State(0, verifies ? DeltaPatchState::VERIFY_RESUME : DeltaPatchState::VERIFY_OLD);
    ASSERT_EVENTS_StateEntered(0, verifies ? DeltaPatchState::VERIFY_RESUME : DeltaPatchState::VERIFY_OLD);
    this->m_resumedEvents = this->eventHistory_PatchResumed->size();
    this->m_busy = true;
    this->m_rc.applies++;
    if (reuse && existing > 0) {
        this->m_rc.resumes++;
    }
}

// ----------------------------------------------------------------------
// Patcher.Tick: one rate-group tick; completion must be byte-exact
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__Tick__precondition() const {
    return this->m_busy;
}

void DeltaPatcherTester ::Patcher__Tick__action() {
    this->m_history += " Tick";
    this->clearHistory();
    this->invoke_to_run(0, 0);
    this->m_rc.ticks++;
    this->m_resumedEvents += this->eventHistory_PatchResumed->size();
    if (this->tlmHistory_ChunksDone->size() > 0) {
        ASSERT_LE(this->tlmHistory_ChunksDone->at(this->tlmHistory_ChunksDone->size() - 1).arg,
                  static_cast<U32>(this->m_rp.chunkCount()));
    }
    if (this->cmdResponseHistory->size() == 0) {
        ASSERT_TRUE(this->component.isPatchBound())
            << "unbound without a response; state " << static_cast<int>(this->component.deltaPatchMachine_getState())
            << " output at apply " << this->m_outputAtApply.size() << " chunkBytes " << this->m_rp.chunkBytes << " new "
            << this->m_rp.newImage.size() << " old " << this->m_rp.oldImage.size() << " events " << this->eventsSize
            << " history:" << this->m_history;
        return;
    }
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, this->m_applySeq, Fw::CmdResponse::OK);
    ASSERT_EVENTS_PatchComplete_SIZE(1);
    ASSERT_TRUE(this->outputMatches());
    if (this->tlmHistory_LastStatus->size() > 0) {  // update-on-change: a repeated OP_OK is not re-sent
        ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::OP_OK);
    }
    ASSERT_LE(this->m_resumedEvents, 1u);
    if (this->m_expectResumed > 0 && this->m_resumedEvents != 1) {
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            writeFile("/tmp/dp_resume_old.bin", this->m_rp.oldImage);
            writeFile("/tmp/dp_resume_patch.bin", this->m_rp.patch);
            writeFile("/tmp/dp_resume_new.bin", this->m_rp.newImage);
            writeFile("/tmp/dp_resume_outatapply.bin", this->m_outputAtApply);
            printf("dumped /tmp/dp_resume_*.bin; rules since the patch was built:%s\n", this->m_history.c_str());
        }
    }
    if (this->m_expectResumed > 0) {
        ASSERT_EQ(this->m_resumedEvents, 1u)
            << "expected resume of " << this->m_expectResumed << " chunks; chunkBytes " << this->m_rp.chunkBytes
            << " new " << this->m_rp.newImage.size() << " chunks " << this->m_rp.chunkCount() << " output at apply "
            << this->m_outputAtApply.size() << " old " << this->m_rp.oldImage.size() << " coder "
            << int(this->m_coderId);
    }
    if (this->eventHistory_PatchResumed->size() == 1) {  // the resume scan ended on this tick
        ASSERT_EQ(this->eventHistory_PatchResumed->at(0).chunk, static_cast<U32>(this->m_expectResumed));
    }
    this->checkEnded();
    this->m_rc.completions++;
}

// ----------------------------------------------------------------------
// Patcher.ApplyWhileBusy: a second APPLY_PATCH is answered BUSY at once
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__ApplyWhileBusy__precondition() const {
    return this->m_busy;
}

void DeltaPatcherTester ::Patcher__ApplyWhileBusy__action() {
    this->m_history += " ApplyWhileBusy";
    this->clearHistory();
    const U32 seq = this->m_nextSeq++;
    this->sendCmd_APPLY_PATCH(0, seq, Fw::CmdStringArg(this->m_old.c_str()), Fw::CmdStringArg(this->m_patch.c_str()),
                              Fw::CmdStringArg(this->m_new.c_str()));
    this->component.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, seq, Fw::CmdResponse::BUSY);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::BUSY);
    ASSERT_TRUE(this->component.isPatchBound());
    this->m_rc.rejections++;
}

// ----------------------------------------------------------------------
// Patcher.Abort: ABORT_PATCH mid-patch; either it takes effect (both
// commands answered, output retained) or completion overtook it
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__Abort__precondition() const {
    return this->m_busy;
}

void DeltaPatcherTester ::Patcher__Abort__action() {
    this->m_history += " Abort";
    this->clearHistory();
    const U32 seq = this->m_nextSeq++;
    this->sendCmd_ABORT_PATCH(0, seq);
    const U32 abortTicks = this->runUntilResponses(2);
    ASSERT_LT(abortTicks, static_cast<U32>(MAX_TICKS))
        << "responses " << this->cmdResponseHistory->size() << " PatchAborted "
        << this->eventHistory_PatchAborted->size() << " AbortIgnored " << this->eventsSize_AbortIgnored
        << " SignalDropped " << this->eventHistory_SignalDropped->size() << " PatchComplete "
        << this->eventHistory_PatchComplete->size() << " bound " << this->component.isPatchBound() << " state "
        << static_cast<int>(this->component.deltaPatchMachine_getState()) << " chunks " << this->m_rp.chunkCount()
        << " chunkBytes " << this->m_rp.chunkBytes << " new " << this->m_rp.newImage.size() << " old "
        << this->m_rp.oldImage.size() << " output " << this->outputSize() << " resumeExpected "
        << this->m_expectResumed;
    ASSERT_CMD_RESPONSE_SIZE(2);
    const CmdResponse& first = this->cmdResponseHistory->at(0);
    const CmdResponse& second = this->cmdResponseHistory->at(1);
    if (this->eventHistory_PatchAborted->size() == 1) {
        // Abort took effect: APPLY_PATCH fails, ABORT_PATCH succeeds, in that order
        ASSERT_EQ(first.opCode, DeltaPatcher::OPCODE_APPLY_PATCH);
        ASSERT_EQ(first.cmdSeq, this->m_applySeq);
        ASSERT_EQ(first.response, Fw::CmdResponse::EXECUTION_ERROR);
        ASSERT_EQ(second.opCode, DeltaPatcher::OPCODE_ABORT_PATCH);
        ASSERT_EQ(second.cmdSeq, seq);
        ASSERT_EQ(second.response, Fw::CmdResponse::OK);
        ASSERT_EVENTS_PatchComplete_SIZE(0);
        ASSERT_EVENTS_AbortIgnored_SIZE(0);
        if (this->tlmHistory_LastStatus->size() > 0) {  // update-on-change
            ASSERT_TLM_LastStatus(this->tlmHistory_LastStatus->size() - 1, DeltaPatchStatus::ABORTED);
        }
        this->m_rc.aborts++;
    } else {
        // Completion on the same tick: the abort finds the machine back in IDLE
        ASSERT_EQ(first.opCode, DeltaPatcher::OPCODE_APPLY_PATCH);
        ASSERT_EQ(first.response, Fw::CmdResponse::OK);
        ASSERT_EQ(second.opCode, DeltaPatcher::OPCODE_ABORT_PATCH);
        ASSERT_EQ(second.response, Fw::CmdResponse::VALIDATION_ERROR);
        ASSERT_EVENTS_PatchComplete_SIZE(1);
        ASSERT_EVENTS_AbortIgnored_SIZE(1);
        this->m_rc.abortsOvertaken++;
    }
    this->checkEnded();
}

// ----------------------------------------------------------------------
// Patcher.AbortIdle: ABORT_PATCH with nothing in progress is ignored
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__AbortIdle__precondition() const {
    return !this->m_busy;
}

void DeltaPatcherTester ::Patcher__AbortIdle__action() {
    this->m_history += " AbortIdle";
    this->clearHistory();
    const U32 seq = this->m_nextSeq++;
    this->sendCmd_ABORT_PATCH(0, seq);
    this->invoke_to_run(0, 0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_ABORT_PATCH, seq, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_AbortIgnored_SIZE(1);
    ASSERT_TLM_State_SIZE(0);
    this->checkEnded();
}

// ----------------------------------------------------------------------
// Patcher.CorruptPatch: a damaged patch file is rejected (header) or fails
// at the damaged chunk; an inert bit may still complete byte-exact
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__CorruptPatch__precondition() const {
    return !this->m_busy && this->m_haveFiles && this->m_rp.chunkCount() > 0;
}

void DeltaPatcherTester ::Patcher__CorruptPatch__action() {
    this->m_history += " CorruptPatch";
    if (this->outputSize() > this->m_rp.newImage.size()) {
        (void)unlink(this->m_new.c_str());  // stale output is Patcher.Apply's case
    }
    std::vector<U8> damaged = this->m_rp.patch;
    const bool header = STest::Pick::lowerUpper(0, 3) == 0;
    const FwSizeType at = header ? STest::Pick::lowerUpper(0, static_cast<U32>(DeltaCodec::HEADER_SIZE - 1))
                                 : STest::Pick::lowerUpper(static_cast<U32>(DeltaCodec::HEADER_SIZE),
                                                           static_cast<U32>(damaged.size() - 1));
    damaged[at] ^= static_cast<U8>(1u << STest::Pick::lowerUpper(0, 7));
    writeFile(this->m_patch, damaged);
    const FwSizeType outputBefore = this->outputSize();
    this->clearHistory();
    const U32 seq = this->m_nextSeq++;
    this->apply(seq);
    if (header) {
        ASSERT_CMD_RESPONSE_SIZE(1);
        ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, seq, Fw::CmdResponse::EXECUTION_ERROR);
        ASSERT_EVENTS_PatchRejected_SIZE(1);
        ASSERT_EVENTS_PatchStarted_SIZE(0);
        ASSERT_TLM_State_SIZE(0);
        ASSERT_EQ(this->outputSize(), outputBefore);
        this->m_rc.rejections++;
    } else {
        ASSERT_LT(this->runUntilResponses(1), static_cast<U32>(MAX_TICKS));
        ASSERT_CMD_RESPONSE_SIZE(1);
        const CmdResponse& response = this->cmdResponseHistory->at(0);
        ASSERT_EQ(response.opCode, DeltaPatcher::OPCODE_APPLY_PATCH);
        ASSERT_EQ(response.cmdSeq, seq);
        if (response.response == Fw::CmdResponse::OK) {
            ASSERT_EVENTS_PatchComplete_SIZE(1);
            ASSERT_TRUE(this->outputMatches());
            this->m_rc.completions++;
        } else {
            ASSERT_EQ(response.response, Fw::CmdResponse::EXECUTION_ERROR);
            ASSERT_EVENTS_PatchComplete_SIZE(0);
            // A damaged chunk header can already fail the geometry walk at begin (PatchRejected);
            // otherwise the damage surfaces at the chunk (ChunkFailed)
            ASSERT_EQ(this->eventHistory_ChunkFailed->size() + this->eventHistory_PatchRejected->size(), 1u);
            if (this->eventHistory_ChunkFailed->size() == 1) {
                this->m_rc.chunkFailures++;
            } else {
                this->m_rc.rejections++;
            }
        }
    }
    writeFile(this->m_patch, this->m_rp.patch);
    this->checkEnded();
}

// ----------------------------------------------------------------------
// Patcher.CorruptOutput: damage the retained output before the next apply
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__CorruptOutput__precondition() const {
    return !this->m_busy && this->m_haveFiles && this->outputSize() > 0;
}

void DeltaPatcherTester ::Patcher__CorruptOutput__action() {
    this->m_history += " CorruptOutput";
    std::vector<U8> data = readBytes(this->m_new);
    switch (STest::Pick::lowerUpper(0, 3)) {
        case 0:
            data.resize(STest::Pick::lowerUpper(0, static_cast<U32>(data.size() - 1)));
            break;
        case 1:
            for (FwSizeType i = STest::Pick::lowerUpper(1, 64); i > 0; i--) {
                data.push_back(static_cast<U8>(STest::Pick::lowerUpper(0, 255)));
            }
            break;
        default:
            data[STest::Pick::lowerUpper(0, static_cast<U32>(data.size() - 1))] ^=
                static_cast<U8>(1u << STest::Pick::lowerUpper(0, 7));
            break;
    }
    writeFile(this->m_new, data);
    this->m_rc.outputCorruptions++;
}

// ----------------------------------------------------------------------
// Patcher.OldMismatch: a modified old image fails verification (same size)
// or is rejected (different size); the output is untouched
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__OldMismatch__precondition() const {
    return !this->m_busy && this->m_haveFiles;
}

void DeltaPatcherTester ::Patcher__OldMismatch__action() {
    this->m_history += " OldMismatch";
    if (this->outputSize() > this->m_rp.newImage.size()) {
        (void)unlink(this->m_new.c_str());  // stale output is Patcher.Apply's case
    }
    std::vector<U8> damaged = this->m_rp.oldImage;
    const bool sameSize = !damaged.empty() && STest::Pick::lowerUpper(0, 1) == 0;
    if (sameSize) {
        damaged[STest::Pick::lowerUpper(0, static_cast<U32>(damaged.size() - 1))] ^=
            static_cast<U8>(1u << STest::Pick::lowerUpper(0, 7));
    } else {
        damaged.push_back(0);
    }
    writeFile(this->m_old, damaged);
    const std::vector<U8> outputBefore = readBytes(this->m_new);
    this->clearHistory();
    const U32 seq = this->m_nextSeq++;
    this->apply(seq);
    if (sameSize) {
        ASSERT_LT(this->runUntilResponses(1), static_cast<U32>(MAX_TICKS));
        ASSERT_EVENTS_OldImageMismatch_SIZE(1);
        ASSERT_EVENTS_PatchStarted_SIZE(1);
        this->m_rc.oldMismatches++;
    } else {
        ASSERT_EVENTS_PatchRejected_SIZE(1);
        ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::OLD_IMAGE_MISMATCH);
        ASSERT_EVENTS_PatchStarted_SIZE(0);
        this->m_rc.rejections++;
    }
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, seq, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_PatchComplete_SIZE(0);
    ASSERT_EQ(readBytes(this->m_new), outputBefore);
    writeFile(this->m_old, this->m_rp.oldImage);
    this->checkEnded();
}

// ----------------------------------------------------------------------
// Patcher.MissingInput: a missing old or patch file is OPEN_FAILED at once
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__MissingInput__precondition() const {
    return !this->m_busy;
}

void DeltaPatcherTester ::Patcher__MissingInput__action() {
    this->m_history += " MissingInput";
    this->clearHistory();
    const U32 seq = this->m_nextSeq++;
    const std::string missing = this->m_dir + "/missing.bin";
    const bool oldMissing = STest::Pick::lowerUpper(0, 1) == 0;
    this->sendCmd_APPLY_PATCH(0, seq, Fw::CmdStringArg((oldMissing ? missing : this->m_old).c_str()),
                              Fw::CmdStringArg((oldMissing ? this->m_patch : missing).c_str()),
                              Fw::CmdStringArg(this->m_new.c_str()));
    this->invoke_to_run(0, 0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, seq, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::OPEN_FAILED);
    ASSERT_TLM_State_SIZE(0);
    this->m_rc.rejections++;
    this->checkEnded();
}

// ----------------------------------------------------------------------
// Patcher.SameFile: new_file naming the old image or the patch is refused
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__SameFile__precondition() const {
    return !this->m_busy && this->m_haveFiles;
}

void DeltaPatcherTester ::Patcher__SameFile__action() {
    this->m_history += " SameFile";
    this->clearHistory();
    const U32 seq = this->m_nextSeq++;
    const std::string& target = (STest::Pick::lowerUpper(0, 1) == 0) ? this->m_old : this->m_patch;
    const std::vector<U8> before = readBytes(target);
    this->sendCmd_APPLY_PATCH(0, seq, Fw::CmdStringArg(this->m_old.c_str()), Fw::CmdStringArg(this->m_patch.c_str()),
                              Fw::CmdStringArg(target.c_str()));
    this->invoke_to_run(0, 0);
    ASSERT_CMD_RESPONSE_SIZE(1);
    // Path checks are argument validation: answered VALIDATION_ERROR before anything is opened
    ASSERT_CMD_RESPONSE(0, DeltaPatcher::OPCODE_APPLY_PATCH, seq, Fw::CmdResponse::VALIDATION_ERROR);
    ASSERT_EVENTS_PatchRejected_SIZE(1);
    ASSERT_EVENTS_PatchRejected(0, DeltaPatchStatus::SAME_FILE);
    ASSERT_EQ(readBytes(target), before);
    ASSERT_TLM_State_SIZE(0);
    this->m_rc.rejections++;
    this->checkEnded();
}

// ----------------------------------------------------------------------
// Patcher.QueueFlood: ground fills the queue with ABORT_PATCH mid-patch;
// every command is answered exactly once and the machine ends in IDLE
// ----------------------------------------------------------------------

bool DeltaPatcherTester ::Patcher__QueueFlood__precondition() const {
    return this->m_busy;
}

void DeltaPatcherTester ::Patcher__QueueFlood__action() {
    this->m_history += " QueueFlood";
    this->clearHistory();
    const U32 aborts = STest::Pick::lowerUpper(2, TEST_INSTANCE_QUEUE_DEPTH + 1);
    const U32 firstSeq = this->m_nextSeq;
    for (U32 i = 0; i < aborts; i++) {
        this->sendCmd_ABORT_PATCH(0, this->m_nextSeq++);
    }
    const U32 floodTicks = this->runUntilResponses(aborts + 1);
    ASSERT_LT(floodTicks, static_cast<U32>(MAX_TICKS))
        << "aborts " << aborts << " responses " << this->cmdResponseHistory->size() << " PatchAborted "
        << this->eventHistory_PatchAborted->size() << " AbortIgnored " << this->eventsSize_AbortIgnored
        << " SignalDropped " << this->eventHistory_SignalDropped->size() << " PatchComplete "
        << this->eventHistory_PatchComplete->size() << " bound " << this->component.isPatchBound() << " state "
        << static_cast<int>(this->component.deltaPatchMachine_getState()) << " chunks " << this->m_rp.chunkCount()
        << " chunkBytes " << this->m_rp.chunkBytes;
    ASSERT_CMD_RESPONSE_SIZE(aborts + 1);
    U32 applyResponses = 0;
    U32 abortOk = 0;
    for (FwSizeType i = 0; i < this->cmdResponseHistory->size(); i++) {
        const CmdResponse& response = this->cmdResponseHistory->at(i);
        if (response.opCode == DeltaPatcher::OPCODE_APPLY_PATCH) {
            ASSERT_EQ(response.cmdSeq, this->m_applySeq);
            ASSERT_TRUE(response.response == Fw::CmdResponse::OK ||
                        response.response == Fw::CmdResponse::EXECUTION_ERROR);
            applyResponses++;
        } else {
            ASSERT_EQ(response.opCode, DeltaPatcher::OPCODE_ABORT_PATCH);
            ASSERT_GE(response.cmdSeq, firstSeq);
            ASSERT_LT(response.cmdSeq, firstSeq + aborts);
            ASSERT_TRUE(response.response == Fw::CmdResponse::OK ||
                        response.response == Fw::CmdResponse::VALIDATION_ERROR ||
                        response.response == Fw::CmdResponse::BUSY)
                << response.response;
            if (response.response == Fw::CmdResponse::OK) {
                abortOk++;
            }
        }
    }
    ASSERT_EQ(applyResponses, 1u);
    ASSERT_LE(abortOk, 1u);
    ASSERT_EQ(this->eventHistory_PatchAborted->size(), abortOk);
    // Further ticks are quiet
    this->clearHistory();
    this->invoke_to_run(0, 0);
    ASSERT_EVENTS_SIZE(0);
    ASSERT_CMD_RESPONSE_SIZE(0);
    this->m_rc.floods++;
    this->checkEnded();
}

}  // namespace Update
