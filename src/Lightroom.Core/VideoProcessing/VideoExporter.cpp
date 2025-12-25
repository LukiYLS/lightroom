#include "VideoExporter.h"
#include "VideoProcessor.h"
#include "../RenderGraph.h"
#include "../RenderNodes/ImageAdjustNode.h"
#include "../RenderNodes/FilterNode.h"
#include "../RenderNodes/RGBToYUVNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11Texture2D.h"
#include "../ImageProcessing/ImageExporter.h"

#include <Windows.h>
#include <dxgi1_3.h> // IDXGIDevice3::Trim
#include <d3d10_1.h> // ID3D10Multithread
#include <iostream>
#include <algorithm>

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

using Microsoft::WRL::ComPtr;

namespace LightroomCore {

	VideoExporter::VideoExporter()
		: m_IsExporting(false), m_ShouldCancel(false) {}

VideoExporter::~VideoExporter() {
    CancelExport();
}

bool VideoExporter::ExportVideo(
    const std::string& videoFilePath,
    RenderGraph* renderGraph,
    const std::string& outputPath,
		VideoExportProgressCallback callback)
	{
		if (m_IsExporting.exchange(true)) {
			m_LastError = "Export already in progress.";
        return false;
    }
    
		m_LastError.clear();
    m_ShouldCancel = false;

		// Convert path to wide string
		int len = MultiByteToWideChar(CP_UTF8, 0, videoFilePath.c_str(), -1, nullptr, 0);
		std::wstring wPath(len, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, videoFilePath.c_str(), -1, &wPath[0], len);

		m_ExportThread = std::thread([this, wPath, renderGraph, outputPath, callback]() {
			ExportThreadFunc(wPath, renderGraph, outputPath, callback);
    });
    
    return true;
}

void VideoExporter::CancelExport() {
    m_ShouldCancel = true;
    if (m_ExportThread.joinable()) {
        m_ExportThread.join();
    }
}

	bool VideoExporter::InitializeExportRHI() {
		if (m_ExportRHI) {
			m_ExportRHI->Shutdown();
			m_ExportRHI.reset();
		}

		m_ExportRHI = std::make_shared<RenderCore::D3D11DynamicRHI>();
		m_ExportRHI->Init();

		auto* d3dRHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_ExportRHI.get());
		if (!d3dRHI || !d3dRHI->GetDevice()) return false;

