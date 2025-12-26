#pragma once

#include "d3d11rhi/DynamicRHI.h"
#include "d3d11rhi/D3D11RHI.h"
#include "D3D9Interop.h"
#include <memory>
#include <unordered_map>
#include <cstdint>

namespace LightroomCore {

class RenderTargetManager {
public:
    struct RenderTargetInfo {
        // RHI 渲染目标（用于 RHI 接口渲染）
        std::shared_ptr<RenderCore::RHIRenderTarget> RHIRenderTarget;
        
        // RHI 纹理（从 RHIRenderTarget 获取，用于渲染图输出）
        std::shared_ptr<RenderCore::RHITexture2D> RHITexture;
        
        // D3D11 共享纹理（直接创建，用于 D3D9 互操作）
        Microsoft::WRL::ComPtr<ID3D11Texture2D> D3D11SharedTexture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> D3D11SharedRTV;
        
        // D3D11 共享句柄（用于 D3D9 互操作）
        HANDLE D3D11SharedHandle;
        
        // D3D9 资源（用于 WPF D3DImage）
        IDirect3DSurface9* D3D9SharedSurface;  // 从 D3D11 共享纹理打开的 Surface（直接用于 WPF）
        
        uint32_t Width;
        uint32_t Height;
        
        RenderTargetInfo()
            : D3D11SharedHandle(nullptr)
            , D3D9SharedSurface(nullptr)
            , Width(0)
            , Height(0)
        {
        }
        
        ~RenderTargetInfo() {
            if (D3D9SharedSurface) {
                D3D9SharedSurface->Release();
                D3D9SharedSurface = nullptr;
            }
        }
    };

    RenderTargetManager(std::shared_ptr<RenderCore::DynamicRHI> rhi, D3D9Interop* d3d9Interop);
    ~RenderTargetManager();

    // 创建渲染目标
    void* CreateRenderTarget(uint32_t width, uint32_t height);

    // 销毁渲染目标
    void DestroyRenderTarget(void* handle);

    // 获取渲染目标信息
    RenderTargetInfo* GetRenderTargetInfo(void* handle);

    // 获取 D3D9 共享句柄（用于 WPF D3DImage.SetBackBuffer）
    void* GetD3D9SharedHandle(void* handle);

    // 调整渲染目标大小
    bool ResizeRenderTarget(void* handle, uint32_t width, uint32_t height);

    // 获取 D3D11 共享句柄（内部使用）
    HANDLE GetD3D11SharedHandle(void* handle);

    // 获取渲染目标的 RHI 纹理（用于渲染）
    std::shared_ptr<RenderCore::RHITexture2D> GetRHITexture(void* handle);

private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    D3D9Interop* m_D3D9Interop;
    std::unordered_map<void*, std::unique_ptr<RenderTargetInfo>> m_RenderTargets;

    // 从 RHI 纹理获取 D3D11 共享句柄
    HANDLE GetSharedHandleFromRHITexture(std::shared_ptr<RenderCore::RHITexture2D> texture);
};

} // namespace LightroomCore

