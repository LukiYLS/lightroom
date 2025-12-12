#pragma once

#include "RenderNode.h"
#include "../d3d11rhi/RHITexture2D.h"
#include "../d3d11rhi/RHIShdader.h"
#include "../d3d11rhi/RHIVertexBuffer.h"
#include "../d3d11rhi/RHIState.h"
#include "../d3d11rhi/RHIUniformBuffer.h"
#include <memory>

namespace LightroomCore {

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

