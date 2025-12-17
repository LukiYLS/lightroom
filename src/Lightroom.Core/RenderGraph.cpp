#include "RenderGraph.h"
#include "VideoProcessing/VideoPerformanceProfiler.h"

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

void RenderGraph::AddComputeNode(std::shared_ptr<ComputeNode> node) {
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

    // If only one node, execute directly
    if (m_Nodes.size() == 1) {
        if (auto* computeNode = std::get_if<std::shared_ptr<ComputeNode>>(&m_Nodes[0])) {
            return (*computeNode)->Execute(inputTexture, outputTarget, width, height);
        } else if (auto* renderNode = std::get_if<std::shared_ptr<RenderNode>>(&m_Nodes[0])) {
            return (*renderNode)->Execute(inputTexture, outputTarget, width, height);
        }
        return false;
    }

    // Multiple nodes: need intermediate textures
    std::shared_ptr<RenderCore::RHITexture2D> currentInput = inputTexture;
    std::shared_ptr<RenderCore::RHITexture2D> currentOutput = nullptr;

    for (size_t i = 0; i < m_Nodes.size(); ++i) {
        bool isLastNode = (i == m_Nodes.size() - 1);
        bool isComputeNode = std::holds_alternative<std::shared_ptr<ComputeNode>>(m_Nodes[i]);
        bool nextIsComputeNode = (i + 1 < m_Nodes.size()) && 
                                 std::holds_alternative<std::shared_ptr<ComputeNode>>(m_Nodes[i + 1]);

        if (isLastNode) {
            // Last node: output to final target
            currentOutput = outputTarget;
        } else {
            // Intermediate node: create appropriate intermediate texture based on node type
            if (isComputeNode) {
                // Current is ComputeNode, output texture must have UAV support (also need SRV for next node to read)
                currentOutput = CreateIntermediateTextureUAV(width, height);
            } else {
                // Current is RenderNode, decide based on next node type
                if (nextIsComputeNode) {
                    // Next is ComputeNode, need UAV support
                    currentOutput = CreateIntermediateTextureUAV(width, height);
                } else {
                    // Next is RenderNode, normal texture is fine
                    currentOutput = CreateIntermediateTexture(width, height);
                }
            }
            if (!currentOutput) {
                return false;
            }
        }

        // Execute node
        bool success = false;
        
        if (isComputeNode) {
            auto* computeNode = std::get_if<std::shared_ptr<ComputeNode>>(&m_Nodes[i]);
            if (computeNode && *computeNode) {
                using namespace LightroomCore;
                std::string timerName = std::string("RenderGraph_ComputeNode_") + (*computeNode)->GetName();
                ScopedTimer nodeTimer(timerName);
                success = (*computeNode)->Execute(currentInput, currentOutput, width, height);
            }
        } else {
            auto* renderNode = std::get_if<std::shared_ptr<RenderNode>>(&m_Nodes[i]);
            if (renderNode && *renderNode) {
                using namespace LightroomCore;
                std::string timerName = std::string("RenderGraph_Node_") + (*renderNode)->GetName();
                ScopedTimer nodeTimer(timerName);
                success = (*renderNode)->Execute(currentInput, currentOutput, width, height);
            }
        }

        if (!success) {
            return false;
        }

        // Next node's input is current node's output
        currentInput = currentOutput;
    }

    return true;
}

std::shared_ptr<RenderCore::RHITexture2D> RenderGraph::CreateIntermediateTexture(uint32_t width, uint32_t height) {
    // Create intermediate texture (for Pixel Shader node transfers)
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

std::shared_ptr<RenderCore::RHITexture2D> RenderGraph::CreateIntermediateTextureUAV(uint32_t width, uint32_t height) {
    // Create intermediate texture (for Compute Shader nodes, need UAV support)
    auto texture = m_RHI->RHICreateTexture2D(
        RenderCore::EPixelFormat::PF_B8G8R8A8,
        RenderCore::ETextureCreateFlags::TexCreate_UAV | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
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
