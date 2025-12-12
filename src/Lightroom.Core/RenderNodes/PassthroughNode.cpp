#include "PassthroughNode.h"

namespace LightroomCore {

PassthroughNode::PassthroughNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : RenderNode(rhi)
{
}

PassthroughNode::~PassthroughNode() {
}

bool PassthroughNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                              std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                              uint32_t width, uint32_t height) {
    if (!m_CommandContext || !inputTexture || !outputTarget) {
        return false;
    }

    // 直接使用 RHI 接口复制资源
    m_CommandContext->RHICopyResource(outputTarget, inputTexture);
    m_CommandContext->FlushCommands();
    return true;
}

} // namespace LightroomCore


