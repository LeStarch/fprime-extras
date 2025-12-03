module Utilities {
    @ Collects buffers from multiple sources and directs them to a single destination via a BufferFanout interface. The
    @ return path is mapped back to the source of the original buffer.
    passive component BufferCollector {
        import Utilities.BufferFanout

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

    }
}