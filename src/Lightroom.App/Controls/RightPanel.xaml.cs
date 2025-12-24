using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Threading;
using System.Runtime.InteropServices;
using Lightroom.App.Core;

namespace Lightroom.App.Controls
{
    public partial class RightPanel : System.Windows.Controls.UserControl
    {
        public event EventHandler<(string parameter, double value)>? AdjustmentChanged;
        public event EventHandler<string>? FilterSelected;
        public event EventHandler<double>? FilterIntensityChanged;

        private Dictionary<string, string> _filterPaths = new Dictionary<string, string>();
        
        // 视频导出相关
        private IntPtr _renderTargetHandle = IntPtr.Zero;
        private NativeMethods.VideoExportProgressDelegate? _progressCallback;

        public RightPanel()
        {
            InitializeComponent();
            Loaded += RightPanel_Loaded;
        }

        private void RightPanel_Loaded(object sender, RoutedEventArgs e)
        {
            LoadFilters();
        }

        private void LoadFilters()
        {
            try
            {
                // 扫描 resources/luts 目录
                // 尝试多个可能的路径
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string currentDir = Directory.GetCurrentDirectory();
                string[] possiblePaths = new string[]
                {
                    // 输出目录下的 resources/luts（构建后复制的位置）- 最优先
                    Path.Combine(baseDir, "resources", "luts"),
                    // 项目根目录下的 resources/luts（开发时）
                    Path.Combine(baseDir, "..", "..", "..", "resources", "luts"),
                    Path.Combine(baseDir, "..", "..", "resources", "luts"),
                    // 当前工作目录
                    Path.Combine(currentDir, "resources", "luts"),
                    Path.Combine(currentDir, "..", "..", "..", "resources", "luts"),
                    Path.Combine(currentDir, "..", "..", "resources", "luts")
                };

                string? lutsDirectory = null;
                foreach (var path in possiblePaths)
                {
                    string fullPath = Path.GetFullPath(path);
                    System.Diagnostics.Debug.WriteLine($"[FilterLoader] Checking path: {fullPath}");
                    if (Directory.Exists(fullPath))
                    {
                        lutsDirectory = fullPath;
                        System.Diagnostics.Debug.WriteLine($"[FilterLoader] Found LUTs directory: {fullPath}");
                        break;
                    }
                }

                if (!string.IsNullOrEmpty(lutsDirectory))
                {
                    var filterFiles = Directory.GetFiles(lutsDirectory, "*.cube")
                        .OrderBy(f => Path.GetFileName(f))
                        .ToList();

                    _filterPaths.Clear();
                    var filterNames = new List<string> { "无" }; // 第一个选项是"无"

                    foreach (var filePath in filterFiles)
                    {
                        string fileName = Path.GetFileNameWithoutExtension(filePath);
                        _filterPaths[fileName] = filePath;
                        filterNames.Add(fileName);
                    }

                    FilterComboBox.ItemsSource = filterNames;
                    FilterComboBox.SelectedIndex = 0; // 默认选择"无"
                }
                else
                {
                    FilterComboBox.ItemsSource = new List<string> { "无", "未找到滤镜文件" };
                    FilterComboBox.SelectedIndex = 0;
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Error loading filters: {ex.Message}");
                FilterComboBox.ItemsSource = new List<string> { "无", "加载滤镜失败" };
                FilterComboBox.SelectedIndex = 0;
            }
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
                "Temperature" => $"{value:F0}",
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
                case "Temperature":
                    TemperatureValue.Text = valueText;
                    break;
                case "Tint":
                    TintValue.Text = valueText;
                    break;
                case "Vibrance":
                    VibranceValue.Text = valueText;
                    break;
                case "Saturation":
                    SaturationValue.Text = valueText;
                    break;
                case "Sharpness":
                    SharpnessValue.Text = valueText;
                    break;
                case "NoiseReduction":
                    NoiseReductionValue.Text = valueText;
                    break;
                case "Vignette":
                    VignetteValue.Text = valueText;
                    break;
                case "Grain":
                    GrainValue.Text = valueText;
                    break;
            }
        }

        private void FilterComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (!IsLoaded || FilterComboBox.SelectedItem == null) return;

            string selectedFilter = FilterComboBox.SelectedItem.ToString() ?? "无";
            FilterSelected?.Invoke(this, selectedFilter);
        }

        private void FilterIntensitySlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (!IsLoaded) return;

