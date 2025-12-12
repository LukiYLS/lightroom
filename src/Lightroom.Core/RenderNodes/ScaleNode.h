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

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();
    void UpdateConstantBuffer(uint32_t outputWidth, uint32_t outputHeight);

    // RHI shader 对象
    std::shared_ptr<RenderCore::RHIVertexShader> m_VertexShader;
    std::shared_ptr<RenderCore::RHIPixelShader> m_PixelShader;
    std::shared_ptr<RenderCore::RHIVertexBuffer> m_VertexBuffer;
    std::shared_ptr<RenderCore::RHISamplerState> m_SamplerState;
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_ConstantBuffer;
    
    // D3D11 原生对象（用于直接访问，因为 RHI 接口需要文件路径）
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_D3D11VS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_D3D11PS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_InputLayout;
    
    // 缩放和平移参数
    double m_ZoomLevel = 1.0;
    double m_PanX = 0.0;
    double m_PanY = 0.0;
    uint32_t m_InputImageWidth = 0;
    uint32_t m_InputImageHeight = 0;
    
    bool m_ShaderResourcesInitialized = false;
};

} // namespace LightroomCore

