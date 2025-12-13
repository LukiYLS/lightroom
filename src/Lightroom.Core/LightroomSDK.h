#pragma once

#include <cstdint>
#include "LightroomSDKTypes.h"

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
    
    // 设置渲染目标的缩放参数
    // zoomLevel: 缩放级别（1.0 = 100%, 2.0 = 200%, 0.5 = 50%）
    // panX, panY: 平移偏移（归一化坐标，范围 -1.0 到 1.0）
    LIGHTROOM_API void SetRenderTargetZoom(void* renderTargetHandle, double zoomLevel, double panX, double panY);
    
    // RAW 格式相关 API
    // 检查文件是否为 RAW 格式
    LIGHTROOM_API bool IsRAWFormat(const char* imagePath);
    
    // 获取 RAW 图片元数据（仅在加载 RAW 文件后有效）
    // 结构体定义在 LightroomSDKTypes.h 中
    LIGHTROOM_API bool GetRAWMetadata(void* renderTargetHandle, RAWImageMetadata* outMetadata);
    
    // 设置图像调整参数（通用，适用于 RAW 和标准图片）
    // 结构体定义在 LightroomSDKTypes.h 中
    LIGHTROOM_API void SetImageAdjustParams(void* renderTargetHandle, const ImageAdjustParams* params);
    
    // 重置图像调整参数为默认值
    LIGHTROOM_API void ResetImageAdjustParams(void* renderTargetHandle);
    
    // 获取直方图数据（从渲染后的纹理读取）
    // outHistogram: 输出数组，大小为 256 * 4 (R, G, B, Luminance)，每个通道 256 个值
    // 返回是否成功
    LIGHTROOM_API bool GetHistogramData(void* renderTargetHandle, uint32_t* outHistogram);
    
    // 滤镜相关 API
    // 加载 LUT 滤镜到渲染目标
    // lutSize: LUT 尺寸（例如 32 表示 32x32x32 的 3D LUT）
    // lutData: LUT 数据，格式为 RGB float 数组，大小为 lutSize^3 * 3，值范围 [0, 1]
    // 返回是否成功
    LIGHTROOM_API bool LoadFilterLUT(void* renderTargetHandle, uint32_t lutSize, const float* lutData);
    
    // 从 .cube 文件加载滤镜
    // filePath: .cube 文件路径（UTF-8 编码）
    // 返回是否成功
    LIGHTROOM_API bool LoadFilterLUTFromFile(void* renderTargetHandle, const char* filePath);
    
    // 设置滤镜强度（0.0 = 无效果，1.0 = 完全应用）
    LIGHTROOM_API void SetFilterIntensity(void* renderTargetHandle, float intensity);
    
    // 移除滤镜（从渲染图中移除 FilterNode）
    LIGHTROOM_API void RemoveFilter(void* renderTargetHandle);
}

