# FFmpeg 库目录

## 获取预编译的 FFmpeg 库

### 方法 1：从官方下载（推荐）

1. 访问 [FFmpeg 官方下载页面](https://ffmpeg.org/download.html)
2. 选择 Windows 版本（推荐使用 gyan.dev 的构建版本）
3. 下载 "ffmpeg-release-essentials.zip" 或类似版本
4. 解压到 `third_party/ffmpeg/` 目录

### 方法 2：使用 vcpkg

```powershell
vcpkg install ffmpeg:x64-windows
```

然后将库文件复制到 `third_party/ffmpeg/` 目录。

### 方法 3：从 GitHub Actions 构建

可以使用 GitHub Actions 自动构建 FFmpeg，参考：
- https://github.com/BtbN/FFmpeg-Builds

## 目录结构

解压后，目录结构应该是：

```
third_party/ffmpeg/
├── include/              # 头文件目录
│   ├── libavformat/
│   ├── libavcodec/
│   ├── libavutil/
│   └── libswscale/
├── lib/                  # 库文件目录
│   ├── avformat.lib
│   ├── avcodec.lib
│   ├── avutil.lib
│   └── swscale.lib
└── bin/                  # DLL 文件目录（可选，用于运行时）
    ├── avformat-XX.dll
    ├── avcodec-XX.dll
    ├── avutil-XX.dll
    └── swscale-XX.dll
```

## 需要的库

项目需要以下 FFmpeg 库：

- **libavformat**: 用于格式解析和文件 I/O
- **libavcodec**: 用于视频解码
- **libavutil**: 工具函数
- **libswscale**: 用于颜色空间转换

## 版本要求

- FFmpeg 4.0 或更高版本
- 支持 x64 架构
- Windows 平台

## 注意事项

1. 确保库文件架构为 x64
2. 确保库文件与项目配置（Debug/Release）匹配
3. 如果使用 DLL，确保 DLL 文件在运行时可用（已配置 PostBuildEvent 自动复制）
4. 项目已配置 `FFMPEG_AVAILABLE` 预处理器定义

## 验证安装

编译项目时，如果出现链接错误，请检查：

1. `include/` 目录是否存在且包含 FFmpeg 头文件
2. `lib/` 目录是否存在且包含 `.lib` 文件
3. 项目配置中的路径是否正确

