// ======================================================================
// \title  StaticRules.cpp
// \author starchmd
// \brief  Static (deterministic) rules for the Delta coder and codec: the
// former direct unit tests, each re-expressed as a rule that runs against
// whichever shipped coder the tester holds. Golden vectors come from
// DeltaTestVectors.hpp (one patch per coder over the same old/new images).
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include <cstdio>
#include <vector>

#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderLzss.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderNone.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoderRle.hpp"
#include "FprimeExtras/Update/Delta/test/ut/DeltaRulesTester.hpp"
#include "FprimeExtras/Update/Delta/test/ut/DeltaTestVectors.hpp"
#include "Utils/Hash/Crc32/Crc32.hpp"

namespace Update {

// ----------------------------------------------------------------------
// Golden vectors for the coder under test
// ----------------------------------------------------------------------

const U8* DeltaRulesTester::goldenPatch() const {
    switch (this->m_coder.id()) {
        case DeltaCoder::ID_NONE:
            return TestVectors::PATCH_NONE;
        case DeltaCoder::ID_RLE:
            return TestVectors::PATCH_RLE;
        default:
            return TestVectors::PATCH_LZSS;
    }
}

FwSizeType DeltaRulesTester::goldenPatchSize() const {
    switch (this->m_coder.id()) {
        case DeltaCoder::ID_NONE:
            return TestVectors::PATCH_NONE_SIZE;
        case DeltaCoder::ID_RLE:
            return TestVectors::PATCH_RLE_SIZE;
        default:
            return TestVectors::PATCH_LZSS_SIZE;
    }
}

const U8* DeltaRulesTester::goldenPatchOtherWidth() const {
    const bool w8 = (sizeof(FwSizeType) == 8);
    switch (this->m_coder.id()) {
        case DeltaCoder::ID_NONE:
            return w8 ? TestVectors::PATCH_NONE_W4 : TestVectors::PATCH_NONE_W8;
        case DeltaCoder::ID_RLE:
            return w8 ? TestVectors::PATCH_RLE_W4 : TestVectors::PATCH_RLE_W8;
        default:
            return w8 ? TestVectors::PATCH_LZSS_W4 : TestVectors::PATCH_LZSS_W8;
    }
}

FwSizeType DeltaRulesTester::goldenPatchOtherWidthSize() const {
    const bool w8 = (sizeof(FwSizeType) == 8);
    switch (this->m_coder.id()) {
        case DeltaCoder::ID_NONE:
            return w8 ? TestVectors::PATCH_NONE_W4_SIZE : TestVectors::PATCH_NONE_W8_SIZE;
        case DeltaCoder::ID_RLE:
            return w8 ? TestVectors::PATCH_RLE_W4_SIZE : TestVectors::PATCH_RLE_W8_SIZE;
        default:
            return w8 ? TestVectors::PATCH_LZSS_W4_SIZE : TestVectors::PATCH_LZSS_W8_SIZE;
    }
}

const U8* DeltaRulesTester::goldenCoded() const {
    switch (this->m_coder.id()) {
        case DeltaCoder::ID_NONE:
            return TestVectors::CODER_NONE;
        case DeltaCoder::ID_RLE:
            return TestVectors::CODER_RLE;
        default:
            return TestVectors::CODER_LZSS;
    }
}

FwSizeType DeltaRulesTester::goldenCodedSize() const {
    switch (this->m_coder.id()) {
        case DeltaCoder::ID_NONE:
            return TestVectors::CODER_NONE_SIZE;
        case DeltaCoder::ID_RLE:
            return TestVectors::CODER_RLE_SIZE;
        default:
            return TestVectors::CODER_LZSS_SIZE;
    }
}

// ----------------------------------------------------------------------
// Helpers shared by the static rules
// ----------------------------------------------------------------------

static void checkCoder(DeltaCoder& coder, const U8* coded, FwSizeType codedSize) {
    const std::vector<U8> expected = toVector(TestVectors::CODER_RAW, TestVectors::CODER_RAW_SIZE);
    const FwSizeType pieces[] = {1, 2, 7, 64, 10000};
    const FwSizeType drains[] = {1, 3, 100, 10000};
    for (FwSizeType piece : pieces) {
        for (FwSizeType drain : drains) {
            std::vector<U8> out;
            ASSERT_EQ(decodeAll(coder, coded, codedSize, piece, drain, out), DeltaCoder::OP_OK)
                << "piece " << piece << " drain " << drain;
            ASSERT_EQ(out, expected) << "piece " << piece << " drain " << drain;
        }
    }
}

static void applyNominal(DeltaCoder& coder, const U8* patch, FwSizeType patchSize) {
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(patch, patchSize);
    MemoryMedia newMedia;
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::VERIFY_OLD);
    EXPECT_EQ(codec.oldSize(), TestVectors::OLD_IMAGE_SIZE);
    EXPECT_EQ(codec.newSize(), TestVectors::NEW_IMAGE_SIZE);
    EXPECT_EQ(codec.oldCrc(), TestVectors::OLD_CRC);
    EXPECT_EQ(codec.newCrc(), TestVectors::NEW_CRC);
    const U32 expectedChunks = (TestVectors::NEW_IMAGE_SIZE + TestVectors::CHUNK_BYTES - 1) / TestVectors::CHUNK_BYTES;
    EXPECT_EQ(codec.chunkCount(), expectedChunks);
    U32 steps = 0;
    ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
    EXPECT_EQ(codec.chunkIndex(), expectedChunks);
    EXPECT_EQ(codec.resumedChunks(), 0u);
    EXPECT_EQ(codec.bytesWritten(), TestVectors::NEW_IMAGE_SIZE);
    // One step verifies the old image (2000 B < 4096 B budget), one per chunk,
    // one to finish (flush), one to read the new image back (< 4096 B)
    EXPECT_EQ(steps, 1u + expectedChunks + 1u + 1u);
    // One flush per committed chunk boundary plus one before read-back
    EXPECT_EQ(newMedia.flushes, static_cast<U32>(expectedChunks) + 1u);
    EXPECT_EQ(newMedia.data, toVector(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE));
}

static DeltaCodec::Status applyPatch(DeltaCoder& coder,
                                     const std::vector<U8>& patch,
                                     const std::vector<U8>& oldImage,
                                     TestCodec::State& finalState) {
    MemoryMedia oldMedia(oldImage.data(), oldImage.size());
    MemoryMedia patchMedia(patch.data(), patch.size());
    MemoryMedia newMedia;
    TestCodec codec(coder);
    DeltaCodec::Status status = codec.begin(oldMedia, patchMedia, newMedia);
    if (status == DeltaCodec::OP_OK) {
        U32 steps = 0;
        status = run(codec, steps);
    }
    finalState = codec.state();
    return status;
}

