// ======================================================================
// \title  SubtopologyTopologyDefs.hpp
// \author starchmd
// \brief  hpp file for Update subtopology topology definitions
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================

#ifndef FPRIME_EXTRAS_UPDATE_SUBTOPOLOGY_SUBTOPOLOGY_DEFS_HPP
#define FPRIME_EXTRAS_UPDATE_SUBTOPOLOGY_SUBTOPOLOGY_DEFS_HPP

namespace Update {
struct SubtopologyState {
    // Empty - no external state needed for Update subtopology
};

struct TopologyState {
    SubtopologyState update;
};
}  // namespace Update

#endif
