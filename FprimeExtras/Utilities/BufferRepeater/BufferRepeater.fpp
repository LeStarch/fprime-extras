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

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

    }
}