static void setHeaderSize(std::vector<U8>& patch, FwSizeType offset, FwSizeType value) {
    putBe(&patch[offset], value, DeltaCodec::SIZE_FIELD_WIDTH);
    putBe(&patch[DeltaCodec::HDR_CRC], crc32(patch.data(), DeltaCodec::HDR_CRC), sizeof(U32));
}

static std::vector<U8> buildSingleChunkPatch(U8 coderId, FwSizeType newSize, U8 fill, const std::vector<U8>& coded) {
    const std::vector<U8> newImage(newSize, fill);
    const U32 newCrc = crc32(newImage.data(), newImage.size());
    std::vector<U8> patch(DeltaCodec::HEADER_SIZE, 0);
    patch[0] = 'S';
    patch[1] = 'P';
    patch[2] = 'A';
    patch[3] = 'T';
    patch[DeltaCodec::HDR_VERSION] = DeltaCodec::VERSION;
    patch[DeltaCodec::HDR_CODER] = coderId;
    patch[DeltaCodec::HDR_FLAGS] = 0;
    patch[DeltaCodec::HDR_SIZE_WIDTH] = DeltaCodec::SIZE_FIELD_WIDTH;
    putBe(&patch[DeltaCodec::HDR_OLD_SIZE], 0, DeltaCodec::SIZE_FIELD_WIDTH);
    putBe(&patch[DeltaCodec::HDR_OLD_CRC], 0, sizeof(U32));  // CRC32 of no bytes
    putBe(&patch[DeltaCodec::HDR_NEW_SIZE], newSize, DeltaCodec::SIZE_FIELD_WIDTH);
    putBe(&patch[DeltaCodec::HDR_NEW_CRC], newCrc, sizeof(U32));
    putBe(&patch[DeltaCodec::HDR_CHUNK_BYTES], DELTA_WINDOW_SIZE, DeltaCodec::SIZE_FIELD_WIDTH);
    putBe(&patch[DeltaCodec::HDR_CRC], crc32(patch.data(), DeltaCodec::HDR_CRC), sizeof(U32));
    std::vector<U8> chunkHeader(DeltaCodec::CHUNK_HEADER_SIZE, 0);
    putBe(&chunkHeader[DeltaCodec::CHUNK_HDR_CODED_LENGTH], coded.size(), DeltaCodec::SIZE_FIELD_WIDTH);
    putBe(&chunkHeader[DeltaCodec::CHUNK_HDR_CRC], newCrc, sizeof(U32));
    patch.insert(patch.end(), chunkHeader.begin(), chunkHeader.end());
    patch.insert(patch.end(), coded.begin(), coded.end());
    return patch;
}

static U32 crcOf(const U8* data, FwSizeType length) {
    U32 crc = 0xFFFFFFFFu;
    return ~Utils::crc32_ieee802_3_update(data, length, crc);
}

static void putU32(std::vector<U8>& out, U32 value) {
    for (FwSizeType i = 0; i < sizeof(U32); i++) {
        out.push_back(static_cast<U8>(value >> (8 * (sizeof(U32) - 1 - i))));
    }
}

static void putSize(std::vector<U8>& out, FwSizeType value) {
    for (FwSizeType i = 0; i < DeltaCodec::SIZE_FIELD_WIDTH; i++) {
        out.push_back(static_cast<U8>(value >> (8 * (DeltaCodec::SIZE_FIELD_WIDTH - 1 - i))));
    }
}

static void putVarint(std::vector<U8>& out, U64 value) {
    do {
        U8 byte = static_cast<U8>(value & 0x7F);
        value >>= 7;
        out.push_back(static_cast<U8>(byte | (value ? 0x80 : 0)));
    } while (value != 0);
}

static void putSeek(std::vector<U8>& out, I64 delta) {
    out.push_back(0x03);
    putVarint(out, (static_cast<U64>(delta) << 1) ^ static_cast<U64>(delta >> 63));
}

static void putCopy(std::vector<U8>& out, U64 length) {
    out.push_back(0x00);
    putVarint(out, length);
}

