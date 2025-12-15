#include "LightroomSDK.h"
#include "LightroomSDK_Internal.h"
#include "D3D9Interop.h"
#include "ImageProcessing/ImageProcessor.h"
#include "ImageProcessing/ImageLoader.h"
#include "ImageProcessing/RAWImageInfo.h"
#include "RenderTargetManager.h"
#include "RenderGraph.h"
#include "RenderNodes/RenderNode.h"
#include "RenderNodes/ScaleNode.h"
#include "RenderNodes/ImageAdjustNode.h"
#include "RenderNodes/FilterNode.h"
#include "RenderNodes/ComputeNode.h"
#include "RenderNodes/RAWDemosaicComputeNode.h"
#include "ImageProcessing/RAWImageLoader.h"
#include "VideoProcessing/VideoProcessor.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>
#include <cstring>

#include "d3d11rhi/Common.h"
#include "d3d11rhi/DynamicRHI.h"
#include "d3d11rhi/D3D11RHI.h"
#include "d3d11rhi/D3D11Texture2D.h"
#include "d3d11rhi/RHIRenderTarget.h"
#include <d3d11.h>
#include <algorithm>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")

using namespace RenderCore;
using namespace LightroomCore;

// 全局状态
// 注意：g_DynamicRHI 和 g_RenderTargetData 在 LightroomSDK_Internal.h 中声明为 extern
// 这里定义它们，供 LightroomSDK_Video.cpp 使用
std::shared_ptr<RenderCore::DynamicRHI> g_DynamicRHI = nullptr;
std::unordered_map<void*, std::unique_ptr<RenderTargetData>> g_RenderTargetData;

// 其他全局状态
// 注意：g_D3D9Interop 需要被 LightroomSDK_Video.cpp 访问，所以不能是 static
std::unique_ptr<LightroomCore::D3D9Interop> g_D3D9Interop = nullptr;
LightroomCore::D3D9Interop* g_D3D9InteropPtr = nullptr;  // 视频 API 需要访问（指向 g_D3D9Interop）

static std::unique_ptr<ImageProcessor> g_ImageProcessor = nullptr;
static std::unique_ptr<RenderTargetManager> g_RenderTargetManagerPtr = nullptr;  // 生命周期管理
LightroomCore::RenderTargetManager* g_RenderTargetManager = nullptr;  // 视频 API 需要访问（指向 g_RenderTargetManagerPtr）

