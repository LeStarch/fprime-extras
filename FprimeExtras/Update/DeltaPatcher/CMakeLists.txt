# FprimeExtras/Update/DeltaPatcher/CMakeLists.txt:
#
# Build registration for the DeltaPatcher module
#
# Copyright (c) 2026 Michael Starch
#
# Licensed under the Apache License, Version 2.0. See LICENSE for details.

register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/DeltaPatcher.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/DeltaPatcher.cpp"
    DEPENDS
        FprimeExtras_Update_Delta
)

register_fprime_ut(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/DeltaPatcher.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/DeltaPatcherTestMain.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/DeltaPatcherTester.cpp"
    DEPENDS
        FprimeExtras_Update_Delta
        STest
    UT_AUTO_HELPERS
)
