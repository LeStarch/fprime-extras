// ======================================================================
// \title  DeltaWindow.hpp
// \author starchmd
// \brief  Decoded-byte window for SPatch coders, built on Types::CircularBuffer
// \copyright Copyright (c) 2026 Michael Starch
// ======================================================================
#ifndef Update_Delta_DeltaWindow_HPP
#define Update_Delta_DeltaWindow_HPP

#include "Fw/FPrimeBasicTypes.hpp"
#include "Fw/Types/Assert.hpp"
#include "Utils/Types/CircularBuffer.hpp"

namespace Update {

//! \brief FIFO of decoded bytes that also retains consumed bytes as LZSS history
//!
//! The underlying Types::CircularBuffer holds the most recent `capacity` bytes pushed. Bytes are consumed in order, but
//! remain in the buffer (and are addressable via history()) until newer bytes evict them, so a single `capacity`-byte
//! store serves as both the coder output FIFO and the LZSS back-reference window.
class DeltaWindow final {
  public:
    //! Construct over externally owned storage
    DeltaWindow(U8* storage, FwSizeType capacity) : m_buffer(storage, capacity), m_consumed(0) {
        FW_ASSERT(storage != nullptr);
        FW_ASSERT(capacity > 0);
    }

    DeltaWindow(const DeltaWindow&) = delete;
    DeltaWindow& operator=(const DeltaWindow&) = delete;

    //! Discard all content and history
    void reset() {
        const Fw::SerializeStatus status = this->m_buffer.rotate(this->m_buffer.get_allocated_size());
        FW_ASSERT(status == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(status));
        this->m_consumed = 0;
    }

    //! Total capacity (also the maximum history distance)
    FwSizeType capacity() const { return this->m_buffer.get_capacity(); }

    //! Number of unconsumed bytes
    FwSizeType count() const { return this->m_buffer.get_allocated_size() - this->m_consumed; }

    //! Number of bytes that may be pushed before the window is full of unconsumed bytes
    FwSizeType space() const { return this->capacity() - this->count(); }

    //! Number of bytes addressable via history(): pushes since reset, saturating at capacity
    FwSizeType historyAvailable() const { return this->m_buffer.get_allocated_size(); }

    //! Push one byte, evicting the oldest consumed byte if the store is full; it is invalid to push when space() == 0
    void push(U8 byte) {
        FW_ASSERT(this->space() > 0);
        if (this->m_buffer.get_free_size() == 0) {
            FW_ASSERT(this->m_consumed > 0);
            const Fw::SerializeStatus status = this->m_buffer.rotate(1);
            FW_ASSERT(status == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(status));
            this->m_consumed--;
        }
        const Fw::SerializeStatus status = this->m_buffer.serialize(&byte, sizeof(byte));
        FW_ASSERT(status == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(status));
    }

    //! Byte pushed `distance` pushes ago (1 = most recent). distance must be in [1, historyAvailable()]
    U8 history(FwSizeType distance) const {
        FW_ASSERT(distance >= 1 && distance <= this->historyAvailable(), static_cast<FwAssertArgType>(distance));
        return this->at(this->historyAvailable() - distance);
    }

    //! Peek at unconsumed byte at `index` (0 = oldest unconsumed). index must be < count()
    U8 peek(FwSizeType index) const {
        FW_ASSERT(index < this->count(), static_cast<FwAssertArgType>(index));
        return this->at(this->m_consumed + index);
    }

    //! Consume `n` bytes without copying them. n must be <= count()
    void pop(FwSizeType n) {
        FW_ASSERT(n <= this->count(), static_cast<FwAssertArgType>(n));
        this->m_consumed += n;
    }

    //! Consume `n` bytes copying them to `out`. n must be <= count()
    void popInto(U8* out, FwSizeType n) {
        FW_ASSERT(out != nullptr);
        FW_ASSERT(n <= this->count(), static_cast<FwAssertArgType>(n));
        const Fw::SerializeStatus status = this->m_buffer.peek(out, n, this->m_consumed);
        FW_ASSERT(status == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(status));
        this->m_consumed += n;
    }

  private:
    U8 at(FwSizeType offset) const {
        U8 value = 0;
        const Fw::SerializeStatus status = this->m_buffer.peek(value, offset);
        FW_ASSERT(status == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(status));
        return value;
    }

    Types::CircularBuffer m_buffer;
    FwSizeType m_consumed;  //!< Bytes at the head of the buffer already handed to the caller, retained as history
};

}  // namespace Update
#endif
