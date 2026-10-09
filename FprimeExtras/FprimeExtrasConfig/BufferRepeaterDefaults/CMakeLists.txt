# Separate from ExtrasConfig (a BASE_CONFIG) because these defaults depend on Fw_Types
register_fprime_config(
    EXCLUDE_FROM_ALL
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/BufferRepeaterDefaults.fpp"
    DEPENDS
        Fw_Types
)
