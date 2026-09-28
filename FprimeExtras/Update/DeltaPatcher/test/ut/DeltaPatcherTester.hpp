// ======================================================================
// \title  DeltaPatcherTester.hpp
// \author starchmd
// \brief  hpp file for DeltaPatcher component test harness implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#ifndef Update_DeltaPatcherTester_HPP
#define Update_DeltaPatcherTester_HPP

#include <string>

#include "FprimeExtras/Update/DeltaPatcher/DeltaPatcher.hpp"
#include "FprimeExtras/Update/DeltaPatcher/DeltaPatcherGTestBase.hpp"

namespace Update {

class DeltaPatcherTester final : public DeltaPatcherGTestBase {
  public:
    // ----------------------------------------------------------------------
    // Constants
    // ----------------------------------------------------------------------

    // Maximum size of histories storing events, telemetry, and port outputs
    static const FwSizeType MAX_HISTORY_SIZE = 200;

    // Instance ID supplied to the component instance under test
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    // Queue depth supplied to the component instance under test
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    // Upper bound on rate-group ticks a test will drive before declaring a hang
    static constexpr U32 MAX_TICKS = 10000;

  public:
    // ----------------------------------------------------------------------
    // Construction and destruction
    // ----------------------------------------------------------------------

    //! Construct object DeltaPatcherTester
    DeltaPatcherTester();

    //! Destroy object DeltaPatcherTester
    ~DeltaPatcherTester();

  public:
    // ----------------------------------------------------------------------
    // Tests
    // ----------------------------------------------------------------------

    //! Patch completes, new file matches, command responds OK, patchComplete fires
    void nominal();

    //! A second APPLY_PATCH while patching is rejected BUSY
    void busy();

    //! Missing input files are rejected OPEN_FAILED
    void openFailed();

    //! Corrupt header is rejected BAD_HEADER at command time
    void badHeader();

    //! Wrong old image reports OldImageMismatch
    void oldImageMismatch();

    //! Corrupt chunk payload reports ChunkFailed and retains verified prefix
    void chunkFailed();

    //! ABORT_PATCH stops the patch; the next APPLY_PATCH resumes from verified chunks
    void abortAndResume();

    //! ABORT_PATCH when idle is a validation error
    void abortIdle();

  private:
    // ----------------------------------------------------------------------
    // Helper functions
    // ----------------------------------------------------------------------

    //! Connect ports
    void connectPorts();

    //! Initialize components
    void initComponents();

    //! Write bytes to a file
    static void writeFile(const std::string& path, const U8* data, FwSizeType size);

    //! Read a whole file
    static std::string readFile(const std::string& path);

    //! Read a little-endian U32 from a SPatch chunk header
    static U32 readLe32(const U8* data);

    //! Send APPLY_PATCH and tick `run` once so it is dispatched
    void apply(U32 cmdSeq);

    //! Tick `run` until a command response is recorded or MAX_TICKS elapse; returns ticks used
    U32 runUntilResponse();

  private:
    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! The component under test
    DeltaPatcher component;

    std::string m_dir;    //!< Scratch directory
    std::string m_old;    //!< Old image path
    std::string m_patch;  //!< Patch path
    std::string m_new;    //!< New image path
};

}  // namespace Update

#endif
