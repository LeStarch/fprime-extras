// ======================================================================
// \title  DeltaPatcher.hpp
// \author starchmd
// \brief  hpp file for DeltaPatcher component implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#ifndef Update_DeltaPatcher_HPP
#define Update_DeltaPatcher_HPP

#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderLzss.hpp"
#include "FprimeExtras/Update/DeltaPatcher/DeltaPatcherComponentAc.hpp"

namespace Update {

//! \brief Applies SPatch delta files to on-board images, one bounded unit of
//! work per rate-group tick
//!
//! The lifecycle is the DeltaPatchMachine state machine; its `tick` actions
//! delegate the work to the DeltaCodec and its transitions report events,
//! telemetry and the deferred APPLY_PATCH response. All handlers and actions
//! execute on the caller of `run` (the queue is dispatched there), so no
//! locking is needed and the engine's fixed buffers are the only patch-time
//! storage. The shipped LZSS coder is used unless a project supplies its own
//! via `setCoder()` during topology setup, before rate groups start.
class DeltaPatcher final : public DeltaPatcherComponentBase {
    friend class DeltaPatcherTester;

  public:
    //! Upper bound on queued messages dispatched per drain; `run` drains twice
    //! (before and after sending `tick`)
    static constexpr FwSizeType MAX_DISPATCH_PER_TICK = 4;

    //! Construct DeltaPatcher object
    DeltaPatcher(const char* const compName  //!< The component name
    );

    //! Initialize; asserts the queue holds one drain of commands plus the
    //! state-machine signals of a tick (2 * MAX_DISPATCH_PER_TICK)
    void init(FwSizeType queueDepth,        //!< The queue depth
              FwEnumStoreType instance = 0  //!< The instance number
    );

    //! Destroy DeltaPatcher object
    ~DeltaPatcher();

    //! \brief Substitute the decompression coder used to decode SPatch payloads
    //! (project plugin seam)
    //!
    //! Call during topology setup, before rate groups start (FW_ASSERTs if a
    //! patch is in progress); the coder must outlive this component.
    void setCoder(DeltaCoder& coder);

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for run
    //!
    //! Rate-group tick: drains the queue, sends `tick` to the state machine, and
    //! drains the queue again
    void run_handler(FwIndexType portNum,  //!< The port number
                     U32 context           //!< The call order
                     ) override;

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command APPLY_PATCH
    //!
    //! Start applying patch_file to old_file writing new_file. Resumes if
    //! new_file already holds verified chunks. The command completes (OK or
    //! EXECUTION_ERROR) when the patch finishes or fails.
    void APPLY_PATCH_cmdHandler(FwOpcodeType opCode,                 //!< The opcode
                                U32 cmdSeq,                          //!< The command sequence number
                                const Fw::CmdStringArg& old_file,    //!< Existing image (never modified)
                                const Fw::CmdStringArg& patch_file,  //!< Uplinked .spatch file
                                const Fw::CmdStringArg& new_file     //!< Output image (created or extended)
                                ) override;

    //! Handler implementation for command ABORT_PATCH
    //!
    //! Abort an in-progress patch; the partial new_file is retained for later
    //! resume
    void ABORT_PATCH_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                U32 cmdSeq            //!< The command sequence number
                                ) override;

    //! Queue-full hook for APPLY_PATCH: respond BUSY on the caller's thread so
    //! the dispatcher never waits
    void APPLY_PATCH_cmdOverflowHook(FwOpcodeType opCode, U32 cmdSeq) override;

    //! Queue-full hook for ABORT_PATCH: respond BUSY on the caller's thread so
    //! the dispatcher never waits
    void ABORT_PATCH_cmdOverflowHook(FwOpcodeType opCode, U32 cmdSeq) override;

  private:
    // ----------------------------------------------------------------------
    // Implementations for internal state machine actions
    // ----------------------------------------------------------------------

    //! Implementation for action doVerifyResume of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Delegate one unit of resume verification to DeltaCodec::verifyResume()
    void Update_DeltaPatchMachine_action_doVerifyResume(SmId smId,  //!< The state machine id
                                                        Update_DeltaPatchMachine::Signal signal  //!< The signal
                                                        ) override;

