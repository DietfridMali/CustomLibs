#pragma once

#include "vkframework.h"
#include "vkupload.h"
#include "base_readtarget.h"
#include "gpuregistry.h"

class GfxReadTarget
	: public BaseReadTarget,
	  public GpuResource {
private:
	VkStagingBuffer m_readback;

	bool IsComplete(void)
	noexcept;

public:
	GfxReadTarget(String name = "")
		: BaseReadTarget(name)
	{
		GpuRegistry::Register(this);
	}

	virtual ~GfxReadTarget() {
		GpuRegistry::Unregister(this);
		Destroy();
	}

	virtual void ReleaseGpu(void)
	noexcept override
	{
		Destroy();
	}

	virtual bool RestoreGpu(void) override
	{
		return true;
	}

	virtual bool Request(RenderTarget& renderTarget, int bufferIndex, int arraySlice = 0) override;

	virtual State Poll(void) override;

	virtual const uint8_t* Data(void) override;

	virtual void Release(void) override;

	virtual void Destroy(void) override;

	bool Allocate(size_t size);

	bool Submit(size_t size, int width, int height, uint64_t frame, int slot);

	inline VkBuffer Buffer(void) const
	noexcept
	{
		return m_readback.buffer;
	}
};
