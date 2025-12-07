# Update/Updater/Updater.fpp:
#
# Updater component used for managing a set of flight software images. It allows basic coordination between these
# images and the underlying platform firmware update mechanisms provided by an UpdateWorker implementation.
#
# Copyright (c) 2025 Michael Starch
# 
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
#
module Update {
    @ Component used to perform a flight software update. This component has commands that provide preparation, update,
    @ and boot configuration. 
    @
    @ This component is designed fore use with a UpdateWorker component that performs the work. This component may be
    @ written to interaction with Flash chips, file i/o, or other specific actions used by the system.
    @
    @ This component allows two styles of booting:
    @   1. TEST: used to boot the software exactly one time. Boot will revert afterward. 
    @   2. PERMANENT: used to set the software image as the permanent boot image.
    @
    @ Users are advised to boot an updated image first in TEST, and upon confirmation of functionality, to then set the
    @ image as permanent.
    @
    active component Updater {
        import Update.UpdateWorkerClient

        @ Allows the user to set the next boot for the given mode. TEST boots the system exactly one time. PERMANENT
        @ then sets the image as the permanent boot image.
        async command CONFIGURE_NEXT_BOOT(
            next: NextBootMode @< Mode of the next boot
        )

        @ Prepare the for updating. This may be a NO-OP on some platforms.  This can ERASE flash, make directories,
        @ validate images, etc.
        async command PREPARE_UPDATE()

        @ Performs an update from the supplied file. Users are expected to have run the PREPARE_UPDATE command prior
        @ to running this command. 
        async command UPDATE_IMAGE_FROM(
            file: string size FileNameStringSize @< File to read the new image from
            crc32: U32 @< Expected CRC32 of the file used to verify file integrity
        )

        @ Confirm that the currently running image is good. This will prevent reversion on next boot if the image
        @ was booted in TEST mode.
        async command CONFIRM_UPDATE()
    
        event SetNextBoot(next: NextBootMode) severity activity high format "Next boot configured: {}"

        event SetNextBootFailed(status: UpdateStatus) severity warning high format "Next boot configuration failed: {}"

        event ConfirmBoot() severity activity high format"Boot confirmed, next boot will use this software"

        event ConfirmBootFailed(status: UpdateStatus) severity warning high format "Boot confirmation failed: {}"

        event PrepareUpdate() severity activity high format "Prepare update started"

        event PrepareUpdateSucceeded() severity activity high format "Prepare update finished successfully"
        
        event PrepareUpdateFailed(status: UpdateStatus) severity warning high format "Prepare update failed: {}"

        event Update(file: string) severity activity high format "Update from {} started"

        event UpdateSucceeded() severity activity high format "Update finished successfully"
        
        event UpdateFailed(status: UpdateStatus) severity warning high format "Update failed: {}"

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
