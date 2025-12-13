#pragma once

#include <string>
#include <memory>
#include <cstdint>

namespace RenderCore {
    class DynamicRHI;
    class RHITexture2D;
}

namespace LightroomCore {

// 视频格式枚举
enum class VideoFormat {
    MP4,
    MOV,
    AVI,
    MKV,
    Unknown
};

// 视频元数据
struct VideoMetadata {
    uint32_t width = 0;
    uint32_t height = 0;
    double frameRate = 0.0;        // fps
    int64_t totalFrames = 0;        // 总帧数
    int64_t duration = 0;           // 时长（微秒）
    VideoFormat format = VideoFormat::Unknown;
    bool hasAudio = false;
};

// 视频加载器接口（类似于 IImageLoader）
class IVideoLoader {
public:
    virtual ~IVideoLoader() = default;
    
    // 检查是否支持该格式
    virtual bool CanLoad(const std::wstring& filePath) const = 0;
    
    // 打开视频文件
    virtual bool Open(const std::wstring& filePath) = 0;
    
    // 获取视频元数据
    virtual bool GetMetadata(VideoMetadata& metadata) const = 0;
    
    // 定位到指定时间（微秒）
    virtual bool Seek(int64_t timestamp) = 0;
    
    // 定位到指定帧
    virtual bool SeekToFrame(int64_t frameIndex) = 0;
    
    // 读取下一帧到纹理
    virtual std::shared_ptr<RenderCore::RHITexture2D> ReadNextFrame(
        std::shared_ptr<RenderCore::DynamicRHI> rhi) = 0;
    
    // 读取指定帧到纹理
    virtual std::shared_ptr<RenderCore::RHITexture2D> ReadFrame(
        int64_t frameIndex,
        std::shared_ptr<RenderCore::DynamicRHI> rhi) = 0;
    
    // 关闭视频
    virtual void Close() = 0;
    
    // 获取当前帧索引
    virtual int64_t GetCurrentFrameIndex() const = 0;
    
    // 获取当前时间戳（微秒）
    virtual int64_t GetCurrentTimestamp() const = 0;
    
    // 检查是否已打开
    virtual bool IsOpen() const = 0;
};

} // namespace LightroomCore

