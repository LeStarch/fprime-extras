// ======================================================================
// \title  DeltaTestEncoders.hpp
// \author starchmd
// \brief  Test-side reference encoders (none / RLE / LZSS, mirroring
// python/fprime_extras_spatch/coders.py) and a random SPatch builder that
// produces a valid patch and the new image it must yield
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_Test_DeltaTestEncoders_HPP
#define Update_Delta_Test_DeltaTestEncoders_HPP

#include <vector>

#include "FprimeExtras/Update/Delta/DeltaCodec.hpp"
#include "FprimeExtras/Update/Delta/DeltaCoder.hpp"
#include "FprimeExtras/Update/Delta/test/ut/DeltaTestSupport.hpp"
#include "STest/Pick/Pick.hpp"

namespace Update {
namespace TestEncoders {

typedef std::vector<U8> Bytes;

// Operation codes of the SPatch op stream (DeltaCodec keeps its own copy private)
static const U8 OP_COPY = 0;
static const U8 OP_ADD = 1;
static const U8 OP_LIT = 2;
static const U8 OP_SEEK = 3;

inline Bytes noneEncode(const Bytes& data) {
    return data;
}

// Control byte < 0x80: literal run of (control + 1) bytes follows; >= 0x80: the next byte repeats (control - 0x80 + 2)
inline Bytes rleEncode(const Bytes& data) {
    static const FwSizeType MAX_LITERAL = 0x80;
    static const FwSizeType MAX_REPEAT = 0x7F + 2;
    Bytes out;
    Bytes literal;
    const FwSizeType n = data.size();
    for (FwSizeType i = 0; i < n;) {
        FwSizeType run = 1;
        for (; i + run < n && data[i + run] == data[i] && run < MAX_REPEAT; run++) {
        }
        if (run >= 2) {
            for (FwSizeType start = 0; start < literal.size(); start += MAX_LITERAL) {
                const FwSizeType piece =
                    ((literal.size() - start) < MAX_LITERAL) ? (literal.size() - start) : MAX_LITERAL;
                out.push_back(static_cast<U8>(piece - 1));
                out.insert(out.end(), literal.begin() + start, literal.begin() + start + piece);
            }
            literal.clear();
            out.push_back(static_cast<U8>(0x80 + (run - 2)));
            out.push_back(data[i]);
            i += run;
        } else {
            literal.push_back(data[i]);
            i++;
        }
    }
    for (FwSizeType start = 0; start < literal.size(); start += MAX_LITERAL) {
        const FwSizeType piece = ((literal.size() - start) < MAX_LITERAL) ? (literal.size() - start) : MAX_LITERAL;
        out.push_back(static_cast<U8>(piece - 1));
        out.insert(out.end(), literal.begin() + start, literal.begin() + start + piece);
    }
    return out;
}

// Flag byte (MSB first) then 8 items: bit 1 = match (distance-1, length-3), bit 0 = literal byte. Window 256,
// match length 3..258. Unused trailing flag bits of the final group are zero.
inline Bytes lzssEncode(const Bytes& data) {
    static const FwSizeType WINDOW = 256;
    static const FwSizeType MIN_MATCH = 3;
    static const FwSizeType MAX_MATCH = 258;
    Bytes out;
    Bytes items;
    U8 flags = 0;
    FwSizeType count = 0;
    const FwSizeType n = data.size();
    for (FwSizeType pos = 0; pos < n;) {
        FwSizeType bestLen = 0;
        FwSizeType bestDist = 0;
        const FwSizeType limit = ((n - pos) < MAX_MATCH) ? (n - pos) : MAX_MATCH;
        for (FwSizeType dist = 1; dist <= WINDOW && dist <= pos && limit >= MIN_MATCH; dist++) {
            FwSizeType length = 0;
            for (; length < limit && data[pos - dist + length] == data[pos + length]; length++) {
            }
            if (length > bestLen) {
                bestLen = length;
                bestDist = dist;
                if (length == limit) {
                    break;
                }
            }
        }
        flags = static_cast<U8>(flags << 1);
        if (bestLen >= MIN_MATCH) {
            flags |= 1;
            items.push_back(static_cast<U8>(bestDist - 1));
            items.push_back(static_cast<U8>(bestLen - MIN_MATCH));
            pos += bestLen;
        } else {
            items.push_back(data[pos]);
            pos++;
        }
        if (++count == 8) {
            out.push_back(flags);
            out.insert(out.end(), items.begin(), items.end());
            items.clear();
            flags = 0;
            count = 0;
        }
    }
    if (count > 0) {
        out.push_back(static_cast<U8>(flags << (8 - count)));
        out.insert(out.end(), items.begin(), items.end());
    }
    return out;
}

inline Bytes encode(U8 coderId, const Bytes& data) {
    switch (coderId) {
        case DeltaCoder::ID_RLE:
            return rleEncode(data);
        case DeltaCoder::ID_LZSS:
            return lzssEncode(data);
        default:
            return noneEncode(data);
    }
}

// Unsigned LEB128
inline void uvar(Bytes& out, U64 value) {
    for (FwSizeType i = 0; i < 10; i++) {
        const U8 byte = static_cast<U8>(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            out.push_back(static_cast<U8>(byte | 0x80));
        } else {
            out.push_back(byte);
            return;
        }
    }
}

// Zigzag then LEB128
inline void svar(Bytes& out, I64 value) {
    const U64 zig = (value < 0) ? (static_cast<U64>(-(value + 1)) * 2u + 1u) : (static_cast<U64>(value) * 2u);
    uvar(out, zig);
}

//! Random data with a chosen character: pure random, run-heavy, or repeated
//! phrases (so each coder sees the input it is designed for and the others)
inline Bytes randomPayload(FwSizeType size) {
    Bytes out;
    out.reserve(size);
    const U32 mode = STest::Pick::lowerUpper(0, 3);
    while (out.size() < size) {
        switch (mode) {
            case 0:  // random bytes
                out.push_back(static_cast<U8>(STest::Pick::lowerUpper(0, 255)));
                break;
            case 1: {  // runs
                const U8 value = static_cast<U8>(STest::Pick::lowerUpper(0, 255));
                const FwSizeType run = STest::Pick::lowerUpper(1, 300);
                for (FwSizeType i = 0; i < run && out.size() < size; i++) {
                    out.push_back(value);
                }
                break;
            }
            case 2: {  // repeat an earlier phrase (LZSS matches at every distance)
                if (out.size() < 4) {
                    out.push_back(static_cast<U8>(STest::Pick::lowerUpper(0, 255)));
                } else {
                    const FwSizeType dist =
                        STest::Pick::lowerUpper(1, static_cast<U32>((out.size() < 300) ? out.size() : 300));
                    const FwSizeType len = STest::Pick::lowerUpper(1, 300);
                    for (FwSizeType i = 0; i < len && out.size() < size; i++) {
                        out.push_back(out[out.size() - dist]);
                    }
                }
                break;
            }
            default: {  // small alphabet: mixes short runs and matches
                out.push_back(static_cast<U8>('a' + STest::Pick::lowerUpper(0, 3)));
                break;
            }
        }
    }
    return out;
}

//! A random, valid SPatch and the image it produces
struct RandomPatch {
    U8 coderId;
    FwSizeType chunkBytes;
    Bytes oldImage;
    Bytes newImage;
    Bytes patch;
    std::vector<FwSizeType> chunkOffsets;  //!< Offset of each chunk header in `patch`
    std::vector<FwSizeType> codedLengths;  //!< Payload length of each chunk

