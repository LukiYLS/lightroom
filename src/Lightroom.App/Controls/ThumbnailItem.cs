using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows.Media.Imaging;
using Lightroom.App.Core;

namespace Lightroom.App.Controls
{
    public class ThumbnailItem : INotifyPropertyChanged
    {
        private BitmapImage? _thumbnail;
        private bool _isLoading = true;
        private bool _hasError = false;
        private bool _isVideo = false;

        public string ImagePath { get; set; }
        
        public BitmapImage? Thumbnail
        {
            get => _thumbnail;
            set
            {
                _thumbnail = value;
                OnPropertyChanged();
            }
        }
        
        public bool IsLoading
        {
            get => _isLoading;
            set
            {
                _isLoading = value;
                OnPropertyChanged();
            }
        }
        
        public bool HasError
        {
            get => _hasError;
            set
            {
                _hasError = value;
                OnPropertyChanged();
            }
        }

        public bool IsVideo
        {
            get => _isVideo;
            set
            {
                _isVideo = value;
                OnPropertyChanged();
            }
        }
        
        public string FileName => Path.GetFileName(ImagePath);

        public event PropertyChangedEventHandler? PropertyChanged;

        protected virtual void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        public ThumbnailItem(string imagePath)
        {
            ImagePath = imagePath;
            LoadThumbnail();
        }

        private void LoadThumbnail()
        {
            try
            {
                if (!File.Exists(ImagePath))
                {
                    HasError = true;
                    IsLoading = false;
                    return;
                }

                // 检查是否为视频文件
                bool isVideoFile = NativeMethods.IsVideoFormat(ImagePath);
                IsVideo = isVideoFile;

                if (isVideoFile)
                {
                    // 对于视频文件，显示视频图标或占位符
                    // 暂时使用一个简单的占位符，后续可以提取视频第一帧
                    System.Windows.Application.Current.Dispatcher.Invoke(() =>
                    {
                        // 创建一个简单的视频图标占位符
                        var placeholder = CreateVideoPlaceholder();
                        Thumbnail = placeholder;
                        IsLoading = false;
                    });
                }
                else
                {
                    // 在后台线程加载图片缩略图
                    System.Threading.Tasks.Task.Run(() =>
                    {
                        try
                        {
                            var bitmap = new BitmapImage();
                            bitmap.BeginInit();
                            bitmap.UriSource = new Uri(ImagePath, UriKind.Absolute);
                            bitmap.DecodePixelWidth = 200; // 限制缩略图大小以提高性能
                            bitmap.CacheOption = BitmapCacheOption.OnLoad;
                            bitmap.EndInit();
                            bitmap.Freeze(); // 使图片可以在不同线程使用

                            // 回到UI线程更新
                            System.Windows.Application.Current.Dispatcher.Invoke(() =>
                            {
                                Thumbnail = bitmap;
                                IsLoading = false;
                            });
                        }
                        catch
                        {
                            System.Windows.Application.Current.Dispatcher.Invoke(() =>
                            {
                                HasError = true;
                                IsLoading = false;
                            });
                        }
                    });
                }
            }
            catch
            {
                HasError = true;
                IsLoading = false;
            }
        }

        private BitmapImage CreateVideoPlaceholder()
        {
            // 创建一个简单的视频图标占位符
            // 使用 RenderTargetBitmap 创建一个带播放图标的占位符
            var drawingVisual = new System.Windows.Media.DrawingVisual();
            using (var drawingContext = drawingVisual.RenderOpen())
            {
                // 绘制一个深色背景
                drawingContext.DrawRectangle(
                    new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(40, 40, 40)),
                    null,
                    new System.Windows.Rect(0, 0, 200, 200));
                
                // 绘制一个播放图标（三角形）
                var geometry = new System.Windows.Media.PathGeometry();
                var figure = new System.Windows.Media.PathFigure();
                figure.StartPoint = new System.Windows.Point(60, 50);
                figure.Segments.Add(new System.Windows.Media.LineSegment(new System.Windows.Point(60, 150), true));
                figure.Segments.Add(new System.Windows.Media.LineSegment(new System.Windows.Point(140, 100), true));
                figure.IsClosed = true;
                geometry.Figures.Add(figure);
                
                drawingContext.DrawGeometry(
                    new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(150, 150, 150)),
                    null,
                    geometry);
            }

            var rtb = new System.Windows.Media.Imaging.RenderTargetBitmap(200, 200, 96, 96, System.Windows.Media.PixelFormats.Pbgra32);
            rtb.Render(drawingVisual);
            rtb.Freeze();

            // 将 RenderTargetBitmap 转换为 BitmapImage
            var encoder = new System.Windows.Media.Imaging.PngBitmapEncoder();
            encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(rtb));
            using (var stream = new System.IO.MemoryStream())
            {
                encoder.Save(stream);
                stream.Position = 0;
                var bitmap = new BitmapImage();
                bitmap.BeginInit();
                bitmap.StreamSource = stream;
                bitmap.CacheOption = BitmapCacheOption.OnLoad;
                bitmap.EndInit();
                bitmap.Freeze();
                return bitmap;
            }
        }
    }
}

