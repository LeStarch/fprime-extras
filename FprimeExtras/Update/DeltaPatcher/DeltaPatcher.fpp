# Update/DeltaPatcher/DeltaPatcher.fpp:
#
# DeltaPatcher component: applies an uplinked SPatch delta file to an on-board old image, producing a new image file.
#
# Copyright (c) 2026 Michael Starch
#
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
#
module Update {
    @ Applies an uplinked SPatch file to an on-board old image, producing a new image file suitable for
    @ Updater.UPDATE_IMAGE_FROM. The component is queued: commands are dispatched and a bounded amount of patch work
    @ (at most one chunk plus a bounded amount of CRC verification) is performed on each `run` invocation.
    @
    @ The old image is never modified. If the new file already holds a prefix of CRC-verified chunks (e.g. after a
    @ reboot), patching resumes at the first unverified chunk. RAM usage is fixed at construction and the engine never
    @ allocates.
    queued component DeltaPatcher {

        @ Rate-group tick: dispatches queued commands, then performs one bounded step of patch work
        sync input port run: Svc.Sched

        @ Emitted on successful completion with (new_file, new_crc32); connect to updater.updateImage or leave open
        output port patchComplete: UpdateFile

        @ Start applying patch_file to old_file writing new_file. Resumes if new_file already holds verified chunks.
        @ The command completes (OK or EXECUTION_ERROR) when the patch finishes or fails.
        async command APPLY_PATCH(
            old_file: string size FileNameStringSize @< Existing image (never modified)
            patch_file: string size FileNameStringSize @< Uplinked .spatch file
            new_file: string size FileNameStringSize @< Output image (created or extended)
        )

        @ Abort an in-progress patch; the partial new_file is retained for later resume
        async command ABORT_PATCH()

        @ A patch has been accepted and started
        event PatchStarted(
            patch_file: string size FileNameStringSize @< Patch file
            old_file: string size FileNameStringSize @< Old image file
            new_file: string size FileNameStringSize @< New image file
            chunks: U32 @< Total chunks in the patch
        ) severity activity high format "Patch {} : {} -> {} started, {} chunks"

        @ Verified chunks already present in the new file were skipped
        event PatchResumed(
            new_file: string size FileNameStringSize @< New image file
            chunk: U32 @< First chunk to be patched
            total: U32 @< Total chunks in the patch
        ) severity activity high format "Resuming {} at chunk {} of {}"

        @ The patch was rejected before any chunk was applied
        event PatchRejected(
            status: DeltaPatchStatus @< Reason
        ) severity warning high format "Patch rejected: {}"

        @ The on-board old image does not match the patch header
        event OldImageMismatch(
            expected_crc: U32 @< CRC declared by the patch
            actual_crc: U32 @< CRC of the on-board old image
            expected_size: U32 @< Size declared by the patch
            actual_size: U64 @< Size of the on-board old image
        ) severity warning high format "Old image mismatch: crc {x} != {x}, size {} != {}"

        @ A chunk failed to apply; patching stops
        event ChunkFailed(
            chunk: U32 @< Failing chunk index
            status: DeltaPatchStatus @< Reason
        ) severity warning high format "Chunk {} failed: {}"

        @ The new image was produced and verified
        event PatchComplete(
            new_file: string size FileNameStringSize @< New image file
            new_size: U32 @< Size of the new image
            crc32: U32 @< CRC32 of the new image
        ) severity activity high format "Patch complete: {} ({} bytes, crc {x})"

        @ The operator aborted an in-progress patch
        event PatchAborted(
            chunk: U32 @< Next chunk that would have been applied
        ) severity activity high format "Patch aborted at chunk {}"

        @ Current patcher state
        telemetry State: DeltaPatchState update on change

        @ Chunks applied or verified so far
        telemetry ChunksDone: U32 update on change

        @ Chunks in the current patch
        telemetry ChunksTotal: U32 update on change

        @ New-image bytes written or verified so far
        telemetry BytesWritten: U64 update on change

        @ Outcome of the last patch operation
        telemetry LastStatus: DeltaPatchStatus update on change

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Port for sending command registrations
        command reg port cmdRegOut

        @ Port for receiving commands
        command recv port cmdIn

        @ Port for sending command responses
        command resp port cmdResponseOut

        @ Port for sending textual representation of events
        text event port logTextOut

        @ Port for sending events to downlink
        event port logOut

        @ Port for sending telemetry channels to downlink
        telemetry port tlmOut
    }
}