    //! Implementation for action doVerifyOld of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Delegate one unit of old-image verification to DeltaCodec::verifyOld()
    void Update_DeltaPatchMachine_action_doVerifyOld(SmId smId,                               //!< The state machine id
                                                     Update_DeltaPatchMachine::Signal signal  //!< The signal
                                                     ) override;

    //! Implementation for action doPatchChunk of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Delegate one chunk (or I/O-budget slice) to DeltaCodec::patchChunk()
    void Update_DeltaPatchMachine_action_doPatchChunk(SmId smId,                               //!< The state machine id
                                                      Update_DeltaPatchMachine::Signal signal  //!< The signal
                                                      ) override;

    //! Implementation for action doVerifyFinal of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Delegate one unit of read-back verification to DeltaCodec::verifyFinal()
    void Update_DeltaPatchMachine_action_doVerifyFinal(SmId smId,  //!< The state machine id
                                                       Update_DeltaPatchMachine::Signal signal  //!< The signal
                                                       ) override;

    //! Implementations for the per-state entry actions enterIdle / enterVerifyResume /
    //! enterVerifyOld / enterPatching / enterVerifyFinal of state machine
    //! Update_DeltaPatchMachine: each announces its state through reportState()
    void Update_DeltaPatchMachine_action_enterIdle(SmId smId, Update_DeltaPatchMachine::Signal signal) override;
    void Update_DeltaPatchMachine_action_enterVerifyResume(SmId smId, Update_DeltaPatchMachine::Signal signal) override;
    void Update_DeltaPatchMachine_action_enterVerifyOld(SmId smId, Update_DeltaPatchMachine::Signal signal) override;
    void Update_DeltaPatchMachine_action_enterPatching(SmId smId, Update_DeltaPatchMachine::Signal signal) override;
    void Update_DeltaPatchMachine_action_enterVerifyFinal(SmId smId, Update_DeltaPatchMachine::Signal signal) override;

    //! Implementation for action reportResumed of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Emit PatchResumed when verified chunks were skipped
    void Update_DeltaPatchMachine_action_reportResumed(SmId smId,  //!< The state machine id
                                                       Update_DeltaPatchMachine::Signal signal  //!< The signal
                                                       ) override;

    //! Implementation for action reportComplete of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Emit PatchComplete, invoke patchComplete, answer APPLY_PATCH OK
    void Update_DeltaPatchMachine_action_reportComplete(SmId smId,  //!< The state machine id
                                                        Update_DeltaPatchMachine::Signal signal  //!< The signal
                                                        ) override;

    //! Implementation for action reportFailed of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Emit OldImageMismatch or ChunkFailed, answer APPLY_PATCH EXECUTION_ERROR
    void Update_DeltaPatchMachine_action_reportFailed(SmId smId,  //!< The state machine id
                                                      Update_DeltaPatchMachine::Signal signal,  //!< The signal
                                                      const Update::DeltaPatchStatus& value     //!< The value
                                                      ) override;

    //! Implementation for action reportAborted of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Emit PatchAborted, answer APPLY_PATCH EXECUTION_ERROR and ABORT_PATCH OK
    void Update_DeltaPatchMachine_action_reportAborted(SmId smId,  //!< The state machine id
                                                       Update_DeltaPatchMachine::Signal signal,  //!< The signal
                                                       const Update::DeltaAbortRequest& value    //!< The value
                                                       ) override;

    //! Implementation for action reportAbortIgnored of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Answer an ABORT_PATCH whose patch had already ended when the signal
    //! arrived
    void Update_DeltaPatchMachine_action_reportAbortIgnored(SmId smId,  //!< The state machine id
                                                            Update_DeltaPatchMachine::Signal signal,  //!< The signal
                                                            const Update::DeltaAbortRequest& value    //!< The value
                                                            ) override;

    //! Implementation for action doRelease of state machine
    //! Update_DeltaPatchMachine
    //!
    //! Close the media and forget the codec progress
    void Update_DeltaPatchMachine_action_doRelease(SmId smId,                               //!< The state machine id
                                                   Update_DeltaPatchMachine::Signal signal  //!< The signal
                                                   ) override;

