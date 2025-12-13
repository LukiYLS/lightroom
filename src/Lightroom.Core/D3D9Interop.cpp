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
        std::cerr << "[D3D9Interop] Failed to create D3D9Ex: 0x" << std::hex << hr << std::endl;
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
        std::cerr << "[D3D9Interop] Failed to create D3D9Ex device: 0x" << std::hex << hr << std::endl;
        m_D3D9Ex->Release();
        m_D3D9Ex = nullptr;
        return false;
    }

    std::cout << "[D3D9Interop] D3D9Ex device created successfully" << std::endl;
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

bool D3D9Interop::CreateSharedTextureFromD3D11(HANDLE d3d11SharedHandle, uint32_t width, uint32_t height,
                                                IDirect3DTexture9** outTexture, IDirect3DSurface9** outSurface) {
    if (!m_Device || !d3d11SharedHandle || !outTexture || !outSurface) {
        return false;
    }

    // 从 D3D11 共享句柄创建 D3D9 纹理
    HRESULT hr = m_Device->CreateTexture(
        width,
        height,
        1,
        D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        outTexture,
        &d3d11SharedHandle  // 共享句柄
    );

    if (FAILED(hr)) {
        std::cerr << "[D3D9Interop] Failed to create shared texture from D3D11 handle: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 获取纹理的表面
    hr = (*outTexture)->GetSurfaceLevel(0, outSurface);
    if (FAILED(hr)) {
        std::cerr << "[D3D9Interop] Failed to get surface level: 0x" << std::hex << hr << std::endl;
        (*outTexture)->Release();
        *outTexture = nullptr;
        return false;
    }

    return true;
}

bool D3D9Interop::CreateRenderTargetSurface(uint32_t width, uint32_t height, IDirect3DSurface9** outSurface) {
    if (!m_Device || !outSurface) {
        return false;
    }

    // 首先尝试创建可锁定的渲染目标表面（WPF D3DImage 需要）
    HRESULT hr = m_Device->CreateRenderTarget(
        width,
        height,
        D3DFMT_A8R8G8B8,
        D3DMULTISAMPLE_NONE,
        0,
        TRUE,  // Lockable = TRUE，WPF D3DImage 需要
        outSurface,
        nullptr
    );

    if (FAILED(hr)) {
        // 如果失败，尝试创建纹理然后获取表面
        IDirect3DTexture9* texture = nullptr;
        hr = m_Device->CreateTexture(
            width,
            height,
            1,
            D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            &texture,
            nullptr
        );

        if (SUCCEEDED(hr) && texture) {
            hr = texture->GetSurfaceLevel(0, outSurface);
            texture->Release();
        }

        if (FAILED(hr)) {
            // 最后尝试创建离屏平面表面
            hr = m_Device->CreateOffscreenPlainSurface(
                width,
                height,
                D3DFMT_A8R8G8B8,
                D3DPOOL_DEFAULT,
                outSurface,
                nullptr
            );
        }

        if (FAILED(hr)) {
            std::cerr << "[D3D9Interop] Failed to create render target surface: 0x" << std::hex << hr << std::endl;
            return false;
        }
    }

    return true;
}

bool D3D9Interop::CopySurface(IDirect3DSurface9* srcSurface, IDirect3DSurface9* dstSurface,
                               uint32_t width, uint32_t height) {
    if (!m_Device || !srcSurface || !dstSurface) {
        return false;
    }

    RECT srcRect = { 0, 0, (LONG)width, (LONG)height };
    RECT dstRect = { 0, 0, (LONG)width, (LONG)height };

    HRESULT hr = m_Device->StretchRect(
        srcSurface,
        &srcRect,
        dstSurface,
        &dstRect,
        D3DTEXF_NONE  // 不使用过滤
    );

    if (FAILED(hr)) {
        std::cerr << "[D3D9Interop] StretchRect failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // 强制提交命令
    m_Device->Present(nullptr, nullptr, nullptr, nullptr);
    return true;
}

} // namespace LightroomCore







