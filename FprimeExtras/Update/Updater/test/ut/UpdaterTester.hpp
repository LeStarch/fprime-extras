// ======================================================================
// \title  UpdaterTester.hpp
// \author starchmd
// \brief  hpp file for Updater component test harness implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#ifndef Update_UpdaterTester_HPP
#define Update_UpdaterTester_HPP

#include "FprimeExtras/Update/Updater/Updater.hpp"
#include "FprimeExtras/Update/Updater/UpdaterGTestBase.hpp"
#include "FprimeExtras/Update/Updater/test/ut/TestState/TestState.hpp"
#include "TestUtils/RuleBasedTesting.hpp"

namespace Update {

class UpdaterTester final : public UpdaterGTestBase {
  public:
    // ----------------------------------------------------------------------
    // Constants
    // ----------------------------------------------------------------------

    //! Maximum size of histories storing events, telemetry, and port outputs
    static const FwSizeType MAX_HISTORY_SIZE = 10;

    //! Instance ID supplied to the component instance under test
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    //! Queue depth supplied to the component instance under test
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

  public:
    // ----------------------------------------------------------------------
    // Construction and destruction
    // ----------------------------------------------------------------------

    //! Construct object UpdaterTester
    UpdaterTester();

    //! Destroy object UpdaterTester
    ~UpdaterTester();

  private:
    // ----------------------------------------------------------------------
    // Test worker: synchronous ports return the shadow-selected status
    // ----------------------------------------------------------------------

    Update::UpdateStatus from_nextBoot_handler(FwIndexType portNum, const Update::NextBootMode& mode) override;

    Update::UpdateStatus from_confirmImage_handler(FwIndexType portNum) override;

  private:
    // ----------------------------------------------------------------------
    // Helper functions
    // ----------------------------------------------------------------------

    //! Connect ports
    void connectPorts();

    //! Initialize components
    void initComponents();

    //! Dispatch one queued message on the component and assert success
    void dispatch();

    //! Assert a command was rejected as BUSY without reaching the worker or emitting events
    void assertRejectedBusy(FwOpcodeType opCode, U32 cmdSeq);

    //! Assert the outstanding long-running command completed with the given response
    void assertPendingResponse(Fw::CmdResponse response);

  public:
    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! The component under test
    Updater component;

    //! Shadow state for rule-based testing
    UpdaterTestState shadow;

  public:
    // ----------------------------------------------------------------------
    // Rule Based Testing
    // ----------------------------------------------------------------------

    //! Rules for the CONFIGURE_NEXT_BOOT command
    FW_RBT_DEFINE_RULE(UpdaterTester, ConfigureNextBoot, Ok);
    FW_RBT_DEFINE_RULE(UpdaterTester, ConfigureNextBoot, Failed);
    FW_RBT_DEFINE_RULE(UpdaterTester, ConfigureNextBoot, Busy);

    //! Rules for the CONFIRM_UPDATE command
    FW_RBT_DEFINE_RULE(UpdaterTester, ConfirmUpdate, Ok);
    FW_RBT_DEFINE_RULE(UpdaterTester, ConfirmUpdate, Failed);
    FW_RBT_DEFINE_RULE(UpdaterTester, ConfirmUpdate, Busy);

    //! Rules for the PREPARE_UPDATE command and prepareImageDone port
    FW_RBT_DEFINE_RULE(UpdaterTester, PrepareUpdate, Start);
    FW_RBT_DEFINE_RULE(UpdaterTester, PrepareUpdate, Busy);
    FW_RBT_DEFINE_RULE(UpdaterTester, PrepareUpdate, DoneOk);
    FW_RBT_DEFINE_RULE(UpdaterTester, PrepareUpdate, DoneFailed);

    //! Rules for the UPDATE_IMAGE_FROM command and updateImageDone port
    FW_RBT_DEFINE_RULE(UpdaterTester, UpdateImage, Start);
    FW_RBT_DEFINE_RULE(UpdaterTester, UpdateImage, Busy);
    FW_RBT_DEFINE_RULE(UpdaterTester, UpdateImage, DoneOk);
    FW_RBT_DEFINE_RULE(UpdaterTester, UpdateImage, DoneFailed);
};

}  // namespace Update

#endif
