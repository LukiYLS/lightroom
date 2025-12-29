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
        private NativeMethods.VideoMetadata _videoMetadata;
        private bool _isSeeking = false; // 是否正在拖拽进度条
        private DispatcherTimer? _videoUpdateTimer = null; // 用于更新视频进度和时间

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
                        
                        // 获取视频元数据
                        if (NativeMethods.GetVideoMetadata(_renderTargetHandle, out _videoMetadata))
                        {
                            System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Video metadata: {_videoMetadata.width}x{_videoMetadata.height}, {_videoMetadata.frameRate} fps, {_videoMetadata.totalFrames} frames, {_videoMetadata.duration} us");
                            UpdateVideoControls();
                        }
                        
                        // 立即渲染第一帧（使用 RenderVideoFrame 来解码并渲染第一帧）
                        bool frameRendered = NativeMethods.RenderVideoFrame(_renderTargetHandle);
                        if (!frameRendered)
                        {
                            System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Failed to render first video frame, trying RenderToTarget");
                            // 如果 RenderVideoFrame 失败，尝试使用 RenderToTarget
                            NativeMethods.RenderToTarget(_renderTargetHandle);
                        }
                        
                        // 更新DirtyRect以刷新显示（不需要切换SetBackBuffer）
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
                        
                        // 显示视频控制栏
                        if (VideoControlsBorder != null)
                        {
                            VideoControlsBorder.Visibility = Visibility.Visible;
                        }
                        
                        // 根据视频帧率调整渲染定时器
                        if (_videoMetadata.frameRate > 0)
                        {
                            // 根据视频帧率计算渲染间隔（毫秒）
                            double frameInterval = 1000.0 / _videoMetadata.frameRate;
                            _renderTimer.Interval = TimeSpan.FromMilliseconds(frameInterval);
                            System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Adjusted render timer interval to {frameInterval:F2} ms for {_videoMetadata.frameRate} fps video");
                        }
                        
                        // 启动视频更新定时器
                        StartVideoUpdateTimer();
                        
                        // 延迟更新直方图，等待渲染完成
                        Dispatcher.BeginInvoke(new Action(() => {
                            UpdateHistogram();
                        }), DispatcherPriority.Loaded);
                    }
                    else
                    {
                        System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Failed to load video: {absolutePath}");
                        
                        // 确保视频控制栏隐藏
                        if (VideoControlsBorder != null)
                        {
                            VideoControlsBorder.Visibility = Visibility.Collapsed;
                        }
                        
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
                        StopVideoUpdateTimer();
                        NativeMethods.CloseVideo(_renderTargetHandle);
                        _isVideo = false;
                        _isPlaying = false;
                        
                        // 恢复默认渲染定时器间隔（60 FPS）
                        _renderTimer.Interval = TimeSpan.FromMilliseconds(16);
                        
                        // 隐藏视频控制栏
                        if (VideoControlsBorder != null)
                        {
                            VideoControlsBorder.Visibility = Visibility.Collapsed;
                        }
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
                bool renderSuccess = false;
                
                // 如果是视频且正在播放，渲染下一帧
                if (_isVideo && _isPlaying && !_isSeeking)
                {
                    renderSuccess = NativeMethods.RenderVideoFrame(_renderTargetHandle);
                }
                else
                {
                    // 渲染到目标（如果已加载图片则显示图片，否则显示黑色）
                    renderSuccess = NativeMethods.RenderToTarget(_renderTargetHandle);
                }

                // 如果渲染失败，跳过这一帧
                if (!renderSuccess)
                    return;

                // 更新 D3DImage（不需要切换SetBackBuffer，只需要标记脏区域）
                // Front Buffer的指针不会改变，C++端已经将内容复制到Front Buffer
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
            // 停止视频更新定时器
            StopVideoUpdateTimer();
            
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
        
        public string? GetCurrentFilePath()
        {
            return _currentImagePath;
        }
        
        public bool IsVideo()
        {
            return _isVideo;
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
                double savedZoomLevel = _zoomLevel;
                bool savedIsVideo = _isVideo;
                bool savedIsPlaying = _isPlaying;
                
                // 对于视频，保存当前播放位置
                long savedVideoFrame = -1;
                if (savedIsVideo)
                {
                    savedVideoFrame = NativeMethods.GetCurrentVideoFrame(_renderTargetHandle);
                }

                // 调用 SDK 更新渲染目标尺寸（双缓冲+拷贝策略，不会导致画面变黑）
                NativeMethods.ResizeRenderTarget(_renderTargetHandle, newWidth, newHeight);
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Resized render target to {newWidth}x{newHeight}");

                // 先渲染一帧到新Buffer，确保有内容（避免显示空白）
                // 对于视频，恢复播放位置并渲染
                if (savedIsVideo && savedVideoFrame >= 0)
                {
                    NativeMethods.SeekVideoToFrame(_renderTargetHandle, savedVideoFrame);
                    if (savedIsPlaying)
                    {
                        NativeMethods.RenderVideoFrame(_renderTargetHandle);
                    }
                    else
                    {
                        NativeMethods.RenderToTarget(_renderTargetHandle);
                    }
                }
                else if (!savedIsVideo && !string.IsNullOrEmpty(_currentImagePath))
                {
                    // 对于图片，立即渲染
                    NativeMethods.RenderToTarget(_renderTargetHandle);
                }

                // 现在更新D3DImage的SetBackBuffer（此时新Buffer已经有内容了）
                IntPtr newSharedHandle = NativeMethods.GetRenderTargetSharedHandle(_renderTargetHandle);
                if (newSharedHandle != IntPtr.Zero && _d3dImage != null)
                {
                    _sharedHandle = newSharedHandle;
                    _d3dImage.Lock();
                    try
                    {
                        // 只在resize时更新SetBackBuffer（此时新Buffer已经有内容，不会显示空白）
                        _d3dImage.SetBackBuffer(System.Windows.Interop.D3DResourceType.IDirect3DSurface9, _sharedHandle, true);
                        
                        // 立即标记脏区域以刷新显示
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

                // 延迟处理，恢复状态
                Dispatcher.BeginInvoke(new Action(() => {
                    try
                    {
                        // 恢复播放状态
                        if (savedIsVideo)
                        {
                            _isVideo = savedIsVideo;
                            _isPlaying = savedIsPlaying;
                            
                            // 显示视频控制栏
                            if (VideoControlsBorder != null)
                            {
                                VideoControlsBorder.Visibility = Visibility.Visible;
                            }
                            
                            // 重新启动视频更新定时器
                            StartVideoUpdateTimer();
                        }
                        else
                        {
                            // 隐藏视频控制栏
                            if (VideoControlsBorder != null)
                            {
                                VideoControlsBorder.Visibility = Visibility.Collapsed;
                            }
                        }
                        
                        // 恢复缩放级别
                        if (savedZoomLevel != _zoomLevel)
                        {
                            _zoomLevel = savedZoomLevel;
                            UpdateZoom();
                        }
                    }
                    catch (Exception ex)
                    {
                        System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Error in resize callback: {ex.Message}");
                    }
                    finally
                    {
                        _isResizing = false;
                    }
                }), DispatcherPriority.Loaded);
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
            // 对于图片和视频都应该显示直方图
            if (_currentImagePath == null && !_isVideo)
            {
                // 没有图片且不是视频时，显示空直方图
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
        
        // 视频控制相关方法
        private void StartVideoUpdateTimer()
        {
            if (_videoUpdateTimer == null)
            {
                _videoUpdateTimer = new DispatcherTimer();
                _videoUpdateTimer.Interval = TimeSpan.FromMilliseconds(100); // 每 100ms 更新一次
                _videoUpdateTimer.Tick += VideoUpdateTimer_Tick;
            }
            _videoUpdateTimer.Start();
        }
        
        private void StopVideoUpdateTimer()
        {
            if (_videoUpdateTimer != null)
            {
                _videoUpdateTimer.Stop();
                _videoUpdateTimer = null;
            }
        }
        
        private void VideoUpdateTimer_Tick(object? sender, EventArgs e)
        {
            if (!_isVideo || _renderTargetHandle == IntPtr.Zero || _isSeeking)
                return;
            
            UpdateVideoProgress();
        }
        
        private void UpdateVideoProgress()
        {
            if (!_isVideo || _renderTargetHandle == IntPtr.Zero)
                return;
            
            try
            {
                long currentTimestamp = NativeMethods.GetCurrentVideoTimestamp(_renderTargetHandle);
                long currentFrame = NativeMethods.GetCurrentVideoFrame(_renderTargetHandle);
                
                if (_videoMetadata.duration > 0 && VideoProgressSlider != null)
                {
                    // 更新进度条（0-100）
                    double progress = (double)currentTimestamp / _videoMetadata.duration * 100.0;
                    progress = Math.Max(0, Math.Min(100, progress));
                    
                    // 只有在不是用户拖拽时才更新进度条
                    if (!_isSeeking)
                    {
                        VideoProgressSlider.Value = progress;
                    }
                    
                    // 更新时间显示
                    UpdateTimeDisplay(currentTimestamp, _videoMetadata.duration);
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Error updating video progress: {ex.Message}");
            }
        }
        
        private void UpdateTimeDisplay(long currentTimestamp, long totalDuration)
        {
            if (CurrentTimeText == null || TotalTimeText == null)
                return;
            
            // 转换微秒到秒
            double currentSeconds = currentTimestamp / 1_000_000.0;
            double totalSeconds = totalDuration / 1_000_000.0;
            
            CurrentTimeText.Text = FormatTime(currentSeconds);
            TotalTimeText.Text = "/ " + FormatTime(totalSeconds);
        }
        
        private string FormatTime(double seconds)
        {
            int totalSeconds = (int)Math.Floor(seconds);
            int minutes = totalSeconds / 60;
            int secs = totalSeconds % 60;
            
            return $"{minutes:D2}:{secs:D2}";
        }
        
        private void UpdateVideoControls()
        {
            if (!_isVideo)
                return;
            
            // 设置进度条最大值
            if (VideoProgressSlider != null)
            {
                VideoProgressSlider.Maximum = 100;
                VideoProgressSlider.Value = 0;
            }
            
            // 更新播放/暂停按钮图标
            UpdatePlayPauseButton();
        }
        
        private void UpdatePlayPauseButton()
        {
            if (PlayPauseIcon == null)
                return;
            
            PlayPauseIcon.Text = _isPlaying ? "⏸" : "▶";
        }
        
        private void PlayPauseButton_Click(object sender, RoutedEventArgs e)
        {
            if (!_isVideo || _renderTargetHandle == IntPtr.Zero)
                return;
            
            _isPlaying = !_isPlaying;
            UpdatePlayPauseButton();
            
            System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Video playback: {(_isPlaying ? "Playing" : "Paused")}");
        }
        
        private void VideoProgressSlider_PreviewMouseDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            _isSeeking = true;
        }
        
        private void VideoProgressSlider_PreviewMouseUp(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (!_isVideo || _renderTargetHandle == IntPtr.Zero || VideoProgressSlider == null)
            {
                _isSeeking = false;
                return;
            }
            
            try
            {
                // 计算目标时间戳
                double progress = VideoProgressSlider.Value / 100.0;
                long targetTimestamp = (long)(_videoMetadata.duration * progress);
                
                // 跳转到指定时间
                bool success = NativeMethods.SeekVideo(_renderTargetHandle, targetTimestamp);
                if (success)
                {
                    System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Seeked to {targetTimestamp} us ({progress * 100:F1}%)");
                    
                    // 立即渲染当前帧
                    NativeMethods.RenderVideoFrame(_renderTargetHandle);
                    
                    // 更新DirtyRect以刷新显示（不需要切换SetBackBuffer）
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
                    
                    // 更新显示
                    UpdateVideoProgress();
                }
                else
                {
                    System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Failed to seek to {targetTimestamp} us");
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[ImageEditorView] Error seeking video: {ex.Message}");
            }
            finally
            {
                _isSeeking = false;
            }
        }
        
        private void VideoProgressSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            // 当用户拖拽进度条时，实时更新时间显示（但不实际跳转）
            if (_isSeeking && _isVideo && _videoMetadata.duration > 0)
            {
                double progress = e.NewValue / 100.0;
                long targetTimestamp = (long)(_videoMetadata.duration * progress);
                UpdateTimeDisplay(targetTimestamp, _videoMetadata.duration);
            }
        }
    }
}


