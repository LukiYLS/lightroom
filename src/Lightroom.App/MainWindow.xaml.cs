using System;
using System.Collections.Generic;
using System.IO;
using System.Windows;
using Lightroom.App.Core;
using Lightroom.App.Controls;

namespace Lightroom.App
{
    public partial class MainWindow : Window
    {
        private Dictionary<string, double> _adjustments = new Dictionary<string, double>();

        public MainWindow()
        {
            InitializeComponent();
            InitializeSDK();
        }

        private void InitializeSDK()
        {
            try
            {
                bool success = NativeMethods.InitSDK();
                if (!success)
                {
                    System.Windows.MessageBox.Show("Failed to initialize Core SDK!", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
            catch (DllNotFoundException)
            {
                System.Windows.MessageBox.Show("LightroomCore.dll not found. Please ensure the C++ project is built.", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show($"Error initializing: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            // 不初始化示例数据，等待用户选择目录
            // FilmstripViewControl.LoadThumbnails(new List<string> 
            // { 
            //     "Image 1", "Image 2", "Image 3", "Image 4", "Image 5" 
            // });
            // LeftPanelControl.UpdateImageCount(5);
        }

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            try
            {
                NativeMethods.ShutdownSDK();
            }
            catch { }
        }

        // TopMenuBar Events
        private void TopMenuBar_ImportPhotosRequested(object? sender, EventArgs e)
        {
            var dialog = new Microsoft.Win32.OpenFileDialog
            {
                Filter = "图片文件|*.jpg;*.jpeg;*.png;*.tiff;*.raw;*.cr2;*.cr3;*.nef;*.nrw;*.arw;*.srf;*.dng;*.orf;*.raf;*.rw2;*.pef;*.ptx;*.x3f;*.3fr;*.fff;*.mef;*.mos|RAW 文件|*.raw;*.cr2;*.cr3;*.nef;*.nrw;*.arw;*.srf;*.dng;*.orf;*.raf;*.rw2;*.pef;*.ptx;*.x3f;*.3fr;*.fff;*.mef;*.mos|标准图片|*.jpg;*.jpeg;*.png;*.tiff;*.bmp;*.gif|所有文件|*.*",
                Multiselect = true
            };

            if (dialog.ShowDialog() == true)
            {
                // 处理导入的图片
                FilmstripViewControl.LoadThumbnails(new List<string>(dialog.FileNames));
                LeftPanelControl.UpdateImageCount(dialog.FileNames.Length);
                
                // 加载第一张图片
                if (dialog.FileNames.Length > 0)
                {
                    ImageEditorViewControl.LoadImage(dialog.FileNames[0]);
                }
            }
        }

        private void TopMenuBar_ExportRequested(object? sender, EventArgs e)
        {
            System.Diagnostics.Debug.WriteLine("[MainWindow] Export requested");
            
            // 检查是否有渲染目标
            var renderHandle = ImageEditorViewControl.GetRenderTargetHandle();
            System.Diagnostics.Debug.WriteLine($"[MainWindow] Render target handle: {renderHandle}");
            
            if (renderHandle == IntPtr.Zero)
            {
                System.Windows.MessageBox.Show("没有可导出的内容", "导出", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            
            // 检查是否是视频
            string? currentPath = ImageEditorViewControl.GetCurrentFilePath();
            bool isVideo = ImageEditorViewControl.IsVideo();
            
            if (isVideo && !string.IsNullOrEmpty(currentPath))
            {
                // 导出视频（MP4格式，H.265编码）
                var dialog = new Microsoft.Win32.SaveFileDialog
                {
                    Filter = "MP4文件|*.mp4|所有文件|*.*",
                    DefaultExt = "mp4",
                    FileName = System.IO.Path.GetFileNameWithoutExtension(currentPath ?? "export") + ".mp4"
                };

                if (dialog.ShowDialog() == true)
                {
                    try
                    {
                        string filePath = dialog.FileName;
                        System.Diagnostics.Debug.WriteLine($"[MainWindow] Exporting video to: {filePath}");
                        
                        // 设置RightPanel的渲染目标句柄（用于显示进度）
                        RightPanelControl.SetRenderTargetHandle(renderHandle);
                        
                        // 开始导出视频（后台导出）
                        bool success = RightPanelControl.StartVideoExport(filePath);
                        
                        if (!success)
                        {
                            System.Windows.MessageBox.Show("无法开始导出视频，可能正在导出中", "导出", MessageBoxButton.OK, MessageBoxImage.Warning);
                        }
                        // 成功开始导出后，进度会在RightPanel中显示
                    }
                    catch (Exception ex)
                    {
                        System.Windows.MessageBox.Show($"导出视频时出错: {ex.Message}", "错误", MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                }
            }
            else
            {
                // 导出图片
                var dialog = new Microsoft.Win32.SaveFileDialog
                {
                    Filter = "JPEG文件|*.jpg|PNG文件|*.png|所有文件|*.*",
                    DefaultExt = "jpg"
                };

                if (dialog.ShowDialog() == true)
                {
                    try
                    {
                        // 从文件扩展名确定格式
                        string filePath = dialog.FileName;
                        string extension = System.IO.Path.GetExtension(filePath).ToLower();
                        string format = "jpeg"; // 默认格式
                        uint quality = 90; // 默认质量

                        if (extension == ".png")
                        {
                            format = "png";
                        }
                        else if (extension == ".jpg" || extension == ".jpeg")
                        {
                            format = "jpeg";
                            quality = 90; // JPEG 质量
                        }
                        else
                        {
                            // 如果没有扩展名或扩展名不支持，根据过滤器选择格式
                            // 这里可以根据需要添加更多格式支持
                            format = "jpeg";
                        }

                        System.Diagnostics.Debug.WriteLine($"[MainWindow] Exporting to: {filePath}, format: {format}, quality: {quality}");

                        // 调用 SDK 导出函数
                        bool success = NativeMethods.ExportImage(renderHandle, filePath, format, quality);
                        
                        System.Diagnostics.Debug.WriteLine($"[MainWindow] Export result: {success}");
                        
                        if (success)
                        {
                            System.Windows.MessageBox.Show($"图片已成功导出到:\n{filePath}", "导出成功", MessageBoxButton.OK, MessageBoxImage.Information);
                        }
                        else
                        {
                            System.Windows.MessageBox.Show("导出失败，请检查文件路径和权限", "导出失败", MessageBoxButton.OK, MessageBoxImage.Error);
                        }
                    }
                    catch (Exception ex)
                    {
                        System.Diagnostics.Debug.WriteLine($"[MainWindow] Export exception: {ex.Message}\n{ex.StackTrace}");
                        System.Windows.MessageBox.Show($"导出时发生错误:\n{ex.Message}", "错误", MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                }
            }
        }
        
        // LeftPanel Events
        private void LeftPanel_FolderSelected(object? sender, string folderPath)
        {
            if (string.IsNullOrEmpty(folderPath))
            {
                return;
            }

            System.Diagnostics.Debug.WriteLine($"Folder selected: {folderPath}");
            
            // 加载文件夹中的图片和视频到缩略图列表
            FilmstripViewControl.LoadImagesFromFolder(folderPath);
            
            // 更新文件数量（图片 + 视频）
            if (FilmstripViewControl.ThumbnailPaths != null)
            {
                LeftPanelControl.UpdateImageCount(FilmstripViewControl.ThumbnailPaths.Count);
                
                // 如果有文件，加载第一个（图片或视频）
                if (FilmstripViewControl.ThumbnailPaths.Count > 0)
                {
                    ImageEditorViewControl.LoadImage(FilmstripViewControl.ThumbnailPaths[0]);
                }
            }
            else
            {
                LeftPanelControl.UpdateImageCount(0);
            }
        }


        // RightPanel Events
        private void RightPanel_AdjustmentChanged(object? sender, (string parameter, double value) args)
        {
            _adjustments[args.parameter] = args.value;
            
            // 将调整参数传递给 C++ SDK 进行渲染
            var renderHandle = ImageEditorViewControl.GetRenderTargetHandle();
            if (renderHandle != IntPtr.Zero)
            {
                // 构建完整的调整参数结构
                var adjustParams = new NativeMethods.ImageAdjustParams();
                
                // 从字典中获取所有调整值
                adjustParams.exposure = (float)(_adjustments.GetValueOrDefault("Exposure", 0.0));
                adjustParams.contrast = (float)(_adjustments.GetValueOrDefault("Contrast", 0.0));
                adjustParams.highlights = (float)(_adjustments.GetValueOrDefault("Highlights", 0.0));
                adjustParams.shadows = (float)(_adjustments.GetValueOrDefault("Shadows", 0.0));
                adjustParams.whites = (float)(_adjustments.GetValueOrDefault("Whites", 0.0));
                adjustParams.blacks = (float)(_adjustments.GetValueOrDefault("Blacks", 0.0));
                adjustParams.temperature = (float)(_adjustments.GetValueOrDefault("Temperature", 5500.0));
                adjustParams.tint = (float)(_adjustments.GetValueOrDefault("Tint", 0.0));
                adjustParams.vibrance = (float)(_adjustments.GetValueOrDefault("Vibrance", 0.0));
                adjustParams.saturation = (float)(_adjustments.GetValueOrDefault("Saturation", 0.0));
                adjustParams.sharpness = (float)(_adjustments.GetValueOrDefault("Sharpness", 0.0));
                adjustParams.noiseReduction = (float)(_adjustments.GetValueOrDefault("NoiseReduction", 0.0));
                adjustParams.vignette = (float)(_adjustments.GetValueOrDefault("Vignette", 0.0));
                adjustParams.grain = (float)(_adjustments.GetValueOrDefault("Grain", 0.0));
                
                // 设置调整参数并重新渲染
                NativeMethods.SetImageAdjustParams(renderHandle, ref adjustParams);
                
                // 对于视频，需要重新渲染当前帧以应用调整参数
                // RenderToTarget 会自动处理图片和视频的情况
                // 对于视频，它会渲染当前帧（不推进到下一帧），并且会应用最新的调整参数
                NativeMethods.RenderToTarget(renderHandle);
            }
        }

        private void RightPanel_FilterSelected(object? sender, string filterName)
        {
            var renderHandle = ImageEditorViewControl.GetRenderTargetHandle();
            if (renderHandle == IntPtr.Zero)
            {
                System.Diagnostics.Debug.WriteLine("[Filter] Render target handle is null");
                return;
            }

            try
            {
                if (filterName == "无" || string.IsNullOrEmpty(filterName))
                {
                    // 移除滤镜
                    System.Diagnostics.Debug.WriteLine("[Filter] Removing filter");
                    NativeMethods.RemoveFilter(renderHandle);
                }
                else
                {
                    // 获取滤镜文件路径
                    string? filterPath = RightPanelControl.GetSelectedFilterPath();
                    System.Diagnostics.Debug.WriteLine($"[Filter] Selected filter: {filterName}, Path: {filterPath}");
                    
                    if (string.IsNullOrEmpty(filterPath))
                    {
                        System.Diagnostics.Debug.WriteLine("[Filter] Filter path is null or empty");
                        return;
                    }
                    
                    if (!File.Exists(filterPath))
                    {
                        System.Diagnostics.Debug.WriteLine($"[Filter] Filter file does not exist: {filterPath}");
                        return;
                    }
                    
                    // 加载滤镜
                    System.Diagnostics.Debug.WriteLine($"[Filter] Loading filter from: {filterPath}");
                    bool success = NativeMethods.LoadFilterLUTFromFile(renderHandle, filterPath);
                    System.Diagnostics.Debug.WriteLine($"[Filter] LoadFilterLUTFromFile returned: {success}");
                    
                    if (success)
                    {
                        // 获取当前强度并应用
                        double intensity = RightPanelControl.GetFilterIntensity();
                        System.Diagnostics.Debug.WriteLine($"[Filter] Setting intensity: {intensity}");
                        NativeMethods.SetFilterIntensity(renderHandle, (float)intensity);
                    }
                    else
                    {
                        System.Diagnostics.Debug.WriteLine("[Filter] Failed to load filter LUT");
                    }
                }

                // 重新渲染
                System.Diagnostics.Debug.WriteLine("[Filter] Rendering to target");
                NativeMethods.RenderToTarget(renderHandle);
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[Filter] Error applying filter: {ex.Message}");
                System.Diagnostics.Debug.WriteLine($"[Filter] Stack trace: {ex.StackTrace}");
            }
        }

        private void RightPanel_FilterIntensityChanged(object? sender, double intensity)
        {
            var renderHandle = ImageEditorViewControl.GetRenderTargetHandle();
            if (renderHandle != IntPtr.Zero)
            {
                try
                {
                    NativeMethods.SetFilterIntensity(renderHandle, (float)intensity);
                    NativeMethods.RenderToTarget(renderHandle);
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"Error setting filter intensity: {ex.Message}");
                }
            }
        }

        // ImageEditorView Events
        private void ImageEditorView_ZoomChanged(object? sender, double zoomLevel)
        {
            // TODO: 处理缩放变化
            System.Diagnostics.Debug.WriteLine($"Zoom changed: {zoomLevel}");
        }

        // FilmstripView Events
        private void FilmstripView_ThumbnailSelected(object? sender, int index)
        {
            // 加载选中的图片或视频
            var filmstrip = sender as FilmstripView;
            if (filmstrip != null && filmstrip.ThumbnailPaths != null && index >= 0 && index < filmstrip.ThumbnailPaths.Count)
            {
                string filePath = filmstrip.ThumbnailPaths[index];
                ImageEditorViewControl.LoadImage(filePath);
                
                // 检查是否为视频文件
                bool isVideo = NativeMethods.IsVideoFormat(filePath);
                if (isVideo)
                {
                    System.Diagnostics.Debug.WriteLine($"Loading video: {filePath}");
                }
                else
                {
                    System.Diagnostics.Debug.WriteLine($"Loading image: {filePath}");
                }
            }
        }

        // Panel Toggle Events
        private void PanelToggle_Checked(object sender, RoutedEventArgs e)
        {
            // 面板展开
        }

        private void PanelToggle_Unchecked(object sender, RoutedEventArgs e)
        {
            // 面板折叠
        }
    }
}

