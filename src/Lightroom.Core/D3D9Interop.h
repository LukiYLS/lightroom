#pragma once

#include <d3d9.h>
#include <dxgi.h>
#include <cstdint>

namespace LightroomCore {

class D3D9Interop {
public:
    D3D9Interop();
    ~D3D9Interop();

    bool Initialize();

    void Shutdown();

    // 从 D3D11 共享句柄打开并获取 D3D9 Surface（用于 WPF D3DImage）
    // 返回的 IDirect3DSurface9* 需要由调用者管理生命周期
    bool CreateSurfaceFromSharedHandle(HANDLE d3d11SharedHandle, uint32_t width, uint32_t height, 
                                       IDirect3DSurface9** outSurface);

    // 获取 D3D9Ex 设备（用于直接访问，谨慎使用）
    IDirect3DDevice9Ex* GetDevice() const { return m_Device; }

    // 检查是否已初始化
    bool IsInitialized() const { return m_Device != nullptr; }

private:
    IDirect3D9Ex* m_D3D9Ex;
    IDirect3DDevice9Ex* m_Device;
};

} // namespace LightroomCore