		// [Intel Fix] Enable multithread protection to prevent driver context collision
		ComPtr<ID3D10Multithread> pMultithread;
		if (SUCCEEDED(d3dRHI->GetDevice()->QueryInterface(__uuidof(ID3D10Multithread), &pMultithread))) {
			pMultithread->SetMultithreadProtected(TRUE);
		}
		return true;
	}

	std::unique_ptr<RenderGraph> VideoExporter::CloneRenderGraph(RenderGraph* source, std::shared_ptr<RenderCore::DynamicRHI> targetRHI) {
		if (!source || source->GetNodeCount() == 0) return nullptr;

		auto newGraph = std::make_unique<RenderGraph>(targetRHI);
		for (size_t i = 0; i < source->GetNodeCount(); ++i) {
			auto originalNode = source->GetNode(i);
			std::shared_ptr<RenderNode> clonedNode;

			if (auto adj = std::dynamic_pointer_cast<ImageAdjustNode>(originalNode)) {
				auto node = std::make_shared<ImageAdjustNode>(targetRHI);
				node->SetAdjustParams(adj->GetAdjustParams());
				clonedNode = node;
			}
			// Add other node types here...

			if (clonedNode) newGraph->AddNode(clonedNode);
		}
		return (newGraph->GetNodeCount() > 0) ? std::move(newGraph) : nullptr;
	}

	void VideoExporter::ExportThreadFunc(
		const std::wstring& videoPath,
		RenderGraph* sourceGraph,
		const std::string& outPath,
		VideoExportProgressCallback callback)
	{
		// 1. Setup isolated environment
		if (!InitializeExportRHI()) {
			m_LastError = "Failed to init export RHI";
            m_IsExporting = false;
			if (callback) callback(0, 0, 0);
            return;
        }
        
		auto* d3dRHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_ExportRHI.get());
		ID3D11Device* device = d3dRHI->GetDevice();
		ID3D11DeviceContext* context = d3dRHI->GetDeviceContext();

		// 2. Setup Processor & Graph
		auto processor = std::make_unique<VideoProcessor>(m_ExportRHI);
		if (!processor->OpenVideo(videoPath)) {
			m_LastError = "Failed to open video";
            m_IsExporting = false;
            return;
        }
        
		const auto* meta = processor->GetMetadata();
		// Warmup: Bind hardware decoder context
		if (!processor->GetNextFrame() || !processor->SeekToFrame(0)) {
			m_LastError = "Decoder warmup failed";
            m_IsExporting = false;
            return;
        }
        
		auto exportGraph = CloneRenderGraph(sourceGraph, m_ExportRHI);

		// 3. Setup Encoder & Resources
		FFmpegContext ctx;
		if (!InitEncoder(ctx, meta->width, meta->height, meta->frameRate, outPath)) {
			m_LastError = "Encoder init failed";
            m_IsExporting = false;
            return;
        }
        
		// Pre-allocate resources to prevent loop allocation overhead
		ComPtr<ID3D11Query> syncQuery;
		std::shared_ptr<RenderCore::RHITexture2D> processedTexture;
		
		// RGB to YUV conversion node (GPU-accelerated)
		std::unique_ptr<RGBToYUVNode> rgbToYuvNode = std::make_unique<RGBToYUVNode>(m_ExportRHI);
		
		// Staging textures for YUV readback (created once, reused for all frames)
		YUVStagingTextures stagingTextures;
		if (!stagingTextures.Init(device, meta->width, meta->height)) {
			m_LastError = "Failed to create staging textures";
            m_IsExporting = false;
            return;
        }
        
		// YUV textures for GPU conversion
		std::shared_ptr<RenderCore::RHITexture2D> yTexture = m_ExportRHI->RHICreateTexture2D(
			RenderCore::EPixelFormat::PF_R8,
			RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
			meta->width, meta->height, 1
		);
		
		uint32_t uvWidth = meta->width / 2;
		uint32_t uvHeight = meta->height / 2;
		std::shared_ptr<RenderCore::RHITexture2D> uTexture = m_ExportRHI->RHICreateTexture2D(
			RenderCore::EPixelFormat::PF_R8,
			RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
			uvWidth, uvHeight, 1
		);
		std::shared_ptr<RenderCore::RHITexture2D> vTexture = m_ExportRHI->RHICreateTexture2D(
			RenderCore::EPixelFormat::PF_R8,
			RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
			uvWidth, uvHeight, 1
		);

		// Sync query for Intel stability
		D3D11_QUERY_DESC qDesc = { D3D11_QUERY_EVENT, 0 };
		device->CreateQuery(&qDesc, syncQuery.GetAddressOf());

		// Output texture for RenderGraph
		if (exportGraph) {
			processedTexture = m_ExportRHI->RHICreateTexture2D(
				RenderCore::EPixelFormat::PF_B8G8R8A8,
				RenderCore::ETextureCreateFlags::TexCreate_RenderTargetable | RenderCore::ETextureCreateFlags::TexCreate_ShaderResource,
				meta->width, meta->height, 1
			);
		}

		// 4. Export Loop
		try {
			int64_t currentFrame = 0;
			while (currentFrame < meta->totalFrames && !m_ShouldCancel.load())
			{
				// Scope to force hardware texture release
				{
					auto frameTex = processor->GetNextFrame();
					if (!frameTex) break;

					auto targetTex = frameTex;

					// Execute RenderGraph if present
					if (exportGraph && processedTexture) {
						if (exportGraph->Execute(frameTex, processedTexture, meta->width, meta->height)) {
							targetTex = processedTexture;
							context->Flush(); // Ensure draw calls are submitted
						}
					}

					// Convert RGB to YUV on GPU
					if (!rgbToYuvNode->Execute(targetTex, yTexture, uTexture, vTexture, meta->width, meta->height)) {
						throw std::runtime_error("Failed to convert RGB to YUV");
					}

					// 确保所有渲染完成
					context->End(syncQuery.Get());
					context->Flush();

					// Wait for GPU (Prevents 'msg_end' crash on Intel)
					while (context->GetData(syncQuery.Get(), nullptr, 0, 0) == S_FALSE) {
						if (m_ShouldCancel.load()) break;
						std::this_thread::yield();
					}					
				}
				// frameTex destructor runs here -> FFmpeg ref count -1

				// Encode YUV textures directly (GPU data)
				EncodeFrame(ctx, yTexture, uTexture, vTexture, meta->width, meta->height, currentFrame, stagingTextures);

				// Memory Trim (Prevents OutOfMemory on long exports)
				if (currentFrame % 50 == 0) {
					context->ClearState();
					context->Flush();
					ComPtr<IDXGIDevice3> dxgiDev;
					if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice3), &dxgiDev))) {
						dxgiDev->Trim();
					}
				}

				if (callback) callback((double)currentFrame / meta->totalFrames, currentFrame, meta->totalFrames);
            currentFrame++;
        }
        
			FlushEncoder(ctx);
			if (callback) callback(1.0, meta->totalFrames, meta->totalFrames);

		}
		catch (const std::exception& e) {
			m_LastError = e.what();
			if (callback) callback(0.0, 0, 0);
		}

		// Cleanup
		CleanupContext(ctx);
		processor->CloseVideo();
		if (m_ExportRHI) {
			m_ExportRHI->Shutdown();
			m_ExportRHI.reset();
		}
    m_IsExporting = false;
}

	// -------------------------------------------------------------------------
	// FFmpeg Helpers
	// -------------------------------------------------------------------------
