# LibRaw 集成下一步

## ✅ 已完成

1. ✅ LibRaw 源码已通过 git submodule 引入
2. ✅ 项目配置已更新（包含目录、预处理器定义、库链接）
3. ✅ 代码已准备好使用 LibRaw

## 📋 下一步：编译 LibRaw

### 方式 1: 使用提供的编译脚本（推荐）

1. **运行编译脚本**
   ```powershell
   cd third_party\libraw
   .\build_libraw.ps1
   ```

2. **脚本会自动**
   - 查找 MSBuild
   - 编译 Release 和 Debug 版本
   - 将库文件复制到 `lib/` 目录

### 方式 2: 手动编译（Visual Studio）

1. **打开 LibRaw 解决方案**
   - 打开 `third_party/libraw/LibRaw.sln`
   - 或打开 `third_party/libraw/buildfiles/libraw.vcxproj`

2. **修改配置为静态库**
   - 右键 `libraw` 项目 → 属性
   - 配置属性 → 常规
     - 配置类型：**静态库 (.lib)**
   - 平台工具集：Visual Studio 2022 (v143)

3. **编译**
   - 选择 `x64 Release` 或 `Debug`
   - 生成 → 生成解决方案

4. **复制库文件**
   - 找到编译后的 `libraw.lib`（通常在 `release-x86_64/` 或 `debug-x86_64/`）
   - 复制到 `third_party/libraw/lib/` 目录

### 方式 3: 使用 CMake

1. **打开 CMake GUI**
   - 源代码目录：`third_party/libraw`
   - 构建目录：`third_party/libraw/build`

2. **配置**
   - 点击 Configure
   - 选择 Visual Studio 2022 和 x64
   - 设置 `BUILD_SHARED_LIBS` 为 `OFF`（静态库）

3. **生成和编译**
   - 点击 Generate
   - 点击 Open Project
   - 在 Visual Studio 中编译

4. **复制库文件**
   - 将 `build/Release/libraw.lib` 复制到 `third_party/libraw/lib/`

## ⚠️ 注意事项

### LibRaw 项目默认是动态库

LibRaw 的 Visual Studio 项目默认编译为**动态库 (DLL)**，我们需要**静态库**。

**解决方案：**
- 在项目属性中修改配置类型为"静态库 (.lib)"
- 或者使用编译脚本（会自动处理）

### 库文件命名

- Release: `libraw.lib`
- Debug: `librawd.lib` 或 `libraw.lib`（取决于配置）

项目配置中链接的是 `libraw.lib`，如果 Debug 版本名称不同，可能需要调整。

## ✅ 验证编译

编译成功后，检查：

1. **库文件存在**
   ```powershell
   Test-Path third_party\libraw\lib\libraw.lib
   ```

2. **编译主项目**
   - 打开 `LightroomClone.sln`
   - 编译 `Lightroom.Core` 项目
   - 应该能成功链接 `libraw.lib`

3. **测试代码**
   - 代码中 `#include <libraw/libraw.h>` 应该能正常编译
   - `LIBRAW_AVAILABLE` 已定义，`LibRawWrapper` 应该能正常工作

## 🐛 常见问题

### 问题 1: 找不到 MSBuild

**解决方案：**
- 确保已安装 Visual Studio 2022
- 或使用 Visual Studio Developer Command Prompt
- 或手动指定 MSBuild 路径

### 问题 2: 链接错误

**解决方案：**
- 确保库文件架构为 x64
- 确保库文件与项目配置（Debug/Release）匹配
- 检查库文件路径是否正确

### 问题 3: 头文件找不到

**解决方案：**
- 验证包含目录：`$(ProjectDir)..\..\third_party\libraw`
- 检查 `libraw/libraw.h` 文件是否存在

## 📝 编译后的文件结构

```
third_party/libraw/
├── lib/
│   ├── libraw.lib      (Release)
│   └── librawd.lib     (Debug, 可选)
├── release-x86_64/     (编译输出)
└── debug-x86_64/       (编译输出)
```

## 🎯 完成后的下一步

编译 LibRaw 成功后：

1. ✅ 编译主项目验证链接
2. ✅ 测试 RAW 文件加载功能
3. ✅ 验证 `LibRawWrapper` 正常工作
4. ✅ 测试 RAW 元数据提取