bool InitSDK() {
    try {
        // 0. 初始化 COM（WIC 需要）
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            return false;
        }
        
        // 1. 创建 D3D11 RHI
        g_DynamicRHI = PlatformCreateDynamicRHI(RHIAPIType::E_D3D11);
        if (!g_DynamicRHI) {
            return false;
        }
        g_DynamicRHI->Init();
        
        // 2. 初始化 D3D9 互操作
        g_D3D9Interop = std::make_unique<LightroomCore::D3D9Interop>();
        g_D3D9InteropPtr = g_D3D9Interop.get();  // 设置指针供视频 API 使用
        if (!g_D3D9Interop->Initialize()) {
            return false;
        }
        
        // 3. 创建图片处理器
        g_ImageProcessor = std::make_unique<ImageProcessor>(g_DynamicRHI);
        
        // 4. 创建渲染目标管理器
        g_RenderTargetManagerPtr = std::make_unique<RenderTargetManager>(g_DynamicRHI, g_D3D9Interop.get());
        g_RenderTargetManager = g_RenderTargetManagerPtr.get();
        // 注意：renderTargetManager 的生命周期由 g_ImageProcessor 管理，这里只是保存指针
        
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

void ShutdownSDK() {
    g_D3D9InteropPtr = nullptr;  // 清除指针
    
    // 清理所有渲染目标数据
    g_RenderTargetData.clear();
    
    // 清理管理器
    g_RenderTargetManager = nullptr;
    g_RenderTargetManagerPtr.reset();
    g_ImageProcessor.reset();
    
    // 清理 D3D9 互操作
    if (g_D3D9Interop) {
        g_D3D9Interop->Shutdown();
        g_D3D9Interop.reset();
    }
    
    // 关闭 RHI
    if (g_DynamicRHI) {
        g_DynamicRHI->Shutdown();
        g_DynamicRHI = nullptr;
    }
    
    ReleasePlatformModule();
    
    CoUninitialize();
}

int GetSDKVersion() {
    return 100; // v1.0.0
}

void* CreateRenderTarget(uint32_t width, uint32_t height) {
    if (!g_RenderTargetManager) {
        return nullptr;
    }
    
    // 创建渲染目标
    void* handle = g_RenderTargetManager->CreateRenderTarget(width, height);
    if (!handle) {
        return nullptr;
    }
    
    // 创建关联的渲染图（初始为空，加载图片时再添加节点）
    auto renderGraph = std::make_unique<RenderGraph>(g_DynamicRHI);
    
    // 存储渲染目标数据
    auto data = std::make_unique<RenderTargetData>();
    data->RenderGraph = std::move(renderGraph);
    g_RenderTargetData[handle] = std::move(data);
    
    return handle;
}

void DestroyRenderTarget(void* renderTargetHandle) {
    if (!renderTargetHandle) return;
    
    // 清理渲染目标数据
    g_RenderTargetData.erase(renderTargetHandle);
    
    // 销毁渲染目标
    if (g_RenderTargetManager) {
        g_RenderTargetManager->DestroyRenderTarget(renderTargetHandle);
    }
}

void* GetRenderTargetSharedHandle(void* renderTargetHandle) {
    if (!g_RenderTargetManager) {
        return nullptr;
    }
    
    // 返回 D3D9 表面的指针（WPF D3DImage 使用）
    return g_RenderTargetManager->GetD3D9SharedHandle(renderTargetHandle);
}

bool LoadImageToTarget(void* renderTargetHandle, const char* imagePath) {
    if (!renderTargetHandle || !g_ImageProcessor || !imagePath) {
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
        // 先检查文件格式（不加载）
        std::wstring wpath;
        int pathLen = MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, nullptr, 0);
        if (pathLen > 0) {
            wpath.resize(pathLen);
            MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, &wpath[0], pathLen);
            if (!wpath.empty() && wpath.back() == L'\0') {
                wpath.pop_back();
            }
        }
        
        // 检查是否为 RAW 格式
        bool isRAW = g_ImageProcessor->IsRAWFormat(wpath);
        
        // 根据图片格式和尺寸，决定使用哪个节点
        auto* renderTargetInfo = g_RenderTargetManager->GetRenderTargetInfo(renderTargetHandle);
        if (renderTargetInfo) {
            uint32_t imageWidth, imageHeight;
            
            // 清除旧的渲染图
            data->RenderGraph->Clear();
            
            // 如果是 RAW 格式，使用 Compute Shader 进行去马赛克
            if (isRAW) {
                // 重新加载 RAW 数据为原始 Bayer 纹理
                RAWImageLoader* rawLoader = g_ImageProcessor->GetRAWLoader();
                if (rawLoader) {
                    auto bayerTexture = rawLoader->LoadRAWDataToTexture(wpath, g_DynamicRHI);
                    if (bayerTexture) {
                        // 更新图像纹理为 Bayer 数据
                        data->ImageTexture = bayerTexture;
                        data->bHasImage = true;
                        data->ImageFormat = LightroomCore::ImageFormat::RAW;
                        
                        // 获取 RAW 信息
                        const LightroomCore::RAWImageInfo& rawInfo = rawLoader->GetRAWInfo();
                        data->RAWInfo = std::make_unique<LightroomCore::RAWImageInfo>(rawInfo);
                        
                        // 添加 RAW 去马赛克 Compute Node
                        auto demosaicNode = std::make_shared<RAWDemosaicComputeNode>(g_DynamicRHI);
                        // 设置 Bayer pattern（从 RAW 信息中获取）
                        demosaicNode->SetBayerPattern(data->RAWInfo->bayerPattern);
                        // 设置白平衡（从 RAW 信息中获取）
                        demosaicNode->SetWhiteBalance(
                            data->RAWInfo->whiteBalance[0],  // R
                            data->RAWInfo->whiteBalance[1],  // G1
                            data->RAWInfo->whiteBalance[2]   // B
                        );
                        data->RenderGraph->AddComputeNode(demosaicNode);
                        
                        // 更新图像尺寸为去马赛克后的尺寸（与原始尺寸相同）
                        imageWidth = data->RAWInfo->width;
                        imageHeight = data->RAWInfo->height;
                    } else {
                        // 如果加载 Bayer 纹理失败，回退到使用标准加载方式
                        data->ImageTexture = g_ImageProcessor->LoadImageFromFile(imagePath);
                        if (!data->ImageTexture) {
                            return false;
                        }
                        data->bHasImage = true;
                        data->ImageFormat = g_ImageProcessor->GetLastImageFormat();
                        g_ImageProcessor->GetLastImageSize(imageWidth, imageHeight);
                        data->RAWInfo.reset();
                    }
                } else {
                    // 如果无法获取 RAW 加载器，使用标准加载方式
                    data->ImageTexture = g_ImageProcessor->LoadImageFromFile(imagePath);
                    if (!data->ImageTexture) {
                        return false;
                    }
                    data->bHasImage = true;
                    data->ImageFormat = g_ImageProcessor->GetLastImageFormat();
                    g_ImageProcessor->GetLastImageSize(imageWidth, imageHeight);
                    data->RAWInfo.reset();
                }
            } else {
                // 非 RAW 格式，使用标准加载方式
                data->ImageTexture = g_ImageProcessor->LoadImageFromFile(imagePath);
                if (!data->ImageTexture) {
                    return false;
                }
                data->bHasImage = true;
                data->ImageFormat = g_ImageProcessor->GetLastImageFormat();
                g_ImageProcessor->GetLastImageSize(imageWidth, imageHeight);
                data->RAWInfo.reset();
            }
            
            // 添加通用的图像调整节点（适用于 RAW 和标准图片）
            auto adjustNode = std::make_shared<ImageAdjustNode>(g_DynamicRHI);
            ImageAdjustParams defaultParams;
            memset(&defaultParams, 0, sizeof(ImageAdjustParams));
            defaultParams.temperature = 5500.0f;  // 默认日光色温
            adjustNode->SetAdjustParams(defaultParams);
            data->RenderGraph->AddNode(adjustNode);
            
            // 总是添加缩放节点以支持缩放和平移功能
            auto scaleNode = std::make_shared<ScaleNode>(g_DynamicRHI);
            scaleNode->SetInputImageSize(imageWidth, imageHeight);
            data->RenderGraph->AddNode(scaleNode);
            
        }
        
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

void RenderToTarget(void* renderTargetHandle) {
    if (!renderTargetHandle || !g_RenderTargetManager) {
        return;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return;
    }
    
    auto& data = it->second;
    if (!data || !data->RenderGraph) {
        return;
    }
    
    try {
        auto* renderTargetInfo = g_RenderTargetManager->GetRenderTargetInfo(renderTargetHandle);
        if (!renderTargetInfo || !renderTargetInfo->RHIRenderTarget) {
            return;
        }
        
        // 如果是视频，使用视频渲染逻辑
        if (data->bIsVideo && data->VideoProcessor) {
            // 获取当前帧（不推进到下一帧，只是渲染当前帧）
            // 如果还没有帧，尝试获取第一帧
            auto frameTexture = data->VideoProcessor->GetCurrentFrame();
            if (!frameTexture) {
                // 如果当前帧不存在，尝试获取下一帧（这会解码第一帧）
                frameTexture = data->VideoProcessor->GetNextFrame();
            }
            
            if (frameTexture) {
                data->ImageTexture = frameTexture;
                
                // 获取输出纹理
                auto outputTexture = g_RenderTargetManager->GetRHITexture(renderTargetHandle);
                if (outputTexture && data->RenderGraph) {
                    // 执行渲染图（这会应用所有调整参数）
                    if (data->RenderGraph->Execute(
                            frameTexture,
                            outputTexture,
                            renderTargetInfo->Width,
                            renderTargetInfo->Height)) {
                        // 刷新命令
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
                }
            }
            
            // 视频渲染后也需要复制到 D3D9 表面（与图片渲染逻辑一致）
            // 注意：不要 return，继续执行后面的 D3D9 表面复制逻辑
        }
        
        // 如果有图片，执行渲染图
        if (data->bHasImage && data->ImageTexture) {
            // 获取输出纹理（从 RenderTargetManager）
            auto outputTexture = g_RenderTargetManager->GetRHITexture(renderTargetHandle);
            if (!outputTexture) {
                return;
            }
            
            // 执行渲染图
            if (!data->RenderGraph->Execute(
                    data->ImageTexture,  // 输入：加载的图片纹理
                    outputTexture,       // 输出：渲染目标纹理
                    renderTargetInfo->Width,
                    renderTargetInfo->Height)) {
                return;
            }
            
            // 渲染完成后，确保命令执行完成
            // 优化：由于 RHIRenderTarget 直接使用 D3D11SharedTexture，无需拷贝
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
        } else {
            // 没有图片，清除渲染目标为红色（测试用）
            auto commandContext = g_DynamicRHI->GetDefaultCommandContext();
            if (commandContext && renderTargetInfo->RHIRenderTarget) {
                // 1. 先设置渲染目标（使用 RHIRenderTarget）
                commandContext->SetRenderTarget(renderTargetInfo->RHIRenderTarget, 0);
                
                // 2. 设置视口
                commandContext->SetViewPort(0, 0, renderTargetInfo->Width, renderTargetInfo->Height);
                
                // 3. 清除渲染目标为红色（使用 RHIRenderTarget）
                commandContext->Clear(renderTargetInfo->RHIRenderTarget, core::FLinearColor(0, 0, 0, 1));
                
                // 4. 确保命令执行完成
                commandContext->FlushCommands();
                
                // 5. 优化：由于 RHIRenderTarget 直接使用 D3D11SharedTexture，无需拷贝
                // 只需确保 D3D11 命令已提交到 GPU
                RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(g_DynamicRHI.get());
                if (d3d11RHI) {
                    ID3D11DeviceContext* d3d11Context = d3d11RHI->GetDeviceContext();
                    if (d3d11Context) {
                        d3d11Context->Flush();
                    }
                }
            }
        }
        
        // 使用 GPU 拷贝从 D3D11 共享纹理拷贝到 D3D9 表面
        if (renderTargetInfo->D3D9SharedSurface && renderTargetInfo->D3D9Surface && g_D3D9InteropPtr) {
            g_D3D9InteropPtr->CopySurface(
                renderTargetInfo->D3D9SharedSurface,
                renderTargetInfo->D3D9Surface,
                renderTargetInfo->Width,
                renderTargetInfo->Height
            );
        }
    }
    catch (const std::exception& e) {
    }
}

void ResizeRenderTarget(void* renderTargetHandle, uint32_t width, uint32_t height) {
    if (!renderTargetHandle || !g_RenderTargetManager) {
        return;
    }
    
    // 调整渲染目标大小
    g_RenderTargetManager->ResizeRenderTarget(renderTargetHandle, width, height);
}

void SetRenderTargetZoom(void* renderTargetHandle, double zoomLevel, double panX, double panY) {
    if (!renderTargetHandle) {
        return;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return;
    }
    
    auto& data = it->second;
    if (!data || !data->RenderGraph) {
        return;
    }
    
    // 查找 ScaleNode 并设置缩放参数
    for (size_t i = 0; i < data->RenderGraph->GetNodeCount(); ++i) {
        auto node = data->RenderGraph->GetNode(i);
        if (node && strcmp(node->GetName(), "Scale") == 0) {
            // 转换为 ScaleNode
            auto scaleNode = std::dynamic_pointer_cast<ScaleNode>(node);
            if (scaleNode) {
                scaleNode->SetZoomParams(zoomLevel, panX, panY);
                
                // 如果已加载图片，设置输入图片尺寸
                if (data->bHasImage && data->ImageTexture) {
                    if (data->bIsVideo) {
                        // 视频
                        const LightroomCore::VideoMetadata* metadata = data->VideoProcessor->GetMetadata();
                        if (metadata) {
                            scaleNode->SetInputImageSize(metadata->width, metadata->height);
                        }
                    } else {
                        // 图片
                        uint32_t imageWidth, imageHeight;
                        g_ImageProcessor->GetLastImageSize(imageWidth, imageHeight);
                        scaleNode->SetInputImageSize(imageWidth, imageHeight);
                    }
                }
            }
            break;
        }
    }
}

bool IsRAWFormat(const char* imagePath) {
    if (!imagePath || !g_ImageProcessor) {
        return false;
    }
    
    // 转换路径为宽字符
    std::wstring wpath;
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, nullptr, 0);
    if (pathLen > 0) {
        wpath.resize(pathLen);
        MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, &wpath[0], pathLen);
        if (!wpath.empty() && wpath.back() == L'\0') {
            wpath.pop_back();
        }
    } else {
        pathLen = MultiByteToWideChar(CP_ACP, 0, imagePath, -1, nullptr, 0);
        if (pathLen > 0) {
            wpath.resize(pathLen);
            MultiByteToWideChar(CP_ACP, 0, imagePath, -1, &wpath[0], pathLen);
            if (!wpath.empty() && wpath.back() == L'\0') {
                wpath.pop_back();
            }
        } else {
            return false;
        }
    }
    
    return g_ImageProcessor->IsRAWFormat(wpath);
}

