// ======================================================================
// \title  DeltaMedia.hpp
// \author starchmd
// \brief  hpp file for the storage abstraction used by DeltaCodec
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaMedia_HPP
#define Update_Delta_DeltaMedia_HPP

#include "Fw/FPrimeBasicTypes.hpp"
#include "Fw/Types/FileNameString.hpp"
#include "Os/File.hpp"

namespace Update {

//! \brief Random-access byte storage used for the old image, the patch, and the new image
//!
//! The codec reads the old image at arbitrary offsets, reads the patch sequentially, and writes the new image
//! sequentially (restarting at a chunk boundary on resume, after reading back the chunks already present).
//! Implementations may be files (DeltaFileMedia) or flash regions supplied by a project.
class DeltaMedia {
  public:
    enum Status {
        OP_OK,
        NOT_OPEN,      //!< Media is not open for the requested access
        OUT_OF_RANGE,  //!< Offset/size exceed the media
        IO_ERROR       //!< Underlying read/write/seek failure
    };

    virtual ~DeltaMedia() = default;

    //! Current size in bytes
    virtual Status size(FwSizeType& size) = 0;

    //! Read exactly `length` bytes at `offset` into `buffer`
    virtual Status read(FwSizeType offset, U8* buffer, FwSizeType length) = 0;

    //! Write exactly `length` bytes from `buffer` at `offset`, extending the media as needed
    virtual Status write(FwSizeType offset, const U8* buffer, FwSizeType length) = 0;

    //! Commit written bytes to storage so subsequent reads observe them; OP_OK when nothing is buffered
    virtual Status flush() = 0;

    //! Re-resolve the media before read-back verification. Media addressed by a
    //! name that can be replaced beneath an open handle (files) re-open by name
    //! here so verification attests to what the name now holds; media with a
    //! fixed identity (flash regions, memory) keep the default.
    virtual Status refresh() { return OP_OK; }
};

//! \brief DeltaMedia backed by Os::File
//!
//! READ_WRITE access uses one reader and one writer handle since Os::File
//! offers no combined mode. Some file systems (FatFs) give each handle its own
//! view of the file size, taken when the handle is opened, so flush() reopens
//! the reader after committing the writer to make the written bytes readable.
//!
//! open() requires the path to name a regular file (or, for READ_WRITE/CREATE,
//! nothing yet): directories, FIFOs, devices and symbolic links are refused
//! with INVALID_ARGUMENT before any blocking open() is attempted. Writes are
//! issued without a per-write sync; flush() commits them.
class DeltaFileMedia final : public DeltaMedia {
  public:
    enum Access {
        READ_ONLY,   //!< Existing file, reads only
        READ_WRITE,  //!< Create if absent, keep existing content, reads and writes
        CREATE       //!< Create or truncate, reads and writes
    };

    DeltaFileMedia();
    ~DeltaFileMedia() override;
    DeltaFileMedia(const DeltaFileMedia&) = delete;
    DeltaFileMedia& operator=(const DeltaFileMedia&) = delete;

    //! Open `path` with the requested access
    Os::File::Status open(const char* path, Access access);

    //! Close any open handles
    void close();

    bool isOpen() const;

    Status size(FwSizeType& size) override;
    Status read(FwSizeType offset, U8* buffer, FwSizeType length) override;
    Status write(FwSizeType offset, const U8* buffer, FwSizeType length) override;
    Status flush() override;
    Status refresh() override;

    //! True when `path` names a regular file (not a directory, link, FIFO or device)
    static bool isRegularFile(const char* path);

  private:
    Os::File m_reader;
    Os::File m_writer;
    Fw::FileNameString m_path;
    bool m_readable;
    bool m_writable;
};

}  // namespace Update
#endif
