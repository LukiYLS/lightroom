#pragma once

// 内部共享的结构和变量声明
// 用于在 LightroomSDK.cpp 和 LightroomSDK_Video.cpp 之间共享

#include "d3d11rhi/DynamicRHI.h"
#include "d3d11rhi/RHITexture2D.h"
#include "RenderGraph.h"
#include "ImageProcessing/ImageLoader.h"
#include "ImageProcessing/RAWImageInfo.h"
#include "VideoProcessing/VideoProcessor.h"
#include <memory>
#include <unordered_map>

namespace RenderCore {
    class DynamicRHI;
    class RHITexture2D;
}

namespace LightroomCore {
    class RenderGraph;
    class VideoProcessor;
}

// 渲染目标关联的渲染图（每个渲染目标可以有独立的渲染图）
struct RenderTargetData {
    std::shared_ptr<RenderCore::RHITexture2D> ImageTexture;  // 加载的图片纹理
    std::unique_ptr<LightroomCore::RenderGraph> RenderGraph;     // 渲染图
    bool bHasImage;
    LightroomCore::ImageFormat ImageFormat;      // 图片格式（Standard 或 RAW）
    std::unique_ptr<LightroomCore::RAWImageInfo> RAWInfo;  // RAW 信息（仅在 RAW 格式时有效）
    
    // 视频相关
    std::unique_ptr<LightroomCore::VideoProcessor> VideoProcessor;
    bool bIsVideo;
    
    RenderTargetData() : bHasImage(false), ImageFormat(LightroomCore::ImageFormat::Unknown), bIsVideo(false) {}
};

// 前向声明
namespace LightroomCore {
    class RenderTargetManager;
}

// 全局变量声明（在 LightroomSDK.cpp 中定义）
extern std::shared_ptr<RenderCore::DynamicRHI> g_DynamicRHI;
extern LightroomCore::RenderTargetManager* g_RenderTargetManager;
extern std::unordered_map<void*, std::unique_ptr<RenderTargetData>> g_RenderTargetData;

// D3D9 互操作前向声明（在 LightroomCore 命名空间中）
namespace LightroomCore {
    class D3D9Interop;
}
extern LightroomCore::D3D9Interop* g_D3D9InteropPtr;

