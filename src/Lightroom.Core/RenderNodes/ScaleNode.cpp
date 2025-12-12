#include "ScaleNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11VertexBuffer.h"
#include "../d3d11rhi/RHI.h"
#include "../d3d11rhi/Common.h"
#include <d3dcompiler.h>
#include <iostream>
#include <cfloat>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

// Constant buffer 结构体（必须 16 字节对齐）
struct ScaleConstantBuffer {
    float ZoomLevel;      // 用户缩放级别（1.0 = 100%, 2.0 = 200%, 0.5 = 50%）
    float PanX;           // X 平移（归一化坐标）
    float PanY;           // Y 平移（归一化坐标）
    float FitScale;       // 自适应缩放比例（保持宽高比，以长边自适应）
    float InputImageWidth;  // 输入图片宽度
    float InputImageHeight; // 输入图片高度
    float OutputWidth;      // 输出宽度
    float OutputHeight;     // 输出高度
};

ScaleNode::ScaleNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : RenderNode(rhi)
    , m_ShaderResourcesInitialized(false)
    , m_ZoomLevel(1.0)
    , m_PanX(0.0)
    , m_PanY(0.0)
    , m_InputImageWidth(0)
    , m_InputImageHeight(0)
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

    // 顶点 shader（使用 constant buffer）
    const char* vsCode = R"(
        cbuffer ScaleParams : register(b0) {
            float ZoomLevel;
            float PanX;
            float PanY;
            float FitScale;
            float InputImageWidth;
            float InputImageHeight;
            float OutputWidth;
            float OutputHeight;
        };
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
            
            // 计算保持宽高比的缩放和平移
            // input.TexCoord 范围是 [0, 1]，转换为 [-1, 1] 的屏幕坐标
            float2 screenPos = (input.TexCoord - 0.5) * 2.0;  // [0,1] -> [-1,1]
            
            // 计算图片在屏幕上的实际显示尺寸（归一化，考虑 FitScale）
            // FitScale = min(OutputWidth/InputImageWidth, OutputHeight/InputImageHeight)
            // 图片显示宽度（归一化）= InputImageWidth * FitScale / OutputWidth
            // 图片显示高度（归一化）= InputImageHeight * FitScale / OutputHeight
            float2 imageDisplaySize;
            imageDisplaySize.x = (InputImageWidth * FitScale) / OutputWidth;
            imageDisplaySize.y = (InputImageHeight * FitScale) / OutputHeight;
            
            // 将屏幕坐标映射到图片显示区域（考虑宽高比）
            // 如果屏幕坐标超出图片显示区域，会在 Pixel Shader 中处理为黑色
            float2 imagePos = screenPos / imageDisplaySize;
            
            // 应用用户缩放级别
            imagePos = imagePos / ZoomLevel;
            
            // 应用平移
            imagePos = imagePos + float2(PanX, PanY);
            
            // 转换回纹理坐标 [0, 1]
            output.TexCoord = imagePos * 0.5 + 0.5;
            
            return output;
        }
    )";

    // 像素 shader（只需要采样纹理，缩放和平移已在 Vertex Shader 中完成）
    const char* psCode = R"(
        Texture2D InputTexture : register(t0);
        SamplerState InputSampler : register(s0);
        struct PSInput {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        float4 main(PSInput input) : SV_TARGET {
            // 如果纹理坐标超出范围，返回黑色
            if (input.TexCoord.x < 0.0 || input.TexCoord.x > 1.0 ||
                input.TexCoord.y < 0.0 || input.TexCoord.y > 1.0) {
                return float4(0.0, 0.0, 0.0, 1.0);
            }
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

        // 创建 constant buffer（16 字节对齐）
        const uint32_t cbSize = sizeof(ScaleConstantBuffer);
        m_ConstantBuffer = m_RHI->RHICreateUniformBuffer(cbSize);
        if (!m_ConstantBuffer) {
            std::cerr << "[ScaleNode] Failed to create constant buffer" << std::endl;
        }

        m_ShaderResourcesInitialized = (m_VertexBuffer != nullptr && 
                                        m_SamplerState != nullptr && 
                                        m_ConstantBuffer != nullptr &&
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
    m_ConstantBuffer.reset();
    m_D3D11VS.Reset();
    m_D3D11PS.Reset();
    m_InputLayout.Reset();
    m_ShaderResourcesInitialized = false;
}

void ScaleNode::SetZoomParams(double zoomLevel, double panX, double panY) {
    m_ZoomLevel = zoomLevel;
    m_PanX = panX;
    m_PanY = panY;
}

void ScaleNode::SetInputImageSize(uint32_t width, uint32_t height) {
    m_InputImageWidth = width;
    m_InputImageHeight = height;
}

void ScaleNode::UpdateConstantBuffer(uint32_t outputWidth, uint32_t outputHeight) {
    if (!m_ConstantBuffer || !m_CommandContext) {
        return;
    }

    // 计算保持宽高比的自适应缩放比例（以长边自适应）
    // FitScale = min(输出宽度/输入宽度, 输出高度/输入高度)
    // 这样确保图片完全显示在输出区域内，短边会有黑边
    float fitScale = 1.0f;
    if (m_InputImageWidth > 0 && m_InputImageHeight > 0 && outputWidth > 0 && outputHeight > 0) {
        float scaleX = static_cast<float>(outputWidth) / static_cast<float>(m_InputImageWidth);
        float scaleY = static_cast<float>(outputHeight) / static_cast<float>(m_InputImageHeight);
        fitScale = (scaleX < scaleY) ? scaleX : scaleY;  // 取较小值，确保图片完全显示
    }

    ScaleConstantBuffer cbData;
    cbData.ZoomLevel = static_cast<float>(m_ZoomLevel);
    cbData.PanX = static_cast<float>(m_PanX);
    cbData.PanY = static_cast<float>(m_PanY);
    cbData.FitScale = fitScale;
    cbData.InputImageWidth = static_cast<float>(m_InputImageWidth);
    cbData.InputImageHeight = static_cast<float>(m_InputImageHeight);
    cbData.OutputWidth = static_cast<float>(outputWidth);
    cbData.OutputHeight = static_cast<float>(outputHeight);

    // 使用 RHI 接口更新 constant buffer
    m_CommandContext->RHIUpdateUniformBuffer(m_ConstantBuffer, &cbData);
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

    // 1. 更新 constant buffer
    UpdateConstantBuffer(width, height);

    // 2. 设置渲染目标
    m_CommandContext->SetRenderTarget(outputTarget, nullptr);

    // 3. 设置视口
    m_CommandContext->SetViewPort(0, 0, width, height);

    // 4. 清除渲染目标
    m_CommandContext->Clear(outputTarget, nullptr, core::FLinearColor(0, 0, 0, 1));

    // 5. 设置 constant buffer 到 Vertex Shader（Pixel Shader 不需要，因为缩放计算在 VS 中完成）
    if (m_ConstantBuffer) {
        m_CommandContext->RHISetShaderUniformBuffer(RenderCore::EShaderFrequency::SF_Vertex, 0, m_ConstantBuffer);
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
            UINT stride = d3d11VB->GetStride();  // 使用实际的 stride
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

    // 13. 禁用深度测试和混合（全屏四边形不需要）
    d3d11Context->OMSetDepthStencilState(nullptr, 0);
    float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    d3d11Context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);

    // 14. 绘制全屏四边形（4 个顶点，三角形条带）
    d3d11Context->Draw(4, 0);

    // 15. 清理状态（可选，避免影响后续渲染）
    ID3D11ShaderResourceView* nullSRV = nullptr;
    d3d11Context->PSSetShaderResources(0, 1, &nullSRV);

    // 16. 刷新命令
    m_CommandContext->FlushCommands();
    
    return true;
}

} // namespace LightroomCore

