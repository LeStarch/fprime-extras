// ======================================================================
// \title  DropDetectorTester.cpp
// \author starchmd
// \brief  cpp file for DropDetector component test harness implementation class
// ======================================================================

#include "DropDetectorTester.hpp"
#include "STest/Pick/Pick.hpp"
#include "ExtrasConfig/DropDetectorConfig.hpp"
#include "Os/FileSystem.hpp"
#include <algorithm>

void DropDetectorTestHarness ::SetUp() {
    TEST_PACKET_SIZE = STest::Pick::lowerUpper(0, 2 * Utilities::DROP_DETECTOR_FILE_READ_BUFFER_SIZE);   
    Os::File file;
    ASSERT_EQ(file.open(this->TEST_FILE_NAME, Os::File::OPEN_CREATE, Os::FileInterface::OverwriteType::OVERWRITE), Os::File::OP_OK);

    this->packets = STest::Pick::lowerUpper(0, 2000);

    U8* buffer = new U8[this->TEST_PACKET_SIZE];
    // Fill buffer with non-zero data
    for (FwSizeType j = 0; j < this->TEST_PACKET_SIZE; j++) {
        buffer[j] = static_cast<U8>(STest::Pick::lowerUpper(1, 255));
    }
    for (U32 i = 0; i < packets; i++) {
        FwSizeType bytes_written = (i == packets - 1) ? STest::Pick::lowerUpper(0, this->TEST_PACKET_SIZE) : this->TEST_PACKET_SIZE;
        Os::File::Status status = file.write(buffer, bytes_written);
        ASSERT_EQ(status, Os::File::OP_OK);
    }
    delete[] buffer;
    file.close();
}

void DropDetectorTestHarness ::TearDown() {
    Os::FileSystem::removeFile(this->TEST_FILE_NAME);
}

void DropHarness ::SetUp() {
    DropDetectorTestHarness::SetUp();
    Os::File file;
    ASSERT_EQ(file.open(this->TEST_FILE_NAME, Os::File::OPEN_WRITE), Os::File::OP_OK);
    U8* zero_buffer = new U8[this->TEST_PACKET_SIZE];
    ::memset(zero_buffer, 0, this->TEST_PACKET_SIZE);

    for (U32 i = 0; i < this->packets; i++) {
        if (STest::Pick::lowerUpper(0, 9) != 0) {
            continue;
        }
        this->drop_indices.push_back(i + 1); // one-based index
        FwSizeType seekOffset = i * this->TEST_PACKET_SIZE;
        Os::File::Status status = file.seek(seekOffset, Os::File::ABSOLUTE);
        ASSERT_EQ(status, Os::File::OP_OK);
        FwSizeType bytes_written = this->TEST_PACKET_SIZE;
        status = file.write(zero_buffer, bytes_written);
        ASSERT_EQ(status, Os::File::OP_OK);
    }
    delete[] zero_buffer;
    file.close();
}

namespace Utilities {

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

DropDetectorTester ::DropDetectorTester()
    : DropDetectorGTestBase("DropDetectorTester", DropDetectorTester::MAX_HISTORY_SIZE), component("DropDetector") {
    this->initComponents();
    this->connectPorts();
}

DropDetectorTester ::~DropDetectorTester() {}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void DropDetectorTester ::test_drops(DropHarness& harness) {
    FwSizeType expected_drops = 0;
    Fw::String fileStr(harness.TEST_FILE_NAME);
    this->sendCmd_DETECT_DROPS(0, 0, fileStr, harness.TEST_PACKET_SIZE);
    ASSERT_EVENTS_DetectingDrops_SIZE(1);
    for (FwSizeType i = 0; i < harness.packets; i++) {
        this->invoke_to_schedIn(0, 0);
        if (std::find(harness.drop_indices.begin(), harness.drop_indices.end(), i + 1) != harness.drop_indices.end()) {
            expected_drops++;
            ASSERT_EVENTS_PossibleDrop(expected_drops - 1, i + 1);
        }
    }
    ASSERT_EVENTS_PossibleDrop_SIZE(expected_drops);
    ASSERT_EVENTS_FileReadError_SIZE(0);
    ASSERT_EVENTS_DetectingDropsCompleted_SIZE(1);
}

void DropDetectorTester ::test_no_drops(NoDropHarness& harness) {
    Fw::String fileStr(harness.TEST_FILE_NAME);
    this->sendCmd_DETECT_DROPS(0, 0, fileStr, harness.TEST_PACKET_SIZE);
    ASSERT_EVENTS_DetectingDrops_SIZE(1);
    for (FwSizeType i = 0; i < harness.packets; i++) {
        this->invoke_to_schedIn(0, 0);
    }
    ASSERT_EVENTS_PossibleDrop_SIZE(0);
    ASSERT_EVENTS_FileReadError_SIZE(0);
    ASSERT_EVENTS_DetectingDropsCompleted_SIZE(1);
}

}  // namespace Utilities
