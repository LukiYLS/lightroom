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

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void RenderToTarget(IntPtr renderTargetHandle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ResizeRenderTarget(IntPtr renderTargetHandle, uint width, uint height);
    }
}

