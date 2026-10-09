# FprimeExtrasConfig/BufferRepeaterDefaults/BufferRepeaterDefaults.fpp:
#
# Default parameter values for Utilities.BufferRepeater
#
# Copyright (c) 2026 Michael Starch
#
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
#
module Utilities {
    @ Default enable state for a BufferRepeater output channel
    constant BUFFER_REPEATER_DEFAULT_CHANNEL_ENABLE = Fw.Enabled.DISABLED

    @ Per-channel default of the BufferRepeater CHANNEL_ENABLED parameter. Must have BUFFER_FANOUT_MULTI_SIZE entries.
    @
    @ This default configuration uses BUFFER_REPEATER_DEFAULT_CHANNEL_ENABLE for every channel. Users may inline
    @ Fw.Enabled literals instead.
    constant BUFFER_REPEATER_DEFAULT_CHANNEL_ENABLES = [
        BUFFER_REPEATER_DEFAULT_CHANNEL_ENABLE, # Channel 0
        BUFFER_REPEATER_DEFAULT_CHANNEL_ENABLE, # Channel 1
        BUFFER_REPEATER_DEFAULT_CHANNEL_ENABLE, # Channel 2
    ]
}
