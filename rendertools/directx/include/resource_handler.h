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
	ComPtr<ID3D12Resource> GetUploadResource(size_t dataSize);

	inline void Cleanup(void) noexcept {
		m_frameIndex = (m_frameIndex + 1) % 2;
		m_frameResources[m_frameIndex].Clear();
	}

	inline void Track(ComPtr<ID3D12Resource> resource) noexcept {
		if (resource)
			m_frameResources[m_frameIndex].Push(std::move(resource));
	}
};

#define gfxResourceHandler GfxResourceHandler::Instance()

// =================================================================================================

