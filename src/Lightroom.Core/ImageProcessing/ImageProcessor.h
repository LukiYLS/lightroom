#pragma once

#include "../d3d11rhi/DynamicRHI.h"
#include "ImageLoader.h"
#include "RAWImageInfo.h"
#include "RAWImageLoader.h"
#include <string>
#include <memory>

namespace LightroomCore {

// Image processing module: responsible for image loading and processing, using RHI interface
// Uses factory pattern to automatically select StandardImageLoader or RAWImageLoader

class ImageProcessor {
public:
    ImageProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~ImageProcessor();

    // Load image from file to RHI texture (factory method - automatically selects loader)
    // Returned texture lifetime is managed by caller
    std::shared_ptr<RenderCore::RHITexture2D> LoadImageFromFile(const char* imagePath);

    // Load image from file to RHI texture (wide character path version)
    std::shared_ptr<RenderCore::RHITexture2D> LoadImageFromFile(const std::wstring& imagePath);

    // Get last loaded image size
    void GetLastImageSize(uint32_t& width, uint32_t& height) const {
        width = m_LastImageWidth;
        height = m_LastImageHeight;
    }

    // Check if file is RAW format
    bool IsRAWFormat(const std::wstring& filePath) const;

    // Get image format
    ImageFormat GetImageFormat(const std::wstring& filePath) const;

    // Get last loaded image format
    ImageFormat GetLastImageFormat() const { return m_LastFormat; }

    // RAW-specific: Get RAW info (only valid after loading RAW file)
    const RAWImageInfo* GetRAWInfo() const {
        return (m_LastFormat == ImageFormat::RAW) ? m_LastRAWInfo.get() : nullptr;
    }

    // Get RAW loader (for direct access to RAW functionality)
    RAWImageLoader* GetRAWLoader() {
        return dynamic_cast<RAWImageLoader*>(m_RAWLoader.get());
    }

private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    uint32_t m_LastImageWidth;
    uint32_t m_LastImageHeight;
    ImageFormat m_LastFormat;

    // Loader instances
    std::unique_ptr<IImageLoader> m_StandardLoader;
    std::unique_ptr<IImageLoader> m_RAWLoader;

    // RAW info (only valid when loading RAW file)
    std::unique_ptr<RAWImageInfo> m_LastRAWInfo;

    // Select appropriate loader
    IImageLoader* SelectLoader(const std::wstring& filePath);
};

} // namespace LightroomCore
