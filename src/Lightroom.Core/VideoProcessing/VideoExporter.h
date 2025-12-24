#pragma once

#include "../d3d11rhi/DynamicRHI.h"
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>

// FFmpeg forward declarations
extern "C" {
    struct AVFormatContext;
    struct AVCodecContext;
    struct AVStream;
    struct AVFrame;
    struct SwsContext;
}

namespace LightroomCore {

    class VideoProcessor;
    class RenderGraph;

    // 进度回调: progress (0.0-1.0), currentFrame, totalFrames
    using VideoExportProgressCallback = std::function<void(double, int64_t, int64_t)>;

    class VideoExporter {
    public:
        VideoExporter();
        ~VideoExporter();

        /**
         * 启动异步视频导出任务
         * @param videoFilePath 源视频路径
         * @param renderGraph   渲染图 (将根据导出设备自动克隆)
         * @param outputPath    输出文件路径
         * @param callback      进度回调
         */
        bool ExportVideo(
            const std::string& videoFilePath,
            RenderGraph* renderGraph,
            const std::string& outputPath,
            VideoExportProgressCallback callback = nullptr
        );

        bool IsExporting() const { return m_IsExporting.load(); }
        void CancelExport();
        std::string GetLastError() const { return m_LastError; }

    private:
        // FFmpeg 上下文容器 (RAII 管理)
        struct FFmpegContext {
            AVFormatContext* formatCtx = nullptr;
            AVCodecContext* codecCtx = nullptr;
            AVStream* stream = nullptr;
            AVFrame* frame = nullptr;      // YUV420P
            AVFrame* rgbFrame = nullptr;   // RGB24
            SwsContext* swsCtx = nullptr;
        };

        void ExportThreadFunc(
            const std::wstring& videoPath,
            RenderGraph* sourceGraph,
            const std::string& outPath,
            VideoExportProgressCallback callback
        );

        bool InitializeExportRHI();
        std::unique_ptr<RenderGraph> CloneRenderGraph(RenderGraph* source, std::shared_ptr<RenderCore::DynamicRHI> targetRHI);

        // 编码管线
        bool InitEncoder(FFmpegContext& ctx, uint32_t w, uint32_t h, double fps, const std::string& path);
        void EncodeFrame(FFmpegContext& ctx, const std::vector<uint8_t>& bgraData, uint32_t w, uint32_t h, uint32_t bgraStride, int64_t pts);
        void FlushEncoder(FFmpegContext& ctx);
        void WritePackets(FFmpegContext& ctx);
        void CleanupContext(FFmpegContext& ctx);

        // 成员变量
        std::shared_ptr<RenderCore::DynamicRHI> m_ExportRHI; // 导出独占 RHI
        std::atomic<bool> m_IsExporting;
        std::atomic<bool> m_ShouldCancel;
        std::thread       m_ExportThread;
        std::string       m_LastError;
    };

} // namespace LightroomCore