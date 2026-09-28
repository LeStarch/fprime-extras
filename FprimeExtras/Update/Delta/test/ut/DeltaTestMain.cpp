// ======================================================================
// \title  DeltaTestMain.cpp
// \author starchmd
// \brief  Unit tests for DeltaRing, the shipped DeltaCoders, DeltaFileMedia,
// and the DeltaCodec engine
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include <gtest/gtest.h>

#include <cstdio>
#include <vector>

#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"
#include "FprimeExtras/Update/Delta/test/ut/DeltaTestVectors.hpp"
#include "Os/FileSystem.hpp"
#include "Utils/Hash/Crc32/Crc32.hpp"

using namespace Update;

// ----------------------------------------------------------------------
// Test helpers
// ----------------------------------------------------------------------

//! In-memory media with fault injection
class MemoryMedia final : public DeltaMedia {
public:
  explicit MemoryMedia(const U8 *data = nullptr, FwSizeType size = 0)
      : data(data, data + size), failReads(false), failWrites(false),
        failFlush(false), corruptStored(false), reads(0), writes(0), flushes(0) {}

  Status size(FwSizeType &size) override {
    size = this->data.size();
    return OP_OK;
  }
  Status read(FwSizeType offset, U8 *buffer, FwSizeType length) override {
    this->reads++;
    if (this->failReads) {
      return IO_ERROR;
    }
    if (offset + length > this->data.size()) {
      return OUT_OF_RANGE;
    }
    for (FwSizeType i = 0; i < length; i++) {
      buffer[i] = this->data[offset + i];
    }
    return OP_OK;
  }
  Status write(FwSizeType offset, const U8 *buffer,
               FwSizeType length) override {
    this->writes++;
    if (this->failWrites) {
      return IO_ERROR;
    }
    if (offset + length > this->data.size()) {
      this->data.resize(offset + length);
    }
    for (FwSizeType i = 0; i < length; i++) {
      this->data[offset + i] = buffer[i];
    }
    if (this->corruptStored) {
      // Storage accepts the write but keeps a flipped bit: only read-back can catch this
      this->data[offset] ^= 0x01;
    }
    return OP_OK;
  }
  Status flush() override {
    this->flushes++;
    return this->failFlush ? IO_ERROR : OP_OK;
  }

  std::vector<U8> data;
  bool failReads;
  bool failWrites;
  bool failFlush;
  bool corruptStored;
  U32 reads;
  U32 writes;
  U32 flushes;
};

static std::vector<U8> toVector(const U8 *data, FwSizeType size) {
  return std::vector<U8>(data, data + size);
}

//! Run an engine to completion or failure; returns the final status. Caps steps
//! to guarantee termination.
static DeltaCodec::Status run(DeltaCodec &codec, U32 &steps) {
  steps = 0;
  DeltaCodec::Status status = DeltaCodec::OP_OK;
  while (codec.state() != DeltaCodec::COMPLETE &&
         codec.state() != DeltaCodec::FAILED && steps < 10000) {
    status = codec.step();
    steps++;
  }
  return status;
}

//! Feed coded data through a coder in `pieceSize` slices, draining the ring
//! into `out`; mimics DeltaCodec::fillRing
static DeltaCoder::Status decodeAll(DeltaCoder &coder, const U8 *coded,
                                    FwSizeType codedSize, FwSizeType pieceSize,
                                    FwSizeType drainSize,
                                    std::vector<U8> &out) {
  U8 storage[DELTA_RING_SIZE];
  DeltaRing ring(storage, sizeof storage);
  ring.reset();
  coder.reset();
  FwSizeType pos = 0;
  while (true) {
    FwSizeType consumed = 0;
    const FwSizeType before = ring.count();
    const FwSizeType avail =
        ((codedSize - pos) < pieceSize) ? (codedSize - pos) : pieceSize;
    const DeltaCoder::Status status =
        coder.decode(coded + pos, avail, consumed, ring);
    if (status != DeltaCoder::OP_OK) {
      return status;
    }
    pos += consumed;
    const bool produced = ring.count() != before;
    const FwSizeType n = (ring.count() < drainSize) ? ring.count() : drainSize;
    for (FwSizeType i = 0; i < n; i++) {
      out.push_back(ring.peek(i));
    }
    ring.pop(n);
    if (consumed == 0 && !produced && n == 0) {
      return (pos < codedSize) ? DeltaCoder::MALFORMED
                               : DeltaCoder::OP_OK; // stuck vs. drained
    }
  }
}

// ----------------------------------------------------------------------
// DeltaRing
// ----------------------------------------------------------------------

