#pragma once

#include "../d3d11rhi/DynamicRHI.h"
#include "../d3d11rhi/RHITexture2D.h"
#include <string>
#include <memory>
#include <vector>

namespace LightroomCore {

// 图片导出格式
enum class ExportFormat {
    PNG,
    JPEG
};

// 图片导出器：从渲染目标纹理导出图片到文件
class ImageExporter {
public:
    ImageExporter(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~ImageExporter();

    // 从 D3D11 纹理直接读取数据（用于共享纹理）
    // d3d11Texture: D3D11 纹理指针
    // width, height: 纹理尺寸
    // outData: 输出的像素数据（BGRA32 格式）
    // outStride: 输出行字节数
    // 返回是否成功
    bool ReadD3D11TextureData(ID3D11Texture2D* d3d11Texture,
                              uint32_t& width,
                              uint32_t& height,
                              std::vector<uint8_t>& outData,
                              uint32_t& outStride);

    // 使用 WIC 保存图片数据（公共方法，供 SDK 使用）
    bool SaveImageDataWithWIC(const std::string& filePath,
                             const uint8_t* imageData,
                             uint32_t width,
                             uint32_t height,
                             uint32_t stride,
                             ExportFormat format,
                             uint32_t quality);

private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
};

} // namespace LightroomCore
