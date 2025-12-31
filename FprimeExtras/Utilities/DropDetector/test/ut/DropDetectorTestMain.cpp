// ======================================================================
// \title  DropDetectorTestMain.cpp
// \author starchmd
// \brief  cpp file for DropDetector component test main function
// ======================================================================

#include "DropDetectorTester.hpp"
#include "STest/Random/Random.hpp"

TEST_F(NoDropHarness, NoDrops) {
    Utilities::DropDetectorTester tester;
    tester.test_no_drops(*this);
}

TEST_F(DropHarness, Drops) {
    Utilities::DropDetectorTester tester;
    tester.test_drops(*this);
}

int main(int argc, char** argv) {
    STest::Random::seed();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
