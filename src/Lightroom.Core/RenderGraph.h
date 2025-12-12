#pragma once

#include "RenderNodes/RenderNode.h"
#include "d3d11rhi/DynamicRHI.h"
#include <memory>
#include <vector>

namespace LightroomCore {

// 渲染图：管理多个渲染节点的执行顺序
class RenderGraph {
public:
    RenderGraph(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~RenderGraph();

    // 添加渲染节点
    void AddNode(std::shared_ptr<RenderNode> node);

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

    // 获取指定索引的节点
    std::shared_ptr<RenderNode> GetNode(size_t index) const {
        if (index < m_Nodes.size()) {
            return m_Nodes[index];
        }
        return nullptr;
    }

private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    std::vector<std::shared_ptr<RenderNode>> m_Nodes;
    
    // 中间纹理缓存（用于节点之间的传递）
    std::vector<std::shared_ptr<RenderCore::RHITexture2D>> m_IntermediateTextures;
    
    // 创建中间纹理
    std::shared_ptr<RenderCore::RHITexture2D> CreateIntermediateTexture(uint32_t width, uint32_t height);
};

} // namespace LightroomCore


