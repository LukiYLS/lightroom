#pragma once

#include "ImageLoader.h"
#include "RAWImageInfo.h"
#include "LibRawWrapper.h"
#include <vector>
#include <memory>

namespace LightroomCore {

// RAW 图片加载器（使用 LibRaw）
class RAWImageLoader : public IImageLoader {
public:
    RAWImageLoader();
    ~RAWImageLoader() override;

    bool CanLoad(const std::wstring& filePath) override;
    std::shared_ptr<RenderCore::RHITexture2D> Load(
        const std::wstring& filePath,
        std::shared_ptr<RenderCore::DynamicRHI> rhi) override;
    ImageFormat GetFormat() const override { return ImageFormat::RAW; }
    void GetImageInfo(uint32_t& width, uint32_t& height) const override;

    // RAW-specific methods
    const RAWImageInfo& GetRAWInfo() const { return m_RAWInfo; }
    
    // 加载 RAW 数据（16-bit Bayer pattern）
    // 返回 true 表示成功，rawData 包含原始 Bayer 数据
    bool LoadRAWData(const std::wstring& filePath, 
                     std::vector<uint16_t>& rawData,
                     uint32_t& outWidth,
                     uint32_t& outHeight);

private:
    RAWImageInfo m_RAWInfo;
    uint32_t m_LastImageWidth;
    uint32_t m_LastImageHeight;
    std::unique_ptr<LibRawWrapper> m_LibRawWrapper;

    // 检查文件扩展名是否为 RAW 格式
    bool IsRAWFormat(const std::wstring& filePath) const;

    // 从文件路径提取元数据
    bool ExtractRAWMetadata(const std::wstring& filePath);
};

} // namespace LightroomCore