//! Build a patch from raw operation streams (one per chunk), each coded with `coderId`
static std::vector<U8> buildOpPatch(U8 coderId,
                                    const std::vector<U8>& oldImage,
                                    const std::vector<U8>& newImage,
                                    FwSizeType chunkBytes,
                                    const std::vector<std::vector<U8>>& opChunks) {
    std::vector<std::vector<U8>> chunks;
    for (const std::vector<U8>& ops : opChunks) {
        chunks.push_back(TestEncoders::encode(coderId, ops));
    }
    std::vector<U8> patch = {
        'S', 'P', 'A', 'T', DeltaCodec::VERSION, coderId, 0, static_cast<U8>(DeltaCodec::SIZE_FIELD_WIDTH)};
    putSize(patch, oldImage.size());
    putU32(patch, crcOf(oldImage.data(), oldImage.size()));
    putSize(patch, newImage.size());
    putU32(patch, crcOf(newImage.data(), newImage.size()));
    putSize(patch, chunkBytes);
    putU32(patch, crcOf(patch.data(), patch.size()));
    for (FwSizeType i = 0; i < chunks.size(); i++) {
        const FwSizeType start = i * chunkBytes;
        const FwSizeType length = ((newImage.size() - start) < chunkBytes) ? (newImage.size() - start) : chunkBytes;
        putSize(patch, chunks[i].size());
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

// ----------------------------------------------------------------------
// Static rules
// ----------------------------------------------------------------------

bool DeltaRulesTester::Static__CoderGolden__precondition() const {
    return true;
}

void DeltaRulesTester::Static__CoderGolden__action() {
    DeltaCoder& coder = this->m_coder;
    checkCoder(coder, this->goldenCoded(), this->goldenCodedSize());
}

bool DeltaRulesTester::Static__LzssHandCrafted__precondition() const {
    return this->m_coder.id() == DeltaCoder::ID_LZSS;
}

void DeltaRulesTester::Static__LzssHandCrafted__action() {
    // flags 0b01000000: literal 'a', then match distance 1 length 3 -> "aaaa"
    const U8 coded[] = {0x40, 'a', 0x00, 0x00};
    DeltaCoder& coder = this->m_coder;
    std::vector<U8> out;
    ASSERT_EQ(decodeAll(coder, coded, sizeof coded, 1, 1, out), DeltaCoder::OP_OK);
    ASSERT_EQ(out, std::vector<U8>({'a', 'a', 'a', 'a'}));
    // Match before any history is malformed
    const U8 bad[] = {0x80, 0x00, 0x00};
    out.clear();
    ASSERT_EQ(decodeAll(coder, bad, sizeof bad, 100, 100, out), DeltaCoder::MALFORMED);
    // Match distance exceeding available history is malformed
    const U8 far[] = {0x40, 'a', 0x05, 0x00};
    out.clear();
    ASSERT_EQ(decodeAll(coder, far, sizeof far, 100, 100, out), DeltaCoder::MALFORMED);
    // A trailing flag byte that introduces no item is surplus: the coder stays
    // pending so the chunk is rejected (the ground decoder applies the same rule)
    const U8 emptyGroup[] = {0x40, 'a', 0x00};
    out.clear();
    ASSERT_EQ(decodeAll(coder, emptyGroup, sizeof emptyGroup, 100, 100, out), DeltaCoder::OP_OK);
    EXPECT_TRUE(coder.pending());
    out.clear();
    ASSERT_EQ(decodeAll(coder, coded, sizeof coded, 100, 100, out), DeltaCoder::OP_OK);
    EXPECT_FALSE(coder.pending());
}

bool DeltaRulesTester::Static__RleHandCrafted__precondition() const {
    return this->m_coder.id() == DeltaCoder::ID_RLE;
}

void DeltaRulesTester::Static__RleHandCrafted__action() {
    const U8 coded[] = {0x01, 'x', 'y', 0x81, 'z'};  // 2 literals, then 'z' x3
    DeltaCoder& coder = this->m_coder;
    std::vector<U8> out;
    ASSERT_EQ(decodeAll(coder, coded, sizeof coded, 1, 1, out), DeltaCoder::OP_OK);
    ASSERT_EQ(out, std::vector<U8>({'x', 'y', 'z', 'z', 'z'}));
}

bool DeltaRulesTester::Static__ApplyGolden__precondition() const {
    return true;
}

void DeltaRulesTester::Static__ApplyGolden__action() {
    DeltaCoder& coder = this->m_coder;
    applyNominal(coder, this->goldenPatch(), this->goldenPatchSize());
}

bool DeltaRulesTester::Static__ResumeSkipsVerifiedChunks__precondition() const {
    return true;
}

void DeltaRulesTester::Static__ResumeSkipsVerifiedChunks__action() {
    DeltaCoder& coder = this->m_coder;
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
    // Output already holds 3 good chunks, a corrupt 4th chunk, and a partial 5th
    const FwSizeType present = 4 * TestVectors::CHUNK_BYTES + 17;
    MemoryMedia newMedia(TestVectors::NEW_IMAGE, present);
    newMedia.data[3 * TestVectors::CHUNK_BYTES + 5] ^= 0xFF;
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::VERIFY_RESUME);
    U32 steps = 0;
    ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
    EXPECT_EQ(codec.resumedChunks(), 3u);
    EXPECT_EQ(newMedia.data, toVector(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE));
}

bool DeltaRulesTester::Static__ResumeCompleteOutput__precondition() const {
    return true;
}

void DeltaRulesTester::Static__ResumeCompleteOutput__action() {
    DeltaCoder& coder = this->m_coder;
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
    MemoryMedia newMedia(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE);
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    U32 steps = 0;
    ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
    EXPECT_EQ(codec.resumedChunks(), codec.chunkCount());
    EXPECT_EQ(newMedia.writes, 0u);
}

bool DeltaRulesTester::Static__OutputStale__precondition() const {
    return true;
}

void DeltaRulesTester::Static__OutputStale__action() {
    DeltaCoder& coder = this->m_coder;
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
    std::vector<U8> big(TestVectors::NEW_IMAGE_SIZE + 1, 0);
    MemoryMedia newMedia(big.data(), big.size());
    TestCodec codec(coder);
    EXPECT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OUTPUT_STALE);
    EXPECT_EQ(codec.state(), TestCodec::FAILED);
}

bool DeltaRulesTester::Static__RejectsBadHeader__precondition() const {
    return true;
}

void DeltaRulesTester::Static__RejectsBadHeader__action() {
    DeltaCoder& coder = this->m_coder;
    const std::vector<U8> oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    const std::vector<U8> good = toVector(this->goldenPatch(), this->goldenPatchSize());
    TestCodec::State state;

    std::vector<U8> patch = good;
    patch[0] = 'X';
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

    patch = good;
    patch[DeltaCodec::HDR_VERSION] = DeltaCodec::VERSION + 1;
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

    patch = good;
    // Coder byte changed to another shipped coder without a CRC update: corruption, not mismatch
    patch[DeltaCodec::HDR_CODER] = static_cast<U8>((coder.id() + 1) % 3);
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

    patch = good;
    patch[DeltaCodec::HDR_NEW_SIZE] ^= 0x01;  // new_size, breaks header CRC
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

    patch = good;
    patch.resize(DeltaCodec::HEADER_SIZE - 1);
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::TRUNCATED);

    // Wrong coder instance for a valid patch: header intact, so a configuration mismatch is reported
    ForeignCoder foreign(0x7F);
    EXPECT_EQ(applyPatch(foreign, good, oldImage, state), DeltaCodec::CODER_MISMATCH);
    EXPECT_EQ(state, TestCodec::FAILED);
}

bool DeltaRulesTester::Static__RejectsPendingCoderState__precondition() const {
    return true;
}

