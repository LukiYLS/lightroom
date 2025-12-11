#pragma once

#include <cstdint>

#ifdef LIGHTROOM_CORE_EXPORTS
#define LIGHTROOM_API __declspec(dllexport)
#else
#define LIGHTROOM_API __declspec(dllimport)
#endif

extern "C" {
    // 初始化SDK
    LIGHTROOM_API bool InitSDK();

    // 释放SDK资源
    LIGHTROOM_API void ShutdownSDK();

    // 模拟图像处理（实际项目中这里会调用GPU加速逻辑）
    LIGHTROOM_API bool ProcessImage(const char* inputPath, const char* outputPath, float brightness, float contrast);
    
    // 获取SDK版本（重命名避免与 Windows API 冲突）
    LIGHTROOM_API int GetSDKVersion();

    // D3D11 渲染接口 - 图片编辑区渲染目标
    // 创建渲染目标纹理（用于图片编辑区，支持共享以便在 WPF 中显示）
    LIGHTROOM_API void* CreateRenderTarget(uint32_t width, uint32_t height);
    
    // 释放渲染目标
    LIGHTROOM_API void DestroyRenderTarget(void* renderTargetHandle);
    
    // 获取共享纹理句柄（用于 WPF D3DImage.SetBackBuffer）
    LIGHTROOM_API void* GetRenderTargetSharedHandle(void* renderTargetHandle);
    
    // 加载图片到渲染目标
    LIGHTROOM_API bool LoadImageToTarget(void* renderTargetHandle, const char* imagePath);
    
    // 渲染到渲染目标纹理（现在用于触发刷新，不再需要颜色参数）
    LIGHTROOM_API void RenderToTarget(void* renderTargetHandle);
    
    // 调整渲染目标大小
    LIGHTROOM_API void ResizeRenderTarget(void* renderTargetHandle, uint32_t width, uint32_t height);
}

