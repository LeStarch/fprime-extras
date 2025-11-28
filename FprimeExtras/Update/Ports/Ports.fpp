# Update/Ports/Ports.fpp:
#
# Ports and types used by the Update subsystem.
#
# Copyright (c) 2025 Michael Starch
# 
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
#
module Update {

    @ Boot modes for software images
    enum NextBootMode : U8 {
        TEST, @< Test the image by running exactly once
        PERMANENT @< Set the image as the permanent software image
    }

    @ Port used to report a status back from a given update action
    port UpdateResponse(
        status: UpdateStatus @< Status to supplied to action
    )

    @ Port used to communicate a file to source an update
    port UpdateFile(
        file: string size FileNameStringSize @< File to supplied to action
    )

    @Port used to set the next boot to use test  
    port SetNextBootPort(
        mode: NextBootMode
    ) -> Update.UpdateStatus

    @ Port used to confirm that the currently running image is good
    port ConfirmImagePort() -> Update.UpdateStatus

    @ Interface: component that performs the platform-specific update actions. It runs on a separate task from the
    @ Updater component allowing long-running operations to take place without blocking command processing.
    @
    @ Must supply `prepareImage`, `updateImage`, and `nextBoot` ports to receive requests from the Updater component
    @ and responds to slow-running operations via the `prepareImageDone` and `updateImageDone` ports.
    interface UpdateWorker {
        @ Start preparation for an image update
        async input port prepareImage: Fw.Signal

        @ Update from a file
        async input port updateImage: UpdateFile

        @ Set the next boot image and mode
        sync input port nextBoot: SetNextBootPort

        @ Confirm that the currently running image is good
        sync input port confirmImage: ConfirmImagePort

        @ Report preparation completion status
        output port prepareImageDone: UpdateResponse

        @ Report update completion status
        output port updateImageDone: UpdateResponse
    }
    @ Interface: client interface used by the Updater component to communicate with an UpdateWorker implementation.
    @ This is the mirror of the UpdateWorker interface.
    interface UpdateWorkerClient{
        @ Start preparation for an image update, connects to prepareImage port of UpdateWorker
        output port prepareImage: Fw.Signal

        @ Update from a file, connects to updateImage port of UpdateWorker
        output port updateImage: UpdateFile

        @ Set the next boot image and mode, connects to nextBoot port of UpdateWorker
        output port nextBoot: SetNextBootPort

        @ Confirm that the currently running image is good, connects to confirmImage port of UpdateWorker
        output port confirmImage: ConfirmImagePort

        @ Receives preparation completion status, connects to prepareImageDone port of UpdateWorker
        sync input port prepareImageDone: UpdateResponse
    
        @ Receives update completion status, connects to updateImageDone port of UpdateWorker
        sync input port updateImageDone: UpdateResponse
    }
}