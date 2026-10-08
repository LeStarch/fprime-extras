# FprimeExtras/Update/Delta/CMakeLists.txt:
#
# Build registration for the Delta module
#
# Copyright (c) 2026 Michael Starch
#
# Licensed under the Apache License, Version 2.0. See LICENSE for details.

####
# Update/Delta: SPatch delta-patch engine, shipped coders, and file media (no autocoded parts)
####
register_fprime_library(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/DeltaCoder.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/DeltaMedia.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/DeltaCodec.cpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/DeltaWindow.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/DeltaCoder.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/DeltaMedia.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/DeltaCodec.hpp"
    DEPENDS
        Fw_Types
        Os
        Utils_Hash
        Utils_Types
)

### Unit Tests ###
register_fprime_ut(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/DeltaTestMain.cpp"
    DEPENDS
        gtest
)
