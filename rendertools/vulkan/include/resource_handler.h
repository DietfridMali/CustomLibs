#pragma once

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "array.hpp"
#include "string.hpp"
#include "gfxstates.h"
#include "shader.h"
#include "dx12framework.h"
#include <functional>

// =================================================================================================

class GfxResourceHandler
	: public BaseSingleton<GfxResourceHandler>
{
private:
	uint32_t							m_frameIndex{ 0 };
	AutoArray<ComPtr<ID3D12Resource>>	m_frameResources[2];  // resources kept alive until after ExecuteAll

public:
	ComPtr<ID3D12Resource> GetUploadResource(const char* name, size_t dataSize);

	inline void Cleanup(void) noexcept {
		static uint64_t callCount = 0;
		m_frameIndex = (m_frameIndex + 1) % 2;
#ifdef _DEBUG
		++callCount;
		AutoArray<ComPtr<ID3D12Resource>>& a = m_frameResources[m_frameIndex];
		for (int i = a.Length(); i > 0; ) {
			char name[256] = {};
			UINT size = sizeof(name);
			--i;
			a[i]->GetPrivateData(WKPDID_D3DDebugObjectName, &size, name);
			a[i] = nullptr;
		}
#endif
		m_frameResources[m_frameIndex].Clear();
	}

	inline void Track(ComPtr<ID3D12Resource> resource) noexcept {
		if (resource)
			m_frameResources[m_frameIndex].Push(std::move(resource));
	}
};

#define gfxResourceHandler GfxResourceHandler::Instance()

// =================================================================================================

