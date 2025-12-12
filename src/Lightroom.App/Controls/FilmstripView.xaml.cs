using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace Lightroom.App.Controls
{
    public partial class FilmstripView : System.Windows.Controls.UserControl
    {
        public event EventHandler<int>? ThumbnailSelected;
        
        public List<string>? ThumbnailPaths { get; private set; }
        private List<ThumbnailItem>? _thumbnailItems;

        // 支持的图片格式
        private static readonly string[] SupportedExtensions = { 
            ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif", ".gif",  // 标准格式
            ".raw", ".cr2", ".cr3", ".nef", ".nrw", ".arw", ".srf",    // Canon, Nikon, Sony
            ".dng", ".orf", ".raf", ".rw2", ".pef", ".ptx",            // Adobe, Olympus, Fujifilm, Panasonic, Pentax
            ".x3f", ".3fr", ".fff", ".mef", ".mos"                     // Sigma, Hasselblad, Mamiya, Leaf
        };

        public FilmstripView()
        {
            InitializeComponent();
        }

        /// <summary>
        /// 从目录加载所有图片
        /// </summary>
        public void LoadImagesFromFolder(string folderPath)
        {
            if (string.IsNullOrEmpty(folderPath) || !Directory.Exists(folderPath))
            {
                ThumbnailsContainer.ItemsSource = null;
                ThumbnailPaths = null;
                _thumbnailItems = null;
                return;
            }

            try
            {
                // 获取目录下所有支持的图片文件
                var imageFiles = Directory.GetFiles(folderPath, "*.*", SearchOption.TopDirectoryOnly)
                    .Where(file => SupportedExtensions.Contains(Path.GetExtension(file).ToLowerInvariant()))
                    .OrderBy(file => file)
                    .ToList();

                ThumbnailPaths = imageFiles;
                
                // 创建缩略图项
                _thumbnailItems = imageFiles.Select(path => new ThumbnailItem(path)).ToList();
                ThumbnailsContainer.ItemsSource = _thumbnailItems;
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[FilmstripView] Error loading images from folder: {ex.Message}");
                ThumbnailsContainer.ItemsSource = null;
                ThumbnailPaths = null;
                _thumbnailItems = null;
            }
        }

        /// <summary>
        /// 加载图片路径列表（兼容旧接口）
        /// </summary>
        public void LoadThumbnails(List<string> imagePaths)
        {
            ThumbnailPaths = imagePaths;
            
            // 创建缩略图项
            _thumbnailItems = imagePaths.Select(path => new ThumbnailItem(path)).ToList();
            ThumbnailsContainer.ItemsSource = _thumbnailItems;
        }

        private void Thumbnail_Click(object sender, MouseButtonEventArgs e)
        {
            if (sender is Border border && border.DataContext is ThumbnailItem item)
            {
                var index = ThumbnailsContainer.Items.IndexOf(item);
                if (index >= 0 && ThumbnailPaths != null && index < ThumbnailPaths.Count)
                {
                    ThumbnailSelected?.Invoke(this, index);
                }
            }
        }

        private void Thumbnail_MouseEnter(object sender, System.Windows.Input.MouseEventArgs e)
        {
            if (sender is Border border)
            {
                border.BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0, 120, 212)); // #0078D4
                border.BorderThickness = new Thickness(2);
            }
        }

        private void Thumbnail_MouseLeave(object sender, System.Windows.Input.MouseEventArgs e)
        {
            if (sender is Border border)
            {
                border.BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(63, 63, 70)); // #3F3F46
                border.BorderThickness = new Thickness(1);
            }
        }
    }
}

