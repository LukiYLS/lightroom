#include "ComputeNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11CommandContext.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include "../d3d11rhi/D3D11UnorderedAccessView.h"
#include "../d3d11rhi/D3D11ReourceTraits.h"
#include "../d3d11rhi/RHIUniformBuffer.h"
#include <d3dcompiler.h>
#include <Windows.h>
#include <wrl/client.h>
#include <cstdio>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

	ComputeNode::ComputeNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
		: m_RHI(rhi)
	{
		if (m_RHI) {
			m_CommandContext = m_RHI->GetDefaultCommandContext();
		}
	}

	ComputeNode::~ComputeNode() {
		m_ComputeShader.reset();
		m_OutputUAV.reset();
	}

	void ComputeNode::SetConstantBuffers() {
		std::shared_ptr<RenderCore::RHIUniformBuffer> constantBuffer = GetConstantBuffer();
		if (constantBuffer) {
			m_CommandContext->RHISetShaderUniformBuffer(
				RenderCore::EShaderFrequency::SF_Compute,
				0,
				constantBuffer
			);
		}
	}

	void ComputeNode::SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) {
		if (!inputTexture) {
			return;
		}
		m_CommandContext->RHISetShaderTexture(
			RenderCore::EShaderFrequency::SF_Compute,
			0,
			inputTexture
		);
	}

	void ComputeNode::SetUnorderedAccessViews(std::shared_ptr<RenderCore::RHITexture2D> outputTexture) {
		if (!outputTexture) {
			return;
		}

		// Create or update UAV
		if (!m_OutputUAV) {
			m_OutputUAV = m_RHI->RHICreateUnorderedAccessView(outputTexture);
			if (!m_OutputUAV) {
				return;
			}
		}
		else {
			auto currentTexture = m_OutputUAV->GetTexture2D();
			if (currentTexture != outputTexture) {
				m_OutputUAV = m_RHI->RHICreateUnorderedAccessView(outputTexture);
				if (!m_OutputUAV) {
					return;
				}
			}
		}

		if (m_OutputUAV) {
			m_CommandContext->RHISetUAVParameter(0, m_OutputUAV);
		}
	}

	void ComputeNode::SetComputeShader() {
		if (!m_ComputeShaderInitialized) {
			if (!InitializeComputeShader(nullptr)) {
				return;
			}
		}
		ID3D11ComputeShader* computeShader = GetD3D11ComputeShader();
		if (!computeShader) {
			return;
		}

		RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
		if (!d3d11RHI) {
			return;
		}

		ID3D11DeviceContext* d3d11Context = d3d11RHI->GetDeviceContext();
		if (d3d11Context) {
			d3d11Context->CSSetShader(computeShader, nullptr, 0);
		}
	}

	bool ComputeNode::InitializeComputeShader(const char* csCode) {
		if (!m_RHI || m_ComputeShaderInitialized) {
			return m_ComputeShaderInitialized;
		}

		m_ComputeShaderInitialized = true;
		return true;
	}

	bool ComputeNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
		std::shared_ptr<RenderCore::RHITexture2D> outputTexture,
		uint32_t width, uint32_t height) {
		if (!m_CommandContext || !inputTexture || !outputTexture) {
			return false;
		}
		RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_RHI.get());
		if (!d3d11RHI) {
			return false;
		}

		ID3D11DeviceContext* d3d11Context = d3d11RHI->GetDeviceContext();
		if (!d3d11Context) {
			return false;
		}

		UpdateConstantBuffers(width, height);
		SetComputeShader();		
		SetConstantBuffers();
		SetShaderResources(inputTexture);

		SetUnorderedAccessViews(outputTexture); 

		uint32_t threadGroupCountX = (width + m_ThreadGroupSizeX - 1) / m_ThreadGroupSizeX;
		uint32_t threadGroupCountY = (height + m_ThreadGroupSizeY - 1) / m_ThreadGroupSizeY;

		m_CommandContext->RHIDispatchComputeShader(threadGroupCountX, threadGroupCountY, 1);

		ID3D11UnorderedAccessView* nullUAV = nullptr;
		d3d11Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		ID3D11ShaderResourceView* nullSRV = nullptr;
		d3d11Context->CSSetShaderResources(0, 1, &nullSRV);
		d3d11Context->CSSetShader(nullptr, nullptr, 0);


		m_CommandContext->FlushCommands();


		return true;
	}

} // namespace LightroomCore