TEST(DeltaRing, PushPeekPopHistory) {
  U8 storage[8];
  DeltaRing ring(storage, sizeof storage);
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
  EXPECT_EQ(ring.history(1), 10);
  EXPECT_EQ(ring.history(8), 3);
  U8 out[5];
  ring.popInto(out, 5);
  EXPECT_EQ(out[0], 6);
  EXPECT_EQ(out[4], 10);
  EXPECT_EQ(ring.count(), 0u);
  ring.reset();
  EXPECT_EQ(ring.historyAvailable(), 0u);
  EXPECT_EQ(ring.history(1), 0);
}

// ----------------------------------------------------------------------
// Coders
// ----------------------------------------------------------------------

static void checkCoder(DeltaCoder &coder, const U8 *coded,
                       FwSizeType codedSize) {
  const std::vector<U8> expected =
      toVector(TestVectors::CODER_RAW, TestVectors::CODER_RAW_SIZE);
  const FwSizeType pieces[] = {1, 2, 7, 64, 10000};
  const FwSizeType drains[] = {1, 3, 100, 10000};
  for (FwSizeType piece : pieces) {
    for (FwSizeType drain : drains) {
      std::vector<U8> out;
      ASSERT_EQ(decodeAll(coder, coded, codedSize, piece, drain, out),
                DeltaCoder::OP_OK)
          << "piece " << piece << " drain " << drain;
      ASSERT_EQ(out, expected) << "piece " << piece << " drain " << drain;
    }
  }
}

TEST(DeltaCoder, NoneGolden) {
  DeltaCoderNone coder;
  EXPECT_EQ(coder.id(), DeltaCoder::ID_NONE);
  checkCoder(coder, TestVectors::CODER_NONE, TestVectors::CODER_NONE_SIZE);
}

TEST(DeltaCoder, RleGolden) {
  DeltaCoderRle coder;
  EXPECT_EQ(coder.id(), DeltaCoder::ID_RLE);
  checkCoder(coder, TestVectors::CODER_RLE, TestVectors::CODER_RLE_SIZE);
}

TEST(DeltaCoder, LzssGolden) {
  DeltaCoderLzss coder;
  EXPECT_EQ(coder.id(), DeltaCoder::ID_LZSS);
  checkCoder(coder, TestVectors::CODER_LZSS, TestVectors::CODER_LZSS_SIZE);
}

TEST(DeltaCoder, LzssHandCrafted) {
  // flags 0b01000000: literal 'a', then match distance 1 length 3 -> "aaaa"
  const U8 coded[] = {0x40, 'a', 0x00, 0x00};
  DeltaCoderLzss coder;
  std::vector<U8> out;
  ASSERT_EQ(decodeAll(coder, coded, sizeof coded, 1, 1, out),
            DeltaCoder::OP_OK);
  ASSERT_EQ(out, std::vector<U8>({'a', 'a', 'a', 'a'}));
  // Match before any history is malformed
  const U8 bad[] = {0x80, 0x00, 0x00};
  out.clear();
  ASSERT_EQ(decodeAll(coder, bad, sizeof bad, 100, 100, out),
            DeltaCoder::MALFORMED);
  // Match distance exceeding available history is malformed
  const U8 far[] = {0x40, 'a', 0x05, 0x00};
  out.clear();
  ASSERT_EQ(decodeAll(coder, far, sizeof far, 100, 100, out),
            DeltaCoder::MALFORMED);
}

TEST(DeltaCoder, RleHandCrafted) {
  const U8 coded[] = {0x01, 'x', 'y', 0x81, 'z'}; // 2 literals, then 'z' x3
  DeltaCoderRle coder;
  std::vector<U8> out;
  ASSERT_EQ(decodeAll(coder, coded, sizeof coded, 1, 1, out),
            DeltaCoder::OP_OK);
  ASSERT_EQ(out, std::vector<U8>({'x', 'y', 'z', 'z', 'z'}));
}

// ----------------------------------------------------------------------
// DeltaCodec: nominal application with every shipped coder
// ----------------------------------------------------------------------

struct PatchCase {
  const char *name;
  const U8 *patch;
  FwSizeType size;
};

static void applyNominal(DeltaCoder &coder, const U8 *patch,
                         FwSizeType patchSize) {
  MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  MemoryMedia patchMedia(patch, patchSize);
  MemoryMedia newMedia;
  DeltaCodec codec(coder);
  ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::VERIFY_OLD);
  EXPECT_EQ(codec.oldSize(), TestVectors::OLD_IMAGE_SIZE);
  EXPECT_EQ(codec.newSize(), TestVectors::NEW_IMAGE_SIZE);
  EXPECT_EQ(codec.oldCrc(), TestVectors::OLD_CRC);
  EXPECT_EQ(codec.newCrc(), TestVectors::NEW_CRC);
  const U32 expectedChunks =
      (TestVectors::NEW_IMAGE_SIZE + TestVectors::CHUNK_BYTES - 1) /
      TestVectors::CHUNK_BYTES;
  EXPECT_EQ(codec.chunkCount(), expectedChunks);
  U32 steps = 0;
  ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::COMPLETE);
  EXPECT_EQ(codec.chunkIndex(), expectedChunks);
  EXPECT_EQ(codec.resumedChunks(), 0u);
  EXPECT_EQ(codec.bytesWritten(), TestVectors::NEW_IMAGE_SIZE);
  // One step verifies the old image (2000 B < 4096 B budget), one per chunk,
  // one to finish (flush), one to read the new image back (< 4096 B)
  EXPECT_EQ(steps, 1u + expectedChunks + 1u + 1u);
  EXPECT_EQ(newMedia.flushes, 1u);
  EXPECT_EQ(newMedia.data,
            toVector(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE));
}