  private:
    // ----------------------------------------------------------------------
    // Implementations for internal state machine guards
    // ----------------------------------------------------------------------

    //! Overflow hook for deltaPatchMachine: a signal found the component queue
    //! full of commands
    void deltaPatchMachine_stateMachineOverflowHook(SmId smId,                    //!< The state machine id
                                                    FwEnumStoreType signal,       //!< The signal
                                                    Fw::SerialBufferBase& buffer  //!< The message buffer
                                                    ) override;

    //! Implementation for guard hasOutputToVerify of state machine
    //! Update_DeltaPatchMachine
    //!
    //! True when the existing new file holds whole chunks to CRC-verify before
    //! patching (resume)
    bool Update_DeltaPatchMachine_guard_hasOutputToVerify(SmId smId,  //!< The state machine id
                                                          Update_DeltaPatchMachine::Signal signal  //!< The signal
    ) const override;

  private:
    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Dispatch up to MAX_DISPATCH_PER_TICK queued messages
    void drainQueue();

    //! Publish progress telemetry and turn a delegate result into a stageDone /
    //! fail signal
    void reportStep(DeltaCodec::Status status, DeltaCodec::StepResult result);

    //! Announce a state entry: StateEntered event and State channel (single
    //! emission site, called by the enter* actions)
    void reportState(DeltaPatchState state);

    //! Reject APPLY_PATCH before a patch is bound
    void rejectApply(FwOpcodeType opCode, U32 cmdSeq, DeltaPatchStatus status, Fw::CmdResponse response);

    //! Answer an ABORT_PATCH that found no patch in progress (single emission
    //! site of AbortIgnored)
    void ignoreAbort(FwOpcodeType opCode, U32 cmdSeq);

    //! Answer the pending APPLY_PATCH and record its outcome
    void completePending(DeltaPatchStatus status, Fw::CmdResponse response);

    //! Close all media
    void closeMedia();

    //! Whether a patch is bound (old-image media open)
    //!
    //! Signals reach the state machine through the component queue, so within one
    //! drain the machine can lag a command handler by one dispatch (patch bound,
    //! machine still IDLE). This predicate is therefore the only lifecycle test
    //! the command handlers consult; the machine remains the authority for
    //! everything else.
    bool isPatchBound() const;

    //! True when the three paths resolve (textually, via Os::FilePathUtils) to
    //! three different files
    enum PathCheck {
        PATHS_DISTINCT,  //!< Three distinct, well-formed paths
        PATHS_INVALID,   //!< A path could not be resolved (empty, too long,
                         //!< malformed)
        PATHS_ALIASED    //!< Two paths name the same file (textually or, where the
                         //!< platform exposes it, by identity)
    };

    //! Resolve the three paths and check that they are distinct
    static PathCheck checkPaths(const Fw::CmdStringArg& first,
                                const Fw::CmdStringArg& second,
                                const Fw::CmdStringArg& third);

    //! True when both paths exist and name the same underlying file (hard links);
    //! false where the platform offers no file identity
    static bool sameIdentity(const char* first, const char* second);

    //! Map an engine status onto the reported status enumeration
    static DeltaPatchStatus toStatus(DeltaCodec::Status status);

  private:
    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! The APPLY_PATCH whose response is deferred until the patch completes,
    //! fails or is aborted
    struct PendingCommand {
        FwOpcodeType opCode;       //!< Opcode of the in-progress APPLY_PATCH
        U32 cmdSeq;                //!< Sequence of the in-progress APPLY_PATCH
        Fw::CmdStringArg newFile;  //!< Output path, reported on completion
    };

    DeltaCoderLzss m_defaultCoder;  //!< Shipped coder, used unless setCoder() is called
    DeltaCodec m_codec;             //!< Streaming SPatch engine (owns all patch-time
                                    //!< buffers); the machine's delegate
    DeltaFileMedia m_oldMedia;      //!< Old image (read only)
    DeltaFileMedia m_patchMedia;    //!< Patch file (read only)
    DeltaFileMedia m_newMedia;      //!< New image (read/write for resume)
    PendingCommand m_pending;       //!< Deferred APPLY_PATCH
};

}  // namespace Update

#endif
