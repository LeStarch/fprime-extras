// ======================================================================
// \title  DeltaMedia.cpp
// \author starchmd
// \brief  cpp file for the Os::File backed DeltaMedia
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include "FprimeExtras/Update/Delta/DeltaMedia.hpp"

#include "Fw/Types/Assert.hpp"

namespace Update {

DeltaFileMedia::DeltaFileMedia() : m_reader(), m_writer(), m_readable(false), m_writable(false) {}

DeltaFileMedia::~DeltaFileMedia() {
    this->close();
}

Os::File::Status DeltaFileMedia::open(const char* path, Access access) {
    FW_ASSERT(path != nullptr);
    this->close();
    Os::File::Status status = Os::File::OP_OK;
    if (access == CREATE) {
        status = this->m_writer.open(path, Os::File::OPEN_CREATE, Os::File::OverwriteType::OVERWRITE);
        this->m_writable = (status == Os::File::OP_OK);
    } else if (access == READ_WRITE) {
        // Preserve existing content (resume); create only when absent
        status = this->m_writer.open(path, Os::File::OPEN_WRITE, Os::File::OverwriteType::NO_OVERWRITE);
        if (status == Os::File::DOESNT_EXIST) {
            status = this->m_writer.open(path, Os::File::OPEN_CREATE, Os::File::OverwriteType::NO_OVERWRITE);
        }
        this->m_writable = (status == Os::File::OP_OK);
    }
    if (status == Os::File::OP_OK) {
        status = this->m_reader.open(path, Os::File::OPEN_READ, Os::File::OverwriteType::NO_OVERWRITE);
        this->m_readable = (status == Os::File::OP_OK);
    }
    if (status != Os::File::OP_OK) {
        this->close();
    }
    return status;
}

void DeltaFileMedia::close() {
    if (this->m_readable) {
        this->m_reader.close();
    }
    if (this->m_writable) {
        this->m_writer.close();
    }
    this->m_readable = false;
    this->m_writable = false;
}

bool DeltaFileMedia::isOpen() const {
    return this->m_readable;
}

DeltaMedia::Status DeltaFileMedia::size(FwSizeType& size) {
    if (!this->m_readable) {
        return NOT_OPEN;
    }
    return (this->m_reader.size(size) == Os::File::OP_OK) ? OP_OK : IO_ERROR;
}

DeltaMedia::Status DeltaFileMedia::read(FwSizeType offset, U8* buffer, FwSizeType length) {
    FW_ASSERT(buffer != nullptr);
    if (!this->m_readable) {
        return NOT_OPEN;
    }
    if (this->m_reader.seek_absolute(offset) != Os::File::OP_OK) {
        return IO_ERROR;
    }
    // Each pass transfers at least one byte or returns, so `length` passes bound the loop
    FwSizeType total = 0;
    for (FwSizeType pass = 0; pass < length && total < length; pass++) {
        FwSizeType requested = length - total;
        const Os::File::Status status = this->m_reader.read(buffer + total, requested, Os::File::WaitType::WAIT);
        if (status != Os::File::OP_OK) {
            return IO_ERROR;
        }
        if (requested == 0) {
            return OUT_OF_RANGE;
        }
        total += requested;
    }
    return (total == length) ? OP_OK : IO_ERROR;
}

DeltaMedia::Status DeltaFileMedia::write(FwSizeType offset, const U8* buffer, FwSizeType length) {
    FW_ASSERT(buffer != nullptr);
    if (!this->m_writable) {
        return NOT_OPEN;
    }
    if (this->m_writer.seek_absolute(offset) != Os::File::OP_OK) {
        return IO_ERROR;
    }
    // Each pass transfers at least one byte or returns, so `length` passes bound the loop
    FwSizeType total = 0;
    for (FwSizeType pass = 0; pass < length && total < length; pass++) {
        FwSizeType requested = length - total;
        const Os::File::Status status = this->m_writer.write(buffer + total, requested, Os::File::WaitType::WAIT);
        if (status != Os::File::OP_OK) {
            return IO_ERROR;
        }
        if (requested == 0) {
            return IO_ERROR;
        }
        total += requested;
    }
    return (total == length) ? OP_OK : IO_ERROR;
}

DeltaMedia::Status DeltaFileMedia::flush() {
    if (!this->m_writable) {
        return OP_OK;
    }
    return (this->m_writer.flush() == Os::File::OP_OK) ? OP_OK : IO_ERROR;
}

}  // namespace Update