TEST(DeltaCodec, ApplyNone) {
  DeltaCoderNone coder;
  applyNominal(coder, TestVectors::PATCH_NONE, TestVectors::PATCH_NONE_SIZE);
}

TEST(DeltaCodec, ApplyRle) {
  DeltaCoderRle coder;
  applyNominal(coder, TestVectors::PATCH_RLE, TestVectors::PATCH_RLE_SIZE);
}

TEST(DeltaCodec, ApplyLzss) {
  DeltaCoderLzss coder;
  applyNominal(coder, TestVectors::PATCH_LZSS, TestVectors::PATCH_LZSS_SIZE);
}

TEST(DeltaCodec, MemoryFootprint) {
  // REQ: engine + coder state fits the constrained-system budget with default
  // configuration
  const FwSizeType buffers =
      DELTA_PATCH_BUFFER_SIZE + DELTA_RING_SIZE + DELTA_OUTPUT_BUFFER_SIZE;
  printf("sizeof(DeltaCodec)=%zu buffers=%zu Lzss=%zu Rle=%zu None=%zu "
         "FileMedia=%zu\n",
         sizeof(DeltaCodec), static_cast<size_t>(buffers),
         sizeof(DeltaCoderLzss), sizeof(DeltaCoderRle), sizeof(DeltaCoderNone),
         sizeof(DeltaFileMedia));
  EXPECT_LE(sizeof(DeltaCodec) + sizeof(DeltaCoderLzss), 1024u);
}

// ----------------------------------------------------------------------
// DeltaCodec: resume
// ----------------------------------------------------------------------

TEST(DeltaCodec, ResumeSkipsVerifiedChunks) {
  DeltaCoderLzss coder;
  MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  MemoryMedia patchMedia(TestVectors::PATCH_LZSS, TestVectors::PATCH_LZSS_SIZE);
  // Output already holds 3 good chunks, a corrupt 4th chunk, and a partial 5th
  const FwSizeType present = 4 * TestVectors::CHUNK_BYTES + 17;
  MemoryMedia newMedia(TestVectors::NEW_IMAGE, present);
  newMedia.data[3 * TestVectors::CHUNK_BYTES + 5] ^= 0xFF;
  DeltaCodec codec(coder);
  ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::VERIFY_NEW);
  U32 steps = 0;
  ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::COMPLETE);
  EXPECT_EQ(codec.resumedChunks(), 3u);
  EXPECT_EQ(newMedia.data,
            toVector(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE));
}

TEST(DeltaCodec, ResumeCompleteOutput) {
  DeltaCoderLzss coder;
  MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  MemoryMedia patchMedia(TestVectors::PATCH_LZSS, TestVectors::PATCH_LZSS_SIZE);
  MemoryMedia newMedia(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE);
  DeltaCodec codec(coder);
  ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
  U32 steps = 0;
  ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::COMPLETE);
  EXPECT_EQ(codec.resumedChunks(), codec.chunkCount());
  EXPECT_EQ(newMedia.writes, 0u);
}

TEST(DeltaCodec, OutputStale) {
  DeltaCoderLzss coder;
  MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  MemoryMedia patchMedia(TestVectors::PATCH_LZSS, TestVectors::PATCH_LZSS_SIZE);
  std::vector<U8> big(TestVectors::NEW_IMAGE_SIZE + 1, 0);
  MemoryMedia newMedia(big.data(), big.size());
  DeltaCodec codec(coder);
  EXPECT_EQ(codec.begin(oldMedia, patchMedia, newMedia),
            DeltaCodec::OUTPUT_STALE);
  EXPECT_EQ(codec.state(), DeltaCodec::FAILED);
}

// ----------------------------------------------------------------------
// DeltaCodec: rejection paths
// ----------------------------------------------------------------------

static DeltaCodec::Status applyPatch(DeltaCoder &coder,
                                     const std::vector<U8> &patch,
                                     const std::vector<U8> &oldImage,
                                     DeltaCodec::State &finalState) {
  MemoryMedia oldMedia(oldImage.data(), oldImage.size());
  MemoryMedia patchMedia(patch.data(), patch.size());
  MemoryMedia newMedia;
  DeltaCodec codec(coder);
  DeltaCodec::Status status = codec.begin(oldMedia, patchMedia, newMedia);
  if (status == DeltaCodec::OP_OK) {
    U32 steps = 0;
    status = run(codec, steps);
  }
  finalState = codec.state();
  return status;
}

