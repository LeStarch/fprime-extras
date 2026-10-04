// ======================================================================
// \title  DeltaPatcherTestMain.cpp
// \author starchmd
// \brief  cpp file for DeltaPatcher component test main function
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#include "DeltaPatcherTester.hpp"

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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
