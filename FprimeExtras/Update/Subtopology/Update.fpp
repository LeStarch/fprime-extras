# Update/Subtopology/Update.fpp:
#
# Subtopology and instances used to provide update functionality.
#
# Copyright (c) 2025 Michael Starch
# 
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
#
module Update {

    instance updater: Update.Updater base id BASE_ID + 0x0000 \
        queue size QueueSizes.updater \
        stack size StackSizes.updater \
        priority Priorities.updater

    topology Subtopology {
        instance updater
        instance worker

        connections Update {
            updater.prepareImage -> worker.prepareImage
            updater.updateImage -> worker.updateImage
            updater.nextBoot -> worker.nextBoot
            updater.confirmImage -> worker.confirmImage
            worker.prepareImageDone -> updater.prepareImageDone
            worker.updateImageDone -> updater.updateImageDone
        }
    } # end Subtopology
} # end Update