TEST(DeltaCodec, RejectsBadHeader) {
  DeltaCoderNone coder;
  const std::vector<U8> oldImage =
      toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  const std::vector<U8> good =
      toVector(TestVectors::PATCH_NONE, TestVectors::PATCH_NONE_SIZE);
  DeltaCodec::State state;

  std::vector<U8> patch = good;
  patch[0] = 'X';
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

  patch = good;
  patch[4] = 2; // version
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

  patch = good;
  patch[5] = DeltaCoder::ID_LZSS; // coder byte changed without CRC update: corruption, not mismatch
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

  patch = good;
  patch[16] ^= 0x01; // new_size, breaks header CRC
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

  patch = good;
  patch.resize(DeltaCodec::HEADER_SIZE - 1);
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::TRUNCATED);

  // Wrong coder instance for a valid patch: header intact, so a configuration mismatch is reported
  DeltaCoderLzss lzss;
  EXPECT_EQ(applyPatch(lzss, good, oldImage, state), DeltaCodec::CODER_MISMATCH);
  EXPECT_EQ(state, DeltaCodec::FAILED);
}

// Rewrite a little-endian U32 header field and recompute the header CRC so only
// the geometry check can fail
static void setHeaderField(std::vector<U8> &patch, FwSizeType offset,
                           U32 value) {
  for (FwSizeType i = 0; i < 4; i++) {
    patch[offset + i] = static_cast<U8>(value >> (8 * i));
  }
  U32 crc = 0;
  Utils::crc32_ieee802_3_update(patch.data(), DeltaCodec::HEADER_SIZE - 4, crc);
  for (FwSizeType i = 0; i < 4; i++) {
    patch[DeltaCodec::HEADER_SIZE - 4 + i] = static_cast<U8>(crc >> (8 * i));
  }
}

TEST(DeltaCodec, RejectsOversizedGeometry) {
  DeltaCoderNone coder;
  const std::vector<U8> oldImage =
      toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  const std::vector<U8> good =
      toVector(TestVectors::PATCH_NONE, TestVectors::PATCH_NONE_SIZE);
  DeltaCodec::State state;

  std::vector<U8> patch = good;
  setHeaderField(patch, 24,
                 DELTA_MAX_CHUNK_BYTES + 1); // chunk_bytes above the flight cap
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

  patch = good;
  setHeaderField(patch, 24, 0); // chunk_bytes == 0
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

  patch = good;
  setHeaderField(patch, 8, DELTA_MAX_IMAGE_SIZE + 1); // old_size
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

  patch = good;
  setHeaderField(patch, 16, DELTA_MAX_IMAGE_SIZE + 1); // new_size
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

  // coded_len above the per-chunk cap must be rejected before any offset
  // arithmetic; a value near U32 max would wrap a naive (pos + header +
  // coded_len) comparison
  patch = good;
  const U32 huge = 0xFFFFFFF0u;
  for (FwSizeType i = 0; i < 4; i++) {
    patch[DeltaCodec::HEADER_SIZE + i] = static_cast<U8>(huge >> (8 * i));
  }
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_OPCODE);
  EXPECT_EQ(state, DeltaCodec::FAILED);
}

// ----------------------------------------------------------------------
// Hand-built patches: operand bounds and multi-step verification
// ----------------------------------------------------------------------

static U32 crcOf(const U8 *data, FwSizeType length) {
  U32 crc = 0xFFFFFFFFu;
  return ~Utils::crc32_ieee802_3_update(data, length, crc);
}

static void putU32(std::vector<U8> &out, U32 value) {
  for (FwSizeType i = 0; i < 4; i++) {
    out.push_back(static_cast<U8>(value >> (8 * i)));
  }
}

static void putVarint(std::vector<U8> &out, U64 value) {
  do {
    U8 byte = static_cast<U8>(value & 0x7F);
    value >>= 7;
    out.push_back(static_cast<U8>(byte | (value ? 0x80 : 0)));
  } while (value != 0);
}

static void putSeek(std::vector<U8> &out, I64 delta) {
  out.push_back(0x03);
  putVarint(out,
            (static_cast<U64>(delta) << 1) ^ static_cast<U64>(delta >> 63));
}

static void putCopy(std::vector<U8> &out, U64 length) {
  out.push_back(0x00);
  putVarint(out, length);
}

