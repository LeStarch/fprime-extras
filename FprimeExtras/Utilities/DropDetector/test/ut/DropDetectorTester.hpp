// ======================================================================
// \title  DropDetectorTester.hpp
// \author starchmd
// \brief  hpp file for DropDetector component test harness implementation class
// ======================================================================

#ifndef Utilities_DropDetectorTester_HPP
#define Utilities_DropDetectorTester_HPP

#include "FprimeExtras/Utilities/DropDetector/DropDetector.hpp"
#include "FprimeExtras/Utilities/DropDetector/DropDetectorGTestBase.hpp"
#include <vector>


class DropDetectorTestHarness : public testing::Test {
  public:
    //! \brief set up test harness, file, etc
    void SetUp() override;

    //! \brief teardown test harness, file, etc
    void TearDown() override;

    U32 packets = 0;
    const char* TEST_FILE_NAME = "test_file.bin";
    FwSizeType TEST_PACKET_SIZE;
};

class NoDropHarness : public DropDetectorTestHarness {};


class DropHarness : public DropDetectorTestHarness {
  public:
    //! \brief set up test harness, file, etc
    void SetUp() override;

    //! \brief teardown test harness, file, etc
    std::vector<U32> drop_indices;

};

namespace Utilities {

class DropDetectorTester final : public DropDetectorGTestBase {
  public:
    // ----------------------------------------------------------------------
    // Constants
    // ----------------------------------------------------------------------

    // Maximum size of histories storing events, telemetry, and port outputs
    static const FwSizeType MAX_HISTORY_SIZE = 3000;

    // Instance ID supplied to the component instance under test
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

  public:
    // ----------------------------------------------------------------------
    // Construction and destruction
    // ----------------------------------------------------------------------

    //! Construct object DropDetectorTester
    DropDetectorTester();

    //! Destroy object DropDetectorTester
    ~DropDetectorTester();

  public:
    // ----------------------------------------------------------------------
    // Tests
    // ----------------------------------------------------------------------

    //! Test non-zero file (no drops)
    void test_no_drops(NoDropHarness& harness);

    //! Test file with drops
    void test_drops(DropHarness& harness);

  private:
    // ----------------------------------------------------------------------
    // Helper functions
    // ----------------------------------------------------------------------

    //! Connect ports
    void connectPorts();

    //! Initialize components
    void initComponents();

  private:
    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! The component under test
    DropDetector component;
};

}  // namespace Utilities

#endif
