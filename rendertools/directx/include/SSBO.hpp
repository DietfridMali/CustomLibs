#pragma once

#include "framework.h"
#include "array.hpp"

// =================================================================================================
// DX12 SSBO stub — same interface as the OGL SSBO so shared code compiles unchanged.
// OGL SSBOs map to UAVs (Unordered Access Views) in DX12.
// Full UAV backend TBD; all operations are currently no-ops.

class BaseSSBO {
public:
    static inline bool IsAvailable{ false };
};

template <typename DATA_T>
class SSBO : public BaseSSBO
{
public:
    AutoArray<DATA_T> m_data;

    SSBO() = default;

    inline DATA_T* Data(void)   { return m_data.Data(); }
    inline int     DataSize(void) { return m_data.DataSize(); }

    bool Create(int size = 0) {
        m_data.Resize(size);
        return true;
    }

    void Destroy(void) {
        m_data.Destroy();
    }

    bool Bind(uint32_t /*bindingPoint*/) { return false; }

    void Release(uint32_t /*bindingPoint*/) {}

    bool Upload(void)   { return false; }
    bool Download(void) { return false; }

    void Clear(DATA_T /*value*/) {}
};

// =================================================================================================
