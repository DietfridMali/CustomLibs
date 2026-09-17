#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#   define NOMINMAX
#endif
#ifndef NOGDI
#   define NOGDI    // suppress GDI declarations (Rectangle, Ellipse, …) — not needed for DX12
#endif
#include <windows.h>

// DirectX 12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Link DirectX libraries
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
