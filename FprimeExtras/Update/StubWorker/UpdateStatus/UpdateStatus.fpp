# UpdateStatus/UpdateStatus.fpp:
#
# File provided by the UpdateWorker component to report status codes back to the Updater component. This is configured
# specifically for the FlashWorker implementation supplied by Proves.
#
# Copyright (c) 2025 Michael Starch
# 
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
#
module Update {
    @ Status codes used by the Update subsystem.
    enum StubWorkerUpdateStatus {
        OP_OK, @< REQUIRED: operation successful
        BUSY, @< REQUIRED: operation could not be started because another operation is in-progress
        NOT_IMPLEMENTED @< Nothing in this stub is implemented
    }
}