    FwSizeType chunkCount() const { return this->chunkOffsets.size(); }

    //! Write the header into `patch` (which must hold HEADER_SIZE bytes) and recompute its CRC
    void writeHeader() {
        Bytes& p = this->patch;
        p[0] = 'S';
        p[1] = 'P';
        p[2] = 'A';
        p[3] = 'T';
        p[DeltaCodec::HDR_VERSION] = DeltaCodec::VERSION;
        p[DeltaCodec::HDR_CODER] = this->coderId;
        p[DeltaCodec::HDR_FLAGS] = 0;
        p[DeltaCodec::HDR_SIZE_WIDTH] = static_cast<U8>(DeltaCodec::SIZE_FIELD_WIDTH);
        putBe(&p[DeltaCodec::HDR_OLD_SIZE], this->oldImage.size(), DeltaCodec::SIZE_FIELD_WIDTH);
        putBe(&p[DeltaCodec::HDR_OLD_CRC], crc32(this->oldImage.data(), this->oldImage.size()), sizeof(U32));
        putBe(&p[DeltaCodec::HDR_NEW_SIZE], this->newImage.size(), DeltaCodec::SIZE_FIELD_WIDTH);
        putBe(&p[DeltaCodec::HDR_NEW_CRC], crc32(this->newImage.data(), this->newImage.size()), sizeof(U32));
        putBe(&p[DeltaCodec::HDR_CHUNK_BYTES], this->chunkBytes, DeltaCodec::SIZE_FIELD_WIDTH);
        putBe(&p[DeltaCodec::HDR_CRC], crc32(p.data(), DeltaCodec::HDR_CRC), sizeof(U32));
    }
};

//! Build a random valid patch: the op stream is generated first (random COPY /
//! ADD / LIT / SEEK obeying the grammar: ops never span a chunk, n > 0, SEEK
//! lands in [0, old_size] and is always followed by a producing op) and the
//! new image is whatever those ops produce from the old image.
inline RandomPatch buildRandomPatch(U8 coderId, FwSizeType oldSize, FwSizeType newSize, FwSizeType chunkBytes) {
    RandomPatch rp;
    rp.coderId = coderId;
    rp.chunkBytes = chunkBytes;
    rp.oldImage = randomPayload(oldSize);
    rp.patch.assign(DeltaCodec::HEADER_SIZE, 0);
    const FwSizeType chunkCount = (newSize + chunkBytes - 1) / chunkBytes;
    for (FwSizeType idx = 0; idx < chunkCount; idx++) {
        const FwSizeType offset = idx * chunkBytes;
        const FwSizeType length = ((newSize - offset) < chunkBytes) ? (newSize - offset) : chunkBytes;
        FwSizeType cursor = offset;  // decoder's old cursor at chunk start
        FwSizeType remaining = length;
        bool lastWasSeek = false;
        Bytes ops;
        Bytes produced;
        for (FwSizeType guard = 0; remaining > 0 && guard < 4 * length + 8; guard++) {
            U32 kind = STest::Pick::lowerUpper(0, 3);
            FwSizeType n = (STest::Pick::lowerUpper(0, 7) == 0)
                               ? remaining
                               : STest::Pick::lowerUpper(1, static_cast<U32>((remaining < 64) ? remaining : 64));
            if (kind == OP_SEEK) {
                if (lastWasSeek || oldSize == 0) {
                    kind = OP_LIT;
                } else {
                    const FwSizeType target = STest::Pick::lowerUpper(0, static_cast<U32>(oldSize));
                    ops.push_back(static_cast<U8>(OP_SEEK));
                    svar(ops, static_cast<I64>(target) - static_cast<I64>(cursor));
                    cursor = target;
                    lastWasSeek = true;
                    continue;
                }
            }
            if (kind == OP_COPY || kind == OP_ADD) {
                if (n > oldSize) {
                    n = oldSize;
                }
                if (n == 0) {
                    kind = OP_LIT;
                    n = 1;
                } else if (cursor + n > oldSize) {
                    if (lastWasSeek) {
                        kind = OP_LIT;
                    } else {
                        const FwSizeType target = STest::Pick::lowerUpper(0, static_cast<U32>(oldSize - n));
                        ops.push_back(static_cast<U8>(OP_SEEK));
                        svar(ops, static_cast<I64>(target) - static_cast<I64>(cursor));
                        cursor = target;
                    }
                }
            }
            ops.push_back(static_cast<U8>(kind));
            uvar(ops, n);
            if (kind == OP_COPY) {
                for (FwSizeType i = 0; i < n; i++) {
                    produced.push_back(rp.oldImage[cursor + i]);
                }
                cursor += n;
            } else if (kind == OP_ADD) {
                for (FwSizeType i = 0; i < n; i++) {
                    const U8 delta = static_cast<U8>(STest::Pick::lowerUpper(0, 255));
                    ops.push_back(delta);
                    produced.push_back(static_cast<U8>(rp.oldImage[cursor + i] + delta));
                }
                cursor += n;
            } else {
                const Bytes lit = randomPayload(n);
                ops.insert(ops.end(), lit.begin(), lit.end());
                produced.insert(produced.end(), lit.begin(), lit.end());
            }
            lastWasSeek = false;
            remaining -= n;
        }
        const Bytes coded = encode(coderId, ops);
        rp.chunkOffsets.push_back(rp.patch.size());
        rp.codedLengths.push_back(coded.size());
        Bytes chunkHeader(DeltaCodec::CHUNK_HEADER_SIZE, 0);
        putBe(&chunkHeader[DeltaCodec::CHUNK_HDR_CODED_LENGTH], coded.size(), DeltaCodec::SIZE_FIELD_WIDTH);
        putBe(&chunkHeader[DeltaCodec::CHUNK_HDR_CRC], crc32(produced.data(), produced.size()), sizeof(U32));
        rp.patch.insert(rp.patch.end(), chunkHeader.begin(), chunkHeader.end());
        rp.patch.insert(rp.patch.end(), coded.begin(), coded.end());
        rp.newImage.insert(rp.newImage.end(), produced.begin(), produced.end());
    }
    rp.writeHeader();
    return rp;
}

}  // namespace TestEncoders
}  // namespace Update

#endif