void DeltaRulesTester::Static__RejectsPendingCoderState__action() {
    // The decoded op stream is byte-exact in every case (so the chunk CRC passes),
    // but the coder is left mid-token or holding undelivered output
    struct Case {
        const char* name;
        U8 coderId;
        FwSizeType newSize;
        std::vector<U8> coded;
    };
    // A 256-byte op stream (LIT, uvar 253, 253 literals) fills the window exactly
    const std::vector<Case> cases = {
        {"RLE surplus repeat byte at full window",
         DeltaCoder::ID_RLE,
         253,
         {0x02, 0x02, 0xFD, 0x01, 0xFF, 0x6C, 0xFB, 0x6C}},
        {"LZSS surplus match byte at full window",
         DeltaCoder::ID_LZSS,
         253,
         {0x08, 0x02, 0xFD, 0x01, 0x6C, 0x00, 0xFA}},
        {"RLE incomplete literal run at full window",
         DeltaCoder::ID_RLE,
         253,
         {0x02, 0x02, 0xFD, 0x01, 0xFF, 0x6C, 0xFA, 0x6C, 0x05}},
        {"RLE incomplete repeat at full window",
         DeltaCoder::ID_RLE,
         253,
         {0x02, 0x02, 0xFD, 0x01, 0xFF, 0x6C, 0xFA, 0x6C, 0x83}},
        {"LZSS incomplete match at full window",
         DeltaCoder::ID_LZSS,
         253,
         {0x0C, 0x02, 0xFD, 0x01, 0x6C, 0x00, 0xF9, 0x00}},
        {"RLE incomplete literal run, short chunk", DeltaCoder::ID_RLE, 22, {0x01, 0x02, 0x16, 0x94, 0x6C, 0x05}},
        {"RLE incomplete repeat, short chunk", DeltaCoder::ID_RLE, 22, {0x01, 0x02, 0x16, 0x94, 0x6C, 0x83}},
        {"LZSS incomplete match, short chunk", DeltaCoder::ID_LZSS, 22, {0x18, 0x02, 0x16, 0x6C, 0x00, 0x12, 0x00}},
    };
    DeltaCoderRle rle;
    DeltaCoderLzss lzss;
    const std::vector<U8> oldImage;
    for (const Case& c : cases) {
        DeltaCoder& coder =
            (c.coderId == DeltaCoder::ID_RLE) ? static_cast<DeltaCoder&>(rle) : static_cast<DeltaCoder&>(lzss);
        const std::vector<U8> patch = buildSingleChunkPatch(c.coderId, c.newSize, 0x6C, c.coded);
        TestCodec::State state;
        EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_OPCODE) << c.name;
        EXPECT_EQ(state, TestCodec::FAILED) << c.name;
    }
    // Controls: the same streams closed at a token boundary are accepted
    const std::vector<Case> valid = {
        {"RLE complete", DeltaCoder::ID_RLE, 253, {0x02, 0x02, 0xFD, 0x01, 0xFF, 0x6C, 0xFA, 0x6C}},
        {"LZSS complete with unused flag bits", DeltaCoder::ID_LZSS, 253, {0x08, 0x02, 0xFD, 0x01, 0x6C, 0x00, 0xF9}},
        {"RLE complete, short chunk", DeltaCoder::ID_RLE, 22, {0x01, 0x02, 0x16, 0x94, 0x6C}},
    };
    for (const Case& c : valid) {
        DeltaCoder& coder =
            (c.coderId == DeltaCoder::ID_RLE) ? static_cast<DeltaCoder&>(rle) : static_cast<DeltaCoder&>(lzss);
        const std::vector<U8> patch = buildSingleChunkPatch(c.coderId, c.newSize, 0x6C, c.coded);
        TestCodec::State state;
        EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::OP_OK) << c.name;
        EXPECT_EQ(state, TestCodec::COMPLETE) << c.name;
    }
}

bool DeltaRulesTester::Static__RejectsSizeWidthMismatch__precondition() const {
    return true;
}

void DeltaRulesTester::Static__RejectsSizeWidthMismatch__action() {
    // A patch built for the other FwSizeType width is reported distinctly, before
    // any size field is interpreted
    DeltaCoder& coder = this->m_coder;
    const std::vector<U8> oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    const std::vector<U8> other = (sizeof(FwSizeType) == 8)
                                      ? toVector(this->goldenPatchOtherWidth(), this->goldenPatchOtherWidthSize())
                                      : toVector(this->goldenPatchOtherWidth(), this->goldenPatchOtherWidthSize());
    TestCodec::State state;
    EXPECT_EQ(applyPatch(coder, other, oldImage, state), DeltaCodec::SIZE_WIDTH_MISMATCH);
    EXPECT_EQ(state, TestCodec::FAILED);

    // A short patch from the other width is still reported as a width mismatch,
    // since the fixed prefix is checked before the width-dependent header length
    std::vector<U8> shortPatch = other;
    shortPatch.resize(DeltaCodec::HDR_OLD_SIZE);
    EXPECT_EQ(applyPatch(coder, shortPatch, oldImage, state), DeltaCodec::SIZE_WIDTH_MISMATCH);

    // Shorter than the fixed prefix: nothing to classify, so truncated
    shortPatch.resize(DeltaCodec::HDR_OLD_SIZE - 1);
    EXPECT_EQ(applyPatch(coder, shortPatch, oldImage, state), DeltaCodec::TRUNCATED);
}

bool DeltaRulesTester::Static__RejectsOversizedGeometry__precondition() const {
    return true;
}

void DeltaRulesTester::Static__RejectsOversizedGeometry__action() {
    DeltaCoder& coder = this->m_coder;
    const std::vector<U8> oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    const std::vector<U8> good = toVector(this->goldenPatch(), this->goldenPatchSize());
    TestCodec::State state;

    std::vector<U8> patch = good;
    setHeaderSize(patch, DeltaCodec::HDR_CHUNK_BYTES,
                  DELTA_MAX_CHUNK_BYTES + 1);  // chunk_bytes above the flight cap
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

    patch = good;
    setHeaderSize(patch, DeltaCodec::HDR_CHUNK_BYTES, 0);  // chunk_bytes == 0
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

    patch = good;
    setHeaderSize(patch, DeltaCodec::HDR_OLD_SIZE,
                  DELTA_MAX_IMAGE_SIZE + 1);  // old_size
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

    patch = good;
    setHeaderSize(patch, DeltaCodec::HDR_NEW_SIZE,
                  DELTA_MAX_IMAGE_SIZE + 1);  // new_size
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_HEADER);

    // coded_len above the per-chunk cap must be rejected before any offset
    // arithmetic; a value near the FwSizeType max would wrap a naive (pos +
    // header + coded_len) comparison
    patch = good;
    const FwSizeType huge = static_cast<FwSizeType>(0) - 16;
    putBe(&patch[DeltaCodec::HEADER_SIZE], huge, DeltaCodec::SIZE_FIELD_WIDTH);
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_OPCODE);
    EXPECT_EQ(state, TestCodec::FAILED);
}

bool DeltaRulesTester::Static__RejectsOutOfRangeOperands__precondition() const {
    return true;
}

