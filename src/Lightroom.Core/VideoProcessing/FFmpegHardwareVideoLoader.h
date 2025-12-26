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
struct AVBufferRef;

// Forward declaration
namespace LightroomCore {
    class YUVToRGBNode;
}

extern "C" {
#include <libavutil/rational.h>
#include <libavutil/buffer.h>
}

namespace LightroomCore {

// FFmpeg hardware video loader (D3D11VA)
class FFmpegHardwareVideoLoader : public IVideoLoader {
public:
    FFmpegHardwareVideoLoader();
    ~FFmpegHardwareVideoLoader();
    
    FFmpegHardwareVideoLoader(const FFmpegHardwareVideoLoader&) = delete;
    FFmpegHardwareVideoLoader& operator=(const FFmpegHardwareVideoLoader&) = delete;
    
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
    
    // Get hardware decoded raw texture (D3D11 format, may be YUV)
    // For GPU conversion, not CPU conversion
    std::shared_ptr<RenderCore::RHITexture2D> GetHardwareFrameTexture(
        std::shared_ptr<RenderCore::DynamicRHI> rhi);
    
    // Check if current frame is hardware texture format
    bool IsHardwareFrame() const;

private:
    bool DecodeFrame();
    bool InitializeHardwareDecoding(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    void CleanupHardwareDecoding();
    
    static void InitializeFFmpeg();
    static bool s_FFmpegInitialized;
    
    AVFormatContext* m_FormatContext;
    AVCodecContext* m_CodecContext;
    AVFrame* m_Frame;              
    AVFrame* m_SoftwareFrame;      
    AVBufferRef* m_HwDeviceCtx;
    AVBufferRef* m_HwFramesCtx;
    
    int m_VideoStreamIndex;
    VideoMetadata m_Metadata;
    int64_t m_CurrentFrameIndex;
    bool m_IsOpen;
    
    AVRational m_TimeBase;
    double m_FrameDuration;
    
    // Cached resources for performance optimization
    std::unique_ptr<class YUVToRGBNode> m_CachedYUVToRGBNode;
    std::shared_ptr<RenderCore::DynamicRHI> m_CachedRHI;
    std::shared_ptr<RenderCore::RHITexture2D> m_CachedRGBTexture;
    uint32_t m_CachedWidth;
    uint32_t m_CachedHeight;
    
    // Staging texture resources (single-layer NV12 texture for copying from array)
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_StagingTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_StagingYSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_StagingUVSRV;
    uint32_t m_CachedStagingWidth;
    uint32_t m_CachedStagingHeight;
};

} // namespace LightroomCore