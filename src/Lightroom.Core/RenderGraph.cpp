#include "RenderGraph.h"
#include "VideoProcessing/VideoPerformanceProfiler.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include <iostream>

namespace LightroomCore {

	RenderGraph::RenderGraph(std::shared_ptr<RenderCore::DynamicRHI> rhi)
		: m_RHI(rhi)
	{
	}

	RenderGraph::~RenderGraph() {
		Clear();
	}

	void RenderGraph::AddNode(std::shared_ptr<RenderNode> node) {
		if (node) {
			m_Nodes.push_back(node);
		}
	}

	void RenderGraph::Clear() {
		m_Nodes.clear();
		m_TexturePool.clear();
	}

	bool RenderGraph::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
		std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
		uint32_t width, uint32_t height) {
		if (!m_RHI || !inputTexture || !outputTarget || m_Nodes.empty()) {
			return false;
		}

		if (m_Nodes.size() == 1) {
			return m_Nodes[0]->Execute(inputTexture, outputTarget, width, height);
		}

		std::shared_ptr<RenderCore::RHITexture2D> currentInput = inputTexture;
		std::shared_ptr<RenderCore::RHITexture2D> currentOutput = nullptr;

		size_t poolIndex = 0;
		for (size_t i = 0; i < m_Nodes.size(); ++i) {
			bool isLastNode = (i == m_Nodes.size() - 1);

			if (isLastNode) {
				currentOutput = outputTarget;
			}
			else {
				currentOutput = GetCachedTexture(width, height, poolIndex);
				poolIndex++;
				if (!currentOutput) {
					return false;
				}
			}

			{
				if (!m_Nodes[i]->Execute(currentInput, currentOutput, width, height)) {
					return false;
				}
			}
			currentInput = currentOutput;
		}

		return true;
	}

	std::shared_ptr<RenderCore::RHITexture2D> RenderGraph::GetCachedTexture(uint32_t width, uint32_t height, size_t index) {
		if (index < m_TexturePool.size()) {
			auto existingTex = m_TexturePool[index];
			if (existingTex) {
				// 2. 检查尺寸是否匹配
				auto* d3dTex = dynamic_cast<RenderCore::D3D11Texture2D*>(existingTex.get());
				if (d3dTex) {
					D3D11_TEXTURE2D_DESC desc;
					d3dTex->GetNativeTex()->GetDesc(&desc);
					if (desc.Width == width && desc.Height == height) {
						return existingTex;
					}
				}
			}
		}

		auto newTexture = m_RHI->RHICreateTexture2D(
			RenderCore::EPixelFormat::PF_B8G8R8A8,
			RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
			width,
			height,
			1
		);

		if (!newTexture)
			return nullptr;

		if (index < m_TexturePool.size()) {
			m_TexturePool[index] = newTexture;
		}
		else {
			m_TexturePool.push_back(newTexture);
		}
		return newTexture;
	}

} // namespace LightroomCore