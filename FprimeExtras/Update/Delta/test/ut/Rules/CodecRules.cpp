// ======================================================================
// \title  CodecRules.cpp
// \author starchmd
// \brief  Engine lifecycle rules: random valid patches run to a byte-exact
// image through any interleaving of aborts, output damage, patch damage and
// media faults; every rejection names the right status
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include <gtest/gtest.h>

#include "FprimeExtras/Update/Delta/test/ut/DeltaRulesTester.hpp"

namespace Update {

static const U32 MAX_STEPS_PER_PATCH = 20000;

// ----------------------------------------------------------------------
// Codec.Begin: bind a fresh patch, or re-bind the current one over its
// existing (possibly damaged) output; stale output is rejected first
// ----------------------------------------------------------------------

bool DeltaRulesTester::Codec__Begin__precondition() const {
    return this->m_phase == IDLE;
}

void DeltaRulesTester::Codec__Begin__action() {
    const bool resume = this->m_haveOutput && STest::Pick::lowerUpper(0, 3) != 0;
    if (!resume) {
        this->newPatch();
    }
    const FwSizeType newSize = this->m_patch.newImage.size();
    if (this->m_newMedia.data.size() > newSize) {
        ASSERT_EQ(this->bind(), DeltaCodec::OUTPUT_STALE);
        this->m_codec.reset();
        this->m_newMedia.data.clear();
        this->m_counters.staleOutputs++;
    }
    {
        char note[64];
        snprintf(note, sizeof note, " Begin(%s,out=%u)", resume ? "resume" : "new",
                 static_cast<unsigned>(this->m_newMedia.data.size()));
        this->m_history += note;
    }
    this->m_expectResumed = this->expectedResumedChunks();
    this->m_existingAtBegin = this->m_newMedia.data.size();
    this->m_savedOutput = this->m_newMedia.data;
    ASSERT_EQ(this->bind(), DeltaCodec::OP_OK);
    // Resume runs when whole chunks exist, or when the output is already complete
    const FwSizeType existing = this->m_newMedia.data.size();
    const bool verifies =
        existing >= this->m_patch.chunkBytes || (existing == newSize && this->m_patch.chunkCount() > 0);
    ASSERT_EQ(this->m_codec.state(), verifies ? TestCodec::VERIFY_RESUME : TestCodec::VERIFY_OLD);
    ASSERT_EQ(this->m_codec.chunkCount(), this->m_patch.chunkCount());
    ASSERT_EQ(this->m_codec.newSize(), newSize);
    ASSERT_EQ(this->m_codec.oldSize(), this->m_patch.oldImage.size());
    this->m_phase = RUNNING;
    this->m_stepsThisPatch = 0;
    this->m_counters.begins++;
    if (resume) {
        this->m_counters.resumes++;
    }
}

// ----------------------------------------------------------------------
// Codec.Step: one bounded unit of work; committed output always matches the
// reference; completion yields the exact image and the predicted resume count
// ----------------------------------------------------------------------

bool DeltaRulesTester::Codec__Step__precondition() const {
    return this->m_phase == RUNNING;
}

void DeltaRulesTester::Codec__Step__action() {
    const DeltaCodec::Status status = this->m_codec.step();
    this->m_counters.steps++;
    {
        char note[48];
        snprintf(note, sizeof note, " S%u:%u", static_cast<unsigned>(this->m_codec.state()),
                 static_cast<unsigned>(this->m_codec.chunkIndex()));
        this->m_history += note;
    }
    if (status != DeltaCodec::OP_OK) {
        this->dumpCase("/tmp/delta_fail");
    }
    ASSERT_EQ(status, DeltaCodec::OP_OK) << "coder " << static_cast<int>(this->m_patch.coderId) << " chunk "
                                         << this->m_codec.chunkIndex() << "/" << this->m_codec.chunkCount()
                                         << " chunkBytes " << this->m_patch.chunkBytes << " old "
                                         << this->m_patch.oldImage.size() << " new " << this->m_patch.newImage.size()
                                         << " patch " << this->m_patch.patch.size() << " coded["
                                         << this->m_codec.chunkIndex() << "] "
                                         << this->m_patch.codedLengths[this->m_codec.chunkIndex()];
    ASSERT_NE(this->m_codec.state(), TestCodec::FAILED);
    ASSERT_LT(++this->m_stepsThisPatch, MAX_STEPS_PER_PATCH);
    this->checkCommittedPrefix();
    if (this->m_codec.state() == TestCodec::COMPLETE) {
        ASSERT_EQ(this->m_newMedia.data, this->m_patch.newImage);
        ASSERT_EQ(this->m_codec.bytesWritten(), this->m_patch.newImage.size());
        ASSERT_EQ(this->m_codec.chunkIndex(), this->m_patch.chunkCount());
        static bool dumped = false;
        if (this->m_codec.resumedChunks() != this->m_expectResumed && !dumped) {
            dumped = true;
            this->dumpCase("/tmp/delta_resume");
        }
        ASSERT_EQ(this->m_codec.resumedChunks(), this->m_expectResumed)
            << "existing at begin " << this->m_existingAtBegin << " chunkBytes " << this->m_patch.chunkBytes << " new "
            << this->m_patch.newImage.size() << " chunks " << this->m_patch.chunkCount();
        this->m_counters.completions++;
        this->release();
    }
}

// ----------------------------------------------------------------------
// Codec.Abort: drop the engine mid-patch, keep whatever was written
// ----------------------------------------------------------------------

bool DeltaRulesTester::Codec__Abort__precondition() const {
    return this->m_phase == RUNNING;
}

void DeltaRulesTester::Codec__Abort__action() {
    this->m_history += " Abort";
    this->checkCommittedPrefix();
    this->release();
    this->m_counters.aborts++;
}

// ----------------------------------------------------------------------
// Codec.CorruptOutput: damage the retained output before a resume — flip a
// byte, cut it short, or grow it past the target (stale)
// ----------------------------------------------------------------------

bool DeltaRulesTester::Codec__CorruptOutput__precondition() const {
    return this->m_phase == IDLE && this->m_haveOutput && !this->m_newMedia.data.empty();
}

void DeltaRulesTester::Codec__CorruptOutput__action() {
    std::vector<U8>& data = this->m_newMedia.data;
    this->m_history += " CorruptOutput";
    switch (STest::Pick::lowerUpper(0, 3)) {
        case 0:
            data.resize(STest::Pick::lowerUpper(0, static_cast<U32>(data.size() - 1)));
            break;
        case 1: {
            const FwSizeType grow = STest::Pick::lowerUpper(1, 64);
            for (FwSizeType i = 0; i < grow; i++) {
                data.push_back(static_cast<U8>(STest::Pick::lowerUpper(0, 255)));
            }
            break;
        }
        default:
            data[STest::Pick::lowerUpper(0, static_cast<U32>(data.size() - 1))] ^=
                static_cast<U8>(1u << STest::Pick::lowerUpper(0, 7));
            break;
    }
    this->m_counters.outputCorruptions++;
}

// ----------------------------------------------------------------------
// Codec.CorruptPatch: flip a bit in a chunk not yet reached, run on: the
// engine must fail (or, if the bit was inert, complete byte-exact) and never
// commit a wrong chunk; the restored patch then resumes
// ----------------------------------------------------------------------

bool DeltaRulesTester::Codec__CorruptPatch__precondition() const {
    return this->m_phase == RUNNING && this->m_codec.state() == TestCodec::PATCHING &&
           this->m_codec.chunkIndex() + 1 < this->m_patch.chunkCount();
}

void DeltaRulesTester::Codec__CorruptPatch__action() {
    const FwSizeType chunk = STest::Pick::lowerUpper(static_cast<U32>(this->m_codec.chunkIndex() + 1),
                                                     static_cast<U32>(this->m_patch.chunkCount() - 1));
    const FwSizeType start = this->m_patch.chunkOffsets[chunk];
    const FwSizeType span = DeltaCodec::CHUNK_HEADER_SIZE + this->m_patch.codedLengths[chunk];
    const FwSizeType at = start + STest::Pick::lowerUpper(0, static_cast<U32>(span - 1));
    const U8 mask = static_cast<U8>(1u << STest::Pick::lowerUpper(0, 7));
    this->m_patchMedia.data[at] ^= mask;
    this->m_history += " CorruptPatch";
    U32 steps = 0;
    const DeltaCodec::Status status = run(this->m_codec, steps);
    this->checkCommittedPrefix();
    if (this->m_codec.state() == TestCodec::COMPLETE) {
        // An inert bit (e.g. unused trailing LZSS flag bits): only a byte-exact image is acceptable
        ASSERT_EQ(status, DeltaCodec::OP_OK);
        ASSERT_EQ(this->m_newMedia.data, this->m_patch.newImage);
    } else {
        ASSERT_EQ(this->m_codec.state(), TestCodec::FAILED);
        ASSERT_TRUE(status == DeltaCodec::CHUNK_CRC || status == DeltaCodec::BAD_OPCODE ||
                    status == DeltaCodec::TRUNCATED || status == DeltaCodec::PATCH_SIZE_MISMATCH)
            << "status " << status << " coder " << static_cast<int>(this->m_patch.coderId);
    }
    this->m_patchMedia.data[at] ^= mask;
    this->release();
    this->m_counters.patchCorruptions++;
}

// ----------------------------------------------------------------------
// Codec.MediaFault: one media call fails for one step; the engine either did
// not touch it or reports the I/O error without committing a wrong chunk
// ----------------------------------------------------------------------

bool DeltaRulesTester::Codec__MediaFault__precondition() const {
    return this->m_phase == RUNNING;
}

void DeltaRulesTester::Codec__MediaFault__action() {
    this->m_history += " MediaFault";
    const TestCodec::State before = this->m_codec.state();
    switch (STest::Pick::lowerUpper(0, 5)) {
        case 0:
            this->m_oldMedia.failReads = true;
            break;
        case 1:
            this->m_patchMedia.failReads = true;
            break;
        case 2:
            this->m_newMedia.failReads = true;
            break;
        case 3:
            this->m_newMedia.failWrites = true;
            break;
        case 4:
            this->m_newMedia.failFlush = true;
            break;
        default:
            this->m_newMedia.failRefresh = true;
            break;
    }
    const DeltaCodec::Status status = this->m_codec.step();
    this->m_oldMedia.failReads = this->m_patchMedia.failReads = this->m_newMedia.failReads = false;
    this->m_newMedia.failWrites = this->m_newMedia.failFlush = this->m_newMedia.failRefresh = false;
    this->m_counters.steps++;
    if (status == DeltaCodec::OP_OK) {
        ASSERT_NE(this->m_codec.state(), TestCodec::FAILED);
        if (before == TestCodec::VERIFY_RESUME && this->m_codec.state() != TestCodec::VERIFY_RESUME) {
            // By design an unreadable existing output ends the resume scan early and patching restarts
            // from that chunk (no error): the resume oracle shrinks to what was verified
            ASSERT_LE(this->m_codec.resumedChunks(), this->m_expectResumed);
            this->m_expectResumed = this->m_codec.resumedChunks();
        }
        this->checkCommittedPrefix();
        if (this->m_codec.state() == TestCodec::COMPLETE) {
            ASSERT_EQ(this->m_newMedia.data, this->m_patch.newImage);
            this->m_counters.completions++;
            this->release();
        }
        return;
    }
    ASSERT_EQ(this->m_codec.state(), TestCodec::FAILED);
    ASSERT_TRUE(status == DeltaCodec::READ_ERROR || status == DeltaCodec::WRITE_ERROR ||
                status == DeltaCodec::NEW_IMAGE_MISMATCH)
        << "status " << status;
    this->checkCommittedPrefix();
    this->release();
    this->m_counters.mediaFaults++;
}

// ----------------------------------------------------------------------
// Codec.WrongCoder: a valid patch for another coder is a configuration mismatch
// ----------------------------------------------------------------------

bool DeltaRulesTester::Codec__WrongCoder__precondition() const {
    return this->m_phase == IDLE;
}

void DeltaRulesTester::Codec__WrongCoder__action() {
    this->newPatch();
    ForeignCoder foreign(static_cast<U8>(this->m_coder.id() + STest::Pick::lowerUpper(1, 200)));
    this->m_codec.setCoder(foreign);
    ASSERT_EQ(this->m_codec.begin(this->m_oldMedia, this->m_patchMedia, this->m_newMedia), DeltaCodec::CODER_MISMATCH);
    ASSERT_TRUE(this->m_newMedia.data.empty());
    this->m_codec.reset();
    this->m_codec.setCoder(this->m_coder);
    this->m_counters.rejections++;
}

// ----------------------------------------------------------------------
// Codec.BadHeader: any header bit flip is BAD_HEADER (SIZE_WIDTH_MISMATCH only
// for the width byte), a short header is TRUNCATED, a changed old image is
// OLD_IMAGE_MISMATCH at verification; none writes output
// ----------------------------------------------------------------------

bool DeltaRulesTester::Codec__BadHeader__precondition() const {
    return this->m_phase == IDLE;
}

void DeltaRulesTester::Codec__BadHeader__action() {
    this->newPatch();
    switch (STest::Pick::lowerUpper(0, 2)) {
        case 0: {
            const FwSizeType at = STest::Pick::lowerUpper(0, static_cast<U32>(DeltaCodec::HEADER_SIZE - 1));
            this->m_patchMedia.data[at] ^= static_cast<U8>(1u << STest::Pick::lowerUpper(0, 7));
            const DeltaCodec::Status status =
                this->m_codec.begin(this->m_oldMedia, this->m_patchMedia, this->m_newMedia);
            if (at == DeltaCodec::HDR_SIZE_WIDTH) {
                ASSERT_TRUE(status == DeltaCodec::BAD_HEADER || status == DeltaCodec::SIZE_WIDTH_MISMATCH) << status;
            } else {
                ASSERT_EQ(status, DeltaCodec::BAD_HEADER) << "offset " << at;
            }
            break;
        }
        case 1:
            this->m_patchMedia.data.resize(STest::Pick::lowerUpper(0, static_cast<U32>(DeltaCodec::HEADER_SIZE - 1)));
            ASSERT_EQ(this->m_codec.begin(this->m_oldMedia, this->m_patchMedia, this->m_newMedia),
                      DeltaCodec::TRUNCATED);
            break;
        default: {
            if (this->m_oldMedia.data.empty()) {
                this->m_oldMedia.data.push_back(0);  // a longer old image: size mismatch
            } else {
                this->m_oldMedia.data[STest::Pick::lowerUpper(0, static_cast<U32>(this->m_oldMedia.data.size() - 1))] ^=
                    static_cast<U8>(1u << STest::Pick::lowerUpper(0, 7));
            }
            const DeltaCodec::Status bound =
                this->m_codec.begin(this->m_oldMedia, this->m_patchMedia, this->m_newMedia);
            if (bound == DeltaCodec::OP_OK) {
                U32 steps = 0;
                ASSERT_EQ(run(this->m_codec, steps), DeltaCodec::OLD_IMAGE_MISMATCH);
            } else {
                ASSERT_EQ(bound, DeltaCodec::OLD_IMAGE_MISMATCH);
            }
            break;
        }
    }
    ASSERT_TRUE(this->m_newMedia.data.empty());
    this->m_codec.reset();
    this->m_counters.rejections++;
}

// ----------------------------------------------------------------------
// Codec.BadPatchSize: a patch cut inside its chunks is TRUNCATED, one with
// trailing bytes is PATCH_SIZE_MISMATCH; committed chunks stay correct
// ----------------------------------------------------------------------

bool DeltaRulesTester::Codec__BadPatchSize__precondition() const {
    return this->m_phase == IDLE;
}

void DeltaRulesTester::Codec__BadPatchSize__action() {
    this->newPatch();
    std::vector<U8>& patch = this->m_patchMedia.data;
    DeltaCodec::Status expected = DeltaCodec::PATCH_SIZE_MISMATCH;
    if (this->m_patch.chunkCount() > 0 && STest::Pick::lowerUpper(0, 1) == 0) {
        patch.resize(
            STest::Pick::lowerUpper(static_cast<U32>(DeltaCodec::HEADER_SIZE), static_cast<U32>(patch.size() - 1)));
        expected = DeltaCodec::TRUNCATED;
    } else {
        const FwSizeType extra = STest::Pick::lowerUpper(1, 16);
        for (FwSizeType i = 0; i < extra; i++) {
            patch.push_back(static_cast<U8>(STest::Pick::lowerUpper(0, 255)));
        }
    }
    const DeltaCodec::Status bound = this->m_codec.begin(this->m_oldMedia, this->m_patchMedia, this->m_newMedia);
    DeltaCodec::Status status = bound;
    if (bound == DeltaCodec::OP_OK) {
        U32 steps = 0;
        status = run(this->m_codec, steps);
        this->checkCommittedPrefix();
    }
    ASSERT_EQ(status, expected) << "coder " << static_cast<int>(this->m_patch.coderId) << " chunks "
                                << this->m_patch.chunkCount();
    this->release();
    this->m_counters.rejections++;
}

}  // namespace Update
