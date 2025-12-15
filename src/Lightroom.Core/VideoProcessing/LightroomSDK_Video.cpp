// 视频相关 API 实现
// 此文件包含视频功能的 SDK API 实现

#include "../LightroomSDK.h"
#include "../LightroomSDKTypes.h"
#include "../LightroomSDK_Internal.h"
#include "../D3D9Interop.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "VideoProcessor.h"
#include "VideoPerformanceProfiler.h"
#include "../RenderTargetManager.h"
#include "../RenderGraph.h"
#include "../RenderNodes/ImageAdjustNode.h"
#include "../RenderNodes/ScaleNode.h"
#include <iostream>
#include <string>
#include <algorithm>

using namespace RenderCore;
// 不完全使用 LightroomCore 命名空间，避免与 C API 类型冲突
// using namespace LightroomCore;

// 使用 C API 的类型（避免与 C++ 命名空间冲突）
// 注意：VideoMetadata 和 VideoFormat 在 C API (LightroomSDKTypes.h) 和 C++ 命名空间 (LightroomCore) 中都有定义
// 在函数参数中使用 struct VideoMetadata 来明确使用 C API 的类型

// 外部全局变量（在 LightroomSDK.cpp 中定义）
extern std::shared_ptr<RenderCore::DynamicRHI> g_DynamicRHI;
extern LightroomCore::RenderTargetManager* g_RenderTargetManager;
extern std::unordered_map<void*, std::unique_ptr<RenderTargetData>> g_RenderTargetData;
extern LightroomCore::D3D9Interop* g_D3D9InteropPtr;

