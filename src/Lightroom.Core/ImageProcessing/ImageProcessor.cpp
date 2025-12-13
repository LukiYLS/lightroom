#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "ImageProcessor.h"
#include "StandardImageLoader.h"
#include "RAWImageLoader.h"
#include <iostream>

namespace LightroomCore {

ImageProcessor::ImageProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : m_RHI(rhi)
    , m_LastImageWidth(0)
    , m_LastImageHeight(0)
    , m_LastFormat(ImageFormat::Unknown)
{
    // 创建加载器实例
    m_StandardLoader = std::make_unique<StandardImageLoader>();
    m_RAWLoader = std::make_unique<RAWImageLoader>();
}

ImageProcessor::~ImageProcessor() {
}

std::shared_ptr<RenderCore::RHITexture2D> ImageProcessor::LoadImageFromFile(const char* imagePath) {
    if (!imagePath) {
        return nullptr;
    }

    // 转换路径为宽字符
    std::wstring wpath;
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, nullptr, 0);
    if (pathLen > 0) {
        wpath.resize(pathLen);
        MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, &wpath[0], pathLen);
        if (!wpath.empty() && wpath.back() == L'\0') {
            wpath.pop_back();
        }
    } else {
        pathLen = MultiByteToWideChar(CP_ACP, 0, imagePath, -1, nullptr, 0);
        if (pathLen > 0) {
            wpath.resize(pathLen);
            MultiByteToWideChar(CP_ACP, 0, imagePath, -1, &wpath[0], pathLen);
            if (!wpath.empty() && wpath.back() == L'\0') {
                wpath.pop_back();
            }
        } else {
            std::cerr << "[ImageProcessor] Failed to convert path to wide string: " << imagePath << std::endl;
            return nullptr;
        }
    }

    return LoadImageFromFile(wpath);
}

std::shared_ptr<RenderCore::RHITexture2D> ImageProcessor::LoadImageFromFile(const std::wstring& imagePath) {
    // 选择适当的加载器
    IImageLoader* loader = SelectLoader(imagePath);
    if (!loader) {
        std::cerr << "[ImageProcessor] No suitable loader found for: " << std::endl;
        return nullptr;
    }

    // 使用选定的加载器加载图片
    auto texture = loader->Load(imagePath, m_RHI);
    if (!texture) {
        std::cerr << "[ImageProcessor] Failed to load image" << std::endl;
        return nullptr;
    }

    // 更新最后加载的图片信息
    loader->GetImageInfo(m_LastImageWidth, m_LastImageHeight);
    m_LastFormat = loader->GetFormat();

    // 如果是 RAW 格式，保存 RAW 信息
    if (m_LastFormat == ImageFormat::RAW) {
        RAWImageLoader* rawLoader = dynamic_cast<RAWImageLoader*>(loader);
        if (rawLoader) {
            if (!m_LastRAWInfo) {
                m_LastRAWInfo = std::make_unique<RAWImageInfo>();
            }
            *m_LastRAWInfo = rawLoader->GetRAWInfo();
        }
    } else {
        // 清除 RAW 信息
        m_LastRAWInfo.reset();
    }

    std::wcout << L"[ImageProcessor] Successfully loaded " 
               << (m_LastFormat == ImageFormat::RAW ? L"RAW" : L"standard")
               << L" image: " << imagePath 
               << L" (" << m_LastImageWidth << L"x" << m_LastImageHeight << L")" << std::endl;
    return texture;
}

bool ImageProcessor::IsRAWFormat(const std::wstring& filePath) const {
    return m_RAWLoader->CanLoad(filePath);
}

ImageFormat ImageProcessor::GetImageFormat(const std::wstring& filePath) const {
    if (m_RAWLoader->CanLoad(filePath)) {
        return ImageFormat::RAW;
    } else if (m_StandardLoader->CanLoad(filePath)) {
        return ImageFormat::Standard;
    }
    return ImageFormat::Unknown;
}

IImageLoader* ImageProcessor::SelectLoader(const std::wstring& filePath) {
    // 优先尝试 RAW 加载器
    if (m_RAWLoader->CanLoad(filePath)) {
        return m_RAWLoader.get();
    }
    
    // 然后尝试标准加载器
    if (m_StandardLoader->CanLoad(filePath)) {
        return m_StandardLoader.get();
    }
    
    return nullptr;
}

} // namespace LightroomCore


