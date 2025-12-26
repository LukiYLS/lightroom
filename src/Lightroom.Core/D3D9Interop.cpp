#include "D3D9Interop.h"
#include <iostream>

#pragma comment(lib, "d3d9.lib")

namespace LightroomCore {

D3D9Interop::D3D9Interop()
    : m_D3D9Ex(nullptr)
    , m_Device(nullptr)
{
}

D3D9Interop::~D3D9Interop() {
    Shutdown();
}

bool D3D9Interop::Initialize() {
    if (m_Device != nullptr) {
        return true; // 已经初始化
    }

    // 创建 D3D9Ex
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &m_D3D9Ex);
    if (FAILED(hr) || !m_D3D9Ex) {
        return false;
    }

    // 创建 D3D9Ex 设备
    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetDesktopWindow();
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferCount = 0;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dpp.Flags = D3DPRESENTFLAG_VIDEO;  // 重要：启用视频模式，支持共享表面

    hr = m_D3D9Ex->CreateDeviceEx(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        GetDesktopWindow(),
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
        &d3dpp,
        nullptr,
        &m_Device
    );

    if (FAILED(hr)) {
        m_D3D9Ex->Release();
        m_D3D9Ex = nullptr;
        return false;
    }

    return true;
}

void D3D9Interop::Shutdown() {
    if (m_Device) {
        m_Device->Release();
        m_Device = nullptr;
    }
    if (m_D3D9Ex) {
        m_D3D9Ex->Release();
        m_D3D9Ex = nullptr;
    }
}

bool D3D9Interop::CreateSurfaceFromSharedHandle(HANDLE d3d11SharedHandle, uint32_t width, uint32_t height, 
                                                 IDirect3DSurface9** outSurface) {
    if (!m_Device || !d3d11SharedHandle || !outSurface) {
        return false;
    }

    IDirect3DTexture9* tempTexture = nullptr;
    HRESULT hr = m_Device->CreateTexture(
        width, 
        height, 
        1, 
        D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        &tempTexture,
        &d3d11SharedHandle
    );

    if (FAILED(hr)) {
        return false;
    }

    // 获取 Surface 指针传给 WPF
    hr = tempTexture->GetSurfaceLevel(0, outSurface);
    tempTexture->Release();

    return SUCCEEDED(hr);
}

} // namespace LightroomCore











