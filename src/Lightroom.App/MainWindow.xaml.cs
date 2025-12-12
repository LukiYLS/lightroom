using System;
using System.Collections.Generic;
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
            // 初始化示例数据
            FilmstripViewControl.LoadThumbnails(new List<string> 
            { 
                "Image 1", "Image 2", "Image 3", "Image 4", "Image 5" 
            });
            LeftPanelControl.UpdateImageCount(5);
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
                Filter = "图片文件|*.jpg;*.jpeg;*.png;*.tiff;*.raw|所有文件|*.*",
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
            var dialog = new Microsoft.Win32.SaveFileDialog
            {
                Filter = "JPEG文件|*.jpg|PNG文件|*.png|TIFF文件|*.tiff|所有文件|*.*"
            };

            if (dialog.ShowDialog() == true)
            {
                // TODO: 实现导出功能
                System.Windows.MessageBox.Show($"导出到: {dialog.FileName}", "导出", MessageBoxButton.OK, MessageBoxImage.Information);
            }
        }

        private void TopMenuBar_LibraryModeRequested(object? sender, EventArgs e)
        {
            // 切换到 Library 模式
            TopMenuBarControl.LibraryButton.Background = System.Windows.Media.Brushes.DarkBlue;
            TopMenuBarControl.DevelopButton.Background = System.Windows.Media.Brushes.Transparent;
        }

        private void TopMenuBar_DevelopModeRequested(object? sender, EventArgs e)
        {
            // 切换到 Develop 模式
            TopMenuBarControl.LibraryButton.Background = System.Windows.Media.Brushes.Transparent;
            TopMenuBarControl.DevelopButton.Background = System.Windows.Media.Brushes.DarkBlue;
        }

        // LeftPanel Events
        private void LeftPanel_FolderSelected(object? sender, string folderPath)
        {
            if (string.IsNullOrEmpty(folderPath))
            {
                return;
            }

            System.Diagnostics.Debug.WriteLine($"Folder selected: {folderPath}");
            
            // 加载文件夹中的图片到缩略图列表
            FilmstripViewControl.LoadImagesFromFolder(folderPath);
            
            // 更新图片数量
            if (FilmstripViewControl.ThumbnailPaths != null)
            {
                LeftPanelControl.UpdateImageCount(FilmstripViewControl.ThumbnailPaths.Count);
                
                // 如果有图片，加载第一张
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
            
            // TODO: 将调整参数传递给 C++ SDK 进行渲染
            var renderHandle = ImageEditorViewControl.GetRenderTargetHandle();
            if (renderHandle != IntPtr.Zero)
            {
                // 这里可以根据调整参数更新渲染
                // 例如：根据曝光度、对比度等调整渲染
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
            // 加载选中的图片
            var filmstrip = sender as FilmstripView;
            if (filmstrip != null && filmstrip.ThumbnailPaths != null && index >= 0 && index < filmstrip.ThumbnailPaths.Count)
            {
                string imagePath = filmstrip.ThumbnailPaths[index];
                ImageEditorViewControl.LoadImage(imagePath);
                System.Diagnostics.Debug.WriteLine($"Loading image: {imagePath}");
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

