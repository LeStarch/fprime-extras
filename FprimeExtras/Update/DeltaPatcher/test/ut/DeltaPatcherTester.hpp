// ======================================================================
// \title  DeltaPatcherTester.hpp
// \author starchmd
// \brief  hpp file for DeltaPatcher component test harness implementation class
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================

#ifndef Update_DeltaPatcherTester_HPP
#define Update_DeltaPatcherTester_HPP

#include <string>

#include "FprimeExtras/Update/Delta/test/ut/DeltaTestEncoders.hpp"
#include "FprimeExtras/Update/DeltaPatcher/DeltaPatcher.hpp"
#include "FprimeExtras/Update/DeltaPatcher/DeltaPatcherGTestBase.hpp"
#include "TestUtils/RuleBasedTesting.hpp"

namespace Update {

class DeltaPatcherTester final : public DeltaPatcherGTestBase {
  public:
    // ----------------------------------------------------------------------
    // Constants
    // ----------------------------------------------------------------------

    // Maximum size of histories storing events, telemetry, and port outputs
    static const FwSizeType MAX_HISTORY_SIZE = 20000;  //!< Rules tick thousands of times per patch

    // Instance ID supplied to the component instance under test
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    // Queue depth supplied to the component instance under test
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    // Upper bound on rate-group ticks a test will drive before declaring a hang
    static constexpr U32 MAX_TICKS = 10000;

  public:
    // ----------------------------------------------------------------------
    // Construction and destruction
    // ----------------------------------------------------------------------

    //! `coder` (when given) replaces the component's shipped coder before any patch; rules build their
    //! patches for whichever coder is in use
    explicit DeltaPatcherTester(DeltaCoder* coder = nullptr);
    ~DeltaPatcherTester();

