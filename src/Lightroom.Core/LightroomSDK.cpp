#include "LightroomSDK.h"
#include "D3D9Interop.h"
#include "ImageProcessing/ImageProcessor.h"
#include "ImageProcessing/ImageLoader.h"
#include "ImageProcessing/RAWImageInfo.h"
#include "RenderTargetManager.h"
#include "RenderGraph.h"
#include "RenderNodes/RenderNode.h"
#include "RenderNodes/ScaleNode.h"
#include "RenderNodes/ImageAdjustNode.h"
#include "ImageProcessing/RAWProcessor.h"
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

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace RenderCore;
using namespace LightroomCore;

// 全局状态
static std::shared_ptr<DynamicRHI> g_DynamicRHI = nullptr;
static std::unique_ptr<D3D9Interop> g_D3D9Interop = nullptr;
static std::unique_ptr<ImageProcessor> g_ImageProcessor = nullptr;
static std::unique_ptr<RenderTargetManager> g_RenderTargetManager = nullptr;

// 渲染目标关联的渲染图（每个渲染目标可以有独立的渲染图）
struct RenderTargetData {
    std::shared_ptr<RHITexture2D> ImageTexture;  // 加载的图片纹理
    std::unique_ptr<RenderGraph> RenderGraph;     // 渲染图
    bool bHasImage;
    LightroomCore::ImageFormat ImageFormat;      // 图片格式（Standard 或 RAW）
    std::unique_ptr<LightroomCore::RAWImageInfo> RAWInfo;  // RAW 信息（仅在 RAW 格式时有效）
    
    RenderTargetData() : bHasImage(false), ImageFormat(LightroomCore::ImageFormat::Unknown) {}
};

static std::unordered_map<void*, std::unique_ptr<RenderTargetData>> g_RenderTargetData;

bool InitSDK() {
    try {
        std::cout << "[SDK] Initializing Lightroom Core SDK..." << std::endl;
        
        // 0. 初始化 COM（WIC 需要）
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            std::cerr << "[SDK] Failed to initialize COM: 0x" << std::hex << hr << std::endl;
            return false;
        }
        
        // 1. 创建 D3D11 RHI
        g_DynamicRHI = PlatformCreateDynamicRHI(RHIAPIType::E_D3D11);
        if (!g_DynamicRHI) {
            std::cerr << "[SDK] Failed to create D3D11 RHI" << std::endl;
            return false;
        }
        g_DynamicRHI->Init();
        
        // 2. 初始化 D3D9 互操作
        g_D3D9Interop = std::make_unique<D3D9Interop>();
        if (!g_D3D9Interop->Initialize()) {
            std::cerr << "[SDK] Failed to initialize D3D9 interop" << std::endl;
            return false;
        }
        
        // 3. 创建图片处理器
        g_ImageProcessor = std::make_unique<ImageProcessor>(g_DynamicRHI);
        
        // 4. 创建渲染目标管理器
        g_RenderTargetManager = std::make_unique<RenderTargetManager>(g_DynamicRHI, g_D3D9Interop.get());
        
        std::cout << "[SDK] SDK Initialized Successfully" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[SDK] Exception during initialization: " << e.what() << std::endl;
        return false;
    }
}

void ShutdownSDK() {
    std::cout << "[SDK] Shutting down..." << std::endl;
    
    // 清理所有渲染目标数据
    g_RenderTargetData.clear();
    
    // 清理管理器
    g_RenderTargetManager.reset();
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
        std::cerr << "[SDK] RenderTargetManager not initialized" << std::endl;
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
        // 使用图片处理器加载图片
        data->ImageTexture = g_ImageProcessor->LoadImageFromFile(imagePath);
        if (!data->ImageTexture) {
            return false;
        }
        
        data->bHasImage = true;
        data->ImageFormat = g_ImageProcessor->GetLastImageFormat();
        
        // 如果是 RAW 格式，保存 RAW 信息
        if (data->ImageFormat == LightroomCore::ImageFormat::RAW) {
            const LightroomCore::RAWImageInfo* rawInfo = g_ImageProcessor->GetRAWInfo();
            if (rawInfo) {
                data->RAWInfo = std::make_unique<LightroomCore::RAWImageInfo>(*rawInfo);
            }
        } else {
            data->RAWInfo.reset();
        }
        
        // 根据图片格式和尺寸，决定使用哪个节点
        auto* renderTargetInfo = g_RenderTargetManager->GetRenderTargetInfo(renderTargetHandle);
        if (renderTargetInfo) {
            uint32_t imageWidth, imageHeight;
            g_ImageProcessor->GetLastImageSize(imageWidth, imageHeight);
            
            // 清除旧的渲染图
            data->RenderGraph->Clear();
            
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
            
            std::cout << "[SDK] Image loaded: " << imageWidth << "x" << imageHeight 
                      << ", Format: " << (data->ImageFormat == LightroomCore::ImageFormat::RAW ? "RAW" : "Standard")
                      << ", Nodes: ImageAdjust+Scale" << std::endl;
        }
        
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[SDK] Exception loading image: " << e.what() << std::endl;
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
        
        // 如果有图片，执行渲染图
        if (data->bHasImage && data->ImageTexture) {
            // 获取输出纹理（从 RenderTargetManager）
            auto outputTexture = g_RenderTargetManager->GetRHITexture(renderTargetHandle);
            if (!outputTexture) {
                std::cerr << "[SDK] Failed to get output texture from render target" << std::endl;
                return;
            }
            
            // 执行渲染图
            if (!data->RenderGraph->Execute(
                    data->ImageTexture,  // 输入：加载的图片纹理
                    outputTexture,       // 输出：渲染目标纹理
                    renderTargetInfo->Width,
                    renderTargetInfo->Height)) {
                std::cerr << "[SDK] Failed to execute render graph" << std::endl;
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
        if (renderTargetInfo->D3D9SharedSurface && renderTargetInfo->D3D9Surface) {
            g_D3D9Interop->CopySurface(
                renderTargetInfo->D3D9SharedSurface,
                renderTargetInfo->D3D9Surface,
                renderTargetInfo->Width,
                renderTargetInfo->Height
            );
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[SDK] Exception rendering to target: " << e.what() << std::endl;
    }
}

void ResizeRenderTarget(void* renderTargetHandle, uint32_t width, uint32_t height) {
    if (!renderTargetHandle || !g_RenderTargetManager) {
        return;
    }
    
    // 调整渲染目标大小
    if (!g_RenderTargetManager->ResizeRenderTarget(renderTargetHandle, width, height)) {
        std::cerr << "[SDK] Failed to resize render target" << std::endl;
    }
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
                    uint32_t imageWidth, imageHeight;
                    g_ImageProcessor->GetLastImageSize(imageWidth, imageHeight);
                    scaleNode->SetInputImageSize(imageWidth, imageHeight);
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
    strncpy_s(outMetadata->cameraModel, sizeof(outMetadata->cameraModel), 
              data->RAWInfo->cameraModel.c_str(), _TRUNCATE);
    strncpy_s(outMetadata->lensModel, sizeof(outMetadata->lensModel), 
              data->RAWInfo->lensModel.c_str(), _TRUNCATE);
    
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

