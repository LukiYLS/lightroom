using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Threading;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Media.Imaging;
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
        private bool _isVideo = false;
        private bool _isPlaying = false;
        private uint[] _histogramData = new uint[256 * 4]; // R, G, B, Luminance (每个 256 bins)
        private bool _isInitializing = false;
        private bool _isResizing = false;

        public event EventHandler<double>? ZoomChanged;
        
        /// <summary>
        /// 加载图片或视频到编辑视图
        /// </summary>
        public bool LoadImage(string imagePath)
        {
            if (_renderTargetHandle == IntPtr.Zero)
            {
                System.Diagnostics.Debug.WriteLine("[ImageEditorView] Cannot load file: render target not initialized");
                return false;
            }
            
            if (string.IsNullOrEmpty(imagePath))
            {
                System.Diagnostics.Debug.WriteLine("[ImageEditorView] File path is null or empty");
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
            
            // 检查是否为视频文件
            bool isVideo = NativeMethods.IsVideoFormat(absolutePath);
            
            try
            {
                bool success = false;
                if (isVideo)
                {
                    // 加载视频
                    System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Loading video: {absolutePath}");
                    
                    // 如果之前是图片，先清理
                    if (!_isVideo && _currentImagePath != null)
                    {
                        // 图片不需要特殊清理，RenderToTarget 会自动处理
                    }
                    
                    success = NativeMethods.OpenVideo(_renderTargetHandle, absolutePath);
                    if (success)
                    {
                        _currentImagePath = absolutePath;
                        _isVideo = true;
                        _isPlaying = true; // 自动开始播放
                        System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Successfully loaded video: {absolutePath}");
                        
                        // 立即渲染第一帧（使用 RenderVideoFrame 来解码并渲染第一帧）
                        bool frameRendered = NativeMethods.RenderVideoFrame(_renderTargetHandle);
                        if (!frameRendered)
                        {
                            System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Failed to render first video frame, trying RenderToTarget");
                            // 如果 RenderVideoFrame 失败，尝试使用 RenderToTarget
                            NativeMethods.RenderToTarget(_renderTargetHandle);
                        }
                        
                        // 延迟更新直方图，等待渲染完成
                        Dispatcher.BeginInvoke(new Action(() => {
                            UpdateHistogram();
                        }), DispatcherPriority.Loaded);
                    }
                    else
                    {
                        System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Failed to load video: {absolutePath}");
                        System.Windows.MessageBox.Show(
                            $"无法加载视频文件：{absolutePath}\n\n可能的原因：\n1. FFmpeg DLL 文件缺失\n2. 视频文件格式不支持\n3. 视频文件已损坏",
                            "视频加载失败",
                            MessageBoxButton.OK,
                            MessageBoxImage.Error);
                    }
                }
                else
                {
                    // 加载图片
                    System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Loading image: {absolutePath}");
                    // 如果之前是视频，先关闭
                    if (_isVideo)
                    {
                        NativeMethods.CloseVideo(_renderTargetHandle);
                        _isVideo = false;
                        _isPlaying = false;
                    }
                    
                    success = NativeMethods.LoadImageToTarget(_renderTargetHandle, absolutePath);
                    if (success)
                    {
                        _currentImagePath = absolutePath;
                        System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Successfully loaded image: {absolutePath}");
                        
                        // 延迟更新直方图，等待渲染完成
                        Dispatcher.BeginInvoke(new Action(() => {
                            UpdateHistogram();
                        }), DispatcherPriority.Loaded);
                    }
                    else
                    {
                        System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Failed to load image: {absolutePath}");
                    }
                }
                return success;
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Exception loading file: {ex.Message}");
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Stack trace: {ex.StackTrace}");
                return false;
            }
        }

        public ImageEditorView()
        {
            InitializeComponent();
            Loaded += ImageEditorView_Loaded;
            Unloaded += ImageEditorView_Unloaded;
            SizeChanged += ImageEditorView_SizeChanged;
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
                _isInitializing = true;
                
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
                
                _isInitializing = false;
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Error initializing D3D rendering: {ex.Message}");
                _isInitializing = false;
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
                
                // 如果是视频且正在播放，渲染下一帧
                if (_isVideo && _isPlaying)
                {
                    NativeMethods.RenderVideoFrame(_renderTargetHandle);
                }
                else
                {
                    // 渲染到目标（如果已加载图片则显示图片，否则显示黑色）
                    NativeMethods.RenderToTarget(_renderTargetHandle);
                }

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

                // 每 10 帧更新一次直方图（降低性能开销）
                if (_renderCount % 10 == 0)
                {
                    UpdateHistogram();
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
            // 停止播放并关闭视频
            if (_isVideo && _renderTargetHandle != IntPtr.Zero)
            {
                _isPlaying = false;
                NativeMethods.CloseVideo(_renderTargetHandle);
                _isVideo = false;
            }

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
            
            // 调用 SDK 设置缩放参数
            if (_renderTargetHandle != IntPtr.Zero)
            {
                // 计算平移偏移（当前为 0，后续可以添加平移功能）
                double panX = 0.0;
                double panY = 0.0;
                
                NativeMethods.SetRenderTargetZoom(_renderTargetHandle, _zoomLevel, panX, panY);
            }
        }

        public IntPtr GetRenderTargetHandle()
        {
            return _renderTargetHandle;
        }

        private void ImageEditorView_SizeChanged(object sender, SizeChangedEventArgs e)
        {
            if (_renderTargetHandle == IntPtr.Zero)
                return;

            // 如果正在初始化或正在 resize，忽略
            if (_isInitializing || _isResizing)
                return;

            // 获取新的尺寸
            uint newWidth = (uint)Math.Max(1, (int)ActualWidth);
            uint newHeight = (uint)Math.Max(1, (int)ActualHeight);

            // 如果尺寸没有变化，不需要更新
            if (newWidth == 0 || newHeight == 0)
                return;

            try
            {
                _isResizing = true;

                // 保存当前状态
                string? savedImagePath = _currentImagePath;
                double savedZoomLevel = _zoomLevel;
                bool savedIsVideo = _isVideo;
                bool savedIsPlaying = _isPlaying;

                // 调用 SDK 更新渲染目标尺寸
                NativeMethods.ResizeRenderTarget(_renderTargetHandle, newWidth, newHeight);
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Resized render target to {newWidth}x{newHeight}");

                // 获取新的共享句柄并更新 D3DImage
                IntPtr newSharedHandle = NativeMethods.GetRenderTargetSharedHandle(_renderTargetHandle);
                if (newSharedHandle != IntPtr.Zero && _d3dImage != null)
                {
                    _sharedHandle = newSharedHandle;
                    _d3dImage.Lock();
                    try
                    {
                        _d3dImage.SetBackBuffer(System.Windows.Interop.D3DResourceType.IDirect3DSurface9, _sharedHandle, true);
                    }
                    finally
                    {
                        _d3dImage.Unlock();
                    }
                }

                // 如果之前有加载文件，需要重新加载
                if (!string.IsNullOrEmpty(savedImagePath))
                {
                    // 延迟重新加载文件，等待 resize 完成
                    Dispatcher.BeginInvoke(new Action(() => {
                        LoadImage(savedImagePath);
                        
                        // 恢复视频播放状态
                        if (savedIsVideo)
                        {
                            _isVideo = savedIsVideo;
                            _isPlaying = savedIsPlaying;
                        }
                        
                        // 恢复缩放级别
                        if (savedZoomLevel != _zoomLevel)
                        {
                            _zoomLevel = savedZoomLevel;
                            UpdateZoom();
                        }
                        
                        _isResizing = false;
                    }), DispatcherPriority.Loaded);
                }
                else
                {
                    _isResizing = false;
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Error resizing render target: {ex.Message}");
                _isResizing = false;
            }
        }

        private void UpdateHistogram()
        {
            if (HistogramCanvas == null || _renderTargetHandle == IntPtr.Zero)
                return;

            // 从 SDK 获取真实的直方图数据
            if (_currentImagePath == null)
            {
                // 没有图片时，显示空直方图
                HistogramCanvas.Children.Clear();
                return;
            }

            // 从 SDK 获取直方图数据
            bool success = NativeMethods.GetHistogramData(_renderTargetHandle, _histogramData);
            if (!success)
            {
                // 如果获取失败，清空直方图
                HistogramCanvas.Children.Clear();
                return;
            }

            // 绘制直方图
            DrawHistogram();
        }

        private void DrawHistogram()
        {
            if (HistogramCanvas == null)
                return;

            HistogramCanvas.Children.Clear();

            // 等待布局完成
            if (HistogramCanvas.ActualWidth <= 0 || HistogramCanvas.ActualHeight <= 0)
            {
                // 如果布局未完成，延迟重试
                Dispatcher.BeginInvoke(new Action(() => {
                    DrawHistogram();
                }), DispatcherPriority.Loaded);
                return;
            }

            double canvasWidth = HistogramCanvas.ActualWidth;
            double canvasHeight = HistogramCanvas.ActualHeight;

            // 找到所有通道的最大值用于归一化
            uint maxValue = 0;
            for (int i = 0; i < 256 * 4; i++)
            {
                if (_histogramData[i] > maxValue)
                    maxValue = _histogramData[i];
            }

            if (maxValue == 0)
            {
                // 如果没有数据，显示空直方图
                return;
            }

            // Photoshop 风格的直方图：绘制 RGB 三个通道和亮度通道
            // 使用柱状图，每个 bin 一个矩形
            double barWidth = canvasWidth / 256.0;
            
            // 绘制亮度通道（白色/灰色，作为背景）
            for (int i = 0; i < 256; i++)
            {
                uint lumValue = _histogramData[768 + i]; // Luminance channel
                if (lumValue > 0)
                {
                    double barHeight = (lumValue / (double)maxValue) * canvasHeight;
                    System.Windows.Shapes.Rectangle rect = new System.Windows.Shapes.Rectangle
                    {
                        Width = Math.Max(0.5, barWidth),
                        Height = Math.Max(1, barHeight),
                        Fill = new SolidColorBrush(System.Windows.Media.Color.FromArgb(180, 200, 200, 200)),
                        VerticalAlignment = VerticalAlignment.Bottom
                    };
                    Canvas.SetLeft(rect, i * barWidth);
                    Canvas.SetBottom(rect, 0);
                    HistogramCanvas.Children.Add(rect);
                }
            }

            // 绘制 RGB 通道（叠加在亮度通道上）
            // R channel (红色)
            for (int i = 0; i < 256; i++)
            {
                uint rValue = _histogramData[i];
                if (rValue > 0)
                {
                    double barHeight = (rValue / (double)maxValue) * canvasHeight;
                    System.Windows.Shapes.Rectangle rect = new System.Windows.Shapes.Rectangle
                    {
                        Width = Math.Max(0.5, barWidth),
                        Height = Math.Max(1, barHeight),
                        Fill = new SolidColorBrush(System.Windows.Media.Color.FromArgb(200, 255, 0, 0)),
                        VerticalAlignment = VerticalAlignment.Bottom
                    };
                    Canvas.SetLeft(rect, i * barWidth);
                    Canvas.SetBottom(rect, 0);
                    HistogramCanvas.Children.Add(rect);
                }
            }

            // G channel (绿色)
            for (int i = 0; i < 256; i++)
            {
                uint gValue = _histogramData[256 + i];
                if (gValue > 0)
                {
                    double barHeight = (gValue / (double)maxValue) * canvasHeight;
                    System.Windows.Shapes.Rectangle rect = new System.Windows.Shapes.Rectangle
                    {
                        Width = Math.Max(0.5, barWidth),
                        Height = Math.Max(1, barHeight),
                        Fill = new SolidColorBrush(System.Windows.Media.Color.FromArgb(200, 0, 255, 0)),
                        VerticalAlignment = VerticalAlignment.Bottom
                    };
                    Canvas.SetLeft(rect, i * barWidth);
                    Canvas.SetBottom(rect, 0);
                    HistogramCanvas.Children.Add(rect);
                }
            }

            // B channel (蓝色)
            for (int i = 0; i < 256; i++)
            {
                uint bValue = _histogramData[512 + i];
                if (bValue > 0)
                {
                    double barHeight = (bValue / (double)maxValue) * canvasHeight;
                    System.Windows.Shapes.Rectangle rect = new System.Windows.Shapes.Rectangle
                    {
                        Width = Math.Max(0.5, barWidth),
                        Height = Math.Max(1, barHeight),
                        Fill = new SolidColorBrush(System.Windows.Media.Color.FromArgb(200, 0, 0, 255)),
                        VerticalAlignment = VerticalAlignment.Bottom
                    };
                    Canvas.SetLeft(rect, i * barWidth);
                    Canvas.SetBottom(rect, 0);
                    HistogramCanvas.Children.Add(rect);
                }
            }
        }
    }
}


