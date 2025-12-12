#pragma once

#include "../d3d11rhi/DynamicRHI.h"
#include "../d3d11rhi/RHICommandContext.h"
#include <memory>

namespace LightroomCore {

// 渲染节点基类：支持不同的图片处理 shader
class RenderNode {
public:
    RenderNode(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    virtual ~RenderNode();

    // 执行渲染
    // inputTexture: 输入纹理
    // outputTarget: 输出渲染目标纹理
    // width, height: 输出尺寸
    virtual bool Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                        std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                        uint32_t width, uint32_t height) = 0;

    // 获取节点名称（用于调试）
    virtual const char* GetName() const = 0;

protected:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    std::shared_ptr<RenderCore::RHICommandContext> m_CommandContext;
};

} // namespace LightroomCore