bool GetRAWMetadata(void* renderTargetHandle, RAWImageMetadata* outMetadata) {
    if (!renderTargetHandle || !outMetadata) {
        return false;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return false;
    }
    
    auto& data = it->second;
    if (!data || data->ImageFormat != LightroomCore::ImageFormat::RAW || !data->RAWInfo) {
        return false;
    }
    
    // 填充元数据
    outMetadata->width = data->RAWInfo->width;
    outMetadata->height = data->RAWInfo->height;
    outMetadata->bitsPerPixel = data->RAWInfo->bitsPerPixel;
    outMetadata->bayerPattern = data->RAWInfo->bayerPattern;
    outMetadata->whiteBalance[0] = data->RAWInfo->whiteBalance[0];
    outMetadata->whiteBalance[1] = data->RAWInfo->whiteBalance[1];
    outMetadata->whiteBalance[2] = data->RAWInfo->whiteBalance[2];
    outMetadata->whiteBalance[3] = data->RAWInfo->whiteBalance[3];
    outMetadata->iso = data->RAWInfo->iso;
    outMetadata->aperture = data->RAWInfo->aperture;
    outMetadata->shutterSpeed = data->RAWInfo->shutterSpeed;
    outMetadata->focalLength = data->RAWInfo->focalLength;
    
    // 复制字符串（确保不溢出）
    size_t cameraModelLen = data->RAWInfo->cameraModel.length();
    size_t cameraModelSize = sizeof(outMetadata->cameraModel);
    size_t cameraModelCopyLen = (cameraModelLen < cameraModelSize - 1) ? cameraModelLen : cameraModelSize - 1;
    strncpy_s(outMetadata->cameraModel, cameraModelSize, 
              data->RAWInfo->cameraModel.c_str(), cameraModelCopyLen);
    outMetadata->cameraModel[cameraModelCopyLen] = '\0';
    
    size_t lensModelLen = data->RAWInfo->lensModel.length();
    size_t lensModelSize = sizeof(outMetadata->lensModel);
    size_t lensModelCopyLen = (lensModelLen < lensModelSize - 1) ? lensModelLen : lensModelSize - 1;
    strncpy_s(outMetadata->lensModel, lensModelSize, 
              data->RAWInfo->lensModel.c_str(), lensModelCopyLen);
    outMetadata->lensModel[lensModelCopyLen] = '\0';
    
    return true;
}

