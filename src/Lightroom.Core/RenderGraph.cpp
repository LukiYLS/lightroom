#include "RenderGraph.h"
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
    m_IntermediateTextures.clear();
}

bool RenderGraph::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                          std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                          uint32_t width, uint32_t height) {
    if (!m_RHI || !inputTexture || !outputTarget || m_Nodes.empty()) {
        return false;
    }

    // 如果只有一个节点，直接执行
    if (m_Nodes.size() == 1) {
        return m_Nodes[0]->Execute(inputTexture, outputTarget, width, height);
    }

    // 多个节点：需要中间纹理
    std::shared_ptr<RenderCore::RHITexture2D> currentInput = inputTexture;
    std::shared_ptr<RenderCore::RHITexture2D> currentOutput = nullptr;

    for (size_t i = 0; i < m_Nodes.size(); ++i) {
        bool isLastNode = (i == m_Nodes.size() - 1);

        if (isLastNode) {
            // 最后一个节点：输出到最终目标
            currentOutput = outputTarget;
        } else {
            // 中间节点：创建中间纹理
            currentOutput = CreateIntermediateTexture(width, height);
            if (!currentOutput) {
                std::cerr << "[RenderGraph] Failed to create intermediate texture for node " << i << std::endl;
                return false;
            }
        }

        // 执行节点
        if (!m_Nodes[i]->Execute(currentInput, currentOutput, width, height)) {
            std::cerr << "[RenderGraph] Node " << i << " (" << m_Nodes[i]->GetName() << ") failed" << std::endl;
            return false;
        }

        // 下一节点的输入是当前节点的输出
        currentInput = currentOutput;
    }

    return true;
}

std::shared_ptr<RenderCore::RHITexture2D> RenderGraph::CreateIntermediateTexture(uint32_t width, uint32_t height) {
    // 创建中间纹理（用于节点之间的传递）
    auto texture = m_RHI->RHICreateTexture2D(
        RenderCore::EPixelFormat::PF_B8G8R8A8,
        RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
        width,
        height,
        1  // NumMips
    );

    if (texture) {
        m_IntermediateTextures.push_back(texture);
    }

    return texture;
}

} // namespace LightroomCore



