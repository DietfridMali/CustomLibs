#include "resource_handler.h"
#include "dx12context.h"

#include <cstring>

// =================================================================================================

ComPtr<ID3D12Resource> GfxResourceHandler::GetUploadResource(size_t dataSize) {
	ID3D12Device* device = dx12Context.Device();
	if (not device or (dataSize == 0))
		return nullptr;

	D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
	D3D12_RESOURCE_DESC rd{};
	rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rd.Width = dataSize;
	rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
	rd.SampleDesc.Count = 1;
	rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ComPtr<ID3D12Resource> resource;
	if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource))))
		return nullptr;

	Track(resource);
	return resource;
}

// =================================================================================================