void DeltaRulesTester::Static__RejectsOutOfRangeOperands__action() {
    // REQ: a hostile patch may never direct a read outside the old image
    DeltaCoder& coder = this->m_coder;
    const FwSizeType oldSize = 100;
    const std::vector<U8> oldImage = pseudoRandom(oldSize, 7);
    TestCodec::State state;
    std::vector<U8> ops;

    // SEEK below zero
    putSeek(ops, -1);
    putCopy(ops, oldSize);
    EXPECT_EQ(applyPatch(coder, buildOpPatch(this->m_coder.id(), oldImage, oldImage, oldSize, {ops}), oldImage, state),
              DeltaCodec::BAD_OPCODE);
    EXPECT_EQ(state, TestCodec::FAILED);

    // SEEK past the end
    ops.clear();
    putSeek(ops, static_cast<I64>(oldSize) + 1);
    putCopy(ops, oldSize);
    EXPECT_EQ(applyPatch(coder, buildOpPatch(this->m_coder.id(), oldImage, oldImage, oldSize, {ops}), oldImage, state),
              DeltaCodec::BAD_OPCODE);

    // SEEK legally to the end, then COPY one byte
    ops.clear();
    putSeek(ops, static_cast<I64>(oldSize));
    putCopy(ops, 1);
    EXPECT_EQ(applyPatch(coder, buildOpPatch(this->m_coder.id(), oldImage, oldImage, oldSize, {ops}), oldImage, state),
              DeltaCodec::BAD_OPCODE);

    // COPY whose span crosses the end of the old image by one byte
    ops.clear();
    putSeek(ops, 1);
    putCopy(ops, oldSize);
    EXPECT_EQ(applyPatch(coder, buildOpPatch(this->m_coder.id(), oldImage, oldImage, oldSize, {ops}), oldImage, state),
              DeltaCodec::BAD_OPCODE);

    // Over-long varint operand (6 continuation bytes)
    ops.clear();
    putCopy(ops, 50);
    ops.push_back(0x00);
    for (int i = 0; i < 5; i++) {
        ops.push_back(0x80);
    }
    ops.push_back(0x01);
    EXPECT_EQ(applyPatch(coder, buildOpPatch(this->m_coder.id(), oldImage, oldImage, oldSize, {ops}), oldImage, state),
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
    const std::vector<U8> patch = buildOpPatch(this->m_coder.id(), oldImage, rotated, oldSize, {ops});
    MemoryMedia patchMedia(patch.data(), patch.size());
    MemoryMedia newMedia;
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    U32 steps = 0;
    EXPECT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
    EXPECT_EQ(newMedia.data, rotated);
}

bool DeltaRulesTester::Static__RejectsConsecutiveSeeks__precondition() const {
    return true;
}

void DeltaRulesTester::Static__RejectsConsecutiveSeeks__action() {
    // REQ: per-step work is bounded by configuration, not patch content. A SEEK
    // produces no output, so a chunk of back-to-back SEEKs (cheap to encode,
    // expensive to decode once a coder expands it) must be rejected at the
    // second SEEK rather than run to exhaustion of the coded payload.
    const FwSizeType oldSize = 100;
    const std::vector<U8> oldImage = pseudoRandom(oldSize, 11);
    TestCodec::State state;

    // Two literal SEEKs with the None coder: a valid encoder folds these into one
    DeltaCoderNone none;
    std::vector<U8> ops;
    putSeek(ops, 1);
    putSeek(ops, -1);
    putCopy(ops, oldSize);
    EXPECT_EQ(applyPatch(none, buildOpPatch(DeltaCoder::ID_NONE, oldImage, oldImage, oldSize, {ops}), oldImage, state),
              DeltaCodec::BAD_OPCODE);
    EXPECT_EQ(state, TestCodec::FAILED);

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
    std::vector<U8> patch = buildOpPatch(this->m_coder.id(), oldImage, oldImage, oldSize, {coded});
    patch[5] = DeltaCoder::ID_LZSS;
    putBe(&patch[DeltaCodec::HDR_CRC], crcOf(patch.data(), DeltaCodec::HDR_CRC), sizeof(U32));
    MemoryMedia oldMedia(oldImage.data(), oldImage.size());
    MemoryMedia patchMedia(patch.data(), patch.size());
    MemoryMedia newMedia;
    TestCodec codec(lzss);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    const U32 readsBefore = patchMedia.reads;
    U32 steps = 0;
    EXPECT_EQ(run(codec, steps), DeltaCodec::BAD_OPCODE);
    EXPECT_EQ(codec.state(), TestCodec::FAILED);
    // Rejection at the second SEEK needs a single patch-buffer fill of the chunk
    EXPECT_LE(patchMedia.reads - readsBefore, 2u);
}

bool DeltaRulesTester::Static__VerifySpansSteps__precondition() const {
    return true;
}

void DeltaRulesTester::Static__VerifySpansSteps__action() {
    // REQ: verification work is bounded per step; partial-chunk CRC state carries
    // across steps and a corrupt chunk that straddles a step boundary is
    // discarded without corrupting the committed CRC
    static_assert(DELTA_VERIFY_BYTES_PER_STEP == 4096, "Test geometry assumes the default verify budget");
    DeltaCoder& coder = this->m_coder;
    const FwSizeType imageSize = 10000;
    const U32 chunkBytes = 3000;  // 4 chunks: 3000, 3000, 3000, 1000
    const std::vector<U8> image = pseudoRandom(imageSize, 42);
    std::vector<std::vector<U8>> chunks;
    for (FwSizeType start = 0; start < imageSize; start += chunkBytes) {
        std::vector<U8> ops;
        putCopy(ops, ((imageSize - start) < chunkBytes) ? (imageSize - start) : chunkBytes);
        chunks.push_back(ops);
    }
    const std::vector<U8> patch = buildOpPatch(this->m_coder.id(), image, image, chunkBytes, chunks);

    // Output holds 3 chunks; the second is corrupt at a byte verified only on the
    // second step
    MemoryMedia oldMedia(image.data(), image.size());
    MemoryMedia patchMedia(patch.data(), patch.size());
    MemoryMedia newMedia(image.data(), 3 * chunkBytes);
    newMedia.data[chunkBytes + 2000] ^= 0xFF;
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::VERIFY_RESUME);

    // Step 1: chunk 0 (3000 B) verified, 1096 B of chunk 1 read
    EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::VERIFY_RESUME);
    EXPECT_EQ(codec.resumedChunks(), 1u);
    // Step 2: chunk 1 completes and fails its CRC -> verification stops, old
    // image check begins
    EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::VERIFY_OLD);
    EXPECT_EQ(codec.resumedChunks(), 1u);
    EXPECT_EQ(codec.chunkIndex(), 1u);
    // Old image: 10000 B takes ceil(10000/4096) = 3 steps
    EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::VERIFY_OLD);
    EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::VERIFY_OLD);
    EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::PATCHING);

    U32 steps = 0;
    ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
    // chunks 1..3 re-applied, finish (flush), then ceil(10000/4096) = 3 read-back
    // steps
    EXPECT_EQ(steps, 3u + 1u + 3u);
    EXPECT_EQ(codec.resumedChunks(), 1u);
    EXPECT_EQ(codec.bytesWritten(),
              imageSize);  // total verified output, including the resumed chunk
    EXPECT_EQ(newMedia.data, image);
}

