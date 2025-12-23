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
struct __declspec(align(16)) ScaleConstantBuffer {
    float ZoomLevel;      // 用户缩放级别（1.0 = 100%, 2.0 = 200%, 0.5 = 50%）
    float PanX;           // X 平移（归一化坐标）
    float PanY;           // Y 平移（归一化坐标）
    float FitScale;       // 自适应缩放比例（保持宽高比，以长边自适应）
    float InputImageWidth;  // 输入图片宽度
    float InputImageHeight; // 输入图片高度
    float OutputWidth;      // 输出宽度
    float OutputHeight;     // 输出高度
    float Padding[2];     // 填充到 16 字节边界（8 个 float = 32 字节，需要填充到 32 字节，已经是16的倍数）
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

    // 使用基类的 CompileShaders 方法
    if (!CompileShaders(vsCode, psCode, m_Shader)) {
        std::cerr << "[ScaleNode] Failed to compile shaders" << std::endl;
        return false;
    }

    // 创建 constant buffer（16 字节对齐）
    const uint32_t cbSize = sizeof(ScaleConstantBuffer);
    m_ConstantBuffer = m_RHI->RHICreateUniformBuffer(cbSize);
    if (!m_ConstantBuffer) {
        std::cerr << "[ScaleNode] Failed to create constant buffer" << std::endl;
        return false;
    }

    m_ShaderResourcesInitialized = (m_ConstantBuffer != nullptr &&
                                    m_Shader.VS != nullptr && 
                                    m_Shader.PS != nullptr && 
                                    m_Shader.InputLayout != nullptr);

    return m_ShaderResourcesInitialized;
}

void ScaleNode::CleanupShaderResources() {
    m_ConstantBuffer.reset();
    m_Shader.VS.Reset();
    m_Shader.PS.Reset();
    m_Shader.InputLayout.Reset();
    m_Shader.Blob.Reset();
    m_ShaderResourcesInitialized = false;
}

void ScaleNode::SetZoomParams(double zoomLevel, double panX, double panY) {
    m_ZoomLevel = zoomLevel;
    m_PanX = panX;
    m_PanY = panY;
}

void ScaleNode::GetZoomParams(double& zoomLevel, double& panX, double& panY) const {
    zoomLevel = m_ZoomLevel;
    panX = m_PanX;
    panY = m_PanY;
}

void ScaleNode::SetInputImageSize(uint32_t width, uint32_t height) {
    m_InputImageWidth = width;
    m_InputImageHeight = height;
}

void ScaleNode::UpdateConstantBuffers(uint32_t width, uint32_t height) {
    uint32_t outputWidth = width;
    uint32_t outputHeight = height;
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
    cbData.Padding[0] = 0.0f;
    cbData.Padding[1] = 0.0f;

    // 使用 RHI 接口更新 constant buffer
    m_CommandContext->RHIUpdateUniformBuffer(m_ConstantBuffer, &cbData);
}

void ScaleNode::SetConstantBuffers() {
    // 设置 constant buffer 到 Vertex Shader（Pixel Shader 不需要，因为缩放计算在 VS 中完成）
    if (m_ConstantBuffer) {
        m_CommandContext->RHISetShaderUniformBuffer(RenderCore::EShaderFrequency::SF_Vertex, 0, m_ConstantBuffer);
    }
}

void ScaleNode::SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) {
    // 设置输入纹理
    m_CommandContext->RHISetShaderTexture(RenderCore::EShaderFrequency::SF_Pixel, 0, inputTexture);
    // 设置采样器（使用基类的公共采样器）
    m_CommandContext->RHISetShaderSampler(RenderCore::EShaderFrequency::SF_Pixel, 0, m_CommonSamplerState);
}

bool ScaleNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) {
    if (!m_ShaderResourcesInitialized) {
        return false;
    }

    if (!m_Shader.VS || !m_Shader.PS || !m_Shader.InputLayout || !m_ConstantBuffer) {
        std::cerr << "[ScaleNode] Shader resources not properly initialized" << std::endl;
        return false;
    }

    // 设置当前 shader（基类 Execute 会使用）
    m_CurrentShader = &m_Shader;

    // 使用基类的 Execute 方法（它会调用我们的钩子方法）
    return RenderNode::Execute(inputTexture, outputTarget, width, height);
}

} // namespace LightroomCore

