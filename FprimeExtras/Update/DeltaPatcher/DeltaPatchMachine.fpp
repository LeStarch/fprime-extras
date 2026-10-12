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
    @ work to the DeltaCodec (verifyNew / verifyOld / patchChunk / verifyFinal), which answers through `stageDone`
    @ or `fail`. The output file is retained on abort and failure so a later APPLY_PATCH resumes.
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

        @ APPLY_PATCH rejected after the files were opened (open or header failure); nothing is bound
        signal reject

        @ True when the existing new file holds whole chunks to CRC-verify before patching (resume)
        guard hasOutputToVerify

        @ Delegate one unit of resume verification to DeltaCodec::verifyNew()
        action doVerifyNew

        @ Delegate one unit of old-image verification to DeltaCodec::verifyOld()
        action doVerifyOld

        @ Delegate one chunk (or I/O-budget slice) to DeltaCodec::patchChunk()
        action doPatchChunk

        @ Delegate one unit of read-back verification to DeltaCodec::verifyFinal()
        action doVerifyFinal

        @ Report VERIFY_NEW on the State telemetry channel (entry actions run before the state is recorded, so each
        @ state names its own)
        action tlmVerifyNew

        @ Report VERIFY_OLD on the State telemetry channel
        action tlmVerifyOld

        @ Report PATCHING on the State telemetry channel
        action tlmPatching

        @ Report VERIFY_FINAL on the State telemetry channel
        action tlmVerifyFinal

        @ Report COMPLETE on the State telemetry channel
        action tlmComplete

        @ Report FAILED on the State telemetry channel
        action tlmFailed

        @ Emit PatchResumed when verified chunks were skipped
        action reportResumed

        @ Emit PatchComplete, invoke patchComplete, answer APPLY_PATCH OK
        action reportComplete

        @ Emit OldImageMismatch or ChunkFailed, answer APPLY_PATCH EXECUTION_ERROR
        action reportFailed: DeltaPatchStatus

        @ Emit PatchAborted, answer APPLY_PATCH EXECUTION_ERROR and ABORT_PATCH OK, report IDLE
        action reportAborted: DeltaAbortRequest

        @ Close the media and forget the codec progress
        action doRelease

        @ No patch bound
        state IDLE {
            on start enter selectResume
            on reject enter FAILED
        }

        @ Resume only when the existing output holds whole chunks worth checking
        choice selectResume {
            if hasOutputToVerify enter VERIFY_NEW else enter VERIFY_OLD
        }

        @ Resume: CRC-check chunks already present in the new file
        state VERIFY_NEW {
            entry do { tlmVerifyNew }
            on tick do { doVerifyNew }
            on stageDone do { reportResumed } enter VERIFY_OLD
            on fail do { reportFailed } enter FAILED
            on abort do { reportAborted, doRelease } enter IDLE
        }

        @ CRC-check the old image against the header
        state VERIFY_OLD {
            entry do { tlmVerifyOld }
            on tick do { doVerifyOld }
            on stageDone enter PATCHING
            on fail do { reportFailed } enter FAILED
            on abort do { reportAborted, doRelease } enter IDLE
        }

        @ Apply chunks; the last patchChunk() flushes and re-resolves the output
        state PATCHING {
            entry do { tlmPatching }
            on tick do { doPatchChunk }
            on stageDone enter VERIFY_FINAL
            on fail do { reportFailed } enter FAILED
            on abort do { reportAborted, doRelease } enter IDLE
        }

        @ Read the stored new image back and check its size and CRC
        state VERIFY_FINAL {
            entry do { tlmVerifyFinal }
            on tick do { doVerifyFinal }
            on stageDone do { reportComplete } enter COMPLETE
            on fail do { reportFailed } enter FAILED
            on abort do { reportAborted, doRelease } enter IDLE
        }

        @ New image produced and verified
        state COMPLETE {
            entry do { doRelease, tlmComplete }
            on start enter selectResume
            on reject enter FAILED
        }

        @ Patch failed; the output is retained for resume
        state FAILED {
            entry do { doRelease, tlmFailed }
            on start enter selectResume
            on reject enter FAILED
        }
    }
}
