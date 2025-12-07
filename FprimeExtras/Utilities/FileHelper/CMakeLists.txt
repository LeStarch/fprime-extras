register_fprime_library(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/FileHelper.cpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/FileHelper.hpp"
    DEPENDS
        Fw_Types
)

### Unit Tests ###
register_fprime_ut(
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/test/ut/FileHelperTestMain.cpp"
    DEPENDS
        gtest # For rules-based testing
)
