#include "FFmpegSoftwareVideoLoader.h"
#include "VideoPerformanceProfiler.h"
#include "../d3d11rhi/DynamicRHI.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include "../d3d11rhi/Common.h"
#include "../RenderNodes/YUVToRGBNode.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>

using Microsoft::WRL::ComPtr;

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace LightroomCore {

    bool FFmpegSoftwareVideoLoader::s_FFmpegInitialized = false;

    FFmpegSoftwareVideoLoader::FFmpegSoftwareVideoLoader()
        : m_FormatContext(nullptr)
        , m_CodecContext(nullptr)
        , m_Frame(nullptr)
        , m_ConvertedFrame(nullptr)
        , m_SwsContext(nullptr)
        , m_VideoStreamIndex(-1)
        , m_CurrentFrameIndex(0)
        , m_IsOpen(false)
        , m_CachedYUVToRGBNode(nullptr)
        , m_CachedRHI(nullptr)
        , m_CachedRGBTexture(nullptr)
        , m_CachedWidth(0)
        , m_CachedHeight(0)
        , m_YTexture(nullptr)
        , m_UTexture(nullptr)
        , m_VTexture(nullptr)
        , m_CachedYUVWidth(0)
        , m_CachedYUVHeight(0)
    {
        InitializeFFmpeg();
    }

    FFmpegSoftwareVideoLoader::~FFmpegSoftwareVideoLoader() {
        Close();
    }

    void FFmpegSoftwareVideoLoader::InitializeFFmpeg() {
        if (!s_FFmpegInitialized) {
            avformat_network_init();
            s_FFmpegInitialized = true;
            std::cout << "[FFmpegSoftwareVideoLoader] FFmpeg initialized" << std::endl;
        }
    }

    bool FFmpegSoftwareVideoLoader::CanLoad(const std::wstring& filePath) const {
        std::wstring ext = filePath.substr(filePath.find_last_of(L".") + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

        return ext == L"mp4" || ext == L"mov" || ext == L"avi" ||
            ext == L"mkv" || ext == L"m4v" || ext == L"flv" ||
            ext == L"webm" || ext == L"3gp";
    }

    bool FFmpegSoftwareVideoLoader::Open(const std::wstring& filePath) {
        if (m_IsOpen) {
            Close();
        }

        // Convert wide char path to UTF-8
        int pathLen = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (pathLen <= 0) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to convert path to UTF-8" << std::endl;
            return false;
        }

        std::vector<char> utf8Path(pathLen);
        WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, utf8Path.data(), pathLen, nullptr, nullptr);

        // Open input file
        m_FormatContext = avformat_alloc_context();
        if (avformat_open_input(&m_FormatContext, utf8Path.data(), nullptr, nullptr) < 0) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to open input file" << std::endl;
            avformat_free_context(m_FormatContext);
            m_FormatContext = nullptr;
            return false;
        }

        // Find stream info
        if (avformat_find_stream_info(m_FormatContext, nullptr) < 0) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to find stream info" << std::endl;
            Close();
            return false;
        }

        // Find video stream
        m_VideoStreamIndex = -1;
        for (unsigned int i = 0; i < m_FormatContext->nb_streams; i++) {
            if (m_FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                m_VideoStreamIndex = i;
                break;
            }
        }

        if (m_VideoStreamIndex == -1) {
            std::cerr << "[FFmpegSoftwareVideoLoader] No video stream found" << std::endl;
            Close();
            return false;
        }

        // Get codec
        AVCodecParameters* codecpar = m_FormatContext->streams[m_VideoStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Codec not found for codec_id: " << codecpar->codec_id << std::endl;
            std::cerr << "[FFmpegSoftwareVideoLoader] This codec may not be compiled in FFmpeg" << std::endl;
            std::cerr << "[FFmpegSoftwareVideoLoader] For ProRes (codec_id=" << AV_CODEC_ID_PRORES 
                      << "), ensure FFmpeg was compiled with ProRes decoder support" << std::endl;
            Close();
            return false;
        }
        
        std::cout << "[FFmpegSoftwareVideoLoader] Using software decoder for codec: " << codec->name 
                  << " (codec_id: " << codecpar->codec_id << ")" << std::endl;

        // Allocate codec context
        m_CodecContext = avcodec_alloc_context3(codec);
        if (!m_CodecContext) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to allocate codec context" << std::endl;
            Close();
            return false;
        }

        // Copy codec parameters
        if (avcodec_parameters_to_context(m_CodecContext, codecpar) < 0) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to copy codec parameters" << std::endl;
            Close();
            return false;
        }

        // Open codec
        if (avcodec_open2(m_CodecContext, codec, nullptr) < 0) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to open codec" << std::endl;
            Close();
            return false;
        }

        // Allocate frames
        m_Frame = av_frame_alloc();
        m_ConvertedFrame = av_frame_alloc();
        if (!m_Frame || !m_ConvertedFrame) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to allocate frames" << std::endl;
            Close();
            return false;
        }

        // Get metadata
        AVStream* videoStream = m_FormatContext->streams[m_VideoStreamIndex];
        m_Metadata.width = m_CodecContext->width;
        m_Metadata.height = m_CodecContext->height;
        m_Metadata.frameRate = av_q2d(videoStream->r_frame_rate);
        m_Metadata.totalFrames = videoStream->nb_frames;
        if (m_Metadata.totalFrames <= 0) {
            m_Metadata.totalFrames = (int64_t)((double)m_FormatContext->duration / AV_TIME_BASE * m_Metadata.frameRate);
        }
        m_Metadata.duration = m_FormatContext->duration;
        m_TimeBase = videoStream->time_base;
        m_FrameDuration = av_q2d(av_inv_q(videoStream->r_frame_rate)) * 1000000.0;  // microseconds

        m_CurrentFrameIndex = 0;
        m_IsOpen = true;

        std::cout << "[FFmpegSoftwareVideoLoader] Video opened: " << m_Metadata.width
            << "x" << m_Metadata.height << " @ " << m_Metadata.frameRate << " fps" << std::endl;

        return true;
    }

    bool FFmpegSoftwareVideoLoader::GetMetadata(VideoMetadata& metadata) const {
        if (!m_IsOpen) {
            return false;
        }
        metadata = m_Metadata;
        return true;
    }

    bool FFmpegSoftwareVideoLoader::Seek(int64_t timestamp) {
        if (!m_IsOpen) {
            return false;
        }

        int64_t seekTarget = av_rescale_q(timestamp, { 1, 1000000 }, m_TimeBase);
        if (av_seek_frame(m_FormatContext, m_VideoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
            return false;
        }

        avcodec_flush_buffers(m_CodecContext);
        m_CurrentFrameIndex = (int64_t)((double)timestamp / m_FrameDuration);

        return true;
    }

    bool FFmpegSoftwareVideoLoader::SeekToFrame(int64_t frameIndex) {
        if (!m_IsOpen) {
            return false;
        }

        int64_t timestamp = static_cast<int64_t>(frameIndex * m_FrameDuration);
        return Seek(timestamp);
    }

    bool FFmpegSoftwareVideoLoader::DecodeFrame() {
        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            return false;
        }

        while (av_read_frame(m_FormatContext, packet) >= 0) {
            if (packet->stream_index == m_VideoStreamIndex) {
                if (avcodec_send_packet(m_CodecContext, packet) < 0) {
                    av_packet_unref(packet);
                    continue;
                }

                int ret = avcodec_receive_frame(m_CodecContext, m_Frame);
                av_packet_unref(packet);

                if (ret == 0) {
                    av_packet_free(&packet);
                    return true;
                }
                else if (ret == AVERROR(EAGAIN)) {
                    continue;
                }
                else {
                    av_packet_free(&packet);
                    return false;
                }
            }
            else {
                av_packet_unref(packet);
            }
        }

        av_packet_free(&packet);
        return false;
    }

    bool FFmpegSoftwareVideoLoader::EnsureYUVTextures(std::shared_ptr<RenderCore::DynamicRHI> rhi, 
                                                       uint32_t width, uint32_t height,
                                                       uint32_t yWidth, uint32_t yHeight,
                                                       uint32_t uvWidth, uint32_t uvHeight) {
        if (m_YTexture && m_UTexture && m_VTexture && 
            m_CachedYUVWidth == width && m_CachedYUVHeight == height) {
            return true;  // Textures already exist and size matches
        }
        
        // Clean up old textures
        m_YTexture.reset();
        m_UTexture.reset();
        m_VTexture.reset();
        
        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(rhi.get());
        if (!d3d11RHI) {
            return false;
        }
        
        ID3D11Device* device = d3d11RHI->GetDevice();
        
        // Create Y texture (R8_UNORM, full resolution)
        m_YTexture = rhi->RHICreateTexture2D(
            RenderCore::EPixelFormat::PF_R8,
            RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
            yWidth, yHeight, 1);
        
        if (!m_YTexture) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to create Y texture" << std::endl;
            return false;
        }
        
        // Create U texture (R8_UNORM, quarter resolution for YUV420P)
        m_UTexture = rhi->RHICreateTexture2D(
            RenderCore::EPixelFormat::PF_R8,
            RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
            uvWidth, uvHeight, 1);
        
        if (!m_UTexture) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to create U texture" << std::endl;
            m_YTexture.reset();
            return false;
        }
        
        // Create V texture (R8_UNORM, quarter resolution for YUV420P)
        m_VTexture = rhi->RHICreateTexture2D(
            RenderCore::EPixelFormat::PF_R8,
            RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
            uvWidth, uvHeight, 1);
        
        if (!m_VTexture) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to create V texture" << std::endl;
            m_YTexture.reset();
            m_UTexture.reset();
            return false;
        }
        
        // Get SRVs from textures (they are managed by the texture objects, so we just store raw pointers)
        RenderCore::D3D11Texture2D* yD3D11Tex = dynamic_cast<RenderCore::D3D11Texture2D*>(m_YTexture.get());
        RenderCore::D3D11Texture2D* uD3D11Tex = dynamic_cast<RenderCore::D3D11Texture2D*>(m_UTexture.get());
        RenderCore::D3D11Texture2D* vD3D11Tex = dynamic_cast<RenderCore::D3D11Texture2D*>(m_VTexture.get());
        
        if (yD3D11Tex && uD3D11Tex && vD3D11Tex) {
            // GetSRV() returns raw pointer, but the SRV is managed by the texture object
            // We'll use these pointers directly when setting custom SRVs
            // Note: We don't need to store them in ComPtr since they're managed by the texture
        }
        
        m_CachedYUVWidth = width;
        m_CachedYUVHeight = height;
        
        std::cout << "[FFmpegSoftwareVideoLoader] Created YUV textures: Y=" << yWidth << "x" << yHeight
                  << ", U/V=" << uvWidth << "x" << uvHeight << std::endl;
        
        return true;
    }

    std::shared_ptr<RenderCore::RHITexture2D> FFmpegSoftwareVideoLoader::ReadNextFrame(
        std::shared_ptr<RenderCore::DynamicRHI> rhi) {
        if (!m_IsOpen || !rhi) {
            return nullptr;
        }

        {
            ScopedTimer timer("DecodeFrame", m_CurrentFrameIndex);
            if (!DecodeFrame()) {
                return nullptr;
            }
        }

        if (!m_Frame || m_Frame->width <= 0 || m_Frame->height <= 0) {
            return nullptr;
        }

        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(rhi.get());
        if (!d3d11RHI) {
            return nullptr;
        }

        ID3D11Device* device = d3d11RHI->GetDevice();
        ID3D11DeviceContext* context = d3d11RHI->GetDeviceContext();

        uint32_t width = m_Frame->width;
        uint32_t height = m_Frame->height;
        
        // Determine pixel format and handle format conversion if needed
        AVPixelFormat pixFmt = (AVPixelFormat)m_Frame->format;
        AVFrame* frameToUse = m_Frame;  // Default to original frame
        
        // Check if format conversion is needed
        bool needsConversion = (pixFmt != AV_PIX_FMT_YUV420P);
        
        if (needsConversion) {
            // Convert to YUV420P using sws_scale (only for format conversion, not color conversion)
            {
                ScopedTimer timer("FormatConversion", m_CurrentFrameIndex);
                
                // Initialize or update SWS context if format changed
                if (!m_SwsContext || m_CodecContext->width != width || m_CodecContext->height != height) {
                    if (m_SwsContext) {
                        sws_freeContext(m_SwsContext);
                        m_SwsContext = nullptr;
                    }
                    
                    m_SwsContext = sws_getContext(
                        width, height, pixFmt,
                        width, height, AV_PIX_FMT_YUV420P,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                    
                    if (!m_SwsContext) {
                        std::cerr << "[FFmpegSoftwareVideoLoader] Failed to create SWS context for format conversion" << std::endl;
                        return nullptr;
                    }
                    
                    // Setup converted frame
                    m_ConvertedFrame->format = AV_PIX_FMT_YUV420P;
                    m_ConvertedFrame->width = width;
                    m_ConvertedFrame->height = height;
                    int ret = av_frame_get_buffer(m_ConvertedFrame, 32);
                    if (ret < 0) {
                        std::cerr << "[FFmpegSoftwareVideoLoader] Failed to allocate converted frame buffer" << std::endl;
                        return nullptr;
                    }
                }
                
                // Convert format (e.g., YUVA444P12LE -> YUV420P)
                sws_scale(m_SwsContext,
                    m_Frame->data, m_Frame->linesize, 0, height,
                    m_ConvertedFrame->data, m_ConvertedFrame->linesize);
                
                frameToUse = m_ConvertedFrame;
                std::cout << "[FFmpegSoftwareVideoLoader] Converted format from " << pixFmt << " to YUV420P" << std::endl;
            }
        }
        
        // YUV420P: Y plane is full resolution, U and V planes are quarter resolution
        uint32_t yWidth = width;
        uint32_t yHeight = height;
        uint32_t uvWidth = (width + 1) / 2;   // Round up for odd widths
        uint32_t uvHeight = (height + 1) / 2;  // Round up for odd heights
        
        // 1. Ensure YUV textures exist
        if (!EnsureYUVTextures(rhi, width, height, yWidth, yHeight, uvWidth, uvHeight)) {
            return nullptr;
        }
        
        // 2. Upload YUV data to textures
        {
            ScopedTimer timer("UploadYUVToTexture", m_CurrentFrameIndex);
            
            // Upload Y plane (use converted frame if format was converted)
            RenderCore::D3D11Texture2D* yD3D11Tex = dynamic_cast<RenderCore::D3D11Texture2D*>(m_YTexture.get());
            if (yD3D11Tex && yD3D11Tex->GetNativeTex()) {
                context->UpdateSubresource(
                    yD3D11Tex->GetNativeTex(),
                    0,
                    nullptr,
                    frameToUse->data[0],  // Y plane data
                    frameToUse->linesize[0],  // Y plane pitch
                    0
                );
            }
            
            // Upload U plane
            RenderCore::D3D11Texture2D* uD3D11Tex = dynamic_cast<RenderCore::D3D11Texture2D*>(m_UTexture.get());
            if (uD3D11Tex && uD3D11Tex->GetNativeTex()) {
                context->UpdateSubresource(
                    uD3D11Tex->GetNativeTex(),
                    0,
                    nullptr,
                    frameToUse->data[1],  // U plane data
                    frameToUse->linesize[1],  // U plane pitch
                    0
                );
            }
            
            // Upload V plane
            RenderCore::D3D11Texture2D* vD3D11Tex = dynamic_cast<RenderCore::D3D11Texture2D*>(m_VTexture.get());
            if (vD3D11Tex && vD3D11Tex->GetNativeTex()) {
                context->UpdateSubresource(
                    vD3D11Tex->GetNativeTex(),
                    0,
                    nullptr,
                    frameToUse->data[2],  // V plane data
                    frameToUse->linesize[2],  // V plane pitch
                    0
                );
            }
        }
        
        // 3. Prepare YUVToRGBNode
        if (!m_CachedYUVToRGBNode || m_CachedRHI != rhi) {
            m_CachedYUVToRGBNode = std::make_unique<YUVToRGBNode>(rhi);
            m_CachedYUVToRGBNode->SetYUVFormat(YUVToRGBNode::YUVFormat::YUV420P);
            if (!m_CachedYUVToRGBNode->InitializeShaderResources()) {
                std::cerr << "[FFmpegSoftwareVideoLoader] Failed to initialize YUV to RGB shader resources" << std::endl;
                m_CachedYUVToRGBNode.reset();
                return nullptr;
            }
            m_CachedRHI = rhi;
            std::cout << "[FFmpegSoftwareVideoLoader] Initialized cached YUVToRGBNode" << std::endl;
        }
        
        // 4. Prepare output RGB texture
        if (!m_CachedRGBTexture || m_CachedWidth != width || m_CachedHeight != height) {
            m_CachedRGBTexture = rhi->RHICreateTexture2D(
                RenderCore::EPixelFormat::PF_B8G8R8A8,
                RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
                width, height, 1);
            
            if (!m_CachedRGBTexture) {
                std::cerr << "[FFmpegSoftwareVideoLoader] Failed to create RGB output texture" << std::endl;
                return nullptr;
            }
            
            m_CachedWidth = width;
            m_CachedHeight = height;
        }
        
        // 5. Set custom SRVs for YUV420P format
        RenderCore::D3D11Texture2D* yD3D11Tex = dynamic_cast<RenderCore::D3D11Texture2D*>(m_YTexture.get());
        RenderCore::D3D11Texture2D* uD3D11Tex = dynamic_cast<RenderCore::D3D11Texture2D*>(m_UTexture.get());
        RenderCore::D3D11Texture2D* vD3D11Tex = dynamic_cast<RenderCore::D3D11Texture2D*>(m_VTexture.get());
        
        if (yD3D11Tex && uD3D11Tex && vD3D11Tex) {
            m_CachedYUVToRGBNode->SetCustomShaderResourceViewsYUV420P(
                yD3D11Tex->GetSRV(), 
                uD3D11Tex->GetSRV(), 
                vD3D11Tex->GetSRV());
        } else {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to get SRVs from YUV textures" << std::endl;
            return nullptr;
        }
        
        // 6. Execute YUV to RGB conversion
        auto dummyWrapper = std::make_shared<RenderCore::D3D11Texture2D>(d3d11RHI);
        
        bool bSuccess = false;
        {
            ScopedTimer timer("ColorConversion", m_CurrentFrameIndex);
            bSuccess = m_CachedYUVToRGBNode->Execute(dummyWrapper, m_CachedRGBTexture, width, height);
        }
        
        // 7. Clear SRV references
        m_CachedYUVToRGBNode->SetCustomShaderResourceViewsYUV420P(nullptr, nullptr, nullptr);
        
        if (!bSuccess) {
            std::cerr << "[FFmpegSoftwareVideoLoader] Failed to convert YUV to RGB" << std::endl;
            return nullptr;
        }
        
        m_CurrentFrameIndex++;
        return m_CachedRGBTexture;
    }

    std::shared_ptr<RenderCore::RHITexture2D> FFmpegSoftwareVideoLoader::ReadFrame(
        int64_t frameIndex,
        std::shared_ptr<RenderCore::DynamicRHI> rhi) {
        if (!SeekToFrame(frameIndex)) {
            return nullptr;
        }

        return ReadNextFrame(rhi);
    }

    void FFmpegSoftwareVideoLoader::Close() {
        // Clear SWS context
        if (m_SwsContext) {
            sws_freeContext(m_SwsContext);
            m_SwsContext = nullptr;
        }
        
        // Clear cached resources
        m_CachedYUVToRGBNode.reset();
        m_CachedRGBTexture.reset();
        m_CachedRHI.reset();
        m_CachedWidth = 0;
        m_CachedHeight = 0;
        
        // Clear YUV textures
        m_YTexture.reset();
        m_UTexture.reset();
        m_VTexture.reset();
        m_CachedYUVWidth = 0;
        m_CachedYUVHeight = 0;

        if (m_ConvertedFrame) {
            av_frame_free(&m_ConvertedFrame);
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
    }

    int64_t FFmpegSoftwareVideoLoader::GetCurrentFrameIndex() const {
        return m_IsOpen ? m_CurrentFrameIndex : -1;
    }

    int64_t FFmpegSoftwareVideoLoader::GetCurrentTimestamp() const {
        if (!m_IsOpen || m_CurrentFrameIndex < 0) {
            return -1;
        }
        return (int64_t)(m_CurrentFrameIndex * m_FrameDuration);
    }

    bool FFmpegSoftwareVideoLoader::IsOpen() const {
		return m_IsOpen;
	}

} // namespace LightroomCore