bool OpenVideo(void* renderTargetHandle, const char* videoPath) {
    if (!renderTargetHandle || !videoPath || !g_DynamicRHI) {
        return false;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return false;
    }
    
    auto& data = it->second;
    if (!data) {
        return false;
    }
    
    try {
        // 关闭之前的视频（如果有）
        if (data->VideoProcessor) {
            data->VideoProcessor->CloseVideo();
        }
        
        // 创建新的视频处理器
        data->VideoProcessor = std::make_unique<LightroomCore::VideoProcessor>(g_DynamicRHI);
        
        // 转换路径
        int pathLen = MultiByteToWideChar(CP_UTF8, 0, videoPath, -1, nullptr, 0);
        if (pathLen <= 0) {
            return false;
        }
        
        std::wstring wVideoPath(pathLen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, videoPath, -1, &wVideoPath[0], pathLen);
        
        // 打开视频
        if (!data->VideoProcessor->OpenVideo(wVideoPath)) {
            data->VideoProcessor.reset();
            return false;
        }
        
        // 获取视频元数据并设置渲染图
        const LightroomCore::VideoMetadata* metadata = data->VideoProcessor->GetMetadata();
        if (!metadata) {
            data->VideoProcessor->CloseVideo();
            data->VideoProcessor.reset();
            return false;
        }
        
        // 清除旧的渲染图
        data->RenderGraph->Clear();
        
        // 添加图像调整节点
        auto adjustNode = std::make_shared<LightroomCore::ImageAdjustNode>(g_DynamicRHI);
        ImageAdjustParams defaultParams;
        memset(&defaultParams, 0, sizeof(ImageAdjustParams));
        defaultParams.temperature = 5500.0f;
        adjustNode->SetAdjustParams(defaultParams);
        data->RenderGraph->AddNode(adjustNode);
        
        // 添加缩放节点
        auto scaleNode = std::make_shared<LightroomCore::ScaleNode>(g_DynamicRHI);
        scaleNode->SetInputImageSize(metadata->width, metadata->height);
        data->RenderGraph->AddNode(scaleNode);
        
        data->bIsVideo = true;
        data->bHasImage = true;
        data->ImageFormat = LightroomCore::ImageFormat::Unknown; // 视频不使用 ImageFormat
        
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

void CloseVideo(void* renderTargetHandle) {
    if (!renderTargetHandle) {
        return;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return;
    }
    
    auto& data = it->second;
    if (data && data->VideoProcessor) {
        data->VideoProcessor->CloseVideo();
        data->VideoProcessor.reset();
        data->bIsVideo = false;
        data->bHasImage = false;
    }
}

bool GetVideoMetadata(void* renderTargetHandle, VideoMetadata* outMetadata) {
    if (!renderTargetHandle || !outMetadata) {
        return false;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return false;
    }
    
    auto& data = it->second;
    if (!data || !data->VideoProcessor || !data->bIsVideo) {
        return false;
    }
    
    const LightroomCore::VideoMetadata* cppMetadata = data->VideoProcessor->GetMetadata();
    if (!cppMetadata) {
        return false;
    }
    
    // 转换 C++ 枚举到 C 枚举
    outMetadata->width = cppMetadata->width;
    outMetadata->height = cppMetadata->height;
    outMetadata->frameRate = cppMetadata->frameRate;
    outMetadata->totalFrames = cppMetadata->totalFrames;
    outMetadata->duration = cppMetadata->duration;
    outMetadata->hasAudio = cppMetadata->hasAudio;
    
    switch (cppMetadata->format) {
        case LightroomCore::VideoFormat::MP4:
            outMetadata->format = VideoFormat_MP4;
            break;
        case LightroomCore::VideoFormat::MOV:
            outMetadata->format = VideoFormat_MOV;
            break;
        case LightroomCore::VideoFormat::AVI:
            outMetadata->format = VideoFormat_AVI;
            break;
        case LightroomCore::VideoFormat::MKV:
            outMetadata->format = VideoFormat_MKV;
            break;
        default:
            outMetadata->format = VideoFormat_Unknown;
            break;
    }
    
    return true;
}

bool SeekVideo(void* renderTargetHandle, int64_t timestamp) {
    if (!renderTargetHandle) {
        return false;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return false;
    }
    
    auto& data = it->second;
    if (!data || !data->VideoProcessor || !data->bIsVideo) {
        return false;
    }
    
    return data->VideoProcessor->Seek(timestamp);
}

bool SeekVideoToFrame(void* renderTargetHandle, int64_t frameIndex) {
    if (!renderTargetHandle) {
        return false;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return false;
    }
    
    auto& data = it->second;
    if (!data || !data->VideoProcessor || !data->bIsVideo) {
        return false;
    }
    
    return data->VideoProcessor->SeekToFrame(frameIndex);
}

bool RenderVideoFrame(void* renderTargetHandle) {
    if (!renderTargetHandle || !g_RenderTargetManager) {
        return false;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return false;
    }
    
    auto& data = it->second;
    if (!data || !data->VideoProcessor || !data->bIsVideo || !data->RenderGraph) {
        return false;
    }
    
    try {
        using namespace LightroomCore;
        ScopedTimer totalTimer("RenderVideoFrame_Total");
        
        // 读取下一帧
        std::shared_ptr<RenderCore::RHITexture2D> frameTexture;
        {
            ScopedTimer getFrameTimer("RenderVideoFrame_GetNextFrame");
            frameTexture = data->VideoProcessor->GetNextFrame();
        }
        if (!frameTexture) {
            return false;
        }
        
        // 更新 ImageTexture（用于渲染图）
        data->ImageTexture = frameTexture;
        
        // 获取输出纹理
        using RenderTargetInfo = LightroomCore::RenderTargetManager::RenderTargetInfo;
        RenderTargetInfo* renderTargetInfo = g_RenderTargetManager->GetRenderTargetInfo(renderTargetHandle);
        if (!renderTargetInfo || !renderTargetInfo->RHIRenderTarget) {
            return false;
        }
        
        std::shared_ptr<RenderCore::RHITexture2D> outputTexture = g_RenderTargetManager->GetRHITexture(renderTargetHandle);
        if (!outputTexture) {
            return false;
        }
        
        // 执行渲染图
        {
            ScopedTimer renderGraphTimer("RenderVideoFrame_RenderGraph");
            if (!data->RenderGraph->Execute(
                    frameTexture,
                    outputTexture,
                    renderTargetInfo->Width,
                    renderTargetInfo->Height)) {
                return false;
            }
        }
        
        // 刷新命令
        {
            ScopedTimer flushTimer("RenderVideoFrame_FlushCommands");
            auto commandContext = g_DynamicRHI->GetDefaultCommandContext();
            if (commandContext) {
                commandContext->FlushCommands();
            }
            
            // 确保 D3D11 命令已提交到 GPU
            RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(g_DynamicRHI.get());
            if (d3d11RHI) {
                ID3D11DeviceContext* d3d11Context = d3d11RHI->GetDeviceContext();
                if (d3d11Context) {
                    d3d11Context->Flush();
                }
            }
        }
        
        // 使用 GPU 拷贝从 D3D11 共享纹理拷贝到 D3D9 表面（与 RenderToTarget 中的逻辑一致）
        // 注意：renderTargetInfo 已经在上面定义过了，直接使用
        if (renderTargetInfo && renderTargetInfo->D3D9SharedSurface && renderTargetInfo->D3D9Surface && g_D3D9InteropPtr) {
            ScopedTimer copyTimer("RenderVideoFrame_D3D9Copy");
            g_D3D9InteropPtr->CopySurface(
                renderTargetInfo->D3D9SharedSurface,
                renderTargetInfo->D3D9Surface,
                renderTargetInfo->Width,
                renderTargetInfo->Height
            );
        }
        
        // Print statistics every 100 frames
        static int frameCount = 0;
        frameCount++;
        if (frameCount % 100 == 0) {
            VideoPerformanceProfiler::GetInstance().PrintStatistics(100);
            VideoPerformanceProfiler::GetInstance().ClearOldRecords(200);
        }
        
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

int64_t GetCurrentVideoFrame(void* renderTargetHandle) {
    if (!renderTargetHandle) {
        return -1;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return -1;
    }
    
    auto& data = it->second;
    if (!data || !data->VideoProcessor || !data->bIsVideo) {
        return -1;
    }
    
    return data->VideoProcessor->GetCurrentFrameIndex();
}

int64_t GetCurrentVideoTimestamp(void* renderTargetHandle) {
    if (!renderTargetHandle) {
        return -1;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return -1;
    }
    
    auto& data = it->second;
    if (!data || !data->VideoProcessor || !data->bIsVideo) {
        return -1;
    }
    
    return data->VideoProcessor->GetCurrentTimestamp();
}

bool IsVideoFormat(const char* filePath) {
    if (!filePath) {
        return false;
    }
    
    // 转换路径
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, filePath, -1, nullptr, 0);
    if (pathLen <= 0) {
        return false;
    }
    
    std::wstring wFilePath(pathLen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, filePath, -1, &wFilePath[0], pathLen);
    
    // 检查扩展名
    std::wstring ext = wFilePath.substr(wFilePath.find_last_of(L".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    
    return ext == L"mp4" || ext == L"mov" || ext == L"avi" || 
           ext == L"mkv" || ext == L"m4v" || ext == L"flv" ||
           ext == L"webm" || ext == L"3gp";
}

