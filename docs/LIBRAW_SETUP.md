# LibRaw 库集成指南

## ✅ 源码已引入

LibRaw 源码已通过 git submodule 引入到 `third_party/libraw/` 目录。

## 快速开始

### 步骤 1: 验证源码

源码应该已经在 `third_party/libraw/` 目录中，包含：
- `libraw/libraw.h` - 主要头文件
- `internal/` - 内部头文件
- `src/` - 源文件
- `buildfiles/libraw.vcxproj` - Visual Studio 项目文件

### 步骤 2: 编译 LibRaw（Windows）

有两种方式：

#### 选项 A: 使用 Visual Studio 编译静态库

1. 打开 Visual Studio Developer Command Prompt
2. 进入 `third_party/libraw/` 目录
3. 创建构建目录：
   ```cmd
   mkdir build
   cd build
   ```
4. 运行 CMake：
   ```cmd
   cmake .. -G "Visual Studio 17 2022" -A x64
   ```
5. 编译：
   ```cmd
   cmake --build . --config Release
   ```
6. 编译后的库文件会在 `build/Release/` 或 `build/libraw/Release/` 目录中

#### 选项 B: 使用预编译的库（如果有）

如果有预编译的 Windows 库，直接复制到 `third_party/libraw/lib/` 目录

### 步骤 4: 项目配置

项目配置已经更新，包含：
- 包含目录：`$(ProjectDir)third_party\libraw`
- 预处理器定义：`LIBRAW_AVAILABLE`
- 库链接：`libraw.lib`（需要根据实际库文件名调整）

### 步骤 5: 验证集成

编译项目，如果成功，说明 LibRaw 已正确集成。

## 常见问题

### 问题 1: 找不到 libraw.h

**解决方案：**
- 确保 LibRaw 源代码已解压到 `third_party/libraw/` 目录
- 检查头文件路径：应该是 `third_party/libraw/libraw/libraw.h`
- 验证项目中的包含目录设置

### 问题 2: 链接错误

**解决方案：**
- 确保已编译 LibRaw 静态库（`libraw.lib`）
- 检查库文件路径是否正确
- 验证库文件架构（x64）是否匹配

### 问题 3: 运行时错误

**解决方案：**
- 如果使用动态库，确保 DLL 在输出目录中
- 检查文件路径编码（使用 UTF-8）

## 当前状态

- ✅ 项目配置已更新
- ✅ 代码已准备好集成
- ⏳ 等待 LibRaw 库下载和编译
- ⏳ 需要根据实际库文件名调整链接设置

## 下一步

1. 下载 LibRaw 源代码
2. 解压到 `third_party/libraw/` 目录
3. 编译静态库
4. 根据实际库文件名调整项目配置中的库链接
5. 编译项目测试

