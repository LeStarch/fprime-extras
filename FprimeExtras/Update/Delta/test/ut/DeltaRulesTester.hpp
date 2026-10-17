// ======================================================================
// \title  DeltaRulesTester.hpp
// \author starchmd
// \brief  Rules-based test state for the shipped DeltaCoders and the
// DeltaCodec engine: random valid patches, aborts, corrupted output and
// patches, media faults, with the reference image as the oracle
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_Test_DeltaRulesTester_HPP
#define Update_Delta_Test_DeltaRulesTester_HPP

#include <string>
#include <vector>

#include "FprimeExtras/Update/Delta/DeltaCoder.hpp"
#include "FprimeExtras/Update/Delta/test/ut/DeltaTestEncoders.hpp"
#include "FprimeExtras/Update/Delta/test/ut/DeltaTestSupport.hpp"
#include "TestUtils/RuleBasedTesting.hpp"

namespace Update {

//! A coder that is not the one a patch was built for: reports another id and
//! decodes nothing, so binding it must be answered CODER_MISMATCH
class ForeignCoder final : public DeltaCoder {
  public:
    explicit ForeignCoder(U8 id) : m_id(id) {}
    U8 id() const override { return this->m_id; }
    void reset() override {}
    Status decode(const U8* in, FwSizeType inLen, FwSizeType& consumed, DeltaWindow& window) override {
        consumed = 0;
        return MALFORMED;
    }
    bool pending() const override { return false; }

  private:
    const U8 m_id;
};

//! Rules over one DeltaCoder (the coder under test) and the DeltaCodec engine
//! driving it. The same rule set runs against every shipped coder.
class DeltaRulesTester {
  public:
    //! Where the shadow lifecycle stands: nothing bound, or a patch under way
    enum Phase { IDLE, RUNNING };

    //! Counts of what the random walk exercised, printed by the scenario
    struct Counters {
        U32 roundTrips;
        U32 truncatedStreams;
        U32 garbageStreams;
        U32 begins;
        U32 resumes;
        U32 completions;
        U32 aborts;
        U32 outputCorruptions;
        U32 staleOutputs;
        U32 patchCorruptions;
        U32 mediaFaults;
        U32 rejections;
        U32 steps;
    };

    //! Rules operate on `coder`, whose id() names the patches that are built
    explicit DeltaRulesTester(DeltaCoder& coder);

    // Shipped coders, stream level
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Coder, RoundTrip);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Coder, Truncated);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Coder, Garbage);

    // Engine lifecycle
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Codec, Begin);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Codec, Step);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Codec, Abort);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Codec, CorruptOutput);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Codec, CorruptPatch);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Codec, MediaFault);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Codec, WrongCoder);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Codec, BadHeader);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Codec, BadPatchSize);

    // Static rules (Rules/StaticRules.cpp): the former direct unit tests, one
    // deterministic rule each, run in sequence against every shipped coder
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, CoderGolden);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, LzssHandCrafted);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RleHandCrafted);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, ApplyGolden);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, ResumeSkipsVerifiedChunks);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, ResumeCompleteOutput);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, OutputStale);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsBadHeader);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsPendingCoderState);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsSizeWidthMismatch);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsOversizedGeometry);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsOutOfRangeOperands);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsConsecutiveSeeks);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, VerifySpansSteps);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsOldImageMismatch);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsTruncatedAndOversizedPatch);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsCorruptChunk);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, StagesIdempotentAfterDone);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, ResumeReentryAfterHeaderReadFailure);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, MediaErrors);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, ReadBackCatchesStorageFaults);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, ResetReturnsToIdle);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, FailedChunkNeverCommitsOutput);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, IoBudgetSpansSteps);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, ClassifiesShrunkMedia);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsWrongImageCrc);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, MediaSizeFaults);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, PatchReadFailsMidChunk);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsShortChunk);
    FW_RBT_DEFINE_RULE(DeltaRulesTester, Static, RejectsMalformedVarint);

    //! Golden vectors for the coder under test (DeltaTestVectors.hpp)
    const U8* goldenPatch() const;
    FwSizeType goldenPatchSize() const;
    const U8* goldenPatchOtherWidth() const;
    FwSizeType goldenPatchOtherWidthSize() const;
    const U8* goldenCoded() const;
    FwSizeType goldenCodedSize() const;

    const Counters& counters() const { return this->m_counters; }
    //! Write old image, patch, expected and actual output to <prefix>_{old,patch,new,out}.bin for offline analysis
    void dumpCase(const char* prefix) const;
    void printCounters() const;

  private:
    //! Replace the bound patch with a fresh random one over a fresh old image, dropping any output
    void newPatch();
    //! Bind the current patch/old/new media to the engine; Begin and the rejection rules share it
    DeltaCodec::Status bind();
    //! Leading whole chunks of the existing output that match the new image: what a resume may skip
    FwSizeType expectedResumedChunks() const;
    //! Committed output (whole chunks up to chunkIndex) must equal the reference image
    void checkCommittedPrefix() const;
    //! The engine stopped (failed or was abandoned): forget it, keep the output for a resume
    void release();
    //! Draw image geometry: sizes that straddle the buffer, window and chunk boundaries
    static FwSizeType pickSize();

    DeltaCoder& m_coder;
    TestCodec m_codec;
    MemoryMedia m_oldMedia;
    MemoryMedia m_patchMedia;
    MemoryMedia m_newMedia;
    TestEncoders::RandomPatch m_patch;
    Phase m_phase;
    bool m_haveOutput;              //!< m_newMedia holds (possibly partial) output of m_patch
    FwSizeType m_expectResumed;     //!< Oracle for resumedChunks at the next completion
    FwSizeType m_existingAtBegin;   //!< Output size when the current patch was bound (diagnostics)
    std::vector<U8> m_savedOutput;  //!< Output content when the current patch was bound (diagnostics)
    std::string m_history;          //!< Rules applied to the current patch since it was built (diagnostics)
    U32 m_stepsThisPatch;
    Counters m_counters;
};

}  // namespace Update

#endif