            double intensity = e.NewValue / 100.0; // 转换为 0.0 - 1.0
            FilterIntensityValue.Text = $"{(int)e.NewValue}";
            FilterIntensityChanged?.Invoke(this, intensity);
        }

        public string? GetSelectedFilterPath()
        {
            if (FilterComboBox.SelectedItem == null) return null;
            
            string selectedFilter = FilterComboBox.SelectedItem.ToString() ?? "无";
            if (selectedFilter == "无" || !_filterPaths.ContainsKey(selectedFilter))
            {
                return null;
            }

            return _filterPaths[selectedFilter];
        }

        public double GetFilterIntensity()
        {
            return FilterIntensitySlider.Value / 100.0;
        }

        public void ResetAll()
        {
            ExposureSlider.Value = 0;
            ContrastSlider.Value = 0;
            HighlightsSlider.Value = 0;
            ShadowsSlider.Value = 0;
            WhitesSlider.Value = 0;
            BlacksSlider.Value = 0;
            TemperatureSlider.Value = 5500;
            TintSlider.Value = 0;
            VibranceSlider.Value = 0;
            SaturationSlider.Value = 0;
            SharpnessSlider.Value = 0;
            NoiseReductionSlider.Value = 0;
            VignetteSlider.Value = 0;
            GrainSlider.Value = 0;
            FilterComboBox.SelectedIndex = 0;
            FilterIntensitySlider.Value = 100;
        }

        private void ResetButton_Click(object sender, RoutedEventArgs e)
        {
            ResetAll();
        }
        
        // 设置渲染目标句柄（用于视频导出）
        public void SetRenderTargetHandle(IntPtr renderTargetHandle)
        {
            _renderTargetHandle = renderTargetHandle;
        }
        
        // 开始导出视频
        public bool StartVideoExport(string outputPath)
        {
            if (_renderTargetHandle == IntPtr.Zero)
            {
                return false;
            }
            
            // 检查是否正在导出
            if (NativeMethods.IsExportingVideo(_renderTargetHandle))
            {
                return false;
            }
            
            // 创建进度回调
            _progressCallback = (progress, currentFrame, totalFrames, userData) =>
            {
                // 在UI线程上更新进度
                Dispatcher.Invoke(() =>
                {
                    UpdateExportProgress(progress, currentFrame, totalFrames);
                });
            };
            
            // 显示进度面板
            ExportProgressPanel.Visibility = Visibility.Visible;
            ExportProgressBar.Value = 0;
            ExportProgressText.Text = "准备中...";
            
            // 开始导出
            bool success = NativeMethods.ExportVideo(_renderTargetHandle, outputPath, _progressCallback, IntPtr.Zero);
            
            if (!success)
            {
                ExportProgressPanel.Visibility = Visibility.Collapsed;
            }
            
            return success;
        }
        
        // 更新导出进度
        private void UpdateExportProgress(double progress, long currentFrame, long totalFrames)
        {
            ExportProgressBar.Value = progress * 100.0;
            ExportProgressText.Text = $"导出中: {currentFrame} / {totalFrames} 帧 ({progress * 100.0:F1}%)";
            
            // 如果完成，隐藏进度面板
            if (progress >= 1.0)
            {
                ExportProgressText.Text = "导出完成";
                // 延迟隐藏，让用户看到完成消息
                var timer = new DispatcherTimer
                {
                    Interval = TimeSpan.FromSeconds(2)
                };
                timer.Tick += (s, e) =>
                {
                    timer.Stop();
                    ExportProgressPanel.Visibility = Visibility.Collapsed;
                };
                timer.Start();
            }
        }
        
        // 取消导出
        private void CancelExportButton_Click(object sender, RoutedEventArgs e)
        {
            if (_renderTargetHandle != IntPtr.Zero)
            {
                NativeMethods.CancelVideoExport(_renderTargetHandle);
                ExportProgressPanel.Visibility = Visibility.Collapsed;
            }
        }
    }
    
    // 进度转换器（用于ProgressBar）
    public class ProgressConverter : IMultiValueConverter
    {
        public object Convert(object[] values, Type targetType, object parameter, System.Globalization.CultureInfo culture)
        {
            if (values.Length == 3 && values[0] is double value && values[1] is double maximum && values[2] is double width)
            {
                if (maximum > 0)
                {
                    return (value / maximum) * width;
                }
            }
            return 0.0;
        }
        
        public object[] ConvertBack(object value, Type[] targetTypes, object parameter, System.Globalization.CultureInfo culture)
        {
            throw new NotImplementedException();
        }
    }
}