//! Assemble a coder-less (ID_NONE) SPatch: one chunk per entry of `chunks`,
//! with chunk CRCs taken from `newImage`
static std::vector<U8>
buildNonePatch(const std::vector<U8> &oldImage, const std::vector<U8> &newImage,
               U32 chunkBytes, const std::vector<std::vector<U8>> &chunks) {
  std::vector<U8> patch = {'S', 'P', 'A', 'T', 1, DeltaCoder::ID_NONE, 0, 0};
  putU32(patch, static_cast<U32>(oldImage.size()));
  putU32(patch, crcOf(oldImage.data(), oldImage.size()));
  putU32(patch, static_cast<U32>(newImage.size()));
  putU32(patch, crcOf(newImage.data(), newImage.size()));
  putU32(patch, chunkBytes);
  putU32(patch, crcOf(patch.data(), patch.size()));
  for (FwSizeType i = 0; i < chunks.size(); i++) {
    const FwSizeType start = i * chunkBytes;
    const FwSizeType length = ((newImage.size() - start) < chunkBytes)
                                  ? (newImage.size() - start)
                                  : chunkBytes;
    putU32(patch, static_cast<U32>(chunks[i].size()));
    putU32(patch, crcOf(newImage.data() + start, length));
    patch.insert(patch.end(), chunks[i].begin(), chunks[i].end());
  }
  return patch;
}

static std::vector<U8> pseudoRandom(FwSizeType size, U32 seed) {
  std::vector<U8> out(size);
  for (FwSizeType i = 0; i < size; i++) {
    seed = seed * 1664525u + 1013904223u;
    out[i] = static_cast<U8>(seed >> 24);
  }
  return out;
}

TEST(DeltaCodec, RejectsOutOfRangeOperands) {
  // REQ: a hostile patch may never direct a read outside the old image
  DeltaCoderNone coder;
  const FwSizeType oldSize = 100;
  const std::vector<U8> oldImage = pseudoRandom(oldSize, 7);
  DeltaCodec::State state;
  std::vector<U8> ops;

  // SEEK below zero
  putSeek(ops, -1);
  putCopy(ops, oldSize);
  EXPECT_EQ(applyPatch(coder,
                       buildNonePatch(oldImage, oldImage, oldSize, {ops}),
                       oldImage, state),
            DeltaCodec::BAD_OPCODE);
  EXPECT_EQ(state, DeltaCodec::FAILED);

  // SEEK past the end
  ops.clear();
  putSeek(ops, static_cast<I64>(oldSize) + 1);
  putCopy(ops, oldSize);
  EXPECT_EQ(applyPatch(coder,
                       buildNonePatch(oldImage, oldImage, oldSize, {ops}),
                       oldImage, state),
            DeltaCodec::BAD_OPCODE);

  // SEEK legally to the end, then COPY one byte
  ops.clear();
  putSeek(ops, static_cast<I64>(oldSize));
  putCopy(ops, 1);
  EXPECT_EQ(applyPatch(coder,
                       buildNonePatch(oldImage, oldImage, oldSize, {ops}),
                       oldImage, state),
            DeltaCodec::BAD_OPCODE);

  // COPY whose span crosses the end of the old image by one byte
  ops.clear();
  putSeek(ops, 1);
  putCopy(ops, oldSize);
  EXPECT_EQ(applyPatch(coder,
                       buildNonePatch(oldImage, oldImage, oldSize, {ops}),
                       oldImage, state),
            DeltaCodec::BAD_OPCODE);

  // Over-long varint operand (6 continuation bytes)
  ops.clear();
  putCopy(ops, 50);
  ops.push_back(0x00);
  for (int i = 0; i < 5; i++) {
    ops.push_back(0x80);
  }
  ops.push_back(0x01);
  EXPECT_EQ(applyPatch(coder,
                       buildNonePatch(oldImage, oldImage, oldSize, {ops}),
                       oldImage, state),
            DeltaCodec::BAD_OPCODE);

  // Boundary success: rotate the old image by one byte using seeks to the last
  // byte and back to the start
  std::vector<U8> rotated;
  rotated.push_back(oldImage[oldSize - 1]);
  rotated.insert(rotated.end(), oldImage.begin(), oldImage.end() - 1);
  ops.clear();
  putSeek(ops, static_cast<I64>(oldSize) - 1);
  putCopy(ops, 1);
  putSeek(ops, -static_cast<I64>(oldSize));
  putCopy(ops, oldSize - 1);
  MemoryMedia oldMedia(oldImage.data(), oldImage.size());
  const std::vector<U8> patch =
      buildNonePatch(oldImage, rotated, oldSize, {ops});
  MemoryMedia patchMedia(patch.data(), patch.size());
  MemoryMedia newMedia;
  DeltaCodec codec(coder);
  ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
  U32 steps = 0;
  EXPECT_EQ(run(codec, steps), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::COMPLETE);
  EXPECT_EQ(newMedia.data, rotated);
}

