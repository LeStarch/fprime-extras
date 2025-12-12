module Utilities {
    @ A component to modulate rate group calls
    passive component RateDelay {
        @ Default divider value
        constant DEFAULT_DIVIDER = 29 # On a 1Hz input, outputs every 30s
        @ Rate schedule port used to trigger divider
        sync input port runIn: Svc.Sched

        @ Output of divided rate schedule port
        output port runOut: Svc.Sched

        @ Divider of the incoming rate tick
        param DIVIDER: U8 default DEFAULT_DIVIDER # Start slow i.e. on a 1S tick, transmit every 30S

        @ Divider set event
        event DividerSet(divider: U8) severity activity high \
            format "Set divider to: {}"

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

        @ Port for sending textual representation of events
        text event port logTextOut

        @ Port for sending events to downlink
        event port logOut

        @ Port to return the value of a parameter
        param get port prmGetOut

        @Port to set the value of a parameter
        param set port prmSetOut
    }
}