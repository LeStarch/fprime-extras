module Utilities {
    @ Detects drops in uplinked files
    passive component DropDetector {

        @ Search the specified file for sequences of zeros of length packet_size and report their
        @ one-based indices via events.
        guarded command DETECT_DROPS(file: string size FileNameStringSize, packet_size: FwSizeType) opcode 0

        @ Detecting drops started
        event DetectingDrops() severity activity high format "Detecting drops in file"

        @ Detected a possible drop
        event PossibleDrop(index: FwSizeType) severity activity high format "Possible drop at index {}"

        @ File seek error
        event FileSeekError(index: FwSizeType, error: Os.FileStatus) severity warning high format "File seek error at index {}: {}"

        @ File read error
        event FileReadError(index: FwSizeType, error: Os.FileStatus) severity warning high format "File read error at index {}: {}"

        @ Detecting drops completed
        event DetectingDropsCompleted() severity activity high format "Completed drop detection"

        @ Scheduler input port for rate group operations
        guarded input port schedIn: Svc.Sched

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

    }
}