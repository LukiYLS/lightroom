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
#include "../ImageProcessing/ImageExporter.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

using namespace RenderCore;


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
        data->VideoFilePath = std::string(videoPath);  // 保存视频文件路径，用于导出
        
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
        
        // 【双缓冲+拷贝策略】获取Back Buffer进行渲染
        auto outputTexture = g_RenderTargetManager->AcquireNextRenderBuffer(renderTargetHandle);
        if (!outputTexture) {
            return false;
        }
        
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
        
        // 获取渲染目标信息
        using RenderTargetInfo = LightroomCore::RenderTargetManager::RenderTargetInfo;
        RenderTargetInfo* renderTargetInfo = g_RenderTargetManager->GetRenderTargetInfo(renderTargetHandle);
        if (!renderTargetInfo) {
            return false;
        }
        
        // 执行渲染图到Back Buffer
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

        // Print statistics every 100 frames
        static int frameCount = 0;
        frameCount++;
        if (frameCount % 100 == 0) {
            VideoPerformanceProfiler::GetInstance().PrintStatistics(100);
            VideoPerformanceProfiler::GetInstance().ClearOldRecords(200);
        }
        
        // 【双缓冲+拷贝策略】将Back Buffer的内容复制到Front Buffer
        return g_RenderTargetManager->PresentBackBuffer(renderTargetHandle);
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

bool ExtractVideoThumbnail(const char* videoPath, uint32_t* outWidth, uint32_t* outHeight, uint8_t* outData, uint32_t maxWidth, uint32_t maxHeight) {
    if (!videoPath || !outWidth || !outHeight || !outData) {
        return false;
    }
    
    if (!g_DynamicRHI) {
        return false;
    }
    
    try {
        // 转换路径
        int pathLen = MultiByteToWideChar(CP_UTF8, 0, videoPath, -1, nullptr, 0);
        if (pathLen <= 0) {
            return false;
        }
        
        std::wstring wVideoPath(pathLen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, videoPath, -1, &wVideoPath[0], pathLen);
        
        // 创建临时的VideoProcessor
        auto videoProcessor = std::make_unique<LightroomCore::VideoProcessor>(g_DynamicRHI);
        if (!videoProcessor->OpenVideo(wVideoPath)) {
            return false;
        }
        
        // 获取视频元数据
        const LightroomCore::VideoMetadata* metadata = videoProcessor->GetMetadata();
        if (!metadata) {
            videoProcessor->CloseVideo();
            return false;
        }
        
        // 定位到第一帧
        if (!videoProcessor->SeekToFrame(0)) {
            videoProcessor->CloseVideo();
            return false;
        }
        
        // 读取第一帧
        auto frameTexture = videoProcessor->GetCurrentFrame();
        if (!frameTexture) {
            videoProcessor->CloseVideo();
            return false;
        }
        
        // 获取原始尺寸
        auto originalSize = frameTexture->GetSize();
        uint32_t originalWidth = originalSize.x;
        uint32_t originalHeight = originalSize.y;
        
        // 计算输出尺寸（保持宽高比）
        uint32_t outputWidth = originalWidth;
        uint32_t outputHeight = originalHeight;
        
        if (maxWidth > 0 && maxHeight > 0) {
            float scaleX = static_cast<float>(maxWidth) / static_cast<float>(originalWidth);
            float scaleY = static_cast<float>(maxHeight) / static_cast<float>(originalHeight);
            float scale = std::min(scaleX, scaleY);
            
            outputWidth = static_cast<uint32_t>(originalWidth * scale);
            outputHeight = static_cast<uint32_t>(originalHeight * scale);
        }
        
        // 创建输出纹理（如果需要缩放）
        std::shared_ptr<RenderCore::RHITexture2D> outputTexture = frameTexture;
        if (outputWidth != originalWidth || outputHeight != originalHeight) {
            // 需要缩放，创建缩放后的纹理
            outputTexture = g_DynamicRHI->RHICreateTexture2D(
                RenderCore::EPixelFormat::PF_B8G8R8A8,
                RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
                outputWidth,
                outputHeight,
                1
            );
            
            if (!outputTexture) {
                videoProcessor->CloseVideo();
                return false;
            }
            
            // 使用ScaleNode进行缩放
            LightroomCore::ScaleNode scaleNode(g_DynamicRHI);
            scaleNode.SetInputImageSize(originalWidth, originalHeight);
            scaleNode.SetZoomParams(1.0, 0.0, 0.0);
            
            if (!scaleNode.Execute(frameTexture, outputTexture, outputWidth, outputHeight)) {
                videoProcessor->CloseVideo();
                return false;
            }
        }
        
        // 刷新命令
        auto commandContext = g_DynamicRHI->GetDefaultCommandContext();
        if (commandContext) {
            commandContext->FlushCommands();
        }
        
        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(g_DynamicRHI.get());
        if (d3d11RHI) {
            ID3D11DeviceContext* d3d11Context = d3d11RHI->GetDeviceContext();
            if (d3d11Context) {
                d3d11Context->Flush();
            }
        }
        
        // 读取纹理数据
        auto d3d11Texture = std::dynamic_pointer_cast<RenderCore::D3D11Texture2D>(outputTexture);
        if (!d3d11Texture) {
            videoProcessor->CloseVideo();
            return false;
        }
        
        ID3D11Texture2D* nativeTexture = d3d11Texture->GetNativeTex();
        if (!nativeTexture) {
            videoProcessor->CloseVideo();
            return false;
        }
        
        // 使用ImageExporter读取像素数据
        LightroomCore::ImageExporter exporter(g_DynamicRHI);
        uint32_t realWidth, realHeight, stride;
        std::vector<uint8_t> imageData;
        
        if (!exporter.ReadD3D11TextureData(nativeTexture, realWidth, realHeight, imageData, stride)) {
            videoProcessor->CloseVideo();
            return false;
        }
        
        // 验证读取的尺寸是否匹配
        if (realWidth != outputWidth || realHeight != outputHeight) {
            videoProcessor->CloseVideo();
            return false;
        }
        
        // 检查输出缓冲区大小
        uint32_t requiredSize = outputWidth * outputHeight * 4;
        if (imageData.size() < requiredSize) {
            videoProcessor->CloseVideo();
            return false;
        }
        
        // 复制数据到输出缓冲区
        *outWidth = outputWidth;
        *outHeight = outputHeight;
        memcpy(outData, imageData.data(), requiredSize);
        
        videoProcessor->CloseVideo();
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

