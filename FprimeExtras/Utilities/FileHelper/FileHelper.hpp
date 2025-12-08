// ======================================================================
// \title  FileHelper.hpp
// \author starchmd
// \brief  hpp file for FileHelper helper class definition
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================
#ifndef FprimeExtras_Utilities_FileHelper_HPP
#define FprimeExtras_Utilities_FileHelper_HPP
#include <type_traits>

#include "Fw/Buffer/Buffer.hpp"
#include "Fw/FPrimeBasicTypes.hpp"
#include "Fw/Types/Assert.hpp"
#include "Os/File.hpp"

// In order to have proper function overloading that supports both primitive types and serializable types, we need to
// define SFINAE style templates that are only enabled for the specific types they handle.
//
// C++11 allows this via std::enable_if and std::is_fundamental / std::is_base_of type traits, however; the syntax is
// ugly. C++14 is not yet fully supported by users of fprime-extras, so we cannot use the cleaner enabled_if_t.
//
// We can hide the ugly syntax behind these macros to make the code more readable and reusable.
//
// Note: in this file we choose to intercept the return type via SFINAE instead of adding an extra dummy template
//       parameter. This is a style choice, both are valid ways to achieve the same goal.

// SFINAE type for serializable types enabling the function only if T is derived from Fw::Serializable
#define T_SERIALIZABLE_FW_STATUS typename std::enable_if<std::is_base_of<Fw::Serializable, T>::value, Os::File::Status>::type

// SFINAE type for primitive types enabling the function only if T is a fundamental type
#define T_PRIMITIVE_FW_STATUS typename std::enable_if<std::is_fundamental<T>::value, Os::File::Status>::type
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
//! is updated with the contents of the serialized serializable from its current position.
//!
//! \warning It is invalid to call this function on a file that is not open and results in an assertion failure.
//!
//! \tparam T type of the serializable.  Must be subclass of Fw::Serializable.
//! \param file The file to write to, must be opened.
//! \param serializable The serializable to write to a file. Must implement Fw::Serializable interface.
//! \return status of the file write operation
template <typename T>
T_SERIALIZABLE_FW_STATUS writeToFile(Os::File& file, const T& serializable) {
    FW_ASSERT(file.isOpen());
    // Allocate exact size buffer for serialization
    U8 buffer_data[T::SERIALIZED_SIZE];
    Fw::Buffer buffer(buffer_data, T::SERIALIZED_SIZE);
    Fw::SerializeStatus serializeStatus = buffer.getSerializer().serializeFrom(serializable);
    FW_ASSERT(serializeStatus == Fw::SerializeStatus::FW_SERIALIZE_OK);

    return FileHelper ::writeToFile(file, buffer);
}

