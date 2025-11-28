
add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/UpdateConfig/")

register_fprime_module(
    EXCLUDE_FROM_ALL
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/Update.fpp"
    HEADERS
        "${CMAKE_CURRENT_LIST_DIR}/SubtopologyTopologyDefs.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/PingEntries.hpp"
    DEPENDS
        FprimeExtras_Update_Subtopology_UpdateConfig
    INTERFACE
)
