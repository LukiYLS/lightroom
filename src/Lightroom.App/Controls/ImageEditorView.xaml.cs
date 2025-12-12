using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Threading;
using Lightroom.App.Core;

namespace Lightroom.App.Controls
{
    public partial class ImageEditorView : System.Windows.Controls.UserControl
    {
        private IntPtr _renderTargetHandle = IntPtr.Zero;
        private IntPtr _sharedHandle = IntPtr.Zero;
        private DispatcherTimer? _renderTimer = null;
        private System.Windows.Interop.D3DImage? _d3dImage = null;
        private double _zoomLevel = 1.0;
        private string? _currentImagePath = null;

        public event EventHandler<double>? ZoomChanged;
        
        /// <summary>
        /// 加载图片到编辑视图
        /// </summary>
        public bool LoadImage(string imagePath)
        {
            if (_renderTargetHandle == IntPtr.Zero)
            {
                System.Diagnostics.Debug.WriteLine("[ImageEditorView] Cannot load image: render target not initialized");
                return false;
            }
            
            if (string.IsNullOrEmpty(imagePath))
            {
                System.Diagnostics.Debug.WriteLine("[ImageEditorView] Image path is null or empty");
                return false;
            }
            
            // 验证文件是否存在
            if (!System.IO.File.Exists(imagePath))
            {
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] File does not exist: {imagePath}");
                return false;
            }
            
            // 转换为绝对路径
            string absolutePath = System.IO.Path.GetFullPath(imagePath);
            System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Loading image: {absolutePath}");
            
            try
            {
                bool success = NativeMethods.LoadImageToTarget(_renderTargetHandle, absolutePath);
                if (success)
                {
                    _currentImagePath = absolutePath;
                    System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Successfully loaded image: {absolutePath}");
                }
                else
                {
                    System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Failed to load image: {absolutePath}");
                }
                return success;
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Exception loading image: {ex.Message}");
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Stack trace: {ex.StackTrace}");
                return false;
            }
        }

        public ImageEditorView()
        {
            InitializeComponent();
            Loaded += ImageEditorView_Loaded;
            Unloaded += ImageEditorView_Unloaded;
        }

        private void ImageEditorView_Loaded(object sender, RoutedEventArgs e)
        {
            InitializeD3DRendering();
        }

        private void ImageEditorView_Unloaded(object sender, RoutedEventArgs e)
        {
            CleanupD3DRendering();
        }

        private void InitializeD3DRendering()
        {
            try
            {
                // 获取渲染区域大小
                var width = (uint)ActualWidth;
                var height = (uint)ActualHeight;
                if (width == 0) width = 800;
                if (height == 0) height = 600;

                // 创建渲染目标纹理
                _renderTargetHandle = NativeMethods.CreateRenderTarget(width, height);
                if (_renderTargetHandle == IntPtr.Zero)
                {
                    return;
                }

                // 获取共享句柄
                _sharedHandle = NativeMethods.GetRenderTargetSharedHandle(_renderTargetHandle);
                if (_sharedHandle == IntPtr.Zero)
                {
                    return;
                }

                // 创建 D3DImage
                _d3dImage = new System.Windows.Interop.D3DImage();
                _d3dImage.Lock();
                try
                {
                    // 注意：SetBackBuffer 需要 IDirect3DSurface9* 的指针，不是共享句柄
                    // enableSoftwareFallback = true 允许在 Remote Desktop 等情况下回退到软件渲染
                    _d3dImage.SetBackBuffer(System.Windows.Interop.D3DResourceType.IDirect3DSurface9, _sharedHandle, true);
                    System.Diagnostics.Debug.WriteLine($"[ImageEditorView] D3DImage SetBackBuffer: handle={_sharedHandle} (0x{_sharedHandle:X}), PixelWidth={_d3dImage.PixelWidth}, PixelHeight={_d3dImage.PixelHeight}, IsFrontBufferAvailable={_d3dImage.IsFrontBufferAvailable}");
                    
                    if (!_d3dImage.IsFrontBufferAvailable)
                    {
                        System.Diagnostics.Debug.WriteLine("[ImageEditorView] WARNING: IsFrontBufferAvailable is false immediately after SetBackBuffer!");
                        System.Diagnostics.Debug.WriteLine("[ImageEditorView] This usually means the D3D9 surface is not accessible. Check C++ logs for surface creation errors.");
                    }
                }
                finally
                {
                    _d3dImage.Unlock();
                }

                D3DImageControl.Source = _d3dImage;
                PlaceholderText.Visibility = Visibility.Collapsed;
                
                // 等待布局更新
                Dispatcher.BeginInvoke(new Action(() => {
                    System.Diagnostics.Debug.WriteLine($"[ImageEditorView] After layout: Image control size: {D3DImageControl.ActualWidth}x{D3DImageControl.ActualHeight}, D3DImage size: {_d3dImage.PixelWidth}x{_d3dImage.PixelHeight}, IsFrontBufferAvailable={_d3dImage.IsFrontBufferAvailable}");
                }), System.Windows.Threading.DispatcherPriority.Loaded);

                // 启动渲染定时器
                _renderTimer = new DispatcherTimer();
                _renderTimer.Interval = TimeSpan.FromMilliseconds(16); // ~60 FPS
                _renderTimer.Tick += RenderTimer_Tick;
                _renderTimer.Start();
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Error initializing D3D rendering: {ex.Message}");
            }
        }

