// ======================================================================
// \title  FileHelper.hpp
// \author starchmd
// \brief  hpp file for FileHelper helper class definition
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================
#ifndef FprimeExtras_Utilities_FileHelper_HPP
#define FprimeExtras_Utilities_FileHelper_HPP
#include "Fw/FPrimeBasicTypes.hpp"
#include "Fw/Types/Assert.hpp"
#include "Fw/Buffer/Buffer.hpp"
#include "Os/File.hpp"
#include <type_traits>

namespace Utilities {
namespace FileHelper {

    //! \brief write buffer to file specified by path
    //!
    //! Writes a buffer to the file specified by filepath. If the file already exists, it will be overwritten clearing
    //! any previous contents.
    //!
    //! \warning this function is intended to write the exact contents of the buffer to the file and it does not handle
    //!          appending for multi-object files.
    //! \warning It is invalid to call this function with a null filepath and results in an assertion failure.
    //!
    //! \param filepath The path to the file to write to. Will be created or overwritten.
    //! \param buffer The buffer to write as the entire contents of the file.
    //!
    //! \return status of the file write operation
    Os::File::Status writeToFile(const CHAR* filepath, const Fw::Buffer& buffer);

    //! \brief write buffer to file
    //!
    //! Writes a buffer to an already opened file. The file must be opened in a mode that allows writing. The file is
    //! updated with the contents of the buffer from its current position.
    //!
    //! If buffer is invalid, no data is written to the file, and OP_OK is returned.
    //!
    //! \warning It is invalid to call this function on a file that is not open and results in an assertion failure.
    //!
    //! \param file The opened file to write to.
    //! \param buffer The buffer to write to the file.
    //! \return status of the file write operation
    Os::File::Status writeToFile(Os::File& file, const Fw::Buffer& buffer);


    //! \brief write serializable to file (template version)
    //!
    //! Writes a serializable to an already opened file. The file must be opened in a mode that allows writing. The file
    //! is updated with the contents of the buffer from its current position.
    //!
    //! If buffer is invalid, no data is written to the file, and OP_OK is returned.
    //!
    //! \warning It is invalid to call this function on a file that is not open and results in an assertion failure.
    //!
    //! \param file The file to write to, must be opened.
    //! \param serializable The serializable to write to a file. Must implement Fw::Serializable interface.
    //! \return status of the file write operation
    template <typename T, typename=std::enable_if_t<!std::is_fundamental<T>::value>>
    Os::File::Status writeToFile(Os::File& file, const T& serializable) {
        static_assert(std::is_base_of<Fw::Serializable, T>::value,
            "FileHelper::writeToFile can only be used with Fw::Serializable types");
        static_assert(T::SERIALIZED_SIZE > 0,
            "FileHelper::writeToFile requires SERIALIZED_SIZE to be defined and greater than zero");
        FW_ASSERT(file.isOpen());
        // Allocate exact size buffer for serialization
        U8 buffer_data[T::SERIALIZED_SIZE];
        Fw::Buffer buffer(buffer_data, T::SERIALIZED_SIZE);
        Fw::SerializeStatus serializeStatus = buffer.getSerializer().serializeFrom(serializable);
        FW_ASSERT(serializeStatus == Fw::SerializeStatus::FW_SERIALIZE_OK);

        return FileHelper ::writeToFile(file, buffer);
    }

    //! \brief write variable to file (template version)
    //!
    //! Writes a variable to an already opened file. The file must be opened in a mode that allows writing. The file is
    //! updated with the contents of the buffer from its current position.
    //!
    //! If buffer is invalid, no data is written to the file, and OP_OK is returned.
    //!
    //! \warning It is invalid to call this function on a file that is not open and results in an assertion failure.
    //!
    //! \param filepath The file to write to, must be opened.
    //! \param primitive The variable to write as the entire contents of the file
    //! \return status of the file write operation
    template <typename T, typename = std::enable_if_t<std::is_fundamental<T>::value>>
    Os::File::Status writeToFile(Os::File& file, T primitive) {
        static_assert(std::is_fundamental<T>::value == true,
            "FileHelper::writeBuffer can only be used with fundamental and Fw::Serializable types");
        FW_ASSERT(file.isOpen());
        // Allocate exact size buffer for serialization
        U8 buffer_data[sizeof(T)];
        Fw::Buffer buffer(buffer_data, sizeof(T));
        Fw::SerializeStatus serializeStatus = buffer.getSerializer().serializeFrom(primitive);
        FW_ASSERT(serializeStatus == Fw::SerializeStatus::FW_SERIALIZE_OK);

        return FileHelper ::writeToFile(file, buffer);
    }

    //! \brief write serializable/primitive to file (template version)
    //!
    //! Writes a serializable object or primitive to the file specified by filepath. If the file already exists, it
    //! will be overwritten clearing any previous contents.
    //!
    //! \warning this function is intended to write the exact contents of the serializable to the file and it does not
    //!          handle appending for multi-object files.
    //! \warning It is invalid to call this function with a null filepath and results in an assertion failure.
    //!
    //! \param filepath The path to the file to write to. Will be created or overwritten.
    //! \param object The  object to write as the entire contents of the file
    template <typename T>
    Os::File::Status writeToFile(const CHAR* filepath, const T& object) {
        FW_ASSERT(filepath != nullptr);
        Os::File file;
        Os::File::Status status = file.open(filepath, Os::File::Mode::OPEN_CREATE, Os::File::OverwriteType::OVERWRITE);
        if (status == Os::File::Status::OP_OK) {
            status = writeToFile<T>(file, object);
            file.close();
        }
        return status;
    }
} // namespace FileHelper
} // namespace Utilities

#endif  // FprimeExtras_Utilities_FileHelper_HPP