bool DeltaRulesTester::Static__RejectsOldImageMismatch__precondition() const {
    return true;
}

void DeltaRulesTester::Static__RejectsOldImageMismatch__action() {
    DeltaCoder& coder = this->m_coder;
    const std::vector<U8> patch = toVector(this->goldenPatch(), this->goldenPatchSize());
    TestCodec::State state;

    std::vector<U8> oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    oldImage[1234] ^= 0x10;
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::OLD_IMAGE_MISMATCH);

    oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE - 1);
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::OLD_IMAGE_MISMATCH);
}

bool DeltaRulesTester::Static__RejectsTruncatedAndOversizedPatch__precondition() const {
    return true;
}

void DeltaRulesTester::Static__RejectsTruncatedAndOversizedPatch__action() {
    DeltaCoder& coder = this->m_coder;
    const std::vector<U8> oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    const std::vector<U8> good = toVector(this->goldenPatch(), this->goldenPatchSize());
    TestCodec::State state;

    std::vector<U8> patch = good;
    patch.resize(good.size() - 5);
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::TRUNCATED);

    patch = good;
    patch.resize(DeltaCodec::HEADER_SIZE + 3);  // partial first chunk header
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::TRUNCATED);

    patch = good;
    patch.push_back(0);
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::PATCH_SIZE_MISMATCH);
}

bool DeltaRulesTester::Static__RejectsCorruptChunk__precondition() const {
    return true;
}

void DeltaRulesTester::Static__RejectsCorruptChunk__action() {
    DeltaCoder& coder = this->m_coder;
    const std::vector<U8> oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    const std::vector<U8> good = toVector(this->goldenPatch(), this->goldenPatchSize());
    TestCodec::State state;

    // Corrupt chunk 0's declared output CRC
    std::vector<U8> patch = good;
    patch[DeltaCodec::HEADER_SIZE + DeltaCodec::CHUNK_HDR_CRC] ^= 0x01;
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::CHUNK_CRC);

    // Corrupt an op byte (first payload byte) into an unknown opcode
    patch = good;
    patch[DeltaCodec::HEADER_SIZE + DeltaCodec::CHUNK_HEADER_SIZE] = 0x7F;
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_OPCODE);

    // Payload with an op whose length exceeds the chunk output
    patch = good;
    patch[DeltaCodec::HEADER_SIZE + DeltaCodec::CHUNK_HEADER_SIZE] = 0x00;      // COPY
    patch[DeltaCodec::HEADER_SIZE + DeltaCodec::CHUNK_HEADER_SIZE + 1] = 0xFF;  // uvar 0x7FFF...
    patch[DeltaCodec::HEADER_SIZE + DeltaCodec::CHUNK_HEADER_SIZE + 2] = 0x7F;
    EXPECT_EQ(applyPatch(coder, patch, oldImage, state), DeltaCodec::BAD_OPCODE);
}

bool DeltaRulesTester::Static__StagesIdempotentAfterDone__precondition() const {
    return true;
}

void DeltaRulesTester::Static__StagesIdempotentAfterDone__action() {
    // A stage function called again after it reported STAGE_DONE (the
    // component's recovery for a dropped stageDone signal) must report
    // STAGE_DONE again rather than fail
    DeltaCoder& coder = this->m_coder;
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
    MemoryMedia newMedia;
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    const U32 MAX_CALLS = 100000;
    DeltaCodec::StepResult result = DeltaCodec::IN_PROGRESS;
    for (U32 i = 0; i < MAX_CALLS && result != DeltaCodec::STAGE_DONE; i++) {
        ASSERT_EQ(codec.codec.verifyOld(result), DeltaCodec::OP_OK);
    }
    ASSERT_EQ(result, DeltaCodec::STAGE_DONE);
    result = DeltaCodec::IN_PROGRESS;
    EXPECT_EQ(codec.codec.verifyOld(result), DeltaCodec::OP_OK);
    EXPECT_EQ(result, DeltaCodec::STAGE_DONE);
    EXPECT_EQ(codec.codec.actualOldCrc(), TestVectors::OLD_CRC);

    result = DeltaCodec::IN_PROGRESS;
    for (U32 i = 0; i < MAX_CALLS && result != DeltaCodec::STAGE_DONE; i++) {
        ASSERT_EQ(codec.codec.patchChunk(result), DeltaCodec::OP_OK);
    }
    ASSERT_EQ(result, DeltaCodec::STAGE_DONE);
    result = DeltaCodec::IN_PROGRESS;
    EXPECT_EQ(codec.codec.patchChunk(result), DeltaCodec::OP_OK);
    EXPECT_EQ(result, DeltaCodec::STAGE_DONE);

    result = DeltaCodec::IN_PROGRESS;
    for (U32 i = 0; i < MAX_CALLS && result != DeltaCodec::STAGE_DONE; i++) {
        ASSERT_EQ(codec.codec.verifyFinal(result), DeltaCodec::OP_OK);
    }
    ASSERT_EQ(result, DeltaCodec::STAGE_DONE);
    result = DeltaCodec::IN_PROGRESS;
    EXPECT_EQ(codec.codec.verifyFinal(result), DeltaCodec::OP_OK);
    EXPECT_EQ(result, DeltaCodec::STAGE_DONE);

    // Resume over the finished output: verifyResume is the stage that runs, and
    // it too must report STAGE_DONE again when re-called
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    ASSERT_TRUE(codec.codec.hasOutputToVerify());
    result = DeltaCodec::IN_PROGRESS;
    for (U32 i = 0; i < MAX_CALLS && result != DeltaCodec::STAGE_DONE; i++) {
        ASSERT_EQ(codec.codec.verifyResume(result), DeltaCodec::OP_OK);
    }
    ASSERT_EQ(result, DeltaCodec::STAGE_DONE);
    result = DeltaCodec::IN_PROGRESS;
    EXPECT_EQ(codec.codec.verifyResume(result), DeltaCodec::OP_OK);
    EXPECT_EQ(result, DeltaCodec::STAGE_DONE);
    EXPECT_EQ(codec.codec.resumedChunks(), codec.codec.chunkCount());
}

bool DeltaRulesTester::Static__ResumeReentryAfterHeaderReadFailure__precondition() const {
    return true;
}

