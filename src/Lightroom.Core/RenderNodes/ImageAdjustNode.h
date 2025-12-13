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
// 参考 Adobe Lightroom 的调整功能
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

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();
    void UpdateConstantBuffer(uint32_t width, uint32_t height);

    ImageAdjustParams m_Params;

    // Shader resources
    std::shared_ptr<RenderCore::RHIVertexBuffer> m_VertexBuffer;
    std::shared_ptr<RenderCore::RHISamplerState> m_SamplerState;
    std::shared_ptr<RenderCore::RHIUniformBuffer> m_ParamsBuffer;

    // D3D11 原生对象（用于直接访问）
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_D3D11VS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_D3D11PS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_InputLayout;

    bool m_ShaderResourcesInitialized = false;
};

} // namespace LightroomCore


