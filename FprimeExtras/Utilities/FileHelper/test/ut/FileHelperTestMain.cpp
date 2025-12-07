// ======================================================================
// \title  FileHelperTestMain.cpp
// \author starchmd
// \brief  cpp file for FileHelper unit tests
// \copyright Copyright (c) 2025 Michael Starch
// ======================================================================
#include <gtest/gtest.h>
#include "Fw/Time/Time.hpp"
#include "Os/FileSystem.hpp"
#include "FprimeExtras/Utilities/FileHelper/FileHelper.hpp"

//! \brief test Serializable class for testing
struct TestSerializable : public Fw::Serializable {
    enum {
        SERIALIZED_SIZE = 2
    };

    Fw::SerializeStatus serializeTo(Fw::SerialBufferBase& buffer, Fw::Endianness mode = Fw::Endianness::BIG) const {
        EXPECT_EQ(buffer.serializeFrom(static_cast<I8>(this->m_int)), Fw::FW_SERIALIZE_OK);
        EXPECT_EQ(buffer.serializeFrom(static_cast<U8>(this->m_uint)), Fw::FW_SERIALIZE_OK);
        return Fw::FW_SERIALIZE_OK;
    }

    Fw::SerializeStatus deserializeFrom(Fw::SerialBufferBase& buffer, Fw::Endianness mode = Fw::Endianness::BIG) {
        I8 incoming_int = 0;
        U8 incoming_uint = 0;
        EXPECT_EQ(buffer.deserializeTo(incoming_int), Fw::FW_SERIALIZE_OK);
        EXPECT_EQ(buffer.deserializeTo(incoming_uint), Fw::FW_SERIALIZE_OK);
        return Fw::FW_SERIALIZE_OK;
    }
    I64 m_int;
    U64 m_uint;
};


const CHAR* TEST_FILEPATH = "testfile.bin";

// \!brief helper function to test direct readback of types
U64 readback(FwSizeType size);

//! \!brief helper function to test writing of primitive types
template <typename T, FwSizeType S>
void testWritingType(T value) {
    Utilities::FileHelper::writeToFile<T>(TEST_FILEPATH, value);
    FwSizeType read_size = 0;
    ASSERT_EQ(Os::FileSystem::getFileSize(TEST_FILEPATH, read_size), Os::FileSystem::Status::OP_OK);
    ASSERT_EQ(read_size, S);
}

//! \!brief helper function to test writing of primitive types
template <typename T>
void testWritingType(T value) {
    static_assert(std::is_fundamental<T>::value,
        "testWritingType can only be used with fundamental types");
    testWritingType<T, sizeof(T)>(value);
}



U64 readback(FwSizeType size) {
    U8 readback_buffer[sizeof(U64)] = {0};
    U64 result = 0;
    // Read back and demand success
    Os::File file;
    FwSizeType read_size = size;
    EXPECT_EQ(file.open(TEST_FILEPATH, Os::File::Mode::OPEN_READ), Os::File::Status::OP_OK);
    EXPECT_EQ(file.read(readback_buffer, read_size), Os::File::Status::OP_OK);
    file.close();
    EXPECT_EQ(read_size, size);

    // Read big-endian as F Prime natively uses big-endian serialization
    for (FwSizeType i = 0; i < size; i++) {
        result = result << 8 | readback_buffer[i];
    }
    return result;
}

TEST(FileHelperTest, PrimitiveTypes) {
    {
        I8 value = -42;
        testWritingType<I8>(static_cast<I8>(value));
        ASSERT_EQ(value, static_cast<I8>(readback(sizeof(I8))));
    }
    {
        U8 value = 42;
        testWritingType<U8>(static_cast<U8>(value));
        ASSERT_EQ(value, static_cast<U8>(readback(sizeof(U8))));
    }
    {
        I16 value = -42;
        testWritingType<I16>(static_cast<I16>(value));
        ASSERT_EQ(value, static_cast<I16>(readback(sizeof(I16))));
    }
    {
        U16 value = 42;
        testWritingType<U16>(static_cast<U16>(value));
        ASSERT_EQ(value, static_cast<U16>(readback(sizeof(U16))));
    }
    {
        I32 value = -42;
        testWritingType<I32>(static_cast<I32>(value));
        ASSERT_EQ(value, static_cast<I32>(readback(sizeof(I32))));
    }
    {
        U32 value = 42;
        testWritingType<U32>(static_cast<U32>(value));
        ASSERT_EQ(value, static_cast<U32>(readback(sizeof(U32))));
    }
    {
        I64 value = -42;
        testWritingType<I64>(static_cast<I64>(value));
        ASSERT_EQ(value, static_cast<I64>(readback(sizeof(I64))));
    }
    {
        U64 value = 42;
        testWritingType<U64>(static_cast<U64>(value));
        ASSERT_EQ(value, static_cast<U64>(readback(sizeof(U64))));
    }
    {
        F32 value = -42.0f;
        testWritingType<F32>(static_cast<F32>(value));
        U32 return_value = static_cast<U32>(readback(sizeof(F32)));
        ASSERT_EQ(value, *reinterpret_cast<F32*>(&return_value));
    }
        {
        F64 value = -42.0f;
        testWritingType<F64>(static_cast<F64>(value));
        U64 return_value = static_cast<U64>(readback(sizeof(F64)));
        ASSERT_EQ(value, *reinterpret_cast<F64*>(&return_value));
    }
}

TEST(FileHelperTest, SerializableTypes) {
    TestSerializable test_object;
    test_object.m_int = -42;
    test_object.m_uint = 42;
    testWritingType<TestSerializable, TestSerializable::SERIALIZED_SIZE>(test_object);

    Fw::TimeValue test_time(TimeBase::TB_WORKSTATION_TIME, 0, 1234, 5678);
    testWritingType<Fw::TimeValue, Fw::TimeValue::SERIALIZED_SIZE>(test_time);
}

TEST(FileHelperTest, DeathNullPointer) {
    TestSerializable test_object;
    ASSERT_DEATH(Utilities::FileHelper::writeToFile<TestSerializable>(nullptr, test_object), ".*FileHelper");
}
//TEST(FileHelperTest, DeathNoFile) {
//    Os::File file;
//    Fw::TimeValue test_time(TimeBase::TB_WORKSTATION_TIME, 0, 1234, 5678);
//    ASSERT_DEATH(Utilities::FileHelper::writeToFile<Fw::TimeValue>(file, test_time), ".*FileHelper");
//}


int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    int status = RUN_ALL_TESTS();
    (void) Os::FileSystem::removeFile("testfile.bin");
    return status;
}
