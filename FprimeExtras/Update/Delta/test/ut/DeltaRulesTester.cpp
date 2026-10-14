// ======================================================================
// \title  DeltaRulesTester.cpp
// \author starchmd
// \brief  Shared helpers of the Delta rules-based tests
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include "FprimeExtras/Update/Delta/test/ut/DeltaRulesTester.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>

namespace Update {

DeltaRulesTester::DeltaRulesTester(DeltaCoder& coder)
    : m_coder(coder),
      m_codec(coder),
      m_phase(IDLE),
      m_haveOutput(false),
      m_expectResumed(0),
      m_existingAtBegin(0),
      m_stepsThisPatch(0) {
    memset(&this->m_counters, 0, sizeof this->m_counters);
}

FwSizeType DeltaRulesTester::pickSize() {
    switch (STest::Pick::lowerUpper(0, 5)) {
        case 0:
            return 0;
        case 1:
            return STest::Pick::lowerUpper(1, 16);
        case 2:
            return STest::Pick::lowerUpper(DELTA_PATCH_BUFFER_SIZE - 2, DELTA_PATCH_BUFFER_SIZE + 2);
        case 3:
            return STest::Pick::lowerUpper(DELTA_WINDOW_SIZE - 2, DELTA_WINDOW_SIZE + 2);
        default:
            return STest::Pick::lowerUpper(1, 3000);
    }
}

void DeltaRulesTester::newPatch() {
    const U8 coderId = this->m_coder.id();
    FwSizeType chunkBytes = 0;
    switch (STest::Pick::lowerUpper(0, 3)) {
        case 0:
            chunkBytes = STest::Pick::lowerUpper(1, 8);
            break;
        case 1:
            chunkBytes = STest::Pick::lowerUpper(DELTA_OUTPUT_BUFFER_SIZE - 1, DELTA_OUTPUT_BUFFER_SIZE + 1);
            break;
        case 2:
            chunkBytes = STest::Pick::lowerUpper(DELTA_WINDOW_SIZE - 1, DELTA_WINDOW_SIZE + 1);
            break;
        default:
            chunkBytes = STest::Pick::lowerUpper(1, 1024);
            break;
    }
    this->m_patch = TestEncoders::buildRandomPatch(coderId, pickSize(), pickSize(), chunkBytes);
    this->m_codec.setCoder(this->m_coder);
    this->m_oldMedia.data = this->m_patch.oldImage;
    this->m_patchMedia.data = this->m_patch.patch;
    this->m_newMedia.data.clear();
    this->m_haveOutput = false;
    this->m_history.clear();
}

DeltaCodec::Status DeltaRulesTester::bind() {
    this->m_codec.setCoder(this->m_coder);
    this->m_oldMedia.data = this->m_patch.oldImage;
    this->m_patchMedia.data = this->m_patch.patch;
    this->m_oldMedia.failReads = this->m_patchMedia.failReads = this->m_newMedia.failReads = false;
    this->m_newMedia.failWrites = this->m_newMedia.failFlush = this->m_newMedia.failRefresh = false;
    return this->m_codec.begin(this->m_oldMedia, this->m_patchMedia, this->m_newMedia);
}

FwSizeType DeltaRulesTester::expectedResumedChunks() const {
    const std::vector<U8>& have = this->m_newMedia.data;
    const std::vector<U8>& want = this->m_patch.newImage;
    const FwSizeType chunkBytes = this->m_patch.chunkBytes;
    // Mirrors DeltaCodec::begin(): a complete-size output has every chunk (the
    // last one possibly short) verified, otherwise only the whole chunks present
    const FwSizeType verify = (have.size() == want.size()) ? this->m_patch.chunkCount() : have.size() / chunkBytes;
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

void DeltaRulesTester::checkCommittedPrefix() const {
    const FwSizeType newSize = this->m_patch.newImage.size();
    FwSizeType committed = this->m_codec.chunkIndex() * this->m_patch.chunkBytes;
    if (committed > newSize) {
        committed = newSize;
    }
    ASSERT_LE(this->m_codec.chunkIndex(), this->m_codec.chunkCount());
    ASSERT_LE(this->m_codec.bytesWritten(), newSize);
    ASSERT_GE(this->m_newMedia.data.size(), committed);
    if (committed > 0) {
        ASSERT_EQ(memcmp(this->m_newMedia.data.data(), this->m_patch.newImage.data(), static_cast<size_t>(committed)),
                  0);
    }
}

void DeltaRulesTester::release() {
    this->m_codec.reset();
    this->m_phase = IDLE;
    this->m_haveOutput = !this->m_newMedia.data.empty();
}

static void writeBytes(const char* prefix, const char* suffix, const std::vector<U8>& data) {
    char path[256];
    snprintf(path, sizeof path, "%s_%s.bin", prefix, suffix);
    FILE* f = fopen(path, "wb");
    if (f != nullptr) {
        if (!data.empty()) {
            (void)fwrite(data.data(), 1, data.size(), f);
        }
        (void)fclose(f);
    }
}

void DeltaRulesTester::dumpCase(const char* prefix) const {
    writeBytes(prefix, "old", this->m_patch.oldImage);
    writeBytes(prefix, "patch", this->m_patchMedia.data);
    writeBytes(prefix, "new", this->m_patch.newImage);
    writeBytes(prefix, "out", this->m_newMedia.data);
    writeBytes(prefix, "outatbegin", this->m_savedOutput);
    printf("dumped case to %s_*.bin (coder %u chunkBytes %u) history: %s\n", prefix, this->m_patch.coderId,
           static_cast<unsigned>(this->m_patch.chunkBytes), this->m_history.c_str());
}

void DeltaRulesTester::printCounters() const {
    const Counters& c = this->m_counters;
    printf(
        "Delta rules: coder round-trips %u, truncated %u, garbage %u | codec begins %u (resumes %u), steps %u, "
        "completions %u, aborts %u, output corruptions %u (stale %u), patch corruptions %u, media faults %u, "
        "rejections %u\n",
        c.roundTrips, c.truncatedStreams, c.garbageStreams, c.begins, c.resumes, c.steps, c.completions, c.aborts,
        c.outputCorruptions, c.staleOutputs, c.patchCorruptions, c.mediaFaults, c.rejections);
}

}  // namespace Update
