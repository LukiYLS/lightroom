#include "FFmpegHardwareVideoLoader.h"
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
#include <dxgi.h>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

namespace LightroomCore {

    bool FFmpegHardwareVideoLoader::s_FFmpegInitialized = false;

    FFmpegHardwareVideoLoader::FFmpegHardwareVideoLoader()
        : m_FormatContext(nullptr)
        , m_CodecContext(nullptr)
        , m_Frame(nullptr)
        , m_SoftwareFrame(nullptr)
        , m_HwDeviceCtx(nullptr)
        , m_HwFramesCtx(nullptr)
        , m_VideoStreamIndex(-1)
        , m_CurrentFrameIndex(0)
        , m_IsOpen(false)
        , m_CachedYUVToRGBNode(nullptr)
        , m_CachedRHI(nullptr)
        , m_CachedRGBTexture(nullptr)
        , m_CachedWidth(0)
        , m_CachedHeight(0)
        , m_CachedStagingWidth(0)
        , m_CachedStagingHeight(0)
    {
        InitializeFFmpeg();
    }

    FFmpegHardwareVideoLoader::~FFmpegHardwareVideoLoader() {
        Close();
    }

    void FFmpegHardwareVideoLoader::InitializeFFmpeg() {
        if (!s_FFmpegInitialized) {
            avformat_network_init();
            s_FFmpegInitialized = true;
        }
    }

    bool FFmpegHardwareVideoLoader::CanLoad(const std::wstring& filePath) const {
        std::wstring ext = filePath.substr(filePath.find_last_of(L".") + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

        return ext == L"mp4" || ext == L"mov" || ext == L"avi" ||
            ext == L"mkv" || ext == L"m4v" || ext == L"flv" ||
            ext == L"webm" || ext == L"3gp";
    }

    bool FFmpegHardwareVideoLoader::Open(const std::wstring& filePath) {
        if (m_IsOpen) {
            Close();
        }

        // Convert wide char path to UTF-8
        int pathLen = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (pathLen <= 0) {
            return false;
        }

        std::vector<char> utf8Path(pathLen);
        WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, utf8Path.data(), pathLen, nullptr, nullptr);

        // Open input file
        m_FormatContext = avformat_alloc_context();
        if (avformat_open_input(&m_FormatContext, utf8Path.data(), nullptr, nullptr) < 0) {
            avformat_free_context(m_FormatContext);
            m_FormatContext = nullptr;
            return false;
        }

        // Find stream info
        if (avformat_find_stream_info(m_FormatContext, nullptr) < 0) {
            std::cerr << "[FFmpegHardwareVideoLoader] Failed to find stream info" << std::endl;
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
            Close();
            return false;
        }

        // Get codec
        AVCodecParameters* codecpar = m_FormatContext->streams[m_VideoStreamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            Close();
            return false;
        }

        // Check if codec supports hardware decoding (D3D11VA)
        // If not, return false early so system can fallback to software decoder
        bool supportsHardware = false;
        for (int i = 0; ; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
            if (!config) {
                break;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
                supportsHardware = true;
                break;
            }
        }

        if (!supportsHardware) {
            Close();
            return false;
        }

        // Allocate codec context
        m_CodecContext = avcodec_alloc_context3(codec);
        if (!m_CodecContext) {
            Close();
            return false;
        }

        // Copy codec parameters
        if (avcodec_parameters_to_context(m_CodecContext, codecpar) < 0) {
            Close();
            return false;
        }

        // Allocate frames
        m_Frame = av_frame_alloc();
        m_SoftwareFrame = av_frame_alloc();
        if (!m_Frame || !m_SoftwareFrame) {
            Close();
            return false;
        }

        // Get metadata (before hardware initialization)
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


        return true;
    }

