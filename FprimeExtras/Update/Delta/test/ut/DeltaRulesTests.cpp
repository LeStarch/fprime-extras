// ======================================================================
// \title  DeltaRulesTests.cpp
// \author starchmd
// \brief  Rules-based scenarios over the shipped coders and the DeltaCodec
// engine (see DeltaRulesTester)
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include <gtest/gtest.h>

#include "FprimeExtras/Update/Delta/DeltaCoderLzss.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderNone.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderRle.hpp"
#include "FprimeExtras/Update/Delta/test/ut/DeltaRulesTester.hpp"
#include "STest/Scenario/BoundedScenario.hpp"
#include "STest/Scenario/RandomScenario.hpp"

using namespace Update;

// Random interleaving of every rule: patches begin, step, abort, resume over
// damaged output, meet corrupted patches and media faults, and are rejected
// for bad headers / sizes / coders — the reference image is the oracle
static void randomScenario(DeltaCoder& coder, U32 applications) {
    DeltaRulesTester tester(coder);
    DeltaRulesTester::Coder__RoundTrip roundTrip;
    DeltaRulesTester::Coder__Truncated truncated;
    DeltaRulesTester::Coder__Garbage garbage;
    DeltaRulesTester::Codec__Begin begin;
    DeltaRulesTester::Codec__Step step;
    DeltaRulesTester::Codec__Abort abort;
    DeltaRulesTester::Codec__CorruptOutput corruptOutput;
    DeltaRulesTester::Codec__CorruptPatch corruptPatch;
    DeltaRulesTester::Codec__MediaFault mediaFault;
    DeltaRulesTester::Codec__WrongCoder wrongCoder;
    DeltaRulesTester::Codec__BadHeader badHeader;
    DeltaRulesTester::Codec__BadPatchSize badPatchSize;
    // Step is listed many times so a bound patch usually progresses rather than
    // being aborted or faulted on its first tick
    STest::Rule<DeltaRulesTester>* rules[] = {
        &roundTrip, &truncated,     &garbage,      &begin,      &step,       &step,      &step,         &step,
        &step,      &step,          &step,         &step,       &step,       &step,      &step,         &step,
        &abort,     &corruptOutput, &corruptPatch, &mediaFault, &wrongCoder, &badHeader, &badPatchSize,
    };
    STest::RandomScenario<DeltaRulesTester> random("DeltaRandom", rules, FW_NUM_ARRAY_ELEMENTS(rules));
    STest::BoundedScenario<DeltaRulesTester> bounded("DeltaBounded", random, applications);
    const U32 steps = bounded.run(tester);
    printf("coder %u: ran %u rule applications\n", coder.id(), steps);
    tester.printCounters();
    EXPECT_GT(tester.counters().completions, applications / 1000);
    EXPECT_GT(tester.counters().resumes, applications / 3000);
    EXPECT_GT(tester.counters().roundTrips, applications / 100);
}

// ----------------------------------------------------------------------
// Static scenario: every deterministic rule once, in order, per coder
// ----------------------------------------------------------------------

