#pragma once

#include "VideoLoader.h"
#include <memory>
#include <string>

namespace LightroomCore {

// 前向声明
class FFmpegHardwareVideoLoader;
class FFmpegSoftwareVideoLoader;

// FFmpeg 视频加载器适配器：优先使用硬件解码，失败时回退到软件解码
class FFmpegVideoLoader : public IVideoLoader {
public:
    FFmpegVideoLoader();
    ~FFmpegVideoLoader();
    
    FFmpegVideoLoader(const FFmpegVideoLoader&) = delete;
    FFmpegVideoLoader& operator=(const FFmpegVideoLoader&) = delete;
    
    bool CanLoad(const std::wstring& filePath) const override;
    bool Open(const std::wstring& filePath) override;
    bool GetMetadata(VideoMetadata& metadata) const override;
    bool Seek(int64_t timestamp) override;
    bool SeekToFrame(int64_t frameIndex) override;
    std::shared_ptr<RenderCore::RHITexture2D> ReadNextFrame(
        std::shared_ptr<RenderCore::DynamicRHI> rhi) override;
    std::shared_ptr<RenderCore::RHITexture2D> ReadFrame(
        int64_t frameIndex,
        std::shared_ptr<RenderCore::DynamicRHI> rhi) override;
    void Close() override;
    int64_t GetCurrentFrameIndex() const override;
    int64_t GetCurrentTimestamp() const override;
    bool IsOpen() const override;

private:
    std::unique_ptr<FFmpegHardwareVideoLoader> m_HardwareLoader;
    std::unique_ptr<FFmpegSoftwareVideoLoader> m_SoftwareLoader;
    IVideoLoader* m_ActiveLoader;  // 当前活动的加载器（硬件或软件）
    bool m_IsOpen;
};

} // namespace LightroomCore
