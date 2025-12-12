#include "RenderNode.h"
#include "d3d11rhi/RHIShdader.h"
#include "d3d11rhi/RHIDefinitions.h"
#include "d3d11rhi/RHI.h"
#include "d3d11rhi/D3D11RHI.h"
#include "d3d11rhi/D3D11Shader.h"
#include "d3d11rhi/D3D11VertexBuffer.h"
#include "d3d11rhi/Common.h"
#include <d3dcompiler.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>
#include <cfloat>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

// ============================================================================
// RenderNode (Base Class)
// ============================================================================

RenderNode::RenderNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : m_RHI(rhi)
{
    if (m_RHI) {
        m_CommandContext = m_RHI->GetDefaultCommandContext();
    }
}

RenderNode::~RenderNode() {
}

// ============================================================================
// PassthroughNode
// ============================================================================

PassthroughNode::PassthroughNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : RenderNode(rhi)
{
}

PassthroughNode::~PassthroughNode() {
}

bool PassthroughNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                              std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                              uint32_t width, uint32_t height) {
    if (!m_CommandContext || !inputTexture || !outputTarget) {
        return false;
    }

    // 直接使用 RHI 接口复制资源
    m_CommandContext->RHICopyResource(outputTarget, inputTexture);
    m_CommandContext->FlushCommands();
    return true;
}

// ============================================================================
// ScaleNode
// ============================================================================

ScaleNode::ScaleNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : RenderNode(rhi)
    , m_ShaderResourcesInitialized(false)
{
    InitializeShaderResources();
}

ScaleNode::~ScaleNode() {
    CleanupShaderResources();
}

bool ScaleNode::InitializeShaderResources() {
    if (m_ShaderResourcesInitialized || !m_RHI) {
        return m_ShaderResourcesInitialized;
    }

    // 简单的全屏四边形顶点 shader
    const char* vsCode = R"(
        struct VSInput {
            float2 Position : POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        struct VSOutput {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.Position = float4(input.Position, 0.0, 1.0);
            output.TexCoord = input.TexCoord;
            return output;
        }
    )";

    // 简单的采样像素 shader
    const char* psCode = R"(
        Texture2D InputTexture : register(t0);
        SamplerState InputSampler : register(s0);
        struct PSInput {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        float4 main(PSInput input) : SV_TARGET {
            return InputTexture.Sample(InputSampler, input.TexCoord);
        }
    )";

    try {
        // 获取 D3D11 RHI 和设备
        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
        if (!d3d11RHI) {
            std::cerr << "[ScaleNode] Failed to cast to D3D11DynamicRHI" << std::endl;
            return false;
        }
        
        ID3D11Device* device = d3d11RHI->GetDevice();
        if (!device) {
            std::cerr << "[ScaleNode] Failed to get D3D11 device" << std::endl;
            return false;
        }

        // 编译 Vertex Shader
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
        HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                std::cerr << "[ScaleNode] VS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
            }
            return false;
        }

        // 编译 Pixel Shader
        hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                std::cerr << "[ScaleNode] PS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
            }
            return false;
        }

        // 创建 Vertex Shader 对象
        Microsoft::WRL::ComPtr<ID3D11VertexShader> d3d11VS;
        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, d3d11VS.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[ScaleNode] Failed to create VS: 0x" << std::hex << hr << std::endl;
            return false;
        }

        // 创建 Pixel Shader 对象
        Microsoft::WRL::ComPtr<ID3D11PixelShader> d3d11PS;
        hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, d3d11PS.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[ScaleNode] Failed to create PS: 0x" << std::hex << hr << std::endl;
            return false;
        }

        // 创建输入布局
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        
        hr = device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_InputLayout.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[ScaleNode] Failed to create input layout: 0x" << std::hex << hr << std::endl;
            return false;
        }

        // 保存 D3D11 shader 对象（在 Execute 中直接使用）
        m_D3D11VS = d3d11VS;
        m_D3D11PS = d3d11PS;

        // 创建全屏四边形顶点缓冲区
        struct SimpleVertex {
            float x, y;
            float u, v;
        };
        SimpleVertex vertices[4] = {
            { -1.0f,  1.0f, 0.0f, 0.0f },  // 左上
            {  1.0f,  1.0f, 1.0f, 0.0f },  // 右上
            { -1.0f, -1.0f, 0.0f, 1.0f },  // 左下
            {  1.0f, -1.0f, 1.0f, 1.0f }   // 右下
        };

        m_VertexBuffer = m_RHI->RHICreateVertexBuffer(
            vertices,
            RenderCore::EBufferUsageFlags::BUF_Static,
            sizeof(SimpleVertex),
            4
        );

        // 创建采样器状态
        // 使用构造函数初始化所有字段，确保所有值都被正确设置
        RenderCore::SamplerStateInitializerRHI samplerInit(
            RenderCore::SF_Bilinear,  // Filter
            RenderCore::AM_Clamp,     // AddressU
            RenderCore::AM_Clamp,     // AddressV
            RenderCore::AM_Clamp,     // AddressW
            0.0f,                      // MipBias
            0,                         // MaxAnisotropy
            0.0f,                      // MinMipLevel
            FLT_MAX,                   // MaxMipLevel
            0,                         // BorderColor
            RenderCore::SCF_Never      // SamplerComparisonFunction
        );
        m_SamplerState = m_RHI->RHICreateSamplerState(samplerInit);
        
        if (!m_SamplerState) {
            std::cerr << "[ScaleNode] Failed to create sampler state" << std::endl;
        }

        m_ShaderResourcesInitialized = (m_VertexBuffer != nullptr && 
                                        m_SamplerState != nullptr && 
                                        m_D3D11VS != nullptr && 
                                        m_D3D11PS != nullptr && 
                                        m_InputLayout != nullptr);
    }
    catch (const std::exception& e) {
        std::cerr << "[ScaleNode] Failed to initialize shader resources: " << e.what() << std::endl;
        m_ShaderResourcesInitialized = false;
    }

    return m_ShaderResourcesInitialized;
}

