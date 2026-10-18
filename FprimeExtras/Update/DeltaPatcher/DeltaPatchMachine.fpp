# Update/DeltaPatcher/DeltaPatchMachine.fpp:
#
# State machine governing the lifecycle of one APPLY_PATCH in the DeltaPatcher component.
#
# Copyright (c) 2026 Michael Starch
#
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
#
module Update {
    @ The ABORT_PATCH command being honoured; answered once the abort has taken effect
    struct DeltaAbortRequest {
        opCode: FwOpcodeType @< Opcode of the ABORT_PATCH command
        cmdSeq: U32 @< Sequence number of the ABORT_PATCH command
    }

    @ Lifecycle of one APPLY_PATCH. The machine owns the state; every `tick` action delegates one bounded unit of
    @ work to the DeltaCodec (verifyResume / verifyOld / patchChunk / verifyFinal), which answers through
    @ `stageDone` or `fail`. Every state announces itself on entry (StateEntered event + State channel). A patch
    @ ends — complete, failed or aborted — by returning to IDLE, whose entry releases the media and resets the
    @ engine; the output file is retained on abort and failure so a later APPLY_PATCH resumes.
    state machine DeltaPatchMachine {
        initial enter IDLE

        @ Rate-group tick: perform one bounded unit of work in the current stage
        signal tick

        @ APPLY_PATCH accepted: header validated, media bound
        signal start

        @ The current stage has finished its work
        signal stageDone

        @ The current stage failed with the given status
        signal fail: DeltaPatchStatus

        @ Operator ABORT_PATCH, carrying the command to answer once the abort has taken effect
        signal abort: DeltaAbortRequest

        @ True when the existing new file holds whole chunks to CRC-verify before patching (resume)
        guard hasOutputToVerify

        @ Delegate one unit of resume verification to DeltaCodec::verifyResume()
        action doVerifyResume

        @ Delegate one unit of old-image verification to DeltaCodec::verifyOld()
        action doVerifyOld

        @ Delegate one chunk (or I/O-budget slice) to DeltaCodec::patchChunk()
        action doPatchChunk

        @ Delegate one unit of read-back verification to DeltaCodec::verifyFinal()
        action doVerifyFinal

        @ Announce entry into IDLE (StateEntered event + State channel). FPP entry actions take no argument and run
        @ before the state is recorded, so each state names its own; all five delegate to DeltaPatcher::reportState().
        @ The initial transition is the exception: it runs inside init(), before ports and telemetry are set up,
        @ so enterIdle stays silent for it (IDLE is the State channel default)
        action enterIdle

        @ Announce entry into VERIFY_RESUME
        action enterVerifyResume

        @ Announce entry into VERIFY_OLD
        action enterVerifyOld

        @ Announce entry into PATCHING
        action enterPatching

        @ Announce entry into VERIFY_FINAL
        action enterVerifyFinal

        @ Emit PatchResumed when verified chunks were skipped
        action reportResumed

        @ Emit PatchComplete, invoke patchComplete, answer APPLY_PATCH OK
        action reportComplete

        @ Emit OldImageMismatch or ChunkFailed, answer APPLY_PATCH EXECUTION_ERROR
        action reportFailed: DeltaPatchStatus

        @ Emit PatchAborted, answer APPLY_PATCH EXECUTION_ERROR and ABORT_PATCH OK
        action reportAborted: DeltaAbortRequest

        @ Answer an ABORT_PATCH that found no patch in progress
        action reportAbortIgnored: DeltaAbortRequest

        @ Close the media and forget the codec progress (idempotent)
        action doRelease

        @ No patch bound: entry releases whatever the ending patch left behind
        state IDLE {
            entry do { doRelease, enterIdle }
            on start enter selectResume
            on abort do { reportAbortIgnored }
        }

        @ Resume only when the existing output holds whole chunks worth checking
        choice selectResume {
            if hasOutputToVerify enter VERIFY_RESUME else enter VERIFY_OLD
        }

        @ Resume: CRC-check the chunks already present in the new file to find where patching picks up
        state VERIFY_RESUME {
            entry do { enterVerifyResume }
            on tick do { doVerifyResume }
            on stageDone do { reportResumed } enter VERIFY_OLD
            on fail do { reportFailed } enter IDLE
            on abort do { reportAborted } enter IDLE
        }

        @ CRC-check the old image against the header
        state VERIFY_OLD {
            entry do { enterVerifyOld }
            on tick do { doVerifyOld }
            on stageDone enter PATCHING
            on fail do { reportFailed } enter IDLE
            on abort do { reportAborted } enter IDLE
        }

        @ Apply chunks; the last patchChunk() flushes and re-resolves the output
        state PATCHING {
            entry do { enterPatching }
            on tick do { doPatchChunk }
            on stageDone enter VERIFY_FINAL
            on fail do { reportFailed } enter IDLE
            on abort do { reportAborted } enter IDLE
        }

        @ Read the stored new image back and check its size and CRC. The media are released before
        @ reportComplete so patchComplete hands over a closed file
        state VERIFY_FINAL {
            entry do { enterVerifyFinal }
            on tick do { doVerifyFinal }
            on stageDone do { doRelease, reportComplete } enter IDLE
            on fail do { reportFailed } enter IDLE
            on abort do { reportAborted } enter IDLE
        }
    }
}
