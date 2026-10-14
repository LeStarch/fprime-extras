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
