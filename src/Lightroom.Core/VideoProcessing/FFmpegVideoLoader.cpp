#include "FFmpegVideoLoader.h"
#include "../d3d11rhi/DynamicRHI.h"
#include "../d3d11rhi/RHITexture2D.h"
#include <iostream>
#include <algorithm>
#include <cstring>

// FFmpeg 头文件
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>  // 用于 AV_NOPTS_VALUE
#include <libswscale/swscale.h>
}

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace LightroomCore {

bool FFmpegVideoLoader::s_FFmpegInitialized = false;

FFmpegVideoLoader::FFmpegVideoLoader()
    : m_FormatContext(nullptr)
    , m_CodecContext(nullptr)
    , m_Frame(nullptr)
    , m_RGBFrame(nullptr)
    , m_SwsContext(nullptr)
    , m_VideoStreamIndex(-1)
    , m_CurrentFrameIndex(0)
    , m_IsOpen(false)
{
    InitializeFFmpeg();
}

FFmpegVideoLoader::~FFmpegVideoLoader() {
    Close();
}

void FFmpegVideoLoader::InitializeFFmpeg() {
    if (!s_FFmpegInitialized) {
        // FFmpeg 4.0+ 不再需要 av_register_all()，所有格式和编解码器自动注册
        // 只需要初始化网络（如果需要网络流）
        avformat_network_init();
        s_FFmpegInitialized = true;
        std::cout << "[FFmpegVideoLoader] FFmpeg initialized" << std::endl;
    }
}

bool FFmpegVideoLoader::CanLoad(const std::wstring& filePath) const {
    // 检查文件扩展名
    std::wstring ext = filePath.substr(filePath.find_last_of(L".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    
    return ext == L"mp4" || ext == L"mov" || ext == L"avi" || 
           ext == L"mkv" || ext == L"m4v" || ext == L"flv" ||
           ext == L"webm" || ext == L"3gp";
}

bool FFmpegVideoLoader::Open(const std::wstring& filePath) {
    if (m_IsOpen) {
        Close();
    }
    
    // 转换宽字符路径为 UTF-8
    int pathLen = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (pathLen <= 0) {
        std::cerr << "[FFmpegVideoLoader] Failed to convert path to UTF-8" << std::endl;
        return false;
    }
    
    std::vector<char> utf8Path(pathLen);
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, utf8Path.data(), pathLen, nullptr, nullptr);
    
    // 打开输入文件
    m_FormatContext = avformat_alloc_context();
    if (avformat_open_input(&m_FormatContext, utf8Path.data(), nullptr, nullptr) < 0) {
        std::cerr << "[FFmpegVideoLoader] Failed to open input file: " << utf8Path.data() << std::endl;
        avformat_free_context(m_FormatContext);
        m_FormatContext = nullptr;
        return false;
    }
    
    // 查找流信息
    if (avformat_find_stream_info(m_FormatContext, nullptr) < 0) {
        std::cerr << "[FFmpegVideoLoader] Failed to find stream info" << std::endl;
        Close();
        return false;
    }
    
    // 查找视频流
    m_VideoStreamIndex = -1;
    for (unsigned int i = 0; i < m_FormatContext->nb_streams; i++) {
        if (m_FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_VideoStreamIndex = i;
            break;
        }
    }
    
    if (m_VideoStreamIndex == -1) {
        std::cerr << "[FFmpegVideoLoader] No video stream found" << std::endl;
        Close();
        return false;
    }
    
    // 获取编解码器参数
    AVCodecParameters* codecpar = m_FormatContext->streams[m_VideoStreamIndex]->codecpar;
    
    // 查找解码器
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "[FFmpegVideoLoader] Codec not found" << std::endl;
        Close();
        return false;
    }
    
    // 分配编解码器上下文
    m_CodecContext = avcodec_alloc_context3(codec);
    if (!m_CodecContext) {
        std::cerr << "[FFmpegVideoLoader] Failed to allocate codec context" << std::endl;
        Close();
        return false;
    }
    
    // 复制编解码器参数到上下文
    if (avcodec_parameters_to_context(m_CodecContext, codecpar) < 0) {
        std::cerr << "[FFmpegVideoLoader] Failed to copy codec parameters" << std::endl;
        Close();
        return false;
    }
    
    // 打开编解码器
    if (avcodec_open2(m_CodecContext, codec, nullptr) < 0) {
        std::cerr << "[FFmpegVideoLoader] Failed to open codec" << std::endl;
        Close();
        return false;
    }
    
    // 分配帧
    m_Frame = av_frame_alloc();
    m_RGBFrame = av_frame_alloc();
    if (!m_Frame || !m_RGBFrame) {
        std::cerr << "[FFmpegVideoLoader] Failed to allocate frames" << std::endl;
        Close();
        return false;
    }
    
    // 设置 RGB 帧的格式和尺寸
    m_RGBFrame->format = AV_PIX_FMT_RGB24;
    m_RGBFrame->width = m_CodecContext->width;
    m_RGBFrame->height = m_CodecContext->height;
    
    // 计算 RGB 帧所需的缓冲区大小
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, m_CodecContext->width, m_CodecContext->height, 1);
    if (numBytes < 0) {
        std::cerr << "[FFmpegVideoLoader] Failed to get buffer size" << std::endl;
        Close();
        return false;
    }
    m_RGBBuffer.resize(numBytes);
    int ret = av_image_fill_arrays(m_RGBFrame->data, m_RGBFrame->linesize, m_RGBBuffer.data(), 
                                    AV_PIX_FMT_RGB24, m_CodecContext->width, m_CodecContext->height, 1);
    if (ret < 0) {
        std::cerr << "[FFmpegVideoLoader] Failed to fill image arrays" << std::endl;
        Close();
        return false;
    }
    
    // 初始化 SwsContext 用于格式转换
    m_SwsContext = sws_getContext(
        m_CodecContext->width, m_CodecContext->height, m_CodecContext->pix_fmt,
        m_CodecContext->width, m_CodecContext->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    
    if (!m_SwsContext) {
        std::cerr << "[FFmpegVideoLoader] Failed to initialize SwsContext" << std::endl;
        Close();
        return false;
    }
    
    // 填充元数据
    AVStream* videoStream = m_FormatContext->streams[m_VideoStreamIndex];
    m_Metadata.width = m_CodecContext->width;
    m_Metadata.height = m_CodecContext->height;
    m_Metadata.frameRate = av_q2d(videoStream->r_frame_rate);
    
    // 获取视频时长（AVFormatContext::duration 的单位是 AV_TIME_BASE，即微秒）
    if (m_FormatContext->duration != AV_NOPTS_VALUE) {
        m_Metadata.duration = m_FormatContext->duration; // 微秒
    } else {
        // 如果总时长未知，尝试从流的时长计算
        if (videoStream->duration != AV_NOPTS_VALUE) {
            // 将流的时长从流时间基转换为微秒
            m_Metadata.duration = av_rescale_q(videoStream->duration, videoStream->time_base, {1, AV_TIME_BASE});
        } else {
            m_Metadata.duration = 0; // 无法确定时长
        }
    }
    
    // 计算总帧数
    if (m_Metadata.frameRate > 0 && m_Metadata.duration > 0) {
        m_Metadata.totalFrames = static_cast<int64_t>(m_Metadata.duration * m_Metadata.frameRate / AV_TIME_BASE);
    } else if (videoStream->nb_frames > 0) {
        // 如果无法从时长计算，使用流的帧数（如果可用）
        m_Metadata.totalFrames = videoStream->nb_frames;
        // 估算时长
        if (m_Metadata.frameRate > 0) {
            m_Metadata.duration = static_cast<int64_t>(m_Metadata.totalFrames * AV_TIME_BASE / m_Metadata.frameRate);
        }
    } else {
        m_Metadata.totalFrames = 0; // 无法确定总帧数
    }
    
    m_Metadata.hasAudio = false; // 简化：暂时不检测音频
    
    // 检测格式
    std::wstring ext = filePath.substr(filePath.find_last_of(L".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext == L"mp4" || ext == L"m4v") m_Metadata.format = VideoFormat::MP4;
    else if (ext == L"mov") m_Metadata.format = VideoFormat::MOV;
    else if (ext == L"avi") m_Metadata.format = VideoFormat::AVI;
    else if (ext == L"mkv") m_Metadata.format = VideoFormat::MKV;
    else m_Metadata.format = VideoFormat::Unknown;
    
    // 保存时间基和帧时长
    m_TimeBase = videoStream->time_base;
    m_FrameDuration = av_q2d(videoStream->r_frame_rate);
    if (m_FrameDuration > 0) {
        m_FrameDuration = 1.0 / m_FrameDuration;
    }
    
    m_CurrentFrameIndex = 0;
    m_IsOpen = true;
    
    std::cout << "[FFmpegVideoLoader] Video opened: " << m_Metadata.width << "x" << m_Metadata.height 
              << ", " << m_Metadata.frameRate << " fps, " << m_Metadata.totalFrames << " frames" << std::endl;
    
    return true;
}

bool FFmpegVideoLoader::GetMetadata(VideoMetadata& metadata) const {
    if (!m_IsOpen) {
        return false;
    }
    metadata = m_Metadata;
    return true;
}

bool FFmpegVideoLoader::Seek(int64_t timestamp) {
    if (!m_IsOpen || m_VideoStreamIndex < 0) {
        return false;
    }
    
    // 将时间戳转换为流时间基
    int64_t seekTarget = av_rescale_q(timestamp, {1, 1000000}, m_TimeBase);
    
    if (av_seek_frame(m_FormatContext, m_VideoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
        std::cerr << "[FFmpegVideoLoader] Failed to seek" << std::endl;
        return false;
    }
    
    // 清空解码器缓冲区
    avcodec_flush_buffers(m_CodecContext);
    
    // 更新当前帧索引
    m_CurrentFrameIndex = static_cast<int64_t>(timestamp * m_Metadata.frameRate / 1000000.0);
    
    return true;
}

bool FFmpegVideoLoader::SeekToFrame(int64_t frameIndex) {
    if (!m_IsOpen || frameIndex < 0 || frameIndex >= m_Metadata.totalFrames) {
        return false;
    }
    
    // 计算时间戳
    int64_t timestamp = static_cast<int64_t>(frameIndex * 1000000.0 / m_Metadata.frameRate);
    return Seek(timestamp);
}

bool FFmpegVideoLoader::DecodeFrame() {
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        return false;
    }
    
    while (av_read_frame(m_FormatContext, packet) >= 0) {
        if (packet->stream_index == m_VideoStreamIndex) {
            // 发送数据包到解码器
            if (avcodec_send_packet(m_CodecContext, packet) < 0) {
                av_packet_unref(packet);
                continue;
            }
            
            // 接收解码后的帧
            int ret = avcodec_receive_frame(m_CodecContext, m_Frame);
            av_packet_unref(packet);
            
            if (ret == 0) {
                // 成功解码一帧
                av_packet_free(&packet);
                return true;
            } else if (ret == AVERROR(EAGAIN)) {
                // 需要更多输入
                continue;
            } else {
                // 错误
                av_packet_free(&packet);
                return false;
            }
        } else {
            av_packet_unref(packet);
        }
    }
    
    av_packet_free(&packet);
    return false;
}

std::shared_ptr<RenderCore::RHITexture2D> FFmpegVideoLoader::ReadNextFrame(
    std::shared_ptr<RenderCore::DynamicRHI> rhi) {
    if (!m_IsOpen || !rhi) {
        return nullptr;
    }
    
    if (!DecodeFrame()) {
        return nullptr;
    }
    
    // 检查解码后的帧是否有效
    if (!m_Frame || m_Frame->width <= 0 || m_Frame->height <= 0) {
        std::cerr << "[FFmpegVideoLoader] Decoded frame has invalid dimensions: " 
                  << (m_Frame ? m_Frame->width : 0) << "x" 
                  << (m_Frame ? m_Frame->height : 0) << std::endl;
        return nullptr;
    }
    
    // 转换颜色空间
    sws_scale(m_SwsContext,
              m_Frame->data, m_Frame->linesize, 0, m_CodecContext->height,
              m_RGBFrame->data, m_RGBFrame->linesize);
    
    // 确保 RGB 帧的尺寸与解码帧一致（可能在解码过程中改变）
    if (m_RGBFrame->width != m_Frame->width || m_RGBFrame->height != m_Frame->height) {
        // 重新分配 RGB 帧缓冲区
        m_RGBFrame->width = m_Frame->width;
        m_RGBFrame->height = m_Frame->height;
        m_RGBFrame->format = AV_PIX_FMT_RGB24;
        
        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, m_Frame->width, m_Frame->height, 1);
        if (numBytes > 0) {
            m_RGBBuffer.resize(numBytes);
            av_image_fill_arrays(m_RGBFrame->data, m_RGBFrame->linesize, m_RGBBuffer.data(), 
                                AV_PIX_FMT_RGB24, m_Frame->width, m_Frame->height, 1);
            
            // 重新创建 SwsContext
            if (m_SwsContext) {
                sws_freeContext(m_SwsContext);
            }
            m_SwsContext = sws_getContext(
                m_Frame->width, m_Frame->height, m_CodecContext->pix_fmt,
                m_Frame->width, m_Frame->height, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            
            if (!m_SwsContext) {
                std::cerr << "[FFmpegVideoLoader] Failed to recreate SwsContext" << std::endl;
                return nullptr;
            }
            
            // 重新转换
            sws_scale(m_SwsContext,
                      m_Frame->data, m_Frame->linesize, 0, m_Frame->height,
                      m_RGBFrame->data, m_RGBFrame->linesize);
        }
    }
    
    // 转换为纹理
    auto texture = ConvertFrameToTexture(m_RGBFrame, rhi);
    
    if (texture) {
        m_CurrentFrameIndex++;
    }
    
    return texture;
}

std::shared_ptr<RenderCore::RHITexture2D> FFmpegVideoLoader::ReadFrame(
    int64_t frameIndex,
    std::shared_ptr<RenderCore::DynamicRHI> rhi) {
    if (!SeekToFrame(frameIndex)) {
        return nullptr;
    }
    
    return ReadNextFrame(rhi);
}

std::shared_ptr<RenderCore::RHITexture2D> FFmpegVideoLoader::ConvertFrameToTexture(
    AVFrame* frame,
    std::shared_ptr<RenderCore::DynamicRHI> rhi) {
    if (!frame || !rhi) {
        std::cerr << "[FFmpegVideoLoader] ConvertFrameToTexture: Invalid parameters" << std::endl;
        return nullptr;
    }
    
    // 检查帧尺寸 - 使用 codec context 的尺寸作为后备
    uint32_t width = static_cast<uint32_t>(frame->width > 0 ? frame->width : m_CodecContext->width);
    uint32_t height = static_cast<uint32_t>(frame->height > 0 ? frame->height : m_CodecContext->height);
    
    if (width == 0 || height == 0) {
        std::cerr << "[FFmpegVideoLoader] ConvertFrameToTexture: Invalid frame dimensions: " 
                  << "frame->width=" << frame->width << ", frame->height=" << frame->height
                  << ", codec->width=" << m_CodecContext->width << ", codec->height=" << m_CodecContext->height << std::endl;
        return nullptr;
    }
    
    // 检查数据指针
    if (!frame->data[0]) {
        std::cerr << "[FFmpegVideoLoader] ConvertFrameToTexture: Frame data is null" << std::endl;
        return nullptr;
    }
    
    // D3D11 要求 pitch 必须是 4 字节对齐的
    // 计算对齐后的 stride（每行字节数，必须是 4 的倍数）
    uint32_t stride = (width * 4 + 3) & ~3; // 向上对齐到 4 字节边界
    uint32_t dataSize = stride * height;
    
    std::vector<uint8_t> bgraData(dataSize, 0); // 初始化为 0，确保对齐部分为 0
    
    // 转换 RGB24 到 BGRA8
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* rgbLine = frame->data[0] + y * frame->linesize[0];
        uint8_t* bgraLine = bgraData.data() + y * stride;
        
        for (uint32_t x = 0; x < width; x++) {
            bgraLine[x * 4 + 0] = rgbLine[x * 3 + 2]; // B
            bgraLine[x * 4 + 1] = rgbLine[x * 3 + 1]; // G
            bgraLine[x * 4 + 2] = rgbLine[x * 3 + 0]; // R
            bgraLine[x * 4 + 3] = 255;                // A
        }
        // 对齐部分已经初始化为 0，不需要额外处理
    }
    
    // 创建纹理
    auto texture = rhi->RHICreateTexture2D(
        RenderCore::EPixelFormat::PF_B8G8R8A8,
        RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
        static_cast<int32_t>(width),
        static_cast<int32_t>(height),
        1,  // NumMips
        bgraData.data(),
        static_cast<int>(stride)
    );
    
    if (!texture) {
        std::cerr << "[FFmpegVideoLoader] ConvertFrameToTexture: Failed to create texture (" 
                  << width << "x" << height << ", stride=" << stride << ")" << std::endl;
    } else {
        std::cout << "[FFmpegVideoLoader] ConvertFrameToTexture: Successfully created texture (" 
                  << width << "x" << height << ", stride=" << stride << ")" << std::endl;
    }
    
    return texture;
}

void FFmpegVideoLoader::Close() {
    if (m_SwsContext) {
        sws_freeContext(m_SwsContext);
        m_SwsContext = nullptr;
    }
    
    if (m_RGBFrame) {
        av_frame_free(&m_RGBFrame);
    }
    
    if (m_Frame) {
        av_frame_free(&m_Frame);
    }
    
    if (m_CodecContext) {
        avcodec_free_context(&m_CodecContext);
    }
    
    if (m_FormatContext) {
        avformat_close_input(&m_FormatContext);
    }
    
    m_VideoStreamIndex = -1;
    m_CurrentFrameIndex = 0;
    m_IsOpen = false;
    
    std::cout << "[FFmpegVideoLoader] Video closed" << std::endl;
}

int64_t FFmpegVideoLoader::GetCurrentFrameIndex() const {
    return m_IsOpen ? m_CurrentFrameIndex : -1;
}

int64_t FFmpegVideoLoader::GetCurrentTimestamp() const {
    if (!m_IsOpen) {
        return -1;
    }
    return static_cast<int64_t>(m_CurrentFrameIndex * 1000000.0 / m_Metadata.frameRate);
}

bool FFmpegVideoLoader::IsOpen() const {
    return m_IsOpen;
}

} // namespace LightroomCore

