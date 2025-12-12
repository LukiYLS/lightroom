#include "LightroomSDK.h"
#include "D3D9Interop.h"
#include "ImageProcessor.h"
#include "RenderTargetManager.h"
#include "RenderGraph.h"
#include "RenderNodes/RenderNode.h"
#include "RenderNodes/PassthroughNode.h"
#include "RenderNodes/ScaleNode.h"
#include "RenderNodes/BrightnessContrastNode.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>

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
    
    RenderTargetData() : bHasImage(false) {}
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
    
    // 创建关联的渲染图
    auto renderGraph = std::make_unique<RenderGraph>(g_DynamicRHI);
    
    // 默认添加一个直通节点（如果图片尺寸匹配）或缩放节点（如果尺寸不匹配）
    // 这里先添加缩放节点，实际使用时可以根据图片尺寸动态调整
    auto scaleNode = std::make_shared<ScaleNode>(g_DynamicRHI);
    renderGraph->AddNode(scaleNode);
    
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
        
        // 根据图片尺寸和渲染目标尺寸，决定使用哪个节点
        auto* renderTargetInfo = g_RenderTargetManager->GetRenderTargetInfo(renderTargetHandle);
        if (renderTargetInfo) {
            uint32_t imageWidth, imageHeight;
            g_ImageProcessor->GetLastImageSize(imageWidth, imageHeight);
            
            // 清除旧的渲染图
            data->RenderGraph->Clear();
            
            if (imageWidth == renderTargetInfo->Width && imageHeight == renderTargetInfo->Height) {
                // 尺寸匹配，使用直通节点
                auto passthroughNode = std::make_shared<PassthroughNode>(g_DynamicRHI);
                data->RenderGraph->AddNode(passthroughNode);
            } else {
                // 尺寸不匹配，使用缩放节点
                auto scaleNode = std::make_shared<ScaleNode>(g_DynamicRHI);
                data->RenderGraph->AddNode(scaleNode);
            }
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

