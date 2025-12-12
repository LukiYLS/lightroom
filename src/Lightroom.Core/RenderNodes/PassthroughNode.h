#pragma once

#include "RenderNode.h"
#include "../d3d11rhi/RHITexture2D.h"
#include <memory>

namespace LightroomCore {

// 直通节点：直接复制输入到输出
class PassthroughNode : public RenderNode {
public:
    PassthroughNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~PassthroughNode();

    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) override;

    virtual const char* GetName() const override { return "Passthrough"; }
};

} // namespace LightroomCore