//! \brief write primitive to file (template version)
//!
//! Writes a primitive to an already opened file. The file must be opened in a mode that allows writing. The file
//! is updated with the contents of the primitive from its current position.
//!
//! \warning It is invalid to call this function on a file that is not open and results in an assertion failure.
//!
//! \tparam T type of the primitive. Must be fundamental.
//! \param filepath The file to write to, must be opened.
//! \param primitive The variable to write as the entire contents of the file
//! \return status of the file write operation
template <typename T>
T_PRIMITIVE_FW_STATUS writeToFile(Os::File& file, T primitive) {
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
//! \tparam T type of object to write to file. Must be fundamental or a subclass of Fw::Serializable.
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

//! \brief read into buffer from file at specified by path
//!
//! Reads a buffer from the file specified by filepath. Buffer must be anchored at the beginning of the file.
//!
//! \warning It is invalid to call this function with a null filepath and results in an assertion failure.
//!
//! \param filepath The path to the file to write to. Will be created or overwritten.
//! \param buffer The buffer to write as the entire contents of the file.
//!
//! \return status of the file read operation
Os::File::Status readFromFile(const CHAR* filepath, Fw::Buffer& buffer);

//! \brief read buffer from file
//!
//! Read a buffer from an already opened file. The file must be opened in a mode that allows reading. The buffer is
//! filled with the contents of the file from its current position.
//!
//! If buffer is invalid, no data is read to the file, and OP_OK is returned.
//!
//! \warning It is invalid to call this function on a file that is not open and results in an assertion failure.
//!
//! \param file The opened file to write to.
//! \param buffer The buffer to write to the file.
//! \return status of the file write operation
Os::File::Status readFromFile(Os::File& file, Fw::Buffer& buffer);

//! \brief read serializable from file (template version)
//!
//! Reads a serializable from an already opened file. The file must be opened in a mode that allows reading. The
//! serializable is deserialized from the contents of the file from its current position. Only the firs
//! serializable is read and ana error is returned on insufficent size.
//!
//! \warning It is invalid to call this function on a file that is not open and results in an assertion failure.
//!
//! \tparam T type of serializable. Must derive from Fw::Serializable.
//! \param file The file to read from, must be opened.
//! \param serializable The serializable to write to a file. Must implement Fw::Serializable interface.
//! \return status of the file write operation
template <typename T>
T_SERIALIZABLE_FW_STATUS readFromFile(Os::File& file, T& serializable) {
    FW_ASSERT(file.isOpen());
    // Allocate exact size buffer for serialization
    U8 buffer_data[T::SERIALIZED_SIZE];
    Fw::Buffer buffer(buffer_data, T::SERIALIZED_SIZE);
    Os::File::Status status = Utilities::FileHelper::readFromFile(file, buffer);
    if (status == Os::File::Status::OP_OK) {
        Fw::SerializeStatus serializeStatus = buffer.getDeserializer().deserializeTo(serializable);
        if (serializeStatus == Fw::SerializeStatus::FW_DESERIALIZE_BUFFER_EMPTY) {
            status = Os::File::Status::BAD_SIZE;
        } else if (serializeStatus != Fw::SerializeStatus::FW_SERIALIZE_OK) {
            status = Os::File::Status::OTHER_ERROR;
        }
    }
    return status;
}

//! \brief read primitive from file (template version)
//!
//! Reads a primitive from an already opened file. The file must be opened in a mode that allows reading. The
//! primitive is updated with the contents of the file from its current position. Only the first primitive is read
//! and an error is returned on insufficient size.
//!
//! If buffer is invalid, no data is read from the file, and OP_OK is returned.
//!
//! \warning It is invalid to call this function on a file that is not open and results in an assertion failure.
//!
//! \tparam T object type to read, must be fundamental.
//! \param filepath The file to write to, must be opened.
//! \param primitive The variable to write as the entire contents of the file
//! \return status of the file write operation
template <typename T>
T_PRIMITIVE_FW_STATUS readFromFile(Os::File& file, T& primitive) {
    static_assert(std::is_fundamental<T>::value,
                  "FileHelper::readBuffer can only be used with fundamental and Fw::Serializable types");
    FW_ASSERT(file.isOpen());
    // Allocate exact size buffer for serialization
    U8 buffer_data[sizeof(T)];
    Fw::Buffer buffer(buffer_data, sizeof(T));
    Os::File::Status status = Utilities::FileHelper::readFromFile(file, buffer);
    if (status == Os::File::Status::OP_OK) {
        Fw::SerializeStatus serializeStatus = buffer.getDeserializer().deserializeTo(primitive);
        if (serializeStatus == Fw::SerializeStatus::FW_DESERIALIZE_BUFFER_EMPTY) {
            status = Os::File::Status::BAD_SIZE;
        } else if (serializeStatus != Fw::SerializeStatus::FW_SERIALIZE_OK) {
            status = Os::File::Status::OTHER_ERROR;
        }
    }
    return status;
}

//! \brief read serializable/primitive to file (template version)
//!
//! Reads a serializable object or primitive to the file specified by filepath. Only the first object is read from
//! the file, returning an error upon insufficient size.
//!
//! \warning It is invalid to call this function with a null filepath and results in an assertion failure.
//!
//! \tparam T the object type to read. Must be fundamental or derive from Fw::Serializable.
//! \param filepath The path to the file to write to. Will be created or overwritten.
//! \param object The  object to write as the entire contents of the file
template <typename T>
Os::File::Status readFromFile(const CHAR* filepath, T& object) {
    FW_ASSERT(filepath != nullptr);
    Os::File file;
    Os::File::Status status = file.open(filepath, Os::File::Mode::OPEN_READ);
    if (status == Os::File::Status::OP_OK) {
        status = readFromFile<T>(file, object);
        file.close();
    }
    return status;
}

}  // namespace FileHelper
}  // namespace Utilities

#endif  // FprimeExtras_Utilities_FileHelper_HPP