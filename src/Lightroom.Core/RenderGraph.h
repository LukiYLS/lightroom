#pragma once

#include "RenderNodes/RenderNode.h"
#include "RenderNodes/ComputeNode.h"
#include "d3d11rhi/DynamicRHI.h"
#include <memory>
#include <vector>
#include <variant>

namespace LightroomCore {

// 节点类型：可以是 ComputeNode 或 RenderNode
using NodeVariant = std::variant<std::shared_ptr<ComputeNode>, std::shared_ptr<RenderNode>>;

// 渲染图：管理多个渲染节点的执行顺序（支持 Compute Shader 和 Pixel Shader 混合）
class RenderGraph {
public:
    RenderGraph(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~RenderGraph();

    // 添加渲染节点（Pixel Shader）
    void AddNode(std::shared_ptr<RenderNode> node);

    // 添加计算节点（Compute Shader）
    void AddComputeNode(std::shared_ptr<ComputeNode> node);

    // 清除所有节点
    void Clear();

    // 执行渲染图
    // inputTexture: 输入纹理
    // outputTarget: 输出渲染目标纹理
    // width, height: 输出尺寸
    bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                 std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                 uint32_t width, uint32_t height);

    // 获取节点数量
    size_t GetNodeCount() const { return m_Nodes.size(); }

    // 获取指定索引的节点（返回 RenderNode，如果是 ComputeNode 则返回 nullptr）
    std::shared_ptr<RenderNode> GetNode(size_t index) const {
        if (index < m_Nodes.size()) {
            if (auto* renderNode = std::get_if<std::shared_ptr<RenderNode>>(&m_Nodes[index])) {
                return *renderNode;
            }
        }
        return nullptr;
    }

private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    std::vector<NodeVariant> m_Nodes;
    
    // 中间纹理缓存（用于节点之间的传递）
    std::vector<std::shared_ptr<RenderCore::RHITexture2D>> m_IntermediateTextures;
    
    // 创建中间纹理（用于 Pixel Shader 节点）
    std::shared_ptr<RenderCore::RHITexture2D> CreateIntermediateTexture(uint32_t width, uint32_t height);
    
    // 创建中间纹理（用于 Compute Shader 节点，需要 UAV 支持）
    std::shared_ptr<RenderCore::RHITexture2D> CreateIntermediateTextureUAV(uint32_t width, uint32_t height);
};

} // namespace LightroomCore


