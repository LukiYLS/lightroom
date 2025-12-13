#pragma once

#include "../d3d11rhi/DynamicRHI.h"
#include "../d3d11rhi/RHICommandContext.h"
#include "../d3d11rhi/RHITexture2D.h"
#include "../d3d11rhi/RHIVertexBuffer.h"
#include "../d3d11rhi/RHIState.h"
#include "../d3d11rhi/RHIUniformBuffer.h"
#include <memory>
#include <wrl/client.h>
#include <d3d11.h>

namespace LightroomCore {

// 简单的顶点结构（全屏四边形）
struct SimpleVertex {
    float Position[2];
    float TexCoord[2];
};

// 渲染节点基类：支持不同的图片处理 shader
class RenderNode {
public:
    RenderNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~RenderNode();

    // 执行渲染（通用框架，子类可以重写或使用）
    // inputTexture: 输入纹理
    // outputTarget: 输出渲染目标纹理
    // width, height: 输出尺寸
    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height);

    // 获取节点名称（用于调试）
    virtual const char* GetName() const = 0;

protected:
    // 初始化公共资源（全屏四边形顶点缓冲区、采样器状态）
    bool InitializeCommonResources();
    void CleanupCommonResources();

    // Shader 编译辅助方法
    struct CompiledShader {
        Microsoft::WRL::ComPtr<ID3DBlob> Blob;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> VS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;
    };
    bool CompileShaders(const char* vsCode, const char* psCode, CompiledShader& outShader);

    // 通用的渲染设置（子类可以重写以自定义）
    virtual void SetupRenderState(ID3D11DeviceContext* d3d11Context,
                                  std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                                  uint32_t width, uint32_t height);
    
    // 执行绘制（子类可以重写以自定义）
    virtual void Draw(ID3D11DeviceContext* d3d11Context);

    // 子类需要实现的钩子方法
    virtual void UpdateConstantBuffers(uint32_t width, uint32_t height) {}
    virtual void SetConstantBuffers() {}
    virtual void SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) {}

    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    std::shared_ptr<RenderCore::RHICommandContext> m_CommandContext;

    // 公共资源（所有节点共享）
    std::shared_ptr<RenderCore::RHIVertexBuffer> m_CommonVertexBuffer;
    std::shared_ptr<RenderCore::RHISamplerState> m_CommonSamplerState;
    bool m_CommonResourcesInitialized = false;

    // 当前渲染使用的 shader（由子类设置）
    CompiledShader* m_CurrentShader = nullptr;
};

} // namespace LightroomCore

