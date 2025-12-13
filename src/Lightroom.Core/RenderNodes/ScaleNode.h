#pragma once

#include "RenderNode.h"
#include "../d3d11rhi/RHITexture2D.h"
#include "../d3d11rhi/RHIShdader.h"
#include "../d3d11rhi/RHIVertexBuffer.h"
#include "../d3d11rhi/RHIState.h"
#include "../d3d11rhi/RHIUniformBuffer.h"
#include <memory>
#include <wrl/client.h>
#include <d3d11.h>

namespace LightroomCore {

// 缩放节点：使用 shader 进行缩放和平移渲染
class ScaleNode : public RenderNode {
public:
    ScaleNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~ScaleNode();

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "Scale"; }

    // 设置缩放参数
    // zoomLevel: 缩放级别（1.0 = 100%, 2.0 = 200%, 0.5 = 50%）
    // panX, panY: 平移偏移（归一化坐标，范围 -1.0 到 1.0）
    void SetZoomParams(double zoomLevel, double panX, double panY);

    // 设置输入图片尺寸（用于计算正确的缩放和平移）
    void SetInputImageSize(uint32_t width, uint32_t height);

protected:
    // 重写基类的钩子方法
    virtual void UpdateConstantBuffers(uint32_t width, uint32_t height) override;
    virtual void SetConstantBuffers() override;
    virtual void SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) override;

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();

    // Shader resources（使用基类的 CompiledShader）
    CompiledShader m_Shader;

    // Constant buffer
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_ConstantBuffer;
    
    // 缩放和平移参数
    double m_ZoomLevel = 1.0;
    double m_PanX = 0.0;
    double m_PanY = 0.0;
    uint32_t m_InputImageWidth = 0;
    uint32_t m_InputImageHeight = 0;
    
    bool m_ShaderResourcesInitialized = false;
};

} // namespace LightroomCore

