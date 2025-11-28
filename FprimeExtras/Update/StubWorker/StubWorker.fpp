# Update/StubWorker/StubWorker.fpp:
#
# StubWorker component used as a stub implementation of the UpdateWorker interface.
#
# Copyright (c) 2025 Michael Starch
# 
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
#
module Update {
    @ Stub implementation of the UpdateWorker. All actions performed with this worker will result in an assertion
    @ trip. This is intended to force users to select a replacement component.
    active component StubWorker {
        import UpdateWorker
    }
}