#include "RenderTargetManager.h"
#include "d3d11rhi/D3D11RHI.h"
#include "d3d11rhi/D3D11Texture2D.h"
#include "d3d11rhi/D3D11RenderTarget.h"
#include "d3d11rhi/RHIRenderTarget.h"
#include "d3d11rhi/Common.h"
#include <dxgi.h>
#include <wrl/client.h>
#include <iostream>

using namespace RenderCore;

namespace LightroomCore {

RenderTargetManager::RenderTargetManager(std::shared_ptr<RenderCore::DynamicRHI> rhi, D3D9Interop* d3d9Interop)
    : m_RHI(rhi)
    , m_D3D9Interop(d3d9Interop)
{
}

RenderTargetManager::~RenderTargetManager() {
    m_RenderTargets.clear();
}

void* RenderTargetManager::CreateRenderTarget(uint32_t width, uint32_t height) {
    if (!m_RHI || !m_D3D9Interop || !m_D3D9Interop->IsInitialized()) {
        std::cerr << "[RenderTargetManager] RHI or D3D9Interop not initialized" << std::endl;
        return nullptr;
    }

    try {
        auto info = std::make_unique<RenderTargetInfo>();
        info->Width = width;
        info->Height = height;

        // 1. 使用 RHI 接口创建共享渲染目标纹理
        // 注意：需要创建支持共享的纹理，但 RHI 接口可能不直接支持 MiscFlags
        // 这里需要访问底层 D3D11 实现来设置共享标志
        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
        if (!d3d11RHI) {
            std::cerr << "[RenderTargetManager] Failed to cast to D3D11DynamicRHI" << std::endl;
            return nullptr;
        }

        // 获取底层 D3D11 设备（用于创建共享纹理）
        ID3D11Device* d3d11Device = d3d11RHI->GetDevice();
        if (!d3d11Device) {
            std::cerr << "[RenderTargetManager] Failed to get D3D11 device" << std::endl;
            return nullptr;
        }

        // 1. 首先创建支持共享的 D3D11 纹理（用于 D3D9 互操作）
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;  // 关键：启用共享

        HRESULT hr = d3d11Device->CreateTexture2D(&desc, nullptr, info->D3D11SharedTexture.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[RenderTargetManager] Failed to create D3D11 shared texture: 0x" << std::hex << hr << std::endl;
            return nullptr;
        }

        // 创建共享纹理的 RTV
        hr = d3d11Device->CreateRenderTargetView(info->D3D11SharedTexture.Get(), nullptr, info->D3D11SharedRTV.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[RenderTargetManager] Failed to create D3D11 shared RTV: 0x" << std::hex << hr << std::endl;
            return nullptr;
        }

        // 获取共享句柄
        Microsoft::WRL::ComPtr<IDXGIResource> dxgiResource;
        hr = info->D3D11SharedTexture.As(&dxgiResource);
        if (FAILED(hr)) {
            std::cerr << "[RenderTargetManager] Failed to query IDXGIResource: 0x" << std::hex << hr << std::endl;
            return nullptr;
        }

        hr = dxgiResource->GetSharedHandle(&info->D3D11SharedHandle);
        if (FAILED(hr) || !info->D3D11SharedHandle) {
            std::cerr << "[RenderTargetManager] Failed to get D3D11 shared handle: 0x" << std::hex << hr << std::endl;
            return nullptr;
        }

        // 2. 将共享纹理包装为 RHI 渲染目标（优化：直接使用共享纹理，消除冗余拷贝）
        // 直接从共享纹理创建 RHI 渲染目标，这样渲染就直接到共享纹理上
        RenderCore::D3D11RenderTarget* d3d11RenderTarget = new RenderCore::D3D11RenderTarget(d3d11RHI);
        if (!d3d11RenderTarget->CreateFromExistingTexture(
                info->D3D11SharedTexture.Get(),
                RenderCore::EPixelFormat::PF_B8G8R8A8)) {
            std::cerr << "[RenderTargetManager] Failed to create RHI render target from shared texture" << std::endl;
            delete d3d11RenderTarget;
            return nullptr;
        }

        // 包装为 shared_ptr
        info->RHIRenderTarget = std::shared_ptr<RenderCore::RHIRenderTarget>(d3d11RenderTarget);

        // 从 RHIRenderTarget 获取纹理
        info->RHITexture = info->RHIRenderTarget->GetTex();
        if (!info->RHITexture) {
            std::cerr << "[RenderTargetManager] Failed to get RHI texture from render target" << std::endl;
            return nullptr;
        }

        // 2. 在 D3D9 中打开共享纹理
        if (!m_D3D9Interop->CreateSharedTextureFromD3D11(
                info->D3D11SharedHandle, width, height,
                &info->D3D9SharedTexture, &info->D3D9SharedSurface)) {
            std::cerr << "[RenderTargetManager] Failed to create D3D9 shared texture" << std::endl;
            return nullptr;
        }

        // 3. 创建用于 WPF D3DImage 的表面
        if (!m_D3D9Interop->CreateRenderTargetSurface(width, height, &info->D3D9Surface)) {
            std::cerr << "[RenderTargetManager] Failed to create D3D9 render target surface" << std::endl;
            return nullptr;
        }

        void* handle = info.get();
        m_RenderTargets[handle] = std::move(info);
        std::cout << "[RenderTargetManager] Created render target: " << width << "x" << height << std::endl;
        return handle;
    }
    catch (const std::exception& e) {
        std::cerr << "[RenderTargetManager] Exception: " << e.what() << std::endl;
        return nullptr;
    }
}

void RenderTargetManager::DestroyRenderTarget(void* handle) {
    if (!handle) {
        return;
    }
    m_RenderTargets.erase(handle);
}

RenderTargetManager::RenderTargetInfo* RenderTargetManager::GetRenderTargetInfo(void* handle) {
    auto it = m_RenderTargets.find(handle);
    if (it != m_RenderTargets.end()) {
        return it->second.get();
    }
    return nullptr;
}

void* RenderTargetManager::GetD3D9SharedHandle(void* handle) {
    auto* info = GetRenderTargetInfo(handle);
    if (info && info->D3D9Surface) {
        // 返回 D3D9 表面的指针（WPF D3DImage 需要）
        return info->D3D9Surface;
    }
    return nullptr;
}

bool RenderTargetManager::ResizeRenderTarget(void* handle, uint32_t width, uint32_t height) {
    auto* info = GetRenderTargetInfo(handle);
    if (!info) {
        return false;
    }

    // 如果尺寸相同，不需要调整
    if (info->Width == width && info->Height == height) {
        return true;
    }

    // 销毁旧的资源
    DestroyRenderTarget(handle);

    // 创建新的渲染目标
    void* newHandle = CreateRenderTarget(width, height);
    if (!newHandle) {
        return false;
    }

    // 更新句柄映射（如果句柄改变了）
    if (newHandle != handle) {
        auto it = m_RenderTargets.find(newHandle);
        if (it != m_RenderTargets.end()) {
            auto infoPtr = std::move(it->second);
            m_RenderTargets.erase(it);
            m_RenderTargets[handle] = std::move(infoPtr);
            m_RenderTargets.erase(newHandle);
        }
    }

    return true;
}

HANDLE RenderTargetManager::GetD3D11SharedHandle(void* handle) {
    auto* info = GetRenderTargetInfo(handle);
    return info ? info->D3D11SharedHandle : nullptr;
}

HANDLE RenderTargetManager::GetSharedHandleFromRHITexture(std::shared_ptr<RenderCore::RHITexture2D> texture) {
    // TODO: 实现从 RHI 纹理获取共享句柄的逻辑
    // 这可能需要扩展 RHI 接口或访问 D3D11 实现细节
    return nullptr;
}

std::shared_ptr<RenderCore::RHITexture2D> RenderTargetManager::GetRHITexture(void* handle) {
    auto* info = GetRenderTargetInfo(handle);
    if (!info || !info->RHIRenderTarget) {
        return nullptr;
    }
    
    // 从 RHIRenderTarget 获取纹理
    return info->RHIRenderTarget->GetTex();
}

} // namespace LightroomCore

