// ======================================================================
// \title  DeltaTestMain.cpp
// \author starchmd
// \brief  Unit tests for DeltaWindow, the shipped DeltaCoders, DeltaFileMedia,
// and the DeltaCodec engine
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderLzss.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderNone.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderRle.hpp"
#include "FprimeExtras/Update/Delta/test/ut/DeltaTestSupport.hpp"
#include "FprimeExtras/Update/Delta/test/ut/DeltaTestVectors.hpp"
#include "Os/FileSystem.hpp"
#include "Utils/Hash/Crc32/Crc32.hpp"

using namespace Update;

// ----------------------------------------------------------------------
// DeltaWindow
// ----------------------------------------------------------------------
TEST(DeltaWindow, PushPeekPopHistory) {
    U8 storage[8];
    DeltaWindow ring(storage, sizeof storage);
    ring.reset();
    EXPECT_EQ(ring.capacity(), 8u);
    EXPECT_EQ(ring.count(), 0u);
    EXPECT_EQ(ring.space(), 8u);
    EXPECT_EQ(ring.historyAvailable(), 0u);
    for (U8 i = 1; i <= 8; i++) {
        ring.push(i);
    }
    EXPECT_EQ(ring.space(), 0u);
    EXPECT_EQ(ring.peek(0), 1);
    EXPECT_EQ(ring.peek(7), 8);
    EXPECT_EQ(ring.history(1), 8);
    EXPECT_EQ(ring.history(8), 1);
    ring.pop(5);
    EXPECT_EQ(ring.count(), 3u);
    EXPECT_EQ(ring.peek(0), 6);
    // Consumed bytes remain in history until overwritten
    EXPECT_EQ(ring.history(8), 1);
    EXPECT_EQ(ring.historyAvailable(), 8u);
    ring.push(9);
    ring.push(10);
    EXPECT_EQ(ring.count(), 5u);
    EXPECT_EQ(ring.history(1), 10);
    EXPECT_EQ(ring.history(8), 3);
    U8 out[5];
    ring.popInto(out, 5);
    EXPECT_EQ(out[0], 6);
    EXPECT_EQ(out[4], 10);
    EXPECT_EQ(ring.count(), 0u);
    EXPECT_EQ(ring.space(), 8u);
    EXPECT_EQ(ring.historyAvailable(), 8u);
    ring.reset();
    EXPECT_EQ(ring.historyAvailable(), 0u);
    EXPECT_EQ(ring.count(), 0u);
    // History persists across many evictions: window holds exactly the last `capacity` pushes
    for (U32 i = 0; i < 1000; i++) {
        ring.push(static_cast<U8>(i));
        ring.pop(1);
    }
    EXPECT_EQ(ring.historyAvailable(), 8u);
    EXPECT_EQ(ring.history(1), static_cast<U8>(999));
    EXPECT_EQ(ring.history(8), static_cast<U8>(992));
}

// ----------------------------------------------------------------------
// DeltaCodec: memory footprint
// ----------------------------------------------------------------------

TEST(DeltaCodec, MemoryFootprint) {
    // REQ: engine + coder state fits the constrained-system budget with default
    // configuration
    const FwSizeType buffers =
        DELTA_PATCH_BUFFER_SIZE + DELTA_WINDOW_SIZE + DELTA_OUTPUT_BUFFER_SIZE + DELTA_OLD_BUFFER_SIZE;
    printf(
        "sizeof(DeltaCodec)=%zu buffers=%zu Lzss=%zu Rle=%zu None=%zu "
        "FileMedia=%zu\n",
        sizeof(DeltaCodec), static_cast<size_t>(buffers), sizeof(DeltaCoderLzss), sizeof(DeltaCoderRle),
        sizeof(DeltaCoderNone), sizeof(DeltaFileMedia));
    EXPECT_LE(sizeof(DeltaCodec) + sizeof(DeltaCoderLzss), 1024u);
}

// ----------------------------------------------------------------------
// DeltaFileMedia: end-to-end through real files
// ----------------------------------------------------------------------

static void writeFile(const char* path, const U8* data, FwSizeType size) {
    FILE* file = fopen(path, "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(fwrite(data, 1, size, file), size);
    fclose(file);
}

static std::vector<U8> readFile(const char* path) {
    std::vector<U8> out;
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return out;
    }
    U8 buffer[512];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof buffer, file)) > 0) {
        out.insert(out.end(), buffer, buffer + n);
    }
    fclose(file);
    return out;
}