void DeltaRulesTester::Static__ResumeReentryAfterHeaderReadFailure__action() {
    // A transient patch read error while reading a chunk header during the resume
    // scan ends the stage with an error; a re-entered stage (the component's fail
    // signal was dropped) must re-verify that chunk without counting its bytes
    // twice, so the final image CRC still matches
    DeltaCoder& coder = this->m_coder;
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
    MemoryMedia newMedia(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE);
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    ASSERT_TRUE(codec.codec.hasOutputToVerify());
    patchMedia.failReads = true;
    DeltaCodec::StepResult result = DeltaCodec::IN_PROGRESS;
    DeltaCodec::Status status = DeltaCodec::OP_OK;
    U32 calls = 0;
    for (; calls < 100 && status == DeltaCodec::OP_OK; calls++) {
        status = codec.codec.verifyResume(result);
    }
    EXPECT_NE(status, DeltaCodec::OP_OK);
    EXPECT_EQ(codec.codec.resumedChunks(), 0u);
    patchMedia.failReads = false;
    U32 steps = 0;
    EXPECT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
    EXPECT_EQ(codec.codec.resumedChunks(), codec.codec.chunkCount());
    EXPECT_EQ(newMedia.data, toVector(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE));
}

bool DeltaRulesTester::Static__MediaErrors__precondition() const {
    return true;
}

void DeltaRulesTester::Static__MediaErrors__action() {
    DeltaCoder& coder = this->m_coder;
    {
        MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
        MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
        MemoryMedia newMedia;
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        oldMedia.failReads = true;
        EXPECT_EQ(codec.step(), DeltaCodec::READ_ERROR);
        EXPECT_EQ(codec.state(), TestCodec::FAILED);
        EXPECT_EQ(codec.lastStatus(), DeltaCodec::READ_ERROR);
        // A failed engine stays failed: a further verify step is still rejected
        DeltaCodec::StepResult stepResult = DeltaCodec::IN_PROGRESS;
        EXPECT_EQ(codec.codec.verifyOld(stepResult), DeltaCodec::READ_ERROR);
    }
    {
        MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
        MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
        MemoryMedia newMedia;
        newMedia.failWrites = true;
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        U32 steps = 0;
        EXPECT_EQ(run(codec, steps), DeltaCodec::WRITE_ERROR);
        EXPECT_EQ(codec.chunkIndex(), 0u);
    }
    {
        MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
        MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
        MemoryMedia newMedia;
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        ASSERT_EQ(codec.step(), DeltaCodec::OP_OK);  // old verify
        patchMedia.failReads = true;
        EXPECT_EQ(codec.step(), DeltaCodec::READ_ERROR);
    }
}

bool DeltaRulesTester::Static__ReadBackCatchesStorageFaults__precondition() const {
    return true;
}

void DeltaRulesTester::Static__ReadBackCatchesStorageFaults__action() {
    // REQ: DP-005/DP-008. COMPLETE is reported only after the stored new image
    // has been flushed, read back and CRC-checked; a media that acknowledges a
    // write but stores it wrong, or that cannot flush, must fail the patch.
    DeltaCoder& coder = this->m_coder;
    {
        MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
        MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
        MemoryMedia newMedia;
        newMedia.corruptStored = true;
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        U32 steps = 0;
        EXPECT_EQ(run(codec, steps), DeltaCodec::NEW_IMAGE_MISMATCH);
        EXPECT_EQ(codec.state(), TestCodec::FAILED);
        EXPECT_EQ(newMedia.flushes, static_cast<U32>(codec.chunkCount()) + 1u);
    }
    {
        MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
        MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
        MemoryMedia newMedia;
        newMedia.failFlush = true;
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        U32 steps = 0;
        EXPECT_EQ(run(codec, steps), DeltaCodec::WRITE_ERROR);
        EXPECT_EQ(codec.state(), TestCodec::FAILED);
    }
    {
        // Read failure during the read-back pass is a READ_ERROR
        MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
        MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
        MemoryMedia newMedia;
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        while (codec.state() != TestCodec::VERIFY_FINAL) {
            ASSERT_EQ(codec.step(), DeltaCodec::OP_OK);
        }
        newMedia.failReads = true;
        EXPECT_EQ(codec.step(), DeltaCodec::READ_ERROR);
        EXPECT_EQ(codec.state(), TestCodec::FAILED);
    }
}

bool DeltaRulesTester::Static__ResetReturnsToIdle__precondition() const {
    return true;
}

void DeltaRulesTester::Static__ResetReturnsToIdle__action() {
    DeltaCoder& coder = this->m_coder;
    MemoryMedia oldMedia(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    MemoryMedia patchMedia(this->goldenPatch(), this->goldenPatchSize());
    MemoryMedia newMedia;
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    ASSERT_EQ(codec.step(), DeltaCodec::OP_OK);
    ASSERT_EQ(codec.step(), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.chunkIndex(), 1u);
    codec.reset();
    EXPECT_EQ(codec.state(), TestCodec::IDLE);
    EXPECT_EQ(codec.chunkIndex(), 0u);
    EXPECT_FALSE(codec.codec.hasOutputToVerify());
}

bool DeltaRulesTester::Static__FailedChunkNeverCommitsOutput__precondition() const {
    return true;
}

void DeltaRulesTester::Static__FailedChunkNeverCommitsOutput__action() {
    // REQ: a chunk is validated before its final output slice is written, so a
    // rejected chunk never exists on the media as a complete chunk a later resume
    // could trust; the retry resumes at the failed chunk, not past it
    DeltaCoder& coder = this->m_coder;
    const std::vector<U8> oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    const std::vector<U8> good = toVector(this->goldenPatch(), this->goldenPatchSize());
    const FwSizeType bad = 2;  // chunk index to corrupt
    // Locate chunk 2's header by walking coded lengths
    FwSizeType pos = DeltaCodec::HEADER_SIZE;
    for (FwSizeType i = 0; i < bad; i++) {
        FwSizeType coded = 0;
        for (FwSizeType b = 0; b < DeltaCodec::SIZE_FIELD_WIDTH; b++) {
            coded = (coded << 8) | good[pos + DeltaCodec::CHUNK_HDR_CODED_LENGTH + b];
        }
        pos += DeltaCodec::CHUNK_HEADER_SIZE + coded;
    }
    std::vector<U8> patch = good;
    patch[pos + DeltaCodec::CHUNK_HDR_CRC] ^= 0x01;

    MemoryMedia oldMedia(oldImage.data(), oldImage.size());
    MemoryMedia badMedia(patch.data(), patch.size());
    MemoryMedia newMedia;
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, badMedia, newMedia), DeltaCodec::OP_OK);
    U32 steps = 0;
    EXPECT_EQ(run(codec, steps), DeltaCodec::CHUNK_CRC);
    EXPECT_EQ(codec.chunkIndex(), bad);
    // Chunks 0..1 are complete; chunk 2 is missing at least its last slice
    const FwSizeType chunkStart = bad * TestVectors::CHUNK_BYTES;
    EXPECT_GE(newMedia.data.size(), chunkStart);
    EXPECT_LT(newMedia.data.size(), chunkStart + TestVectors::CHUNK_BYTES);
    EXPECT_EQ(newMedia.flushes, static_cast<U32>(bad));

    // Retry with the good patch: resume trusts exactly the committed chunks
    MemoryMedia goodMedia(good.data(), good.size());
    ASSERT_EQ(codec.begin(oldMedia, goodMedia, newMedia), DeltaCodec::OP_OK);
    steps = 0;
    ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
    EXPECT_EQ(codec.resumedChunks(), bad);
    EXPECT_EQ(newMedia.data, toVector(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE));
}

