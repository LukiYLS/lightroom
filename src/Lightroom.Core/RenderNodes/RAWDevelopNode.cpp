#include "RAWDevelopNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11VertexBuffer.h"
#include "../d3d11rhi/D3D11UniformBuffer.h"
#include "../d3d11rhi/RHI.h"
#include "../d3d11rhi/Common.h"
#include <d3dcompiler.h>
#include <iostream>
#include <cfloat>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

// Constant buffer 结构体（必须 16 字节对齐）
struct RAWDevelopConstantBuffer {
    float WhiteBalance[4];  // R, G1, B, G2
    float Exposure;          // EV adjustment
    float Contrast;          // Contrast adjustment
    float Saturation;       // Saturation multiplier
    float Highlights;       // Highlights recovery
    float Shadows;          // Shadows lift
    float Temperature;     // Color temperature (Kelvin)
    float Tint;            // Tint adjustment
    float ImageWidth;      // Input image width
    float ImageHeight;     // Input image height
    uint32_t BayerPattern; // Bayer pattern (0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG)
    uint32_t EnableDemosaicing; // 0 or 1
    uint32_t DemosaicAlgorithm; // 0=bilinear, 1=AMaZE, etc.
    float Padding[4];      // 对齐到 16 字节
};

RAWDevelopNode::RAWDevelopNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : RenderNode(rhi)
    , m_ShaderResourcesInitialized(false)
{
    // 创建 RAWProcessor
    m_RAWProcessor = std::make_unique<RAWProcessor>(rhi);
    
    InitializeShaderResources();
}

RAWDevelopNode::~RAWDevelopNode() {
    CleanupShaderResources();
}

