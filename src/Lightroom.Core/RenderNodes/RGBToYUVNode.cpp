#include "RGBToYUVNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11UniformBuffer.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include <d3dcompiler.h>
#include <iostream>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

	// YUV 转换参数 (BT.601 Limited Range)
	// 16字节对齐
	struct __declspec(align(16)) YUVConvertConstants {
		float Resolution[2]; // width, height
		float Padding[2];
	};

	RGBToYUVNode::RGBToYUVNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
		: RenderNode(rhi)
		, m_ShaderResourcesInitialized(false)
	{
		InitializeShaderResources();
	}

	RGBToYUVNode::~RGBToYUVNode() {
		CleanupShaderResources();
	}

	bool RGBToYUVNode::InitializeShaderResources() {
		if (m_ShaderResourcesInitialized || !m_RHI) return true;

		// ----------------------------------------------------------------------------------
		// 1. Vertex Shader (通用全屏三角形)
		// ----------------------------------------------------------------------------------
		// 2. Vertex Shader (SV_VertexID 技术，无需 InputLayout)
		const char* vsCode = R"(
            struct VSOutput { float4 Pos : SV_POSITION; float2 UV : TEXCOORD0; };
            VSOutput main(uint id : SV_VertexID) {
                VSOutput output;
                output.UV = float2((id << 1) & 2, id & 2);
                output.Pos = float4(output.UV * float2(2, -2) + float2(-1, 1), 0, 1);
                return output;
            }
        )";

		// 3. Pixel Shader - Y Plane
		// [修正] 直接采样 .rgb。不要手动交换 R/B，否则人脸会变蓝。
		const char* psCodeY = R"(
            Texture2D RGBTexture : register(t0);
            SamplerState LinearSampler : register(s0);
            float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
                float3 rgb = RGBTexture.Sample(LinearSampler, uv).rgb;
                float y = dot(rgb, float3(0.299, 0.587, 0.114));
                // [0,1] -> [16, 235]
                y = (y * 219.0 / 255.0) + (16.0 / 255.0);
                return float4(y, y, y, 1.0);
            }
        )";

		// 4. Pixel Shader - U Plane (自动 2x2 插值)
		const char* psCodeU = R"(
            Texture2D RGBTexture : register(t0);
            SamplerState LinearSampler : register(s0);
            float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
                float3 rgb = RGBTexture.Sample(LinearSampler, uv).rgb;
                float u = dot(rgb, float3(-0.14713, -0.28886, 0.436));
                // [-0.5, 0.5] -> [0, 1] -> [16, 240]
                u = (u + 0.5) * (224.0 / 255.0) + (16.0 / 255.0);
                return float4(u, u, u, 1.0);
            }
        )";

		// 5. Pixel Shader - V Plane
		const char* psCodeV = R"(
            Texture2D RGBTexture : register(t0);
            SamplerState LinearSampler : register(s0);
            float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
                float3 rgb = RGBTexture.Sample(LinearSampler, uv).rgb;
                float v = dot(rgb, float3(0.615, -0.51499, -0.10001));
                // [-0.5, 0.5] -> [0, 1] -> [16, 240]
                v = (v + 0.5) * (224.0 / 255.0) + (16.0 / 255.0);
                return float4(v, v, v, 1.0);
            }
        )";

		// 编译 Shaders
		if (!CompileShaders(vsCode, psCodeY, m_YShader)) return false;
		if (!CompileShaders(vsCode, psCodeU, m_UShader)) return false;
		if (!CompileShaders(vsCode, psCodeV, m_VShader)) return false;

		// 创建常量缓冲区 (虽然现在的简化 Shader 暂时没用到 Resolution，但保留以备后续修正采样偏移)
		m_ParamsBuffer = m_RHI->RHICreateUniformBuffer(sizeof(YUVConvertConstants));

		// 初始化采样器 (必须是 Linear Clamp)
		// 假设 RenderNode 基类中已经创建了 m_CommonSamplerState 且为 Linear
		// 如果没有，需要在这里创建一个

		m_ShaderResourcesInitialized = true;
		return true;
	}

	void RGBToYUVNode::CleanupShaderResources() {
		m_ParamsBuffer.reset();
		m_YShader.VS.Reset();
		m_YShader.PS.Reset();
		m_YShader.InputLayout.Reset();
		m_YShader.Blob.Reset();
		m_UShader.VS.Reset();
		m_UShader.PS.Reset();
		m_UShader.InputLayout.Reset();
		m_UShader.Blob.Reset();
		m_VShader.VS.Reset();
		m_VShader.PS.Reset();
		m_VShader.InputLayout.Reset();
		m_VShader.Blob.Reset();
		m_ShaderResourcesInitialized = false;
	}

	bool RGBToYUVNode::Execute(
		std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
		std::shared_ptr<RenderCore::RHITexture2D> yTexture,
		std::shared_ptr<RenderCore::RHITexture2D> uTexture,
		std::shared_ptr<RenderCore::RHITexture2D> vTexture,
		uint32_t width, uint32_t height)
	{
		if (!m_ShaderResourcesInitialized || !inputTexture || !yTexture || !uTexture || !vTexture) {
			return false;
		}

		if (!m_CommandContext || !m_CommonResourcesInitialized) {
			return false;
		}

		auto* d3dRHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
		if (!d3dRHI) {
			return false;
		}
		
		ID3D11DeviceContext* context = d3dRHI->GetDeviceContext();
		if (!context) {
			return false;
		}

		// -------------------------------------------------------------------------
		// Pass 1: Render Y Plane
		// -------------------------------------------------------------------------
		m_CurrentShader = &m_YShader;
		m_CommandContext->SetRenderTarget(yTexture, nullptr);
		m_CommandContext->SetViewPort(0, 0, width, height);
		m_CommandContext->Clear(yTexture, nullptr, core::FLinearColor(0, 0, 0, 1));

		// 设置 shader 和渲染状态（SetupRenderState 会调用 SetShaderResources 设置纹理）
		SetupRenderState(context, inputTexture, width, height);
		Draw(context);

		// -------------------------------------------------------------------------
		// Pass 2: Render U Plane (Half Res)
		// -------------------------------------------------------------------------
		m_CurrentShader = &m_UShader;
		m_CommandContext->SetRenderTarget(uTexture, nullptr);
		m_CommandContext->SetViewPort(0, 0, width / 2, height / 2);
		m_CommandContext->Clear(uTexture, nullptr, core::FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));

		SetupRenderState(context, inputTexture, width / 2, height / 2);
		Draw(context);

		// -------------------------------------------------------------------------
		// Pass 3: Render V Plane (Half Res)
		// -------------------------------------------------------------------------
		m_CurrentShader = &m_VShader;
		m_CommandContext->SetRenderTarget(vTexture, nullptr);
		m_CommandContext->SetViewPort(0, 0, width / 2, height / 2);
		m_CommandContext->Clear(vTexture, nullptr, core::FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));

		SetupRenderState(context, inputTexture, width / 2, height / 2);
		Draw(context);

		// 必须 Flush 确保渲染完成，否则读取时可能读到未完成的数据
		m_CommandContext->FlushCommands();
		context->Flush();

		// 清理 SRV 绑定，防止后续将此纹理作为 RenderTarget 时报 Warning
		ID3D11ShaderResourceView* nullSRV[] = { nullptr };
		context->PSSetShaderResources(0, 1, nullSRV);

		return true;
	}

	void RGBToYUVNode::UpdateConstantBuffers(uint32_t width, uint32_t height) {
		// 当前实现不需要 constant buffer，保留空实现以满足基类接口
	}

	void RGBToYUVNode::SetConstantBuffers() {
		// 当前实现不需要 constant buffer，保留空实现以满足基类接口
	}

	void RGBToYUVNode::SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) {
		// 设置输入纹理和采样器
		if (inputTexture && m_CommandContext) {
			m_CommandContext->RHISetShaderTexture(RenderCore::EShaderFrequency::SF_Pixel, 0, inputTexture);
			m_CommandContext->RHISetShaderSampler(RenderCore::EShaderFrequency::SF_Pixel, 0, m_CommonSamplerState);
		}
	}
} // namespace LightroomCore