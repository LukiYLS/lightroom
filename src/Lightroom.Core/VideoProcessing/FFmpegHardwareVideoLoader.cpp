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
		if (m_IsOpen)
			Close();

		int pathLen = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (pathLen <= 0) return false;
		std::vector<char> utf8Path(pathLen);
		WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, utf8Path.data(), pathLen, nullptr, nullptr);

		m_FormatContext = avformat_alloc_context();
		if (!m_FormatContext) return false;

		if (avformat_open_input(&m_FormatContext, utf8Path.data(), nullptr, nullptr) < 0) {
			m_FormatContext = nullptr;
			return false;
		}

		if (avformat_find_stream_info(m_FormatContext, nullptr) < 0) {
			goto Exit;
		}

		// 查找视频流
		m_VideoStreamIndex = -1;
		for (unsigned int i = 0; i < m_FormatContext->nb_streams; i++) {
			if (m_FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
				m_VideoStreamIndex = i;
				break;
			}
		}
		if (m_VideoStreamIndex == -1)
			goto Exit;

		// 查找解码器
		{
			AVCodecParameters* codecpar = m_FormatContext->streams[m_VideoStreamIndex]->codecpar;
			const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
			if (!codec)
				goto Exit;

			// 检查硬件支持
			bool supportsHardware = false;
			for (int i = 0; ; i++) {
				const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
				if (!config) break;
				if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
					config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
					supportsHardware = true;
					break;
				}
			}
			if (!supportsHardware)
				goto Exit;

			// 分配上下文
			m_CodecContext = avcodec_alloc_context3(codec);
			if (!m_CodecContext)
				goto Exit;

			if (avcodec_parameters_to_context(m_CodecContext, codecpar) < 0)
				goto Exit;

			// 设置回调
			m_CodecContext->get_format = [](AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
				for (const enum AVPixelFormat* p = pix_fmts; *p != -1; p++) {
					if (*p == AV_PIX_FMT_D3D11) return *p;
				}
				return AV_PIX_FMT_NONE;
				};

			// 打开解码器
			if (avcodec_open2(m_CodecContext, codec, nullptr) < 0)
				goto Exit;
		}

		// 分配帧
		m_Frame = av_frame_alloc();
		m_SoftwareFrame = av_frame_alloc();
		if (!m_Frame || !m_SoftwareFrame)
			goto Exit;

		// 获取元数据
		{
			AVStream* videoStream = m_FormatContext->streams[m_VideoStreamIndex];
			m_Metadata.width = m_CodecContext->width;
			m_Metadata.height = m_CodecContext->height;
			m_Metadata.frameRate = av_q2d(videoStream->r_frame_rate);
			m_Metadata.duration = m_FormatContext->duration;
			m_Metadata.totalFrames = videoStream->nb_frames;
			if (m_Metadata.totalFrames <= 0 && m_Metadata.frameRate > 0) {
				m_Metadata.totalFrames = (int64_t)((double)m_FormatContext->duration / AV_TIME_BASE * m_Metadata.frameRate);
			}
			m_TimeBase = videoStream->time_base;
			m_FrameDuration = av_q2d(av_inv_q(videoStream->r_frame_rate)) * 1000000.0;
		}

		m_CurrentFrameIndex = 0;
		m_IsOpen = true;
		return true;

		// 统一的错误清理代码块
	Exit:
		if (m_SoftwareFrame) av_frame_free(&m_SoftwareFrame);
		if (m_Frame) av_frame_free(&m_Frame);
		if (m_CodecContext) avcodec_free_context(&m_CodecContext);
		if (m_FormatContext) avformat_close_input(&m_FormatContext);
		m_FormatContext = nullptr;
		return false;
	}

	bool FFmpegHardwareVideoLoader::InitializeHardwareDecoding(std::shared_ptr<RenderCore::DynamicRHI> rhi) {
		if (m_HwDeviceCtx) return true;

		if (!rhi) return false;
		auto* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(rhi.get());
		if (!d3d11RHI) return false;
		ID3D11Device* d3d11Device = d3d11RHI->GetDevice();
		if (!d3d11Device) return false;

		// 1. 创建 Device Context
		m_HwDeviceCtx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
		if (!m_HwDeviceCtx) return false;

		AVHWDeviceContext* hwDeviceCtx = (AVHWDeviceContext*)m_HwDeviceCtx->data;
		AVD3D11VADeviceContext* d3d11vaDeviceCtx = (AVD3D11VADeviceContext*)hwDeviceCtx->hwctx;

		d3d11vaDeviceCtx->device = d3d11Device;
		d3d11Device->AddRef();

		if (av_hwdevice_ctx_init(m_HwDeviceCtx) < 0) {
			av_buffer_unref(&m_HwDeviceCtx);
			return false;
		}

		m_CodecContext->hw_device_ctx = av_buffer_ref(m_HwDeviceCtx);

		// 2. 创建 Frames Context
		m_HwFramesCtx = av_hwframe_ctx_alloc(m_HwDeviceCtx);
		if (!m_HwFramesCtx) return false;

		AVHWFramesContext* framesCtx = (AVHWFramesContext*)m_HwFramesCtx->data;
		framesCtx->format = AV_PIX_FMT_D3D11;
		framesCtx->sw_format = AV_PIX_FMT_NV12;
		framesCtx->width = m_CodecContext->width;
		framesCtx->height = m_CodecContext->height;
		framesCtx->initial_pool_size = 8;

		AVD3D11VAFramesContext* d3d11vaFramesCtx = (AVD3D11VAFramesContext*)framesCtx->hwctx;
		d3d11vaFramesCtx->BindFlags = D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE;
		d3d11vaFramesCtx->texture = nullptr;
		d3d11vaFramesCtx->MiscFlags = 0;

		if (av_hwframe_ctx_init(m_HwFramesCtx) < 0) {
			av_buffer_unref(&m_HwFramesCtx);
			return false;
		}

		m_CodecContext->hw_frames_ctx = av_buffer_ref(m_HwFramesCtx);
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
        if (!m_IsOpen || !m_FormatContext) {
            return false;
        }

        int64_t seekTarget = av_rescale_q(timestamp, { 1, 1000000 }, m_TimeBase);
        if (av_seek_frame(m_FormatContext, m_VideoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
            return false;
        }

        // 刷新解码器缓冲区（确保CodecContext有效且已打开）
        // 注意：在硬件解码器中，CodecContext 在 Open() 时分配，但 avcodec_open2 在 InitializeHardwareDecoding() 中调用
        // InitializeHardwareDecoding() 通常在第一次 ReadNextFrame() 时调用
        // 因此，如果 Seek 在第一次 ReadNextFrame 之前调用，CodecContext 可能还未通过 avcodec_open2 打开
        // 在这种情况下，我们跳过 flush，因为还没有解码器缓冲区需要刷新
        if (m_CodecContext) {
            // 检查 codec_id 确保 codec 至少已分配
            if (m_CodecContext->codec_id != AV_CODEC_ID_NONE) {
                // 检查 codec 是否已打开（通过检查 codec 指针）
                // 如果 codec 指针为 nullptr，说明 avcodec_open2 还未调用
                // 在这种情况下，不需要刷新缓冲区（因为还没有缓冲区）
                if (m_CodecContext->codec != nullptr) {
                    avcodec_flush_buffers(m_CodecContext);
                }
                // 如果 codec 未打开，我们继续执行，因为 seek 操作本身已经完成
            }
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
		if (!packet)
			return false;

		bool success = false;
		while (av_read_frame(m_FormatContext, packet) >= 0) {
			if (packet->stream_index == m_VideoStreamIndex) {
				if (avcodec_send_packet(m_CodecContext, packet) < 0) {
					av_packet_unref(packet);
					continue;
				}

				int ret = avcodec_receive_frame(m_CodecContext, m_Frame);
				av_packet_unref(packet);

				if (ret == 0) {
					success = true;
					break;
				}
				else if (ret == AVERROR(EAGAIN)) {
					continue;
				}
				else {
					break;
				}
			}
			else {
				av_packet_unref(packet);
			}
		}
		av_packet_free(&packet);
		return success;
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
		if (!m_IsOpen || !rhi) return nullptr;

		// 1. 初始化硬件解码 (如果未初始化)
		if (!m_HwDeviceCtx) {
			if (!InitializeHardwareDecoding(rhi)) {
				std::cerr << "[FFmpegLoader] Failed to init hardware decoding" << std::endl;
				return nullptr;
			}
		}

		// 2. 解码一帧
		if (!DecodeFrame()) return nullptr;
		if (!m_Frame || m_Frame->width <= 0 || m_Frame->height <= 0) return nullptr;

		// 3. 处理硬件帧 (Zero-Copy 路径)
		if (m_Frame->format == AV_PIX_FMT_D3D11) {

			RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(rhi.get());
			ID3D11Device* device = d3d11RHI->GetDevice();
			ID3D11DeviceContext* context = d3d11RHI->GetDeviceContext();

			// 获取解码器输出的纹理和索引
			// data[0] 是 ID3D11Texture2D 指针
			// data[1] 是纹理数组中的索引 (Array Slice Index)
			ID3D11Texture2D* srcTexture = (ID3D11Texture2D*)(uintptr_t)m_Frame->data[0];
			intptr_t srcIndex = (intptr_t)m_Frame->data[1];

			if (!srcTexture) return nullptr;

			// ---------------------------------------------------------------------
			// 准备 Staging 纹理 (仅在尺寸变化时重建，复用显存)
			// ---------------------------------------------------------------------
			// 这里的 StagingTexture 其实是一个 Default Usage 的纹理，用于作为 SRV 输入
			uint32_t width = m_Frame->width;
			uint32_t height = m_Frame->height;

			bool needRecreate = !m_StagingTexture || m_CachedStagingWidth != width || m_CachedStagingHeight != height;

			if (needRecreate) {
				// 清理旧资源
				m_StagingTexture.Reset();
				m_StagingYSRV.Reset();
				m_StagingUVSRV.Reset();

				// 创建 NV12 纹理 (Shader Resource)
				D3D11_TEXTURE2D_DESC desc = {};
				desc.Width = width;
				desc.Height = height;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_NV12;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT; // GPU 读写
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.CPUAccessFlags = 0; // 不需要 CPU 访问

				if (FAILED(device->CreateTexture2D(&desc, nullptr, m_StagingTexture.ReleaseAndGetAddressOf()))) {
					return nullptr;
				}

				// 创建 SRV (Y平面 R8)
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = 1;
				srvDesc.Format = DXGI_FORMAT_R8_UNORM;
				device->CreateShaderResourceView(m_StagingTexture.Get(), &srvDesc, m_StagingYSRV.ReleaseAndGetAddressOf());

				// 创建 SRV (UV平面 R8G8)
				srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
				device->CreateShaderResourceView(m_StagingTexture.Get(), &srvDesc, m_StagingUVSRV.ReleaseAndGetAddressOf());

				m_CachedStagingWidth = width;
				m_CachedStagingHeight = height;
			}

			// ---------------------------------------------------------------------
			// GPU 直接拷贝 (显存对显存)
			// ---------------------------------------------------------------------
			// srcIndex 是解码器纹理数组的子资源索引
			// 0 是目标纹理的子资源索引
			context->CopySubresourceRegion(
				m_StagingTexture.Get(), 0, 0, 0, 0, // 目标：Mip0, (0,0,0)
				srcTexture, (UINT)srcIndex, nullptr // 源：指定 Slice, 拷贝整个区域
			);

			// ---------------------------------------------------------------------
			// 后续处理 (YUV -> RGB Shader)
			// ---------------------------------------------------------------------

			// 初始化转换节点
			if (!m_CachedYUVToRGBNode || m_CachedRHI != rhi) {
				m_CachedYUVToRGBNode = std::make_unique<YUVToRGBNode>(rhi);
				m_CachedYUVToRGBNode->SetYUVFormat(YUVToRGBNode::YUVFormat::NV12);
				m_CachedYUVToRGBNode->InitializeShaderResources();
				m_CachedRHI = rhi;
			}

			// 准备输出纹理
			if (!m_CachedRGBTexture || m_CachedWidth != width || m_CachedHeight != height) {
				m_CachedRGBTexture = rhi->RHICreateTexture2D(
					RenderCore::EPixelFormat::PF_B8G8R8A8,
					RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
					width, height, 1);
				m_CachedWidth = width;
				m_CachedHeight = height;
			}

			// 设置 SRV 并执行
			m_CachedYUVToRGBNode->SetCustomShaderResourceViews(m_StagingYSRV.Get(), m_StagingUVSRV.Get());

			// Dummy wrapper 只是为了满足接口，实际上 SRV 已经在上面 Set 了
			auto dummyWrapper = std::make_shared<RenderCore::D3D11Texture2D>(d3d11RHI);
			bool success = m_CachedYUVToRGBNode->Execute(dummyWrapper, m_CachedRGBTexture, width, height);

			// 清理引用
			m_CachedYUVToRGBNode->SetCustomShaderResourceViews(nullptr, nullptr);

			if (!success)
				return nullptr;

			m_CurrentFrameIndex++;
			return m_CachedRGBTexture;
		}

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
		// 1. 清理硬件资源 (顺序很重要)
		if (m_HwFramesCtx)
			av_buffer_unref(&m_HwFramesCtx);
		if (m_HwDeviceCtx)
			av_buffer_unref(&m_HwDeviceCtx);

		// 2. 清理 FFmpeg 对象
		if (m_SoftwareFrame) {
			av_frame_free(&m_SoftwareFrame);
			m_SoftwareFrame = nullptr;
		}
		if (m_Frame) {
			av_frame_free(&m_Frame);
			m_Frame = nullptr;
		}
		if (m_CodecContext) {
			avcodec_free_context(&m_CodecContext);
			m_CodecContext = nullptr;
		}
		if (m_FormatContext) {
			avformat_close_input(&m_FormatContext);
			m_FormatContext = nullptr;
		}

		// 3. 清理 RHI 缓存
		m_CachedYUVToRGBNode.reset();
		m_CachedRGBTexture.reset();
		m_CachedRHI.reset();

		// 4. 清理 Staging 纹理 (ComPtr Reset 是安全的)
		m_StagingTexture.Reset();
		m_StagingYSRV.Reset();
		m_StagingUVSRV.Reset();

		m_CachedWidth = 0;
		m_CachedHeight = 0;
		m_CachedStagingWidth = 0;
		m_CachedStagingHeight = 0;
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






