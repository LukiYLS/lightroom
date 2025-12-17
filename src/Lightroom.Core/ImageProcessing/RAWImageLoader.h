#pragma once

#include "ImageLoader.h"
#include "RAWImageInfo.h"
#include "LibRawWrapper.h"
#include <vector>
#include <memory>

namespace LightroomCore {

// RAW image loader (using LibRaw)
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
    
    // Load RAW data (16-bit Bayer pattern)
    // Returns true on success, rawData contains raw Bayer data
    bool LoadRAWData(const std::wstring& filePath, 
                     std::vector<uint16_t>& rawData,
                     uint32_t& outWidth,
                     uint32_t& outHeight);

    // Load RAW data to texture (16-bit Bayer pattern, for GPU processing)
    // Returns texture with format R16_UNORM (single channel 16-bit)
    std::shared_ptr<RenderCore::RHITexture2D> LoadRAWDataToTexture(
        const std::wstring& filePath,
        std::shared_ptr<RenderCore::DynamicRHI> rhi);

private:
    RAWImageInfo m_RAWInfo;
    uint32_t m_LastImageWidth;
    uint32_t m_LastImageHeight;
    std::unique_ptr<LibRawWrapper> m_LibRawWrapper;

    // Check if file extension is RAW format
    bool IsRAWFormat(const std::wstring& filePath) const;

    // Extract metadata from file path
    bool ExtractRAWMetadata(const std::wstring& filePath);
};

} // namespace LightroomCore
