using System;
using System.Windows;
using System.Windows.Controls;

namespace Lightroom.App.Controls
{
    public partial class RightPanel : UserControl
    {
        public event EventHandler<(string parameter, double value)>? AdjustmentChanged;

        public RightPanel()
        {
            InitializeComponent();
        }

        private void Slider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (!IsLoaded) return;

            var slider = sender as Slider;
            if (slider == null) return;

            string parameterName = slider.Name.Replace("Slider", "");
            double value = e.NewValue;

            // 更新显示值
            UpdateValueText(parameterName, value);

            // 触发调整事件
            AdjustmentChanged?.Invoke(this, (parameterName, value));
        }

        private void UpdateValueText(string parameterName, double value)
        {
            string valueText = parameterName switch
            {
                "Exposure" => $"{value:F2}",
                _ => $"{value:F0}"
            };

            switch (parameterName)
            {
                case "Exposure":
                    ExposureValue.Text = valueText;
                    break;
                case "Contrast":
                    ContrastValue.Text = valueText;
                    break;
                case "Highlights":
                    HighlightsValue.Text = valueText;
                    break;
                case "Shadows":
                    ShadowsValue.Text = valueText;
                    break;
                case "Whites":
                    WhitesValue.Text = valueText;
                    break;
                case "Blacks":
                    BlacksValue.Text = valueText;
                    break;
            }
        }

        public void ResetAll()
        {
            ExposureSlider.Value = 0;
            ContrastSlider.Value = 0;
            HighlightsSlider.Value = 0;
            ShadowsSlider.Value = 0;
            WhitesSlider.Value = 0;
            BlacksSlider.Value = 0;
        }
    }
}



