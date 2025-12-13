#pragma once

#include "RenderNode.h"
#include "../LightroomSDKTypes.h"
#include "../d3d11rhi/RHITexture2D.h"
#include "../d3d11rhi/RHIShdader.h"
#include "../d3d11rhi/RHIVertexBuffer.h"
#include "../d3d11rhi/RHIState.h"
#include "../d3d11rhi/RHIUniformBuffer.h"
#include <memory>
#include <wrl/client.h>
#include <d3d11.h>

namespace LightroomCore {

// 图像调整节点：通用的图像调整处理（适用于 RAW 和标准图片）
class ImageAdjustNode : public RenderNode {
public:
    ImageAdjustNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~ImageAdjustNode();

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "ImageAdjust"; }

    // 设置调整参数
    void SetAdjustParams(const ImageAdjustParams& params);

    // 获取当前调整参数
    const ImageAdjustParams& GetAdjustParams() const { return m_Params; }

protected:
    // 重写基类的钩子方法
    virtual void UpdateConstantBuffers(uint32_t width, uint32_t height) override;
    virtual void SetConstantBuffers() override;
    virtual void SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) override;

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();

    ImageAdjustParams m_Params;

    // Shader resources（使用基类的 CompiledShader）
    CompiledShader m_Shader;

    // Constant buffer
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_ParamsBuffer;

    bool m_ShaderResourcesInitialized = false;
};

} // namespace LightroomCore




