#pragma once

#include "RenderNode.h"
#include "../d3d11rhi/RHITexture2D.h"
#include "../d3d11rhi/RHIShdader.h"
#include "../d3d11rhi/RHIVertexBuffer.h"
#include "../d3d11rhi/RHIState.h"
#include <memory>
#include <wrl/client.h>
#include <d3d11.h>

namespace LightroomCore {

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

} // namespace LightroomCore

