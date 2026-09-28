// ======================================================================
// \title  DeltaRing.hpp
// \author starchmd
// \brief  hpp file for the fixed-size ring buffer shared by DeltaCoder and DeltaCodec
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaRing_HPP
#define Update_Delta_DeltaRing_HPP

#include "Fw/FPrimeBasicTypes.hpp"
#include "Fw/Types/Assert.hpp"

namespace Update {

//! \brief Ring buffer of decoded operation bytes
//!
//! A DeltaCoder pushes decoded bytes into the ring and the DeltaCodec consumes them. The ring doubles as the
//! coder's history window: bytes remain addressable via history() after they are consumed, until overwritten by
//! subsequent pushes. Thus a window-based coder (e.g. LZSS) needs no window storage of its own. This
//! consumed-but-addressable history is why Types::CircularBuffer is not used: it releases bytes on deserialize.
class DeltaRing {
  public:
    //! Construct a ring over externally owned storage
    DeltaRing(U8* storage, FwSizeType capacity)
        : m_data(storage), m_capacity(capacity), m_head(0), m_count(0), m_history(0) {
        FW_ASSERT(storage != nullptr);
        FW_ASSERT(capacity > 0);
    }

    //! Discard all content and zero the history so that references before the stream start are deterministic
    void reset() {
        for (FwSizeType i = 0; i < this->m_capacity; i++) {
            this->m_data[i] = 0;
        }
        this->m_head = 0;
        this->m_count = 0;
        this->m_history = 0;
    }

    //! Total capacity (also the maximum history distance)
    FwSizeType capacity() const { return this->m_capacity; }

    //! Number of unconsumed bytes
    FwSizeType count() const { return this->m_count; }

    //! Number of bytes that may be pushed before the ring is full
    FwSizeType space() const { return this->m_capacity - this->m_count; }

    //! Number of bytes addressable via history(): pushes since reset, saturating at capacity
    FwSizeType historyAvailable() const { return this->m_history; }

    //! Push one byte; it is invalid to push into a full ring
    void push(U8 byte) {
        FW_ASSERT(this->m_count < this->m_capacity, static_cast<FwAssertArgType>(this->m_count));
        this->m_data[this->m_head] = byte;
        this->m_head = (this->m_head + 1) % this->m_capacity;
        this->m_count++;
        if (this->m_history < this->m_capacity) {
            this->m_history++;
        }
    }

    //! Byte pushed `distance` pushes ago (1 = most recent). distance must be in [1, capacity]
    U8 history(FwSizeType distance) const {
        FW_ASSERT(distance >= 1 && distance <= this->m_capacity, static_cast<FwAssertArgType>(distance));
        return this->m_data[(this->m_head + this->m_capacity - distance) % this->m_capacity];
    }

    //! Peek at unconsumed byte at `index` (0 = oldest unconsumed). index must be < count()
    U8 peek(FwSizeType index) const {
        FW_ASSERT(index < this->m_count, static_cast<FwAssertArgType>(index));
        return this->m_data[(this->m_head + this->m_capacity - this->m_count + index) % this->m_capacity];
    }

    //! Consume `n` bytes without copying them. n must be <= count()
    void pop(FwSizeType n) {
        FW_ASSERT(n <= this->m_count, static_cast<FwAssertArgType>(n));
        this->m_count -= n;
    }

    //! Consume `n` bytes copying them to `out`. n must be <= count()
    void popInto(U8* out, FwSizeType n) {
        FW_ASSERT(out != nullptr);
        for (FwSizeType i = 0; i < n; i++) {
            out[i] = this->peek(i);
        }
        this->pop(n);
    }

  private:
    U8* m_data;
    FwSizeType m_capacity;
    FwSizeType m_head;     //!< Next write index
    FwSizeType m_count;    //!< Unconsumed bytes
    FwSizeType m_history;  //!< Bytes pushed since reset, saturating at capacity
};

}  // namespace Update
#endif