TEST(DeltaCodec, RejectsConsecutiveSeeks) {
  // REQ: per-step work is bounded by configuration, not patch content. A SEEK
  // produces no output, so a chunk of back-to-back SEEKs (cheap to encode,
  // expensive to decode once a coder expands it) must be rejected at the
  // second SEEK rather than run to exhaustion of the coded payload.
  const FwSizeType oldSize = 100;
  const std::vector<U8> oldImage = pseudoRandom(oldSize, 11);
  DeltaCodec::State state;

  // Two literal SEEKs with the None coder: a valid encoder folds these into one
  DeltaCoderNone none;
  std::vector<U8> ops;
  putSeek(ops, 1);
  putSeek(ops, -1);
  putCopy(ops, oldSize);
  EXPECT_EQ(applyPatch(none, buildNonePatch(oldImage, oldImage, oldSize, {ops}),
                       oldImage, state),
            DeltaCodec::BAD_OPCODE);
  EXPECT_EQ(state, DeltaCodec::FAILED);

  // LZSS flood: literal "SEEK 0" then maximal overlapping matches expand a
  // 16 KiB coded chunk toward ~2 MiB of SEEKs; the patch media counts reads to
  // show the rejection happens after the first fill, not after the whole chunk
  DeltaCoderLzss lzss;
  std::vector<U8> coded;
  coded.push_back(0x40);  // flags: literal, literal, then six matches
  coded.push_back(0x03);  // SEEK
  coded.push_back(0x00);  // delta 0
  for (int i = 0; i < 6; i++) {
    coded.push_back(1);    // distance 2
    coded.push_back(255);  // length 258
  }
  while (coded.size() + 17 <= DELTA_MAX_CODED_CHUNK_BYTES) {
    coded.push_back(0x00);  // flags: eight matches
    for (int i = 0; i < 8; i++) {
      coded.push_back(1);
      coded.push_back(255);
    }
  }
  std::vector<U8> patch = buildNonePatch(oldImage, oldImage, oldSize, {coded});
  patch[5] = DeltaCoder::ID_LZSS;
  const U32 headerCrc = crcOf(patch.data(), DeltaCodec::HDR_CRC);
  for (FwSizeType i = 0; i < 4; i++) {
    patch[DeltaCodec::HDR_CRC + i] = static_cast<U8>(headerCrc >> (8 * i));
  }
  MemoryMedia oldMedia(oldImage.data(), oldImage.size());
  MemoryMedia patchMedia(patch.data(), patch.size());
  MemoryMedia newMedia;
  DeltaCodec codec(lzss);
  ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
  const U32 readsBefore = patchMedia.reads;
  U32 steps = 0;
  EXPECT_EQ(run(codec, steps), DeltaCodec::BAD_OPCODE);
  EXPECT_EQ(codec.state(), DeltaCodec::FAILED);
  // Rejection at the second SEEK needs a single patch-buffer fill of the chunk
  EXPECT_LE(patchMedia.reads - readsBefore, 2u);
}

TEST(DeltaCodec, VerifySpansSteps) {
  // REQ: verification work is bounded per step; partial-chunk CRC state carries
  // across steps and a corrupt chunk that straddles a step boundary is
  // discarded without corrupting the committed CRC
  static_assert(DELTA_VERIFY_BYTES_PER_STEP == 4096,
                "Test geometry assumes the default verify budget");
  DeltaCoderNone coder;
  const FwSizeType imageSize = 10000;
  const U32 chunkBytes = 3000; // 4 chunks: 3000, 3000, 3000, 1000
  const std::vector<U8> image = pseudoRandom(imageSize, 42);
  std::vector<std::vector<U8>> chunks;
  for (FwSizeType start = 0; start < imageSize; start += chunkBytes) {
    std::vector<U8> ops;
    putCopy(ops, ((imageSize - start) < chunkBytes) ? (imageSize - start)
                                                    : chunkBytes);
    chunks.push_back(ops);
  }
  const std::vector<U8> patch =
      buildNonePatch(image, image, chunkBytes, chunks);

  // Output holds 3 chunks; the second is corrupt at a byte verified only on the
  // second step
  MemoryMedia oldMedia(image.data(), image.size());
  MemoryMedia patchMedia(patch.data(), patch.size());
  MemoryMedia newMedia(image.data(), 3 * chunkBytes);
  newMedia.data[chunkBytes + 2000] ^= 0xFF;
  DeltaCodec codec(coder);
  ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::VERIFY_NEW);

  // Step 1: chunk 0 (3000 B) verified, 1096 B of chunk 1 read
  EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::VERIFY_NEW);
  EXPECT_EQ(codec.resumedChunks(), 1u);
  // Step 2: chunk 1 completes and fails its CRC -> verification stops, old
  // image check begins
  EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::VERIFY_OLD);
  EXPECT_EQ(codec.resumedChunks(), 1u);
  EXPECT_EQ(codec.chunkIndex(), 1u);
  // Old image: 10000 B takes ceil(10000/4096) = 3 steps
  EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::VERIFY_OLD);
  EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::VERIFY_OLD);
  EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::PATCHING);

  U32 steps = 0;
  ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::COMPLETE);
  // chunks 1..3 re-applied, finish (flush), then ceil(10000/4096) = 3 read-back
  // steps
  EXPECT_EQ(steps, 3u + 1u + 3u);
  EXPECT_EQ(codec.resumedChunks(), 1u);
  EXPECT_EQ(codec.bytesWritten(),
            imageSize); // total verified output, including the resumed chunk
  EXPECT_EQ(newMedia.data, image);
}

