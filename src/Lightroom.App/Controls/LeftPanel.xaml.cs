using System;
using System.IO;
using System.Windows;
using System.Windows.Controls;

namespace Lightroom.App.Controls
{
    public partial class LeftPanel : System.Windows.Controls.UserControl
    {
        public event EventHandler<string>? FolderSelected;

        private string? _selectedFolderPath = null;

        public LeftPanel()
        {
            InitializeComponent();
        }

        private void SelectFolderButton_Click(object sender, RoutedEventArgs e)
        {
            using (var dialog = new System.Windows.Forms.FolderBrowserDialog())
            {
                dialog.Description = "选择包含图片的文件夹";
                dialog.ShowNewFolderButton = false;
                
                // 如果之前选择过目录，默认打开该目录
                if (!string.IsNullOrEmpty(_selectedFolderPath) && Directory.Exists(_selectedFolderPath))
                {
                    dialog.SelectedPath = _selectedFolderPath;
                }

                if (dialog.ShowDialog() == System.Windows.Forms.DialogResult.OK)
                {
                    _selectedFolderPath = dialog.SelectedPath;
                    SelectedFolderPathText.Text = _selectedFolderPath;
                    SelectedFolderPathText.Visibility = Visibility.Visible;
                    
                    // 触发文件夹选择事件
                    FolderSelected?.Invoke(this, _selectedFolderPath);
                }
            }
        }

        public void UpdateImageCount(int count)
        {
            ImageCountText.Text = $"{count} 张照片";
        }

        public string? GetSelectedFolderPath()
        {
            return _selectedFolderPath;
        }
    }
}



