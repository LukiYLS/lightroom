# Lightroom Clone Framework

这是一个类似 Lightroom 的软件架构原型，包含 WPF 前端和 C++ 底层 SDK。

## 项目结构

- **src/Lightroom.App**: WPF UI 项目 (C#)
- **src/Lightroom.Core**: 底层图像处理 SDK (C++)
- **LightroomClone.sln**: 解决方案文件

## 如何构建

### 1. 准备工作
确保安装了 Visual Studio 2022，并勾选了以下工作负载：
- .NET 桌面开发
- 使用 C++ 的桌面开发

### 2. 配置 C++ 项目
由于 `dotnet` 命令行工具无法自动处理 C++ 项目文件 (.vcxproj) 到解决方案的链接，你需要手动添加一次（如果解决方案中没有显示 C++ 项目）。

1. 打开 `LightroomClone.sln`。
2. 在“解决方案资源管理器”中，右键点击“解决方案 'LightroomClone'”。
3. 选择 **添加 -> 现有项目**。
4. 浏览并选择 `src\Lightroom.Core\Lightroom.Core.vcxproj`。
5. 右键点击 `Lightroom.App` 项目，选择 **生成依赖项 -> 项目依赖项**，勾选 `LightroomCore`。这将确保构建顺序正确。

### 3. 运行
1. 选择 `Debug` 或 `Release` 配置，以及 `x64` 平台（C++ SDK 默认为 x64）。
2. 点击启动。

## 架构说明

- **UI 层 (WPF)**: 处理用户交互、参数调整。
- **Core 层 (C++)**: 导出 C 接口 (extern "C") 供 C# 通过 P/Invoke 调用。实际的 GPU 渲染逻辑应在此层实现（如使用 CUDA, OpenCL, DirectX 或 Vulkan）。
- **交互**: `NativeMethods.cs` 封装了对 `LightroomCore.dll` 的调用。

## 扩展建议

1. **GPU 渲染**: 在 C++ 项目中集成图形 API。
2. **互操作**: 对于高性能预览，可以使用 D3DImage (WPF) 与 DirectX 共享表面，避免 CPU/GPU 内存拷贝。
3. **数据传递**: 对于大图处理，避免传递文件路径，而是传递内存指针或共享句柄。


