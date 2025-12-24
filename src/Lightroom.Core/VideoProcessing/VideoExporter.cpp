#include "VideoExporter.h"
#include "VideoProcessor.h"
#include "../RenderGraph.h"
#include "../RenderNodes/ImageAdjustNode.h"
#include "../RenderNodes/FilterNode.h"
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
		std::vector<uint8_t> frameData;
		uint32_t frameStride = 0;
		uint32_t readWidth = 0, readHeight = 0;
		ComPtr<ID3D11Query> syncQuery;
		std::shared_ptr<RenderCore::RHITexture2D> processedTexture;
		ImageExporter imageExporter(m_ExportRHI);

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

					auto d3dWrapper = std::dynamic_pointer_cast<RenderCore::D3D11Texture2D>(targetTex);
					ID3D11Texture2D* nativeTex = d3dWrapper->GetNativeTex();

					// 确保渲染完成
					context->End(syncQuery.Get());
					context->Flush();

					// Wait for GPU (Prevents 'msg_end' crash on Intel)
					while (context->GetData(syncQuery.Get(), nullptr, 0, 0) == S_FALSE) {
						if (m_ShouldCancel.load()) break;
						std::this_thread::yield();
					}

					// 使用 ImageExporter 读取纹理数据（正确处理 stride）
					if (!imageExporter.ReadD3D11TextureData(nativeTex, readWidth, readHeight, frameData, frameStride)) {
						throw std::runtime_error("Failed to read texture data");
					}
				}
				// frameTex destructor runs here -> FFmpeg ref count -1

				EncodeFrame(ctx, frameData, readWidth, readHeight, frameStride, currentFrame);

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

		const AVCodec* codec = avcodec_find_encoder_by_name("libx265");
		if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
		if (!codec) return false;

		ctx.codecCtx = avcodec_alloc_context3(codec);
		ctx.codecCtx->width = w;
		ctx.codecCtx->height = h;
		ctx.codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
		ctx.codecCtx->time_base = av_make_q(1000, static_cast<int>(fps * 1000 + 0.5));
		ctx.codecCtx->framerate = av_inv_q(ctx.codecCtx->time_base);
		ctx.codecCtx->bit_rate = 10000000; // 10Mbps
		ctx.codecCtx->gop_size = 30;

		av_opt_set(ctx.codecCtx->priv_data, "preset", "medium", 0);
		av_opt_set(ctx.codecCtx->priv_data, "crf", "23", 0);

		if (avcodec_open2(ctx.codecCtx, codec, nullptr) < 0) return false;

		ctx.stream = avformat_new_stream(ctx.formatCtx, codec);
		ctx.stream->time_base = ctx.codecCtx->time_base;
		avcodec_parameters_from_context(ctx.stream->codecpar, ctx.codecCtx);

		if (!(ctx.formatCtx->oformat->flags & AVFMT_NOFILE)) {
			if (avio_open(&ctx.formatCtx->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) return false;
		}

		return avformat_write_header(ctx.formatCtx, nullptr) >= 0;
	}

	void VideoExporter::EncodeFrame(FFmpegContext& ctx, const std::vector<uint8_t>& bgraData, uint32_t w, uint32_t h, uint32_t bgraStride, int64_t pts) {
		if (!ctx.rgbFrame) {
			ctx.rgbFrame = av_frame_alloc();
			ctx.rgbFrame->format = AV_PIX_FMT_RGB24;
			ctx.rgbFrame->width = w; ctx.rgbFrame->height = h;
			av_frame_get_buffer(ctx.rgbFrame, 0);

			ctx.frame = av_frame_alloc();
			ctx.frame->format = AV_PIX_FMT_YUV420P;
			ctx.frame->width = w; ctx.frame->height = h;
			av_frame_get_buffer(ctx.frame, 0);

			ctx.swsCtx = sws_getContext(w, h, AV_PIX_FMT_RGB24, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
		}

		// BGRA -> RGB (CPU conversion, 正确处理 stride)
		uint8_t* dst = ctx.rgbFrame->data[0];
		const uint8_t* src = bgraData.data();
		uint32_t rgbStride = ctx.rgbFrame->linesize[0];
		
		for (uint32_t y = 0; y < h; ++y) {
			const uint8_t* srcRow = src + (y * bgraStride);
			uint8_t* dstRow = dst + (y * rgbStride);
			for (uint32_t x = 0; x < w; ++x) {
				dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // R
				dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
				dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // B
			}
		}

		// RGB -> YUV
		sws_scale(ctx.swsCtx, ctx.rgbFrame->data, ctx.rgbFrame->linesize, 0, h, ctx.frame->data, ctx.frame->linesize);

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
		AVPacket* pkt = av_packet_alloc();
		while (avcodec_receive_packet(ctx.codecCtx, pkt) == 0) {
			av_packet_rescale_ts(pkt, ctx.codecCtx->time_base, ctx.stream->time_base);
			pkt->stream_index = ctx.stream->index;
			av_interleaved_write_frame(ctx.formatCtx, pkt);
			av_packet_unref(pkt);
		}
		av_packet_free(&pkt);
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
	}

} // namespace LightroomCore