#pragma once

#include "RenderNode.h"
#include "../d3d11rhi/RHITexture2D.h"
#include <memory>

namespace LightroomCore {

// RGB to YUV conversion node: performs color space conversion on GPU
// Outputs YUV420P format (separate Y, U, V planes)
class RGBToYUVNode : public RenderNode {
public:
    RGBToYUVNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~RGBToYUVNode();

    // Execute conversion: RGB input -> YUV420P output (Y, U, V textures)
    // inputTexture: RGB input texture (BGRA or RGBA)
    // yTexture: Y plane output (full resolution)
    // uTexture: U plane output (half resolution)
    // vTexture: V plane output (half resolution)
    // width, height: input texture dimensions
    bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                 std::shared_ptr<RenderCore::RHITexture2D> yTexture,
                 std::shared_ptr<RenderCore::RHITexture2D> uTexture,
                 std::shared_ptr<RenderCore::RHITexture2D> vTexture,
                 uint32_t width, uint32_t height);

    virtual const char* GetName() const override { return "RGBToYUV"; }

protected:
    virtual void UpdateConstantBuffers(uint32_t width, uint32_t height) override;
    virtual void SetConstantBuffers() override;
    virtual void SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) override;

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();

    // Shader resources
    CompiledShader m_YShader;  // For Y plane (full resolution)
    CompiledShader m_UShader;  // For U plane (half resolution)
    CompiledShader m_VShader;  // For V plane (half resolution)

    // Constant buffer
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_ParamsBuffer;

    bool m_ShaderResourcesInitialized = false;
};

} // namespace LightroomCore

