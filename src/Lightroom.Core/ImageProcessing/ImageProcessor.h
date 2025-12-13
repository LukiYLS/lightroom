#pragma once

#include "../d3d11rhi/DynamicRHI.h"
#include "ImageLoader.h"
#include "RAWImageInfo.h"
#include <string>
#include <memory>

namespace LightroomCore {

// 图片处理模块：负责图片加载和处理，使用 RHI 接口
// 使用工厂模式自动选择 StandardImageLoader 或 RAWImageLoader

class ImageProcessor {
public:
    ImageProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~ImageProcessor();

    // 从文件加载图片到 RHI 纹理（工厂方法 - 自动选择加载器）
    // 返回的纹理由调用者管理生命周期
    std::shared_ptr<RenderCore::RHITexture2D> LoadImageFromFile(const char* imagePath);

    // 从文件加载图片到 RHI 纹理（宽字符路径版本）
    std::shared_ptr<RenderCore::RHITexture2D> LoadImageFromFile(const std::wstring& imagePath);

    // 获取最后加载的图片尺寸
    void GetLastImageSize(uint32_t& width, uint32_t& height) const {
        width = m_LastImageWidth;
        height = m_LastImageHeight;
    }

    // 检查文件是否为 RAW 格式
    bool IsRAWFormat(const std::wstring& filePath) const;

    // 获取图片格式
    ImageFormat GetImageFormat(const std::wstring& filePath) const;

    // 获取最后加载的图片格式
    ImageFormat GetLastImageFormat() const { return m_LastFormat; }

    // RAW-specific: 获取 RAW 信息（仅在加载 RAW 文件后有效）
    const RAWImageInfo* GetRAWInfo() const {
        return (m_LastFormat == ImageFormat::RAW) ? m_LastRAWInfo.get() : nullptr;
    }

private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    uint32_t m_LastImageWidth;
    uint32_t m_LastImageHeight;
    ImageFormat m_LastFormat;

    // 加载器实例
    std::unique_ptr<IImageLoader> m_StandardLoader;
    std::unique_ptr<IImageLoader> m_RAWLoader;

    // RAW 信息（仅在加载 RAW 文件时有效）
    std::unique_ptr<RAWImageInfo> m_LastRAWInfo;

    // 选择适当的加载器
    IImageLoader* SelectLoader(const std::wstring& filePath);
};

} // namespace LightroomCore