void ScaleNode::CleanupShaderResources() {
    m_VertexShader.reset();
    m_PixelShader.reset();
    m_VertexBuffer.reset();
    m_SamplerState.reset();
    m_D3D11VS.Reset();
    m_D3D11PS.Reset();
    m_InputLayout.Reset();
    m_ShaderResourcesInitialized = false;
}

bool ScaleNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) {
    if (!m_CommandContext || !inputTexture || !outputTarget || !m_ShaderResourcesInitialized) {
        return false;
    }

    if (!m_D3D11VS || !m_D3D11PS || !m_InputLayout || !m_VertexBuffer || !m_SamplerState) {
        std::cerr << "[ScaleNode] Shader resources not properly initialized" << std::endl;
        return false;
    }

    // 获取 D3D11 命令上下文（用于直接设置 shader）
    RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
    if (!d3d11RHI) {
        return false;
    }
    
    ID3D11DeviceContext* d3d11Context = d3d11RHI->GetDeviceContext();
    if (!d3d11Context) {
        return false;
    }

    // 1. 设置渲染目标
    m_CommandContext->SetRenderTarget(outputTarget, nullptr);

    // 2. 设置视口
    m_CommandContext->SetViewPort(0, 0, width, height);

    // 3. 清除渲染目标
    m_CommandContext->Clear(outputTarget, nullptr, core::FLinearColor(0, 0, 0, 1));

    // 4. 设置 Vertex Shader
    d3d11Context->VSSetShader(m_D3D11VS.Get(), nullptr, 0);

    // 5. 设置 Pixel Shader
    d3d11Context->PSSetShader(m_D3D11PS.Get(), nullptr, 0);

    // 6. 设置输入布局
    d3d11Context->IASetInputLayout(m_InputLayout.Get());

    // 7. 设置顶点缓冲区
    RenderCore::D3D11VertexBuffer* d3d11VB = dynamic_cast<RenderCore::D3D11VertexBuffer*>(m_VertexBuffer.get());
    if (d3d11VB) {
        ID3D11Buffer* vb = d3d11VB->GetNativeBuffer();
        if (vb) {
            UINT stride = d3d11VB->GetStride();  // 使用实际的 stride
            UINT offset = 0;
            d3d11Context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        }
    }

    // 8. 设置图元拓扑
    d3d11Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // 9. 设置输入纹理
    m_CommandContext->RHISetShaderTexture(RenderCore::EShaderFrequency::SF_Pixel, 0, inputTexture);

    // 10. 设置采样器
    m_CommandContext->RHISetShaderSampler(RenderCore::EShaderFrequency::SF_Pixel, 0, m_SamplerState);

    // 11. 禁用深度测试和混合（全屏四边形不需要）
    d3d11Context->OMSetDepthStencilState(nullptr, 0);
    float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    d3d11Context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);

    // 12. 绘制全屏四边形（4 个顶点，三角形条带）
    d3d11Context->Draw(4, 0);

    // 13. 清理状态（可选，避免影响后续渲染）
    ID3D11ShaderResourceView* nullSRV = nullptr;
    d3d11Context->PSSetShaderResources(0, 1, &nullSRV);

    // 14. 刷新命令
    m_CommandContext->FlushCommands();
    
    return true;
}

// ============================================================================
// BrightnessContrastNode
// ============================================================================

BrightnessContrastNode::BrightnessContrastNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : RenderNode(rhi)
    , m_Brightness(0.0f)
    , m_Contrast(1.0f)
    , m_ShaderResourcesInitialized(false)
{
    InitializeShaderResources();
}

BrightnessContrastNode::~BrightnessContrastNode() {
    CleanupShaderResources();
}

bool BrightnessContrastNode::InitializeShaderResources() {
    // 类似 ScaleNode 的实现
    // TODO: 实现亮度/对比度 shader
    m_ShaderResourcesInitialized = false;
    return false;
}

void BrightnessContrastNode::CleanupShaderResources() {
    m_VertexShader.reset();
    m_PixelShader.reset();
    m_VertexBuffer.reset();
    m_SamplerState.reset();
    m_UniformBuffer.reset();
    m_ShaderResourcesInitialized = false;
}

bool BrightnessContrastNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                                     std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                                     uint32_t width, uint32_t height) {
    if (!m_CommandContext || !inputTexture || !outputTarget || !m_ShaderResourcesInitialized) {
        return false;
    }

    // TODO: 实现亮度/对比度调整渲染
    return false;
}

} // namespace LightroomCore

