using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;

namespace Lightroom.App.Controls
{
    public partial class FilmstripView : UserControl
    {
        public event EventHandler<int>? ThumbnailSelected;
        
        public List<string>? ThumbnailPaths { get; private set; }

        public FilmstripView()
        {
            InitializeComponent();
        }

        public void LoadThumbnails(List<string> imagePaths)
        {
            ThumbnailPaths = imagePaths;
            ThumbnailsContainer.ItemsSource = imagePaths;
        }

        private void Thumbnail_Click(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (sender is Border border && border.DataContext is string imagePath)
            {
                var index = ThumbnailsContainer.Items.IndexOf(imagePath);
                ThumbnailSelected?.Invoke(this, index);
            }
        }
    }
}

