// ======================================================================
// \title  DeltaMedia.hpp
// \author starchmd
// \brief  hpp file for the storage abstraction used by DeltaCodec
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaMedia_HPP
#define Update_Delta_DeltaMedia_HPP

#include "Fw/FPrimeBasicTypes.hpp"
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
};

//! \brief DeltaMedia backed by Os::File
//!
//! READ_WRITE access uses one reader and one writer handle since Os::File offers no combined mode.
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

  private:
    Os::File m_reader;
    Os::File m_writer;
    bool m_readable;
    bool m_writable;
};

}  // namespace Update
#endif
