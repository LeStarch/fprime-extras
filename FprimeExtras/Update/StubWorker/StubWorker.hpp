// ======================================================================
// \title  StubWorker.hpp
// \author starchmd
// \brief  hpp file for StubWorker component implementation class
// ======================================================================

#ifndef Update_StubWorker_HPP
#define Update_StubWorker_HPP

#include "FprimeExtras/Update/StubWorker/StubWorkerComponentAc.hpp"

namespace Update {

class StubWorker final : public StubWorkerComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct StubWorker object
    StubWorker(const char* const compName  //!< The component name
    );

    //! Destroy StubWorker object
    ~StubWorker();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for confirmImage
    //!
    //! Confirm that the currently running image is good
    Update::UpdateStatus confirmImage_handler(FwIndexType portNum  //!< The port number
                                              ) override;

    //! Handler implementation for nextBoot
    //!
    //! Set the next boot image and mode
    Update::UpdateStatus nextBoot_handler(FwIndexType portNum,  //!< The port number
                                          const Update::NextBootMode& mode) override;

    //! Handler implementation for prepareImage
    //!
    //! Start preparation for an image update
    void prepareImage_handler(FwIndexType portNum  //!< The port number
                              ) override;

    //! Handler implementation for updateImage
    //!
    //! Update from a file
    void updateImage_handler(FwIndexType portNum,        //!< The port number
                             const Fw::StringBase& file  //!< File to supplied to action
                             ) override;
};

}  // namespace Update

#endif
