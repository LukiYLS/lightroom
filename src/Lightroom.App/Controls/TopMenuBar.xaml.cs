using System.Windows;
using System.Windows.Controls;

namespace Lightroom.App.Controls
{
    public partial class TopMenuBar : UserControl
    {
        public event EventHandler? ImportPhotosRequested;
        public event EventHandler? ExportRequested;
        public event EventHandler? LibraryModeRequested;
        public event EventHandler? DevelopModeRequested;

        public TopMenuBar()
        {
            InitializeComponent();
        }

        private void ImportPhotos_Click(object sender, RoutedEventArgs e)
        {
            ImportPhotosRequested?.Invoke(this, EventArgs.Empty);
        }

        private void Export_Click(object sender, RoutedEventArgs e)
        {
            ExportRequested?.Invoke(this, EventArgs.Empty);
        }

        private void Exit_Click(object sender, RoutedEventArgs e)
        {
            Application.Current.Shutdown();
        }

        private void Preferences_Click(object sender, RoutedEventArgs e)
        {
            MessageBox.Show("首选项功能待实现", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private void FullScreen_Click(object sender, RoutedEventArgs e)
        {
            var window = Window.GetWindow(this);
            if (window != null)
            {
                window.WindowState = window.WindowState == WindowState.Maximized 
                    ? WindowState.Normal 
                    : WindowState.Maximized;
            }
        }

        private void SecondDisplay_Click(object sender, RoutedEventArgs e)
        {
            MessageBox.Show("第二显示器功能待实现", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private void About_Click(object sender, RoutedEventArgs e)
        {
            MessageBox.Show("Lightroom Clone v1.0.0\n基于 WPF 和 D3D11 的图片编辑软件", 
                "关于", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private void LibraryMode_Click(object sender, RoutedEventArgs e)
        {
            LibraryModeRequested?.Invoke(this, EventArgs.Empty);
        }

        private void DevelopMode_Click(object sender, RoutedEventArgs e)
        {
            DevelopModeRequested?.Invoke(this, EventArgs.Empty);
        }
    }
}



