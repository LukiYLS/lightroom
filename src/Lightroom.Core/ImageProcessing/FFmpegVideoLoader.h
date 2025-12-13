#pragma once

#include "VideoLoader.h"
#include <memory>
#include <string>
#include <vector>

// FFmpeg 前向声明（避免直接包含头文件）
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

// AVRational 需要完整定义（因为用作成员变量）
// 在头文件中包含 rational.h，因为我们需要完整的类型定义
extern "C" {
#include <libavutil/rational.h>
}

namespace LightroomCore {

// FFmpeg 视频加载器实现
class FFmpegVideoLoader : public IVideoLoader {
public:
    FFmpegVideoLoader();
    ~FFmpegVideoLoader();
    
    // 禁止拷贝
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
    // 将 AVFrame 转换为 D3D11 纹理
    std::shared_ptr<RenderCore::RHITexture2D> ConvertFrameToTexture(
        AVFrame* frame,
        std::shared_ptr<RenderCore::DynamicRHI> rhi);
    
    // 读取并解码一帧
    bool DecodeFrame();
    
    // 初始化 FFmpeg（静态方法，确保只初始化一次）
    static void InitializeFFmpeg();
    static bool s_FFmpegInitialized;
    
    AVFormatContext* m_FormatContext;
    AVCodecContext* m_CodecContext;
    AVFrame* m_Frame;
    AVFrame* m_RGBFrame;
    SwsContext* m_SwsContext;
    
    int m_VideoStreamIndex;
    VideoMetadata m_Metadata;
    int64_t m_CurrentFrameIndex;
    bool m_IsOpen;
    
    // RGB 帧缓冲区
    std::vector<uint8_t> m_RGBBuffer;
    
    // 用于帧索引到时间戳的转换
    AVRational m_TimeBase;
    double m_FrameDuration; // 每帧的时长（秒）
};

} // namespace LightroomCore