void SetImageAdjustParams(void* renderTargetHandle, const ImageAdjustParams* params) {
    if (!renderTargetHandle || !params) {
        return;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return;
    }
    
    auto& data = it->second;
    if (!data || !data->RenderGraph) {
        return;
    }
    
    // 查找 ImageAdjustNode 并设置参数
    for (size_t i = 0; i < data->RenderGraph->GetNodeCount(); ++i) {
        auto node = data->RenderGraph->GetNode(i);
        if (node && strcmp(node->GetName(), "ImageAdjust") == 0) {
            auto adjustNode = std::dynamic_pointer_cast<ImageAdjustNode>(node);
            if (adjustNode) {
                adjustNode->SetAdjustParams(*params);
            }
            break;
        }
    }
}

void ResetImageAdjustParams(void* renderTargetHandle) {
    if (!renderTargetHandle) {
        return;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end()) {
        return;
    }
    
    auto& data = it->second;
    if (!data || !data->RenderGraph) {
        return;
    }
    
    // 查找 ImageAdjustNode 并重置为默认值
    for (size_t i = 0; i < data->RenderGraph->GetNodeCount(); ++i) {
        auto node = data->RenderGraph->GetNode(i);
        if (node && strcmp(node->GetName(), "ImageAdjust") == 0) {
            auto adjustNode = std::dynamic_pointer_cast<ImageAdjustNode>(node);
            if (adjustNode) {
                ImageAdjustParams defaultParams;
                memset(&defaultParams, 0, sizeof(ImageAdjustParams));
                defaultParams.temperature = 5500.0f;  // 默认日光色温
                adjustNode->SetAdjustParams(defaultParams);
            }
            break;
        }
    }
}