    // ----------------------------------------------------------------------
    // Rules (Rules/PatcherRules.cpp): a random walk over commands, ticks,
    // aborts, damaged files and a flooded queue, with the component's events,
    // telemetry and command responses as the oracle
    // ----------------------------------------------------------------------

    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, Apply);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, Tick);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, ApplyWhileBusy);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, Abort);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, AbortIdle);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, CorruptPatch);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, CorruptOutput);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, OldMismatch);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, MissingInput);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, SameFile);
    FW_RBT_DEFINE_RULE(DeltaPatcherTester, Patcher, QueueFlood);

    //! Counts of what the random walk exercised
    struct RuleCounters {
        U32 applies;
        U32 resumes;
        U32 completions;
        U32 aborts;
        U32 abortsOvertaken;
        U32 rejections;
        U32 chunkFailures;
        U32 oldMismatches;
        U32 outputCorruptions;
        U32 floods;
        U32 ticks;
    };
    const RuleCounters& ruleCounters() const { return this->m_rc; }
    void printRuleCounters() const;

  public:
    // ----------------------------------------------------------------------
    // Tests
    // ----------------------------------------------------------------------

    //! Patch completes, new file matches, command responds OK, patchComplete fires
    void nominal();

    //! A second APPLY_PATCH while patching is rejected BUSY
    void busy();

    //! Missing input files are rejected OPEN_FAILED
    void openFailed();

    //! Corrupt header is rejected BAD_HEADER at command time
    void badHeader();

    //! Valid patch for a different coder is rejected CODER_MISMATCH at command time
    void coderMismatch();

    //! Patch serialized with the other FwSizeType width is rejected SIZE_WIDTH_MISMATCH at command time
    void sizeWidthMismatch();

    //! Wrong old image reports OldImageMismatch
    void oldImageMismatch();

    //! Corrupt chunk payload reports ChunkFailed and retains verified prefix
    void chunkFailed();

    //! ABORT_PATCH stops the patch; the next APPLY_PATCH resumes from verified chunks
    void abortAndResume();

    //! ABORT_PATCH when idle is a validation error
    void abortIdle();

    //! Abort while in VERIFY_OLD and VERIFY_FINAL
    void abortDuringVerify();

    //! Queue held full of ABORT_PATCH while bound: hook absorbs dropped signals
    void signalOverflow();

    //! start signal dropped by a full queue: hook unbinds and answers BUSY
    void startDropped();

    //! Overflow hook answers a dropped abort directly
    void hookAnswersDroppedAbort();

    //! Abort dispatched with the completing tick: answered from IDLE as ignored
    void abortOvertakenByCompletion();

    //! Queue-full commands are answered BUSY immediately via the overflow hook
    void queueOverflow();

    //! APPLY_PATCH with new_file == old_file is rejected with SAME_FILE before any file is opened
    void sameFile();

    //! new_file naming the old image through a different spelling (`dir/./old.bin`) is rejected SAME_FILE
    void sameFileAliased();

    //! new_file that is a hard link to old_file is rejected SAME_FILE (device/inode identity)
    void sameFileHardLink();

    //! FIFOs, directories and symbolic links are rejected OPEN_FAILED without blocking the caller
    void specialFiles();

    //! A rejected header does not leave an empty new_file behind
    void badHeaderLeavesNoOutput();

    //! new_file replaced by another file before read-back is reported NEW_IMAGE_MISMATCH, not PatchComplete
    void outputReplacedBeforeVerify();

  private:
    // ----------------------------------------------------------------------
    // Helper functions
    // ----------------------------------------------------------------------

    //! Connect ports
    void connectPorts();

    //! Initialize components
    void initComponents();

    //! Write bytes to a file
    static void writeFile(const std::string& path, const U8* data, FwSizeType size);

    //! Read a whole file
    static std::string readFile(const std::string& path);

    //! Read a big-endian FwSizeType from a SPatch chunk header
    static FwSizeType readSize(const U8* data);

    //! Send APPLY_PATCH and tick `run` once so it is dispatched
    void apply(U32 cmdSeq);

    //! Tick `run` until a command response is recorded or MAX_TICKS elapse; returns ticks used
    U32 runUntilResponse();

    // Rule helpers
    //! Build a fresh random patch for the coder in use and write old + patch files; drops the output file
    void newRandomPatch();
    //! Whole chunks at the head of the existing output that match the intended image (resume oracle)
    FwSizeType expectedResumedChunks() const;
    //! Size of the output file, 0 when absent
    FwSizeType outputSize() const;
    //! The output file equals the intended image
    bool outputMatches() const;
    //! Write a byte vector to a file
    static void writeFile(const std::string& path, const std::vector<U8>& data);
    //! Tick until `responses` command responses are recorded or MAX_TICKS elapse
    U32 runUntilResponses(FwSizeType responses);
    //! Common checks once a patch has ended (any outcome): State IDLE, nothing bound, no false completion
    void checkEnded();

  private:
    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! The component under test
    DeltaPatcher component;

    std::string m_dir;    //!< Scratch directory
    std::string m_old;    //!< Old image path
    std::string m_patch;  //!< Patch path
    std::string m_new;    //!< New image path

    // Rule shadow state
    U8 m_coderId;                     //!< Coder the component decodes with
    TestEncoders::RandomPatch m_rp;   //!< Current random patch and the image it must produce
    bool m_haveFiles;                 //!< old + patch files describe m_rp
    bool m_busy;                      //!< An APPLY_PATCH is in progress
    U32 m_applySeq;                   //!< Sequence of the in-progress APPLY_PATCH
    U32 m_nextSeq;                    //!< Next command sequence number
    FwSizeType m_expectResumed;       //!< Resume oracle for the in-progress patch
    FwSizeType m_resumedEvents;       //!< PatchResumed events seen so far for the in-progress patch
    std::vector<U8> m_outputAtApply;  //!< Output content when the in-progress patch was applied (diagnostics)
    std::string m_history;            //!< Rules applied since the current patch was built (diagnostics)
    RuleCounters m_rc;
};

}  // namespace Update

#endif
