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

    // 如果只有一个节点，直接执行
    if (m_Nodes.size() == 1) {
        if (auto* computeNode = std::get_if<std::shared_ptr<ComputeNode>>(&m_Nodes[0])) {
            return (*computeNode)->Execute(inputTexture, outputTarget, width, height);
        } else if (auto* renderNode = std::get_if<std::shared_ptr<RenderNode>>(&m_Nodes[0])) {
            return (*renderNode)->Execute(inputTexture, outputTarget, width, height);
        }
        return false;
    }

    // 多个节点：需要中间纹理
    std::shared_ptr<RenderCore::RHITexture2D> currentInput = inputTexture;
    std::shared_ptr<RenderCore::RHITexture2D> currentOutput = nullptr;

    for (size_t i = 0; i < m_Nodes.size(); ++i) {
        bool isLastNode = (i == m_Nodes.size() - 1);
        bool isComputeNode = std::holds_alternative<std::shared_ptr<ComputeNode>>(m_Nodes[i]);
        bool nextIsComputeNode = (i + 1 < m_Nodes.size()) && 
                                 std::holds_alternative<std::shared_ptr<ComputeNode>>(m_Nodes[i + 1]);

        if (isLastNode) {
            // 最后一个节点：输出到最终目标
            currentOutput = outputTarget;
        } else {
            // 中间节点：根据下一个节点类型创建合适的中间纹理
            if (nextIsComputeNode) {
                // 下一个是 ComputeNode，需要 UAV 支持
                currentOutput = CreateIntermediateTextureUAV(width, height);
            } else {
                // 下一个是 RenderNode，普通纹理即可
                currentOutput = CreateIntermediateTexture(width, height);
            }
            if (!currentOutput) {
                return false;
            }
        }

        // 执行节点
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

        // 下一节点的输入是当前节点的输出
        currentInput = currentOutput;
    }

    return true;
}

std::shared_ptr<RenderCore::RHITexture2D> RenderGraph::CreateIntermediateTexture(uint32_t width, uint32_t height) {
    // 创建中间纹理（用于 Pixel Shader 节点之间的传递）
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
    // 创建中间纹理（用于 Compute Shader 节点，需要 UAV 支持）
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