bool GetHistogramData(void* renderTargetHandle, uint32_t* outHistogram) {
    if (!renderTargetHandle || !outHistogram) {
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
    
    // 检查是否有图片或视频内容（用于直方图计算）
    // 对于视频，bIsVideo为true；对于图片，bHasImage为true
    if (!data->bHasImage && !data->bIsVideo) {
        return false;
    }
    
    try {
        // 获取渲染后的输出纹理（这是实际显示的内容，应该用于直方图计算）
        auto* renderTargetInfo = g_RenderTargetManager->GetRenderTargetInfo(renderTargetHandle);
        if (!renderTargetInfo || !renderTargetInfo->RHIRenderTarget) {
            return false;
        }
        
        auto outputTexture = g_RenderTargetManager->GetRHITexture(renderTargetHandle);
        if (!outputTexture) {
            return false;
        }
        
        // 获取 D3D11 纹理
        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(g_DynamicRHI.get());
        if (!d3d11RHI) {
            return false;
        }
        
        auto d3d11Texture = std::dynamic_pointer_cast<RenderCore::D3D11Texture2D>(outputTexture);
        if (!d3d11Texture) {
            return false;
        }
        
        ID3D11Texture2D* nativeTex = d3d11Texture->GetNativeTex();
        if (!nativeTex) {
            // 尝试直接从RenderTargetInfo获取D3D11SharedTexture
            auto* renderTargetInfo = g_RenderTargetManager->GetRenderTargetInfo(renderTargetHandle);
            if (renderTargetInfo && renderTargetInfo->D3D11SharedTexture) {
                nativeTex = renderTargetInfo->D3D11SharedTexture.Get();
            } else {
                return false;
            }
        }
        
        // 获取纹理描述
        D3D11_TEXTURE2D_DESC texDesc;
        nativeTex->GetDesc(&texDesc);
        
        // 检查纹理格式，必须是BGRA格式（4字节每像素）
        if (texDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && texDesc.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS) {
            return false;
        }
        
        // 确保纹理尺寸有效
        if (texDesc.Width == 0 || texDesc.Height == 0) {
            return false;
        }
        
        // 创建 staging texture 用于读取数据
        ID3D11Device* device = d3d11RHI->GetDevice();
        ID3D11DeviceContext* context = d3d11RHI->GetDeviceContext();
        
        D3D11_TEXTURE2D_DESC stagingDesc = texDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // 确保staging texture是BGRA格式
        
        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
        HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[SDK] GetHistogramData: Failed to create staging texture: 0x" 
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }
        
        // 从 GPU 拷贝到 staging texture
        context->CopyResource(stagingTexture.Get(), nativeTex);
        
        // 确保GPU命令执行完成
        context->Flush();
        
        // 映射并读取数据
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            std::cerr << "[SDK] GetHistogramData: Failed to map staging texture: 0x" 
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }
        
        // 初始化直方图数组（256 bins * 4 channels: R, G, B, Luminance）
        memset(outHistogram, 0, 256 * 4 * sizeof(uint32_t));
        
        // 采样像素（为了性能，只采样部分像素）
        uint32_t sampleStep = std::max(1u, std::max(texDesc.Width / 256, texDesc.Height / 256));
        
        const uint8_t* srcData = static_cast<const uint8_t*>(mapped.pData);
        uint32_t bytesPerPixel = 4;  // BGRA = 4 bytes per pixel
        
        for (uint32_t y = 0; y < texDesc.Height; y += sampleStep) {
            for (uint32_t x = 0; x < texDesc.Width; x += sampleStep) {
                uint32_t pixelOffset = y * mapped.RowPitch + x * bytesPerPixel;
                
                // 确保不越界
                if (pixelOffset + bytesPerPixel > mapped.RowPitch * texDesc.Height) {
                    continue;
                }
                
                uint8_t b = srcData[pixelOffset + 0];
                uint8_t g = srcData[pixelOffset + 1];
                uint8_t r = srcData[pixelOffset + 2];
                
                // 计算亮度 (使用 Rec. 709 标准)
                float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                uint8_t lum = static_cast<uint8_t>(luminance);
                
                // 更新直方图
                outHistogram[r]++;           // R channel
                outHistogram[256 + g]++;    // G channel
                outHistogram[512 + b]++;    // B channel
                outHistogram[768 + lum]++;  // Luminance
            }
        }
        
        context->Unmap(stagingTexture.Get(), 0);
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[SDK] Exception getting histogram: " << e.what() << std::endl;
        return false;
    }
}

