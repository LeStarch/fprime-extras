// ======================================================================
// \title  FileHelper.cpp
// \author starchmd
// \brief  cpp file for FileHelper helper class implementation
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================
#include "FprimeExtras/Utilities/FileHelper/FileHelper.hpp"
namespace Utilities {
namespace FileHelper {

Os::File::Status writeToFile(const CHAR* filepath, const Fw::Buffer& buffer) {
    FW_ASSERT(filepath != nullptr);
    Os::File file;
    Os::File::Status status = file.open(filepath, Os::File::Mode::OPEN_CREATE, Os::File::OverwriteType::OVERWRITE);
    if (status == Os::File::Status::OP_OK) {
        status = writeToFile(file, buffer);
        file.close();
    }
    return status;
}

Os::File::Status writeToFile(Os::File& file, const Fw::Buffer& buffer) {
    FW_ASSERT(file.isOpen());
    Os::File::Status status = Os::File::Status::OP_OK;
    FwSizeType write_size = buffer.getSize();
    if (buffer.isValid() && (write_size > 0)) {
        status = file.write(buffer.getData(), write_size);
    }
    return status;
}

Os::File::Status readFromFile(const CHAR* filepath, Fw::Buffer& buffer) {
    FW_ASSERT(filepath != nullptr);
    Os::File file;
    Os::File::Status status = file.open(filepath, Os::File::Mode::OPEN_READ);
    if (status == Os::File::Status::OP_OK) {
        status = readFromFile(file, buffer);
        file.close();
    }
    return status;
}

Os::File::Status readFromFile(Os::File& file, Fw::Buffer& buffer) {
    FW_ASSERT(file.isOpen());
    Os::File::Status status = Os::File::Status::OP_OK;
    FwSizeType read_size = buffer.getSize();
    if (buffer.isValid() && (read_size > 0)) {
        status = file.read(buffer.getData(), read_size);
    }
    // Check for fill read and if that failed, return BAD_SIZE
    if ((status == Os::File::Status::OP_OK) && (read_size != buffer.getSize())) {
        status = Os::File::Status::BAD_SIZE;
    }
    return status;
}

}  // namespace FileHelper
}  // namespace Utilities