bool VideoExporter::InitEncoder(FFmpegContext& ctx, uint32_t w, uint32_t h, double fps, const std::string& path) {
	avformat_alloc_output_context2(&ctx.formatCtx, nullptr, nullptr, path.c_str());
	if (!ctx.formatCtx) return false;

	// 尝试寻找硬件编码器 (可选，如果不想折腾 NV12 格式，先继续用 libx265)
	const AVCodec* codec = avcodec_find_encoder_by_name("libx265");
	if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
	if (!codec) return false;

	ctx.codecCtx = avcodec_alloc_context3(codec);
	ctx.codecCtx->width = w;
	ctx.codecCtx->height = h;
	ctx.codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

	// [重要] 时间基准设置
	ctx.codecCtx->time_base = av_make_q(1, static_cast<int>(fps));
	ctx.codecCtx->framerate = av_make_q(static_cast<int>(fps), 1);

	// 4K 60fps 码率通常需要 20M - 40M
	ctx.codecCtx->bit_rate = 30000000; // 30 Mbps

	// 1. 开启多线程
	ctx.codecCtx->thread_count = 0;
	if (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS)
		ctx.codecCtx->thread_type = FF_THREAD_FRAME;

	// 2. 速度优先预设
	av_opt_set(ctx.codecCtx->priv_data, "preset", "ultrafast", 0);

	// 3. 降低延迟
	av_opt_set(ctx.codecCtx->priv_data, "tune", "zerolatency", 0);

	// 4. 适当放宽画质以换取速度 (23 -> 28)
	av_opt_set(ctx.codecCtx->priv_data, "crf", "28", 0);

	// 5. 限制 x265 内部参数以提升速度
	// - no-deblock: 关闭去块滤波（画质略降，速度升）
	// - no-sao: 关闭采样自适应偏移（显著提升 4K 编码速度）
	av_opt_set(ctx.codecCtx->priv_data, "x265-params", "no-deblock=1:no-sao=1", 0);

	if (avcodec_open2(ctx.codecCtx, codec, nullptr) < 0) return false;

	ctx.stream = avformat_new_stream(ctx.formatCtx, codec);
	ctx.stream->time_base = ctx.codecCtx->time_base;
	avcodec_parameters_from_context(ctx.stream->codecpar, ctx.codecCtx);

	if (!(ctx.formatCtx->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open(&ctx.formatCtx->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) return false;
	}
	ctx.packet = av_packet_alloc();
	if (!ctx.packet) return false;

	return avformat_write_header(ctx.formatCtx, nullptr) >= 0;
}

	void VideoExporter::EncodeFrame(FFmpegContext& ctx, 
	                                 std::shared_ptr<RenderCore::RHITexture2D> yTexture,
	                                 std::shared_ptr<RenderCore::RHITexture2D> uTexture,
	                                 std::shared_ptr<RenderCore::RHITexture2D> vTexture,
	                                 uint32_t w, uint32_t h, int64_t pts,
	                                 YUVStagingTextures& staging) {
		// Initialize frame if needed
		if (!ctx.frame) {
			ctx.frame = av_frame_alloc();
			ctx.frame->format = AV_PIX_FMT_YUV420P;
			ctx.frame->width = w; ctx.frame->height = h;
			av_frame_get_buffer(ctx.frame, 0);
		}
		// Get native D3D11 textures
		auto yD3D = std::dynamic_pointer_cast<RenderCore::D3D11Texture2D>(yTexture);
		auto uD3D = std::dynamic_pointer_cast<RenderCore::D3D11Texture2D>(uTexture);
		auto vD3D = std::dynamic_pointer_cast<RenderCore::D3D11Texture2D>(vTexture);
		
		if (!yD3D || !uD3D || !vD3D) {
			return;
		}

		ID3D11Texture2D* yTex = yD3D->GetNativeTex();
		ID3D11Texture2D* uTex = uD3D->GetNativeTex();
		ID3D11Texture2D* vTex = vD3D->GetNativeTex();

		// Read YUV data from GPU textures using pre-allocated staging textures
		if (!ReadYUVTextureData(yTex, uTex, vTex, w, h,
		                        ctx.frame->data[0], ctx.frame->data[1], ctx.frame->data[2],
		                        ctx.frame->linesize[0], ctx.frame->linesize[1], ctx.frame->linesize[2],
		                        staging)) {
			return;
		}
		
		// Convert U/V from Full Range [0, 255] to Limited Range [16, 240]
		// RGBToYUV outputs U/V in [0, 1] (Full Range), but encoder expects [16, 240] (Limited Range)
		uint32_t uvWidth = w / 2;
		uint32_t uvHeight = h / 2;
		for (uint32_t y = 0; y < uvHeight; ++y) {
			uint8_t* uLine = ctx.frame->data[1] + y * ctx.frame->linesize[1];
			uint8_t* vLine = ctx.frame->data[2] + y * ctx.frame->linesize[2];
			for (uint32_t x = 0; x < uvWidth; ++x) {
				// Map [0, 255] -> [16, 240]
				uLine[x] = (uint8_t)std::clamp((int)(uLine[x] * 224.0f / 255.0f + 16.0f), 16, 240);
				vLine[x] = (uint8_t)std::clamp((int)(vLine[x] * 224.0f / 255.0f + 16.0f), 16, 240);
			}
		}

		ctx.frame->pts = pts;
		if (avcodec_send_frame(ctx.codecCtx, ctx.frame) >= 0) {
			WritePackets(ctx);
		}
	}

	void VideoExporter::FlushEncoder(FFmpegContext& ctx) {
		if (ctx.codecCtx) {
			avcodec_send_frame(ctx.codecCtx, nullptr);
			WritePackets(ctx);
			if (ctx.formatCtx) av_write_trailer(ctx.formatCtx);
		}
	}

	void VideoExporter::WritePackets(FFmpegContext& ctx) {
		while (avcodec_receive_packet(ctx.codecCtx, ctx.packet) == 0) {
			av_packet_rescale_ts(ctx.packet, ctx.codecCtx->time_base, ctx.stream->time_base);
			ctx.packet->stream_index = ctx.stream->index;
			av_interleaved_write_frame(ctx.formatCtx, ctx.packet);
			av_packet_unref(ctx.packet);
		}
	}

	void VideoExporter::CleanupContext(FFmpegContext& ctx) {
		if (ctx.swsCtx) { sws_freeContext(ctx.swsCtx); ctx.swsCtx = nullptr; }
		if (ctx.rgbFrame) { av_frame_free(&ctx.rgbFrame); ctx.rgbFrame = nullptr; }
		if (ctx.frame) { av_frame_free(&ctx.frame); ctx.frame = nullptr; }
		if (ctx.codecCtx) { avcodec_free_context(&ctx.codecCtx); ctx.codecCtx = nullptr; }
		if (ctx.formatCtx) {
			if (ctx.formatCtx->pb) avio_closep(&ctx.formatCtx->pb);
			avformat_free_context(ctx.formatCtx);
			ctx.formatCtx = nullptr;
		}
		if (ctx.packet) av_packet_free(&ctx.packet);
	}

	bool VideoExporter::YUVStagingTextures::Init(ID3D11Device* device, uint32_t width, uint32_t height) {
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;

		if (FAILED(device->CreateTexture2D(&desc, nullptr, &Y))) {
        return false;
    }
    
		desc.Width = width / 2;
		desc.Height = height / 2;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &U))) {
        return false;
    }
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &V))) {
        return false;
    }
    
    return true;
}

	bool VideoExporter::ReadYUVTextureData(ID3D11Texture2D* yTex, ID3D11Texture2D* uTex, ID3D11Texture2D* vTex,
	                                        uint32_t width, uint32_t height,
	                                        uint8_t* yData, uint8_t* uData, uint8_t* vData,
	                                        uint32_t yStride, uint32_t uStride, uint32_t vStride,
	                                        YUVStagingTextures& staging) {
		if (!yTex || !uTex || !vTex || !yData || !uData || !vData) {
			return false;
		}

		RenderCore::D3D11DynamicRHI* d3d11RHI = dynamic_cast<RenderCore::D3D11DynamicRHI*>(m_ExportRHI.get());
		if (!d3d11RHI) {
			return false;
		}

		ID3D11DeviceContext* context = d3d11RHI->GetDeviceContext();
		if (!context) {
			return false;
		}

		// Helper lambda to read a texture plane using pre-allocated staging texture
		auto ReadPlane = [&](ID3D11Texture2D* texture, ID3D11Texture2D* stagingTex,
		                     uint32_t texWidth, uint32_t texHeight, 
		                     uint8_t* dstData, uint32_t dstStride) -> bool {
			// Copy to staging
			context->CopyResource(stagingTex, texture);

			// Map and read
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (FAILED(context->Map(stagingTex, 0, D3D11_MAP_READ, 0, &mapped))) {
				return false;
			}

			// Copy data with stride handling
			uint32_t copyWidth = std::min(texWidth, dstStride);
			for (uint32_t y = 0; y < texHeight; ++y) {
				memcpy(dstData + (y * dstStride), 
				       (uint8_t*)mapped.pData + (y * mapped.RowPitch), 
				       copyWidth);
			}

			context->Unmap(stagingTex, 0);
			return true;
		};

		// Read Y plane (full resolution)
		if (!ReadPlane(yTex, staging.Y.Get(), width, height, yData, yStride)) {
        return false;
    }
    
		// Read U plane (half resolution)
		uint32_t uvWidth = width / 2;
		uint32_t uvHeight = height / 2;
		if (!ReadPlane(uTex, staging.U.Get(), uvWidth, uvHeight, uData, uStride)) {
            return false;
        }
        
		// Read V plane (half resolution)
		if (!ReadPlane(vTex, staging.V.Get(), uvWidth, uvHeight, vData, vStride)) {
            return false;
    }
    
    return true;
}

} // namespace LightroomCore