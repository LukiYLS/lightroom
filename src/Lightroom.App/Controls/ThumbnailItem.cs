using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows.Media.Imaging;

namespace Lightroom.App.Controls
{
    public class ThumbnailItem : INotifyPropertyChanged
    {
        private BitmapImage? _thumbnail;
        private bool _isLoading = true;
        private bool _hasError = false;

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

                // 在后台线程加载缩略图
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
            catch
            {
                HasError = true;
                IsLoading = false;
            }
        }
    }
}