TEST(DeltaCodec, RejectsOldImageMismatch) {
  DeltaCoderNone coder;
  const std::vector<U8> patch =
      toVector(TestVectors::PATCH_NONE, TestVectors::PATCH_NONE_SIZE);
  DeltaCodec::State state;

  std::vector<U8> oldImage =
      toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  oldImage[1234] ^= 0x10;
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state),
            DeltaCodec::OLD_IMAGE_MISMATCH);

  oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE - 1);
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state),
            DeltaCodec::OLD_IMAGE_MISMATCH);
}

TEST(DeltaCodec, RejectsTruncatedAndOversizedPatch) {
  DeltaCoderNone coder;
  const std::vector<U8> oldImage =
      toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  const std::vector<U8> good =
      toVector(TestVectors::PATCH_NONE, TestVectors::PATCH_NONE_SIZE);
  DeltaCodec::State state;

  std::vector<U8> patch = good;
  patch.resize(good.size() - 5);
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::TRUNCATED);

  patch = good;
  patch.resize(DeltaCodec::HEADER_SIZE + 3); // partial first chunk header
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::TRUNCATED);

  patch = good;
  patch.push_back(0);
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state),
            DeltaCodec::PATCH_SIZE_MISMATCH);
}

TEST(DeltaCodec, RejectsCorruptChunk) {
  DeltaCoderNone coder;
  const std::vector<U8> oldImage =
      toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  const std::vector<U8> good =
      toVector(TestVectors::PATCH_NONE, TestVectors::PATCH_NONE_SIZE);
  DeltaCodec::State state;

  // Corrupt chunk 0's declared output CRC
  std::vector<U8> patch = good;
  patch[DeltaCodec::HEADER_SIZE + 4] ^= 0x01;
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::CHUNK_CRC);

  // Corrupt an op byte (first payload byte) into an unknown opcode
  patch = good;
  patch[DeltaCodec::HEADER_SIZE + DeltaCodec::CHUNK_HEADER_SIZE] = 0x7F;
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_OPCODE);

  // Payload with an op whose length exceeds the chunk output
  patch = good;
  patch[DeltaCodec::HEADER_SIZE + DeltaCodec::CHUNK_HEADER_SIZE] = 0x00; // COPY
  patch[DeltaCodec::HEADER_SIZE + DeltaCodec::CHUNK_HEADER_SIZE + 1] =
      0xFF; // uvar 0x7FFF...
  patch[DeltaCodec::HEADER_SIZE + DeltaCodec::CHUNK_HEADER_SIZE + 2] = 0x7F;
  EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_OPCODE);
}

TEST(DeltaCodec, MediaErrors) {
  DeltaCoderLzss coder;
  {
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(TestVectors::PATCH_LZSS,
                           TestVectors::PATCH_LZSS_SIZE);
    MemoryMedia newMedia;
    DeltaCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    oldMedia.failReads = true;
    EXPECT_EQ(codec.step(), DeltaCodec::READ_ERROR);
    EXPECT_EQ(codec.state(), DeltaCodec::FAILED);
    EXPECT_EQ(codec.lastStatus(), DeltaCodec::READ_ERROR);
    EXPECT_EQ(codec.step(), DeltaCodec::READ_ERROR); // stays failed
  }
  {
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(TestVectors::PATCH_LZSS,
                           TestVectors::PATCH_LZSS_SIZE);
    MemoryMedia newMedia;
    newMedia.failWrites = true;
    DeltaCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    U32 steps = 0;
    EXPECT_EQ(run(codec, steps), DeltaCodec::WRITE_ERROR);
    EXPECT_EQ(codec.chunkIndex(), 0u);
  }
  {
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(TestVectors::PATCH_LZSS,
                           TestVectors::PATCH_LZSS_SIZE);
    MemoryMedia newMedia;
    DeltaCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    ASSERT_EQ(codec.step(), DeltaCodec::OP_OK); // old verify
    patchMedia.failReads = true;
    EXPECT_EQ(codec.step(), DeltaCodec::READ_ERROR);
  }
}

