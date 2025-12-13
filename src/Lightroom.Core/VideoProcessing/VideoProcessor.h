#pragma once

#include "VideoLoader.h"
#include <memory>
#include <string>

namespace RenderCore {
    class DynamicRHI;
    class RHITexture2D;
}

namespace LightroomCore {

// 视频处理器：封装视频加载和处理
class VideoProcessor {
public:
    VideoProcessor(std::shared_ptr<RenderCore::DynamicRHI> rhi);
    ~VideoProcessor();
    
    // 打开视频文件
    bool OpenVideo(const std::wstring& filePath);
    
    // 关闭视频
    void CloseVideo();
    
    // 获取视频元数据
    const VideoMetadata* GetMetadata() const;
    
    // 定位到指定时间
    bool Seek(int64_t timestamp);
    
    // 定位到指定帧
    bool SeekToFrame(int64_t frameIndex);
    
    // 读取当前帧到纹理
    std::shared_ptr<RenderCore::RHITexture2D> GetCurrentFrame();
    
    // 读取下一帧（用于播放）
    std::shared_ptr<RenderCore::RHITexture2D> GetNextFrame();
    
    // 获取当前帧索引
    int64_t GetCurrentFrameIndex() const;
    
    // 获取当前时间戳
    int64_t GetCurrentTimestamp() const;
    
    // 检查是否已打开视频
    bool IsOpen() const;

private:
    std::shared_ptr<RenderCore::DynamicRHI> m_RHI;
    std::unique_ptr<IVideoLoader> m_VideoLoader;
    VideoMetadata m_Metadata;
    bool m_IsOpen;
};

} // namespace LightroomCore

