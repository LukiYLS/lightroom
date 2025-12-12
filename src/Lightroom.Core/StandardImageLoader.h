#pragma once

#include "ImageLoader.h"
#include <vector>

namespace LightroomCore {

// 标准图片加载器（使用 WIC 加载 JPEG, PNG, BMP 等格式）
class StandardImageLoader : public IImageLoader {
public:
    StandardImageLoader();
    ~StandardImageLoader() override;

    bool CanLoad(const std::wstring& filePath) override;
    std::shared_ptr<RenderCore::RHITexture2D> Load(
        const std::wstring& filePath,
        std::shared_ptr<RenderCore::DynamicRHI> rhi) override;
    ImageFormat GetFormat() const override { return ImageFormat::Standard; }
    void GetImageInfo(uint32_t& width, uint32_t& height) const override;

private:
    // 使用 WIC 加载图片数据到内存
    bool LoadImageDataWithWIC(const std::wstring& imagePath, 
                               std::vector<uint8_t>& outData, 
                               uint32_t& outWidth, 
                               uint32_t& outHeight, 
                               uint32_t& outStride);

    uint32_t m_LastImageWidth;
    uint32_t m_LastImageHeight;

    // 检查文件扩展名是否为标准图片格式
    bool IsStandardImageFormat(const std::wstring& filePath) const;
};

} // namespace LightroomCore


