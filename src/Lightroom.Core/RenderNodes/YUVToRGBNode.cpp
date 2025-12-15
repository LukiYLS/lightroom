#include "YUVToRGBNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include "../d3d11rhi/D3D11StateCachePrivate.h"
#include "../d3d11rhi/RHIUniformBuffer.h"
#include "../d3d11rhi/Common.h"
#include <d3dcompiler.h>
#include <iostream>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

    YUVToRGBNode::YUVToRGBNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
        : RenderNode(rhi)
        , m_YUVFormat(YUVFormat::NV12)
        , m_ShaderResourcesInitialized(false)
    {
    }

    YUVToRGBNode::~YUVToRGBNode() {
        CleanupShaderResources();
    }

    bool YUVToRGBNode::InitializeShaderResources() {
        if (m_ShaderResourcesInitialized) {
            return true;
        }

        if (!CompileShaderForFormat(m_YUVFormat)) {
            std::cerr << "[YUVToRGBNode] Failed to compile shader for format" << std::endl;
            return false;
        }

        // Create constant buffer
        m_ParamsBuffer = m_RHI->RHICreateUniformBuffer(sizeof(YUVToRGBCBuffer));
        if (!m_ParamsBuffer) {
            std::cerr << "[YUVToRGBNode] Failed to create constant buffer" << std::endl;
            return false;
        }

        m_ShaderResourcesInitialized = true;
        return true;
    }

    void YUVToRGBNode::CleanupShaderResources() {
        m_ParamsBuffer.reset();
        m_Shader.VS.Reset();
        m_Shader.PS.Reset();
        m_Shader.InputLayout.Reset();
        m_Shader.Blob.Reset();
        m_ShaderResourcesInitialized = false;
    }

    bool YUVToRGBNode::CompileShaderForFormat(YUVFormat format) {
        // Vertex shader (same for all formats)
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

        // Pixel shader for NV12 format
		// Pixel shader for NV12/NV21 format
		const char* psCodeNV12 = R"(
        Texture2D YTexture : register(t0);
        Texture2D UVTexture : register(t1);
        SamplerState LinearSampler : register(s0);

        cbuffer YUVParams : register(b0) {
            float Width;
            float Height;
            float Padding[2];
        };

        float4 main(float4 position : SV_POSITION, float2 texCoord : TEXCOORD0) : SV_Target {
            // 1. ���� Y (Limited Range ת��)
            float y_raw = YTexture.Sample(LinearSampler, texCoord).r;
            float y = (y_raw - 0.062745) * 1.16438; 

            // 2. ���� UV
            float2 uv_raw = UVTexture.Sample(LinearSampler, texCoord).rg;
             float u = uv_raw.r - 0.5;
             float v = uv_raw.g - 0.5;

            // 3. YUV ת RGB (BT.601)
            float r = y + 1.596 * v;
            float g = y - 0.391 * u - 0.813 * v;
            float b = y + 2.018 * u;

            // 4. ���
            // ȷ�����˳���� float4(r, g, b, 1.0)
            // ��� DirectX Ĭ�� RT �� BGRA���������Ҫ return float4(b, g, r, 1.0);
            return float4(r, g, b, 1.0);
        }
    )";

        // Pixel shader for YUV420P format (separate Y/U/V planes)
		const char* psCodeYUV420P = R"(
            Texture2D YTexture : register(t0);
            Texture2D UTexture : register(t1);
            Texture2D VTexture : register(t2);
            SamplerState LinearSampler : register(s0);

            cbuffer YUVParams : register(b0) {
                float Width;
                float Height;
                float Padding[2];
            };

            float4 main(float4 position : SV_POSITION, float2 texCoord : TEXCOORD0) : SV_Target {
                // 1. ���� Y ������ Limited Range (16-235 -> 0-255)
                // ������ NV12 Shader һ�µ��߼�
                float y_raw = YTexture.Sample(LinearSampler, texCoord).r;
                float y = (y_raw - 0.062745) * 1.16438;

                // 2. ���� U �� V (YUV420P �������������������ǵ�ͨ�� R8)
                float u_raw = UTexture.Sample(LinearSampler, texCoord).r;
                float v_raw = VTexture.Sample(LinearSampler, texCoord).r;

                float u = u_raw - 0.5;
                float v = v_raw - 0.5;

                // 3. YUV ת RGB (BT.601 Limited Range ϵ��)
                // ��Ȼ NV12 ������ϵ���ǶԵģ�����Ҳ����������
                float r = y + 1.596 * v;
                float g = y - 0.391 * u - 0.813 * v;
                float b = y + 2.018 * u;

                // 4. ���
                // ����Ϊ RGB ˳���� NV12 ����һ��
                return float4(r, g, b, 1.0);
            }
        )";

        const char* psCode = (format == YUVFormat::NV12) ? psCodeNV12 : psCodeYUV420P;

        // Use base class CompileShaders method
        if (!CompileShaders(vsCode, psCode, m_Shader)) {
            std::cerr << "[YUVToRGBNode] Failed to compile shaders" << std::endl;
            return false;
        }

        return true;
    }

    bool YUVToRGBNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
        uint32_t width, uint32_t height) {
        if (!m_ShaderResourcesInitialized) {
            if (!InitializeShaderResources()) {
                return false;
            }
        }

        if (!m_Shader.VS || !m_Shader.PS || !m_Shader.InputLayout || !m_ParamsBuffer) {
            std::cerr << "[YUVToRGBNode] Shader resources not properly initialized" << std::endl;
            return false;
        }

        // Set current shader (base class Execute will use it)
        m_CurrentShader = &m_Shader;

        // Use base class Execute method (it will call our hook methods)
        return RenderNode::Execute(inputTexture, outputTarget, width, height);
    }

    bool YUVToRGBNode::ExecuteMultiTexture(
        std::shared_ptr<RenderCore::RHITexture2D> yTexture,
        std::shared_ptr<RenderCore::RHITexture2D> uTexture,
        std::shared_ptr<RenderCore::RHITexture2D> vTexture,
        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
        uint32_t width, uint32_t height) {
        // For now, this is a placeholder - would need to bind multiple textures
        // For NV12, we use Execute with single texture
        return false;
    }

    void YUVToRGBNode::UpdateConstantBuffers(uint32_t width, uint32_t height) {
        if (!m_ParamsBuffer) {
            return;
        }

        YUVToRGBCBuffer cbData;
        cbData.Width = static_cast<float>(width);
        cbData.Height = static_cast<float>(height);
        cbData.Padding[0] = 0.0f;
        cbData.Padding[1] = 0.0f;

        m_CommandContext->RHIUpdateUniformBuffer(m_ParamsBuffer, &cbData);
    }

    void YUVToRGBNode::SetConstantBuffers() {
        if (m_ParamsBuffer) {
            m_CommandContext->RHISetShaderUniformBuffer(RenderCore::EShaderFrequency::SF_Pixel, 0, m_ParamsBuffer);
        }
    }

    void YUVToRGBNode::SetCustomShaderResourceViews(ID3D11ShaderResourceView* ySRV, ID3D11ShaderResourceView* uvSRV) {
        m_CustomYSRV = ySRV;
        m_CustomUVSRV = uvSRV;
        // Clear YUV420P SRVs when setting NV12 SRVs
        m_CustomUSRV = nullptr;
        m_CustomVSRV = nullptr;
    }
    
    void YUVToRGBNode::SetCustomShaderResourceViewsYUV420P(ID3D11ShaderResourceView* ySRV, ID3D11ShaderResourceView* uSRV, ID3D11ShaderResourceView* vSRV) {
        m_CustomYSRV = ySRV;
        m_CustomUSRV = uSRV;
        m_CustomVSRV = vSRV;
        // Clear NV12 SRV when setting YUV420P SRVs
        m_CustomUVSRV = nullptr;
    }

    void YUVToRGBNode::SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) {
        // Get D3D11 RHI to access StateCache
        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
        if (!d3d11RHI) {
            if (inputTexture) {
                m_CommandContext->RHISetShaderTexture(RenderCore::EShaderFrequency::SF_Pixel, 0, inputTexture);
            }
            m_CommandContext->RHISetShaderSampler(RenderCore::EShaderFrequency::SF_Pixel, 0, m_CommonSamplerState);
            return;
        }
        
        auto& stateCache = d3d11RHI->GetStateCache();
        
        // If custom SRV are set (for NV12 format), use them directly
        if (m_CustomYSRV && m_CustomUVSRV && m_YUVFormat == YUVFormat::NV12) {
            // Set Y texture (slot 0)
            stateCache.SetShaderResourceView<RenderCore::SF_Pixel>(m_CustomYSRV, 0);
            // Set UV texture (slot 1)
            stateCache.SetShaderResourceView<RenderCore::SF_Pixel>(m_CustomUVSRV, 1);
        }
        // If custom SRV are set (for YUV420P format), use them directly
        else if (m_CustomYSRV && m_CustomUSRV && m_CustomVSRV && m_YUVFormat == YUVFormat::YUV420P) {
            // Set Y texture (slot 0)
            stateCache.SetShaderResourceView<RenderCore::SF_Pixel>(m_CustomYSRV, 0);
            // Set U texture (slot 1)
            stateCache.SetShaderResourceView<RenderCore::SF_Pixel>(m_CustomUSRV, 1);
            // Set V texture (slot 2)
            stateCache.SetShaderResourceView<RenderCore::SF_Pixel>(m_CustomVSRV, 2);
        }
        else if (inputTexture) {
            // Fallback to normal texture binding
            m_CommandContext->RHISetShaderTexture(RenderCore::EShaderFrequency::SF_Pixel, 0, inputTexture);
        }

        // Set sampler (use base class common sampler)
        m_CommandContext->RHISetShaderSampler(RenderCore::EShaderFrequency::SF_Pixel, 0, m_CommonSamplerState);
    }

} // namespace LightroomCore