TEST(DeltaCodec, ReadBackCatchesStorageFaults) {
  // REQ: DP-005/DP-008. COMPLETE is reported only after the stored new image
  // has been flushed, read back and CRC-checked; a media that acknowledges a
  // write but stores it wrong, or that cannot flush, must fail the patch.
  DeltaCoderNone coder;
  {
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(TestVectors::PATCH_NONE,
                           TestVectors::PATCH_NONE_SIZE);
    MemoryMedia newMedia;
    newMedia.corruptStored = true;
    DeltaCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    U32 steps = 0;
    EXPECT_EQ(run(codec, steps), DeltaCodec::NEW_IMAGE_MISMATCH);
    EXPECT_EQ(codec.state(), DeltaCodec::FAILED);
    EXPECT_EQ(newMedia.flushes, 1u);
  }
  {
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(TestVectors::PATCH_NONE,
                           TestVectors::PATCH_NONE_SIZE);
    MemoryMedia newMedia;
    newMedia.failFlush = true;
    DeltaCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    U32 steps = 0;
    EXPECT_EQ(run(codec, steps), DeltaCodec::WRITE_ERROR);
    EXPECT_EQ(codec.state(), DeltaCodec::FAILED);
  }
  {
    // Read failure during the read-back pass is a READ_ERROR
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(TestVectors::PATCH_NONE,
                           TestVectors::PATCH_NONE_SIZE);
    MemoryMedia newMedia;
    DeltaCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    while (codec.state() != DeltaCodec::VERIFY_FINAL) {
      ASSERT_EQ(codec.step(), DeltaCodec::OP_OK);
    }
    newMedia.failReads = true;
    EXPECT_EQ(codec.step(), DeltaCodec::READ_ERROR);
    EXPECT_EQ(codec.state(), DeltaCodec::FAILED);
  }
}

TEST(DeltaCodec, ResetReturnsToIdle) {
  DeltaCoderLzss coder;
  MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  MemoryMedia patchMedia(TestVectors::PATCH_LZSS, TestVectors::PATCH_LZSS_SIZE);
  MemoryMedia newMedia;
  DeltaCodec codec(coder);
  ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
  ASSERT_EQ(codec.step(), DeltaCodec::OP_OK);
  ASSERT_EQ(codec.step(), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.chunkIndex(), 1u);
  codec.reset();
  EXPECT_EQ(codec.state(), DeltaCodec::IDLE);
  EXPECT_EQ(codec.chunkIndex(), 0u);
  EXPECT_EQ(codec.step(), DeltaCodec::OP_OK); // no-op when idle
}

// ----------------------------------------------------------------------
// DeltaFileMedia: end-to-end through real files
// ----------------------------------------------------------------------

static void writeFile(const char *path, const U8 *data, FwSizeType size) {
  FILE *file = fopen(path, "wb");
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(fwrite(data, 1, size, file), size);
  fclose(file);
}

static std::vector<U8> readFile(const char *path) {
  std::vector<U8> out;
  FILE *file = fopen(path, "rb");
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
  const char *oldPath = "delta_ut_old.bin";
  const char *patchPath = "delta_ut_patch.spatch";
  const char *newPath = "delta_ut_new.bin";
  Os::FileSystem::removeFile(newPath);
  writeFile(oldPath, TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
  writeFile(patchPath, TestVectors::PATCH_LZSS, TestVectors::PATCH_LZSS_SIZE);

  DeltaFileMedia oldMedia;
  DeltaFileMedia patchMedia;
  DeltaFileMedia newMedia;
  DeltaFileMedia missing;
  EXPECT_NE(
      missing.open("delta_ut_does_not_exist.bin", DeltaFileMedia::READ_ONLY),
      Os::File::OP_OK);
  EXPECT_FALSE(missing.isOpen());
  ASSERT_EQ(oldMedia.open(oldPath, DeltaFileMedia::READ_ONLY), Os::File::OP_OK);
  ASSERT_EQ(patchMedia.open(patchPath, DeltaFileMedia::READ_ONLY),
            Os::File::OP_OK);
  ASSERT_EQ(newMedia.open(newPath, DeltaFileMedia::READ_WRITE),
            Os::File::OP_OK);
  FwSizeType size = 0;
  ASSERT_EQ(newMedia.size(size), DeltaMedia::OP_OK);
  EXPECT_EQ(size, 0u);
  U8 byte = 0;
  EXPECT_EQ(oldMedia.write(0, &byte, 1), DeltaMedia::NOT_OPEN);
  EXPECT_EQ(oldMedia.read(TestVectors::OLD_IMAGE_SIZE, &byte, 1),
            DeltaMedia::OUT_OF_RANGE);

  DeltaCoderLzss coder;
  DeltaCodec codec(coder);
  ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
  U32 steps = 0;
  ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
  EXPECT_EQ(codec.state(), DeltaCodec::COMPLETE);
  newMedia.close();
  EXPECT_EQ(readFile(newPath),
            toVector(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE));

  // Re-open preserves the finished output and the engine resumes everything
  ASSERT_EQ(newMedia.open(newPath, DeltaFileMedia::READ_WRITE),
            Os::File::OP_OK);
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