        private int _renderCount = 0;
        
        private void RenderTimer_Tick(object? sender, EventArgs e)
        {
            if (_renderTargetHandle == IntPtr.Zero)
                return;

            try
            {
                _renderCount++;
                
                // 渲染到目标（如果已加载图片则显示图片，否则显示黑色）
                NativeMethods.RenderToTarget(_renderTargetHandle);

                // 更新 D3DImage
                if (_d3dImage != null)
                {
                    _d3dImage.Lock();
                    try
                    {
                        if (_d3dImage.PixelWidth > 0 && _d3dImage.PixelHeight > 0)
                        {
                            _d3dImage.AddDirtyRect(new Int32Rect(0, 0, _d3dImage.PixelWidth, _d3dImage.PixelHeight));
                        }
                    }
                    finally
                    {
                        _d3dImage.Unlock();
                    }
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Render error: {ex.Message}");
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Stack trace: {ex.StackTrace}");
            }
        }

        private void CleanupD3DRendering()
        {
            if (_renderTimer != null)
            {
                _renderTimer.Stop();
                _renderTimer = null;
            }

            if (_d3dImage != null)
            {
                _d3dImage.Lock();
                _d3dImage.SetBackBuffer(System.Windows.Interop.D3DResourceType.IDirect3DSurface9, IntPtr.Zero);
                _d3dImage.Unlock();
                _d3dImage = null;
            }

            if (_renderTargetHandle != IntPtr.Zero)
            {
                NativeMethods.DestroyRenderTarget(_renderTargetHandle);
                _renderTargetHandle = IntPtr.Zero;
            }
        }

        private void ZoomIn_Click(object sender, RoutedEventArgs e)
        {
            _zoomLevel = Math.Min(_zoomLevel * 1.2, 10.0);
            UpdateZoom();
        }

        private void ZoomOut_Click(object sender, RoutedEventArgs e)
        {
            _zoomLevel = Math.Max(_zoomLevel / 1.2, 0.1);
            UpdateZoom();
        }

        private void ZoomFit_Click(object sender, RoutedEventArgs e)
        {
            _zoomLevel = 1.0;
            UpdateZoom();
        }

        private void UpdateZoom()
        {
            ZoomText.Text = $"{_zoomLevel * 100:F0}%";
            ZoomChanged?.Invoke(this, _zoomLevel);
        }

        public IntPtr GetRenderTargetHandle()
        {
            return _renderTargetHandle;
        }
    }
}