    bool FFmpegHardwareVideoLoader::InitializeHardwareDecoding(std::shared_ptr<RenderCore::DynamicRHI> rhi) {
        if (m_HwDeviceCtx) {
            return true;  // Already initialized
        }

        if (!rhi) {
            std::cerr << "[FFmpegHardwareVideoLoader] RHI is null" << std::endl;
            return false;
        }

        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(rhi.get());
        if (!d3d11RHI) {
            std::cerr << "[FFmpegHardwareVideoLoader] Failed to cast to D3D11DynamicRHI" << std::endl;
            return false;
        }

        ID3D11Device* d3d11Device = d3d11RHI->GetDevice();
        if (!d3d11Device) {
            std::cerr << "[FFmpegHardwareVideoLoader] Failed to get D3D11 device" << std::endl;
            return false;
        }

        // Create hardware device context
        int ret = av_hwdevice_ctx_create(&m_HwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
        if (ret < 0) {
            std::cerr << "[FFmpegHardwareVideoLoader] Failed to create hardware device context" << std::endl;
            return false;
        }

        // Get D3D11VA device context
        AVHWDeviceContext* hwDeviceCtx = (AVHWDeviceContext*)m_HwDeviceCtx->data;
        AVD3D11VADeviceContext* d3d11vaDeviceCtx = (AVD3D11VADeviceContext*)hwDeviceCtx->hwctx;

        // Set the D3D11 device (if not already set)
        if (!d3d11vaDeviceCtx->device) {
            d3d11vaDeviceCtx->device = d3d11Device;
            d3d11Device->AddRef();  // FFmpeg will release it
        }

        // Set device context for codec
        m_CodecContext->hw_device_ctx = av_buffer_ref(m_HwDeviceCtx);

        // Find codec again (needed for hardware config lookup)
        const AVCodec* codec = avcodec_find_decoder(m_CodecContext->codec_id);
        if (!codec) {
            std::cerr << "[FFmpegHardwareVideoLoader] Codec not found for hardware decoding" << std::endl;
            CleanupHardwareDecoding();
            return false;
        }

        // Find hardware config
        const AVCodecHWConfig* hwConfig = nullptr;
        std::cout << "[FFmpegHardwareVideoLoader] Looking for hardware config for codec: " << codec->name 
                  << " (codec_id: " << m_CodecContext->codec_id << ")" << std::endl;
        
        for (int i = 0; ; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
            if (!config) {
                break;
            }
            
            std::cout << "[FFmpegHardwareVideoLoader] Found hardware config " << i 
                      << ": device_type=" << config->device_type 
                      << ", methods=" << config->methods << std::endl;
            
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
                hwConfig = config;
                std::cout << "[FFmpegHardwareVideoLoader] Found matching D3D11VA hardware config!" << std::endl;
                break;
            }
        }

        if (!hwConfig) {
            std::cerr << "[FFmpegHardwareVideoLoader] No hardware config found for codec " 
                      << codec->name << " (codec_id: " << m_CodecContext->codec_id << ")" << std::endl;
            std::cerr << "[FFmpegHardwareVideoLoader] This codec may not support D3D11VA hardware decoding" << std::endl;
            std::cerr << "[FFmpegHardwareVideoLoader] Common codecs that support D3D11VA: h264, hevc (h265), vp9, av1" << std::endl;
            CleanupHardwareDecoding();
            return false;
        }

        // Set pixel format
        m_CodecContext->get_format = [](AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) -> enum AVPixelFormat {
            const enum AVPixelFormat* p;
            for (p = pix_fmts; *p != -1; p++) {
                if (*p == AV_PIX_FMT_D3D11) {
                    return *p;
                }
            }
            return AV_PIX_FMT_NONE;
            };

        // Open codec with hardware context (codec already found above)
        if (avcodec_open2(m_CodecContext, codec, nullptr) < 0) {
            std::cerr << "[FFmpegHardwareVideoLoader] Failed to open codec with hardware context" << std::endl;
            CleanupHardwareDecoding();
            return false;
        }

        // Create hardware frames context
        m_HwFramesCtx = av_hwframe_ctx_alloc(m_HwDeviceCtx);
        if (!m_HwFramesCtx) {
            std::cerr << "[FFmpegHardwareVideoLoader] Failed to allocate hardware frames context" << std::endl;
            CleanupHardwareDecoding();
            return false;
        }

        AVHWFramesContext* framesCtx = (AVHWFramesContext*)m_HwFramesCtx->data;
        framesCtx->format = AV_PIX_FMT_D3D11;
        framesCtx->sw_format = AV_PIX_FMT_NV12;
        framesCtx->width = m_CodecContext->width;
        framesCtx->height = m_CodecContext->height;
        framesCtx->initial_pool_size = 20;

        // Set D3D11VA specific parameters
        AVD3D11VAFramesContext* d3d11vaFramesCtx = (AVD3D11VAFramesContext*)framesCtx->hwctx;
        if (d3d11vaFramesCtx) {
            // Set BindFlags for decoder usage
            d3d11vaFramesCtx->BindFlags = D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE;
            // Let FFmpeg allocate the texture (set to NULL)
            d3d11vaFramesCtx->texture = nullptr;
            // Use the same device as the device context
            d3d11vaFramesCtx->MiscFlags = 0;
        }

        int initRet = av_hwframe_ctx_init(m_HwFramesCtx);
        if (initRet < 0) {
            char errBuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(initRet, errBuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "[FFmpegHardwareVideoLoader] Failed to initialize hardware frames context: " 
                      << errBuf << " (error code: " << initRet << ")" << std::endl;
            std::cerr << "[FFmpegHardwareVideoLoader] Width: " << framesCtx->width 
                      << ", Height: " << framesCtx->height << std::endl;
            std::cerr << "[FFmpegHardwareVideoLoader] Format: " << framesCtx->format 
                      << ", SW Format: " << framesCtx->sw_format << std::endl;
            if (d3d11vaFramesCtx) {
                std::cerr << "[FFmpegHardwareVideoLoader] BindFlags: " << d3d11vaFramesCtx->BindFlags << std::endl;
            }
            CleanupHardwareDecoding();
            return false;
        }

        m_CodecContext->hw_frames_ctx = av_buffer_ref(m_HwFramesCtx);

        std::cout << "[FFmpegHardwareVideoLoader] Hardware decoding initialized successfully" << std::endl;
        return true;
    }

    void FFmpegHardwareVideoLoader::CleanupHardwareDecoding() {
        if (m_HwFramesCtx) {
            av_buffer_unref(&m_HwFramesCtx);
        }

        if (m_HwDeviceCtx) {
            av_buffer_unref(&m_HwDeviceCtx);
        }

        if (m_CodecContext) {
            m_CodecContext->hw_device_ctx = nullptr;
            m_CodecContext->hw_frames_ctx = nullptr;
        }
    }

    bool FFmpegHardwareVideoLoader::GetMetadata(VideoMetadata& metadata) const {
        if (!m_IsOpen) {
            return false;
        }
        metadata = m_Metadata;
        return true;
    }

    bool FFmpegHardwareVideoLoader::Seek(int64_t timestamp) {
        if (!m_IsOpen) {
            return false;
        }

        int64_t seekTarget = av_rescale_q(timestamp, { 1, 1000000 }, m_TimeBase);
        if (av_seek_frame(m_FormatContext, m_VideoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
            return false;
        }

        if (m_CodecContext) {
            avcodec_flush_buffers(m_CodecContext);
        }
        m_CurrentFrameIndex = (int64_t)((double)timestamp / m_FrameDuration);

        return true;
    }

    bool FFmpegHardwareVideoLoader::SeekToFrame(int64_t frameIndex) {
        if (!m_IsOpen) {
            return false;
        }

        int64_t timestamp = static_cast<int64_t>(frameIndex * m_FrameDuration);
        return Seek(timestamp);
    }

    bool FFmpegHardwareVideoLoader::DecodeFrame() {
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

    std::shared_ptr<RenderCore::RHITexture2D> FFmpegHardwareVideoLoader::GetHardwareFrameTexture(
        std::shared_ptr<RenderCore::DynamicRHI> rhi) {
        if (!m_Frame || m_Frame->format != AV_PIX_FMT_D3D11) {
            return nullptr;
        }

        // Get D3D11 texture from hardware frame
        ID3D11Texture2D* d3d11Texture = (ID3D11Texture2D*)(uintptr_t)m_Frame->data[0];
        if (!d3d11Texture) {
            return nullptr;
        }

        // Wrap D3D11 texture in RHI texture using CreateFromExistingTexture
        RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(rhi.get());
        if (!d3d11RHI) {
            return nullptr;
        }

        // Create RHI texture wrapper for the D3D11 texture
        // Hardware decoded frames are typically in NV12 format
        std::shared_ptr<RenderCore::D3D11Texture2D> d3d11TextureWrapper = std::make_shared<RenderCore::D3D11Texture2D>(d3d11RHI);
        if (!d3d11TextureWrapper->CreateFromExistingTexture(d3d11Texture, RenderCore::EPixelFormat::PF_NV12)) {
            std::cerr << "[FFmpegHardwareVideoLoader] Failed to create RHI texture from D3D11 texture" << std::endl;
            return nullptr;
        }

        return d3d11TextureWrapper;
    }

    // -------------------------------------------------------------------------
    // 核心函数 2：读取下一帧并渲染
    // -------------------------------------------------------------------------
    std::shared_ptr<RenderCore::RHITexture2D> FFmpegHardwareVideoLoader::ReadNextFrame(
        std::shared_ptr<RenderCore::DynamicRHI> rhi) 
    {
        if (!m_IsOpen || !rhi) {
            return nullptr;
        }

        // 确保硬件解码上下文已初始化
        if (!m_HwDeviceCtx) {
            if (!InitializeHardwareDecoding(rhi)) {
                std::cerr << "[FFmpegHardwareVideoLoader] Failed to initialize hardware decoding" << std::endl;
                return nullptr;
            }
        }

        // FFmpeg 解码一帧到 m_Frame
        {
            ScopedTimer timer("DecodeFrame", m_CurrentFrameIndex);
            if (!DecodeFrame()) {
                return nullptr;
            }
        }

        // 基本校验
        if (!m_Frame || m_Frame->width <= 0 || m_Frame->height <= 0) {
            return nullptr;
        }

        // 处理 D3D11 硬件帧 - 使用 av_hwframe_transfer_data 转换为软件帧
        if (m_Frame->format == AV_PIX_FMT_D3D11) {
            
            RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(rhi.get());
            if (!d3d11RHI) {
                return nullptr;
            }

            ID3D11Device* device = d3d11RHI->GetDevice();
            ID3D11DeviceContext* context = d3d11RHI->GetDeviceContext();
            //ID3D11Texture2D* srcTextureArray = (ID3D11Texture2D*)(uintptr_t)m_Frame->data[0];
            // 1. 使用 av_hwframe_transfer_data 将硬件帧转换为软件帧（NV12格式）
            {
                ScopedTimer timer("HWFrameTransfer", m_CurrentFrameIndex);
                
                // 先清空软件帧（确保是"clean"状态，让 av_hwframe_transfer_data 自动分配缓冲区）
                av_frame_unref(m_SoftwareFrame);
                
                // 设置软件帧格式为 NV12（必须在调用 av_hwframe_transfer_data 之前设置）
                // 如果设置为 AV_PIX_FMT_NONE，函数会自动选择第一个支持的格式
                m_SoftwareFrame->format = AV_PIX_FMT_NV12;
                m_SoftwareFrame->width = m_Frame->width;
                m_SoftwareFrame->height = m_Frame->height;
                
                // 从硬件帧传输到软件帧
                // 注意：如果 dst->buf[0] 未设置（clean状态），函数会自动调用 av_frame_get_buffer() 分配缓冲区
                int ret = av_hwframe_transfer_data(m_SoftwareFrame, m_Frame, 0);
                if (ret < 0) {
                    char errBuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errBuf, AV_ERROR_MAX_STRING_SIZE);
                    std::cerr << "[FFmpegHardwareVideoLoader] Failed to transfer hardware frame to software frame: " 
                              << errBuf << " (error code: " << ret << ")" << std::endl;
                    std::cerr << "[FFmpegHardwareVideoLoader] Hardware frame: " << m_Frame->width << "x" << m_Frame->height 
                              << ", format: " << m_Frame->format << std::endl;
                    std::cerr << "[FFmpegHardwareVideoLoader] Software frame requested: " << m_SoftwareFrame->width << "x" 
                              << m_SoftwareFrame->height << ", format: " << m_SoftwareFrame->format << std::endl;
                    return nullptr;
                }
                
                std::cout << "[FFmpegHardwareVideoLoader] Transferred hardware frame to software frame: " 
                          << m_SoftwareFrame->width << "x" << m_SoftwareFrame->height 
                          << ", format: " << m_SoftwareFrame->format << std::endl;
            }
            
            // 2. 从软件帧（NV12格式）创建纹理
            // NV12格式：data[0] = Y平面，data[1] = UV平面（交错）
            uint32_t width = m_SoftwareFrame->width;
            uint32_t height = m_SoftwareFrame->height;
            
            // 准备中转纹理（单层 NV12 纹理）
            bool needRecreateStaging = false;
            if (!m_StagingTexture || m_CachedStagingWidth != width || m_CachedStagingHeight != height) {
                needRecreateStaging = true;
            }
            
            if (needRecreateStaging) {
                // 清理旧资源
                m_StagingTexture.Reset();
                m_StagingYSRV.Reset();
                m_StagingUVSRV.Reset();
                
                // 创建单层 NV12 纹理
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = width;
                desc.Height = height;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_NV12;
                desc.SampleDesc.Count = 1;
                desc.SampleDesc.Quality = 0;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.CPUAccessFlags = 0;
                desc.MiscFlags = 0;
                
                HRESULT hr = device->CreateTexture2D(&desc, nullptr, m_StagingTexture.ReleaseAndGetAddressOf());
                if (FAILED(hr)) {
                    std::cerr << "[FFmpegHardwareVideoLoader] Failed to create staging texture: HRESULT=0x"
                              << std::hex << hr << std::dec << std::endl;
                    return nullptr;
                }
                
                // 创建 SRV
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDescY = {};
                srvDescY.Format = DXGI_FORMAT_R8_UNORM;
                srvDescY.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDescY.Texture2D.MostDetailedMip = 0;
                srvDescY.Texture2D.MipLevels = 1;
                hr = device->CreateShaderResourceView(m_StagingTexture.Get(), &srvDescY, m_StagingYSRV.ReleaseAndGetAddressOf());
                if (FAILED(hr)) {
                    std::cerr << "[FFmpegHardwareVideoLoader] Failed to create Y SRV: HRESULT=0x" << std::hex << hr << std::dec << std::endl;
                    return nullptr;
                }
                
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDescUV = {};
                srvDescUV.Format = DXGI_FORMAT_R8G8_UNORM;
                srvDescUV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDescUV.Texture2D.MostDetailedMip = 0;
                srvDescUV.Texture2D.MipLevels = 1;
                hr = device->CreateShaderResourceView(m_StagingTexture.Get(), &srvDescUV, m_StagingUVSRV.ReleaseAndGetAddressOf());
                if (FAILED(hr)) {
                    std::cerr << "[FFmpegHardwareVideoLoader] Failed to create UV SRV: HRESULT=0x" << std::hex << hr << std::dec << std::endl;
                    m_StagingYSRV.Reset();
                    return nullptr;
                }
                
                m_CachedStagingWidth = width;
                m_CachedStagingHeight = height;
                
                std::cout << "[FFmpegHardwareVideoLoader] Created staging texture: " << width << "x" << height << std::endl;
            }
            
            // 3. 上传软件帧数据到纹理
            {
                ScopedTimer timer("UploadToTexture", m_CurrentFrameIndex);
                
                // NV12格式：Y平面在data[0]，UV平面在data[1]（交错存储）
                // 需要分别上传Y和UV数据
                // 注意：NV12纹理需要特殊处理，因为UV是交错的
                
                // 创建临时staging纹理用于上传
                D3D11_TEXTURE2D_DESC stagingDesc = {};
                stagingDesc.Width = width;
                stagingDesc.Height = height;
                stagingDesc.MipLevels = 1;
                stagingDesc.ArraySize = 1;
                stagingDesc.Format = DXGI_FORMAT_NV12;
                stagingDesc.SampleDesc.Count = 1;
                stagingDesc.SampleDesc.Quality = 0;
                stagingDesc.Usage = D3D11_USAGE_STAGING;
                stagingDesc.BindFlags = 0;
                stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                stagingDesc.MiscFlags = 0;
                
                ComPtr<ID3D11Texture2D> uploadTexture;
                HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, uploadTexture.GetAddressOf());
                if (FAILED(hr)) {
                    std::cerr << "[FFmpegHardwareVideoLoader] Failed to create upload staging texture: HRESULT=0x"
                              << std::hex << hr << std::dec << std::endl;
                    return nullptr;
                }
                
                // 映射纹理并上传数据
                D3D11_MAPPED_SUBRESOURCE mapped;
                hr = context->Map(uploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &mapped);
                if (SUCCEEDED(hr)) {
                    // 上传Y平面
                    uint8_t* dstY = (uint8_t*)mapped.pData;
                    uint8_t* srcY = m_SoftwareFrame->data[0];
                    uint32_t yPitch = m_SoftwareFrame->linesize[0];
                    
                    for (uint32_t y = 0; y < height; y++) {
                        memcpy(dstY, srcY, width);
                        dstY += mapped.RowPitch;
                        srcY += yPitch;
                    }
                    
                    // 上传UV平面（交错存储，在Y平面之后）
                    uint8_t* dstUV = (uint8_t*)mapped.pData + mapped.RowPitch * height;
                    uint8_t* srcUV = m_SoftwareFrame->data[1];
                    uint32_t uvPitch = m_SoftwareFrame->linesize[1];
                    uint32_t uvHeight = height / 2;  // UV平面高度是Y平面的一半
                    
                    for (uint32_t y = 0; y < uvHeight; y++) {
                        memcpy(dstUV, srcUV, width);  // UV交错，宽度与Y相同
                        dstUV += mapped.RowPitch;
                        srcUV += uvPitch;
                    }
                    
                    context->Unmap(uploadTexture.Get(), 0);
                    
                    // 从staging纹理复制到目标纹理
                    context->CopyResource(m_StagingTexture.Get(), uploadTexture.Get());
                    
                    std::cout << "[FFmpegHardwareVideoLoader] Uploaded software frame data to texture" << std::endl;
                } else {
                    std::cerr << "[FFmpegHardwareVideoLoader] Failed to map upload texture: HRESULT=0x"
                              << std::hex << hr << std::dec << std::endl;
                    return nullptr;
                }
            }

            // 5. 准备渲染节点 (YUVToRGBNode)
            if (!m_CachedYUVToRGBNode || m_CachedRHI != rhi) {
                m_CachedYUVToRGBNode = std::make_unique<YUVToRGBNode>(rhi);
                m_CachedYUVToRGBNode->SetYUVFormat(YUVToRGBNode::YUVFormat::NV12);
                // 只需要初始化 Shader，不需要纹理，因为我们会手动注入 SRV
                if (!m_CachedYUVToRGBNode->InitializeShaderResources()) {
                    std::cerr << "[FFmpegHardwareVideoLoader] Failed to initialize YUV to RGB shader resources" << std::endl;
                    m_CachedYUVToRGBNode.reset();
                    return nullptr;
                }
                m_CachedRHI = rhi;
                std::cout << "[FFmpegHardwareVideoLoader] Initialized cached YUVToRGBNode" << std::endl;
            }

            // 6. 准备输出 RGB 纹理
            if (!m_CachedRGBTexture || m_CachedWidth != m_Frame->width || m_CachedHeight != m_Frame->height) {
                m_CachedRGBTexture = rhi->RHICreateTexture2D(
                    RenderCore::EPixelFormat::PF_B8G8R8A8,
                    RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
                    m_Frame->width, m_Frame->height, 1);

                if (!m_CachedRGBTexture) {
                    std::cerr << "[FFmpegHardwareVideoLoader] Failed to create RGB output texture" << std::endl;
                    return nullptr;
                }

                m_CachedWidth = m_Frame->width;
                m_CachedHeight = m_Frame->height;
            }

            // 4. 将准备好的 SRV 注入 Node
            // m_StagingYSRV 和 m_StagingUVSRV 可能直接指向纹理数组切片，或者指向复制后的 staging texture
            m_CachedYUVToRGBNode->SetCustomShaderResourceViews(m_StagingYSRV.Get(), m_StagingUVSRV.Get());

            // 7. 执行 YUV 到 RGB 转换
            // 创建一个临时的 dummy 包装器满足接口签名（内容不重要，因为 SRV 已经被上面的 SetCustom 覆盖了）
            auto dummyWrapper = std::make_shared<RenderCore::D3D11Texture2D>(d3d11RHI);
            
            bool bSuccess = false;
            {
                ScopedTimer timer("ColorConversion", m_CurrentFrameIndex);
                bSuccess = m_CachedYUVToRGBNode->Execute(dummyWrapper, m_CachedRGBTexture, m_Frame->width, m_Frame->height);
            }

            // 8. 清理 SRV 引用
            // 防止 Node 持有 m_StagingYSRV 的引用，虽然是 ComPtr 没大问题，但保持状态清洁是个好习惯
            m_CachedYUVToRGBNode->SetCustomShaderResourceViews(nullptr, nullptr);

            if (!bSuccess) {
                std::cerr << "[FFmpegHardwareVideoLoader] Failed to convert YUV to RGB" << std::endl;
                return nullptr;
            }

            m_CurrentFrameIndex++;
            return m_CachedRGBTexture;
        }

        // Fallback: should not happen if hardware decoding is working
        std::cerr << "[FFmpegHardwareVideoLoader] Frame is not hardware format" << std::endl;
        return nullptr;
    }

    std::shared_ptr<RenderCore::RHITexture2D> FFmpegHardwareVideoLoader::ReadFrame(
        int64_t frameIndex,
        std::shared_ptr<RenderCore::DynamicRHI> rhi) {
        if (!SeekToFrame(frameIndex)) {
            return nullptr;
        }

        return ReadNextFrame(rhi);
    }

    void FFmpegHardwareVideoLoader::Close() {
        CleanupHardwareDecoding();

        // Clear cached resources
        m_CachedYUVToRGBNode.reset();
        m_CachedRGBTexture.reset();
        m_CachedRHI.reset();
        m_CachedWidth = 0;
        m_CachedHeight = 0;
        
        // Clear staging resources
        m_StagingTexture.Reset();
        m_StagingYSRV.Reset();
        m_StagingUVSRV.Reset();
        m_CachedStagingWidth = 0;
        m_CachedStagingHeight = 0;

        if (m_SoftwareFrame) {
            av_frame_free(&m_SoftwareFrame);
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

    int64_t FFmpegHardwareVideoLoader::GetCurrentFrameIndex() const {
        return m_IsOpen ? m_CurrentFrameIndex : -1;
    }

    int64_t FFmpegHardwareVideoLoader::GetCurrentTimestamp() const {
        if (!m_IsOpen || m_CurrentFrameIndex < 0) {
            return -1;
        }
        return (int64_t)(m_CurrentFrameIndex * m_FrameDuration);
    }

    bool FFmpegHardwareVideoLoader::IsOpen() const {
        return m_IsOpen;
    }

    bool FFmpegHardwareVideoLoader::IsHardwareFrame() const {
        return m_Frame && m_Frame->format == AV_PIX_FMT_D3D11;
    }

} // namespace LightroomCore






