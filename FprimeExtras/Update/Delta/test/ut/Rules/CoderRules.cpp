// ======================================================================
// \title  CoderRules.cpp
// \author starchmd
// \brief  Stream-level rules for the shipped coders: every encoder output
// decodes back byte-exact under any slicing; cut or random streams never
// yield wrong bytes or run away
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#include <gtest/gtest.h>

#include "FprimeExtras/Update/Delta/test/ut/DeltaRulesTester.hpp"

namespace Update {

// ----------------------------------------------------------------------
// Coder.RoundTrip: encode(reference) -> decode(flight) in random slices == input
// ----------------------------------------------------------------------

bool DeltaRulesTester::Coder__RoundTrip__precondition() const {
    return true;
}

void DeltaRulesTester::Coder__RoundTrip__action() {
    const U8 id = this->m_coder.id();
    const std::vector<U8> payload = TestEncoders::randomPayload(STest::Pick::lowerUpper(0, 2048));
    const std::vector<U8> coded = TestEncoders::encode(id, payload);
    std::vector<U8> out;
    DeltaCoder& coder = this->m_coder;
    ASSERT_EQ(coder.id(), id);
    const DeltaCoder::Status status = decodeAll(coder, coded.data(), coded.size(), STest::Pick::lowerUpper(1, 300),
                                                STest::Pick::lowerUpper(1, DELTA_WINDOW_SIZE), out);
    ASSERT_EQ(status, DeltaCoder::OP_OK) << "coder " << static_cast<int>(id) << " size " << payload.size();
    ASSERT_EQ(out, payload) << "coder " << static_cast<int>(id);
    ASSERT_FALSE(coder.pending());
    this->m_counters.roundTrips++;
}

// ----------------------------------------------------------------------
// Coder.Truncated: a stream cut anywhere decodes to a prefix of the input
// ----------------------------------------------------------------------

bool DeltaRulesTester::Coder__Truncated__precondition() const {
    return true;
}

void DeltaRulesTester::Coder__Truncated__action() {
    const U8 id = this->m_coder.id();
    const std::vector<U8> payload = TestEncoders::randomPayload(STest::Pick::lowerUpper(1, 1024));
    std::vector<U8> coded = TestEncoders::encode(id, payload);
    coded.resize(STest::Pick::lowerUpper(0, static_cast<U32>(coded.size() - 1)));
    std::vector<U8> out;
    DeltaCoder& coder = this->m_coder;
    (void)decodeAll(coder, coded.data(), coded.size(), STest::Pick::lowerUpper(1, 300),
                    STest::Pick::lowerUpper(1, DELTA_WINDOW_SIZE), out);
    ASSERT_LE(out.size(), payload.size());
    ASSERT_TRUE(std::equal(out.begin(), out.end(), payload.begin())) << "coder " << static_cast<int>(id);
    this->m_counters.truncatedStreams++;
}

// ----------------------------------------------------------------------
// Coder.Garbage: random bytes terminate with bounded output and no assert
// ----------------------------------------------------------------------

bool DeltaRulesTester::Coder__Garbage__precondition() const {
    return true;
}

void DeltaRulesTester::Coder__Garbage__action() {
    const U8 id = this->m_coder.id();
    std::vector<U8> coded(STest::Pick::lowerUpper(0, 512));
    for (FwSizeType i = 0; i < coded.size(); i++) {
        coded[i] = static_cast<U8>(STest::Pick::lowerUpper(0, 255));
    }
    std::vector<U8> out;
    (void)decodeAll(this->m_coder, coded.data(), coded.size(), STest::Pick::lowerUpper(1, 300),
                    STest::Pick::lowerUpper(1, DELTA_WINDOW_SIZE), out);
    // RLE expands at most 129x per control byte; LZSS 258x per match token
    ASSERT_LE(out.size(), coded.size() * 258u + 1u);
    this->m_counters.garbageStreams++;
}

}  // namespace Update
