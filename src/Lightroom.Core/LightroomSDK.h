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
    
    // 渲染到渲染目标纹理（双缓冲+拷贝策略）
    LIGHTROOM_API bool RenderToTarget(void* renderTargetHandle);
    
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
    
    // 视频相关 API
    // 打开视频文件到渲染目标
    // videoPath: 视频文件路径（UTF-8 编码）
    // 返回是否成功
    LIGHTROOM_API bool OpenVideo(void* renderTargetHandle, const char* videoPath);
    
    // 关闭视频
    LIGHTROOM_API void CloseVideo(void* renderTargetHandle);
    
    // 获取视频元数据
    // outMetadata: 输出元数据结构（定义在 LightroomSDKTypes.h 中）
    // 返回是否成功
    LIGHTROOM_API bool GetVideoMetadata(void* renderTargetHandle, struct VideoMetadata* outMetadata);
    
    // 视频定位
    // timestamp: 时间戳（微秒）
    // 返回是否成功
    LIGHTROOM_API bool SeekVideo(void* renderTargetHandle, int64_t timestamp);
    
    // 视频定位到指定帧
    // frameIndex: 帧索引（从 0 开始）
    // 返回是否成功
    LIGHTROOM_API bool SeekVideoToFrame(void* renderTargetHandle, int64_t frameIndex);
    
    // 读取并渲染视频帧（用于播放循环，双缓冲+拷贝策略）
    // 返回是否成功
    LIGHTROOM_API bool RenderVideoFrame(void* renderTargetHandle);
    
    // 获取当前视频帧索引
    // 返回当前帧索引，如果未打开视频则返回 -1
    LIGHTROOM_API int64_t GetCurrentVideoFrame(void* renderTargetHandle);
    
    // 获取当前视频时间戳（微秒）
    // 返回当前时间戳，如果未打开视频则返回 -1
    LIGHTROOM_API int64_t GetCurrentVideoTimestamp(void* renderTargetHandle);
    
    // 检查文件是否为视频格式
    // filePath: 文件路径（UTF-8 编码）
    // 返回是否为视频格式
    LIGHTROOM_API bool IsVideoFormat(const char* filePath);
    
    // 提取视频第一帧作为缩略图
    // videoPath: 视频文件路径（UTF-8 编码）
    // outWidth: 输出宽度
    // outHeight: 输出高度
    // outData: 输出像素数据（BGRA32格式，由调用者分配内存，建议大小 width*height*4）
    // maxWidth: 最大宽度（用于缩略图，0表示使用原始尺寸）
    // maxHeight: 最大高度（用于缩略图，0表示使用原始尺寸）
    // 返回是否成功，如果成功，outData包含像素数据
    LIGHTROOM_API bool ExtractVideoThumbnail(const char* videoPath, uint32_t* outWidth, uint32_t* outHeight, uint8_t* outData, uint32_t maxWidth, uint32_t maxHeight);
    
    // 导出图片相关 API
    // 从渲染目标导出图片到文件
    // renderTargetHandle: 渲染目标句柄
    // filePath: 输出文件路径（UTF-8 编码）
    // format: 导出格式（"png" 或 "jpeg"）
    // quality: JPEG 质量（1-100，仅对 JPEG 有效，PNG 忽略此参数）
    // 返回是否成功
    LIGHTROOM_API bool ExportImage(void* renderTargetHandle, const char* filePath, const char* format, uint32_t quality);
    
    // 导出视频相关 API
    // 从渲染目标导出视频到文件（MP4格式，H.265编码）
    // renderTargetHandle: 渲染目标句柄
    // filePath: 输出文件路径（UTF-8 编码）
    // progressCallback: 进度回调函数指针（可选，C风格回调）
    //   progress: 0.0 - 1.0
    //   currentFrame: 当前处理的帧索引
    //   totalFrames: 总帧数
    //   userData: 用户数据指针
    // 返回是否成功开始导出（实际导出在后台线程中进行）
    typedef void (*VideoExportProgressCallback)(double progress, int64_t currentFrame, int64_t totalFrames, void* userData);
    LIGHTROOM_API bool ExportVideo(void* renderTargetHandle, const char* filePath, VideoExportProgressCallback progressCallback, void* userData);
    
    // 检查是否正在导出视频
    LIGHTROOM_API bool IsExportingVideo(void* renderTargetHandle);
    
    // 取消视频导出
    LIGHTROOM_API void CancelVideoExport(void* renderTargetHandle);
}

