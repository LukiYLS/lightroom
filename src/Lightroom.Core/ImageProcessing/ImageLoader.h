#pragma once

#include "../d3d11rhi/DynamicRHI.h"
#include <string>
#include <memory>

namespace LightroomCore {

// 图片格式枚举
enum class ImageFormat {
    Unknown,
    Standard,  // JPEG, PNG, BMP, etc. (WIC)
    RAW        // CR2, NEF, ARW, DNG, etc. (LibRaw)
};

// 图片加载器接口（策略模式）
class IImageLoader {
public:
    virtual ~IImageLoader() = default;

    // 检查是否可以加载指定文件
    virtual bool CanLoad(const std::wstring& filePath) = 0;

    // 加载图片到 RHI 纹理
    // 返回的纹理由调用者管理生命周期
    virtual std::shared_ptr<RenderCore::RHITexture2D> Load(
        const std::wstring& filePath,
        std::shared_ptr<RenderCore::DynamicRHI> rhi) = 0;

    // 获取图片格式
    virtual ImageFormat GetFormat() const = 0;

    // 获取最后加载的图片尺寸
    virtual void GetImageInfo(uint32_t& width, uint32_t& height) const = 0;
};

} // namespace LightroomCore


