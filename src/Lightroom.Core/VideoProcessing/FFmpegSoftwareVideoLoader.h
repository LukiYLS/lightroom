#pragma once

#include "VideoLoader.h"
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <d3d11.h>

// FFmpeg forward declarations
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

extern "C" {
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

namespace LightroomCore {

// FFmpeg software video loader
class FFmpegSoftwareVideoLoader : public IVideoLoader {
public:
    FFmpegSoftwareVideoLoader();
    ~FFmpegSoftwareVideoLoader();
    
    FFmpegSoftwareVideoLoader(const FFmpegSoftwareVideoLoader&) = delete;
    FFmpegSoftwareVideoLoader& operator=(const FFmpegSoftwareVideoLoader&) = delete;
    
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
    bool DecodeFrame();
    bool EnsureYUVTextures(std::shared_ptr<RenderCore::DynamicRHI> rhi, uint32_t width, uint32_t height, 
                           uint32_t yWidth, uint32_t yHeight, uint32_t uvWidth, uint32_t uvHeight);
    static void InitializeFFmpeg();
    static bool s_FFmpegInitialized;
    
    AVFormatContext* m_FormatContext;
    AVCodecContext* m_CodecContext;
    AVFrame* m_Frame;
    AVFrame* m_ConvertedFrame;  // For format conversion (YUVA444P12LE -> YUV420P)
    SwsContext* m_SwsContext;   // For format conversion only
    
    int m_VideoStreamIndex;
    VideoMetadata m_Metadata;
    int64_t m_CurrentFrameIndex;
    bool m_IsOpen;
    
    // Cached resources for GPU-based YUV to RGB conversion
    std::unique_ptr<class YUVToRGBNode> m_CachedYUVToRGBNode;
    std::shared_ptr<RenderCore::DynamicRHI> m_CachedRHI;
    std::shared_ptr<RenderCore::RHITexture2D> m_CachedRGBTexture;
    uint32_t m_CachedWidth;
    uint32_t m_CachedHeight;
    
    // YUV texture resources (for YUV420P format: separate Y, U, V planes)
    std::shared_ptr<RenderCore::RHITexture2D> m_YTexture;
    std::shared_ptr<RenderCore::RHITexture2D> m_UTexture;
    std::shared_ptr<RenderCore::RHITexture2D> m_VTexture;
    uint32_t m_CachedYUVWidth;
    uint32_t m_CachedYUVHeight;
    
    AVRational m_TimeBase;
    double m_FrameDuration;
};

} // namespace LightroomCore


