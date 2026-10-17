// ======================================================================
// \title  DeltaPatcherTestMain.cpp
// \author starchmd
// \brief  cpp file for DeltaPatcher component test main function
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "DeltaPatcherTester.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderLzss.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderNone.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderRle.hpp"
#include "STest/Scenario/BoundedScenario.hpp"
#include "STest/Scenario/RandomScenario.hpp"

TEST(Nominal, Nominal) {
    Update::DeltaPatcherTester tester;
    tester.nominal();
}

TEST(Nominal, AbortAndResume) {
    Update::DeltaPatcherTester tester;
    tester.abortAndResume();
}

TEST(OffNominal, Busy) {
    Update::DeltaPatcherTester tester;
    tester.busy();
}

TEST(OffNominal, OpenFailed) {
    Update::DeltaPatcherTester tester;
    tester.openFailed();
}

TEST(OffNominal, BadHeader) {
    Update::DeltaPatcherTester tester;
    tester.badHeader();
}

TEST(OffNominal, CoderMismatch) {
    Update::DeltaPatcherTester tester;
    tester.coderMismatch();
}

TEST(OffNominal, SameFileAliased) {
    Update::DeltaPatcherTester tester;
    tester.sameFileAliased();
}

TEST(OffNominal, SizeWidthMismatch) {
    Update::DeltaPatcherTester tester;
    tester.sizeWidthMismatch();
}

TEST(OffNominal, OldImageMismatch) {
    Update::DeltaPatcherTester tester;
    tester.oldImageMismatch();
}

TEST(OffNominal, ChunkFailed) {
    Update::DeltaPatcherTester tester;
    tester.chunkFailed();
}

TEST(OffNominal, AbortDuringVerify) {
    Update::DeltaPatcherTester tester;
    tester.abortDuringVerify();
}

TEST(OffNominal, SignalOverflow) {
    Update::DeltaPatcherTester tester;
    tester.signalOverflow();
}

TEST(OffNominal, StartDropped) {
    Update::DeltaPatcherTester tester;
    tester.startDropped();
}

TEST(OffNominal, HookAnswersDroppedAbort) {
    Update::DeltaPatcherTester tester;
    tester.hookAnswersDroppedAbort();
}

TEST(OffNominal, AbortOvertakenByCompletion) {
    Update::DeltaPatcherTester tester;
    tester.abortOvertakenByCompletion();
}

TEST(OffNominal, AbortIdle) {
    Update::DeltaPatcherTester tester;
    tester.abortIdle();
}

TEST(OffNominal, QueueOverflow) {
    Update::DeltaPatcherTester tester;
    tester.queueOverflow();
}

TEST(OffNominal, SameFile) {
    Update::DeltaPatcherTester tester;
    tester.sameFile();
}

TEST(OffNominal, SameFileHardLink) {
    Update::DeltaPatcherTester tester;
    tester.sameFileHardLink();
}

TEST(OffNominal, SpecialFiles) {
    Update::DeltaPatcherTester tester;
    tester.specialFiles();
}

TEST(OffNominal, BadHeaderLeavesNoOutput) {
    Update::DeltaPatcherTester tester;
    tester.badHeaderLeavesNoOutput();
}

TEST(OffNominal, OutputReplacedBeforeVerify) {
    Update::DeltaPatcherTester tester;
    tester.outputReplacedBeforeVerify();
}

// ----------------------------------------------------------------------
// Rules-based random walk, once per shipped coder
// ----------------------------------------------------------------------

static void patcherRandomScenario(Update::DeltaCoder& coder, U32 applications) {
    Update::DeltaPatcherTester tester(&coder);
    Update::DeltaPatcherTester::Patcher__Apply apply;
    Update::DeltaPatcherTester::Patcher__Tick tick;
    Update::DeltaPatcherTester::Patcher__ApplyWhileBusy applyWhileBusy;
    Update::DeltaPatcherTester::Patcher__Abort abort;
    Update::DeltaPatcherTester::Patcher__AbortIdle abortIdle;
    Update::DeltaPatcherTester::Patcher__CorruptPatch corruptPatch;
    Update::DeltaPatcherTester::Patcher__CorruptOutput corruptOutput;
    Update::DeltaPatcherTester::Patcher__OldMismatch oldMismatch;
    Update::DeltaPatcherTester::Patcher__MissingInput missingInput;
    Update::DeltaPatcherTester::Patcher__SameFile sameFile;
    Update::DeltaPatcherTester::Patcher__QueueFlood queueFlood;
    // Tick dominates so a bound patch usually progresses before it is aborted or flooded
    STest::Rule<Update::DeltaPatcherTester>* rules[] = {
        &apply,       &tick,         &tick,           &tick,       &tick,      &tick,         &tick,
        &tick,        &tick,         &tick,           &tick,       &tick,      &tick,         &tick,
        &tick,        &tick,         &applyWhileBusy, &abort,      &abortIdle, &corruptPatch, &corruptOutput,
        &oldMismatch, &missingInput, &sameFile,       &queueFlood,
    };
    STest::RandomScenario<Update::DeltaPatcherTester> random("PatcherRandom", rules, FW_NUM_ARRAY_ELEMENTS(rules));
    STest::BoundedScenario<Update::DeltaPatcherTester> bounded("PatcherBounded", random, applications);
    const U32 steps = bounded.run(tester);
    printf("coder %u: ran %u rule applications\n", coder.id(), steps);
    tester.printRuleCounters();
    EXPECT_GT(tester.ruleCounters().completions, applications / 200);
    EXPECT_GT(tester.ruleCounters().aborts, 0u);
    EXPECT_GT(tester.ruleCounters().resumes, 0u);
}

static const U32 PATCHER_RANDOM_APPLICATIONS = 20000;

TEST(Rules, RandomNone) {
    Update::DeltaCoderNone coder;
    patcherRandomScenario(coder, PATCHER_RANDOM_APPLICATIONS);
}

TEST(Rules, RandomRle) {
    Update::DeltaCoderRle coder;
    patcherRandomScenario(coder, PATCHER_RANDOM_APPLICATIONS);
}

TEST(Rules, RandomLzss) {
    Update::DeltaCoderLzss coder;
    patcherRandomScenario(coder, PATCHER_RANDOM_APPLICATIONS);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
