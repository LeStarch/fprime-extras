// ======================================================================
// \title  Updater.hpp
// \author starchmd
// \brief  hpp file for Updater component implementation class
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================

#ifndef Update_Updater_HPP
#define Update_Updater_HPP

#include <atomic>

#include "FprimeExtras/Update/Updater/UpdaterComponentAc.hpp"

namespace Update {

class Updater final : public UpdaterComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct Updater object
    Updater(const char* const compName  //!< The component name
    );

    //! Destroy Updater object
    ~Updater();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for prepareImageDone
    //!
    //! Receives preparation completion status, connects to prepareImageDone port of UpdateWorker
    void prepareImageDone_handler(FwIndexType portNum,                //!< The port number
                                  const Update::UpdateStatus& status  //!< Status to supplied to action
                                  ) override;

    //! Handler implementation for updateImageDone
    //!
    //! Receives update completion status, connects to updateImageDone port of UpdateWorker
    void updateImageDone_handler(FwIndexType portNum,                //!< The port number
                                 const Update::UpdateStatus& status  //!< Status to supplied to action
                                 ) override;

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command CONFIGURE_NEXT_BOOT
    //!
    //! Allows the user to set the next boot for the given mode. TEST boots the system exactly one time. PERMANENT
    //! then sets the image as the permanent boot image.
    void CONFIGURE_NEXT_BOOT_cmdHandler(FwOpcodeType opCode,       //!< The opcode
                                        U32 cmdSeq,                //!< The command sequence number
                                        Update::NextBootMode next  //!< Mode of the next boot
                                        ) override;

    //! Handler implementation for command PREPARE_UPDATE
    //!
    //! Prepare the for updating. This may be a NO-OP on some platforms.  This can ERASE flash, make directories,
    //! validate images, etc.
    void PREPARE_UPDATE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                   U32 cmdSeq            //!< The command sequence number
                                   ) override;

    //! Handler implementation for command UPDATE_IMAGE_FROM
    //!
    //! Performs an update from the supplied file. Users are expected to have run the PREPARE_UPDATE command prior
    //! to running this command.
    void UPDATE_IMAGE_FROM_cmdHandler(FwOpcodeType opCode,          //!< The opcode
                                      U32 cmdSeq,                   //!< The command sequence number
                                      const Fw::CmdStringArg& file, //!< File to read the new image from
                                      U32 crc32                     //!< Expected CRC32 of the file used to verify file integrity
                                      ) override;

    //! Handler implementation for command CONFIRM_UPDATE
    //!
    //! Confirm that the currently running image is good. This will prevent reversion on next boot if the image
    //! was booted in TEST mode.
    void CONFIRM_UPDATE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                   U32 cmdSeq            //!< The command sequence number
                                   ) override;

  private:
    FwOpcodeType m_opCode;
    U32 m_cmdSeq;
    std::atomic<bool> m_busy;
};

}  // namespace Update

#endif
