#pragma once

#include <d3d9.h>
#include <cstdint>

namespace LightroomCore {

class D3D9Interop {
public:
    D3D9Interop();
    ~D3D9Interop();

    bool Initialize();

    void Shutdown();

    // 从 D3D11 共享句柄创建 D3D9 共享纹理
    // 返回的 IDirect3DTexture9* 和 IDirect3DSurface9* 需要由调用者管理生命周期
    bool CreateSharedTextureFromD3D11(HANDLE d3d11SharedHandle, uint32_t width, uint32_t height, 
                                      IDirect3DTexture9** outTexture, IDirect3DSurface9** outSurface);

    // 创建用于 WPF D3DImage 的共享渲染目标表面
    // 返回的 IDirect3DSurface9* 需要由调用者管理生命周期
    bool CreateRenderTargetSurface(uint32_t width, uint32_t height, IDirect3DSurface9** outSurface);

    // 使用 GPU 拷贝（StretchRect）从源表面拷贝到目标表面
    bool CopySurface(IDirect3DSurface9* srcSurface, IDirect3DSurface9* dstSurface, 
                     uint32_t width, uint32_t height);

    // 获取 D3D9Ex 设备（用于直接访问，谨慎使用）
    IDirect3DDevice9Ex* GetDevice() const { return m_Device; }

    // 检查是否已初始化
    bool IsInitialized() const { return m_Device != nullptr; }

private:
    IDirect3D9Ex* m_D3D9Ex;
    IDirect3DDevice9Ex* m_Device;
};

} // namespace LightroomCore


