#pragma once

#include "d3d11rhi/DynamicRHI.h"
#include "d3d11rhi/RHICommandContext.h"
#include <memory>
#include <string>
#include <wrl/client.h>
#include <d3d11.h>

namespace LightroomCore {

// 渲染节点基类：支持不同的图片处理 shader
class RenderNode {
public:
    RenderNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~RenderNode();

    // 执行渲染
    // inputTexture: 输入纹理
    // outputTarget: 输出渲染目标纹理
    // width, height: 输出尺寸
    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) = 0;

    // 获取节点名称（用于调试）
    virtual const char* GetName() const = 0;

protected:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    std::shared_ptr<RenderCore::RHICommandContext> m_CommandContext;
};

// 直通节点：直接复制输入到输出
class PassthroughNode : public RenderNode {
public:
    PassthroughNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~PassthroughNode();

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "Passthrough"; }
};

// 缩放节点：使用 shader 进行缩放渲染
class ScaleNode : public RenderNode {
public:
    ScaleNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~ScaleNode();

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "Scale"; }

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();

    // RHI shader 对象
    std::shared_ptr<RenderCore::RHIVertexShader> m_VertexShader;
    std::shared_ptr<RenderCore::RHIPixelShader> m_PixelShader;
    std::shared_ptr<RenderCore::RHIVertexBuffer> m_VertexBuffer;
    std::shared_ptr<RenderCore::RHISamplerState> m_SamplerState;
    
    // D3D11 原生对象（用于直接访问，因为 RHI 接口需要文件路径）
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_D3D11VS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_D3D11PS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_InputLayout;
    
    bool m_ShaderResourcesInitialized = false;
};

// 亮度/对比度调整节点
class BrightnessContrastNode : public RenderNode {
public:
    BrightnessContrastNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~BrightnessContrastNode();

    void SetBrightness(float brightness) { m_Brightness = brightness; }
    void SetContrast(float contrast) { m_Contrast = contrast; }

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "BrightnessContrast"; }

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();

    float m_Brightness = 0.0f;
    float m_Contrast = 1.0f;
    std::shared_ptr<RenderCore::RHIVertexShader> m_VertexShader;
    std::shared_ptr<RenderCore::RHIPixelShader> m_PixelShader;
    std::shared_ptr<RenderCore::RHIVertexBuffer> m_VertexBuffer;
    std::shared_ptr<RenderCore::RHISamplerState> m_SamplerState;
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_UniformBuffer;
    bool m_ShaderResourcesInitialized = false;
};

} // namespace LightroomCore

