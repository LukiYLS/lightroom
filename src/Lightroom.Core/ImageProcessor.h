#pragma once

#include "d3d11rhi/DynamicRHI.h"
#include <string>
#include <memory>

namespace LightroomCore {

// 图片处理模块：负责图片加载和处理，使用 RHI 接口

class ImageProcessor {
public:
    ImageProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~ImageProcessor();

    // 从文件加载图片到 RHI 纹理
    // 返回的纹理由调用者管理生命周期
    std::shared_ptr<RenderCore::RHITexture2D> LoadImageFromFile(const char* imagePath);

    // 从文件加载图片到 RHI 纹理（宽字符路径版本）
    std::shared_ptr<RenderCore::RHITexture2D> LoadImageFromFile(const std::wstring& imagePath);

    // 获取最后加载的图片尺寸
    void GetLastImageSize(uint32_t& width, uint32_t& height) const {
        width = m_LastImageWidth;
        height = m_LastImageHeight;
    }

private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    uint32_t m_LastImageWidth;
    uint32_t m_LastImageHeight;

    // 使用 WIC 加载图片数据到内存
    bool LoadImageDataWithWIC(const std::wstring& imagePath, std::vector<uint8_t>& outData, 
                               uint32_t& outWidth, uint32_t& outHeight, uint32_t& outStride);
};

} // namespace LightroomCore

