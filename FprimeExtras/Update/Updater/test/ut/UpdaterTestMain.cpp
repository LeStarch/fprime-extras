// ======================================================================
// \title  UpdaterTestMain.cpp
// \author starchmd
// \brief  cpp file for Updater component test main function
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "FprimeExtras/Update/Updater/test/ut/UpdaterTester.hpp"
#include "Fw/Test/UnitTest.hpp"
#include "STest/Random/Random.hpp"
#include "STest/Scenario/BoundedScenario.hpp"
#include "STest/Scenario/RandomScenario.hpp"

using Update::UpdaterTester;

// Nominal flow from the SDD: prepare, update, test-boot, confirm
TEST(Updater, NominalUpdateSequence) {
    COMMENT("Prepare, update, configure TEST boot, then confirm the image");
    UpdaterTester tester;
    UpdaterTester::PrepareUpdate__Start prepare;
    UpdaterTester::PrepareUpdate__DoneOk prepareDone;
    UpdaterTester::UpdateImage__Start update;
    UpdaterTester::UpdateImage__DoneOk updateDone;
    UpdaterTester::ConfigureNextBoot__Ok configure;
    UpdaterTester::ConfirmUpdate__Ok confirm;
    prepare.apply(tester);
    prepareDone.apply(tester);
    update.apply(tester);
    updateDone.apply(tester);
    configure.apply(tester);
    confirm.apply(tester);
}

// Worker failures are reported as EXECUTION_ERROR and release the busy flag
TEST(Updater, WorkerFailures) {
    COMMENT("Every worker failure path responds EXECUTION_ERROR and leaves the component idle");
    UpdaterTester tester;
    UpdaterTester::PrepareUpdate__Start prepare;
    UpdaterTester::PrepareUpdate__DoneFailed prepareFailed;
    UpdaterTester::UpdateImage__Start update;
    UpdaterTester::UpdateImage__DoneFailed updateFailed;
    UpdaterTester::ConfigureNextBoot__Failed configureFailed;
    UpdaterTester::ConfirmUpdate__Failed confirmFailed;
    UpdaterTester::ConfirmUpdate__Ok confirm;
    prepare.apply(tester);
    prepareFailed.apply(tester);
    update.apply(tester);
    updateFailed.apply(tester);
    configureFailed.apply(tester);
    confirmFailed.apply(tester);
    confirm.apply(tester);
}

// All commands are rejected BUSY while a prepare is outstanding
TEST(Updater, BusyDuringPrepare) {
    COMMENT("All commands are rejected BUSY while PREPARE_UPDATE is outstanding");
    UpdaterTester tester;
    UpdaterTester::PrepareUpdate__Start prepare;
    UpdaterTester::PrepareUpdate__Busy prepareBusy;
    UpdaterTester::UpdateImage__Busy updateBusy;
    UpdaterTester::ConfigureNextBoot__Busy configureBusy;
    UpdaterTester::ConfirmUpdate__Busy confirmBusy;
    UpdaterTester::PrepareUpdate__DoneOk prepareDone;
    UpdaterTester::ConfigureNextBoot__Ok configure;
    prepare.apply(tester);
    prepareBusy.apply(tester);
    updateBusy.apply(tester);
    configureBusy.apply(tester);
    confirmBusy.apply(tester);
    prepareDone.apply(tester);
    configure.apply(tester);
}

// All commands are rejected BUSY while an update is outstanding
TEST(Updater, BusyDuringUpdate) {
    COMMENT("All commands are rejected BUSY while UPDATE_IMAGE_FROM is outstanding");
    UpdaterTester tester;
    UpdaterTester::UpdateImage__Start update;
    UpdaterTester::PrepareUpdate__Busy prepareBusy;
    UpdaterTester::UpdateImage__Busy updateBusy;
    UpdaterTester::ConfigureNextBoot__Busy configureBusy;
    UpdaterTester::ConfirmUpdate__Busy confirmBusy;
    UpdaterTester::UpdateImage__DoneFailed updateFailed;
    UpdaterTester::ConfirmUpdate__Ok confirm;
    update.apply(tester);
    prepareBusy.apply(tester);
    updateBusy.apply(tester);
    configureBusy.apply(tester);
    confirmBusy.apply(tester);
    updateFailed.apply(tester);
    confirm.apply(tester);
}

// Randomized test: apply rules in a random sequence for a large number of iterations
TEST(Updater, RandomizedTesting) {
    COMMENT("Apply all rules in a random order, checking the shadow model at every step");
    const U32 numRulesToApply = 10000;
    UpdaterTester tester;
    UpdaterTester::ConfigureNextBoot__Ok configureOk;
    UpdaterTester::ConfigureNextBoot__Failed configureFailed;
    UpdaterTester::ConfigureNextBoot__Busy configureBusy;
    UpdaterTester::ConfirmUpdate__Ok confirmOk;
    UpdaterTester::ConfirmUpdate__Failed confirmFailed;
    UpdaterTester::ConfirmUpdate__Busy confirmBusy;
    UpdaterTester::PrepareUpdate__Start prepareStart;
    UpdaterTester::PrepareUpdate__Busy prepareBusy;
    UpdaterTester::PrepareUpdate__DoneOk prepareDoneOk;
    UpdaterTester::PrepareUpdate__DoneFailed prepareDoneFailed;
    UpdaterTester::UpdateImage__Start updateStart;
    UpdaterTester::UpdateImage__Busy updateBusy;
    UpdaterTester::UpdateImage__DoneOk updateDoneOk;
    UpdaterTester::UpdateImage__DoneFailed updateDoneFailed;

    STest::Rule<UpdaterTester>* rules[] = {
        &configureOk, &configureFailed, &configureBusy, &confirmOk,        &confirmFailed,
        &confirmBusy, &prepareStart,    &prepareBusy,   &prepareDoneOk,    &prepareDoneFailed,
        &updateStart, &updateBusy,      &updateDoneOk,  &updateDoneFailed,
    };

    STest::RandomScenario<UpdaterTester> random("Random Rules", rules, FW_NUM_ARRAY_ELEMENTS(rules));
    STest::BoundedScenario<UpdaterTester> bounded("Bounded Random Rules Scenario", random, numRulesToApply);
    const U32 numSteps = bounded.run(tester);
    printf("Ran %u steps.\n", numSteps);
}

int main(int argc, char** argv) {
    STest::Random::seed();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
