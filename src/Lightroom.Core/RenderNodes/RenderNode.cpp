#include "RenderNode.h"

namespace LightroomCore {

RenderNode::RenderNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : m_RHI(rhi)
{
    if (m_RHI) {
        m_CommandContext = m_RHI->GetDefaultCommandContext();
    }
}

RenderNode::~RenderNode() {
}

} // namespace LightroomCore


