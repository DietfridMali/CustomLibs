#pragma once

#include "std_defines.h"
#include "string.hpp"

class RenderTarget;

class BaseReadTarget {
public:
    enum class State : uint8_t {
        Idle,
        Pending,
        Ready
    };

protected:
    String      m_name{ "" };
    size_t      m_size{ 0 };
    size_t      m_capacity{ 0 };
    int         m_width{ 0 };
    int         m_height{ 0 };
    State       m_state{ State::Idle };
    uint64_t    m_requestFrame{ 0 };
    int         m_requestSlot{ 0 };

    inline void SetRequest(size_t size, int width, int height, uint64_t frame, int slot) noexcept {
        m_size = size;
        m_width = width;
        m_height = height;
        m_requestFrame = frame;
        m_requestSlot = slot;
        m_state = State::Pending;
    }

public:
    BaseReadTarget(String name = "") : m_name(name) {}

    virtual ~BaseReadTarget() = default;

    virtual bool Request(RenderTarget& renderTarget, int bufferIndex, int arraySlice = 0) = 0;

    virtual State Poll(void) = 0;

    virtual const uint8_t* Data(void) = 0;

    virtual void Release(void) = 0;

    virtual void Destroy(void) = 0;

    inline State GetState(void) const noexcept {
        return m_state;
    }

    inline bool IsIdle(void) const noexcept {
        return m_state == State::Idle;
    }

    inline bool IsPending(void) const noexcept {
        return m_state == State::Pending;
    }

    inline bool IsReady(void) const noexcept {
        return m_state == State::Ready;
    }

    inline size_t Size(void) const noexcept {
        return m_size;
    }

    inline int Width(void) const noexcept {
        return m_width;
    }

    inline int Height(void) const noexcept {
        return m_height;
    }

    inline uint64_t RequestFrame(void) const noexcept {
        return m_requestFrame;
    }

    inline const String& Name(void) const noexcept {
        return m_name;
    }
};
