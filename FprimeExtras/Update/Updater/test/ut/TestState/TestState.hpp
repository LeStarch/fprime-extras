// ======================================================================
// \title  TestState.hpp
// \author starchmd
// \brief  Shadow state model for Updater rule-based testing
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#ifndef Update_Updater_TestState_HPP
#define Update_Updater_TestState_HPP

#include "FprimeExtras/Update/Updater/Updater.hpp"
#include "Fw/Types/String.hpp"

namespace Update {

//! Shadow of the Updater busy flag and the command awaiting a worker "done" response
class UpdaterTestState {
  public:
    //! Long-running operation outstanding on the worker
    enum class Pending { NONE, PREPARE, UPDATE };

    //! Outstanding operation (NONE when the component is idle)
    Pending shadow_pending = Pending::NONE;

    //! Opcode of the outstanding command
    FwOpcodeType shadow_pendingOpCode = 0;

    //! Sequence number of the outstanding command
    U32 shadow_pendingCmdSeq = 0;

    //! Status the test worker returns from the synchronous nextBoot / confirmImage ports
    Update::UpdateStatus shadow_workerReturn = Update::UpdateStatus::OP_OK;

  public:
    //! True when a long-running operation is outstanding
    bool shadow_isBusy() const;

    //! Record that a long-running operation was started
    void shadow_start(Pending pending, FwOpcodeType opCode, U32 cmdSeq);

    //! Record that the outstanding operation finished
    void shadow_finish();

    //! Pick a random UpdateStatus other than OP_OK
    static Update::UpdateStatus pickFailureStatus();

    //! Pick a random NextBootMode
    static Update::NextBootMode pickBootMode();

    //! Fill file with a random printable path that fits in a command string argument
    static void pickFileName(Fw::String& file);
};

}  // namespace Update

#endif
