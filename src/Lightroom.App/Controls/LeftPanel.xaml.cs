using System.Windows;
using System.Windows.Controls;

namespace Lightroom.App.Controls
{
    public partial class LeftPanel : UserControl
    {
        public event EventHandler<string>? FolderSelected;
        public event EventHandler<string>? CollectionSelected;

        public LeftPanel()
        {
            InitializeComponent();
        }

        private void CollectionsList_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (CollectionsList.SelectedItem is ListBoxItem item)
            {
                CollectionSelected?.Invoke(this, item.Content?.ToString() ?? "");
            }
        }

        public void UpdateImageCount(int count)
        {
            ImageCountText.Text = $"{count} 张照片";
        }
    }
}



