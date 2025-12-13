module Utilities {
    @ The maximum number of fanout port pairs configured for the BufferFanout interfaces.
    constant BUFFER_FANOUT_MULTI_SIZE = 3

    @ The maximum number of buffers that can be waited on for returning
    constant BUFFER_FANOUT_MAX_BUFFERS_IN_FLIGHT = 10
}