bool RAWDevelopNode::InitializeShaderResources() {
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

    // RAW 开发像素 shader（应用白平衡、曝光、对比度、饱和度等调整）
    const char* psCode = R"(
        cbuffer RAWDevelopParams : register(b0) {
            float4 WhiteBalance;
            float Exposure;
            float Contrast;
            float Saturation;
            float Highlights;
            float Shadows;
            float Temperature;
            float Tint;
            float ImageWidth;
            float ImageHeight;
            uint BayerPattern;
            uint EnableDemosaicing;
            uint DemosaicAlgorithm;
            float2 Padding;
        };
        Texture2D InputTexture : register(t0);
        SamplerState InputSampler : register(s0);
        struct PSInput {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        float4 main(PSInput input) : SV_TARGET {
            float4 color = InputTexture.Sample(InputSampler, input.TexCoord);
            
            // 注意：输入纹理已经是处理过的 RGB（通过 dcraw_process）
            // dcraw_process 已经应用了：
            // - 去马赛克
            // - 相机白平衡
            // - gamma 校正（sRGB）
            // - 色彩空间转换（sRGB）
            // - 自动亮度调整
            
            // 这里只进行额外的调整（相对于 LibRaw 处理结果的微调）
            
            // 应用白平衡调整（相对于相机白平衡的微调）
            // WhiteBalance = 1.0 表示不调整，使用 LibRaw 的结果
            color.rgb *= WhiteBalance.rgb;
            
            // 应用曝光补偿（EV 调整）
            float exposureMultiplier = pow(2.0, Exposure);
            color.rgb *= exposureMultiplier;
            
            // 应用对比度（简化实现）
            // Contrast 范围通常是 [-100, 100]，这里归一化到 [-1, 1]
            color.rgb = (color.rgb - 0.5) * (1.0 + Contrast) + 0.5;
            
            // 应用饱和度
            float luminance = dot(color.rgb, float3(0.299, 0.587, 0.114));
            color.rgb = lerp(float3(luminance, luminance, luminance), color.rgb, Saturation);
            
            // 裁剪到有效范围 [0, 1]
            color.rgb = saturate(color.rgb);
            
            return color;
        }
    )";

    try {
        // 获取 D3D11 RHI 和设备
        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
        if (!d3d11RHI) {
            std::cerr << "[RAWDevelopNode] Failed to cast to D3D11DynamicRHI" << std::endl;
            return false;
        }
        
        ID3D11Device* device = d3d11RHI->GetDevice();
        if (!device) {
            std::cerr << "[RAWDevelopNode] Failed to get D3D11 device" << std::endl;
            return false;
        }

        // 编译 Vertex Shader
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
        HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                std::cerr << "[RAWDevelopNode] VS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
            }
            return false;
        }

        // 编译 Pixel Shader
        hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                std::cerr << "[RAWDevelopNode] PS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
            }
            return false;
        }

        // 创建 Vertex Shader 对象
        Microsoft::WRL::ComPtr<ID3D11VertexShader> d3d11VS;
        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, d3d11VS.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[RAWDevelopNode] Failed to create VS: 0x" << std::hex << hr << std::endl;
            return false;
        }

        // 创建 Pixel Shader 对象
        Microsoft::WRL::ComPtr<ID3D11PixelShader> d3d11PS;
        hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, d3d11PS.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[RAWDevelopNode] Failed to create PS: 0x" << std::hex << hr << std::endl;
            return false;
        }

        // 创建输入布局
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        
        hr = device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_InputLayout.GetAddressOf());
        if (FAILED(hr)) {
            std::cerr << "[RAWDevelopNode] Failed to create input layout: 0x" << std::hex << hr << std::endl;
            return false;
        }

        // 保存 D3D11 shader 对象
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
        RenderCore::SamplerStateInitializerRHI samplerInit(
            RenderCore::SF_Bilinear,
            RenderCore::AM_Clamp,
            RenderCore::AM_Clamp,
            RenderCore::AM_Clamp,
            0.0f,
            0,
            0.0f,
            FLT_MAX,
            0,
            RenderCore::SCF_Never
        );
        m_SamplerState = m_RHI->RHICreateSamplerState(samplerInit);
        
        if (!m_SamplerState) {
            std::cerr << "[RAWDevelopNode] Failed to create sampler state" << std::endl;
        }

        // 创建 constant buffer
        const uint32_t cbSize = sizeof(RAWDevelopConstantBuffer);
        m_ParamsBuffer = m_RHI->RHICreateUniformBuffer(cbSize);
        if (!m_ParamsBuffer) {
            std::cerr << "[RAWDevelopNode] Failed to create constant buffer" << std::endl;
        }

        m_ShaderResourcesInitialized = (m_VertexBuffer != nullptr && 
                                        m_SamplerState != nullptr && 
                                        m_ParamsBuffer != nullptr &&
                                        m_D3D11VS != nullptr && 
                                        m_D3D11PS != nullptr && 
                                        m_InputLayout != nullptr);
    }
    catch (const std::exception& e) {
        std::cerr << "[RAWDevelopNode] Failed to initialize shader resources: " << e.what() << std::endl;
        m_ShaderResourcesInitialized = false;
    }

    return m_ShaderResourcesInitialized;
}

void RAWDevelopNode::CleanupShaderResources() {
    m_VertexBuffer.reset();
    m_SamplerState.reset();
    m_ParamsBuffer.reset();
    m_D3D11VS.Reset();
    m_D3D11PS.Reset();
    m_InputLayout.Reset();
    m_RAWProcessor.reset();
    m_ShaderResourcesInitialized = false;
}

void RAWDevelopNode::SetDevelopParams(const RAWDevelopParamsInternal& params) {
    m_Params = params;
}

void RAWDevelopNode::SetRAWInfo(const RAWImageInfo& rawInfo) {
    m_RAWInfo = rawInfo;
}

