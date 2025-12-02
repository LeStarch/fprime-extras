module Utilities {
    @ Interface used for multiple Fw.Buffer port pairs (in/out) on one side and a single Fw.Buffer port pair on the
    @ other side. This allows users to draft components that can repeat buffers to multiple clients, or receive
    @ multiple buffers and forward them to a single client.
    @
    @                         ______________  <--> multiBuffer0
    @      singleBuffer <--> | BufferFanout | ...
    @                         --------------  <--> multiBufferN
    @
    @ Ports use the nomenclature of "single" for the single buffer side, and "multi" for the multiple buffer side.
    @ "in" designates incoming buffers, and "out" designates outgoing buffers.
    @
    @ In a repeater configuration, the "single in" is repeated to multiple "multi out" ports, and "multi in" ports
    @ act as the return path to eventually return the original buffer via the "single out" port.
    @
    @ In the collector configuration, multiple "multi in" ports are forwarded to the single "single out" port.
    @ "single in" acts as the return path to eventually return all buffers via the "multi out" ports back to the
    @ source.
    @
    @ Note: these are not the only two possible configurations.
    @
    @ BUFFER_FANOUT_MULTI_SIZE defines the number of multi ports on the fanout side
    interface BufferFanout {
        @ Incoming buffer on the single port pair side. This collects the returned buffer when `multiOut` is the source
        @ of the original buffer. Otherwise, this receives the buffer to be forwarded to the multi ports.
        sync input port singleIn: Fw.BufferSend

        @ Outgoing buffer on the single port pair side. Used to return th original buffer when `singleIn` is the source
        @ of the original buffer. Otherwise, this forwards the buffer out.
        output port singleOut: Fw.BufferSend

        @ Incoming buffers on the multi-port pair side. Used to return buffers when `singleOut` is the source of the
        @ original buffer. Otherwise, these collect incoming buffers.
        sync input port multiIn: [BUFFER_FANOUT_MULTI_SIZE] Fw.BufferSend

        @ Outgoing buffers on the multi-port pair side. Used to forward buffers when `singleIn` is the source of the
        @ original buffer. Otherwise, these return buffers back to the source when `multiIn` is the source.
        output port multiOut: [BUFFER_FANOUT_MULTI_SIZE] Fw.BufferSend
    }
}