// 辅助函数：查找渲染图中的 FilterNode
static std::shared_ptr<FilterNode> FindFilterNode(void* renderTargetHandle) {
    if (!renderTargetHandle) {
        return nullptr;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end() || !it->second || !it->second->RenderGraph) {
        return nullptr;
    }
    
    // 在渲染图中查找 FilterNode
    for (size_t i = 0; i < it->second->RenderGraph->GetNodeCount(); ++i) {
        auto node = it->second->RenderGraph->GetNode(i);
        if (node && strcmp(node->GetName(), "Filter") == 0) {
            return std::dynamic_pointer_cast<FilterNode>(node);
        }
    }
    
    return nullptr;
}

bool LoadFilterLUT(void* renderTargetHandle, uint32_t lutSize, const float* lutData) {
    if (!renderTargetHandle || !lutData || lutSize == 0 || lutSize > 256) {
        return false;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end() || !it->second || !it->second->RenderGraph) {
        return false;
    }
    
    try {
        auto& data = it->second;
        auto& renderGraph = data->RenderGraph;
        
        // 查找是否已存在 FilterNode
        std::shared_ptr<FilterNode> filterNode = FindFilterNode(renderTargetHandle);
        
        if (!filterNode) {
            // 创建新的 FilterNode
            filterNode = std::make_shared<FilterNode>(g_DynamicRHI);
            
            // 查找 ImageAdjustNode 的位置，在其后插入 FilterNode
            // 渲染图顺序：ImageAdjust -> Filter -> Scale
            size_t insertIndex = 0;
            for (size_t i = 0; i < renderGraph->GetNodeCount(); ++i) {
                auto node = renderGraph->GetNode(i);
                if (node && strcmp(node->GetName(), "ImageAdjust") == 0) {
                    insertIndex = i + 1;
                    break;
                }
            }
            
            // 由于 RenderGraph 没有 InsertNode 方法，我们需要重建渲染图
            // 或者使用一个更简单的方法：在 ImageAdjust 之后、Scale 之前插入
            // 暂时先清除并重建（这不是最优方案，但可以工作）
            // TODO: 优化 RenderGraph 以支持插入节点
            
            // 临时方案：保存现有节点，清除，然后重新添加
            std::vector<std::shared_ptr<RenderNode>> savedNodes;
            for (size_t i = 0; i < renderGraph->GetNodeCount(); ++i) {
                savedNodes.push_back(renderGraph->GetNode(i));
            }
            
            renderGraph->Clear();
            
            // 重新添加节点，在 ImageAdjust 之后插入 FilterNode
            for (size_t i = 0; i < savedNodes.size(); ++i) {
                renderGraph->AddNode(savedNodes[i]);
                if (savedNodes[i] && strcmp(savedNodes[i]->GetName(), "ImageAdjust") == 0) {
                    renderGraph->AddNode(filterNode);
                }
            }
        }
        
        // 加载 LUT
        if (!filterNode->LoadLUT(lutSize, lutData)) {
            return false;
        }
        
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

bool LoadFilterLUTFromFile(void* renderTargetHandle, const char* filePath) {
    if (!renderTargetHandle || !filePath) {
        return false;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end() || !it->second || !it->second->RenderGraph) {
        return false;
    }
    
    try {
        auto& data = it->second;
        auto& renderGraph = data->RenderGraph;
        
        // 查找是否已存在 FilterNode
        std::shared_ptr<FilterNode> filterNode = FindFilterNode(renderTargetHandle);
        
        if (!filterNode) {
            // 创建新的 FilterNode
            filterNode = std::make_shared<FilterNode>(g_DynamicRHI);
            
            // 保存现有节点，清除，然后重新添加
            std::vector<std::shared_ptr<RenderNode>> savedNodes;
            for (size_t i = 0; i < renderGraph->GetNodeCount(); ++i) {
                savedNodes.push_back(renderGraph->GetNode(i));
            }
            
            renderGraph->Clear();
            
            // 重新添加节点，在 ImageAdjust 之后插入 FilterNode
            for (size_t i = 0; i < savedNodes.size(); ++i) {
                renderGraph->AddNode(savedNodes[i]);
                if (savedNodes[i] && strcmp(savedNodes[i]->GetName(), "ImageAdjust") == 0) {
                    renderGraph->AddNode(filterNode);
                }
            }
        }
        
        // 从文件加载 LUT
        if (!filterNode->LoadLUTFromFile(filePath)) {
            return false;
        }
        
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

void SetFilterIntensity(void* renderTargetHandle, float intensity) {
    if (!renderTargetHandle) {
        return;
    }
    
    auto filterNode = FindFilterNode(renderTargetHandle);
    if (filterNode) {
        filterNode->SetIntensity(intensity);
    }
}

void RemoveFilter(void* renderTargetHandle) {
    if (!renderTargetHandle) {
        return;
    }
    
    auto it = g_RenderTargetData.find(renderTargetHandle);
    if (it == g_RenderTargetData.end() || !it->second || !it->second->RenderGraph) {
        return;
    }
    
    try {
        auto& renderGraph = it->second->RenderGraph;
        
        // 查找并移除 FilterNode
        // 由于 RenderGraph 没有 RemoveNode 方法，我们需要重建渲染图
        std::vector<std::shared_ptr<RenderNode>> savedNodes;
        for (size_t i = 0; i < renderGraph->GetNodeCount(); ++i) {
            auto node = renderGraph->GetNode(i);
            if (node && strcmp(node->GetName(), "Filter") != 0) {
                savedNodes.push_back(node);
            }
        }
        
        renderGraph->Clear();
        for (auto& node : savedNodes) {
            renderGraph->AddNode(node);
        }
    }
    catch (const std::exception& e) {
    }
}