void RAWDevelopNode::UpdateConstantBuffer(uint32_t width, uint32_t height) {
    if (!m_ParamsBuffer || !m_CommandContext) {
        return;
    }

    RAWDevelopConstantBuffer cbData;
    cbData.WhiteBalance[0] = m_Params.whiteBalance[0];
    cbData.WhiteBalance[1] = m_Params.whiteBalance[1];
    cbData.WhiteBalance[2] = m_Params.whiteBalance[2];
    cbData.WhiteBalance[3] = m_Params.whiteBalance[3];
    cbData.Exposure = m_Params.exposure;
    cbData.Contrast = m_Params.contrast / 100.0f;  // 归一化到 -1 到 1
    cbData.Saturation = m_Params.saturation;
    cbData.Highlights = m_Params.highlights / 100.0f;
    cbData.Shadows = m_Params.shadows / 100.0f;
    cbData.Temperature = m_Params.temperature;
    cbData.Tint = m_Params.tint / 150.0f;  // 归一化到 -1 到 1
    cbData.ImageWidth = static_cast<float>(width);
    cbData.ImageHeight = static_cast<float>(height);
    cbData.BayerPattern = m_RAWInfo.bayerPattern;
    cbData.EnableDemosaicing = m_Params.enableDemosaicing ? 1 : 0;
    cbData.DemosaicAlgorithm = m_Params.demosaicAlgorithm;
    cbData.Padding[0] = 0.0f;
    cbData.Padding[1] = 0.0f;

    // 使用 RHI 接口更新 constant buffer
    m_CommandContext->RHIUpdateUniformBuffer(m_ParamsBuffer, &cbData);
}

bool RAWDevelopNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                            std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                            uint32_t width, uint32_t height) {
    if (!m_CommandContext || !inputTexture || !outputTarget || !m_ShaderResourcesInitialized) {
        return false;
    }

    if (!m_D3D11VS || !m_D3D11PS || !m_InputLayout || !m_VertexBuffer || !m_SamplerState || !m_ParamsBuffer) {
        std::cerr << "[RAWDevelopNode] Shader resources not properly initialized" << std::endl;
        return false;
    }

    // 获取 D3D11 命令上下文
    RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
    if (!d3d11RHI) {
        return false;
    }
    
    ID3D11DeviceContext* d3d11Context = d3d11RHI->GetDeviceContext();
    if (!d3d11Context) {
        return false;
    }

    // 1. 更新 constant buffer
    UpdateConstantBuffer(width, height);

    // 2. 设置渲染目标
    m_CommandContext->SetRenderTarget(outputTarget, nullptr);

    // 3. 设置视口
    m_CommandContext->SetViewPort(0, 0, width, height);

    // 4. 清除渲染目标
    m_CommandContext->Clear(outputTarget, nullptr, core::FLinearColor(0, 0, 0, 1));

    // 5. 设置 constant buffer 到 Pixel Shader
    if (m_ParamsBuffer) {
        m_CommandContext->RHISetShaderUniformBuffer(RenderCore::EShaderFrequency::SF_Pixel, 0, m_ParamsBuffer);
    }

    // 6. 设置 Vertex Shader
    d3d11Context->VSSetShader(m_D3D11VS.Get(), nullptr, 0);

    // 7. 设置 Pixel Shader
    d3d11Context->PSSetShader(m_D3D11PS.Get(), nullptr, 0);

    // 8. 设置输入布局
    d3d11Context->IASetInputLayout(m_InputLayout.Get());

    // 9. 设置顶点缓冲区
    RenderCore::D3D11VertexBuffer* d3d11VB = dynamic_cast<RenderCore::D3D11VertexBuffer*>(m_VertexBuffer.get());
    if (d3d11VB) {
        ID3D11Buffer* vb = d3d11VB->GetNativeBuffer();
        if (vb) {
            UINT stride = d3d11VB->GetStride();
            UINT offset = 0;
            d3d11Context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        }
    }

    // 10. 设置图元拓扑
    d3d11Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // 11. 设置输入纹理
    m_CommandContext->RHISetShaderTexture(RenderCore::EShaderFrequency::SF_Pixel, 0, inputTexture);

    // 12. 设置采样器
    m_CommandContext->RHISetShaderSampler(RenderCore::EShaderFrequency::SF_Pixel, 0, m_SamplerState);

    // 13. 禁用深度测试和混合
    d3d11Context->OMSetDepthStencilState(nullptr, 0);
    float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    d3d11Context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);

    // 14. 绘制全屏四边形
    d3d11Context->Draw(4, 0);

    // 15. 清理状态
    ID3D11ShaderResourceView* nullSRV = nullptr;
    d3d11Context->PSSetShaderResources(0, 1, &nullSRV);

    // 16. 刷新命令
    m_CommandContext->FlushCommands();
    
    return true;
}

} // namespace LightroomCore