bool DeltaRulesTester::Static__IoBudgetSpansSteps__precondition() const {
    return true;
}

void DeltaRulesTester::Static__IoBudgetSpansSteps__action() {
    // REQ: media I/O per step is bounded by DELTA_MAX_IO_PER_STEP regardless of
    // operation density, the chunk carries over to the next step, and adjacent
    // short COPYs are served from the old-image cache rather than one read each
    DeltaCoder& coder = this->m_coder;
    const FwSizeType imageSize = 3000;
    const std::vector<U8> image = pseudoRandom(imageSize, 7);
    std::vector<U8> ops;
    for (FwSizeType i = 0; i < imageSize; i++) {
        putCopy(ops, 1);  // 3000 one-byte COPYs
    }
    const std::vector<U8> patch = buildOpPatch(this->m_coder.id(), image, image, imageSize, {ops});
    MemoryMedia oldMedia(image.data(), image.size());
    MemoryMedia patchMedia(patch.data(), patch.size());
    MemoryMedia newMedia;
    TestCodec codec(coder);
    ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.step(), DeltaCodec::OP_OK);  // old image verified
    ASSERT_EQ(codec.state(), TestCodec::PATCHING);

    U32 patchSteps = 0;
    U32 maxIoPerStep = 0;
    for (U32 i = 0; i < 1000 && codec.state() == TestCodec::PATCHING; i++) {
        const U32 before = oldMedia.reads + patchMedia.reads + newMedia.reads + newMedia.writes;
        ASSERT_EQ(codec.step(), DeltaCodec::OP_OK);
        const U32 io = oldMedia.reads + patchMedia.reads + newMedia.reads + newMedia.writes - before;
        maxIoPerStep = (io > maxIoPerStep) ? io : maxIoPerStep;
        patchSteps++;
    }
    // The budget check follows each operation; one operation issues at most a
    // patch read, an old read and a write, so the overshoot is bounded by 3
    EXPECT_LE(maxIoPerStep, DELTA_MAX_IO_PER_STEP + 3);
    EXPECT_GT(patchSteps, 1u);
    EXPECT_EQ(codec.chunkIndex(), 1u);
    // Old-image reads: one per cache fill while patching (plus the verification
    // pass, which reads in output-buffer pieces), not one per COPY
    EXPECT_LE(oldMedia.reads,
              static_cast<U32>(imageSize / DELTA_OLD_BUFFER_SIZE + imageSize / DELTA_OUTPUT_BUFFER_SIZE + 2));
    U32 steps = 0;
    ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
    EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
    EXPECT_EQ(newMedia.data, image);
}

bool DeltaRulesTester::Static__ClassifiesShrunkMedia__precondition() const {
    return true;
}

void DeltaRulesTester::Static__ClassifiesShrunkMedia__action() {
    // REQ: media that no longer has the size captured at begin() is reported as
    // the documented mismatch (TRUNCATED / OLD_IMAGE_MISMATCH), not READ_ERROR;
    // existing output that becomes unreadable during the resume walk restarts the
    // patch at that chunk instead of failing it
    DeltaCoder& coder = this->m_coder;
    const std::vector<U8> oldImage = toVector(TestVectors::OLD_IMAGE, TestVectors::OLD_IMAGE_SIZE);
    const std::vector<U8> patch = toVector(this->goldenPatch(), this->goldenPatchSize());
    {
        MemoryMedia oldMedia(oldImage.data(), oldImage.size());
        MemoryMedia patchMedia(patch.data(), patch.size());
        MemoryMedia newMedia;
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        patchMedia.data.resize(DeltaCodec::HEADER_SIZE + 2);
        U32 steps = 0;
        EXPECT_EQ(run(codec, steps), DeltaCodec::TRUNCATED);
    }
    {
        MemoryMedia oldMedia(oldImage.data(), oldImage.size());
        MemoryMedia patchMedia(patch.data(), patch.size());
        MemoryMedia newMedia;
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        oldMedia.data.resize(100);
        U32 steps = 0;
        EXPECT_EQ(run(codec, steps), DeltaCodec::OLD_IMAGE_MISMATCH);
    }
    {
        MemoryMedia oldMedia(oldImage.data(), oldImage.size());
        MemoryMedia patchMedia(patch.data(), patch.size());
        MemoryMedia newMedia(TestVectors::NEW_IMAGE, 2 * TestVectors::CHUNK_BYTES);
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        EXPECT_EQ(codec.state(), TestCodec::VERIFY_RESUME);
        newMedia.data.resize(10);  // output vanished under us
        U32 steps = 0;
        ASSERT_EQ(run(codec, steps), DeltaCodec::OP_OK);
        EXPECT_EQ(codec.state(), TestCodec::COMPLETE);
        EXPECT_EQ(codec.resumedChunks(), 0u);
        EXPECT_EQ(newMedia.data, toVector(TestVectors::NEW_IMAGE, TestVectors::NEW_IMAGE_SIZE));
    }
    {
        // Media that cannot be re-resolved for read-back is a mismatch, not
        // COMPLETE
        MemoryMedia oldMedia(oldImage.data(), oldImage.size());
        MemoryMedia patchMedia(patch.data(), patch.size());
        MemoryMedia newMedia;
        newMedia.failRefresh = true;
        TestCodec codec(coder);
        ASSERT_EQ(codec.begin(oldMedia, patchMedia, newMedia), DeltaCodec::OP_OK);
        U32 steps = 0;
        EXPECT_EQ(run(codec, steps), DeltaCodec::NEW_IMAGE_MISMATCH);
        EXPECT_EQ(newMedia.refreshes, 1u);
    }
}

}  // namespace Update
