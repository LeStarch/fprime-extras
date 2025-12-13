# ======================================================================
# \title  BufferRepeater.fpp
# \author starchmd
# \brief  fpp file for BufferRepeater component implementation class
# \copyright Copyright (c) 2025 Michael Starch
# ======================================================================

module Utilities {
    @ Repeats Fw.Buffer objects to multiple components via a BufferFanout interface. The intended source of the buffer
    @ is the singleIn port. The BufferRepeater will forward the buffer to all multiOut ports. When all multiIn ports
    @ have returned the buffer via their respective mulktiIn ports, the BufferRepeater will return the original buffer
    @ via the singleOut port.
    passive component BufferRepeater {
        # Uses the fanout shape
        import Utilities.BufferFanout

        @ Array of booleans to enable/disable output channels
        array OutputChannelEnables = [BUFFER_FANOUT_MULTI_SIZE] Fw.Enabled

        @ Parameter to set which output channels are enabled
        param CHANNEL_ENABLED: OutputChannelEnables default [Fw.Enabled.ENABLED, Fw.Enabled.ENABLED, Fw.Enabled.ENABLED]


        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Port for sending command registrations
        command reg port cmdRegOut

        @ Port for receiving commands
        command recv port cmdIn

        @ Port for sending command responses
        command resp port cmdResponseOut

        @ Port to return the value of a parameter
        param get port prmGetOut

        @ Port to set the value of a parameter
        param set port prmSetOut

    }
}