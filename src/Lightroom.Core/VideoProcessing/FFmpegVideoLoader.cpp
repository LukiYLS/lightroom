#include "FFmpegVideoLoader.h"
#include "FFmpegHardwareVideoLoader.h"
#include "FFmpegSoftwareVideoLoader.h"
#include <iostream>

namespace LightroomCore {

FFmpegVideoLoader::FFmpegVideoLoader()
    : m_HardwareLoader(nullptr)
    , m_SoftwareLoader(nullptr)
    , m_ActiveLoader(nullptr)
    , m_IsOpen(false)
{
}

FFmpegVideoLoader::~FFmpegVideoLoader() {
    Close();
}

bool FFmpegVideoLoader::CanLoad(const std::wstring& filePath) const {
    // 硬件和软件解码器都支持相同的格式
    FFmpegHardwareVideoLoader hwLoader;
    return hwLoader.CanLoad(filePath);
}

bool FFmpegVideoLoader::Open(const std::wstring& filePath) {
    if (m_IsOpen) {
        Close();
    }
    
    // 优先尝试硬件解码（Open 时可以检查 codec 是否支持硬件解码）
    m_HardwareLoader = std::make_unique<FFmpegHardwareVideoLoader>();
    if (m_HardwareLoader->Open(filePath)) {
        // 硬件解码器打开成功（codec 支持硬件解码）
        m_ActiveLoader = m_HardwareLoader.get();
        m_IsOpen = true;
        std::cout << "[FFmpegVideoLoader] Video opened with hardware decoder" << std::endl;
        return true;
    }
    
    // 硬件解码失败，回退到软件解码
    m_HardwareLoader.reset();
    std::cout << "[FFmpegVideoLoader] Hardware decoder not available, falling back to software decoder" << std::endl;
    
    m_SoftwareLoader = std::make_unique<FFmpegSoftwareVideoLoader>();
    if (!m_SoftwareLoader->Open(filePath)) {
        std::cerr << "[FFmpegVideoLoader] Failed to open video with software decoder" << std::endl;
        m_SoftwareLoader.reset();
        return false;
    }
    
    m_ActiveLoader = m_SoftwareLoader.get();
    m_IsOpen = true;
    std::cout << "[FFmpegVideoLoader] Video opened with software decoder" << std::endl;
    return true;
}

bool FFmpegVideoLoader::GetMetadata(VideoMetadata& metadata) const {
    if (!m_ActiveLoader) {
        return false;
    }
    return m_ActiveLoader->GetMetadata(metadata);
}

bool FFmpegVideoLoader::Seek(int64_t timestamp) {
    if (!m_ActiveLoader) {
        return false;
    }
    return m_ActiveLoader->Seek(timestamp);
}

bool FFmpegVideoLoader::SeekToFrame(int64_t frameIndex) {
    if (!m_ActiveLoader) {
        return false;
    }
    return m_ActiveLoader->SeekToFrame(frameIndex);
}

std::shared_ptr<RenderCore::RHITexture2D> FFmpegVideoLoader::ReadNextFrame(
    std::shared_ptr<RenderCore::DynamicRHI> rhi) {
    if (!m_IsOpen || !rhi) {
        return nullptr;
    }
    
    if (!m_ActiveLoader) {
        return nullptr;
    }
    
    return m_ActiveLoader->ReadNextFrame(rhi);
}

std::shared_ptr<RenderCore::RHITexture2D> FFmpegVideoLoader::ReadFrame(
    int64_t frameIndex,
    std::shared_ptr<RenderCore::DynamicRHI> rhi) {
    if (!m_ActiveLoader) {
        return nullptr;
    }
    return m_ActiveLoader->ReadFrame(frameIndex, rhi);
}

void FFmpegVideoLoader::Close() {
    if (m_HardwareLoader) {
        m_HardwareLoader->Close();
        m_HardwareLoader.reset();
    }
    if (m_SoftwareLoader) {
        m_SoftwareLoader->Close();
        m_SoftwareLoader.reset();
    }
    m_ActiveLoader = nullptr;
    m_IsOpen = false;
}

int64_t FFmpegVideoLoader::GetCurrentFrameIndex() const {
    if (!m_ActiveLoader) {
        return -1;
    }
    return m_ActiveLoader->GetCurrentFrameIndex();
}

int64_t FFmpegVideoLoader::GetCurrentTimestamp() const {
    if (!m_ActiveLoader) {
        return -1;
    }
    return m_ActiveLoader->GetCurrentTimestamp();
}

bool FFmpegVideoLoader::IsOpen() const {
    return m_IsOpen;
}

} // namespace LightroomCore
