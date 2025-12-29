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
                    // 对于视频文件，提取第一帧作为封面图
                    System.Threading.Tasks.Task.Run(() =>
                    {
                        try
                        {
                            // 提取视频第一帧（缩略图尺寸：200x200）
                            uint width = 0, height = 0;
                            int maxSize = 200 * 200 * 4; // 最大200x200，BGRA32格式
                            IntPtr pixelDataPtr = System.Runtime.InteropServices.Marshal.AllocHGlobal(maxSize);
                            
                            try
                            {
                                bool success = NativeMethods.ExtractVideoThumbnail(ImagePath, out width, out height, pixelDataPtr, 200, 200);
                                
                                if (success && width > 0 && height > 0)
                                {
                                    // 从非托管内存复制数据
                                    int dataSize = (int)(width * height * 4);
                                    byte[] pixelData = new byte[dataSize];
                                    System.Runtime.InteropServices.Marshal.Copy(pixelDataPtr, pixelData, 0, dataSize);
                                    
                                    // 创建BitmapImage
                                    var bitmap = new BitmapImage();
                                    bitmap.BeginInit();
                                    
                                    // 从像素数据创建BitmapSource
                                    var stride = width * 4; // BGRA32，每像素4字节
                                    var bitmapSource = System.Windows.Media.Imaging.BitmapSource.Create(
                                        (int)width,
                                        (int)height,
                                        96, 96, // DPI
                                        System.Windows.Media.PixelFormats.Bgra32,
                                        null,
                                        pixelData,
                                        (int)stride
                                    );
                                    
                                    // 将BitmapSource编码为PNG格式的MemoryStream
                                    var encoder = new System.Windows.Media.Imaging.PngBitmapEncoder();
                                    encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmapSource));
                                    
                                    using (var stream = new System.IO.MemoryStream())
                                    {
                                        encoder.Save(stream);
                                        stream.Position = 0;
                                        
                                        bitmap.StreamSource = stream;
                                        bitmap.CacheOption = BitmapCacheOption.OnLoad;
                                        bitmap.EndInit();
                                        bitmap.Freeze();
                                        
                                        // 回到UI线程更新
                                        System.Windows.Application.Current.Dispatcher.Invoke(() =>
                                        {
                                            Thumbnail = bitmap;
                                            IsLoading = false;
                                        });
                                    }
                                }
                                else
                                {
                                    // 如果提取失败，使用占位符
                                    System.Windows.Application.Current.Dispatcher.Invoke(() =>
                                    {
                                        var placeholder = CreateVideoPlaceholder();
                                        Thumbnail = placeholder;
                                        IsLoading = false;
                                    });
                                }
                            }
                            finally
                            {
                                // 释放非托管内存
                                System.Runtime.InteropServices.Marshal.FreeHGlobal(pixelDataPtr);
                            }
                        }
                        catch
                        {
                            // 如果出错，使用占位符
                            System.Windows.Application.Current.Dispatcher.Invoke(() =>
                            {
                                var placeholder = CreateVideoPlaceholder();
                                Thumbnail = placeholder;
                                IsLoading = false;
                            });
                        }
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

