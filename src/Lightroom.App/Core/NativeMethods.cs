using System;
using System.Runtime.InteropServices;

namespace Lightroom.App.Core
{
    public static class NativeMethods
    {
        private const string DllName = "LightroomCore.dll";

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool InitSDK();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ShutdownSDK();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool ProcessImage(string inputPath, string outputPath, float brightness, float contrast);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int GetSDKVersion();

        // D3D11 渲染接口 - 图片编辑区渲染目标
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr CreateRenderTarget(uint width, uint height);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void DestroyRenderTarget(IntPtr renderTargetHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr GetRenderTargetSharedHandle(IntPtr renderTargetHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern bool LoadImageToTarget(IntPtr renderTargetHandle, string imagePath);

        // 渲染到目标（双缓冲+拷贝策略，内部处理，WPF端无需关心缓冲区切换）
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool RenderToTarget(IntPtr renderTargetHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ResizeRenderTarget(IntPtr renderTargetHandle, uint width, uint height);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void SetRenderTargetZoom(IntPtr renderTargetHandle, double zoomLevel, double panX, double panY);

        // 图像调整参数结构（与 C++ 中的 ImageAdjustParams 对应）
        [StructLayout(LayoutKind.Sequential)]
        public struct ImageAdjustParams
        {
            // 基本调整
            public float exposure;
            public float contrast;
            public float highlights;
            public float shadows;
            public float whites;
            public float blacks;
            
            // 白平衡
            public float temperature;
            public float tint;
            
            // 颜色调整
            public float vibrance;
            public float saturation;
            
            // HSL 调整 - 色相
            public float hueRed;
            public float hueOrange;
            public float hueYellow;
            public float hueGreen;
            public float hueAqua;
            public float hueBlue;
            public float huePurple;
            public float hueMagenta;
            
            // HSL 调整 - 饱和度
            public float satRed;
            public float satOrange;
            public float satYellow;
            public float satGreen;
            public float satAqua;
            public float satBlue;
            public float satPurple;
            public float satMagenta;
            
            // HSL 调整 - 明亮度
            public float lumRed;
            public float lumOrange;
            public float lumYellow;
            public float lumGreen;
            public float lumAqua;
            public float lumBlue;
            public float lumPurple;
            public float lumMagenta;
            
            // 细节调整
            public float sharpness;
            public float noiseReduction;
            
            // 镜头校正
            public float lensDistortion;
            public float chromaticAberration;
            
            // 效果
            public float vignette;
            public float grain;
            
            // 校准
            public float shadowTint;
            public float redHue;
            public float redSaturation;
            public float greenHue;
            public float greenSaturation;
            public float blueHue;
            public float blueSaturation;
        }

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void SetImageAdjustParams(IntPtr renderTargetHandle, ref ImageAdjustParams adjustParams);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ResetImageAdjustParams(IntPtr renderTargetHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool GetHistogramData(IntPtr renderTargetHandle, [Out] uint[] outHistogram);

        // 滤镜相关 API
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern bool LoadFilterLUTFromFile(IntPtr renderTargetHandle, string filePath);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void SetFilterIntensity(IntPtr renderTargetHandle, float intensity);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void RemoveFilter(IntPtr renderTargetHandle);

        // 视频相关 API
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern bool OpenVideo(IntPtr renderTargetHandle, string videoPath);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CloseVideo(IntPtr renderTargetHandle);

        // 视频格式枚举
        public enum VideoFormat
        {
            Unknown = 0,
            MP4 = 1,
            MOV = 2,
            AVI = 3,
            MKV = 4
        }

        // 视频元数据结构
        [StructLayout(LayoutKind.Sequential)]
        public struct VideoMetadata
        {
            public uint width;
            public uint height;
            public double frameRate;        // fps
            public long totalFrames;        // 总帧数
            public long duration;           // 时长（微秒）
            public VideoFormat format;
            [MarshalAs(UnmanagedType.Bool)]
            public bool hasAudio;
        }

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool GetVideoMetadata(IntPtr renderTargetHandle, out VideoMetadata outMetadata);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool SeekVideo(IntPtr renderTargetHandle, long timestamp);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool SeekVideoToFrame(IntPtr renderTargetHandle, long frameIndex);

        // 渲染视频帧（双缓冲+拷贝策略，内部处理，WPF端无需关心缓冲区切换）
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool RenderVideoFrame(IntPtr renderTargetHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern long GetCurrentVideoFrame(IntPtr renderTargetHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern long GetCurrentVideoTimestamp(IntPtr renderTargetHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern bool IsVideoFormat(string filePath);

        // 提取视频第一帧作为缩略图
        // videoPath: 视频文件路径
        // outWidth: 输出宽度
        // outHeight: 输出高度
        // outData: 输出像素数据（BGRA32格式，由调用者分配足够的内存，建议 maxWidth*maxHeight*4）
        // maxWidth: 最大宽度（用于缩略图，0表示使用原始尺寸）
        // maxHeight: 最大高度（用于缩略图，0表示使用原始尺寸）
        // 返回是否成功
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern bool ExtractVideoThumbnail([MarshalAs(UnmanagedType.LPStr)] string videoPath, out uint outWidth, out uint outHeight, IntPtr outData, uint maxWidth, uint maxHeight);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern bool ExportImage(IntPtr renderTargetHandle, [MarshalAs(UnmanagedType.LPStr)] string filePath, [MarshalAs(UnmanagedType.LPStr)] string format, uint quality);

        // 视频导出相关 API
        // 进度回调委托
        public delegate void VideoExportProgressDelegate(double progress, long currentFrame, long totalFrames, IntPtr userData);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern bool ExportVideo(IntPtr renderTargetHandle, [MarshalAs(UnmanagedType.LPStr)] string filePath, VideoExportProgressDelegate progressCallback, IntPtr userData);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool IsExportingVideo(IntPtr renderTargetHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void CancelVideoExport(IntPtr renderTargetHandle);
    }
}

