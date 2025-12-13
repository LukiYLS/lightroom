#include "VideoProcessor.h"
#include "FFmpegVideoLoader.h"
#include <iostream>

namespace LightroomCore {

VideoProcessor::VideoProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : m_RHI(rhi)
    , m_IsOpen(false)
{
    // 创建 FFmpeg 视频加载器
    m_VideoLoader = std::make_unique<FFmpegVideoLoader>();
}

VideoProcessor::~VideoProcessor() {
    CloseVideo();
}

bool VideoProcessor::OpenVideo(const std::wstring& filePath) {
    if (m_IsOpen) {
        CloseVideo();
    }
    
    if (!m_VideoLoader->Open(filePath)) {
        std::cerr << "[VideoProcessor] Failed to open video: " << std::endl;
        return false;
    }
    
    if (!m_VideoLoader->GetMetadata(m_Metadata)) {
        std::cerr << "[VideoProcessor] Failed to get video metadata" << std::endl;
        m_VideoLoader->Close();
        return false;
    }
    
    m_IsOpen = true;
    std::cout << "[VideoProcessor] Video opened successfully" << std::endl;
    return true;
}

void VideoProcessor::CloseVideo() {
    if (m_VideoLoader) {
        m_VideoLoader->Close();
    }
    m_IsOpen = false;
}

const VideoMetadata* VideoProcessor::GetMetadata() const {
    return m_IsOpen ? &m_Metadata : nullptr;
}

bool VideoProcessor::Seek(int64_t timestamp) {
    if (!m_IsOpen) {
        return false;
    }
    return m_VideoLoader->Seek(timestamp);
}

bool VideoProcessor::SeekToFrame(int64_t frameIndex) {
    if (!m_IsOpen) {
        return false;
    }
    return m_VideoLoader->SeekToFrame(frameIndex);
}

std::shared_ptr<RenderCore::RHITexture2D> VideoProcessor::GetCurrentFrame() {
    if (!m_IsOpen || !m_RHI) {
        return nullptr;
    }
    
    int64_t currentFrame = m_VideoLoader->GetCurrentFrameIndex();
    return m_VideoLoader->ReadFrame(currentFrame, m_RHI);
}

std::shared_ptr<RenderCore::RHITexture2D> VideoProcessor::GetNextFrame() {
    if (!m_IsOpen || !m_RHI) {
        return nullptr;
    }
    
    return m_VideoLoader->ReadNextFrame(m_RHI);
}

int64_t VideoProcessor::GetCurrentFrameIndex() const {
    if (!m_IsOpen) {
        return -1;
    }
    return m_VideoLoader->GetCurrentFrameIndex();
}

int64_t VideoProcessor::GetCurrentTimestamp() const {
    if (!m_IsOpen) {
        return -1;
    }
    return m_VideoLoader->GetCurrentTimestamp();
}

bool VideoProcessor::IsOpen() const {
    return m_IsOpen;
}

} // namespace LightroomCore