TEST(DeltaFileMedia, ApplyThroughFiles) {
    const char* oldPath = "delta_ut_old.bin";
    const char* patchPath = "delta_ut_patch.spatch";
    const char* newPath = "delta_ut_new.bin";
    Os::FileSystem::removeFile(newPath);
    writeFile(oldPath, TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    writeFile(patchPath, TestVectors::PATCH_LZSS, TestVectors::PATCH_LZSS_SIZE);

    DeltaFileMedia oldMedia;
    DeltaFileMedia patchMedia;
    DeltaFileMedia newMedia;
    DeltaFileMedia missing;
    EXPECT_NE(missing.open("delta_ut_does_not_exist.bin", DeltaFileMedia::READ_ONLY), Os::File::OP_OK);
    EXPECT_FALSE(missing.isOpen());
    ASSERT_EQ(oldMedia.open(oldPath, DeltaFileMedia::READ_ONLY), Os::File::OP_OK);
    ASSERT_EQ(patchMedia.open(patchPath, DeltaFileMedia::READ_ONLY), Os::File::OP_OK);
    ASSERT_EQ(newMedia.open(newPath, DeltaFileMedia::READ_WRITE), Os::File::OP_OK);
    FwSizeType size = 0;
    ASSERT_EQ(newMedia.size(size), DeltaMedia::OP_OK);
    EXPECT_EQ(size, 0u);
    U8 byte = 0;
    EXPECT_EQ(oldMedia.write(0, &byte, 1), DeltaMedia::NOT_OPEN);
    EXPECT_EQ(oldMedia.read(TestVectors::OLD_IMAGE_SIZE, &byte, 1), DeltaMedia::OUT_OF_RANGE);

    DeltaCoderLzss coder;
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    U32 steps = 0;
    ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
    newMedia.close();
    EXPECT_EQ(readFile(newPath), toVector(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE));

    // Re-open preserves the finished output and the engine resumes everything
    ASSERT_EQ(newMedia.open(newPath, DeltaFileMedia::READ_WRITE), Os::File::OP_OK);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.resumedChunks(), codec.chunkCount());

    oldMedia.close();
    patchMedia.close();
    newMedia.close();
    Os::FileSystem::removeFile(oldPath);
    Os::FileSystem::removeFile(patchPath);
    Os::FileSystem::removeFile(newPath);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(DeltaFileMedia, NotOpenAndBadPaths) {
    DeltaFileMedia media;
    FwSizeType size = 0;
    U8 byte = 0;
    EXPECT_EQ(media.size(size), DeltaMedia::NOT_OPEN);
    EXPECT_EQ(media.read(0, &byte, 1), DeltaMedia::NOT_OPEN);
    EXPECT_EQ(media.write(0, &byte, 1), DeltaMedia::NOT_OPEN);
    EXPECT_EQ(media.flush(), DeltaMedia::OP_OK);  // nothing open, nothing to commit
    EXPECT_NE(media.open("/nonexistent/dir/spatch-ut.bin", DeltaFileMedia::READ_ONLY), Os::File::OP_OK);
    EXPECT_NE(media.open("/tmp", DeltaFileMedia::READ_WRITE), Os::File::OP_OK);
    // Read-only media refuse writes
    char path[] = "/tmp/spatch-ro-XXXXXX";
    const int fd = mkstemp(path);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::write(fd, "abc", 3), 3);
    ::close(fd);
    ASSERT_EQ(media.open(path, DeltaFileMedia::READ_ONLY), Os::File::OP_OK);
    EXPECT_EQ(media.write(0, &byte, 1), DeltaMedia::NOT_OPEN);
    EXPECT_EQ(media.size(size), DeltaMedia::OP_OK);
    EXPECT_EQ(size, 3u);
    EXPECT_EQ(media.read(2, &byte, 1), DeltaMedia::OP_OK);
    EXPECT_EQ(byte, static_cast<U8>('c'));
    EXPECT_NE(media.read(3, &byte, 1), DeltaMedia::OP_OK);
    media.close();
    ::unlink(path);
}

// A FIFO swapped in for the output path mid-patch must fail the commit, not block the (rate-group) thread in open()
TEST(DeltaFileMedia, FlushRejectsPathSwappedForFifo) {
    char dir[] = "/tmp/spatch-fifo-XXXXXX";
    ASSERT_NE(mkdtemp(dir), nullptr);
    std::string path = std::string(dir) + "/out.bin";
    DeltaFileMedia media;
    ASSERT_EQ(media.open(path.c_str(), DeltaFileMedia::CREATE), Os::File::OP_OK);
    const U8 bytes[4] = {1, 2, 3, 4};
    ASSERT_EQ(media.write(0, bytes, sizeof(bytes)), DeltaMedia::OP_OK);
    ASSERT_EQ(media.flush(), DeltaMedia::OP_OK);
    ASSERT_EQ(::unlink(path.c_str()), 0);
    ASSERT_EQ(::mkfifo(path.c_str(), 0600), 0);
    EXPECT_EQ(media.write(4, bytes, sizeof(bytes)), DeltaMedia::OP_OK);  // the orphaned inode still accepts writes
    EXPECT_EQ(media.flush(), DeltaMedia::IO_ERROR);                      // but the commit refuses the swapped path
    EXPECT_FALSE(media.isOpen());
    EXPECT_EQ(media.refresh(), DeltaMedia::NOT_OPEN);
    media.close();
    ::unlink(path.c_str());
    ::rmdir(dir);
}