static void staticScenario(DeltaCoder& coder) {
    DeltaRulesTester tester(coder);
    DeltaRulesTester::Static__CoderGolden coderGolden;
    DeltaRulesTester::Static__LzssHandCrafted lzssHandCrafted;
    DeltaRulesTester::Static__RleHandCrafted rleHandCrafted;
    DeltaRulesTester::Static__ApplyGolden applyGolden;
    DeltaRulesTester::Static__ResumeSkipsVerifiedChunks resumeSkipsVerifiedChunks;
    DeltaRulesTester::Static__ResumeCompleteOutput resumeCompleteOutput;
    DeltaRulesTester::Static__OutputStale outputStale;
    DeltaRulesTester::Static__RejectsBadHeader rejectsBadHeader;
    DeltaRulesTester::Static__RejectsPendingCoderState rejectsPendingCoderState;
    DeltaRulesTester::Static__RejectsSizeWidthMismatch rejectsSizeWidthMismatch;
    DeltaRulesTester::Static__RejectsOversizedGeometry rejectsOversizedGeometry;
    DeltaRulesTester::Static__RejectsOutOfRangeOperands rejectsOutOfRangeOperands;
    DeltaRulesTester::Static__RejectsConsecutiveSeeks rejectsConsecutiveSeeks;
    DeltaRulesTester::Static__VerifySpansSteps verifySpansSteps;
    DeltaRulesTester::Static__RejectsOldImageMismatch rejectsOldImageMismatch;
    DeltaRulesTester::Static__RejectsTruncatedAndOversizedPatch rejectsTruncatedAndOversizedPatch;
    DeltaRulesTester::Static__RejectsCorruptChunk rejectsCorruptChunk;
    DeltaRulesTester::Static__StagesIdempotentAfterDone stagesIdempotentAfterDone;
    DeltaRulesTester::Static__ResumeReentryAfterHeaderReadFailure resumeReentryAfterHeaderReadFailure;
    DeltaRulesTester::Static__MediaErrors mediaErrors;
    DeltaRulesTester::Static__ReadBackCatchesStorageFaults readBackCatchesStorageFaults;
    DeltaRulesTester::Static__ResetReturnsToIdle resetReturnsToIdle;
    DeltaRulesTester::Static__FailedChunkNeverCommitsOutput failedChunkNeverCommitsOutput;
    DeltaRulesTester::Static__IoBudgetSpansSteps ioBudgetSpansSteps;
    DeltaRulesTester::Static__ClassifiesShrunkMedia classifiesShrunkMedia;
    STest::Rule<DeltaRulesTester>* rules[] = {
        &coderGolden,
        &lzssHandCrafted,
        &rleHandCrafted,
        &applyGolden,
        &resumeSkipsVerifiedChunks,
        &resumeCompleteOutput,
        &outputStale,
        &rejectsBadHeader,
        &rejectsPendingCoderState,
        &rejectsSizeWidthMismatch,
        &rejectsOversizedGeometry,
        &rejectsOutOfRangeOperands,
        &rejectsConsecutiveSeeks,
        &verifySpansSteps,
        &rejectsOldImageMismatch,
        &rejectsTruncatedAndOversizedPatch,
        &rejectsCorruptChunk,
        &stagesIdempotentAfterDone,
        &resumeReentryAfterHeaderReadFailure,
        &mediaErrors,
        &readBackCatchesStorageFaults,
        &resetReturnsToIdle,
        &failedChunkNeverCommitsOutput,
        &ioBudgetSpansSteps,
        &classifiesShrunkMedia,
    };
    U32 applied = 0;
    for (FwSizeType i = 0; i < FW_NUM_ARRAY_ELEMENTS(rules); i++) {
        if (rules[i]->precondition(tester)) {
            rules[i]->apply(tester);
            applied++;
        }
    }
    printf("coder %u: applied %u static rules\n", coder.id(), applied);
    EXPECT_GE(applied,
              static_cast<U32>(FW_NUM_ARRAY_ELEMENTS(rules)) - 2);  // two hand-crafted rules are coder-specific
}

TEST(DeltaStatic, None) {
    DeltaCoderNone coder;
    staticScenario(coder);
}

TEST(DeltaStatic, Rle) {
    DeltaCoderRle coder;
    staticScenario(coder);
}

TEST(DeltaStatic, Lzss) {
    DeltaCoderLzss coder;
    staticScenario(coder);
}

static const U32 RANDOM_APPLICATIONS = 50000;

TEST(DeltaRules, RandomNone) {
    DeltaCoderNone coder;
    randomScenario(coder, RANDOM_APPLICATIONS);
}

TEST(DeltaRules, RandomRle) {
    DeltaCoderRle coder;
    randomScenario(coder, RANDOM_APPLICATIONS);
}

TEST(DeltaRules, RandomLzss) {
    DeltaCoderLzss coder;
    randomScenario(coder, RANDOM_APPLICATIONS);
}
