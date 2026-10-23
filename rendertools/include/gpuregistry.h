#pragma once

#include <mutex>
#include <unordered_set>
#include <vector>

class GpuResource {
public:
	virtual ~GpuResource() = default;

	virtual void ReleaseGpu(void) noexcept = 0;

	virtual bool RestoreGpu(void) = 0;
};


class GpuRegistry {
public:
	static void Register(GpuResource* resource)
	{
		std::lock_guard<std::mutex> lock(Lock());
		Resources().insert(resource);
	}

	static void Unregister(GpuResource* resource)
	{
		std::lock_guard<std::mutex> lock(Lock());
		Resources().erase(resource);
	}

	static void ReleaseAll(void)
	{
		for (GpuResource* resource : Snapshot()) {
			if (IsRegistered(resource))
				resource->ReleaseGpu();
		}
	}

	static void RestoreAll(void)
	{
		for (GpuResource* resource : Snapshot()) {
			if (IsRegistered(resource))
				resource->RestoreGpu();
		}
	}

private:
	static std::mutex& Lock(void)
	{
		static std::mutex* lock = new std::mutex();

		return *lock;
	}

	static std::unordered_set<GpuResource*>& Resources(void)
	{
		static std::unordered_set<GpuResource*>* resources = new std::unordered_set<GpuResource*>();

		return *resources;
	}

	static std::vector<GpuResource*> Snapshot(void)
	{
		std::lock_guard<std::mutex> lock(Lock());

		return std::vector<GpuResource*>(Resources().begin(), Resources().end());
	}

	static bool IsRegistered(GpuResource* resource)
	{
		std::lock_guard<std::mutex> lock(Lock());

		return Resources().count(resource) > 0;
	}
};
