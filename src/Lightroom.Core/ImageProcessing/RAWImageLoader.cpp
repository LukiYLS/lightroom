// 防止 Winsock 冲突 - 必须在包含任何 Windows 头文件之前
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "RAWImageLoader.h"
#include <iostream>
#include <algorithm>
#include <string>

namespace LightroomCore {

RAWImageLoader::RAWImageLoader()
    : m_LastImageWidth(0)
    , m_LastImageHeight(0)
{
    // 初始化 RAW 信息
    m_RAWInfo = RAWImageInfo();
    
    // 创建 LibRaw 包装器
    m_LibRawWrapper = std::make_unique<LibRawWrapper>();
}

RAWImageLoader::~RAWImageLoader() {
    // LibRawWrapper 会自动清理
    m_LibRawWrapper.reset();
}

bool RAWImageLoader::IsRAWFormat(const std::wstring& filePath) const {
    // 转换为小写进行比较
    std::wstring lowerPath = filePath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::towlower);

    // 检查常见 RAW 格式扩展名
    const wchar_t* extensions[] = {
        L".cr2", L".cr3",   // Canon
        L".nef", L".nrw",   // Nikon
        L".arw", L".srf",   // Sony
        L".dng",            // Adobe DNG
        L".orf",            // Olympus
        L".raf",            // Fujifilm
        L".rw2",            // Panasonic
        L".pef", L".ptx",   // Pentax
        L".x3f",            // Sigma
        L".3fr", L".fff",   // Hasselblad (3FR and FFF formats)
        L".mef",            // Mamiya
        L".mos",            // Leaf
        L".raw"             // Generic RAW
    };
    
    for (const wchar_t* ext : extensions) {
        size_t extLen = wcslen(ext);
        if (lowerPath.length() >= extLen) {
            if (lowerPath.compare(lowerPath.length() - extLen, extLen, ext) == 0) {
                return true;
            }
        }
    }
    
    return false;
}

bool RAWImageLoader::CanLoad(const std::wstring& filePath) {
    // 首先检查扩展名
    if (!IsRAWFormat(filePath)) {
        return false;
    }

    // 验证文件是否存在
    DWORD fileAttributes = GetFileAttributesW(filePath.c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    // TODO: 可以进一步尝试用 LibRaw 打开文件来确认
    // 这里先简单返回 true，实际加载时再验证
    return true;
}

std::shared_ptr<RenderCore::RHITexture2D> RAWImageLoader::Load(
    const std::wstring& filePath,
    std::shared_ptr<RenderCore::DynamicRHI> rhi) {
    
    if (!rhi) {
        return nullptr;
    }

    // 打开 RAW 文件
    if (!m_LibRawWrapper->OpenFile(filePath)) {
        return nullptr;
    }

    // 提取元数据
    if (!ExtractRAWMetadata(filePath)) {
        return nullptr;
    }

    m_LastImageWidth = m_RAWInfo.width;
    m_LastImageHeight = m_RAWInfo.height;

    // 选项 1: 使用 LibRaw 的内置处理（快速，但控制有限）
    // 选项 2: 使用我们的 RAWProcessor（更多控制，支持自定义参数）
    // 当前使用选项 1，后续可以切换到选项 2
    
    // 处理 RAW 数据（去马赛克 + 转换为 RGB）
    std::vector<uint8_t> rgbData;
    uint32_t processedWidth, processedHeight;
    if (!m_LibRawWrapper->ProcessRAW(rgbData, processedWidth, processedHeight)) {
        return nullptr;
    }

    // 转换 RGB 到 BGRA（RHI 期望的格式）
    std::vector<uint8_t> bgraData(processedWidth * processedHeight * 4);
    for (uint32_t i = 0; i < processedWidth * processedHeight; ++i) {
        bgraData[i * 4 + 0] = rgbData[i * 3 + 2];  // B
        bgraData[i * 4 + 1] = rgbData[i * 3 + 1];  // G
        bgraData[i * 4 + 2] = rgbData[i * 3 + 0];  // R
        bgraData[i * 4 + 3] = 255;                  // A
    }

    // 使用 RHI 接口创建纹理
    uint32_t stride = processedWidth * 4;  // BGRA = 4 bytes per pixel
    auto texture = rhi->RHICreateTexture2D(
        RenderCore::EPixelFormat::PF_B8G8R8A8,
        RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
        processedWidth,
        processedHeight,
        1,  // NumMips
        bgraData.data(),
        stride
    );

    if (!texture) {
        return nullptr;
    }
    return texture;
}

void RAWImageLoader::GetImageInfo(uint32_t& width, uint32_t& height) const {
    width = m_LastImageWidth;
    height = m_LastImageHeight;
}

bool RAWImageLoader::LoadRAWData(const std::wstring& filePath,
                                 std::vector<uint16_t>& rawData,
                                 uint32_t& outWidth,
                                 uint32_t& outHeight) {
    // 打开 RAW 文件（如果尚未打开）
    if (!m_LibRawWrapper->IsOpen()) {
        if (!m_LibRawWrapper->OpenFile(filePath)) {
            return false;
        }
    }

    // 解包 RAW 数据（16-bit Bayer pattern）
    if (!m_LibRawWrapper->UnpackRAW(rawData, outWidth, outHeight)) {
        return false;
    }

    return true;
}

bool RAWImageLoader::ExtractRAWMetadata(const std::wstring& filePath) {
    // 确保文件已打开
    if (!m_LibRawWrapper->IsOpen()) {
        if (!m_LibRawWrapper->OpenFile(filePath)) {
            return false;
        }
    }

    // 提取元数据
    if (!m_LibRawWrapper->ExtractMetadata(m_RAWInfo)) {
        return false;
    }

    return true;
}

} // namespace LightroomCore

