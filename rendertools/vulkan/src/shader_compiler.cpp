#ifdef LINUX

// On Linux DXC ships its own COM shim via <dxc/WinAdapter.h>: BSTR / IUnknown / IStream /
// HRESULT plus the CComPtr smart pointer. <windows.h> does not exist here.
#include <dxc/WinAdapter.h>

#else

// NOMINMAX must precede <windows.h> — otherwise min/max macros collide with std::array's
// member functions (and any other STL header that uses ::min / ::max).
#define NOMINMAX

// <windows.h> must be the very first include — SDL_syswm.h (pulled in via SDL.h in
// vkcontext.h) defines WIN32_LEAN_AND_MEAN before its own <windows.h>, which strips out the
// OLE/COM portion that DXC requires (BSTR, IStream, IUnknown). Loading <windows.h> first,
// before any SDL/Vulkan header, captures the full COM surface in the header guard.
#include <windows.h>
#include <unknwn.h>
#include <oaidl.h>
#include <objidl.h>

#endif

#include "shader_compiler.h"
#include "vkcontext.h"

#ifndef LINUX
// dxc/dxcapi.h pulls in BSTR / IStream / IUnknown - Windows COM types that live in windows.h.
// Must be included before <dxc/dxcapi.h>.
// WIN32_LEAN_AND_MEAN MUST NOT be set: it strips OLE/COM headers where BSTR/IStream live.
#include <windows.h>
#endif

#include <dxc/dxcapi.h>

#ifndef LINUX
#include <wrl/client.h>
#endif

#include <cstdio>
#include <cstring>

#ifndef LINUX
// On Linux dxcompiler is linked via the Makefile (-ldxcompiler).
#pragma comment(lib, "dxcompiler.lib")
#endif

// DXC's COM-pointer type differs per platform: Microsoft::WRL::ComPtr on Windows,
// CComPtr (from WinAdapter.h) on Linux. The Linux ComPtr below is a thin CComPtr
// subclass that re-exposes the two WRL method names this file uses (GetAddressOf,
// Reset), so the compiler code below stays identical on both platforms.
#ifdef LINUX
template <typename T>
struct ComPtr : public CComPtr<T>
{
    using CComPtr<T>::CComPtr;
    T** GetAddressOf(void) noexcept { return &this->p; }
    void Reset(void) noexcept { this->Release(); }
};
#else
using Microsoft::WRL::ComPtr;
#endif

// =================================================================================================
// shader_compiler implementation

namespace
{
    ComPtr<IDxcUtils>     g_dxcUtils;
    ComPtr<IDxcCompiler3> g_dxcCompiler;
    bool                  g_initialized = false;
}

namespace ShaderCompiler
{

bool Initialize(void) noexcept
{
    if (g_initialized)
        return true;

    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(g_dxcUtils.GetAddressOf()));
    if (FAILED(hr)) {
        fprintf(stderr, "ShaderCompiler::Initialize: DxcCreateInstance(IDxcUtils) failed (0x%08X)\n", (unsigned)hr);
        return false;
    }
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(g_dxcCompiler.GetAddressOf()));
    if (FAILED(hr)) {
        fprintf(stderr, "ShaderCompiler::Initialize: DxcCreateInstance(IDxcCompiler3) failed (0x%08X)\n", (unsigned)hr);
        g_dxcUtils.Reset();
        return false;
    }
    g_initialized = true;
    return true;
}


void Shutdown(void) noexcept
{
    g_dxcCompiler.Reset();
    g_dxcUtils.Reset();
    g_initialized = false;
}


// Helper: convert a UTF-8 narrow string to a UTF-16 wide string (DXC takes wchar_t* args).
static std::wstring ToWide(const char* utf8) noexcept
{
    if (not utf8)
        return std::wstring();
    std::wstring result;
    while (*utf8) {
        // Source files are ASCII-only in this project; widen char-by-char.
        result.push_back(wchar_t(uint8_t(*utf8++)));
    }
    return result;
}


bool CompileHlslToSpirv(const char* hlslSource,
                        const char* entryPoint,
                        const char* targetProfile,
                        const wchar_t* const* extraArgs,
                        uint32_t extraArgsCount,
                        std::vector<uint8_t>& outSpirv,
                        String& outError) noexcept
{
    outSpirv.clear();
    outError = "";

    if (not g_initialized) {
        if (not Initialize()) {
            outError = "ShaderCompiler not initialized";
            return false;
        }
    }
    if ((not hlslSource) or (not entryPoint) or (not targetProfile)) {
        outError = "null argument";
        return false;
    }

    DxcBuffer source { };
    source.Ptr = hlslSource;
    source.Size = std::strlen(hlslSource);
    source.Encoding = DXC_CP_UTF8;

    // Stable storage for the per-call args (entry/target are converted from narrow strings).
    std::wstring entryWide = ToWide(entryPoint);
    std::wstring targetWide = ToWide(targetProfile);

    // Standard args: emit SPIR-V, target Vulkan 1.3, set entry + profile.
    std::vector<const wchar_t*> args;
    args.push_back(L"-spirv");
    args.push_back(L"-fspv-target-env=vulkan1.3");
    args.push_back(L"-E");
    args.push_back(entryWide.c_str());
    args.push_back(L"-T");
    args.push_back(targetWide.c_str());
    for (uint32_t i = 0; i < extraArgsCount; ++i)
        args.push_back(extraArgs[i]);

    ComPtr<IDxcResult> result;
    HRESULT hr = g_dxcCompiler->Compile(&source, args.data(), uint32_t(args.size()),
                                        nullptr, IID_PPV_ARGS(result.GetAddressOf()));
    if (FAILED(hr)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Compile call failed (0x%08X)", (unsigned)hr);
        outError = buf;
        return false;
    }

    HRESULT compileStatus = E_FAIL;
    result->GetStatus(&compileStatus);

    // Always pull errors/warnings out (even on success, for warnings).
    ComPtr<IDxcBlobUtf8> errors;
    if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr))) {
        if (errors and (errors->GetStringLength() > 0)) {
            outError = String(errors->GetStringPointer());
        }
    }

    if (FAILED(compileStatus))
        return false;

    ComPtr<IDxcBlob> objectBlob;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(objectBlob.GetAddressOf()), nullptr))
        or (not objectBlob) or (objectBlob->GetBufferSize() == 0)) {
        if (outError.IsEmpty())
            outError = "no SPIR-V output";
        return false;
    }

    const uint8_t* src = static_cast<const uint8_t*>(objectBlob->GetBufferPointer());
    size_t bytes = size_t(objectBlob->GetBufferSize());
    outSpirv.assign(src, src + bytes);
    return true;
}


VkShaderModule CreateShaderModule(const std::vector<uint8_t>& spirv) noexcept
{
    VkDevice device = vkContext.Device();
    if ((device == VK_NULL_HANDLE) or spirv.empty() or ((spirv.size() % 4) != 0))
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo info { };
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spirv.size();
    info.pCode = reinterpret_cast<const uint32_t*>(spirv.data());

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult res = vkCreateShaderModule(device, &info, nullptr, &module);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "ShaderCompiler::CreateShaderModule: vkCreateShaderModule failed (%d)\n", (int)res);
        return VK_NULL_HANDLE;
    }
    return module;
}


void DestroyShaderModule(VkShaderModule module) noexcept
{
    VkDevice device = vkContext.Device();
    if ((module != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE))
        vkDestroyShaderModule(device, module, nullptr);
}

}  // namespace ShaderCompiler

// =================================================================================================
