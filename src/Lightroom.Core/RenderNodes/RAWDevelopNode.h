#pragma once

#include "RenderNode.h"
#include "../RAWImageInfo.h"
#include "../RAWProcessor.h"
#include "../d3d11rhi/RHITexture2D.h"
#include "../d3d11rhi/RHIShdader.h"
#include "../d3d11rhi/RHIVertexBuffer.h"
#include "../d3d11rhi/RHIState.h"
#include "../d3d11rhi/RHIUniformBuffer.h"
#include <memory>
#include <wrl/client.h>
#include <d3d11.h>

namespace LightroomCore {

// RAW 开发节点：处理 RAW 图片的开发参数（白平衡、曝光等）
class RAWDevelopNode : public RenderNode {
public:
    RAWDevelopNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~RAWDevelopNode();

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "RAWDevelop"; }

    // 设置 RAW 开发参数（使用 C++ 内部完整版本）
    void SetDevelopParams(const RAWDevelopParamsInternal& params);

    // 设置 RAW 图片信息
    void SetRAWInfo(const RAWImageInfo& rawInfo);

private:
    bool InitializeShaderResources();
    void CleanupShaderResources();
    void UpdateConstantBuffer(uint32_t width, uint32_t height);

    RAWDevelopParamsInternal m_Params;
    RAWImageInfo m_RAWInfo;
    std::unique_ptr<RAWProcessor> m_RAWProcessor;

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

