// ======================================================================
// \title  DeltaPatcher.hpp
// \author starchmd
// \brief  hpp file for DeltaPatcher component implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#ifndef Update_DeltaPatcher_HPP
#define Update_DeltaPatcher_HPP

#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"
#include "FprimeExtras/Update/DeltaPatcher/DeltaPatcherComponentAc.hpp"

namespace Update {

//! \brief Applies SPatch delta files to on-board images, one bounded step per rate-group tick
//!
//! All handlers execute on the caller of `run` (commands are dispatched from the queue there), so no locking is needed
//! and the engine's fixed buffers are the only patch-time storage. The shipped LZSS coder is used unless a project
//! supplies its own via `setCoder()` during topology setup, before rate groups start.
class DeltaPatcher final : public DeltaPatcherComponentBase {
    friend class DeltaPatcherTester;

  public:
    //! Upper bound on queued messages dispatched per `run` tick
    static constexpr FwSizeType MAX_DISPATCH_PER_TICK = 4;

    //! Construct DeltaPatcher object
    DeltaPatcher(const char* const compName  //!< The component name
    );

    //! Destroy DeltaPatcher object
    ~DeltaPatcher();

    //! \brief Substitute the decompression coder used to decode SPatch payloads (project plugin seam)
    //!
    //! Call during topology setup, before rate groups start (FW_ASSERTs if a patch is in progress); the coder must
    //! outlive this component.
    void setCoder(DeltaCoder& coder);

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for run
    //!
    //! Rate-group tick: dispatches queued commands, then performs one bounded step of patch work
    void run_handler(FwIndexType portNum,  //!< The port number
                     U32 context           //!< The call order
                     ) override;

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command APPLY_PATCH
    //!
    //! Start applying patch_file to old_file writing new_file. Resumes if new_file already holds verified chunks.
    //! The command completes (OK or EXECUTION_ERROR) when the patch finishes or fails.
    void APPLY_PATCH_cmdHandler(FwOpcodeType opCode,                 //!< The opcode
                                U32 cmdSeq,                          //!< The command sequence number
                                const Fw::CmdStringArg& old_file,    //!< Existing image (never modified)
                                const Fw::CmdStringArg& patch_file,  //!< Uplinked .spatch file
                                const Fw::CmdStringArg& new_file     //!< Output image (created or extended)
                                ) override;

    //! Handler implementation for command ABORT_PATCH
    //!
    //! Abort an in-progress patch; the partial new_file is retained for later resume
    void ABORT_PATCH_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                U32 cmdSeq            //!< The command sequence number
                                ) override;

    //! Queue-full hook for APPLY_PATCH: respond BUSY on the caller's thread so the dispatcher never waits
    void APPLY_PATCH_cmdOverflowHook(FwOpcodeType opCode, U32 cmdSeq) override;

    //! Queue-full hook for ABORT_PATCH: respond BUSY on the caller's thread so the dispatcher never waits
    void ABORT_PATCH_cmdOverflowHook(FwOpcodeType opCode, U32 cmdSeq) override;

  private:
    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Advance the engine by one step and report state transitions
    void stepPatch();

    //! Finish the in-progress patch: report status, close media, respond to APPLY_PATCH
    void finish(DeltaPatchState state, DeltaPatchStatus status, Fw::CmdResponse response);

    //! Close all media
    void closeMedia();

    //! True when the three paths resolve (textually, via Os::FilePathUtils) to three different files
    static bool distinctPaths(const Fw::CmdStringArg& first,
                              const Fw::CmdStringArg& second,
                              const Fw::CmdStringArg& third);

    //! Map an engine status onto the reported status enumeration
    static DeltaPatchStatus toStatus(DeltaCodec::Status status);

  private:
    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    DeltaCoderLzss m_defaultCoder;  //!< Shipped coder, used unless setCoder() is called
    DeltaCodec m_codec;             //!< Streaming SPatch engine (owns all patch-time buffers)
    DeltaFileMedia m_oldMedia;      //!< Old image (read only)
    DeltaFileMedia m_patchMedia;    //!< Patch file (read only)
    DeltaFileMedia m_newMedia;      //!< New image (read/write for resume)
    Fw::String m_newFile;           //!< Output path, reported on completion
    FwOpcodeType m_opCode;          //!< Opcode of the in-progress APPLY_PATCH
    U32 m_cmdSeq;                   //!< Sequence of the in-progress APPLY_PATCH
    DeltaPatchState m_state;        //!< Reported state
    bool m_resumeReported;          //!< PatchResumed emitted for the current patch
};

}  // namespace Update

#endif
